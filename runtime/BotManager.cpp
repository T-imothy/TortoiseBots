#include "Guild/Guild.h"
#include "Guild/GuildMgr.h"
#include "NativeGuildTrades.h"
#include "BotManager.h"
#include "BotActivityLease.h"
#include "BotWorldActions.h"
#include "PlayerbotAIAdapter.h"
#include "PlayerbotAIStorage.h"
#include "GearSeedingGuard.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../host/BotSessionAdapter.h"
#include "../commands/BotCommands.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
#include "AccountMgr.h"
#include "WorldPacket.h"
#include "Chat.h"
#include "Group/Group.h"
#include "Maps/GridMap.h"
#include "Map.h"
#include "../commands/BotCommands.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../ai/playerbot/WorldPosition.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"

#include "Database/DatabaseEnv.h"
#include "../host/ModuleLog.h"

#include <algorithm>

namespace TortoiseBots {

namespace {
// One-shot headless RNDBOT scatter using persisted GenericRpg destinations.
// Fail-closed: any validation miss retains original position. No DB scan,
// no GenerateTravelNodes, no homebind update.
bool IsUsableTeleportPoint(ai::WorldPosition const& point)
{
    if (!point.isOverworld() || !point.isValid() || !point.loadMapAndVMap(0))
        return false;

    // loadMapAndVMap above validates the navmesh; getTerrain and
    // GetWaterOrGroundLevel load the map grid/VMAP and resolve terrain height.
    // Reject stale spawn Z instead of teleporting a bot into the ground or air.
    TerrainInfo const* terrain = point.getTerrain();
    if (!terrain)
        return false;

    float groundZ = INVALID_HEIGHT;
    float maxZ = terrain->GetWaterOrGroundLevel(point.getX(), point.getY(), point.getZ(), &groundZ, false);
    return groundZ > INVALID_HEIGHT && maxZ > INVALID_HEIGHT &&
        point.getZ() >= groundZ && point.getZ() <= maxZ + 2.0f;
}

// Shared level-fitting picker for login scatter and post-rez rescue.
// Probes GenericRpg destinations in the bot's ±5 validated level window and
// returns a terrain-validated point. Destinations stay TravelMgr-owned.
// Returns nullptr on any miss (fail-closed, original position retained).
ai::WorldPosition const* PickLevelFittingPoint(::Player* bot,
    ai::TravelDestinationPurpose purpose = ai::TravelDestinationPurpose::GenericRpg,
    float maxDistance = 0, bool innkeeper = false, bool checkPossible = false)
{
    if (!bot)
        return nullptr;
    auto& travelMgr = MaNGOS::Singleton<ai::TravelMgr>::Instance();
    // Use bounded validated levels: persisted ai_playerbot_zone_level first (with
    // parent-zone cached fallback), then immutable DBC AreaTable AreaLevel / parent
    // AreaLevel. No creature scan, no DB write, no lazy GetAreaLevel mutation.
    // An empty stock-install table therefore still allows DBC-validated scatter
    // while an entirely unvalidated point/area still fails closed.
    ai::PlayerTravelInfo info(bot);
    // Fetch without level filtering (onlyPossible=false) and without distance bias (maxDistance=0)
    // to avoid lazy IsPossible scans and to scatter across all level-appropriate zones, not just near logout pos.
    auto dests = travelMgr.GetDestinations(info, (uint32)purpose, {}, false, maxDistance);
    // Filter cheap immutable NPC metadata before the bounded terrain probes.
    // Otherwise rare inns compete with every RPG NPC for just 32 attempts.
    if (innkeeper)
        dests.erase(std::remove_if(dests.begin(), dests.end(), [](ai::TravelDestination* destination)
        {
            auto* entry = dynamic_cast<ai::EntryTravelDestination*>(destination);
            return !entry || !entry->GetCreatureInfo() || !entry->HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER);
        }), dests.end());
    if (dests.empty())
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no GenericRpg destinations for bot %s level %u", bot->GetName(), bot->GetLevel());
        return nullptr;
    }

    // Probe a bounded random subset. Destination points can be numerous, and
    // loading terrain/MMAP for every point would turn login into a world stall.
    constexpr uint32 maxDestinationAttempts = 32;
    constexpr uint32 maxPointAttempts = 8;
    ai::WorldPosition* chosen = nullptr;
    uint32 destinationAttempts = std::min<uint32>(maxDestinationAttempts, dests.size());
    // Level window rationale (both bounds, no speculative config):
    //  Upper +5 from local RpgTravelDestination::IsPossible (destAreaLevel > botLevel+5 => reject) and
    //   quest level gate (questLevel > botLevel+5 => reject).
    //  Lower -5 from donor AiPlayerbot.RandomBotTeleLevel=5 window where creature avg satisfies
    //   botLevel-5 <= creatureLevel <= botLevel (see RandomPlayerbotMgr::PrepareTeleportCache query delta in [0,5])
    //   and local Grind lower bound approx botLevel-12. Symmetric ±5 is the minimal conservative window
    //   that prevents both 60->Elwynn and 10->Winterspring mismatches without new config; enforced on
    //   bounded validated levels (TryGetValidatedAreaLevel: cached + DBC AreaLevel/parent), never via lazy GetAreaLevel/IsLocationLevelValid.
    int32 botLevel = (int32)bot->GetLevel();
    int32 lower = botLevel - 5;
    if (lower < 1) lower = 1;
    int32 upper = botLevel + 5;
    if (upper > 60) upper = 60;
    for (uint32 destinationAttempt = 0; destinationAttempt < destinationAttempts && !chosen; ++destinationAttempt)
    {
        std::swap(dests[destinationAttempt], dests[urand(destinationAttempt, static_cast<uint32>(dests.size() - 1))]);
        ai::TravelDestination* destination = dests[destinationAttempt];
        if (!destination)
            continue;
        if (checkPossible)
        {
            auto* entry = dynamic_cast<ai::EntryTravelDestination*>(destination);
            if (!entry || !entry->GetCreatureInfo() || !destination->IsPossible(info)) continue;
            ai::GuidPosition creature(HIGHGUID_UNIT, destination->GetEntry());
            if (purpose == ai::TravelDestinationPurpose::GenericRpg && creature.IsHostileTo(bot)) continue;
            if (innkeeper && !entry->HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER)) continue;
        }

        auto points = destination->GetPoints();
        uint32 pointAttempts = std::min<uint32>(maxPointAttempts, points.size());
        for (uint32 pointAttempt = 0; pointAttempt < pointAttempts; ++pointAttempt)
        {
            std::swap(points[pointAttempt], points[urand(pointAttempt, static_cast<uint32>(points.size() - 1))]);
            ai::WorldPosition* point = points[pointAttempt];
            if (!point)
                continue;
            // A destination can contain both nearby and distant spawn points.
            // Its nearest-point query cannot validate the point chosen here.
            if (maxDistance > 0 && (point->getMapId() != bot->GetMapId() ||
                point->sqDistance(info.getPosition()) > maxDistance * maxDistance))
                continue;
            // Reject unresolved area flag rather than silently using linkedZone fallback
            // from GetByAreaFlagAndMap. Allow safe parent-zone cached lookup via helper.
            if (point->getAreaFlag() == 0)
                continue;
            AreaTableEntry const* area = point->GetArea();
            if (!area)
                continue;
            uint32 zoneId = area->ZoneId ? area->ZoneId : area->Id;
            if (zoneId == 5536 || zoneId == 5225)
                continue;
            if (point->IsEnemyHomeZoneFor(info.GetTeam()))
                continue;
            int32 areaLevel;
            if (!travelMgr.TryGetValidatedAreaLevel(area->Id, areaLevel))
                continue;
            if (areaLevel < lower || areaLevel > upper)
                continue;
            if (IsUsableTeleportPoint(*point))
            {
                chosen = point;
                break;
            }
        }
    }

    if (!chosen)
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no valid overworld point for bot %s level %u", bot->GetName(), bot->GetLevel());
        return nullptr;
    }
    return chosen;
}

