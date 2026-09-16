#include "BotWorldActions.h"
#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "Player.h"
#include "WorldSession.h"
#include "ByteBuffer.h"
#include "Log.h"
#include <chrono>

namespace TortoiseBots
{
thread_local bool BotWorldActions::draining = false;
thread_local bool BotWorldActions::mapExecution = false;

BotWorldActions& BotWorldActions::Instance()
{
    static BotWorldActions actions;
    return actions;
}

std::optional<bool> BotWorldActions::Defer(Player* bot, std::string const& action, ai::Event const& event)
{
    if (draining || !mapExecution)
        return std::nullopt;
    // Direct/nested bool Execute calls cannot represent pending completion.
    // Engine owns deferral before eligibility/listeners. A missed boundary
    // fails here without executing or enqueueing a detached side effect.
    return false;
}

bool BotWorldActions::Enqueue(Player* bot, std::string const& action, ai::Event const& event)
{
    return EnqueueContinuation(bot, action, event, {});
}

bool BotWorldActions::EnqueueContinuation(Player* bot, std::string const& action, ai::Event const& event,
    Continuation continuation)
{
    if (!bot || action.empty() || event.HasExpiredOwner() || !bot->IsInWorld() || bot->IsBeingTeleported() ||
        !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;

    unsigned const guid = bot->GetGUIDLow();
    std::lock_guard<std::mutex> lock(mutex);
    auto found = perBot.find(guid);
    if (pending.size() >= MaxPending || (found != perBot.end() && found->second >= MaxPerBot))
        return false;
    Request request{ai::Event("world action lifetime", std::string(), bot), guid,
        {bot->GetMapId(), bot->GetInstanceId(), bot->GetMapWorkGeneration()}, action, event, std::move(continuation)};
    auto [count, inserted] = perBot.try_emplace(guid, 0);
    try
    {
        pending.push_back(std::move(request));
    }
    catch (...)
    {
        if (inserted)
            perBot.erase(count);
        throw;
    }
    ++count->second;
    return true;
}

void BotWorldActions::Drain()
{
    if (draining)
        return;
    struct DrainScope
    {
        DrainScope() { draining = true; }
        ~DrainScope() { draining = false; }
    } scope;
    auto const start = std::chrono::steady_clock::now();
    for (unsigned processed = 0; processed < MaxPerTick; ++processed)
    {
        if (processed && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(4))
            break;
        std::optional<Request> request;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (pending.empty())
                break;
            request.emplace(std::move(pending.front()));
            pending.pop_front();
            auto count = perBot.find(request->guid);
            if (--count->second == 0)
                perBot.erase(count);
        }
        Player* bot = request->lifetime.GetOwner();
        if (!bot || request->event.HasExpiredOwner() ||
            !BotManager::Instance().IsControllableBot(bot) || bot->IsBeingTeleported() ||
            !request->stamp.Matches(bot->GetMapId(), bot->GetInstanceId(),
                bot->GetMapWorkGeneration(), bot->IsInWorld()))
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        try
        {
            // Re-evaluate current native eligibility and preserve nested action
            // order. Removal is deferred by the surrounding BotManager guard.
            if (request->continuation)
                request->continuation(*ai);
            else
                ai->DoSpecificAction(request->action, request->event, true);
        }
        catch (ByteBufferException const&)
        {
            sLog.outError("TortoiseBots: malformed queued world action %s for guid %u",
                request->action.c_str(), request->guid);
        }
    }
}

void BotWorldActions::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);
    pending.clear();
    perBot.clear();
}
}

// End bounded world action implementation.
