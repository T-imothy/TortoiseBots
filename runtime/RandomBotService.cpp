#include "PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "RandomBotService.h"
#include "BotActivityLease.h"

#include "BotManager.h"
#include "GearSeedingGuard.h"
#include "../host/BotSessionAdapter.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Map.h"
#include "LFT/LFTMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "AccountMgr.h"
#include "SharedDefines.h"
#include "Database/DBCStores.h"
#include "Util.h"
#include "../host/ModuleLog.h"

#if __has_include("Handlers/CharacterCreation.h")
#include "Handlers/CharacterCreation.h"
#elif __has_include("CharacterCreation.h")
#include "CharacterCreation.h"
#else
// Feature 01 requires core PR #416
// src/game/Handlers/CharacterCreation.h. If this fires, the module is built
// against a core without the generic synchronous CharacterCreation seam.
#error "TortoiseBots feature 01 requires core PR #416 (Handlers/CharacterCreation.h not found)"
#endif

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <memory>
#include <random>
#include <set>
#if __has_include(<openssl/rand.h>)
#include <openssl/rand.h>
#endif

namespace TortoiseBots
{

namespace
{
std::string GenerateRndBotName()
{
    // Pronounceable 2-3 syllable fantasy names (consonant onset + vowel nucleus,
    // optional coda). Letters only, first upper -> passes CheckPlayerName.
    // Uniqueness via DB (GetPlayerGuidByName + core NAME_IN_USE) with retry.
    static const char* onset[] = {
        "b","br","c","cr","d","dr","f","g","gr","h","j","k","kr","l","m","n",
        "p","r","s","sh","st","t","th","tr","v","w","z","kh","gh","mor","thar","zar"
    };
    static const char* nucleus[] = { "a","e","i","o","u","ae","ar","or","ur","al","yr" };
    static const char* coda[]    = { "","","","n","r","k","l","th","sh","g","rn" };
    uint32 const nOn = sizeof(onset)   / sizeof(onset[0]);
    uint32 const nNu = sizeof(nucleus) / sizeof(nucleus[0]);
    uint32 const nCo = sizeof(coda)    / sizeof(coda[0]);
    uint32 const syllables = urand(2, 3);
    std::string name;
    name.reserve(12);
    for (uint32 i = 0; i < syllables; ++i)
    {
        name += onset[urand(0, nOn - 1)];
        name += nucleus[urand(0, nNu - 1)];
    }
    name += coda[urand(0, nCo - 1)];
    if (name.size() > 11)
        name.resize(11);
    name[0] = char(name[0] & ~0x20); // uppercase first letter, ASCII a-z
    return name;
}

std::string GenerateRandomPassword()
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    constexpr size_t kLen = 12;
    constexpr size_t kCharsetSize = sizeof(charset) - 1;
    static_assert(kLen <= MAX_ACCOUNT_STR, "password must fit MAX_ACCOUNT_STR");
    // Never log/store plaintext outside immediate AccountMgr::CreateAccount call;
    // core hashes via CalculateShaPassHash. Prefer secure OS/stdlib RNG over game urand.
    std::string pw;
    pw.reserve(kLen);
    try
    {
        std::random_device rd;
        std::uniform_int_distribution<size_t> dist(0, kCharsetSize - 1);
        for (size_t i = 0; i < kLen; ++i)
            pw.push_back(charset[dist(rd)]);
        if (pw.size() == kLen)
            return pw;
    }
    catch (...)
    {
    }
#if __has_include(<openssl/rand.h>)
    {
        unsigned char buf[kLen];
        if (RAND_bytes(buf, static_cast<int>(kLen)) == 1)
        {
            pw.clear();
            for (size_t i = 0; i < kLen; ++i)
                pw.push_back(charset[buf[i] % kCharsetSize]);
            if (pw.size() == kLen)
                return pw;
        }
    }
#endif
    // Safe bounded fallback: game MTRand, still within length/charset bounds.
    pw.clear();
    for (size_t i = 0; i < kLen; ++i)
        pw.push_back(charset[urand(0, uint32(kCharsetSize - 1))]);
    return pw;
}
} // namespace

RandomBotService& RandomBotService::Instance()
{
    static RandomBotService instance;
    return instance;
}

