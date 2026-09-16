#include "playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "PerformanceMonitor.h"
#include "Chat/Chat.h"
#include "../../commands/BotCommands.h"
#include <sstream>

#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"

namespace bot_perf {

PerformanceMonitor::PerformanceMonitor()
{

}

PerformanceMonitor::~PerformanceMonitor()
{
}

std::unique_ptr<PerformanceMonitorOperation> PerformanceMonitor::start(PerformanceMetric metric, std::string_view name, PerformanceStack* stack, uint32 mapId, uint32 instanceId)
{
    if (!sPlayerbotAIConfig.perfMonEnabled)
    {
        return {};
    }

    std::lock_guard<std::mutex> registryGuard(lock);
    // The optional module has no legacy RandomPlayerbotMgr map-init pass.
    // Lazily create this map/instance bucket under the registry lock so enabling
    // monitoring after startup immediately covers existing maps as well.
    auto& instanceData = mapsData[mapId][instanceId];

    std::vector<std::string> localStack;

    // Build the key vector efficiently
    if (stack)
    {
        stack->emplace_back(name);
        localStack = *stack;
    }
    else
    {
        localStack = {std::string(name)};
    }

    auto& pd = instanceData[metric][localStack];

    return std::make_unique<PerformanceMonitorOperation>(pd, name, stack);
}

std::unique_ptr<PerformanceMonitorOperation> PerformanceMonitor::start(PerformanceMetric metric, std::string_view name, PlayerbotAI * ai)
{
    if (!sPlayerbotAIConfig.perfMonEnabled) return NULL;

    if (ai->GetAiObjectContext())
        return start(metric, name, &ai->GetAiObjectContext()->performanceStack, ai->GetBot()->GetMapId(), ai->GetBot()->GetInstanceId());
    else
        return start(metric, name);
}

std::string StackString(const std::vector<std::string>& stack, bool fullStack = false)
{
    if (stack.empty())
        return "";

    if (stack.size() == 1)
        return stack[0];

    // Pre-calculate total size to avoid reallocations
    size_t totalSize = stack.back().size() + 3; // last element + " []"
    for (size_t i = 0; i < stack.size() - 1; ++i)
        totalSize += stack[i].size() + 1; // +1 for '|' separator

    std::string result;
    result.reserve(totalSize);

    // Add the last element (most recent)
    result = stack.back();
    result += " [";

    // Add remaining elements in reverse order (oldest first)
    for (auto it = stack.rbegin() + 1; it != stack.rend(); ++it)
    {
        result += *it;

        if (!fullStack)
            break;

        if (std::next(it) != stack.rend())
            result += '|';
    }

    result += ']';
    return result;
}

void PerformanceMonitor::PrintStats(bool perTick, bool fullStack, bool showMap)
{
    uint64 total = 0;
    performanceMetricMap data;
    {
        // Copy a coherent counter snapshot; never hold the registry/counter
        // locks while sorting or printing a potentially long report.
        std::lock_guard<std::mutex> registryGuard(lock);
        for (auto& [mapId, mapData] : mapsData)
        {
            for (auto& [instanceId, instanceData] : mapData)
            {
                for (auto& [metric, namedData] : instanceData)
                {
                    for (auto& [stack, performanceData] : namedData)
                    {
                        std::lock_guard<std::mutex> counterGuard(performanceData.lock);
                        std::vector<std::string> newStack = stack;

                        if (showMap)
                        {
                            if (metric != PERF_MON_TOTAL)
                                newStack[0] = newStack[0] + " " + std::to_string(mapId) + (instanceId ? " (" + std::to_string(instanceId) + ")" : "");
                            else if (newStack[0].find(" I") != std::string::npos)
                                newStack[0] = newStack[0] + " " + std::to_string(mapId) + (instanceId ? " (" + std::to_string(instanceId) + ")" : "");
                            else if (newStack[0].find("PlayerbotAI::UpdateAI") == std::string::npos && newStack[0].find("PlayerbotAIBase::FullTick") == std::string::npos)
                                newStack[0] = newStack[0] + " " + std::to_string(mapId) + (instanceId ? " (" + std::to_string(instanceId) + ")" : "");
                            else
                                newStack[0] = newStack[0];
                        }

                        PerformanceData& pd = data[metric][newStack];

                        if (performanceData.totalTime > 0)
                        {
                            if (!pd.minTime || pd.minTime > performanceData.minTime)
                                pd.minTime = performanceData.minTime;
                            if (!pd.maxTime || pd.maxTime < performanceData.maxTime)
                                pd.maxTime = performanceData.maxTime;
                            pd.totalTime += performanceData.totalTime;
                        }
                        pd.count += performanceData.count;
                    }
                }
            }
        }
    }

    if (data.empty())
        return;

    uint64 totalCount = 0;

    sLog.outString(" ");
    sLog.outString(" ");

    if (!perTick)
    {
        for (auto& [name, performanceData] : data[PERF_MON_TOTAL])
        {
            if (name[0].find("PlayerbotAI::UpdateAI ") == 0)
            {
                totalCount += performanceData.count;
                total += performanceData.totalTime;
            }
        }

        sLog.outString("--------------------------------------[TOTAL BOT]------------------------------------------------------");
    }
    else
    {
        totalCount = data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].count;
        total = data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].totalTime;

