#pragma once

namespace TortoiseBots
{
// Item tables are built before AI admission. Queries must not insert missing
// keys into shared maps; in particular, nested misses must not grow the cache.
// The empty value is immutable and initialized once. Rebuilds still require
// exclusive ownership with no active readers.
template<class Cache, class Key>
typename Cache::mapped_type const& LookupItemCache(Cache const& cache, Key const& key)
{
    auto const found = cache.find(key);
    if (found != cache.end())
        return found->second;
    static typename Cache::mapped_type const empty{};
    return empty;
}
}