bool TryRandomTeleport(::Player* bot, BotRecord const& record)
{
    if (!sPlayerbotAIConfig.enableRandomTeleports)
        return false;
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;
    if (!record.random)
        return false;
    // Low-level bots are not scattered at all: below level 10 their quests are in the
    // starting area and a level-fitting inn elsewhere (a capital, say) puts zones they
    // cannot cross between them and their work.
    if (bot->GetLevel() < 10)
        return false;
    if (bot->IsBeingTeleported())
        return false;
    if (!bot->IsInWorld())
        return false;
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
    {
        TB_LOG_DEBUG("TortoiseBots: random teleport skipped pinned bot %s", bot->GetName());
        return false;
    }
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        TB_LOG_DETAIL("TortoiseBots: random teleport no AI for bot %s, retaining position", bot->GetName());
        return false;
    }
    ai::WorldPosition const* chosen = PickLevelFittingPoint(bot);
    if (!chosen)
        return false;

    bool ok = bot->TeleportTo(chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ(), bot->GetOrientation(), 0);
    if (ok)
        TB_LOG_DETAIL("TortoiseBots: random teleport bot %s level %u to map %u %.1f %.1f %.1f", bot->GetName(), bot->GetLevel(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    else
        sLog.outError("TortoiseBots: random teleport TeleportTo failed for bot %s to map %u %.1f %.1f %.1f, retaining position", bot->GetName(), chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    return ok;
}
} // namespace

bool BotManager::RelocateRandomBot(::Player* bot, RandomBotDestination destination)
{
    if (!bot || !IsControllableBot(bot) || !bot->IsAlive() || bot->GetLevel() < 10 ||
        bot->IsBeingTeleported() || bot->IsInCombat() || bot->IsTaxiFlying() ||
        bot->InBattleGround() || bot->InBattleGroundQueue() || bot->GetGroup() ||
        !bot->GetMap() || bot->GetMap()->IsDungeon() || sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()) ||
        !BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow())) return false;
    auto const* record = FindBot(bot->GetObjectGuid());
    auto* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!record || !record->random || !record->masterGuid.IsEmpty() || !ai || ai->HasActivePlayerMaster()) return false;
    bool const rpg = destination == RandomBotDestination::Rpg;
    auto const* point = PickLevelFittingPoint(bot, rpg ? ai::TravelDestinationPurpose::GenericRpg :
        ai::TravelDestinationPurpose::Grind, destination == RandomBotDestination::LocalGrind ?
            float(std::max<uint32>(1, sPlayerbotAIConfig.randomBotTeleportDistance)) : 0,
        rpg, true);
    if (!point) return false;
    auto const* area = point->GetArea();
    if (!area) return false;
    // The native teleport owns movement teardown, packets and map transfer.
    // Change the home bind only after an accepted friendly inn relocation.
    if (!bot->TeleportTo(point->getMapId(), point->getX(), point->getY(), point->getZ(), bot->GetOrientation(), 0)) return false;
    if (rpg) bot->SetHomebindToLocation(*point, area->Id);
    ai->Reset(true);
    if (rpg)
    {
        // Reset clears the old trip. Restore the mature inn dwell period only
        // after native teleport acceptance, so the next decision stays here.
        auto* target = ai->GetAiObjectContext()->GetValue<ai::TravelTarget*>("travel target")->Get();
        if (target)
        {
            MaNGOS::Singleton<ai::TravelMgr>::Instance().SetNullTravelTarget(target);
            target->SetStatus(ai::TravelStatus::TRAVEL_STATUS_COOLDOWN);
            target->SetExpireIn(10 * MINUTE * IN_MILLISECONDS);
        }
    }
    return true;
}

bool BotManager::RelocateHopelessBot(::Player* bot)
{
    if (!sPlayerbotAIConfig.relocateHopelessDeaths)
        return false;
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->InBattleGround())
        return false;
    if (bot->GetGroup())
        return false;
    BotRecord* record = FindBot(bot->GetObjectGuid());
    if (!record || !record->random || record->lifecycle != BotLifecycle::InWorld)
        return false;
    if (!record->masterGuid.IsEmpty())
        return false;
    if (sRandomBotFacade.IsPinnedBot(bot->GetGUIDLow()))
        return false;
    ::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    auto* deathCountValue = ai->GetAiObjectContext()->GetValue<uint32>("death count");
    uint32 deathCount = deathCountValue ? deathCountValue->Get() : 0;
    if (deathCount < 2)
        return false;
    // Same +5 tolerance the travel and quest gates use: only a zone the bot
    // cannot plausibly survive counts as hopeless. Unknown levels fail closed.
    int32 areaLevel = 0;
    auto& travelMgr = MaNGOS::Singleton<ai::TravelMgr>::Instance();
    if (!travelMgr.TryGetValidatedAreaLevel(bot->GetAreaId(), areaLevel) || areaLevel <= 0)
        return false;
    if (areaLevel <= (int32)bot->GetLevel() + 5)
        return false;
    // A bot below level 10 belongs in its starting area: its quests are there, and any
    // capital the picker would choose lies behind zones it cannot cross alive (a level-6
    // dwarf sent to Stormwind walks the Burning Steppes to reach Coldridge Valley - or,
    // from Darnassus, cannot reach it at all and dies or spins on the spot). Home bind.
    if (bot->GetLevel() < 10)
    {
        if (!bot->TeleportToHomebind(0, false))
        {
            sLog.outError("TortoiseBots: hopeless-death relocation to home bind failed for bot %s, retaining position", bot->GetName());
            return false;
        }
        if (deathCountValue)
            deathCountValue->Reset();
        sLog.outString("TortoiseBots: relocated hopeless bot %s level %u after %u deaths from area level %d to its home bind",
            bot->GetName(), bot->GetLevel(), deathCount, areaLevel);
        return true;
    }
    ai::WorldPosition const* chosen = PickLevelFittingPoint(bot);
    if (!chosen)
        return false;
    if (!bot->TeleportTo(chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ(), bot->GetOrientation(), 0))
    {
        sLog.outError("TortoiseBots: hopeless-death relocation TeleportTo failed for bot %s, retaining position", bot->GetName());
        return false;
    }
    if (deathCountValue)
        deathCountValue->Reset();
    TB_LOG_DETAIL("TortoiseBots: relocated hopeless bot %s level %u after %u deaths from area level %d to map %u %.1f %.1f %.1f",
        bot->GetName(), bot->GetLevel(), deathCount, areaLevel, chosen->getMapId(), chosen->getX(), chosen->getY(), chosen->getZ());
    return true;
}

// A Headless session must never render an owned bot as an account-level GM.
// Its Network owner retains all account privileges; this only normalizes the
// separately logged-in bot character after the core restores saved GM flags.
bool NormalizeHeadlessGmPresentation(::Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;

    uint32 const gmFlags = PLAYER_EXTRA_GM_ON | PLAYER_EXTRA_GM_ACCEPT_TICKETS |
        PLAYER_EXTRA_GM_INVISIBLE | PLAYER_EXTRA_GM_CHAT |
        PLAYER_EXTRA_GM_DISABLE_SOCIAL;
    uint32 const flags = bot->GetExtraFlags();
    if (!(flags & gmFlags) && (flags & PLAYER_EXTRA_ACCEPT_WHISPERS) &&
        bot->IsGMVisible() && !bot->IsGameMaster() && bot->GetInvincibilityHpThreshold() == 0)
    {
        return false;
    }

    bot->SetInvincibilityHpThreshold(0);
    bot->SetGameMaster(false);
    if (flags & PLAYER_EXTRA_GM_CHAT)
        bot->SetGMChat(false);
    if (flags & PLAYER_EXTRA_GM_ACCEPT_TICKETS)
        bot->SetAcceptTicket(false);
    if (flags & PLAYER_EXTRA_GM_DISABLE_SOCIAL)
        bot->SetGMSocials(true);
    if (!(flags & PLAYER_EXTRA_ACCEPT_WHISPERS))
        bot->SetAcceptWhispers(true);

    // Persist only the bot's presentation flags. The owner's account rank and
    // its Network session are untouched.
    bot->SetGMVisible(true);
    return true;
}

BotEntry::~BotEntry() = default;

BotManager& BotManager::Instance()
{
    static BotManager instance;
    return instance;
}

static bool HasPrimaryProfession(::Player* player)
{
    static uint16 const kPrimary[] = {SKILL_ALCHEMY, SKILL_BLACKSMITHING,
        SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_HERBALISM,
        SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING, SKILL_TAILORING};
    for (uint16 skill : kPrimary)
        if (player->HasSkill(skill))
            return true;
    return false;
}

