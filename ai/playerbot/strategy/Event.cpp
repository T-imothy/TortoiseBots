
#include "playerbot/playerbot.h"
#include "Objects/Player.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include <atomic>
#include <mutex>
#include <unordered_map>


using namespace ai;

ObjectGuid Event::getObject()
{
    if (packet.empty())
        return ObjectGuid();

    WorldPacket p(packet);
    p.rpos(0);

    ObjectGuid guid;
    p >> guid;

    return guid;
}
namespace ai
{
struct EventOwnerIdentity
{
    ObjectGuid guid;
    Player* original;
    std::atomic<bool> valid{true};
    EventOwnerIdentity(Player* player) : guid(player->GetObjectGuid()), original(player) {}
};
namespace
{
std::mutex eventOwnersMutex;
std::unordered_map<Player*, std::weak_ptr<EventOwnerIdentity>> eventOwners;
std::shared_ptr<EventOwnerIdentity> CaptureEventOwner(Player* player)
{
    if (!player)
        return {};
    std::lock_guard<std::mutex> lock(eventOwnersMutex);
    auto& slot = eventOwners[player];
    auto identity = slot.lock();
    if (!identity)
        slot = identity = std::make_shared<EventOwnerIdentity>(player);
    return identity;
}
}

Event::Event(std::string source, std::string param, Player* player)
    : source(std::move(source)), param(std::move(param)), owner(CaptureEventOwner(player)) {}
Event::Event(std::string source, WorldPacket& packet, Player* player)
    : source(std::move(source)), packet(packet), owner(CaptureEventOwner(player)) {}
Event::Event(std::string source, ObjectGuid object, Player* player)
    : source(std::move(source)), owner(CaptureEventOwner(player)) { packet << object; }

Player* Event::getOwner() const
{
    if (!owner || !owner->valid.load())
        return nullptr;
    // ObjectAccessor resolves before dereference. The revocable token also
    // fences a reconnect that reuses the same GUID and allocator address.
    Player* player = sObjectAccessor.FindPlayer(owner->guid);
    return player == owner->original ? player : nullptr;
}

bool Event::HasExpiredOwner() const
{
    return owner && !getOwner();
}

void Event::InvalidateOwner(Player* player)
{
    if (!player)
        return;
    std::lock_guard<std::mutex> lock(eventOwnersMutex);
    auto it = eventOwners.find(player);
    if (it == eventOwners.end())
        return;
    if (auto identity = it->second.lock())
        identity->valid.store(false);
    eventOwners.erase(it);
}
}

// End event requester lifetime implementation.
