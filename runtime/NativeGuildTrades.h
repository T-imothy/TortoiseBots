#pragma once
#include <cstdint>
class Player;
class Item;
namespace TortoiseBots
{
// World-owned native trade offers. Completion is separate from offer admission.
class NativeGuildTrades
{
public:
    static bool Offer(Player* sender, Player* receiver, Item* item, std::uint32_t count, char const** refusal = nullptr, bool party = false);
    static bool OfferParty(Player* sender, Player* receiver, Item* item, std::uint32_t count)
    { return Offer(sender, receiver, item, count, nullptr, true); }
    static void Update();
    static void Clear();
};
}