void BotManager::OnPlayerLogin(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    ::WorldSession* session = player->GetSession();
    if (!session || session->HasNetworkTransport())
    {
        if (it != m_bots.end())
            ReleaseToClient(player);
        RebindOwnedBots(player);
        return;
    }

    if (it == m_bots.end())
        return;

    NormalizeHeadlessGmPresentation(player);

    BotEntry& entry = it->second;
    BotRecord& record = entry.record;
    if (record.enteredWorld)
        return;

    if (!entry.aiAdapter)
    {
        ::Player* masterPlayer = nullptr;
        if (record.masterGuid && !record.masterGuid.IsEmpty())
            masterPlayer = sObjectAccessor.FindPlayer(record.masterGuid);

        entry.aiAdapter = std::make_unique<PlayerbotAIAdapter>(player, masterPlayer);
    }

    if (!entry.aiAdapter->IsInitialized() && !entry.aiAdapter->Initialize())
    {
        sLog.outError("TortoiseBots: PlayerbotAI attach failed for %s; stopping the Headless session",
            player->GetName());
        record.enteredWorld = false;
        record.lifecycle = BotLifecycle::Removing;
        entry.aiAdapter->Shutdown();
        BotSessionAdapter::StopHeadlessSession(record.characterGuid, true);
        return;
    }

    // Do not publish an in-world/controllable record until the adapter has
    // registered the exact AI instance that owns this live Player.
    if (!entry.aiAdapter->IsUsable())
    {
        sLog.outError("TortoiseBots: PlayerbotAI attach for %s is not usable; stopping the Headless session",
            player->GetName());
        record.enteredWorld = false;
        record.lifecycle = BotLifecycle::Removing;
        entry.aiAdapter->Shutdown();
        BotSessionAdapter::StopHeadlessSession(record.characterGuid, true);
        return;
    }

    record.enteredWorld = true;
    record.lifecycle = BotLifecycle::InWorld;

    // Normalize Goblin and High Elf bot starting zone: relocate from player-only
    // custom starting zones (Blackstone Island 5536 and Thalassian Highlands 5225,
    // which lack navmesh/transport paths to mainland) to standard faction starting zones.
    if (record.random && player->GetLevel() < 10)
    {
        uint32 zoneId = player->GetZoneId();
        if (player->GetRace() == RACE_GOBLIN && zoneId == 5536)
        {
            player->TeleportTo(1, -618.518f, -4251.67f, 38.718f, 0.0f);
            player->SetHomebindToLocation(WorldLocation(1, -618.518f, -4251.67f, 38.718f, 0.0f), 14);
            player->SaveToDB();
            TB_LOG_DETAIL("TortoiseBots: normalized Goblin bot %s spawn to Valley of Trials", player->GetName());
        }
        else if (player->GetRace() == RACE_HIGH_ELF && zoneId == 5225)
        {
            player->TeleportTo(0, -8949.95f, -132.493f, 83.5312f, 0.0f);
            player->SetHomebindToLocation(WorldLocation(0, -8949.95f, -132.493f, 83.5312f, 0.0f), 12);
            player->SaveToDB();
            TB_LOG_DETAIL("TortoiseBots: normalized High Elf bot %s spawn to Northshire", player->GetName());
        }
    }

    // One-shot random scatter on headless login only; fail-closed, no DB mutation, no homebind
    TryRandomTeleport(player, record);

    // Initial gear seeding is for fresh pool bots only, never earned progression.
    // See GearSeedingGuard.h for the dual-heuristic rule. Stamp after the
    // attempt, not before: a crash between stamp and equip must not matter, and
    // a bot that received no enrichment simply progresses on earned drops.
    // CharacterCreation creates level 1 starter kit; factory's InitEquipment
    // intentionally no-ops for <5. For any auto-created level >=5 (future
    // higher-level pool or manual leveling), trigger gear enrichment immediately
    // on world-thread login rather than waiting ~6h for RandomBotService's
    // randomize interval. Safe and synchronous; the existing
    // randomGearUpgradeEnabled setting controls this (default enabled).
    uint32 botGuidLow = player->GetGUIDLow();
    bool freshBot = TortoiseBots::NeedsInitialGearSeeding(
        player->GetTotalPlayedTime(), sRandomBotFacade.GetValue(botGuidLow, "seeded"));
    if (record.random && sPlayerbotAIConfig.randomGearUpgradeEnabled && player->GetLevel() >= 5 && freshBot)
    {
        if (sRandomBotFacade.UpdateGearSpells(player))
            sRandomBotFacade.SetValue(botGuidLow, "seeded", 1);
    }

    // Persistent-level random bots previously skipped Randomize entirely,
    // leaving weapon skill at 1 and no professions. Existing professions are
    // the durable guard: never reroll a bot that has already learned one.
    if (record.random && !HasPrimaryProfession(player))
    {
        PlayerbotFactory skills(player, player->GetLevel());
        skills.InitAllSkills();
        sLog.outString("TortoiseBots: bot %s received level-bound skills and professions", player->GetName());
    }

    if (m_packetTestEnabled && player->GetObjectGuid() == m_packetTestMasterGuid &&
        !player->HasUnitState(UNIT_STAT_STUNNED))
    {
        player->AddUnitState(UNIT_STAT_STUNNED);
        m_packetTestInjectedStun = true;
    }
    TB_LOG_DETAIL("TortoiseBots: bot %s entered world through native PlayerScript", player->GetName());
}

void BotManager::OnPlayerBeforeLogout(::Player* player)
{
    if (!player)
        return;
    if (m_packetTestInjectedStun && player->GetObjectGuid() == m_packetTestMasterGuid)
    {
        player->ClearUnitState(UNIT_STAT_STUNNED);
        m_packetTestInjectedStun = false;
    }


    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless())
        DetachOwnedBots(player);

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    if (it->second.record.lifecycle != BotLifecycle::Removing)
        it->second.record.lifecycle = BotLifecycle::Removing;
}

void BotManager::OnPlayerLogout(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it != m_bots.end() && it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();
}

void BotManager::DetachOwnedBots(::Player* master)
{
    if (!master)
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->DetachMaster();
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot detach master for %s because the module AI adapter is unavailable",
                bot->GetName());

        TB_LOG_DEBUG("TortoiseBots: detached live master pointer %s from bot %s; ownership GUID retained",
            master->GetName(), bot->GetName());
    }
}

void BotManager::RebindOwnedBots(::Player* master)
{
    if (!master || !master->GetSession() || !master->GetSession()->HasNetworkTransport())
        return;

    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_bots)
    {
        BotEntry& entry = kv.second;
        if (entry.record.masterGuid != masterGuid ||
            entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        ::Player* bot = sObjectAccessor.FindPlayer(entry.record.characterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
            continue;

        if (entry.aiAdapter && entry.aiAdapter->IsInitialized())
            entry.aiAdapter->RebindMaster(master);
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
            sLog.outError("TortoiseBots: cannot rebind master for %s because the module AI adapter is unavailable",
                bot->GetName());
        BotActivityLeaseManager::Instance().ClaimForMaster(entry.record.characterGuid.GetCounter());

        TB_LOG_DETAIL("TortoiseBots: rebound master %s to existing Headless bot %s; mature movement preserved",
            master->GetName(), bot->GetName());
    }
}

void BotManager::ReleaseToClient(::Player* player)
{
    if (!player)
        return;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end())
        return;

    if (it->second.aiAdapter)
        it->second.aiAdapter->Shutdown();

    TB_LOG_DETAIL("TortoiseBots: releasing module control of %s to a network client", player->GetName());
    uint32_t guidLow = player->GetObjectGuid().GetCounter();
    m_bots.erase(it);
    // Human reclaim owns the character now: evict any background lease with
    // active cleanup, then drop the master lock so no ghost lease lingers.
    BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
    BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
}

bool BotManager::RunPendingAddRemoveTest(uint32_t accountId, ::ObjectGuid guid)
{
    HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(guid);
    if (st != HeadlessSessionState::NotFound ||
        sObjectAccessor.FindPlayer(guid) || FindBot(guid))
    {
        sLog.outError("TortoiseBots: PendingAddRemoveTest precondition failed for acct %u guid %s",
            accountId, guid.GetString().c_str());
        return false;
    }

    bool queued = AddBot(accountId, guid);
    bool removed = RemoveBot(guid, false);
    bool noSession = BotSessionAdapter::GetHeadlessSessionState(guid) == HeadlessSessionState::NotFound;
    bool noPlayer = !sObjectAccessor.FindPlayer(guid);
    bool noRecord = !FindBot(guid);

    bool passed = queued && removed && noSession && noPlayer && noRecord;
    TB_LOG_BASIC("TortoiseBots: PendingAddRemoveTest %s acct %u queued %u session %u player %u record %u",
        passed ? "PASSED" : "FAILED", accountId, queued, !noSession, !noPlayer, !noRecord);
    return passed;
}

bool BotManager::AddBot(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    return AddBotWithMaster(accountId, guid, masterGuid);
}

bool BotManager::HasRandomAdmissionCapacity()
{
    uint32 const limit = sPlayerbotAIConfig.randomBotLoginDbQueueLimit;
    return !limit || (CharacterDatabase.GetPendingAsyncOperationCount() < limit &&
        CharacterDatabase.GetPendingResultCount() < limit &&
        LoginDatabase.GetPendingAsyncOperationCount() < limit &&
        LoginDatabase.GetPendingResultCount() < limit);
}

bool BotManager::AddRandomBot(uint32_t accountId, ::ObjectGuid guid)
{
    // Background admission yields to pending SQL/callback work. Explicit human
    // AddBotWithMaster requests keep their existing interactive path.
    if (!HasRandomAdmissionCapacity())
        return false;
    bool ok = AddBotWithMaster(accountId, guid, ::ObjectGuid());
    if (ok)
        if (BotRecord* record = FindBot(guid))
            record->random = true;
    return ok;
}