void RandomBotService::Initialize()
{
    if (m_initialized)
        return;

    m_initialized = true;
    m_serviceElapsedMs = 0;
    m_targetReady = false;
    m_activity = BotActivityController{};
    m_pinnedGuids.clear();
    m_pinnedResolved = false;
    m_rndBotAccountIds.clear();
    m_failedAutoCreateAccounts.clear();
    m_freshAutoCreateDisabled = false;
    m_autoCreateNoValidData = false;
    m_accountAllocNextRetry = 0;
    m_charCreateErrorNextRetry = 0;
    m_pendingAccountName.clear();
    m_pendingNextRetry = 0;
    m_pendingSince = 0;
    m_pendingStaleLogged = false;
    m_resetPending = false;
    m_resetFailed = false;
    m_nextResetAccount = 0;
    m_resetAccountIds.clear();
    m_initializedThisRun.clear();
    if (!sPlayerbotAIConfig.enabled)
    {
        TB_LOG_BASIC("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }
    if (sPlayerbotAIConfig.deleteRandomBotAccounts)
    {
        if (!PrepareAccountReset())
        {
            m_resetFailed = true;
            sLog.outError("TortoiseBots: random-account reset refused; population service will not start");
            return;
        }
        if (m_resetPending)
            return;
    }
    // Load pool even if autologin is off when auto-create is on: the deficit
    // check needs the real candidate set.
    if (!sPlayerbotAIConfig.randomBotAutologin && !sPlayerbotAIConfig.randomBotAutoCreate)
    {
        TB_LOG_BASIC("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }

    LoadCandidates();
    RefreshPopulationTarget();
    m_started = sPlayerbotAIConfig.randomBotLoginAtStartup &&
        (!sPlayerbotAIConfig.randomBotLoginWithPlayer || m_humanSessions > 0);

    TB_LOG_BASIC("TortoiseBots: native random-bot pool loaded (%u candidates, target %u, startup %u, autoCreate %u)",
        static_cast<uint32>(m_candidates.size()), m_targetCount, m_started, sPlayerbotAIConfig.randomBotAutoCreate ? 1 : 0);
}

bool RandomBotService::PrepareAccountReset()
{
    // Only accounts with the exact six-digit suffix emitted by this service
    // are eligible. A wildcard, altered prefix, or ambiguous account aborts
    // rather than widening a destructive reset to human-owned accounts.
    std::string const& prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty() || prefix.size() + 6 > MAX_ACCOUNT_STR ||
        !std::all_of(prefix.begin(), prefix.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        }))
        return false;

    std::string escaped = prefix;
    LoginDatabase.escape_string(escaped);
    std::unique_ptr<QueryResult> count(LoginDatabase.PQuery(
        "SELECT COUNT(*) FROM account WHERE username LIKE '%s%%'", escaped.c_str()));
    if (!count)
        return false;
    if (!count->Fetch()[0].GetUInt64())
        return true;

    std::unique_ptr<QueryResult> rows(LoginDatabase.PQuery(
        "SELECT id, username FROM account WHERE username LIKE '%s%%' ORDER BY id", escaped.c_str()));
    if (!rows)
        return false;
    do
    {
        Field* fields = rows->Fetch();
        std::string const name = fields[1].GetCppString();
        if (name.size() != prefix.size() + 6 ||
            !std::equal(prefix.begin(), prefix.end(), name.begin(), [](char a, char b) {
                if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
                if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
                return a == b;
            }) ||
            !std::all_of(name.begin() + prefix.size(), name.end(), [](char c) {
                return c >= '0' && c <= '9';
            }))
        {
            sLog.outError("TortoiseBots: reset refused: prefix also matches non-generated account %s", name.c_str());
            m_resetAccountIds.clear();
            return false;
        }
        m_resetAccountIds.push_back(fields[0].GetUInt32());
    } while (rows->NextRow());
    m_resetPending = !m_resetAccountIds.empty();
    sLog.outString("TortoiseBots: configured one-start reset queued for %u generated accounts; set DeleteRandomBotAccounts back to 0 after this start",
        static_cast<uint32>(m_resetAccountIds.size()));
    return true;
}

void RandomBotService::ProgressAccountReset()
{
    if (!m_resetPending || m_resetFailed)
        return;
    if (m_nextResetAccount < m_resetAccountIds.size())
    {
        uint32 const accountId = m_resetAccountIds[m_nextResetAccount];
        if (sWorld.FindSession(accountId))
        {
            m_resetFailed = true;
            sLog.outError("TortoiseBots: reset stopped: account %u has a network session", accountId);
            return;
        }
        std::unique_ptr<QueryResult> owned(CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account=%u", accountId));
        if (owned)
        {
            if (m_resetCharacterAccount != accountId)
            {
                do
                {
                    ObjectGuid guid(HIGHGUID_PLAYER, owned->Fetch()[0].GetUInt32());
                    Player::DeleteFromDB(guid, accountId, false, true);
                } while (owned->NextRow());
                m_resetCharacterAccount = accountId;
            }
            // Observe completion before AccountMgr queries these same characters.
            return;
        }
        if (sAccountMgr.DeleteAccount(accountId) != AOR_OK)
        {
            m_resetFailed = true;
            sLog.outError("TortoiseBots: random-account reset stopped at account %u; population remains disabled", accountId);
            return;
        }
        ++m_nextResetAccount;
        m_resetCharacterAccount = 0;
        return; // One native account deletion per cadence; never a startup-long loop.
    }

    // AccountMgr uses asynchronous database writes. Do not load candidates or
    // begin creating a replacement cohort until both databases have observed
    // removal of the old account/character ownership.
    std::string escaped = sPlayerbotAIConfig.randomBotAccountPrefix;
    LoginDatabase.escape_string(escaped);
    std::unique_ptr<QueryResult> accounts(LoginDatabase.PQuery(
        "SELECT COUNT(*) FROM account WHERE username LIKE '%s%%'", escaped.c_str()));
    if (!accounts || accounts->Fetch()[0].GetUInt64())
        return;
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM characters WHERE account IN (";
    for (size_t i = 0; i < m_resetAccountIds.size(); ++i)
    {
        if (i) sql << ',';
        sql << m_resetAccountIds[i];
    }
    sql << ')';
    std::unique_ptr<QueryResult> characters(CharacterDatabase.Query(sql.str().c_str()));
    if (!characters || characters->Fetch()[0].GetUInt64())
        return;

    m_resetPending = false;
    m_resetAccountIds.clear();
    LoadCandidates();
    RefreshPopulationTarget();
    m_started = sPlayerbotAIConfig.randomBotLoginAtStartup &&
        (!sPlayerbotAIConfig.randomBotLoginWithPlayer || m_humanSessions > 0);
    sLog.outString("TortoiseBots: random-account reset complete; replacement population may now be created");
}

uint32_t RandomBotService::QueueAdminAction(RandomBotAdminAction action, std::string selector)
{
    if (!m_initialized || !sPlayerbotAIConfig.enabled || selector.empty()) return 0;
    bool const all = selector == "all" || selector == "%";
    if (!all && !normalizePlayerName(selector)) return 0;
    uint32_t queued = 0;
    for (Player* player : BotManager::Instance().GetAllBots())
    {
        if (!player || !BotManager::Instance().IsRandomBot(player->GetObjectGuid()) ||
            (!all && std::string(player->GetName()).rfind(selector, 0) != 0)) continue;
        auto const* record = BotManager::Instance().FindBot(player->GetObjectGuid());
        if (!record || !record->generation) continue;
        auto key = std::make_pair(player->GetGUIDLow(), action);
        if (!m_adminKeys.insert(key).second) continue;
        m_adminRequests.push_back({player->GetObjectGuid(), action, record->generation});
        ++queued;
    }
    return queued;
}

void RandomBotService::RequestUpdate()
{
    m_serviceElapsedMs = std::max<uint32_t>(100, sPlayerbotAIConfig.randomBotUpdateInterval);
}

bool RandomBotService::ResetPersistentState()
{
    if (!m_initialized || !sPlayerbotAIConfig.enabled || !sRandomBotFacade.ResetPersistentValues()) return false;
    m_adminRequests.clear();
    m_adminKeys.clear();
    m_targetReady = false;
    RequestUpdate();
    return true;
}

void RandomBotService::ProcessAdminActions()
{
    uint32_t const started = WorldTimer::getMSTime();
    uint32_t const limit = std::max<uint32_t>(1, sPlayerbotAIConfig.randomBotMaintenanceBatch);
    for (uint32_t count = 0; count < limit && !m_adminRequests.empty(); ++count)
    {
        if (count && WorldTimer::getMSTimeDiff(started, WorldTimer::getMSTime()) >=
            sPlayerbotAIConfig.randomBotMaintenanceBudgetMs) break;
        AdminRequest request = m_adminRequests.front();
        m_adminRequests.pop_front();
        m_adminKeys.erase({request.guid.GetCounter(), request.action});
        Player* player = sObjectAccessor.FindPlayer(request.guid);
        auto const* record = BotManager::Instance().FindBot(request.guid);
        // Human reclaim, logout, removal or failed attachment cancels the work.
        if (!player || !record || record->generation != request.generation ||
            !BotManager::Instance().IsRandomBot(request.guid) ||
            !BotManager::Instance().IsControllableBot(player) || player->IsBeingTeleported()) continue;
        std::string const name = player->GetName();
        switch (request.action)
        {
            case RandomBotAdminAction::Teleport:
            case RandomBotAdminAction::Rpg:
            case RandomBotAdminAction::Grind:
                if (!BotManager::Instance().RelocateRandomBot(player,
                    request.action == RandomBotAdminAction::Rpg ? RandomBotDestination::Rpg :
                    request.action == RandomBotAdminAction::Grind ? RandomBotDestination::LocalGrind : RandomBotDestination::Level))
                {
                    sLog.outString("TortoiseBots: skipped random-bot relocation for %s (eligibility or no validated destination)", name.c_str());
                    continue;
                }
                break;
            case RandomBotAdminAction::Initialize:
                if (!sRandomBotFacade.InitializeBot(player))
                {
                    sLog.outString("TortoiseBots: skipped random-bot initialization for %s (native eligibility)", name.c_str());
                    continue;
                }
                break;
            case RandomBotAdminAction::Refresh:
                if (!sRandomBotFacade.Refresh(player))
                {
                    sLog.outString("TortoiseBots: skipped random-bot refresh for %s (native eligibility or recovery rejected)", name.c_str());
                    continue;
                }
                break;
            case RandomBotAdminAction::Upgrade: sRandomBotFacade.UpdateGearSpells(player); break;
            case RandomBotAdminAction::Revive:
                if (!sRandomBotFacade.Revive(player))
                {
                    sLog.outString("TortoiseBots: skipped random-bot revival for %s (native eligibility or recovery rejected)", name.c_str());
                    continue;
                }
                break;
            case RandomBotAdminAction::ChangeStrategy: sRandomBotFacade.ChangeStrategy(player); break;
            case RandomBotAdminAction::Remove: BotManager::Instance().RemoveBot(request.guid, true); break;
        }
        // Removal can destroy Player; use only the name copied beforehand.
        sLog.outString("TortoiseBots: completed queued random-bot admin action %u for %s",
            static_cast<unsigned>(request.action), name.c_str());
    }
}
// End bounded random-bot admin requests.

void RandomBotService::LoadCandidates()
{
    m_candidates.clear();
    m_ageMs.clear();
    m_strategyAgeMs.clear();
    m_randomizeAgeMs.clear();
    m_rndBotAccountIds.clear();
    m_nextCandidate = 0;
    m_nextMaintenance = 0;
    m_nextRemoval = 0;

    std::set<uint32> accountIds;
    std::unique_ptr<QueryResult> accounts(LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '%s%%'",
        sPlayerbotAIConfig.randomBotAccountPrefix.c_str()));
    if (!accounts)
        return;

    do
    {
        Field* fields = accounts->Fetch();
        uint32 accountId = fields[0].GetUInt32();
        if (accountId)
        {
            accountIds.insert(accountId);
            sPlayerbotAIConfig.IsInRandomAccountList(accountId);
        }
    } while (accounts->NextRow());

    m_rndBotAccountIds.assign(accountIds.begin(), accountIds.end());

    std::set<uint32> characterIds;
    for (uint32 accountId : accountIds)
    {
        std::unique_ptr<QueryResult> characters(CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account = '%u' ORDER BY guid", accountId));
        if (!characters)
            continue;

        do
        {
            Field* fields = characters->Fetch();
            uint32 guidLow = fields[0].GetUInt32();
            if (!guidLow || !characterIds.insert(guidLow).second)
                continue;

            m_candidates.push_back({accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow)});
        } while (characters->NextRow());
    }

    // Rotate the stable DB order so the first startup does not always select
    // the same low GUIDs. The candidate pool is seeded from the DB at startup
    // and grows only via bounded auto-create appends; there is no per-tick
    // query or full-world scan.
    if (!m_candidates.empty())
    {
        size_t offset = static_cast<size_t>(std::time(nullptr)) % m_candidates.size();
        std::rotate(m_candidates.begin(), m_candidates.begin() + offset, m_candidates.end());
    }

    m_ageMs.assign(m_candidates.size(), 0);
    m_strategyAgeMs.assign(m_candidates.size(), 0);
    m_randomizeAgeMs.assign(m_candidates.size(), 0);
}

void RandomBotService::RefreshPopulationTarget()
{
    uint32 const minimum = std::min(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    uint32 const maximum = std::max(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    uint32 desired = sRandomBotFacade.GetValue(uint32(0), "bot_count");
    bool const selectedZero = m_targetReady && m_desiredTargetCount == 0 && minimum == 0;
    if (desired < minimum || desired > maximum || (!desired && maximum && !selectedZero))
    {
        desired = DesiredTargetCount();
        sRandomBotFacade.SetValue(uint32(0), "bot_count", desired);
    }
    else if (!maximum && desired)
    {
        desired = 0;
        sRandomBotFacade.SetValue(uint32(0), "bot_count", 0);
    }
    m_desiredTargetCount = desired;
    m_targetCount = sPlayerbotAIConfig.randomBotAutoCreate ? desired :
        std::min<uint32>(desired, uint32(m_candidates.size()));
    m_targetReady = true;
}

uint32 RandomBotService::DesiredTargetCount() const
{
    return urand(std::min(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots),
        std::max(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots));
}

void RandomBotService::RemoveSurplusBots(uint32_t online)
{
    if (online <= m_targetCount || m_candidates.empty()) return;
    uint32 const budget = std::min(online - m_targetCount,
        std::max<uint32>(1, sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval));
    uint32 const started = WorldTimer::getMSTime();
    uint32 removed = 0;
    size_t const limit = std::min<size_t>(m_candidates.size(), sPlayerbotAIConfig.randomBotMaintenanceBatch);
    for (size_t examined = 0; examined < limit && removed < budget; ++examined)
    {
        if (examined && WorldTimer::getMSTimeDiff(started, WorldTimer::getMSTime()) >=
            sPlayerbotAIConfig.randomBotMaintenanceBudgetMs) break;
        Candidate const candidate = m_candidates[m_nextRemoval++ % m_candidates.size()];
        uint32 const guid = candidate.characterGuid.GetCounter();
        auto* record = BotManager::Instance().FindBot(candidate.characterGuid);
        if (!record || !record->random || record->lifecycle == BotLifecycle::Removing ||
            !record->masterGuid.IsEmpty() || IsPinnedGuid(guid) ||
            sLFTMgr.IsQueued(candidate.characterGuid) || sLFTMgr.IsInOffer(candidate.characterGuid) ||
            !BotActivityLeaseManager::Instance().IsAvailableForBackground(guid)) continue;
        Player* player = sObjectAccessor.FindPlayerNotInWorld(candidate.characterGuid);
        if (player && (!player->GetSession() || player->GetSession()->HasNetworkTransport() ||
            player->IsBeingTeleported() || player->IsInCombat() || player->GetGroup() ||
            player->InBattleGround() || player->InBattleGroundQueue() || player->GetTransport() ||
            player->IsTaxiFlying() || (player->GetMap() && player->GetMap()->IsDungeon()))) continue;
        if (BotManager::Instance().RemoveBot(candidate.characterGuid, true)) ++removed;
    }
}

// Deterministic allowed team from all cached candidates for this account.
// TEAM_NONE = empty or unknown (any faction allowed). Sets isMixed=true when
// the cached candidates contain both factions (incompatible under
// ALLOW_TWO_SIDE_ACCOUNTS=0) – caller must exclude that account.
uint32_t RandomBotService::GetAccountAllowedTeam(uint32_t accountId, bool& isMixed) const
{
    bool hasAlliance = false;
    bool hasHorde = false;
    isMixed = false;
    for (Candidate const& c : m_candidates)
    {
        if (c.accountId != accountId)
            continue;
        PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(c.characterGuid.GetCounter());
        if (!data || !data->uiRace)
            continue;
        Team t = Player::TeamForRace(data->uiRace);
        if (t == ALLIANCE)
            hasAlliance = true;
        else if (t == HORDE)
            hasHorde = true;
        if (hasAlliance && hasHorde)
        {
            isMixed = true;
            return TEAM_NONE;
        }
    }
    if (hasAlliance && hasHorde)
    {
        isMixed = true;
        return TEAM_NONE;
    }
    if (hasAlliance)
        return ALLIANCE;
    if (hasHorde)
        return HORDE;
    return TEAM_NONE;
}

RandomBotService::AutoCreateCharResult RandomBotService::TryCreateCharacterOnAccount(uint32_t accountId, std::vector<std::pair<uint8_t, uint8_t>> const& validForAccount)
{
    if (validForAccount.empty())
    {
        sLog.outError("TortoiseBots: auto-create account %u has no valid race/class for its team, skipping", accountId);
        return AutoCreateCharResult::Permanent;
    }
    if (m_charCreateErrorNextRetry && time(nullptr) < m_charCreateErrorNextRetry)
        return AutoCreateCharResult::TransientError;

    // Up to 5 name attempts per account per cadence. Transient name collisions
    // (NAME_IN_USE/RESERVED/PROFANE/CHAR_CREATE_FAILED or local normalize miss) are
    // retried silently and do not mark the account permanently failed. Permanent
    // materialization errors (ACCOUNT/SERVER_LIMIT or other) are logged once and
    // the caller must remember the account as permanently failed so it is never
    // retried every RandomBotUpdateInterval. CHAR_CREATE_DISABLED and
    // CHAR_CREATE_PVP_TEAMS_VIOLATION are treated as transient 60s backoff
    // (dynamic creation-disabled/faction-balance, not permanent): core result
    // does not distinguish static NOT_PLAYABLE from dynamic IsFactionImbalanced /
    // CHARACTERS_CREATING_DISABLED, and validAll is already DBC/PlayerInfo and
    // team-filtered, so remaining DISABLED/PVP is likely transient server state
    // that must not permanently poison a healthy account (bounded via
    // m_charCreateErrorNextRetry).
    for (int nameAttempt = 0; nameAttempt < 5; ++nameAttempt)
    {
        std::string name = GenerateRndBotName();
        std::string norm = name;
        if (!normalizePlayerName(norm))
            continue;
        if (sObjectMgr.GetPlayerGuidByName(norm))
            continue;

        auto pr = validForAccount[urand(0, uint32(validForAccount.size() - 1))];
        uint8 race = pr.first;
        uint8 cls = pr.second;

        CharacterCreateInfo info;
        info.name = norm;
        info.race = race;
        info.class_ = cls;
        info.gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;
        info.skin = uint8(urand(0, 7));
        info.face = uint8(urand(0, 7));
        info.hairStyle = uint8(urand(0, 7));
        info.hairColor = uint8(urand(0, 7));
        info.facialHair = uint8(urand(0, 7));
        info.outfitId = 0;
        info.challengeMask = 0;

        if (!sObjectMgr.GetPlayerInfo(race, cls))
        {
            if (!m_autoCreateNoValidData)
            {
                sLog.outError("TortoiseBots: auto-create race %u class %u has no PlayerInfo (playercreateinfo), disabling for this process", uint32(race), uint32(cls));
                m_autoCreateNoValidData = true;
            }
            return AutoCreateCharResult::Permanent;
        }

        CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(accountId, info);
        if (outcome.result == CHAR_CREATE_SUCCESS)
        {
            if (race == RACE_GOBLIN)
            {
                CharacterDatabase.PExecute(
                    "UPDATE characters SET map = 1, zone = 14, position_x = -618.518, position_y = -4251.67, position_z = 38.718, orientation = 0 WHERE guid = '%u'",
                    outcome.guid.GetCounter());
                CharacterDatabase.PExecute(
                    "REPLACE INTO character_homebind (guid, map, zone, position_x, position_y, position_z) VALUES ('%u', 1, 14, -618.518, -4251.67, 38.718)",
                    outcome.guid.GetCounter());
            }
            else if (race == RACE_HIGH_ELF)
            {
                CharacterDatabase.PExecute(
                    "UPDATE characters SET map = 0, zone = 12, position_x = -8949.95, position_y = -132.493, position_z = 83.5312, orientation = 0 WHERE guid = '%u'",
                    outcome.guid.GetCounter());
                CharacterDatabase.PExecute(
                    "REPLACE INTO character_homebind (guid, map, zone, position_x, position_y, position_z) VALUES ('%u', 0, 12, -8949.95, -132.493, 83.5312)",
                    outcome.guid.GetCounter());
            }

            m_candidates.push_back({accountId, outcome.guid});
            m_ageMs.push_back(0);
            m_strategyAgeMs.push_back(0);
            m_randomizeAgeMs.push_back(0);
            TB_LOG_DETAIL("TortoiseBots: auto-create created character %s (%s) race %u class %u on account %u",
                norm.c_str(), outcome.guid.GetString().c_str(), uint32(race), uint32(cls), accountId);
            return AutoCreateCharResult::Success;
        }

        if (outcome.result == CHAR_CREATE_NAME_IN_USE || outcome.result == CHAR_NAME_RESERVED || outcome.result == CHAR_NAME_PROFANE || outcome.result == CHAR_CREATE_FAILED)
        {
            // Transient name candidate failure – try another name silently.
            // CHAR_NAME_PROFANE is a name-candidate rejection (profane regex) like
            // RESERVED/NAME_IN_USE; CHAR_CREATE_FAILED is DBC-missing. validAll is
            // pre-filtered with ChrRaces/ChrClasses so FAILED is rare – treat as
            // name-like transient to avoid permanently excluding a healthy account.
            continue;
        }
        if (outcome.result == CHAR_CREATE_ERROR)
        {
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient failure (CHAR_CREATE_ERROR) for %s race %u class %u on account %u, backing off", norm.c_str(), uint32(race), uint32(cls), accountId);
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
        }
        if (outcome.result == CHAR_CREATE_PVP_TEAMS_VIOLATION)
        {
            // Dynamic faction-balance / ALLOW_TWO_SIDE state: transient backoff, not permanent.
            // validAll is team-filtered and mixed accounts are already excluded, so PVP is likely
            // transient server ALLOW_TWO_SIDE/faction state. Use bounded 60s backoff to avoid poisoning.
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient PVP team violation for account %u race %u (faction balance), backing off", accountId, uint32(race));
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
        }
        if (outcome.result == CHAR_CREATE_ACCOUNT_LIMIT || outcome.result == CHAR_CREATE_SERVER_LIMIT)
        {
            TB_LOG_DETAIL("TortoiseBots: auto-create account %u at limit (%u), excluding from auto-create", accountId, uint32(outcome.result));
            return AutoCreateCharResult::Permanent;
        }
        if (outcome.result == CHAR_CREATE_DISABLED)
        {
            // Dynamic creation-disabled / faction-imbalance state: transient backoff, not permanent.
            // Core does not distinguish static NOT_PLAYABLE from dynamic IsFactionImbalanced /
            // CHARACTERS_CREATING_DISABLED mask; validAll already excludes NOT_PLAYABLE via DBC,
            // so remaining DISABLED is likely transient balance/disabled state.
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient disabled race %u class %u (creation disabled/faction balance) for account %u, backing off", uint32(race), uint32(cls), accountId);
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
        }
        // Generic fail-closed – permanent for this process, log once.
        sLog.outError("TortoiseBots: auto-create for %s race %u class %u on account %u failed %u, excluding account", norm.c_str(), uint32(race), uint32(cls), accountId, uint32(outcome.result));
        return AutoCreateCharResult::Permanent;
    }

    // Exhausted name attempts – transient, not a permanent exclusion.
    return AutoCreateCharResult::TransientName;
}

bool RandomBotService::TryAutoCreate()
{
    if (!sPlayerbotAIConfig.randomBotAutoCreate || !BotManager::HasRandomAdmissionCapacity())
        return false;
    if (!m_initialized || !sPlayerbotAIConfig.enabled)
        return false;
    if (sWorld.IsShutdowning())
        return false;

    // Reconciliation chooses a stable desired target before this bounded attempt.
    uint32 desired = m_targetCount;
    if (!desired)
        return false;
    if (m_candidates.size() >= desired)
        return false;

    // One bounded attempt per cadence (caller is throttled to
    // RandomBotUpdateInterval, >=1s). No per-tick LIKE scan.

    uint32 perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT);
    if (!perAccountLimit)
        perAccountLimit = 10;
    uint32 perRealmLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    if (!perRealmLimit)
        perRealmLimit = 50;
    uint32 accountLimit = std::min(perAccountLimit, perRealmLimit);

    if (m_autoCreateNoValidData)
        return false;
    if (m_charCreateErrorNextRetry && time(nullptr) < m_charCreateErrorNextRetry)
        return false;

    std::vector<std::pair<uint8_t,uint8_t>> validAll;
    validAll.reserve(64);
    for (uint32_t race = 1; race < MAX_RACES; ++race)
    {
        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
        if (!raceEntry || raceEntry->HasFlag(CHRRACES_FLAGS_NOT_PLAYABLE))
            continue;
        for (uint32_t cls = 1; cls < MAX_CLASSES; ++cls)
        {
            ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(cls);
            if (!classEntry)
                continue;
            if (!sObjectMgr.GetPlayerInfo(race, cls))
                continue;
            validAll.emplace_back(uint8_t(race), uint8_t(cls));
        }
    }
    if (validAll.empty())
    {
        if (!m_autoCreateNoValidData)
        {
            sLog.outError("TortoiseBots: auto-create no valid race/class (DBC/PlayerInfo missing), disabling for this process");
            m_autoCreateNoValidData = true;
        }
        return false;
    }

    bool allowTwoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS) != 0;

    // Pending-account handling for LoginDatabase async AccountMgr::CreateAccount
    // (LoginDatabase queues INSERT after AllowAsyncTransactions, separate from core PR #416): keep
    // exactly one pending fresh account name after AOR_OK with no visible id,
    // retry that same name with bounded/log-throttled cadence (60s) while
    // continuing the existing-account selection path and without allocating
    // another fresh account; log once after prolonged unresolved period (~300s).
    time_t now = time(nullptr);
    if (!m_pendingAccountName.empty())
    {
        bool shouldRetry = !m_pendingNextRetry || now >= m_pendingNextRetry;
        if (shouldRetry)
        {
            uint32 pendingId = sAccountMgr.GetId(m_pendingAccountName);
            if (pendingId)
            {
                std::string resolvedName = m_pendingAccountName;
                m_pendingAccountName.clear();
                m_pendingSince = 0;
                m_pendingNextRetry = 0;
                m_pendingStaleLogged = false;
                m_accountAllocNextRetry = 0;
                if (std::find(m_rndBotAccountIds.begin(), m_rndBotAccountIds.end(), pendingId) == m_rndBotAccountIds.end())
                    m_rndBotAccountIds.push_back(pendingId);
                else
                    TB_LOG_DETAIL("TortoiseBots: auto-create pending account %s (%u) already in pool, proceeding to character",
                        resolvedName.c_str(), pendingId);
                AutoCreateCharResult pendingRes = TryCreateCharacterOnAccount(pendingId, validAll);
                if (pendingRes == AutoCreateCharResult::Success)
                    return true;
                if (pendingRes == AutoCreateCharResult::TransientError)
                    return false;
                if (pendingRes == AutoCreateCharResult::Permanent)
                {
                    m_failedAutoCreateAccounts.insert(pendingId);
                    m_freshAutoCreateDisabled = true;
                    sLog.outError("TortoiseBots: auto-create pending account %s (%u) permanently failed, disabling further fresh RNDBOT account allocation for this process (existing accounts remain eligible)",
                        resolvedName.c_str(), pendingId);
                    return false;
                }
                return false;
            }
            m_pendingNextRetry = now + 1;
            if (!m_pendingStaleLogged && m_pendingSince && now - m_pendingSince >= 300)
            {
                sLog.outError("TortoiseBots: auto-create pending account %s unresolved for %ld seconds, continuing with existing accounts (one pending kept, no new allocation)",
                    m_pendingAccountName.c_str(), long(now - m_pendingSince));
                m_pendingStaleLogged = true;
            }
        }
        else if (!m_pendingStaleLogged && m_pendingSince && now - m_pendingSince >= 300)
        {
            sLog.outError("TortoiseBots: auto-create pending account %s unresolved for %ld seconds, continuing with existing accounts (one pending kept, no new allocation)",
                m_pendingAccountName.c_str(), long(now - m_pendingSince));
            m_pendingStaleLogged = true;
        }
        // Unresolved pending does not freeze existing-account creation; fall
        // through to the existing-account selection path below. Fresh-account
        // allocation is still blocked while pending remains.
    }

    // Try existing accounts in deterministic order. Permanently failed accounts
    // (mixed, disabled, faction violation, limit) are remembered in
    // m_failedAutoCreateAccounts, logged once, and never retried every
    // RandomBotUpdateInterval. Transient name collisions are not remembered
    // and remain retryable next cadence.
    for (uint32 accId : m_rndBotAccountIds)
    {
        if (m_failedAutoCreateAccounts.find(accId) != m_failedAutoCreateAccounts.end())
            continue;

        uint32 cnt = 0;
        for (Candidate const& c : m_candidates)
            if (c.accountId == accId)
                ++cnt;
        if (cnt >= accountLimit)
        {
            sLog.outError("TortoiseBots: auto-create account %u at character limit (%u), excluding from auto-create", accId, accountLimit);
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }

        bool isMixed = false;
        uint32_t allowed = GetAccountAllowedTeam(accId, isMixed);
        if (isMixed && !allowTwoSide)
        {
            sLog.outError("TortoiseBots: auto-create account %u has mixed-faction RNDBOT characters, excluding from auto-create", accId);
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }

        std::vector<std::pair<uint8_t,uint8_t>> validForAccount = validAll;
        if (allowed != uint32_t(TEAM_NONE) && !allowTwoSide)
        {
            std::vector<std::pair<uint8_t,uint8_t>> filtered;
            filtered.reserve(validAll.size());
            for (auto const& pr : validAll)
                if (Player::TeamForRace(pr.first) == allowed)
                    filtered.push_back(pr);
            if (!filtered.empty())
                validForAccount.swap(filtered);
            else
            {
                sLog.outError("TortoiseBots: auto-create account %u team %u has no valid race/class, excluding from auto-create", accId, uint32_t(allowed));
                m_failedAutoCreateAccounts.insert(accId);
                continue;
            }
        }

        AutoCreateCharResult res = TryCreateCharacterOnAccount(accId, validForAccount);
        if (res == AutoCreateCharResult::Success)
            return true;
        if (res == AutoCreateCharResult::Permanent)
        {
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }
        if (res == AutoCreateCharResult::TransientError)
            return false;
        // TransientName – try next account same cadence, do not remember.
    }

    // Do not allocate another fresh account while one pending remains unresolved.
    if (!m_pendingAccountName.empty())
        return false;
    if (m_freshAutoCreateDisabled)
        return false;
    if (m_accountAllocNextRetry && time(nullptr) < m_accountAllocNextRetry)
        return false;

    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";

    constexpr size_t kSuffixDigits = 6; // fixed-width "000000".."999999" so long prefix cannot make every suffix identical after truncation
    std::string safePrefix = prefix.substr(0, MAX_ACCOUNT_STR > kSuffixDigits ? MAX_ACCOUNT_STR - kSuffixDigits : 0);

    uint32 newAccountId = 0;
    std::string newUsername;
    bool hadAllocDbError = false;
    for (int attempt = 0; attempt < 20 && !newAccountId; ++attempt)
    {
        uint32 suffix = urand(1000, 999999);
        char suffixBuf[16];
        std::snprintf(suffixBuf, sizeof(suffixBuf), "%06u", suffix);
        std::string username = safePrefix + suffixBuf;
        AccountMgr::normalizeString(username);
        if (sAccountMgr.GetId(username) != 0)
            continue;
        std::string password = GenerateRandomPassword();
        AccountOpResult res = sAccountMgr.CreateAccount(username, password);
        if (res == AOR_OK)
        {
            uint32 id = sAccountMgr.GetId(username);
            if (id)
            {
                newAccountId = id;
                newUsername = username;
                m_rndBotAccountIds.push_back(id);
                TB_LOG_DETAIL("TortoiseBots: auto-create created RNDBOT account %s (%u)", username.c_str(), id);
            }
            else
            {
                // Async LoginDatabase INSERT (AllowAsyncTransactions; separate from core PR #416):
                // AccountMgr queues the INSERT, so GetId may still return 0
                // on this world thread. Remember exactly one pending name,
                // retry it with bounded/log-throttled cadence while continuing
                // existing-account creation, and never allocate another fresh
                // account or spin. Log once after prolonged unresolved period.
                m_pendingAccountName = username;
                m_pendingSince = time(nullptr);
                m_pendingNextRetry = m_pendingSince + 1;
                m_pendingStaleLogged = false;
                return false;
            }
        }
        else if (res == AOR_NAME_ALREDY_EXIST)
        {
            continue;
        }
        else
        {
            hadAllocDbError = true;
            continue;
        }
    }
    if (!newAccountId)
    {
        if (!m_accountAllocNextRetry || time(nullptr) >= m_accountAllocNextRetry)
        {
            if (hadAllocDbError)
                sLog.outError("TortoiseBots: auto-create could not allocate RNDBOT account after attempts (LoginDatabase failure), backing off");
            else
                sLog.outError("TortoiseBots: auto-create could not allocate RNDBOT account after attempts, backing off");
            m_accountAllocNextRetry = time(nullptr) + 60;
        }
        return false;
    }
    m_accountAllocNextRetry = 0;

    AutoCreateCharResult freshRes = TryCreateCharacterOnAccount(newAccountId, validAll);
    if (freshRes == AutoCreateCharResult::Success)
        return true;
    if (freshRes == AutoCreateCharResult::TransientError)
        return false;
    if (freshRes == AutoCreateCharResult::Permanent)
    {
        m_failedAutoCreateAccounts.insert(newAccountId);
        m_freshAutoCreateDisabled = true;
        sLog.outError("TortoiseBots: auto-create fresh account %s (%u) permanently failed, disabling further fresh RNDBOT account allocation for this process (existing accounts remain eligible)", newUsername.c_str(), newAccountId);
        return false;
    }
    return false;
}

void RandomBotService::ResolvePinnedBots()
{
    if (m_pinnedResolved)
        return;
    if (sPlayerbotAIConfig.pinnedBotNames.empty())
    {
        m_pinnedResolved = true;
        return;
    }

    // One-time, deferred until DB is usable: `characters.name` -> guid lookup
    // using the database's stored collation (separate from the teleport
    // facade's normalized comparison). PQuery null means not found or lookup
    // failed (does not distinguish); m_pinnedResolved avoids retry until
    // restart. No per-tick SELECT.
    for (std::string const& rawName : sPlayerbotAIConfig.pinnedBotNames)
    {
        if (rawName.empty())
            continue;
        std::string escaped = rawName;
        CharacterDatabase.escape_string(escaped);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name = '%s' LIMIT 1", escaped.c_str()));
        if (!result)
        {
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' was not found or the lookup failed (no retry until restart)", rawName.c_str());
            continue;
        }
        Field* fields = result->Fetch();
        uint32 guidLow = fields[0].GetUInt32();
        if (!guidLow)
        {
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' resolved to invalid guid", rawName.c_str());
            continue;
        }
        // Bounded, resolution-only check: a name that exists in `characters`
        // but is not in the discovered RNDBOT pool is logged and ignored.
        // Without this, such names were silently ignored later in the
        // prioritized pool pass, which mismatched the documented behavior.
        bool inPool = false;
        for (Candidate const& candidate : m_candidates)
            if (candidate.characterGuid.GetCounter() == guidLow)
            {
                inPool = true;
                break;
            }
        if (!inPool)
        {
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' (guid %u) not in RNDBOT pool, ignoring", rawName.c_str(), guidLow);
            continue;
        }
        m_pinnedGuids.insert(guidLow);
        TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' resolved to guid %u", rawName.c_str(), guidLow);
    }

    m_pinnedResolved = true;
    if (m_pinnedGuids.empty())
        TB_LOG_BASIC("TortoiseBots: no pinned bots resolved from %u configured names", static_cast<uint32>(sPlayerbotAIConfig.pinnedBotNames.size()));
    else
        TB_LOG_BASIC("TortoiseBots: %u pinned bot(s) cached", static_cast<uint32>(m_pinnedGuids.size()));
}

void RandomBotService::OnHumanLogin()
{
    ++m_humanSessions;
    if (m_initialized && sPlayerbotAIConfig.randomBotAutologin && !m_started)
    {
        m_started = true;
        TB_LOG_BASIC("TortoiseBots: native random-bot service started after a human login");
    }
}

void RandomBotService::OnHumanLogout()
{
    if (m_humanSessions)
        --m_humanSessions;
}

void RandomBotService::RemoveExpiredBots(uint32_t diff)
{
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        Candidate const& candidate = m_candidates[i];
        if (!BotManager::Instance().IsRandomBot(candidate.characterGuid))
        {
            m_ageMs[i] = 0;
            continue;
        }

        BotRecord* record = BotManager::Instance().FindBot(candidate.characterGuid);
        if (!record || !record->enteredWorld)
            continue;

        // Pinned bots are exempt from timed logout (native login teleport is
        // skipped separately via sRandomBotFacade::IsPinnedBot's normalized
        // name match, best-effort) but still gated by
        // RandomBotLoginWithPlayer=1 (see MaintainOnlinePool).
        if (IsPinnedGuid(candidate.characterGuid.GetCounter()))
        {
            m_ageMs[i] = 0;
            continue;
        }

        m_ageMs[i] += diff;
        if (!sPlayerbotAIConfig.randomBotTimedLogout || !sPlayerbotAIConfig.maxRandomBotInWorldTime)
            continue;

        uint64_t limit = static_cast<uint64_t>(sPlayerbotAIConfig.maxRandomBotInWorldTime) * 1000;
        if (m_ageMs[i] < limit)
            continue;

        TB_LOG_DETAIL("TortoiseBots: native random bot %s reached its online lifetime; removing",
            candidate.characterGuid.GetString().c_str());
        BotManager::Instance().RemoveBot(candidate.characterGuid, true);
        BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
        m_ageMs[i] = 0;
    }
}

void RandomBotService::MaintainOnlinePool()
{
    if (!m_started || sWorld.IsShutdowning())
        return;
    if (sPlayerbotAIConfig.randomBotLoginWithPlayer && !m_humanSessions)
    {
        for (Candidate const& candidate : m_candidates)
            if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
            {
                BotManager::Instance().RemoveBot(candidate.characterGuid, true);
                BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
            }
        return;
    }

    uint32 online = 0;
    for (Candidate const& candidate : m_candidates)
    {
        auto* record = BotManager::Instance().FindBot(candidate.characterGuid);
        if (record && record->random && record->lifecycle != BotLifecycle::Removing) ++online;
    }

    if (online > m_targetCount)
        RemoveSurplusBots(online);
    if (online >= m_targetCount || m_candidates.empty() || !BotManager::HasRandomAdmissionCapacity())
        return;

    uint32 perInterval = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;
    if (!perInterval)
        return;

    uint32 added = 0;

    // Pinned bots are prioritized within the cap, not additive beyond it.
    if (!m_pinnedGuids.empty())
    {
        for (uint32 pinnedGuid : m_pinnedGuids)
        {
            if (online >= m_targetCount || added >= perInterval)
                break;

            Candidate const* pinnedCandidate = nullptr;
            for (Candidate const& c : m_candidates)
                if (c.characterGuid.GetCounter() == pinnedGuid)
                {
                    pinnedCandidate = &c;
                    break;
                }

            if (!pinnedCandidate)
                continue;

            if (BotManager::Instance().IsBot(pinnedCandidate->characterGuid))
                continue;

            if (Player* player = sObjectAccessor.FindPlayer(pinnedCandidate->characterGuid))
            {
                if (!player->GetSession() || !player->GetSession()->IsHeadless())
                    continue;
            }
            if (BotSessionAdapter::GetHeadlessSessionState(pinnedCandidate->characterGuid) != HeadlessSessionState::NotFound)
                continue;
            uint32 guidLow = pinnedCandidate->characterGuid.GetCounter();
            if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Grinding, 0))
                continue;

            if (BotManager::Instance().AddRandomBot(pinnedCandidate->accountId, pinnedCandidate->characterGuid))
            {
                ++online;
                ++added;
                TB_LOG_DETAIL("TortoiseBots: pinned random bot %s queued on account %u (prioritized)",
                    pinnedCandidate->characterGuid.GetString().c_str(), pinnedCandidate->accountId);
            }
            else
                BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Grinding);
        }
    }

    size_t attempts = 0;
    while (online < m_targetCount && added < perInterval && attempts < m_candidates.size())
    {
        size_t index = m_nextCandidate++ % m_candidates.size();
        ++attempts;
        Candidate const& candidate = m_candidates[index];

        if (BotManager::Instance().IsBot(candidate.characterGuid))
            continue;

        if (Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid))
        {
            // A network session owns the character; native random control may
            // never steal it. Headless sessions are likewise left to the
            // existing BotManager record/reclaim path.
            if (!player->GetSession() || !player->GetSession()->IsHeadless())
                continue;
        }
        if (BotSessionAdapter::GetHeadlessSessionState(candidate.characterGuid) != HeadlessSessionState::NotFound)
            continue;

        uint32 guidLow = candidate.characterGuid.GetCounter();
        if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Grinding, 0))
            continue;

        if (BotManager::Instance().AddRandomBot(candidate.accountId, candidate.characterGuid))
        {
            ++online;
            ++added;
            TB_LOG_DETAIL("TortoiseBots: native random bot %s queued on account %u",
                candidate.characterGuid.GetString().c_str(), candidate.accountId);
        }
        else
            BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Grinding);
    }
}