        sLog.outString("---------------------------------------[PER TICK]------------------------------------------------------");
    }

    for (auto& [metric, namedData] : data)
    {
        std::string key;
        switch (metric)
        {
            case PERF_MON_TRIGGER: key = "T"; break;
            case PERF_MON_VALUE: key = "V"; break;
            case PERF_MON_ACTION: key = "A"; break;
            case PERF_MON_RNDBOT: key = "RndBot"; break;
            case PERF_MON_TOTAL: key = "Total"; break;
            default: key = "?";
        }

        std::list<std::vector<std::string>> stacks;

        for (auto& [stack, performanceData] : namedData)
        {
            if (!perTick && metric == PERF_MON_TOTAL && stack[0].find("PlayerbotAI") == std::string::npos)
                continue;
            stacks.push_back(stack);
        }

        auto& nameD = namedData;

        stacks.sort([&](std::vector<std::string> i, std::vector<std::string> j) { return nameD.at(i).totalTime < nameD.at(j).totalTime; });

        float tPerc = 0, tCount = 0;
        uint64 tMin = UINT64_MAX, tMax = 0, tTime = 0;

        sLog.outString("percentage   time    |   min  ..    max (     avg  of     count ) - type : name                        ");

        for (auto& stack : stacks)
        {
            PerformanceData& pd = namedData[stack];
            float perc = total ? (float)pd.totalTime / (float)total * 100.0f : 0.0f;
            float secs = (float)pd.totalTime / (perTick ? std::max<uint64>(1, totalCount) : 1000.0f);
            float avg = pd.count ? (float)pd.totalTime / (float)pd.count : 0.0f;
            float amount = (float)pd.count / (perTick ? (float)std::max<uint64>(1, totalCount) : 1);

            std::string disName = StackString(stack, fullStack);
            if (!fullStack && disName.find("|") != std::string::npos)
                disName = disName.substr(0, disName.find("|")) + disName.substr(disName.find("]"));

            if (perc > 0.1)
            {
                if (perTick)
                    sLog.outString("%7.3f%% %9ums | " UI64FMTD " .. " UI64FMTD " (%9.2f of %10.2f) - %s    : %s", perc, (uint32)secs, pd.minTime, pd.maxTime, avg, amount, key.c_str(), disName.c_str());
                else
                    sLog.outString("%7.3f%% %10.3fs | " UI64FMTD " .. " UI64FMTD " (%9.4f of %10u) - %s    : %s", perc, secs, pd.minTime, pd.maxTime, avg, (uint32)amount, key.c_str(), disName.c_str());

            }

            bool countTot = false;

            if (metric == PERF_MON_VALUE)
            {
                if (disName.find("<") == std::string::npos)
                    countTot = count(disName.begin(), disName.end(), '|') < 1;
                else
                    countTot = disName.find("|") == std::string::npos;
            }
            else
                countTot = disName.find("|") == std::string::npos;

            if (countTot)
            {
                tPerc += perc;
                tMin = pd.minTime < tMin ? pd.minTime : tMin;
                tMax = pd.maxTime > tMax ? pd.maxTime : tMax;
                tCount += amount;
                tTime += pd.totalTime;
            }
        }

        float secs = tTime / (perTick ? std::max<uint64>(1, totalCount) : 1000.0f);
        float avg = tCount ? tTime / tCount : 0.0f;

        if (metric != PERF_MON_TOTAL)
        {
            if (perTick)
                sLog.outString("%7.3f%% %9ums | " UI64FMTD " .. " UI64FMTD " (%9.4f of %10.2f) - %s    : %s", tPerc, (uint32)secs, tMin, tMax, avg, tCount, key.c_str(), "TOTAL");
            else
                sLog.outString("%7.3f%% %10.3fs | " UI64FMTD " .. " UI64FMTD " (%9.2f of %10u) - %s    : %s", tPerc, secs, tMin, tMax, avg, (uint32)tCount, key.c_str(), "TOTAL");
        }
        sLog.outString(" ");
    }

    uint64 maxMapTime = 0;

    for (auto& [stack, performanceData] : data[PERF_MON_TOTAL])
        if (stack[0].find("PlayerbotAI::UpdateAI ") == 0)
            maxMapTime = std::max(performanceData.totalTime, maxMapTime);

    if (total && data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].count &&
        data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].totalTime)
    {
        float avgDiff = data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].totalTime / data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].count;
        float aiPerc = (maxMapTime * 100.0f) / (float)(data[PERF_MON_TOTAL][{"PlayerbotAIBase::FullTick"}].totalTime);

        sLog.outString("Estimated avg diff: %3.2f with ai load at least: %5.2f%%", avgDiff, aiPerc);

        sLog.outString(" ");
    }
}

