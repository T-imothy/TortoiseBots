#include "BotPlayerAdapter.h"

#include "../behavior/PlayerConvenience.h"
#include "../runtime/BotManager.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/PlayerbotAIAdapter.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Event.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AiFactory.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/strategy/actions/ChangeTalentsAction.h"
#include <sstream>
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"
#include "Chat.h"
#include "../runtime/ObservabilityEmitter.h"
#include "ModuleLog.h"

namespace TortoiseBots {

BotPlayerAdapter::BotPlayerAdapter()
    : PlayerScript("tortoisebots_players", {
        PLAYERHOOK_ON_RELEASE_TO_CLIENT,
        PLAYERHOOK_IS_MANAGED_BOT,
        PLAYERHOOK_GET_BOT_ROLES,
        PLAYERHOOK_IS_AI_CONTROLLED,
        PLAYERHOOK_HAS_AI_FOLLOWERS,
        PLAYERHOOK_GET_ALLOWED_ROLES,
        PLAYERHOOK_SET_FORCED_ROLE,
        PLAYERHOOK_IS_MACHINE_DRIVEN,
        PLAYERHOOK_IS_UPDATE_CRITICAL,
        PLAYERHOOK_IS_AI_UPDATE_DUE,
        PLAYERHOOK_ON_AI_UPDATE,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT })
{
}

void BotPlayerAdapter::OnReleaseToClient(Player* player)
{
    ai::Event::InvalidateOwner(player);
    BotManager::Instance().ReleaseToClient(player);
}

bool BotPlayerAdapter::IsAIControlled(Player const* player)
{
    return IsMachineDriven(player);
}

bool BotPlayerAdapter::IsManagedBot(Player* player)
{
    return IsAIControlled(player);
}

uint8 BotPlayerAdapter::GetBotRoles(Player* player)
{
    if (!IsAIControlled(player)) return 0;
    auto* ai = PlayerbotAIStorage::Instance().GetAI(player);
    return ai->GetForcedRole() ? ai->GetForcedRole() : uint8(AiFactory::GetPlayerRoles(player));
}

bool BotPlayerAdapter::HasAIFollowers(Player const* player)
{
    return player && !BotManager::Instance().GetBotsForMaster(player->GetObjectGuid()).empty();
}

bool BotPlayerAdapter::GetAllowedRoles(Player const* player, uint8& roles)
{
    if (!IsAIControlled(player)) return false;
    roles = uint8(AiFactory::GetPlayerRoles(player));
    return true;
}

void BotPlayerAdapter::SetForcedRole(Player* player, uint8 role)
{
    if (!IsAIControlled(player) || (role != 0 && role != 1 && role != 2 && role != 4)) return;
    auto* ai = PlayerbotAIStorage::Instance().GetAI(player);
    if (!ai || ai->GetForcedRole() == role) return;
    ai->SetForcedRole(role);
    // Preserve the baseline's contradiction-only respec. Human-owned bots keep
    // their chosen talents, and unreachable class roles never wipe a tree.
    if (role && player->GetLevel() >= 10 && !ai->HasActivePlayerMaster() &&
        BotManager::Instance().IsRandomBot(player->GetObjectGuid()) &&
        !(AiFactory::GetPlayerRoles(player) & role) &&
        !ai::ChangeTalentsAction::getPremadePaths(player->getClass(), "", BotRoles(role)).empty())
    {
        sRandomBotFacade.SetValue(player->GetGUIDLow(), "specNo", 0);
        sRandomBotFacade.SetValue(player->GetGUIDLow(), "specLink", 0);
        player->ResetTalents(true);
        std::ostringstream result;
        ai::ChangeTalentsAction::AutoSelectTalents(player, &result, BotRoles(role));
    }
    ai->ResetStrategies();
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

bool BotPlayerAdapter::IsAIUpdateDue(Player* player, uint32 /*diff*/)
{
    // Sagiroth's UpdateAI services packets, movement, combat transitions and
    // replies before its action-delay gate. Skipping the entire update based
    // on that delay would starve those services. Native map budgets/elapsed
    // clocks bound admission; UpdateAI retains its own decision cadence.
    return PlayerbotAIAdapter::CanUpdatePlayer(player);
}

void BotPlayerAdapter::OnAIUpdate(Player* player, uint32 diff, bool minimal)
{
    PlayerbotAIAdapter::UpdatePlayer(player, diff, minimal);
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
            TB_LOG_DETAIL("TortoiseBots: returning bot %s after master %s left a dungeon",
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
    ai::Event::InvalidateOwner(player);
    BotManager::Instance().OnPlayerBeforeLogout(player);
}

void BotPlayerAdapter::OnLogout(Player* player)
{
    ai::Event::InvalidateOwner(player);
    BotManager::Instance().OnPlayerLogout(player);
    if (player && player->GetSession() && player->GetSession()->HasNetworkTransport())
        RandomBotService::Instance().OnHumanLogout();
}

BotUnitAdapter::BotUnitAdapter()
    : UnitScript("tortoisebots_units", { UNITHOOK_ON_UNIT_DEATH })
{
}

void BotUnitAdapter::OnUnitDeath(Unit* unit, Unit* killer)
{
    if (!unit || !unit->IsPlayer())
        return;

    Player* player = unit->ToPlayer();
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
        return;

    ai->SetLastKiller(killer);
}

} // namespace TortoiseBots

