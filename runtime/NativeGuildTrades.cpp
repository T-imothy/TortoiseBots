#include "NativeGuildTrades.h"
#include "BotManager.h"
#include "BotWorldActions.h"
#include "PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Event.h"
#include "Player.h"
#include "Group.h"
#include "Item.h"
#include "Bag.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <optional>

namespace TortoiseBots
{
namespace
{
struct GuildOffer
{
    ai::Event sender;
    ai::Event receiver;
    MapWorkStamp senderMap;
    MapWorkStamp receiverMap;
    ObjectGuid senderMaster;
    ObjectGuid receiverMaster;
    ObjectGuid itemGuid;
    uint32 relationshipId;
    bool party;
    uint32 itemId;
    uint32 count;
    std::chrono::steady_clock::time_point readyAt;
};
// Native trades already admit only one peer per player. Bound total retained
// offers as well; this service runs solely inside the post-map update guard.
std::deque<GuildOffer> offers;
constexpr size_t MaxOffers = 64;

uint32 RelationshipId(Player* sender, Player* receiver, bool party)
{
    if (party)
    {
        Group* group = sender->GetGroup();
        return group && group == receiver->GetGroup() ? group->GetId() : 0;
    }
    return sender->GetGuildId() == receiver->GetGuildId() ? sender->GetGuildId() : 0;
}

bool EligiblePair(Player* sender, Player* receiver, bool party)
{
    return sender && receiver && sender != receiver &&
        BotManager::Instance().IsControllableBot(sender) &&
        BotManager::Instance().IsControllableBot(receiver) &&
        PlayerbotAIStorage::Instance().GetAI(sender) && PlayerbotAIStorage::Instance().GetAI(receiver) &&
        PlayerbotAI::IsSafe(sender, receiver) && sender->IsAlive() && receiver->IsAlive() &&
        !sender->IsInCombat() && !receiver->IsInCombat() &&
        !sender->IsTaxiFlying() && !receiver->IsTaxiFlying() &&
        RelationshipId(sender, receiver, party) != 0;
}

bool OwnOffer(GuildOffer const& offer, Player* sender, Player* receiver)
{
    if (!sender || !receiver || sender->GetTrader() != receiver || receiver->GetTrader() != sender)
        return false;
    TradeData* from = sender->GetTradeData();
    TradeData* to = receiver->GetTradeData();
    if (!from || !to || from->GetMoney() || to->GetMoney() || from->GetSpell() || to->GetSpell())
        return false;
    Item* item = from->GetItem(TradeSlots(0));
    if (!item || item->GetObjectGuid() != offer.itemGuid || item->GetEntry() != offer.itemId ||
        item->GetCount() != offer.count)
        return false;
    for (uint8 slot = 0; slot < TRADE_SLOT_COUNT; ++slot)
        if (to->GetItem(TradeSlots(slot)) || (slot && from->GetItem(TradeSlots(slot))))
            return false;
    return true;
}

bool CurrentOffer(GuildOffer const& offer, Player* sender, Player* receiver)
{
    if (!EligiblePair(sender, receiver, offer.party) || RelationshipId(sender, receiver, offer.party) != offer.relationshipId ||
        !offer.senderMap.Matches(sender->GetMapId(), sender->GetInstanceId(), sender->GetMapWorkGeneration(), sender->IsInWorld()) ||
        !offer.receiverMap.Matches(receiver->GetMapId(), receiver->GetInstanceId(), receiver->GetMapWorkGeneration(), receiver->IsInWorld()))
        return false;
    auto* from = BotManager::Instance().FindBot(sender->GetObjectGuid());
    auto* to = BotManager::Instance().FindBot(receiver->GetObjectGuid());
    return from && to && from->masterGuid == offer.senderMaster && to->masterGuid == offer.receiverMaster;
}

// Select an empty native inventory position; never split into an existing
// stack or reconstruct an item with CreateItem (which loses native metadata).
std::optional<uint16> SplitPosition(Player* player, Item* item, uint32 count)
{
    auto usable = [&](uint8 bag, uint8 slot)
    {
        if (player->GetItemByPos(bag, slot)) return false;
        ItemPosCountVec dest;
        return player->CanStoreNewItem(bag, slot, dest, item->GetEntry(), count) == EQUIP_ERR_OK;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (usable(INVENTORY_SLOT_BAG_0, slot)) return uint16(INVENTORY_SLOT_BAG_0 << 8) | slot;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = static_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                if (usable(bag, uint8(slot))) return uint16(bag << 8) | uint8(slot);
    return std::nullopt;
}
}

bool NativeGuildTrades::Offer(Player* sender, Player* receiver, Item* item, uint32 count, char const** refusal, bool party)
{
    if (refusal) *refusal = nullptr;
    auto reject = [refusal](char const* reason) { if (refusal) *refusal = reason; return false; };
    if (BotWorldActions::IsMapExecution() || offers.size() >= MaxOffers ||
        !EligiblePair(sender, receiver, party) || sender->GetTradeData() || receiver->GetTradeData() ||
        !item || item->GetOwnerGuid() != sender->GetObjectGuid() || !item->CanBeTraded() ||
        !count || count > item->GetCount())
        return reject("pair eligibility, existing trade or item eligibility");

    uint32 const itemId = item->GetEntry();
    WorldPacket initiate(CMSG_INITIATE_TRADE);
    initiate << receiver->GetObjectGuid();
    sender->GetSession()->HandleInitiateTradeOpcode(initiate);
    if (sender->GetTrader() != receiver || receiver->GetTrader() != sender)
        return reject("native trade initiation");
    if (!EligiblePair(sender, receiver, party))
    {
        sender->TradeCancel(true);
        return reject("pair eligibility after initiation");
    }
    if (count < item->GetCount())
    {
        auto const destination = SplitPosition(sender, item, count);
        if (!destination)
        {
            sender->TradeCancel(true);
            return reject("no empty split position");
        }
        uint16 const source = item->GetPos();
        sender->SplitItem(source, *destination, count);
        item = sender->GetItemByPos(uint8(*destination >> 8), uint8(*destination));
        if (!item || item->GetEntry() != itemId || item->GetCount() != count)
        {
            sender->TradeCancel(true);
            return reject("native split rejected");
        }
    }

    WorldPacket begin(CMSG_BEGIN_TRADE);
    receiver->GetSession()->HandleBeginTradeOpcode(begin);
    WorldPacket set(CMSG_SET_TRADE_ITEM);
    set << uint8(0) << item->GetBagSlot() << item->GetSlot();
    sender->GetSession()->HandleSetTradeItemOpcode(set);
    auto* from = BotManager::Instance().FindBot(sender->GetObjectGuid());
    auto* to = BotManager::Instance().FindBot(receiver->GetObjectGuid());
    if (!from || !to || !sender->GetTradeData() || !receiver->GetTradeData() || sender->GetTradeData()->GetItem(TradeSlots(0)) != item)
    {
        if (sender->GetTrader() == receiver) sender->TradeCancel(true);
        return reject("native trade item rejected");
    }
    auto const delay = std::max(sender->GetTradeData()->GetScamPreventionDelay(), receiver->GetTradeData()->GetScamPreventionDelay());
    GuildOffer offer{ai::Event("guild gift sender", std::string(), sender), ai::Event("guild gift receiver", std::string(), receiver),
        {sender->GetMapId(), sender->GetInstanceId(), sender->GetMapWorkGeneration()},
        {receiver->GetMapId(), receiver->GetInstanceId(), receiver->GetMapWorkGeneration()},
        from->masterGuid, to->masterGuid, item->GetObjectGuid(), RelationshipId(sender, receiver, party), party, itemId, count,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1000 + std::max<time_t>(0, delay))};
    try { offers.push_back(std::move(offer)); }
    catch (...) { sender->TradeCancel(true); throw; }
    sLog.outString("TortoiseBots: native %s gift offered by %s to %s (item %u x%u); acceptance pending",
        party ? "party" : "guild", sender->GetName(), receiver->GetName(), itemId, count);
    return true;
}

void NativeGuildTrades::Update()
{
    if (BotWorldActions::IsMapExecution()) return;
    auto const started = std::chrono::steady_clock::now();
    unsigned processed = 0;
    for (size_t index = 0; index < offers.size(); )
    {
        if (processed >= 8 || (processed && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(4))) break;
        GuildOffer& queued = offers[index];
        Player* sender = queued.sender.GetOwner();
        Player* receiver = queued.receiver.GetOwner();
        if (!OwnOffer(queued, sender, receiver))
        {
            // Native logout/transfer may already have canceled the trade. A
            // changed offer belongs to its current owner; never overwrite it.
            offers.erase(offers.begin() + index);
            ++processed;
            continue;
        }
        bool const current = CurrentOffer(queued, sender, receiver);
        if (current && std::chrono::steady_clock::now() < queued.readyAt) { ++index; continue; }
        GuildOffer offer = std::move(queued);
        offers.erase(offers.begin() + index);
        ++processed;
        bool complete = false;
        if (current)
        {
            uint32 const senderBefore = sender->GetItemCount(offer.itemId, true);
            uint32 const receiverBefore = receiver->GetItemCount(offer.itemId, true);
            WorldPacket accept(CMSG_ACCEPT_TRADE);
            accept << uint32(0);
            sender->GetSession()->HandleAcceptTradeOpcode(accept);
            if (OwnOffer(offer, sender, receiver) && sender->GetTradeData()->IsAccepted())
            {
                WorldPacket receive(CMSG_ACCEPT_TRADE);
                receive << uint32(0);
                receiver->GetSession()->HandleAcceptTradeOpcode(receive);
                complete = !sender->GetTradeData() && !receiver->GetTradeData() &&
                    senderBefore >= offer.count && sender->GetItemCount(offer.itemId, true) == senderBefore - offer.count &&
                    receiver->GetItemCount(offer.itemId, true) == receiverBefore + offer.count;
            }
        }
        if (!complete && OwnOffer(offer, sender, receiver)) sender->TradeCancel(true);
        sLog.outString("TortoiseBots: native %s gift %s from %s to %s (item %u x%u)",
            offer.party ? "party" : "guild", complete ? "completed" : "not completed", sender->GetName(), receiver->GetName(), offer.itemId, offer.count);
    }
}

void NativeGuildTrades::Clear()
{
    if (BotWorldActions::IsMapExecution()) return;
    while (!offers.empty())
    {
        GuildOffer offer = std::move(offers.front());
        offers.pop_front();
        Player* sender = offer.sender.GetOwner();
        Player* receiver = offer.receiver.GetOwner();
        if (OwnOffer(offer, sender, receiver)) sender->TradeCancel(true);
    }
}
}

// End native guild trade implementation.
