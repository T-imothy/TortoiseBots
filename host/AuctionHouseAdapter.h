#pragma once
#include "AuctionHouse/AuctionHouseMgr.h"
#include <vector>

namespace TortoiseBots
{
// Preserve all native fields required by appraisal (including random property
// and lock metadata). No live pointer escapes the house's ownership lock.
inline std::vector<AuctionEntry> CopyAuctionEntries(AuctionHouseObject& house)
{
    AuctionHouseObject::Guard guard(house.GetLock());
    auto const bounds = house.GetAuctionsBounds_locked();
    std::vector<AuctionEntry> entries;
    entries.reserve(house.GetCount());
    for (auto it = bounds.first; it != bounds.second; ++it)
        if (it->second)
            entries.push_back(*it->second);
    return entries;
}

inline AuctionEntry* CopyAuctionEntry(AuctionHouseObject& house, uint32 id, AuctionEntry& copy)
{
    AuctionHouseObject::Guard guard(house.GetLock());
    auto const* entry = house.GetAuction(id);
    if (!entry)
        return nullptr;
    copy = *entry;
    return &copy;
}
}
