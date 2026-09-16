#include "playerbot/RandomItemMgr.h"
// Small adapters for mature Vanilla/Tortoise strategy code.
//
// These functions translate behavior-facing queries to the native owners. They
// do not own sessions, players, AI instances, or random-bot population.

#include "../ai/playerbot/RandomBotFacade.h"
#include "../host/AuctionHouseAdapter.h"

#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../runtime/BotManager.h"
#include "BotWorldActions.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/PlayerbotAIStorage.h"

#include "AuctionHouse/AuctionHouseMgr.h"
#include "Database/DBCStores.h"
#include "World.h"
#include "WorldSession.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "SocialMgr.h"
#include "Guild/GuildMgr.h"
#include "Guild/Guild.h"
#include "Objects/Player.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "../host/ModuleLog.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
struct StoredValue
{
    uint32 value = 0;
    std::string data;
    int32 validIn = -1;
    uint64 expiresAt = 0;
};

std::mutex s_valuesMutex;
std::unordered_map<std::string, StoredValue> s_values;
bool s_valuesReady = false;

std::string ValueKey(uint32 guid, std::string const& name)
{
    return std::to_string(guid) + "\n" + name;
}

// Caller owns s_valuesMutex, so database enqueue order matches cache publication.
bool StoreValue(uint32 guid, std::string const& type, uint32 value,
    std::string const& data, int32 validIn)
{
    if (!s_valuesReady || type.empty() || type.size() > 45 || data.size() > 255)
        return false;
    std::string eventSql = type, dataSql = data;
    CharacterDatabase.escape_string(eventSql);
    CharacterDatabase.escape_string(dataSql);
    uint64 expiresAt = validIn > 0 ? uint64(time(nullptr)) + uint32(validIn) : 0;
    bool queued;
    if (value)
        queued = CharacterDatabase.PExecute(
            "INSERT INTO ai_playerbot_values (bot, event, value, data, expires_at) "
            "VALUES (%u, '%s', %u, '%s', " UI64FMTD ") "
            "ON DUPLICATE KEY UPDATE value=VALUES(value), data=VALUES(data), expires_at=VALUES(expires_at)",
            guid, eventSql.c_str(), value, dataSql.c_str(), expiresAt);
    else
        queued = CharacterDatabase.PExecute(
            "DELETE FROM ai_playerbot_values WHERE bot=%u AND event='%s'", guid, eventSql.c_str());
    if (!queued)
        return false;
    if (value)
        s_values[ValueKey(guid, type)] = StoredValue{value, data, validIn, expiresAt};
    else
        s_values.erase(ValueKey(guid, type));
    return true;
}

}

void RandomBotFacade::SyncNativePlayers()
{
    players.clear();
    // Donor GetPlayers was the non-random/controlled population, not the
    // autonomous pool. Human friends and guild members must remain visible.
    for (auto const& entry : sWorld.GetAllSessions())
    {
        auto* session = entry.second;
        Player* player = session && session->HasNetworkTransport() ? session->GetPlayer() : nullptr;
        if (player && player->IsInWorld())
            players[player->GetObjectGuid().GetCounter()] = player;
    }
    auto const bots = TortoiseBots::BotManager::Instance().GetAllBots();
    for (Player* player : bots)
    {
        if (!player || !player->GetSession() || !player->GetSession()->IsHeadless())
            continue;

        TortoiseBots::BotRecord* record =
            TortoiseBots::BotManager::Instance().FindBot(player->GetObjectGuid());
        if (record && (!record->random || !record->masterGuid.IsEmpty()) && player->IsInWorld())
            players[player->GetObjectGuid().GetCounter()] = player;
    }
    auto snapshot = std::make_shared<SocialSnapshot>();
    snapshot->hasControlledPopulation = !players.empty();
    for (auto const& entry : players)
        if (auto* social = entry.second->GetSocial())
            for (auto const& guid : social->GetFriendGuids())
                snapshot->friendGuids.insert(guid.GetCounter());

    // Classify each referenced guild once, on its native world owner. Copy
    // only IDs; map activity evaluation must not retain Guild or Player pointers.
    std::unordered_set<uint32> examinedGuilds;
    for (Player* player : bots)
    {
        if (!player || !player->IsInWorld()) continue;
        uint32 const guildId = player->GetGuildId();
        if (!guildId || !examinedGuilds.insert(guildId).second) continue;
        auto* guild = sGuildMgr.GetGuildById(guildId);
        if (!guild) continue;
        uint32 const account = sObjectMgr.GetPlayerAccountIdByGUID(guild->GetLeaderGuid());
        if (account && !sPlayerbotAIConfig.IsInRandomAccountList(account))
            snapshot->realGuildIds.insert(guildId);
    }
    std::atomic_store(&socialSnapshot, std::shared_ptr<SocialSnapshot const>(std::move(snapshot)));
}