bool BotManager::AddBotWithMaster(uint32_t accountId, ::ObjectGuid guid, ::ObjectGuid masterGuid)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it != m_bots.end())
    {
        TB_LOG_DETAIL("TortoiseBots: AddBot guid %s already tracked (state %u enteredWorld %u)",
            guid.GetString().c_str(), static_cast<uint32_t>(it->second.record.lifecycle), it->second.record.enteredWorld);
        return false;
    }

    HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(guid);
    if (sObjectAccessor.FindPlayer(guid) ||
        state != HeadlessSessionState::NotFound)
    {
        sLog.outError("TortoiseBots: AddBot guid %s rejected because the character already has a live or pending session (state %u)",
            guid.GetString().c_str(), static_cast<uint32>(state));
        return false;
    }

    if (!BotSessionAdapter::StartHeadlessSession(accountId, guid))
        return false;

    BotEntry entry;
    entry.record.generation = ++m_recordGeneration;
    entry.record.accountId = accountId;
    entry.record.characterGuid = guid;
    entry.record.masterGuid = masterGuid;
    entry.record.lifecycle = BotLifecycle::PendingAdd;
    m_bots.emplace(key, std::move(entry));
    if (!masterGuid.IsEmpty())
        BotActivityLeaseManager::Instance().ClaimForMaster(key);
    TB_LOG_DETAIL("TortoiseBots: AddBot %s on acct %u master %s (PendingAdd, StartHeadlessSession)",
        guid.GetString().c_str(), accountId, masterGuid.GetString().c_str());
    return true;
}

bool BotManager::RegisterOwnedCharacter(uint32_t ownerAccountId, uint32_t characterAccountId,
    ::ObjectGuid characterGuid, ::ObjectGuid masterGuid)
{
    if (!ownerAccountId || characterGuid.IsEmpty())
        return false;

    bool stored = CharacterDatabase.PExecute(
        "REPLACE INTO `tortoise_bots_owned_character` "
        "(`character_guid`, `owner_account_id`, `character_account_id`, `master_guid`) "
        "VALUES ('%u', '%u', '%u', '%u')",
        characterGuid.GetCounter(), ownerAccountId, characterAccountId,
        masterGuid.IsEmpty() ? 0 : masterGuid.GetCounter());
    if (!stored)
    {
        sLog.outError("TortoiseBots: could not persist ownership for character %s (owner account %u)",
            characterGuid.GetString().c_str(), ownerAccountId);
        return false;
    }

    if (BotRecord* record = FindBot(characterGuid))
        record->ownerAccountId = ownerAccountId;
    return true;
}

bool BotManager::GetOwnedCharacter(::ObjectGuid characterGuid, OwnedCharacter& result)
{
    std::unique_ptr<QueryResult> query(CharacterDatabase.PQuery(
        "SELECT `owner_account_id`, `character_account_id`, `character_guid`, `master_guid` "
        "FROM `tortoise_bots_owned_character` WHERE `character_guid` = '%u' LIMIT 1",
        characterGuid.GetCounter()));
    if (!query)
        return false;

    Field* fields = query->Fetch();
    result.ownerAccountId = fields[0].GetUInt32();
    result.characterAccountId = fields[1].GetUInt32();
    result.characterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[2].GetUInt32());
    result.masterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
    return true;
}

std::vector<OwnedCharacter> BotManager::GetOwnedCharacters(uint32_t ownerAccountId)
{
    std::vector<OwnedCharacter> result;
    if (!ownerAccountId)
        return result;

    // Same-account characters are owned candidates even before their first
    // Headless login; explicit rows extend the roster to GM-owned accounts.
    std::unique_ptr<QueryResult> query(CharacterDatabase.PQuery(
        "SELECT COALESCE(o.`owner_account_id`, c.`account`), c.`account`, c.`guid`, "
        "COALESCE(o.`master_guid`, 0), c.`name`, c.`class`, c.`online`, c.`map`, "
        "c.`zone`, c.`position_x`, c.`position_y`, c.`position_z` "
        "FROM `characters` c "
        "LEFT JOIN `tortoise_bots_owned_character` o "
        "ON o.`character_guid` = c.`guid` AND o.`owner_account_id` = '%u' "
        "WHERE c.`deleteDate` IS NULL "
        "AND (c.`account` = '%u' OR o.`character_guid` IS NOT NULL) "
        "ORDER BY c.`name`, c.`guid`",
        ownerAccountId, ownerAccountId));
    if (!query)
        return result;

    do
    {
        Field* fields = query->Fetch();
        OwnedCharacter row;
        row.ownerAccountId = fields[0].GetUInt32();
        row.characterAccountId = fields[1].GetUInt32();
        row.characterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[2].GetUInt32());
        row.masterGuid = ::ObjectGuid(HIGHGUID_PLAYER, fields[3].GetUInt32());
        row.name = fields[4].GetString();
        row.classId = static_cast<uint8_t>(fields[5].GetUInt32());
        row.characterOnline = fields[6].GetUInt32() != 0;
        row.mapId = fields[7].GetUInt32();
        row.zoneId = fields[8].GetUInt32();
        row.positionX = fields[9].GetFloat();
        row.positionY = fields[10].GetFloat();
        row.positionZ = fields[11].GetFloat();
        result.push_back(std::move(row));
    } while (query->NextRow());
    return result;
}

void BotManager::DrainPendingBotRemovals()
{
    if (m_inBotUpdate || BotWorldActions::IsMapExecution())
        return;
    for (auto const& pending : m_pendingBotRemovals.Take())
    {
        auto found = m_bots.find(pending.guid);
        if (found != m_bots.end() && found->second.record.generation == pending.generation)
            RemoveBot(found->second.record.characterGuid, pending.save);
    }
}

bool BotManager::RemoveBot(::ObjectGuid guid, bool save)
{
    uint32_t key = guid.GetCounter();
    auto it = m_bots.find(key);
    if (it == m_bots.end())
        return false;

    BotRecord& rec = it->second.record;
    // Map execution may request removal, but never changes the world-owned
    // registry/leases or stops a session while any joined AI stack is live.
    if (BotWorldActions::IsMapExecution())
        return m_pendingBotRemovals.Push(key, rec.generation, save);
    // Reentrant removal from inside an AI update (UpdateBots sets
    // m_inBotUpdate): stopping the session now would run the logout hooks
    // synchronously, delete the updating PlayerbotAI out from under its own
    // UpdateAI stack, and erase this entry. Mark Removing, queue the stop for
    // the post-update drain, and return while every object is still alive.
    if (m_inBotUpdate)
    {
        rec.lifecycle = BotLifecycle::Removing;
        BotActivityLeaseManager::Instance().Release(key, BotActivity::Grinding);
        m_pendingBotRemovals.Push(key, rec.generation, save);
        TB_LOG_DETAIL("TortoiseBots: RemoveBot %s deferred until AI update completes (Removing)",
            guid.GetString().c_str());
        return true;
    }

    rec.lifecycle = BotLifecycle::Removing;
    // Grinding is indefinite with no timeout: clear it on logout so no ghost
    // lease lingers. Other activities are service-owned (LFT/BG/Trading via
    // Reconcile/Prune, PlayerMaster via Clear/ReleaseToClient).
    BotActivityLeaseManager::Instance().Release(key, BotActivity::Grinding);

    // Request core to stop the headless session while record remains so
    // PlayerScript logout hooks can shut down AI.
    BotSessionAdapter::StopHeadlessSession(guid, save);

    // If core already reports NotFound, erase immediately.
    if (BotSessionAdapter::GetHeadlessSessionState(guid) == HeadlessSessionState::NotFound)
    {
        // Check human reclaim: if player now has Network transport, core owns transfer.
        if (::Player* p = sObjectAccessor.FindPlayer(guid))
        {
            ::WorldSession* s = p->GetSession();
            if (s && s->HasNetworkTransport())
                TB_LOG_DETAIL("TortoiseBots: RemoveBot %s reclaimed by network — releasing", guid.GetString().c_str());
        }
        m_bots.erase(it);
        TB_LOG_DETAIL("TortoiseBots: RemoveBot %s immediate NotFound — erased", guid.GetString().c_str());
        return true;
    }

    TB_LOG_DETAIL("TortoiseBots: RemoveBot %s (Removing; erasure deferred until NotFound)", guid.GetString().c_str());
    return true;
}

BotRecord* BotManager::FindBot(::ObjectGuid guid)
{
    auto it = m_bots.find(guid.GetCounter());
    if (it == m_bots.end())
        return nullptr;
    return &it->second.record;
}

bool BotManager::IsBot(::ObjectGuid guid) const
{
    return m_bots.find(guid.GetCounter()) != m_bots.end();
}

bool BotManager::IsRandomBot(::ObjectGuid guid) const
{
    auto it = m_bots.find(guid.GetCounter());
    return it != m_bots.end() && it->second.record.random;
}