void PerformanceMonitor::Reset()
{
    std::lock_guard<std::mutex> registryGuard(lock);
    for (auto& [mapId, mapData] : mapsData)
    {
        for (auto& [instanceId, instanceData] : mapData)
        {
            for (auto& [metric, namedData] : instanceData)
            {
                for (auto& [name, performanceData] : namedData)
                {
                    std::lock_guard<std::mutex> counterGuard(performanceData.lock);
                    performanceData.minTime = performanceData.maxTime = performanceData.totalTime = performanceData.count = 0;
                }
            }
        }
    }
}

void PerformanceMonitor::Init(uint32 mapId, uint32 instanceId)
{
    if (sPlayerbotAIConfig.perfMonEnabled)
    {
        std::lock_guard<std::mutex> registryGuard(lock);
        mapsData[mapId][instanceId];
    }
}

} // namespace bot_perf — close before global PerformanceMonitorOperation impl

PerformanceMonitorOperation::PerformanceMonitorOperation(PerformanceData& data, std::string_view name, PerformanceStack* stack) : data(data), name(name), stack(stack)
{
    started = (std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now())).time_since_epoch();
}

PerformanceMonitorOperation::~PerformanceMonitorOperation()
{
   finish();
}

void PerformanceMonitorOperation::finish()
{
    // An operation already started must always finish and balance its stack,
    // even if monitoring has been disabled in the meantime.
    auto const finished = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()).time_since_epoch();
    uint64 const elapsed = uint64((finished - started).count());
    {
        std::lock_guard<std::mutex> guard(data.lock);
        if (elapsed > 0)
        {
            if (!data.minTime || data.minTime > elapsed) data.minTime = elapsed;
            if (data.maxTime < elapsed) data.maxTime = elapsed;
            data.totalTime += elapsed;
        }
        ++data.count;
    }
    // Each AI owns its nesting stack. Remove only this operation's most recent
    // frame; recursive operations with the same name retain their outer frame.
    if (stack)
    {
        auto frame = std::find(stack->rbegin(), stack->rend(), name);
        if (frame != stack->rend()) stack->erase(std::next(frame).base());
    }
}

namespace TortoiseBots { namespace BotCommands {
bool HandlePerformanceCommand(ChatHandler* handler, char const* args)
{
    if (!handler) return false;
    ChatCommand const* command = handler->FindCommand("perfmon");
    if (!command || !handler->IsCommandAvailable(*command))
    {
        handler->SendSysMessage("You do not have permission to manage bot performance monitoring.");
        return true;
    }
    std::istringstream input(args ? args : "");
    std::string token;
    bool tick = false, stack = false, map = false;
    while (input >> token)
    {
        if (token == "reset" || token == "toggle")
        {
            std::string extra;
            if (tick || stack || map || (input >> extra))
            {
                handler->SendSysMessage("Usage: .perfmon [tick] [stack] [map] | reset | toggle");
                return true;
            }
            if (token == "reset")
            {
                sPerformanceMonitor.Reset();
                handler->SendSysMessage("Bot performance monitor reset.");
            }
            else
            {
                // Native command dispatch owns the world control phase.
                sPlayerbotAIConfig.perfMonEnabled = !sPlayerbotAIConfig.perfMonEnabled;
                handler->SendSysMessage(sPlayerbotAIConfig.perfMonEnabled ?
                    "Bot performance monitor enabled." : "Bot performance monitor disabled.");
            }
            return true;
        }
        if (token == "tick") tick = true;
        else if (token == "stack") stack = true;
        else if (token == "map") map = true;
        else
        {
            handler->SendSysMessage("Usage: .perfmon [tick] [stack] [map] | reset | toggle");
            return true;
        }
    }
    sPerformanceMonitor.PrintStats(tick, stack, map);
    handler->SendSysMessage("Bot performance report written to the server console/log.");
    return true;
}
} }

// The host owns the legacy ChatHandler::HandlePerfMonCommand symbol. Keeping
// the monitor implementation here avoids a second definition when the
// native module is linked with the core's command compatibility table.
