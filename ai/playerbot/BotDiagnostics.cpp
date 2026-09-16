#include "BotDiagnostics.h"
#include "BattleGroundTG.h"
#include "strategy/Engine.h"
#include "PlayerbotAIConfig.h"
#include "playerbot.h"
#include "PlayerbotAI.h"
#include "TravelMgr.h"
#include "strategy/values/LastMovementValue.h"
#include "BoundedBotTrace.h"
#include "MoveSpline.h"
#include "Group.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>

namespace ai { namespace botdiag {
    thread_local const char* gLastPhaseTag     = nullptr;
    thread_local const char* gLastPhaseBotName = nullptr;

    bool IsActionLogEnabled()
    {
        return sPlayerbotAIConfig.enableActionLog;
    }

    // Diagnostic discovery deliberately includes unavailable/dead NPCs. The
    // native predicate below supplies the rejection reason; this search must
    // never supply a replacement NPC to gameplay or relax its eligibility.
    class DiagnosticFlightMasterCheck
    {
    public:
        explicit DiagnosticFlightMasterCheck(Player const* bot) : bot(bot) {}
        WorldObject const& GetFocusObject() const { return *bot; }
        bool operator()(Creature const* candidate)
        {
            if (!candidate->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_FLIGHTMASTER) ||
                !bot->IsWithinDistInMap(candidate, range))
                return false;
            range = bot->GetDistance(candidate);
            return true;
        }
    private:
        Player const* bot;
        float range = 4 * INTERACTION_DISTANCE;
    };

    static std::string DescribeTaxiInteraction(Player* bot, uint32 path, bool probe)
    {
        std::ostringstream out;
        TaxiPathEntry const* route = sTaxiPathStore.LookupEntry(path);
        out << " taxi_path=" << path << " taxi_from=" << (route ? route->from : 0)
            << " taxi_to=" << (route ? route->to : 0) << " taxi_probe=" << probe;
        if (!probe || !route)
            return out.str();

        Creature* npc = nullptr;
        DiagnosticFlightMasterCheck check(bot);
        MaNGOS::CreatureLastSearcher<DiagnosticFlightMasterCheck> searcher(npc, check);
        Cell::VisitAllObjects(bot, searcher, 4 * INTERACTION_DISTANCE);
        char const* failure = nullptr;
        bool const interactable = bot->CanInteractWithNPC(npc, UNIT_NPC_FLAG_FLIGHTMASTER, &failure);
        uint32 const node = npc ? sObjectMgr.GetNearestTaxiNode(npc->GetPositionX(),
            npc->GetPositionY(), npc->GetPositionZ(), npc->GetMapId(), bot->GetTeam()) : 0;
        out << " npc_guid=" << (npc ? npc->GetGUIDLow() : 0)
            << " npc_entry=" << (npc ? npc->GetEntry() : 0)
            << " npc_node=" << node << " npc_source_match=" << (node == route->from)
            << " npc_distance=" << (npc ? bot->GetDistance(npc) : -1.0f)
            << " npc_alive=" << (npc && npc->IsAlive())
            << " npc_combat=" << (npc && npc->IsInCombat())
            << " npc_flags=" << (npc ? npc->GetUInt32Value(UNIT_NPC_FLAGS) : 0)
            << " npc_interactable=" << interactable
            << " npc_reject=" << (failure ? failure : "unspecified");
        return out.str();
    }