std::vector<::Player*> BotManager::GetBotsForMaster(::ObjectGuid masterGuid) const
{
    std::vector<::Player*> result;
    for (auto const& entry : m_bots)
    {
        if (entry.second.record.masterGuid != masterGuid)
            continue;

        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

std::vector<::Player*> BotManager::GetAllBots() const
{
    std::vector<::Player*> result;
    result.reserve(m_bots.size());
    for (auto const& entry : m_bots)
    {
        ::Player* player = sObjectAccessor.FindPlayer(entry.second.record.characterGuid);
        if (IsLiveHeadlessBot(entry.second, player))
            result.push_back(player);
    }
    return result;
}

bool BotManager::IsLiveHeadlessBot(BotEntry const& entry, ::Player* player) const
{
    if (!player || entry.record.lifecycle != BotLifecycle::InWorld || !player->IsInWorld())
        return false;

    ::WorldSession* session = player->GetSession();
    if (!session || !session->IsHeadless() || session->HasNetworkTransport())
        return false;
    if (!entry.aiAdapter || !entry.aiAdapter->IsUsable())
        return false;

    // Core owns Headless session; module bookkeeping is via BotRecord + AI.
    HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(entry.record.characterGuid);
    return state == HeadlessSessionState::Active || state == HeadlessSessionState::Loading;
}

bool BotManager::IsControllableBot(::Player* player) const
{
    if (!player)
        return false;

    auto it = m_bots.find(player->GetObjectGuid().GetCounter());
    if (it == m_bots.end() || !IsLiveHeadlessBot(it->second, player))
        return false;

    return BotSessionAdapter::GetHeadlessSessionState(it->second.record.characterGuid) ==
        HeadlessSessionState::Active;
}

bool BotManager::BindBotMaster(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (masterGuid.IsEmpty() || botGuid == masterGuid)
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    auto masterEntry = m_bots.find(masterGuid.GetCounter());
    bool moduleHeadlessMaster = master && masterEntry != m_bots.end() &&
        IsLiveHeadlessBot(masterEntry->second, master);
    if (!master || !master->IsInWorld() || !master->GetSession() ||
        (master->GetSession()->IsHeadless() && !moduleHeadlessMaster))
    {
        sLog.outError("TortoiseBots: cannot bind bot %s to missing or unsupported master %s",
            botGuid.GetString().c_str(), masterGuid.GetString().c_str());
        return false;
    }

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot bind bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = masterGuid;
    if (ai)
        it->second.aiAdapter->RebindMaster(master);
    // Human claim wins (issue #89): evict any background lease with active
    // cleanup, then lock to PlayerMaster. Non-binding whispers never reach
    // here; only master-binding paths (.bot add, invite, follow) do.
    BotActivityLeaseManager::Instance().ClaimForMaster(botGuid.GetCounter());

    return true;
}

bool BotManager::ClearBotMaster(::ObjectGuid botGuid)
{
    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = nullptr;
    if (bot)
    {
        if (!IsLiveHeadlessBot(it->second, bot))
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its live session is not module-owned Headless",
                botGuid.GetString().c_str());
            return false;
        }

        ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because mature PlayerbotAI is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
        if (!it->second.aiAdapter || !it->second.aiAdapter->IsInitialized())
        {
            sLog.outError("TortoiseBots: cannot clear master for bot %s because its module AI adapter is unavailable",
                botGuid.GetString().c_str());
            return false;
        }
    }

    it->second.record.masterGuid = ObjectGuid();
    if (ai)
        it->second.aiAdapter->DetachMaster();
    BotActivityLeaseManager::Instance().ReleaseMaster(botGuid.GetCounter());

    return true;
}

bool BotManager::SetBotFollow(::ObjectGuid botGuid, ::ObjectGuid masterGuid)
{
    if (!BindBotMaster(botGuid, masterGuid))
        return false;

    auto it = m_bots.find(botGuid.GetCounter());
    if (it == m_bots.end())
        return false;

    ::Player* bot = sObjectAccessor.FindPlayer(botGuid);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !IsLiveHeadlessBot(it->second, bot) || !ai)
    {
        sLog.outError("TortoiseBots: SetBotFollow bot %s rejected because Headless bot or mature AI is unavailable",
            botGuid.GetString().c_str());
        return false;
    }

    ::Player* master = sObjectAccessor.FindPlayer(masterGuid);
    ai::Event followEvent("follow", "", master);
    if (!ai->DoSpecificAction("follow chat shortcut", followEvent, true))
    {
        sLog.outError("TortoiseBots: mature follow action failed for bot %s",
            botGuid.GetString().c_str());
        return false;
    }

    TB_LOG_DETAIL("TortoiseBots: SetBotFollow bot %s -> master %s",
        botGuid.GetString().c_str(), masterGuid.GetString().c_str());
    return true;
}

void BotManager::SetAutoTestEnabled(bool enable, uint32_t accountId, ::ObjectGuid guid)
{
    m_autoTestEnabled = enable;
    m_autoTestAccount = accountId;
    m_autoTestGuid = guid;
    m_autoTestTicks = 0;
    m_autoState = AutoState::Idle;
    m_autoTestPassed = false;
    TB_LOG_BASIC("TortoiseBots: AutoTest %s acct %u guid %s",
        enable ? "enabled" : "disabled", accountId, guid.GetString().c_str());
}

void BotManager::SetPacketBridgeTestEnabled(bool enable, uint32_t accountId,
    ::ObjectGuid masterGuid, ::ObjectGuid botGuid)
{
    m_packetTestEnabled = enable;
    m_packetTestAccount = accountId;
    m_packetTestMasterGuid = masterGuid;
    m_packetTestBotGuid = botGuid;
    m_packetTestTicks = 0;
    m_packetTestStage = 0;
    TB_LOG_BASIC("TortoiseBots: PacketBridgeTest %s acct %u master %s bot %s",
        enable ? "enabled" : "disabled", accountId,
        masterGuid.GetString().c_str(), botGuid.GetString().c_str());
}

void BotManager::UpdateBots(uint32_t diff)
{
    DrainPendingBotRemovals();

    // Guard: AI updates below can request (their own or another bot's) removal.
    // RemoveBot defers the session stop while this is set; the queue drains
    // after the loop, when no PlayerbotAI Update remains on the stack.
    // RAII so an unexpected unwind still clears the flag; anything queued
    // drains at the end of the next successful pass.
    struct BotUpdateGuard
    {
        BotManager* manager;
        explicit BotUpdateGuard(BotManager* m) : manager(m) { manager->m_inBotUpdate = true; }
        ~BotUpdateGuard() { manager->m_inBotUpdate = false; }
    };
    BotUpdateGuard botUpdateGuard(this);

    std::vector<uint32_t> guids;
    guids.reserve(m_bots.size());
    for (auto const& kv : m_bots)
        guids.push_back(kv.first);

    for (uint32_t guidLow : guids)
    {
        auto it = m_bots.find(guidLow);
        if (it == m_bots.end())
            continue;

        BotEntry& entry = it->second;
        // A removal queued earlier in this same loop only takes effect at the
        // drain below, but the record is already Removing: never drive AI for
        // a bot whose session stop is pending.
        if (entry.record.lifecycle != BotLifecycle::InWorld)
            continue;

        // Headless players have no client to acknowledge a core near/far
        // teleport. The donor PlayerbotMgr drove this acknowledgement from its
        // session loop; this module owns that loop now, so do the same before
        // the normal AI usability gate. FindPlayer() intentionally excludes a
        // far-teleporting player, while the public NotInWorld lookup retains
        // the object long enough for HandleTeleportAck() to finish the move.
        ::Player* player = sObjectAccessor.FindPlayerNotInWorld(entry.record.characterGuid);
        if (player && player->GetSession() && player->GetSession()->IsHeadless() &&
            player->IsBeingTeleported() && entry.aiAdapter && entry.aiAdapter->IsInitialized())
        {
            // A far teleport can spend one tick in the core's pending queue
            // before ExecuteTeleportFar raises the ACK semaphore. Keep the
            // AI paused for that tick, but only acknowledge an actual near/far
            // transfer; acknowledging the pending marker would reset state
            // before the destination has been installed.
            PlayerbotAI* ai = entry.aiAdapter->GetAI();
            if ((player->IsBeingTeleportedNear() || player->IsBeingTeleportedFar()) && ai)
                ai->HandleTeleportAck();
            continue;
        }

        if (entry.aiAdapter && entry.aiAdapter->IsUsable())
        {
            // Consume the mature logout intent outside PlayerbotAI's stack.
            // The guard queues native session teardown until this pass joins.
            PlayerbotAI* ai = entry.aiAdapter->GetAI();
            if (ai && ai->GetShouldLogOut())
            {
                ai->SetShouldLogOut(false);
                RemoveBot(entry.record.characterGuid, true);
                continue;
            }
            // Individual AI runs only through the native map hook. World
            // maintenance retains teleport acknowledgements and logout intent.
        }
    }

    BotWorldActions::Instance().Drain();
    NativeGuildTrades::Update();

    m_inBotUpdate = false;

    DrainPendingBotRemovals();
}

