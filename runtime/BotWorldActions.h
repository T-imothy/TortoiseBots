#pragma once
#include "../ai/playerbot/strategy/Event.h"
#include "MapWork.h"
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class PlayerbotAI;

namespace TortoiseBots
{
// Player/world mutations execute only from BotManager's post-map update guard.
// Request capture is thread-safe; no retained Player/AI pointer is dereferenced.
class BotWorldActions
{
public:
    static BotWorldActions& Instance();
    static bool IsMapExecution() { return mapExecution && !draining; }
    // For existing native world-owned command entry points only. The caller
    // already owns the world/player lifetime barrier; this is not a mutex.
    class WorldScope
    {
    public:
        WorldScope() : previous(draining) { draining = true; }
        ~WorldScope() { draining = previous; }
        WorldScope(WorldScope const&) = delete;
        WorldScope& operator=(WorldScope const&) = delete;
    private:
        bool previous;
    };
    // Host map execution must explicitly establish this scope. Default native
    // world execution remains synchronous; admission is never mistaken for
    // completed gameplay by native world-owned continuations.
    class MapScope
    {
    public:
        MapScope() : previous(mapExecution) { mapExecution = true; }
        ~MapScope() { mapExecution = previous; }
        MapScope(MapScope const&) = delete;
        MapScope& operator=(MapScope const&) = delete;
    private:
        bool previous;
    };
    // nullopt means execution already owns the world; execute now.
    // Otherwise false rejects a direct bool call; the engine queues resumable work.
    std::optional<bool> Defer(Player* bot, std::string const& action, ai::Event const& event);
    bool Enqueue(Player* bot, std::string const& action, ai::Event const& event);
    using Continuation = std::function<void(PlayerbotAI&)>;
    bool EnqueueContinuation(Player* bot, std::string const& action, ai::Event const& event,
        Continuation continuation);
    void Drain();
    void Clear();

private:
    struct Request
    {
        ai::Event lifetime;
        unsigned guid;
        MapWorkStamp stamp;
        std::string action;
        ai::Event event;
        Continuation continuation;
    };
    static thread_local bool draining;
    static thread_local bool mapExecution;
    static constexpr unsigned MaxPending = 1024;
    static constexpr unsigned MaxPerBot = 8;
    static constexpr unsigned MaxPerTick = 64;
    std::mutex mutex;
    std::deque<Request> pending;
    std::unordered_map<unsigned, unsigned> perBot;
};
}