void RandomBotService::UpdateMaintenance(uint32_t elapsed)
{
    // Keep cheap elapsed-time accounting independent of the work budget so
    // a deferred bot does not lose strategy/randomization time.
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        auto* record = BotManager::Instance().FindBot(m_candidates[i].characterGuid);
        if (!record || !record->enteredWorld || record->lifecycle != BotLifecycle::InWorld || !sObjectAccessor.FindPlayer(m_candidates[i].characterGuid)) continue;
        m_strategyAgeMs[i] += elapsed;
        m_randomizeAgeMs[i] += elapsed;
    }
    if (m_candidates.empty()) return;
    uint32 const started = WorldTimer::getMSTime();
    size_t const limit = std::min<size_t>(m_candidates.size(), sPlayerbotAIConfig.randomBotMaintenanceBatch);
    for (size_t examined = 0; examined < limit; ++examined)
    {
        // At least one candidate advances on every pass. A single action is
        // not preemptible, but subsequent work respects the elapsed budget.
        if (examined && WorldTimer::getMSTimeDiff(started, WorldTimer::getMSTime()) >=
            sPlayerbotAIConfig.randomBotMaintenanceBudgetMs) break;
        size_t const i = m_nextMaintenance++ % m_candidates.size();
        if (m_nextMaintenance >= m_candidates.size()) m_nextMaintenance = 0;
        Candidate const& candidate = m_candidates[i];
        BotRecord* record = BotManager::Instance().FindBot(candidate.characterGuid);
        Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid);
        if (!record || !record->enteredWorld || record->lifecycle != BotLifecycle::InWorld || !player)
            continue;

        // Auto-created characters start at the core's creation level. The
        // administrative initializer was previously the only caller of
        // PlayerbotFactory::Randomize, so level-range settings had no effect
        // on the autonomous pool. Initialize eligible bots once, recording
        // the durable "level" value in RandomBotFacade. The maintenance
        // budget bounds this expensive work rather than doing it on login.
        uint32 const guidLow = player->GetGUIDLow();
        if (sPlayerbotAIConfig.instantRandomize &&
            !sRandomBotFacade.GetValue(guidLow, "level") &&
            m_initializedThisRun.find(guidLow) == m_initializedThisRun.end() &&
            sRandomBotFacade.InitializeBot(player))
        {
            m_initializedThisRun.insert(guidLow);
            if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
                sRandomBotFacade.SetValue(guidLow, "equipment_ready", 1);
            // Continue this bot's setup now; do not wait for a population-wide
            // cursor cycle before initial placement.
        }

        // Repair only autonomous, initialized bots. Never reroll their level,
        // talents or earned gear to recover the empty-cache deployment.
        auto* ai = PlayerbotAIStorage::Instance().GetAI(player);
        bool const eligible = sRandomBotFacade.GetValue(guidLow, "level") && ai &&
            BotManager::Instance().IsControllableBot(player) && player->GetSession() &&
            player->GetSession()->IsHeadless() && !player->GetSession()->isLogingOut() &&
            player->IsAlive() && !player->IsBeingTeleported() && !player->IsInCombat() &&
            !player->GetGroup() && !player->InBattleGround() && !player->InBattleGroundQueue() &&
            !player->IsTaxiFlying() && !ai->HasActivePlayerMaster() && !ai->IsInRealGuild() &&
            !IsPinnedGuid(guidLow) && BotActivityLeaseManager::Instance().IsAvailableForBackground(guidLow);
        if (eligible && !sRandomBotFacade.GetValue(guidLow, "equipment_ready"))
        {
            bool const equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) &&
                (player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST) ||
                 player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_LEGS));
            if (equipped || sRandomBotFacade.UpdateGearSpells(player))
            {
                sRandomBotFacade.SetValue(guidLow, "equipment_ready", 1);
                sRandomBotFacade.SetValue(guidLow, "seeded", 1);
                player->SaveToDB();
            }
            else
                continue; // Incomplete gear must remain pending.
        }
        if (eligible && player->GetLevel() >= 10 &&
            !sRandomBotFacade.GetValue(guidLow, "initial_placement"))
        {
            if (BotManager::Instance().RelocateRandomBot(player, RandomBotDestination::Level))
                sRandomBotFacade.SetValue(guidLow, "initial_placement", 1);
            continue;
        }

        // ProcessBot is optional expired-value/background cleanup. Its
        // near-human refusal must not prevent pending initialization, gear
        // recovery or initial placement above from ever completing.
        if (!sRandomBotFacade.ProcessBot(player))
            continue;

        uint32 strategyInterval = sPlayerbotAIConfig.minRandomBotChangeStrategyTime;
        if (sPlayerbotAIConfig.maxRandomBotChangeStrategyTime > strategyInterval)
            strategyInterval = urand(strategyInterval, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
        if (strategyInterval && m_strategyAgeMs[i] >= strategyInterval * 1000)
        {
            sRandomBotFacade.ChangeStrategy(player);
            m_strategyAgeMs[i] = 0;
        }

        uint32 randomizeInterval = sPlayerbotAIConfig.minRandomBotRandomizeTime;
        if (sPlayerbotAIConfig.maxRandomBotRandomizeTime > randomizeInterval)
            randomizeInterval = urand(randomizeInterval, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        if (sPlayerbotAIConfig.randomGearUpgradeEnabled && randomizeInterval &&
            m_randomizeAgeMs[i] >= randomizeInterval * 1000)
        {
            // Same fresh-bot-only rule as login seeding (GearSeedingGuard.h):
            // never overwrite earned gear on a timer tick. The login path
            // normally seeds first; this is a backstop for pool bots that
            // somehow entered the world unseeded.
            uint32 timerGuidLow = player->GetGUIDLow();
            bool freshTimerBot = TortoiseBots::NeedsInitialGearSeeding(
                player->GetTotalPlayedTime(), sRandomBotFacade.GetValue(timerGuidLow, "seeded"));
            if (player->GetLevel() >= 5 && freshTimerBot)
            {
                if (sRandomBotFacade.UpdateGearSpells(player))
                    sRandomBotFacade.SetValue(timerGuidLow, "seeded", 1);
            }
            m_randomizeAgeMs[i] = 0;
        }
    }
}