bool RandomBotFacade::IsRandomBot(Player* bot)
{
    return bot && IsRandomBot(bot->GetObjectGuid().GetCounter());
}

bool RandomBotFacade::IsRandomBot(uint32 guid)
{
    return TortoiseBots::BotManager::Instance().IsRandomBot(ObjectGuid(HIGHGUID_PLAYER, guid));
}

bool RandomBotFacade::IsFreeBot(Player* bot)
{
    return bot && IsFreeBot(bot->GetObjectGuid().GetCounter());
}

bool RandomBotFacade::IsFreeBot(uint32 guid)
{
    return IsRandomBot(guid) || sPlayerbotAIConfig.IsFreeAltBot(guid);
}

bool RandomBotFacade::LoadPersistentValues()
{
    // Initialization runs before map workers and bot admission. Do not reload over
    // pending writes on a configuration reload.
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    if (s_valuesReady)
        return true;
    std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery("SELECT COUNT(*) FROM ai_playerbot_values"));
    if (!count)
        return false;
    std::unordered_map<std::string, StoredValue> loaded;
    if (count->Fetch()[0].GetUInt64())
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT bot, event, value, data, expires_at FROM ai_playerbot_values"));
        if (!rows)
            return false;
        uint64 const now = uint64(time(nullptr));
        do
        {
            Field* fields = rows->Fetch();
            std::string name = fields[1].GetCppString();
            uint32 value = fields[2].GetUInt32();
            uint64 expiresAt = fields[4].GetUInt64();
            if (name.empty() || !value || (expiresAt && expiresAt <= now))
                continue;
            int32 validIn = expiresAt ? int32(std::min<uint64>(expiresAt - now,
                std::numeric_limits<int32>::max())) : -1;
            loaded[ValueKey(fields[0].GetUInt32(), name)] =
                StoredValue{value, fields[3].GetCppString(), validIn, expiresAt};
        } while (rows->NextRow());
    }
    s_values.swap(loaded);
    s_valuesReady = true;
    return true;
}

bool RandomBotFacade::ResetPersistentValues()
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    if (!s_valuesReady || !CharacterDatabase.PExecute(
        "DELETE FROM ai_playerbot_values WHERE event <> 'temporary'")) return false;
    for (auto it = s_values.begin(); it != s_values.end(); )
    {
        auto const separator = it->first.find('\n');
        if (separator == std::string::npos || it->first.substr(separator + 1) != "temporary")
            it = s_values.erase(it);
        else ++it;
    }
    return true;
}

uint32 RandomBotFacade::GetValue(Player* bot, std::string type)
{
    return bot ? GetValue(bot->GetObjectGuid().GetCounter(), std::move(type)) : 0;
}

uint32 RandomBotFacade::GetValue(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end())
        return 0;
    if (it->second.expiresAt && uint64(time(nullptr)) >= it->second.expiresAt)
        return 0;
    return it->second.value;
}

int32 RandomBotFacade::GetValueValidTime(uint32 guid, std::string event)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, event));
    if (it == s_values.end() || !it->second.expiresAt)
        return it == s_values.end() ? 0 : it->second.validIn;
    uint64 const now = uint64(time(nullptr));
    return it->second.expiresAt > now ? int32(std::min<uint64>(it->second.expiresAt - now,
        std::numeric_limits<int32>::max())) : 0;
}

std::string RandomBotFacade::GetData(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end() || (it->second.expiresAt && uint64(time(nullptr)) >= it->second.expiresAt))
        return {};
    return it->second.data;
}

void RandomBotFacade::SetValue(uint32 guid, std::string type, uint32 value, std::string data, int32 validIn)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    StoreValue(guid, type, value, data, validIn);
}

void RandomBotFacade::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    if (bot)
        SetValue(bot->GetObjectGuid().GetCounter(), std::move(type), value, std::move(data), validIn);
}

double RandomBotFacade::GetBuyMultiplier(Player* bot)
{
    if (!bot)
        return 1.0;

    uint32 value = GetValue(bot, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        SetValue(bot, "buymultiplier", value, {},
            static_cast<int32>(sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval));
    }
    return static_cast<double>(value) / 100.0;
}

