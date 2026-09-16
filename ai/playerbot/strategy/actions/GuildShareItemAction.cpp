#include "runtime/NativeGuildTrades.h"
#include "runtime/BotWorldActions.h"
#include "playerbot/playerbot.h"
#include "GuildShareItemAction.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

bool GuildShareItemAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    if (bot->IsInCombat() || bot->GetTradeData())
        return false;

    return AI_VALUE(GuildShareTarget, "guild share target").IsValid();
}

bool GuildShareItemAction::Execute(Event& event)
{
    if (auto deferred = TortoiseBots::BotWorldActions::Instance().Defer(bot, getName(), event))
        return *deferred;
    if (!bot->IsAlive() || !bot->IsInWorld() || !bot->GetMap() || bot->IsBeingTeleported() || bot->IsInCombat())
        return false;
    // Re-evaluate recipient and amounts at execution, not the trigger's cached
    // decision. A saved GUID never grants access to a reclaimed Player object.
    RESET_AI_VALUE(GuildShareTarget, "guild share target");
    GuildShareTarget shareTarget = AI_VALUE(GuildShareTarget, "guild share target");
    if (!shareTarget.IsValid())
        return false;

    Player* receiver = bot->GetMap()->GetPlayer(shareTarget.receiverGuid);
    if (!receiver || receiver == bot || !PlayerbotAI::IsSafe(bot, receiver) ||
        !receiver->IsAlive() || receiver->IsInCombat() || !bot->GetGuildId() ||
        receiver->GetGuildId() != bot->GetGuildId())
        return false;
    uint32 itemId = shareTarget.itemId;
    uint32 shareAmount = shareTarget.amount; // 0 = give all (existing behavior)

    PlayerbotAI* receiverAi = PlayerbotAIStorage::Instance().GetAI(receiver);
    if (!receiverAi)
        return false;

    std::vector<Item*> items = ai->GetInventoryItems();
    for (Item* item : items)
    {
        if (item->GetEntry() != itemId)
            continue;
        uint32 const count = shareAmount ? std::min(shareAmount, item->GetCount()) : item->GetCount();
        // Success here means an actual native offer was created. The service
        // observes timed acceptance and logs completed/canceled separately.
        if (TortoiseBots::NativeGuildTrades::Offer(bot, receiver, item, count))
            return true;
    }
    return false;
}
