#pragma once
// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:unknown_type_name,clang:use_of_undeclared_identifier
#include "ObjectGuid.h"
#include <map>
#include <unordered_map>
#include <mutex>

class Player;
class PlayerbotAI;
class WorldPacket;

// Module-local PlayerbotAI registry. The Tortoise core remains unaware of
// PlayerBots; the module maps live Player*/ObjectGuid identities to AI objects
// through the Headless GUID lifecycle owned by BotManager.

class PlayerbotAIStorage
{
public:
    static PlayerbotAIStorage& Instance();

    enum class PacketDirection { BotOutgoing, MasterIncoming, MasterOutgoing };
    // Keep the registry lifetime lock through enqueue only. RemoveAI cannot
    // release an AI while a packet producer still uses it. No gameplay runs here.
    void QueuePacket(Player* player, WorldPacket const& packet, PacketDirection direction);

    void SetAI(Player* player, PlayerbotAI* ai);
    void RemoveAI(Player* player);
    PlayerbotAI* GetAI(Player* player) const;
    PlayerbotAI* GetAI(ObjectGuid guid) const;
    // A negated Player pointer is a predicate, never a character identity.
    // Reject mechanical !GetAI(p) -> GetAI(!p) porting mistakes at compile time.
    PlayerbotAI* GetAI(bool) const = delete;


private:
    mutable std::mutex mutex_;
    std::unordered_map<ObjectGuid, PlayerbotAI*> byGuid_;
    std::unordered_map<Player*, PlayerbotAI*> byPlayer_;
};

#ifndef GET_PLAYERBOT_AI
#define GET_PLAYERBOT_AI(player) PlayerbotAIStorage::Instance().GetAI(player)
#endif
