#include "BotCommands.h"
#include "../runtime/RandomBotService.h"
#include "../runtime/BotManager.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Chat.h"
#include "World.h"
#include "WorldSession.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "playerbot/PlayerbotAI.h"
#include "../runtime/PlayerbotAIStorage.h"
#include <algorithm>
#include <vector>
#include <charconv>
#include <sstream>
#include <string>

namespace TortoiseBots { namespace BotCommands {
bool HandleRandomCommand(ChatHandler* handler, char const* args)
{
    if (!handler) return false;
    auto const* command = handler->FindCommand("rndbot");
    if (!command || !handler->IsCommandAvailable(*command))
    {
        handler->SendSysMessage("You do not have permission to manage the random-bot population.");
        return true;
    }
    std::istringstream input(args ? args : "");
    std::string action, selector, extra;
    input >> action;
    if (action.empty() || action == "help")
    {
        handler->SendSysMessage("rndbot stats | inspect <online-character> | update | reset | diff [player-ms empty-ms] | pid <p> <i> <d> | init/teleport/rpg/grind/refresh/upgrade/revive/change_strategy/remove <name-prefix|all>");
        return true;
    }
    if (!sPlayerbotAIConfig.enabled)
    {
        handler->SendSysMessage("The playerbot module is disabled by configuration.");
        return true;
    }
    if (action == "pid")
    {
        double p, i, d;
        if (!(input >> p >> i >> d) || (input >> extra) ||
            !RandomBotService::Instance().ConfigureActivityController(p, i, d))
            handler->SendSysMessage("Usage: rndbot pid <finite p> <finite i> <finite d>.");
        else
            handler->SendSysMessage("Random-bot activity PID updated; accumulated controller history reset.");
        return true;
    }
    if (action == "diff")
    {
        std::string playerMs, emptyMs, trailing;
        if (!(input >> playerMs))
        {
            handler->PSendSysMessage("World diff: average %u ms, maximum %u ms; random-bot targets %u ms with humans / %u ms empty.",
                sWorld.GetAverageDiff(), sWorld.GetMaxDiff(), sPlayerbotAIConfig.diffWithPlayer, sPlayerbotAIConfig.diffEmpty);
            return true;
        }
        unsigned playerValue = 0, emptyValue = 0;
        auto positive = [](std::string const& text, unsigned& value)
        {
            auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
        };
        if (!(input >> emptyMs) || (input >> trailing) || !positive(playerMs, playerValue) || !positive(emptyMs, emptyValue))
            handler->SendSysMessage("Usage: rndbot diff [positive player-ms positive empty-ms].");
        else
        {
            sPlayerbotAIConfig.diffWithPlayer = playerValue;
            sPlayerbotAIConfig.diffEmpty = emptyValue;
            handler->SendSysMessage("Random-bot activity targets updated for this process.");
        }
        return true;
    }
    input >> selector >> extra;
    if (action == "inspect")
    {
        Player* observer = !selector.empty() && extra.empty() ?
            sObjectAccessor.FindPlayerByName(selector.c_str()) : nullptr;
        if (!observer || !observer->IsInWorld())
        {
            handler->SendSysMessage("Usage: rndbot inspect <online-character>. Reports up to eight nearest bots within 120 yards.");
            return true;
        }
        handler->PSendSysMessage("Observer %s map=%u instance=%u network=%u GM=%u visible=%u; activity=%.1f%%.",
            observer->GetName(), observer->GetMapId(), observer->GetInstanceId(),
            uint32(observer->GetSession() && observer->GetSession()->HasNetworkTransport()),
            uint32(observer->IsGameMaster()), uint32(observer->IsGMVisible()),
            RandomBotService::Instance().GetActivityPercentage());
        std::vector<std::pair<float, Player*>> nearby;
        for (Player* bot : BotManager::Instance().GetAllBots())
            if (bot && bot->IsInWorld() && bot->GetMap() == observer->GetMap())
            {
                float distance = bot->GetDistance(observer);
                if (distance <= 120.0f) nearby.emplace_back(distance, bot);
            }
        std::sort(nearby.begin(), nearby.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (size_t i = 0; i < std::min<size_t>(8, nearby.size()); ++i)
        {
            Player* bot = nearby[i].second;
            auto* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai) continue;
            handler->PSendSysMessage("%s L%u %.0fyd: active=%u react=%u near=%u priority=%u delay=%ums sinceAI=%ums combat=%u teleport=%u.",
                bot->GetName(), uint32(bot->GetLevel()), nearby[i].first,
                uint32(ai->CachedActivity(ALL_ACTIVITY)), uint32(ai->CachedActivity(REACT_ACTIVITY)),
                uint32(ai->HasPlayerNearby()), uint32(ai->GetPriorityType()), ai->GetAIInternalUpdateDelay(),
                bot->GetAIElapsed(WorldTimer::getMSTime()), uint32(bot->IsInCombat()), uint32(bot->IsBeingTeleported()));
        }
        return true;
    }
    if (action == "reset" && selector.empty())
    {
        handler->SendSysMessage(RandomBotService::Instance().ResetPersistentState() ?
            "Random-bot event state reset; temporary values preserved. Population reconciliation requested." :
            "Random-bot state reset was not accepted; the persistent store is unavailable.");
        return true;
    }
    if (action == "stats" && selector.empty())
    {
        handler->PSendSysMessage("Native bots: %u registered; random target: %u; queued admin actions: %u.",
            BotManager::Instance().GetBotCount(), RandomBotService::Instance().GetTargetCount(),
            static_cast<unsigned>(RandomBotService::Instance().GetPendingAdminCount()));
        handler->PSendSysMessage("Random-bot activity: %.1f%%.", RandomBotService::Instance().GetActivityPercentage());
        return true;
    }
    if (action == "update" && selector.empty())
    {
        RandomBotService::Instance().RequestUpdate();
        handler->SendSysMessage("Random-bot maintenance requested for the next world update.");
        return true;
    }
    RandomBotAdminAction operation;
    if (action == "init") operation = RandomBotAdminAction::Initialize;
    else if (action == "teleport") operation = RandomBotAdminAction::Teleport;
    else if (action == "rpg") operation = RandomBotAdminAction::Rpg;
    else if (action == "grind") operation = RandomBotAdminAction::Grind;
    else if (action == "refresh") operation = RandomBotAdminAction::Refresh;
    else if (action == "upgrade") operation = RandomBotAdminAction::Upgrade;
    else if (action == "revive") operation = RandomBotAdminAction::Revive;
    else if (action == "change_strategy") operation = RandomBotAdminAction::ChangeStrategy;
    else if (action == "remove") operation = RandomBotAdminAction::Remove;
    else
    {
        handler->SendSysMessage("Unsupported random-bot command. Use rndbot help for the native module commands.");
        return true;
    }
    if (selector.empty() || !extra.empty())
    {
        handler->SendSysMessage("Specify one bot name prefix or all; no extra arguments are accepted.");
        return true;
    }
    auto queued = RandomBotService::Instance().QueueAdminAction(operation, selector);
    handler->PSendSysMessage("Queued %u random-bot admin actions. Human-controlled characters are excluded.", queued);
    return true;
}
// End native random-bot command.
} }
