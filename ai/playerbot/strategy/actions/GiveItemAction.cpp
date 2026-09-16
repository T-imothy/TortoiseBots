#include "runtime/BotWorldActions.h"
#include "runtime/NativeGuildTrades.h"

#include "playerbot/playerbot.h"
#include "GiveItemAction.h"

#include "playerbot/strategy/values/ItemCountValue.h"

using namespace ai;

std::vector<std::string> split(const std::string &s, char delim);

bool GiveItemAction::Execute(Event& event)
{
    if (auto deferred = TortoiseBots::BotWorldActions::Instance().Defer(bot, getName(), event))
        return *deferred;
    Player* receiver = dynamic_cast<Player*>(GetTarget());
    if (!receiver || !ai->IsSafe(receiver)) return false;
    PlayerbotAI* receiverAi = PlayerbotAIStorage::Instance().GetAI(receiver);
    if (!receiverAi) return false;
    if (receiverAi->GetAiObjectContext()->GetValue<uint32>("item count", item)->Get())
        return true;
    for (Item* gift : ai->InventoryParseItems(item, IterateItemsMask::ITERATE_ITEMS_IN_BAGS))
    {
        if (receiver->CanUseItem(gift->GetProto()) != EQUIP_ERR_OK)
            continue;
        // Native trade retains conjured-item metadata, range/faction rules and
        // both inventories' persistence. Admission is not a completion message.
        if (TortoiseBots::NativeGuildTrades::OfferParty(bot, receiver, gift, gift->GetCount()))
            return true;
    }
    return false;
}

Unit* GiveItemAction::GetTarget()
{
    return AI_VALUE2(Unit*, "party member without item", item);
}

Unit* GiveFoodAction::GetTarget()
{
    return AI_VALUE(Unit*, "party member without food");
}

Unit* GiveWaterAction::GetTarget()
{
    return AI_VALUE(Unit*, "party member without water");
}