void BotManager::OnWorldUpdate(uint32_t diff)
{
    // Core owns Headless session lifecycle (queue, LoginPlayer dispatch, reclaim).
    // Module only tracks BotRecord and polls adapter state. No pending promotion.
    for (auto it = m_bots.begin(); it != m_bots.end(); )
    {
        BotEntry& entry = it->second;
        BotRecord& rec = entry.record;
        ::Player* p = sObjectAccessor.FindPlayer(rec.characterGuid);
        HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(rec.characterGuid);

        if (rec.lifecycle == BotLifecycle::Removing)
        {
            // Ensure Stop was requested; if player was reclaimed by Network, core owns transfer.
            if (p && p->GetSession() && p->GetSession()->HasNetworkTransport())
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s reclaimed by network during removal — releasing",
                    rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            if (state == HeadlessSessionState::NotFound)
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s removal complete (NotFound)", rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            // Still pending/loading/active → keep record until NotFound.
            ++it;
            continue;
        }

        // Human reclaim detection via Network transport.
        if (p)
        {
            ::WorldSession* playerSess = p->GetSession();
            if (playerSess && playerSess->HasNetworkTransport())
            {
                TB_LOG_DETAIL("TortoiseBots: Bot %s reclaimed by network session acct %u — releasing",
                    rec.characterGuid.GetString().c_str(), playerSess->GetAccountId());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            if (p->IsInWorld())
            {
                if (!rec.enteredWorld)
                    OnPlayerLogin(p);

                // OnPlayerLogin owns the only promotion to InWorld. In
                // particular, do not overwrite Removing after an AI attach
                // failure just because the Player object is still present.
                if (rec.lifecycle == BotLifecycle::Removing)
                {
                    ++it;
                    continue;
                }

                if (rec.lifecycle != BotLifecycle::InWorld ||
                    !entry.aiAdapter || !entry.aiAdapter->IsUsable())
                {
                    sLog.outError("TortoiseBots: Bot %s lost usable PlayerbotAI; stopping the Headless session",
                        rec.characterGuid.GetString().c_str());
                    rec.enteredWorld = false;
                    rec.lifecycle = BotLifecycle::Removing;
                    BotSessionAdapter::StopHeadlessSession(rec.characterGuid, true);
                    ++it;
                    continue;
                }

                rec.enteredWorld = true;
                ++rec.ticksInWorld;
            }
            ++it;
            continue;
        }

        // No Player object; check core state.
        if (rec.lifecycle == BotLifecycle::PendingAdd)
        {
            if (state == HeadlessSessionState::NotFound)
            {
                sLog.outError("TortoiseBots: Bot %s login ended without a session (NotFound)",
                    rec.characterGuid.GetString().c_str());
                uint32_t guidLow = rec.characterGuid.GetCounter();
                it = m_bots.erase(it);
                BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
                BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
                continue;
            }
            // Still Pending/Loading → keep waiting; core will drive to Active.
            ++it;
            continue;
        }

        if (state == HeadlessSessionState::NotFound)
        {
            TB_LOG_DETAIL("TortoiseBots: Bot %s session ended (NotFound) — releasing",
                rec.characterGuid.GetString().c_str());
            uint32_t guidLow = rec.characterGuid.GetCounter();
            it = m_bots.erase(it);
            BotActivityLeaseManager::Instance().ClaimForMaster(guidLow);
            BotActivityLeaseManager::Instance().ReleaseMaster(guidLow);
            continue;
        }
        ++it;
    }

    // Refresh the non-random/controlled population view from native sessions
    // and records immediately before social/activity queries; it owns no sessions.
    sRandomBotFacade.SyncNativePlayers();
    UpdateBots(diff);

    if (m_autoTestEnabled)
        UpdateAutoTest(diff);

    if (m_packetTestEnabled)
        UpdatePacketBridgeTest(diff);
}

void BotManager::UpdatePacketBridgeTest(uint32_t diff)
{
    (void)diff;
    ++m_packetTestTicks;

    if (!m_packetTestAccount || m_packetTestMasterGuid.IsEmpty() ||
        m_packetTestBotGuid.IsEmpty() || m_packetTestMasterGuid == m_packetTestBotGuid)
    {
        sLog.outError("TortoiseBots: PacketBridgeTest invalid fixture");
        m_packetTestEnabled = false;
        return;
    }

    if (m_packetTestStage == 0)
    {
        if (!FindBot(m_packetTestMasterGuid) && !FindBot(m_packetTestBotGuid))
        {
            AddBot(m_packetTestAccount, m_packetTestMasterGuid);
            AddBotWithMaster(m_packetTestAccount, m_packetTestBotGuid, m_packetTestMasterGuid);
            m_packetTestStage = 1;
            m_packetTestTicks = 0;
        }
        else
        {
            sLog.outError("TortoiseBots: PacketBridgeTest fixture is already active");
            m_packetTestEnabled = false;
        }
        return;
    }

    Player* master = sObjectAccessor.FindPlayer(m_packetTestMasterGuid);
    Player* bot = sObjectAccessor.FindPlayer(m_packetTestBotGuid);
    auto cleanGiftFixture = [&]
    {
        bool clean = true;
        if (master && bot && master->GetTrader() == bot) master->TradeCancel(true);
        if (m_packetTestGiftCreated)
        {
            clean = master && bot && master->GetItemCount(117, true) + bot->GetItemCount(117, true) <= 10;
            if (clean)
            {
                master->DestroyItemCount(117, 10, true, false, true);
                bot->DestroyItemCount(117, 10, true, false, true);
                master->SaveInventoryAndGoldToDB();
                bot->SaveInventoryAndGoldToDB();
                m_packetTestGiftCreated = false;
            }
        }
        if (m_packetTestGuildId)
        {
            Guild* guild = sGuildMgr.GetGuildById(m_packetTestGuildId);
            if (guild && guild->GetName() == "TBPLAYNativeGift")
                guild->Disband();
            else if (guild)
                clean = false;
            m_packetTestGuildId = 0;
        }
        if (m_packetTestGroupId)
        {
            Group* group = master ? master->GetGroup() : (bot ? bot->GetGroup() : nullptr);
            if (group && group->GetId() == m_packetTestGroupId && group->GetMembersCount() == 2 &&
                group->IsMember(m_packetTestMasterGuid) && group->IsMember(m_packetTestBotGuid))
                group->Disband();
            else if (group)
                clean = false;
            m_packetTestGroupId = 0;
        }
        return clean && (!master || !master->GetGuildId()) && (!bot || !bot->GetGuildId());
    };

    if (m_packetTestStage == 1)
    {
        if (master && bot && master->IsInWorld() && bot->IsInWorld())
        {
            bool retainedStun = master->HasUnitState(UNIT_STAT_STUNNED) && !master->GetSession()->isLogingOut();
            sLog.outString("TortoiseBots: PacketBridgeTest stun retention %s", retainedStun ? "PASSED" : "FAILED");
            if (m_packetTestInjectedStun)
            {
                master->ClearUnitState(UNIT_STAT_STUNNED);
                m_packetTestInjectedStun = false;
            }
            // Recreate the GM-invisible state a GM account would restore on a
            // Headless character, then require the bot-login normalization to
            // remove every GM-facing flag before command processing continues.
            bot->SetGMVisible(false);
            uint32 const botGmFlags = PLAYER_EXTRA_GM_ON |
                PLAYER_EXTRA_GM_ACCEPT_TICKETS | PLAYER_EXTRA_GM_INVISIBLE |
                PLAYER_EXTRA_GM_CHAT | PLAYER_EXTRA_GM_DISABLE_SOCIAL;
            bool headlessPresentationPassed = NormalizeHeadlessGmPresentation(bot) &&
                bot->IsGMVisible() && !bot->IsGameMaster() &&
                !(bot->GetExtraFlags() & botGmFlags);
            if (!headlessPresentationPassed)
            {
                sLog.outError("TortoiseBots: PacketBridgeTest headless GM presentation FAILED");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }

            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            ChatHandler commandHandler(syntheticNetwork);
            std::string followCommand = "follow " + std::string(bot->GetName());
            std::string inviteCommand = "invite " + std::string(bot->GetName());
            bool commandSurfacePassed =
                BotCommands::HandleChatCommand(&commandHandler, "list") &&
                BotCommands::HandleChatCommand(&commandHandler, "stats") &&
                BotCommands::HandleChatCommand(&commandHandler, followCommand.c_str()) &&
                PlayerbotAIStorage::Instance().GetAI(bot) &&
                PlayerbotAIStorage::Instance().GetAI(bot)->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);

            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest native command surface %s — list/stats/follow dispatched through ChatHandler",
                commandSurfacePassed ? "PASSED" : "FAILED");

            bool immediateInvitePassed =
                BotCommands::HandleChatCommand(&commandHandler, inviteCommand.c_str()) &&
                master->GetGroup() && bot->GetGroup() == master->GetGroup() &&
                master->GetGroup()->IsMember(bot->GetObjectGuid());

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;

            if (!immediateInvitePassed)
            {
                sLog.outError("TortoiseBots: PacketBridgeTest immediate native invite FAILED");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }

            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest immediate native invite PASSED");
            PlayerbotAI* botAI = PlayerbotAIStorage::Instance().GetAI(bot);
            ai::Event worldEvent("native world-owner test", std::string(), master);
            bool queued = BotWorldActions::Instance().Enqueue(bot, "stay chat shortcut", worldEvent);
            if (queued != true || !botAI || botAI->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
            {
                sLog.outError("TortoiseBots: PacketBridgeTest world action admission FAILED");
                m_packetTestStage = 3;
                m_packetTestTicks = 0;
                return;
            }
            m_packetTestStage = 2;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 600)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest login timeout");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 2)
    {
        if (master && bot && master->GetGroup() && bot->GetGroup() == master->GetGroup() &&
            master->GetGroup()->IsMember(bot->GetObjectGuid()))
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest group invite/accept PASSED — mature PlayerbotAI joined group");
            PlayerbotAI* botAI = PlayerbotAIStorage::Instance().GetAI(bot);
            bool queuedActionPassed = botAI && botAI->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
            sLog.outString("TortoiseBots: PacketBridgeTest queued world action %s",
                queuedActionPassed ? "PASSED" : "FAILED");


            ai::Event leaveEvent("native continuation test", std::string(), bot);
            ai::ActionResult admission = ai::ACTION_RESULT_FAILED;
            {
                BotWorldActions::MapScope simulatedMapOwner;
                if (botAI && botAI->GetCurrentEngine())
                    admission = botAI->GetCurrentEngine()->ExecuteAction("leave", leaveEvent);
            }
            bool deferred = admission == ai::ACTION_RESULT_DEFERRED && bot->GetGroup() == master->GetGroup();
            sLog.outString("TortoiseBots: PacketBridgeTest deferred action admission %s",
                deferred ? "PASSED" : "FAILED");
            m_packetTestStage = 5;
            m_packetTestTicks = 0;

        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest group invite/accept FAILED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 5)
    {
        if (master && bot && (!bot->GetGroup() || m_packetTestTicks > 100))
        {
            sLog.outString("TortoiseBots: PacketBridgeTest deferred action completion %s",
                !bot->GetGroup() ? "PASSED" : "FAILED");
            // Exercise the native uninvite handler for cleanup. Incoming packet
            // delivery is intentionally not injected here: the strict runtime
            // proof must come from a real Network session through Penqle's
            // ServerScript::CanPacketReceive hook.
            WorldSession* originalSession = master->GetSession();
            WorldSession* syntheticNetwork = new WorldSession(
                m_packetTestAccount, nullptr, sAccountMgr.GetSecurity(m_packetTestAccount),
                time_t(0), LOCALE_enUS, "packet-test", 0, SessionTransport::Network);
            syntheticNetwork->SetPlayer(master);
            master->SetSession(syntheticNetwork);

            WorldPacket uninvite(CMSG_GROUP_UNINVITE);
            uninvite << bot->GetName();
            syntheticNetwork->HandleGroupUninviteOpcode(uninvite);

            master->SetSession(originalSession);
            syntheticNetwork->SetPlayer(nullptr);
            delete syntheticNetwork;

            // Default-off, disposable low-level fixture only. Exercise the same
            // native self-damage path as the core die command, never hardcore.
            // Restore the temporary random eligibility by record incarnation.
            bool revivalPassed = false;
            BotRecord* fixture = FindBot(m_packetTestBotGuid);
            if (fixture && bot->IsAlive() && bot->IsInWorld() &&
                !bot->IsHardcore() && bot->GetLevel() < 5 && !bot->InBattleGround() &&
                !bot->IsBeingTeleported() && IsControllableBot(bot))
            {
                struct RestoreRandomEligibility
                {
                    BotManager& manager;
                    ObjectGuid guid;
                    uint64_t generation;
                    bool random;
                    ~RestoreRandomEligibility()
                    {
                        if (BotRecord* current = manager.FindBot(guid))
                            if (current->generation == generation)
                                current->random = random;
                    }
                } restore{*this, m_packetTestBotGuid, fixture->generation, fixture->random};
                fixture->random = true;
                bot->DealDamage(bot, bot->GetHealth(), nullptr, DIRECT_DAMAGE,
                    SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
                bool const died = !bot->IsAlive();
                revivalPassed = died && sRandomBotFacade.Revive(bot) && bot->IsAlive() &&
                    bot->IsInWorld() && !bot->IsBeingTeleported();
            }
            sLog.outString("TortoiseBots: PacketBridgeTest native admin revival %s",
                revivalPassed ? "PASSED" : "FAILED");

            PlayerbotAI* masterAI = PlayerbotAIStorage::Instance().GetAI(master);
            PlayerbotAI* botAI = PlayerbotAIStorage::Instance().GetAI(bot);
            if (masterAI) masterAI->DoSpecificAction("stay chat shortcut", ai::Event(), true);
            if (botAI) botAI->DoSpecificAction("stay chat shortcut", ai::Event(), true);
            // The persisted fixture may be a ghost from earlier population
            // tests. Native trade correctly refuses dead actors; prepare both
            // in their data-defined starting area rather than a saved danger zone.
            if (!master->IsAlive() && !master->IsHardcore())
            {
                master->ResurrectPlayer(1.0f);
                if (master->IsAlive()) master->SpawnCorpseBones();
            }
            PlayerInfo const* start = sObjectMgr.GetPlayerInfo(master->GetRace(), master->GetClass());
            bool const moved = master->IsAlive() && start &&
                master->TeleportTo(start->mapId, start->positionX, start->positionY, start->positionZ, start->orientation) &&
                bot->TeleportTo(start->mapId, start->positionX + 1, start->positionY, start->positionZ, start->orientation);
            m_packetTestStage = moved ? 6 : 3;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 100)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest deferred action completion FAILED — fixture lost");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage >= 6 && m_packetTestStage <= 9)
    {
        auto failGift = [&](char const* reason)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest native guild trade FAILED at stage %u: %s", m_packetTestStage, reason);
            cleanGiftFixture();
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        };
        if (!master || !bot || m_packetTestTicks > 200 || !PlayerbotAIStorage::Instance().GetAI(master) ||
            !PlayerbotAIStorage::Instance().GetAI(bot)) { failGift("actor lifetime or timeout"); return; }
        if (!PlayerbotAI::IsSafe(master, bot) || master->GetDistance3dToCenter(bot) > TRADE_DISTANCE)
            return;
        if (m_packetTestStage == 6)
        {
            if (master->GetGuildId() || bot->GetGuildId() || sGuildMgr.GetGuildByName("TBPLAYNativeGift") ||
                master->GetItemCount(117, true) || bot->GetItemCount(117, true)) { failGift("fixture already owns guild or item"); return; }
            Guild* guild = new Guild;
            if (!guild->Create(master, "TBPLAYNativeGift")) { delete guild; failGift("native guild creation"); return; }
            sGuildMgr.AddGuild(guild);
            m_packetTestGuildId = guild->GetId();
            if (guild->AddMember(bot->GetObjectGuid(), guild->GetLowestRank()) != GuildAddStatus::OK) { failGift("native guild membership"); return; }
            m_packetTestGiftCreated = true;
            if (!master->StoreNewItemInBestSlots(117, 10)) { failGift("native item storage"); return; }
            Item* gift = nullptr;
            for (Item* item : PlayerbotAIStorage::Instance().GetAI(master)->GetInventoryItems())
                if (item->GetEntry() == 117 && item->GetCount() == 10) { gift = item; break; }
            char const* refusal = "missing ten-item stack";
            if (!gift || !NativeGuildTrades::Offer(master, bot, gift, 4, &refusal)) { failGift(refusal ? refusal : "partial offer admission"); return; }
            m_packetTestStage = 7;
            m_packetTestTicks = 0;
            return;
        }
        if (master->GetTradeData() || bot->GetTradeData()) return;
        if (m_packetTestStage == 7)
        {
            bool const conserved = master->GetItemCount(117, true) == 6 && bot->GetItemCount(117, true) == 4;
            sLog.outString("TortoiseBots: PacketBridgeTest native guild partial trade %s", conserved ? "PASSED" : "FAILED");
            if (!conserved) { failGift("partial item conservation"); return; }
            Item* gift = nullptr;
            for (Item* item : PlayerbotAIStorage::Instance().GetAI(master)->GetInventoryItems())
                if (item->GetEntry() == 117 && item->GetCount() == 6) { gift = item; break; }
            if (!gift || !NativeGuildTrades::Offer(master, bot, gift, 6)) { failGift("whole offer admission"); return; }
            m_packetTestStage = 8;
            m_packetTestTicks = 0;
            return;
        }
        if (m_packetTestStage == 8)
        {
            bool const conserved = !master->GetItemCount(117, true) && bot->GetItemCount(117, true) == 10;
            sLog.outString("TortoiseBots: PacketBridgeTest native guild whole trade %s", conserved ? "PASSED" : "FAILED");
            if (!conserved) { failGift("whole item conservation"); return; }
            bool const clean = cleanGiftFixture();
            sLog.outString("TortoiseBots: PacketBridgeTest native guild cleanup %s", clean ? "PASSED" : "FAILED");
            if (!clean) { failGift("fixture cleanup"); return; }
            if (master->GetGroup() || bot->GetGroup()) { failGift("pre-existing party"); return; }
            WorldPacket invite(CMSG_GROUP_INVITE); invite << bot->GetName();
            master->GetSession()->HandleGroupInviteOpcode(invite);
            WorldPacket accept(CMSG_GROUP_ACCEPT);
            bot->GetSession()->HandleGroupAcceptOpcode(accept);
            Group* group = master->GetGroup();
            if (!group || group != bot->GetGroup() || group->GetMembersCount() != 2) { failGift("native party creation"); return; }
            m_packetTestGroupId = group->GetId();
            m_packetTestGiftCreated = true;
            if (!master->StoreNewItemInBestSlots(117, 3)) { failGift("party item storage"); return; }
            Item* gift = nullptr;
            for (Item* item : PlayerbotAIStorage::Instance().GetAI(master)->GetInventoryItems())
                if (item->GetEntry() == 117 && item->GetCount() == 3) { gift = item; break; }
            if (!gift || !NativeGuildTrades::OfferParty(master, bot, gift, 3)) { failGift("native party offer"); return; }
            m_packetTestStage = 9;
            m_packetTestTicks = 0;
            return;
        }
        bool const conserved = !master->GetGuildId() && !bot->GetGuildId() &&
            !master->GetItemCount(117, true) && bot->GetItemCount(117, true) == 3;
        sLog.outString("TortoiseBots: PacketBridgeTest native party trade %s", conserved ? "PASSED" : "FAILED");
        if (!conserved) { failGift("party item conservation"); return; }
        bool const clean = cleanGiftFixture() && !master->GetGroup() && !bot->GetGroup();
        sLog.outString("TortoiseBots: PacketBridgeTest native party cleanup %s", clean ? "PASSED" : "FAILED");
        if (!clean) { failGift("party cleanup"); return; }
        Map* botMap = bot->GetMap();
        if (!botMap)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest missing bot map before stranded-session check");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
            return;
        }
        botMap->Remove(bot, false);
        sLog.outString("TortoiseBots: PacketBridgeTest forced an out-of-world Headless bot");
        m_packetTestStage = 4;
        m_packetTestTicks = 0;
        return;
    }

    if (m_packetTestStage == 4)
    {
        bool released = BotSessionAdapter::GetHeadlessSessionState(m_packetTestBotGuid) ==
            HeadlessSessionState::NotFound;
        bool materialized = sObjectAccessor.FindPlayerNotInWorld(m_packetTestBotGuid) != nullptr;
        if (released && !materialized)
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest stranded Headless recovery PASSED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest stranded Headless recovery FAILED");
            m_packetTestStage = 3;
            m_packetTestTicks = 0;
        }
        return;
    }

    if (m_packetTestStage == 3)
    {
        if (m_packetTestGuildId || m_packetTestGroupId || m_packetTestGiftCreated) cleanGiftFixture();
        if (FindBot(m_packetTestBotGuid))
            RemoveBot(m_packetTestBotGuid, true);
        if (FindBot(m_packetTestMasterGuid))
            RemoveBot(m_packetTestMasterGuid, true);

        if (!FindBot(m_packetTestBotGuid) && !FindBot(m_packetTestMasterGuid) &&
            BotSessionAdapter::GetHeadlessSessionState(m_packetTestBotGuid) == HeadlessSessionState::NotFound &&
            BotSessionAdapter::GetHeadlessSessionState(m_packetTestMasterGuid) == HeadlessSessionState::NotFound)
        {
            TB_LOG_BASIC("TortoiseBots: PacketBridgeTest cleanup PASSED");
            m_packetTestEnabled = false;
        }
        else if (m_packetTestTicks > 300)
        {
            sLog.outError("TortoiseBots: PacketBridgeTest cleanup timed out");
            m_packetTestEnabled = false;
        }
    }
}

