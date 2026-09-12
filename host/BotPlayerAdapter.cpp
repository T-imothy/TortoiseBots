#include "BotPlayerAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"
#include "Chat.h"
#include "../runtime/ObservabilityEmitter.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_RELEASE_TO_CLIENT,
        PLAYERHOOK_IS_MACHINE_DRIVEN,
        PLAYERHOOK_IS_UPDATE_CRITICAL,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT })
{
}

void BotPlayerAdapter::OnReleaseToClient(Player* player)
{
    BotManager::Instance().ReleaseToClient(player);
}

bool BotPlayerAdapter::IsMachineDriven(Player const* player)
{
    // Network ownership wins immediately, even before the world-owned record
    // reconciliation has removed a former bot's adapter.
    return player && player->GetSession() && player->GetSession()->IsHeadless() &&
        PlayerbotAIStorage::Instance().GetAI(const_cast<Player*>(player));
}

bool BotPlayerAdapter::IsUpdateCritical(Player const* player)
{
    if (!IsMachineDriven(player)) return false;
    auto* ai = PlayerbotAIStorage::Instance().GetAI(const_cast<Player*>(player));
    if (!ai) return false;
    float const range = ai::WorldPosition(const_cast<Player*>(player)).getVisibilityDistance() +
        sPlayerbotAIConfig.reactDistance;
    if (player->IsBeingTeleported() || ai->HasRealPlayerMaster() || ai->HasPlayerNearby(range))
        return true;
    if (Group* group = const_cast<Player*>(player)->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member && member != player && member->IsInWorld() && member->GetSession() &&
                member->GetSession()->HasNetworkTransport())
                return true;
        }
    return false;
}

void BotPlayerAdapter::OnLogin(Player* player)
{
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
    {
        RandomBotService::Instance().OnHumanLogin();
        if (player->GetSession()->GetSecurity() >= SEC_DEVELOPER && sObservabilityEmitter.IsEnabled())
        {
            ChatHandler(player).PSendSysMessage("|cff00ff00[TortoiseBots]|r Observability dashboard active: http://localhost:8095/dashboard");
        }
    }
    BotManager::Instance().OnPlayerLogin(player);
}

void BotPlayerAdapter::OnMapChanged(Player* player)
{
    if (!player || !player->GetSession() || !player->GetSession()->HasNetworkTransport() ||
        !player->IsInWorld() || !player->GetMap() || player->GetMap()->IsDungeon())
    {
        return;
    }

    // This is a player-convenience transition, not lifecycle work. BotManager
    // supplies a live owned-bot snapshot; PlayerConvenience owns the queued
    // summon and its cancellation/completion state.
    for (Player* bot : BotManager::Instance().GetBotsForMaster(player->GetObjectGuid()))
    {
        if (!bot || !bot->GetMap() || !bot->GetMap()->IsDungeon())
            continue;

        if (PlayerConvenience::Instance().RequestSummon(player, bot))
        {
            sLog.outString("TortoiseBots: returning bot %s after master %s left a dungeon",
                bot->GetName(), player->GetName());
        }
        else
        {
            sLog.outError("TortoiseBots: bot %s remained in a dungeon after master %s left; "
                "the native summon preconditions rejected its return",
                bot->GetName(), player->GetName());
        }
    }
}

void BotPlayerAdapter::OnBeforeLogout(Player* player)
{
    BotManager::Instance().OnPlayerBeforeLogout(player);
}

void BotPlayerAdapter::OnLogout(Player* player)
{
    BotManager::Instance().OnPlayerLogout(player);
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        RandomBotService::Instance().OnHumanLogout();
}



} // namespace TortoiseBots