    void TraceBehavior(PlayerbotAI* ai, const char* reason, const char* detail, uint32 taxiPath)
    {
        // Never enable the global per-action/spell logging to collect this sample.
        static std::atomic<bool> finished{false};
        if (!sPlayerbotAIConfig.behaviorTrace || finished.load(std::memory_order_relaxed) || !ai)
            return;
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return;
        float dx = bot->GetPositionX() - sPlayerbotAIConfig.behaviorTraceX;
        float dy = bot->GetPositionY() - sPlayerbotAIConfig.behaviorTraceY;
        bool const inArea = bot->GetMapId() == sPlayerbotAIConfig.behaviorTraceMap &&
            bot->GetInstanceId() == 0 && dx * dx + dy * dy <=
            sPlayerbotAIConfig.behaviorTraceRadius * sPlayerbotAIConfig.behaviorTraceRadius;
        bool const periodic = std::strcmp(reason, "journey") == 0;

        static std::mutex mutex;
        static BoundedBotTrace sample;
        uint32 sequence; uint64 dropped;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!sample.Take(WorldTimer::getMSTime(), bot->GetGUIDLow(), inArea, periodic, std::strcmp(reason, "action") == 0))
            {
                if (sample.Finished() && !finished.exchange(true))
                    Log::Instance().out(LOG_PERFORMANCE, "TW_BOT_BEHAVIOR_END emitted=%u suppressed=%llu", sample.Emitted(), (unsigned long long)sample.Suppressed());
                return;
            }
            sequence = sample.Emitted(); dropped = sample.Suppressed();
        }
        // Read manual values on the existing AI owner only; never evaluate an
        // action, run a trigger, change its master, or retain object pointers.
        AiObjectContext* context = ai->GetAiObjectContext();
        GuidPosition rpg = context->GetValue<GuidPosition>("rpg target")->Get();
        TravelTarget* travel = context->GetValue<TravelTarget*>("travel target")->Get();
        LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();
        WorldPosition destination = movement.lastMoveShort;
        WorldPosition next = destination;
        if (!movement.lastPath.empty())
        {
            destination = movement.lastPath.getBack();
            next = movement.lastPath.getPath().front().point;
        }
        std::string nextAction = context->GetValue<std::string>("next rpg action")->Get();
        WorldPosition goal;
        if (travel && travel->getPosition()) goal = *travel->getPosition();
        auto* spline = bot->movespline;
        // Unit allocates MoveSpline before the first movement. Duration()
        // indexes its timing array and requires Initialized(), even though
        // the MoveSpline pointer itself is already non-null. Diagnostics
        // must also tolerate an absent or cleared trajectory.
        if (spline && !spline->Initialized())
            spline = nullptr;
        G3D::Vector3 splineEnd = spline ? spline->FinalDestination() : G3D::Vector3();
        // Admission above bounds scans/formatting as well as emitted lines.
        // Successful activations have already entered taxi state: do not
        // mistake their post-activation state for a failed NPC interaction.
        std::string taxiDetail = taxiPath ? DescribeTaxiInteraction(bot, taxiPath,
            std::strcmp(reason, "taxi_reject") == 0) : std::string();
        Log::Instance().out(LOG_PERFORMANCE,
            "TW_BOT_BEHAVIOR seq=%u suppressed=%llu bot=%s guid=%u reason=%s detail=%.160s "
            "pos=%.2f,%.2f,%.2f map=%u motion=%u moving=%u taxi=%u combat=%u afk=%u master=%u "
            "rpg=%llu entry=%d rpgpos=%.2f,%.2f,%.2f next_rpg=%.80s travel_status=%d travel_entry=%d "
            "path_size=%zu next=%.2f,%.2f,%.2f dest_map=%u dest=%.2f,%.2f,%.2f "
            "tick=%u instance=%u in_area=%u level=%u leader=%u purpose=%u goal_map=%u goal=%.2f,%.2f,%.2f "
            "leg_type=%d leg_entry=%u spline=%u spline_ms=%d/%d spline_done=%u spline_end=%.2f,%.2f,%.2f spline_initialized=%u "
            "retry_move=%u retry_target=%u travel_left_ms=%d%s",
            sequence, (unsigned long long)dropped, bot->GetName(), bot->GetGUIDLow(), reason, detail,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
            uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()), uint32(bot->IsMoving()),
            uint32(bot->IsTaxiFlying()), uint32(bot->IsInCombat()), uint32(bot->isAFK()),
            ai->GetMaster() ? ai->GetMaster()->GetGUIDLow() : 0,
            (unsigned long long)rpg.GetRawValue(), int32(rpg.GetEntry()), rpg.getX(), rpg.getY(), rpg.getZ(), nextAction.c_str(),
            travel ? int32(travel->GetStatus()) : -1, travel ? travel->GetEntry() : 0,
            movement.lastPath.getPath().size(), next.getX(), next.getY(), next.getZ(),
            destination.getMapId(), destination.getX(), destination.getY(), destination.getZ(),
            WorldTimer::getMSTime(), bot->GetInstanceId(), uint32(inArea), uint32(bot->GetLevel()),
            bot->GetGroup() ? bot->GetGroup()->GetLeaderGuid().GetCounter() : 0,
            travel && travel->GetDestination() ? uint32(travel->GetDestination()->GetPurpose()) : 0,
            goal.getMapId(), goal.getX(), goal.getY(), goal.getZ(),
            movement.lastPath.empty() ? -1 : int32(movement.lastPath.getPath().front().type),
            movement.lastPath.empty() ? 0 : movement.lastPath.getPath().front().entry,
            spline ? spline->GetId() : 0, spline ? spline->timePassed() : 0,
            spline ? spline->Duration() : 0, uint32(!spline || spline->Finalized()), splineEnd.x, splineEnd.y, splineEnd.z,
            uint32(spline != nullptr),
            travel ? travel->GetRetryCount(true) : 0, travel ? travel->GetRetryCount(false) : 0,
            travel ? travel->GetTimeLeft() : 0, taxiDetail.c_str());
    }

    void TraceThornBehavior(PlayerbotAI* ai, bool minimal)
    {
        if (!ai) return;
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported()) return;
        BattleGround* bg = bot->GetBattleGround();
        if (!bg || bg->GetTypeId() != BATTLEGROUND_TG ||
            !static_cast<BattleGroundTG*>(bg)->AdmitBotDiagnostic(bot)) return;
        // Read existing owner-local state only, after admission. Never evaluate
        // priorities/triggers, initiate paths, or force a bot to move for tracing.
        auto* context = ai->GetAiObjectContext();
        LastMovement& move = context->GetValue<LastMovement&>("last movement")->Get();
        auto* engine = ai->GetEngine(ai->GetState());
        std::string actions = engine ? engine->GetLastAction() : "none";
        for (char& c : actions) if (c <= ' ' || c == '"') c = '_';
        if (actions.size() > 256) actions = actions.substr(actions.size() - 256);
        auto* spline = bot->movespline;
        if (spline && !spline->Initialized()) spline = nullptr;
        uint32 now = WorldTimer::getMSTime();
        int32 retry = int32(move.failedPathRetryUntil - now);
        G3D::Vector3 const next = spline ? spline->CurrentDestination() : G3D::Vector3();
        G3D::Vector3 const end = spline ? spline->FinalDestination() : G3D::Vector3();
        Log::Instance().out(LOG_BG,
            "THORN_GORGE schema=1 map=821 event=bot_ai inst=%u guid=%u tick=%u minimal=%u state=%u "
            "cached_active=%u cached_detailed=%u cached_react=%u motion=%u moving=%u unit_state=%u "
            "spline_initialized=%u spline_done=%u spline_ms=%u path_size=%zu path_retry_ms=%u "
            "next_teleport=%lld pvp_nc=%u pvp_combat=%u speed_cheat=%u run_speed=%.3f swim_speed=%.3f "
            "move_flags=%u spline_id=%u spline_total_ms=%u spline_index=%d spline_sent=%d "
            "next_x=%.2f next_y=%.2f next_z=%.2f end_x=%.2f end_y=%.2f end_z=%.2f actions=%s",
            bot->GetInstanceId(), bot->GetGUIDLow(), now, uint32(minimal), uint32(ai->GetState()),
            uint32(ai->CachedActivity(ALL_ACTIVITY)), uint32(ai->CachedActivity(DETAILED_MOVE_ACTIVITY)),
            uint32(ai->CachedActivity(REACT_ACTIVITY)), uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            uint32(bot->IsMoving()), bot->GetUnitState(), uint32(spline != nullptr),
            uint32(!spline || spline->Finalized()), spline ? spline->timePassed() : 0,
            move.lastPath.getPath().size(), move.failedPathRetryUntil && retry > 0 ? uint32(retry) : 0, (long long)move.nextTeleport,
            uint32(ai->HasStrategy("pvp", BotState::BOT_STATE_NON_COMBAT)), uint32(ai->HasStrategy("pvp", BotState::BOT_STATE_COMBAT)),
            uint32(ai->HasCheat(BotCheatMask::movespeed)), bot->GetSpeed(MOVE_RUN), bot->GetSpeed(MOVE_SWIM),
            bot->m_movementInfo.GetMovementFlags(), spline ? spline->GetId() : 0, spline ? spline->Duration() : 0,
            spline ? spline->_currentSplineIdx() : -1, spline ? spline->getLastPointSent() : -1,
            next.x, next.y, next.z, end.x, end.y, end.z, actions.c_str());
    }
}}
