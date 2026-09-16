#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
// Requests are admitted only for registered records. Coalescing therefore
// bounds storage by the registered population, not the number of producers.
class BotRemovalQueue
{
public:
    struct Request { uint32_t guid; uint64_t generation; bool save; };
    bool Push(uint32_t guid, uint64_t generation, bool save)
    {
        if (!guid || !generation)
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        auto found = pending.find(guid);
        if (found == pending.end())
            pending.emplace(guid, Request{guid, generation, save});
        else if (found->second.generation < generation)
            found->second = Request{guid, generation, save};
        else if (found->second.generation == generation)
            found->second.save = found->second.save || save;
        else
            return false;
        return true;
    }
    std::vector<Request> Take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Request> result;
        result.reserve(pending.size());
        for (auto const& entry : pending)
            result.push_back(entry.second);
        pending.clear();
        return result;
    }
private:
    std::mutex mutex;
    std::unordered_map<uint32_t, Request> pending;
};
}