void RandomBotService::Update(uint32_t diff)
{
    if (!m_initialized || !sPlayerbotAIConfig.enabled)
        return;

    if (m_resetFailed)
        return;
    if (m_resetPending)
    {
        m_serviceElapsedMs += diff;
        if (m_serviceElapsedMs >= std::max<uint32_t>(100, sPlayerbotAIConfig.randomBotUpdateInterval))
        {
            m_serviceElapsedMs = 0;
            ProgressAccountReset();
        }
        return;
    }

    ProcessAdminActions();
    sRandomBotFacade.RefreshAuctionPrices(diff);

    uint32_t cadence = std::max<uint32_t>(100, sPlayerbotAIConfig.randomBotUpdateInterval);
    m_serviceElapsedMs += diff;
    if (m_serviceElapsedMs < cadence)
        return;

    uint32_t elapsed = m_serviceElapsedMs;
    m_serviceElapsedMs = 0;

    m_activity.Update(m_humanSessions ? sPlayerbotAIConfig.diffWithPlayer :
        sPlayerbotAIConfig.diffEmpty, sWorld.GetAverageDiff(), elapsed / 1000.0);
    RefreshPopulationTarget();

    // Native creation is bounded by count and elapsed time; a short configured
    // cadence must not be silently clamped to a one-second bottleneck.
    if (sPlayerbotAIConfig.randomBotAutoCreate)
    {
        uint32 const creationStarted = WorldTimer::getMSTime();
        for (uint32 i = 0; i < sPlayerbotAIConfig.randomBotsMaxCreatesPerInterval; ++i)
        {
            if (i && WorldTimer::getMSTimeDiff(creationStarted, WorldTimer::getMSTime()) >=
                sPlayerbotAIConfig.randomBotCreationBudgetMs) break;
            if (!TryAutoCreate())
                break;
        }
    }

    if (!sPlayerbotAIConfig.randomBotAutologin)
        return;

    if (!m_pinnedResolved)
        ResolvePinnedBots();
    RemoveExpiredBots(elapsed);

    UpdateMaintenance(elapsed);

    MaintainOnlinePool();
}

void RandomBotService::Shutdown()
{
    if (!m_initialized)
        return;

    for (Candidate const& candidate : m_candidates)
    {
        if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
            BotManager::Instance().RemoveBot(candidate.characterGuid, true);
        BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
    }

    m_adminRequests.clear();
    m_adminKeys.clear();
    m_started = false;
    m_initialized = false;
    m_pinnedGuids.clear();
    m_pinnedResolved = false;
    m_rndBotAccountIds.clear();
    m_failedAutoCreateAccounts.clear();
    m_freshAutoCreateDisabled = false;
    m_autoCreateNoValidData = false;
    m_accountAllocNextRetry = 0;
    m_charCreateErrorNextRetry = 0;
    m_pendingAccountName.clear();
    m_pendingNextRetry = 0;
    m_pendingSince = 0;
    m_pendingStaleLogged = false;
    m_resetPending = false;
    m_resetFailed = false;
    m_nextResetAccount = 0;
    m_resetAccountIds.clear();
    m_initializedThisRun.clear();
}

} // namespace TortoiseBots
