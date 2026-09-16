#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <set>
#include <string>
#include "BotActivityController.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

namespace TortoiseBots
{

enum class RandomBotAdminAction { Refresh, Upgrade, Revive, ChangeStrategy, Remove, Initialize, Teleport, Rpg, Grind };

class RandomBotService
{
public:
    static RandomBotService& Instance();

    // Load the configured random-account character pool once. With
    // AiPlayerbot.RandomBotAutoCreate=1 the service also creates the bounded
    // deficit toward the configured target via AccountMgr/CharacterCreation
    // on the world thread (core PR #416). BotManager
    // remains the sole Headless-session owner.
    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    // Native admin requests are applied by this world-owned service, in
    // bounded slices. Queued work retains GUIDs, never live Player pointers.
    uint32_t QueueAdminAction(RandomBotAdminAction action, std::string selector);
    void RequestUpdate();
    bool ResetPersistentState();
    bool ConfigureActivityController(double p, double i, double d) { return m_activity.Configure(p, i, d); }
    float GetActivityPercentage() const { return static_cast<float>(m_activity.Percentage()); }
    uint32_t GetTargetCount() const { return m_desiredTargetCount; }
    size_t GetPendingAdminCount() const { return m_adminRequests.size(); }
    void OnHumanLogin();
    void OnHumanLogout();

private:
    struct Candidate
    {
        uint32_t accountId = 0;
        ObjectGuid characterGuid;
    };

    RandomBotService() = default;
    ~RandomBotService() = default;

    struct AdminRequest { ObjectGuid guid; RandomBotAdminAction action; uint64_t generation; };
    std::deque<AdminRequest> m_adminRequests;
    std::set<std::pair<uint32_t, RandomBotAdminAction>> m_adminKeys;
    void ProcessAdminActions();
    bool PrepareAccountReset();
    void ProgressAccountReset();
    void LoadCandidates();
    void MaintainOnlinePool();
    void UpdateMaintenance(uint32_t elapsed);
    void RemoveExpiredBots(uint32_t diff);
    void RefreshPopulationTarget();
    void RemoveSurplusBots(uint32_t online);
    uint32_t DesiredTargetCount() const;
    bool TryAutoCreate();
    enum class AutoCreateCharResult { Success, TransientName, TransientError, Permanent };
    AutoCreateCharResult TryCreateCharacterOnAccount(uint32_t accountId, std::vector<std::pair<uint8_t, uint8_t>> const& validForAccount);
    // Returns TEAM_NONE if empty/unknown, otherwise HORDE/ALLIANCE (67/469). Sets isMixed
    // when cached candidates contain both factions (must be excluded).
    uint32_t GetAccountAllowedTeam(uint32_t accountId, bool& isMixed) const;
    void ResolvePinnedBots();
    bool IsPinnedGuid(uint32 guidLow) const { return m_pinnedGuids.find(guidLow) != m_pinnedGuids.end(); }

    std::vector<Candidate> m_candidates;
    std::vector<uint32_t> m_ageMs;
    std::vector<uint32_t> m_strategyAgeMs;
    std::vector<uint32_t> m_randomizeAgeMs;
    // Prevent repeated expensive randomization in this process if the
    // persistent value write is delayed or unavailable.
    std::set<uint32_t> m_initializedThisRun;
    size_t m_nextCandidate = 0;
    size_t m_nextMaintenance = 0;
    size_t m_nextRemoval = 0;
    uint32_t m_serviceElapsedMs = 0;
    BotActivityController m_activity;
    // Desired target survives restart; available candidates cap admission only.
    uint32_t m_targetCount = 0;
    uint32_t m_desiredTargetCount = 0;
    bool m_targetReady = false;
    uint32_t m_humanSessions = 0;
    bool m_initialized = false;
    bool m_started = false;
    bool m_resetPending = false;
    bool m_resetFailed = false;
    size_t m_nextResetAccount = 0;
    uint32_t m_resetCharacterAccount = 0;
    std::vector<uint32_t> m_resetAccountIds;
    std::set<uint32> m_pinnedGuids;
    bool m_pinnedResolved = false;
    // Idempotent creation: known RNDBOT account ids (from LoadCandidates and
    // auto-created). No per-tick LIKE scan; one bounded DB COUNT per creation
    // happens inside CharacterCreation validation.
    std::vector<uint32_t> m_rndBotAccountIds;
    // Process-lifetime auto-create failure state: permanently failed accounts
    // (mixed, limit, or other materialization errors) are logged once and
    // never retried. Transient name collisions (NAME_IN_USE/RESERVED/PROFANE)
    // are not recorded here and remain retryable; CHAR_CREATE_DISABLED and
    // CHAR_CREATE_PVP_TEAMS_VIOLATION are treated as transient 60s backoff
    // (dynamic creation-disabled/faction-balance, not permanent) via
    // m_charCreateErrorNextRetry to avoid poisoning a healthy account.
    // Reset only on Initialize.
    std::set<uint32_t> m_failedAutoCreateAccounts;
    // After a fresh-account permanent character-creation failure, stop
    // allocating additional empty RNDBOT accounts for this process (log once);
    // existing accounts remain eligible.
    bool m_freshAutoCreateDisabled = false;
    // Process-lifetime disable when no valid DBC/PlayerInfo race/class remains
    // (missing CharRaces/CharClasses or playercreateinfo) – log once.
    bool m_autoCreateNoValidData = false;
    // Bounded retry backoff for allocation/creation transient failures.
    // Throttled to at most one error line per interval; retry after expiry.
    time_t m_accountAllocNextRetry = 0;
    time_t m_charCreateErrorNextRetry = 0;
    // Minimal pending-account state for AccountMgr::CreateAccount async
    // login-DB INSERT visibility (LoginDatabase async after AllowAsyncTransactions, separate from core PR #416): after
    // AOR_OK but GetId still 0, remember exactly one pending fresh account
    // name, retry that same name with bounded/log-throttled cadence while
    // continuing the existing-account selection path and without allocating
    // another fresh account; log once after prolonged unresolved period.
    // Cleared once the id is visible, then one character creation is attempted.
    std::string m_pendingAccountName;
    time_t m_pendingNextRetry = 0;
    time_t m_pendingSince = 0;
    bool m_pendingStaleLogged = false;
};

} // namespace TortoiseBots