double RandomBotFacade::GetSellMultiplier(Player* bot)
{
    if (!bot)
        return 1.0;

    uint32 value = GetValue(bot, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        SetValue(bot, "sellmultiplier", value, {},
            static_cast<int32>(sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval));
    }
    return static_cast<double>(value) / 100.0;
}

uint32 RandomBotFacade::GetTradeDiscount(Player* bot, Player* master)
{
    return bot && master ? GetValue(bot, "trade_discount_" +
        std::to_string(master->GetGUIDLow())) : 0;
}

void RandomBotFacade::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (bot && master)
        SetValue(bot, "trade_discount_" + std::to_string(master->GetGUIDLow()), value, {},
            int32(std::min<uint32>(sPlayerbotAIConfig.maxRandomBotInWorldTime,
                std::numeric_limits<int32>::max())));
}

void RandomBotFacade::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!bot || !master)
        return;
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    std::string name = "trade_discount_" + std::to_string(master->GetGUIDLow());
    auto it = s_values.find(ValueKey(bot->GetGUIDLow(), name));
    uint32 current = it != s_values.end() && (!it->second.expiresAt ||
        uint64(time(nullptr)) < it->second.expiresAt) ? it->second.value : 0;
    int64 next = std::max<int64>(0, std::min<int64>(int64(current) + value,
        std::numeric_limits<uint32>::max()));
    StoreValue(bot->GetGUIDLow(), name, uint32(next), {},
        int32(std::min<uint32>(sPlayerbotAIConfig.maxRandomBotInWorldTime,
            std::numeric_limits<int32>::max())));
}

void RandomBotFacade::Remove(Player* bot)
{
    if (bot)
        TortoiseBots::BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true);
}

bool RandomBotFacade::InitializeBot(Player* bot)
{
    if (!bot || !IsRandomBot(bot) || !bot->IsInWorld() || bot->IsBeingTeleported() ||
        !bot->GetSession() || !bot->GetSession()->IsHeadless() || bot->GetGroup() ||
        bot->IsInCombat() || bot->InBattleGround() || bot->InBattleGroundQueue() || bot->IsTaxiFlying() ||
        IsPinnedBot(bot->GetGUIDLow()) ||
        !TortoiseBots::BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow())) return false;
    auto* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || ai->HasActivePlayerMaster() || ai->IsInRealGuild()) return false;

    uint32 const cap = std::max<uint32>(1, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));
    uint32 minimum = std::clamp<uint32>(std::max(sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL),
        sPlayerbotAIConfig.randomBotMinLevel), 1, cap);
    uint32 maximum = std::clamp<uint32>(sPlayerbotAIConfig.randomBotMaxLevel, minimum, cap);
    if (sPlayerbotAIConfig.syncLevelWithPlayers)
    {
        uint32 highest = 0;
        for (auto const& entry : sWorld.GetAllSessions())
        {
            auto* session = entry.second;
            auto* player = session && session->HasNetworkTransport() ? session->GetPlayer() : nullptr;
            if (player && player->IsInWorld()) highest = std::max(highest, player->GetLevel());
        }
        if (!highest) highest = sPlayerbotAIConfig.syncLevelNoPlayer;
        maximum = std::clamp<uint32>(uint32(std::min<uint64>(cap,
            uint64(highest) + sPlayerbotAIConfig.syncLevelMaxAbove)), minimum, cap);
    }
    uint32 level = bot->GetLevel();
    if (!sPlayerbotAIConfig.disableRandomLevels)
    {
        level = urand(minimum, maximum);
        if (urand(0, 100) < 100 * std::clamp(sPlayerbotAIConfig.randomBotMaxLevelChance, 0.0f, 1.0f))
            level = maximum;
    }
    if (level >= 5 && !sRandomItemMgr.HasEquipmentCache())
        return false;
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false, false);
    SetValue(bot, "level", bot->GetLevel());
    ai->Reset(true);
    return true;
}