void BotManager::FinishAutoTest(bool passed)
{
    m_autoTestPassed = passed;
    if (FindBot(m_autoTestGuid))
        RemoveBot(m_autoTestGuid, true);
    m_autoState = AutoState::CleaningUp;
    m_autoTestTicks = 0;
}

void BotManager::UpdateAutoTest(uint32_t diff)
{
    (void)diff;
    ++m_autoTestTicks;

    switch (m_autoState)
    {
        case AutoState::Idle:
            if (m_autoTestTicks > 20)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest step 1 — login bot %s", m_autoTestGuid.GetString().c_str());
                if (AddBot(m_autoTestAccount, m_autoTestGuid))
                {
                    m_autoState = AutoState::LoggingIn;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not queue its fixture");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::LoggingIn:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 2 — bot entered world, tick %u", rec->ticksInWorld);
                    m_autoState = AutoState::InWorld;
                    m_autoTestTicks = 0;
                }
                else
                {
                    if (m_autoTestTicks % 40 == 0)
                    {
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                        std::string sessInfo = (st != HeadlessSessionState::NotFound) ? "headless" : "<null>";
                        bool loading = (st == HeadlessSessionState::Loading || st == HeadlessSessionState::Pending);
                        std::string playerInfo = p ? (p->IsInWorld() ? "IsInWorld" : "not InWorld") : "FindPlayer null";
                        TB_LOG_DEBUG("TortoiseBots: AutoTest LoggingIn tick %u sess %s state %u acct %u loading %u player %s pending %u", m_autoTestTicks, sessInfo.c_str(), static_cast<uint32>(st), rec->accountId, loading, playerInfo.c_str(), st == HeadlessSessionState::Pending);
                        if (st != HeadlessSessionState::NotFound && p && p->GetSession())
                            TB_LOG_DEBUG("TortoiseBots:   sess details network %u headless %u", p->GetSession()->HasNetworkTransport(), p->GetSession()->IsHeadless());
                    }
                    if (m_autoTestTicks > 400)
                    {
                        ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                        HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                        sLog.outError("TortoiseBots: AutoTest login timeout after %u ticks (state %u player %s)", m_autoTestTicks, static_cast<uint32>(st), p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                        FinishAutoTest(false);
                    }
                }
            }
            else
            {
                sLog.outError("TortoiseBots: AutoTest LoggingIn but FindBot null tick %u", m_autoTestTicks);
                if (m_autoTestTicks > 400)
                    FinishAutoTest(false);
            }
            break;
        case AutoState::InWorld:
            if (m_autoTestTicks > 600)
            {
                if (BotRecord* rec = FindBot(m_autoTestGuid))
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    if (p)
                    {
                        p->SaveToDB(false, false);
                        TB_LOG_BASIC("TortoiseBots: AutoTest step 3 — saved bot %s", p->GetName());
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest expected an in-world fixture before save");
                        FinishAutoTest(false);
                        break;
                    }
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest lost its fixture before save");
                    FinishAutoTest(false);
                    break;
                }
                m_autoState = AutoState::Saving;
                m_autoTestTicks = 0;
            }
            break;
        case AutoState::Saving:
            if (m_autoTestTicks > 20)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest step 4 — logout bot");
                if (RemoveBot(m_autoTestGuid, true))
                {
                    m_autoState = AutoState::LoggingOut;
                    m_autoTestTicks = 0;
                }
                else
                {
                    sLog.outError("TortoiseBots: AutoTest could not request fixture logout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::LoggingOut:
            if (m_autoTestTicks > 40)
            {
                if (!sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                    !FindBot(m_autoTestGuid) &&
                    BotSessionAdapter::GetHeadlessSessionState(m_autoTestGuid) == HeadlessSessionState::NotFound)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 5 — re-login bot");
                    if (AddBot(m_autoTestAccount, m_autoTestGuid))
                    {
                        m_autoState = AutoState::Relogging;
                        m_autoTestTicks = 0;
                    }
                    else
                    {
                        sLog.outError("TortoiseBots: AutoTest could not queue fixture relogin");
                        FinishAutoTest(false);
                    }
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest logout timeout");
                    FinishAutoTest(false);
                }
            }
            break;
        case AutoState::Relogging:
            if (BotRecord* rec = FindBot(m_autoTestGuid))
            {
                if (rec->enteredWorld)
                {
                    TB_LOG_BASIC("TortoiseBots: AutoTest step 6 — bot re-entered world, lifecycle PASSED; cleaning up");
                    FinishAutoTest(true);
                }
                else if (m_autoTestTicks > 400)
                {
                    sLog.outError("TortoiseBots: AutoTest relog timeout");
                    FinishAutoTest(false);
                }
                else if (m_autoTestTicks % 40 == 0)
                {
                    ::Player* p = sObjectAccessor.FindPlayer(m_autoTestGuid);
                    HeadlessSessionState st = BotSessionAdapter::GetHeadlessSessionState(rec->characterGuid);
                    TB_LOG_DEBUG("TortoiseBots: AutoTest Relogging tick %u state %u pending %u player %s", m_autoTestTicks, static_cast<uint32>(st), st == HeadlessSessionState::Pending, p ? (p->IsInWorld() ? "IsInWorld" : "notInWorld") : "null");
                }
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest relog lost its fixture");
                FinishAutoTest(false);
            }
            break;
        case AutoState::CleaningUp:
            if (!FindBot(m_autoTestGuid) &&
                !sObjectAccessor.FindPlayer(m_autoTestGuid) &&
                BotSessionAdapter::GetHeadlessSessionState(m_autoTestGuid) == HeadlessSessionState::NotFound)
            {
                TB_LOG_BASIC("TortoiseBots: AutoTest cleanup %s; diagnostic disabled",
                    m_autoTestPassed ? "PASSED" : "FAILED");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            else if (m_autoTestTicks > 400)
            {
                sLog.outError("TortoiseBots: AutoTest cleanup timed out; diagnostic disabled with lifecycle state still active");
                m_autoTestEnabled = false;
                m_autoState = AutoState::Done;
            }
            break;
        case AutoState::Done:
            break;
    }
}

} // namespace TortoiseBots
