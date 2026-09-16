
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "playerbot/RandomBotFacade.h"
#include "SecurityCheckAction.h"

using namespace ai;

bool SecurityCheckAction::isUseful()
{
    Player* master = ai->GetMaster();
    return sRandomBotFacade.IsRandomBot(bot) && master && master->GetSession() &&
        master->GetSession()->GetSecurity() < SEC_GAMEMASTER &&
        !PlayerbotAIStorage::Instance().GetAI(master);
}

bool SecurityCheckAction::Execute(Event& event)
{
    if (auto deferred = TortoiseBots::BotWorldActions::Instance().Defer(bot, getName(), event))
        return *deferred;
    // The anti-ninja check is strictly for autonomous world/random bots, not player-owned bots.
    if (!sRandomBotFacade.IsRandomBot(bot))
        return false;

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Group* group = bot->GetGroup();
    if (group)
    {
        LootMethod method = group->GetLootMethod();
        ItemQualities threshold = group->GetLootThreshold();
        if (method == MASTER_LOOT || method == FREE_FOR_ALL || threshold > ITEM_QUALITY_UNCOMMON)
        {
            Player* leader = ai->GetGroupMaster();
            if (!leader || !leader->GetSession())
                return false;
            if (leader->GetSession()->GetSecurity() == SEC_PLAYER &&
                (!bot->GetGuildId() || bot->GetGuildId() != leader->GetGuildId()))
            {
                ai->TellError(requester, "I will play with this loot type only if I'm in your guild :/");
                ai->ChangeStrategy("+passive,+stay", BotState::BOT_STATE_NON_COMBAT);
                ai->ChangeStrategy("+passive,+stay", BotState::BOT_STATE_COMBAT);
            }
            return true;
        }
    }
    return false;
}