bool RandomBotFacade::Refresh(Player* bot)
{
    if (!bot || !IsRandomBot(bot) || !TortoiseBots::BotManager::Instance().IsControllableBot(bot) ||
        bot->IsBeingTeleported() || TortoiseBots::BotWorldActions::IsMapExecution())
        return false;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;

    // Preserve mature native recovery independently of the item-cheat toggle.
    // The host can refuse resurrection (for example, permanent hardcore death).
    if (!bot->IsAlive())
    {
        bot->ResurrectPlayer(1.0f);
        if (!bot->IsAlive())
            return false;
        bot->SpawnCorpseBones();
        ai->ResetStrategies();
    }
    if (sPlayerbotAIConfig.disableRandomLevels || bot->InBattleGround())
        return true;

    ai->Reset();
    bot->DurabilityRepairAll(false, 1.0f);
    bot->SetHealthPercent(100);
    bot->SetPvP(true);
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();
    if (bot->GetMaxPower(POWER_MANA))
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
    if (bot->GetMaxPower(POWER_ENERGY))
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));
    // Native money mutation preserves hooks and the core's upper bound.
    bot->ModifyMoney(int32(500 * std::sqrt(urand(1, std::max<uint32>(1, bot->GetLevel() * 5)))));
    return true;
}

bool RandomBotFacade::UpdateGearSpells(Player* bot)
{
    if (!bot || !IsRandomBot(bot) || !PlayerbotAIStorage::Instance().GetAI(bot))
        return false;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.UpgradeGearBest();
    return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) != nullptr;
}

bool RandomBotFacade::ProcessBot(Player* player)
{
    // Native death AI owns release, corpse travel and resurrection. Background
    // maintenance must not repeatedly teleport ghosts back to the graveyard.
    if (!player || !IsRandomBot(player) || !player->IsInWorld() || !player->IsAlive() ||
        player->IsBeingTeleported() || player->GetGroup() || player->IsTaxiFlying() ||
        player->InBattleGround() || player->InBattleGroundQueue() ||
        !TortoiseBots::BotManager::Instance().IsControllableBot(player) ||
        player->GetSession()->isLogingOut() || TortoiseBots::BotWorldActions::IsMapExecution())
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player);
    if (!ai || ai->HasActivePlayerMaster() || ai->HasPlayerNearby())
        return false;
    ai->GetAiObjectContext()->ClearExpiredValues();
    return true;
}

bool RandomBotFacade::GetNamedLocation(std::string const& name, WorldLocation& location)
{
    std::string escaped = name;
    WorldDatabase.escape_string(escaped);
    auto result = WorldDatabase.PQuery(
        "SELECT map_id, position_x, position_y, position_z, orientation "
        "FROM ai_playerbot_named_location WHERE name = '%s' LIMIT 1", escaped.c_str());
    if (!result)
        return false;

    Field* fields = result->Fetch();
    location = WorldLocation(fields[0].GetUInt32(), fields[1].GetFloat(), fields[2].GetFloat(),
        fields[3].GetFloat(), fields[4].GetFloat());
    return true;
}

void RandomBotFacade::LoadBattleMastersCache()
{
    battleMastersCache.clear();
    auto result = WorldDatabase.Query("SELECT entry, bg_template FROM battlemaster_entry");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();
        uint32 bgTemplate = fields[1].GetUInt32();
        if (entry && sObjectMgr.GetCreatureTemplate(entry))
            battleMastersCache[TEAM_BOTH_ALLOWED][BattleGroundTypeId(bgTemplate)].push_back(entry);
    } while (result->NextRow());
}

void RandomBotFacade::LoadAuctionPrices()
{
    std::lock_guard<std::mutex> lock(m_ahActionMutex);
    auto next = std::make_shared<AuctionPriceMap>();

    // Iterate all DBC auction house entries, deduplicating by object pointer
    // (cross-faction mode collapses all entries to one object).
    std::vector<AuctionHouseObject*> visited;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(i);
        if (!houseEntry)
            continue;

        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
        if (!auctionHouse)
            continue;
        if (std::find(visited.begin(), visited.end(), auctionHouse) != visited.end())
            continue;
        visited.push_back(auctionHouse);

        for (auto const& snapshot : TortoiseBots::CopyAuctionEntries(*auctionHouse))
        {
            AuctionEntry const* entry = &snapshot;

            // Only consider buyout listings for unit-price appraisal
            if (!entry->buyout)
                continue;

            if (!entry->itemCount)
                continue;

            // Bounded per-item listings: keep up to 64 lowest-unit-price entries
            // per item template so memory and appraisal sorting stay bounded.
            constexpr size_t kMaxAuctionsPerItem = 64;
            auto& listings = (*next)[entry->itemTemplate];
            if (listings.size() < kMaxAuctionsPerItem)
            {
                listings.push_back(*entry);
            }
            else
            {
                float currentUnitPrice = float(entry->buyout) / float(entry->itemCount);
                size_t maxIdx = 0;
                float maxUnitPrice = 0.0f;
                for (size_t idx = 0; idx < listings.size(); ++idx)
                {
                    uint32 existingCount = std::max<uint32>(1, listings[idx].itemCount);
                    float existingUnitPrice = float(listings[idx].buyout) / float(existingCount);
                    if (existingUnitPrice > maxUnitPrice)
                    {
                        maxUnitPrice = existingUnitPrice;
                        maxIdx = idx;
                    }
                }
                if (currentUnitPrice < maxUnitPrice)
                {
                    listings[maxIdx] = *entry;
                }
            }
        }
    }
    std::shared_ptr<AuctionPriceMap const> published = std::move(next);
    std::atomic_store(&ahMirror, std::move(published));
}

void RandomBotFacade::RefreshAuctionPrices(uint32 diff)
{
    static uint32 elapsed = 0;
    elapsed += diff;
    uint32 intervalMs = sPlayerbotAIConfig.auctionPriceRefreshInterval * 1000;
    if (intervalMs < 5000)
        intervalMs = 5000;
    if (elapsed >= intervalMs)
    {
        elapsed = 0;
        LoadAuctionPrices();
    }
}

std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32 itemId) const
{
    static const std::vector<AuctionEntry> empty;
    auto snapshot = std::atomic_load(&ahMirror);
    auto it = snapshot->find(itemId);
    return it == snapshot->end() ? empty : it->second;
}

std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32 itemId, uint32 houseFaction) const
{
    std::vector<AuctionEntry> result;
    auto snapshot = std::atomic_load(&ahMirror);
    auto it = snapshot->find(itemId);
    if (it == snapshot->end())
        return result;

    bool twoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION);
    for (auto const& entry : it->second)
    {
        if (twoSide)
        {
            result.push_back(entry);
            continue;
        }

        uint32 team = entry.auctionHouseEntry ? AuctionHouseMgr::GetAuctionHouseTeam(entry.auctionHouseEntry) : 0;
        // team == 0 is neutral AH (accessible to all factions)
        if (team == 0 || team == houseFaction || houseFaction == 0)
        {
            result.push_back(entry);
        }
    }

    return result;
}

std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32 itemId, Player* bot) const
{
    if (!bot)
        return GetAhPrices(itemId, (uint32)0);

    return GetAhPrices(itemId, bot->GetTeam());
}

InventoryResult RandomBotFacade::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    if (!player)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(item);
    return prototype ? player->CanEquipItem(slot, dest, prototype, nullptr, false) : EQUIP_ERR_ITEM_NOT_FOUND;
}

bool RandomBotFacade::IsPinnedBot(uint32 guidLow)
{
    PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(guidLow);
    if (!data)
        return false;

    for (std::string pinned : sPlayerbotAIConfig.pinnedBotNames)
    {
        std::string cached = data->sName;
        if (normalizePlayerName(pinned) && normalizePlayerName(cached) && pinned == cached)
            return true;
    }
    return false;
}

void RandomBotFacade::ChangeStrategy(Player* player)
{
    if (!player)
        return;

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player))
    {
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies, BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotNonCombatStrategies, BotState::BOT_STATE_NON_COMBAT);
    }
}

bool RandomBotFacade::Revive(Player* player)
{
    // Administrative recovery is separate from the automatic native death AI.
    // Preserve the donor's BG exclusion and honor native resurrection refusal.
    if (!player || player->IsAlive() || player->InBattleGround())
        return false;
    bool const atCorpse = player->GetDeathState() == CORPSE;
    if (!Refresh(player))
        return false;

    SetValue(player, "dead", 0);
    SetValue(player, "revive", 0);
    // The donor attempted nearby rescue for an unreleased corpse and a
    // level-fitting destination for a ghost. Reuse native validated teleport
    // admission; never move the live Player around to probe candidate points.
    // Missing destinations, human/group ownership and pins preserve recovery
    // at the current location. A rejected relocation is not a failed revival.
    bool const relocated = TortoiseBots::BotManager::Instance().RelocateRandomBot(player,
        atCorpse ? TortoiseBots::RandomBotDestination::LocalGrind : TortoiseBots::RandomBotDestination::Level);
    sLog.outString("TortoiseBots: revived random bot %s; validated rescue relocation %s",
        player->GetName(), relocated ? "accepted" : "skipped");
    return true;
}

void RandomBotFacade::PrintTeleportCache()
{
    auto locations = WorldDatabase.Query("SELECT COUNT(*) FROM ai_playerbot_named_location");
    uint32 namedLocations = locations ? locations->Fetch()[0].GetUInt32() : 0;
    TB_LOG_BASIC("TortoiseBots: native travel points; named-location rows: %u", namedLocations);
}
