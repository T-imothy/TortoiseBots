---
id: guide-configuration-tuning
title: Configuration Knobs & Feature Flags
category: guides
summary: Complete guide to customization knobs, player quality-of-life flags, world immersion settings, and in-game tactical toggles.
tags: [guide, config, tuning, settings, feature-flags, knobs]
relates_to:
  - guide-getting-started
  - guide-player-controls
  - guide-observability-dashboard
  - concept-strategy-engine
---

# Configuration Knobs & Feature Flags

TortoiseBots provides a rich set of feature flags and tuning knobs. Whether you are running a solo private server or hosting a community realm, these settings allow you to customize bot intelligence, party convenience, world immersion, and economy.

Configuration lives in two files:
1. `conf/tortoise_bots.conf` — Native module options, diagnostic levels, and telemetry.
2. `conf/aiplayerbot.conf` — Gameplay feature flags, QoL toggles, AI thresholds, and services.

---

## 1. Player Quality-of-Life (QoL) Flags

These settings dramatically enhance the solo or small-group experience with owned bots:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.SyncAltLevelToMaster` | `0` | **`1`** | **Auto-Level Bot Alts:** When enabled, all bot characters on your account automatically level up to match your main character's level as you progress. |
| `AiPlayerbot.BoostFollow` | `0` | **`1`** | **Sprint to Catch Up:** Bots temporarily increase run speed when they fall behind the leader, preventing them from trailing or getting caught on dungeon geometry. |
| `AiPlayerbot.NonGmFreeSummon` | `0` | **`1`** | **Unrestricted Summoning:** Allows regular players without GM status to use `.bot summon` to gather their bots out of combat anywhere. |
| `AiPlayerbot.AutoLearnQuestSpells` | `1` | **`1`** | **Class Quest Rewards:** Automatically teaches spells awarded by completed class quests (e.g. Paladin Resurrection, Warlock pet summons, Shaman totems). |
| `AiPlayerbot.RollBadItemsWithPlayer` | `0` | **`1`** | **Need on Empty Slots:** Forces party bots to roll Need on dungeon drops if their corresponding equipment slot is empty or severely under-leveled. |
| `AiPlayerbot.RandomGearUpgradeEnabled` | `1` | **`1`** | **Automatic Gear Scaling:** Periodically equips bots with level-appropriate dungeon and quest gear as they level up. |

---

## 2. World Population & Immersion Flags

These flags control the behavior of autonomous random bots roaming the world:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.RandomBotAutologin` | `0` | **`1`** | Automatically logs in random bots (`RNDBOT*`) at server startup. |
| `AiPlayerbot.RandomBotAutoCreate` | `0` | **`1`** | Automatically creates new bot accounts/characters if the active pool is below `MinRandomBots`. |
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | `0` | `50` / `150` | Sets the minimum and maximum active random bot population. |
| `AiPlayerbot.SyncLevelWithPlayers` | `0` | **`1`** | **Dynamic Level Bracket:** Restricts random bot levels to the highest online human player level + 5, ensuring the world levels up alongside you. |
| `AiPlayerbot.RandomBotInvitePlayer` | `1` | `1` | Random bots in the open world will invite solo human players to form questing groups. |
| `AiPlayerbot.RandomBotGroupNearby` | `1` | `1` | Bots will organically invite each other to form questing parties and dungeon groups. |
| `AiPlayerbot.RandomBotFormGuild` | `1` | `1` | Bots will buy guild charters, collect signatures from other bots, and found their own guilds. |
| `AiPlayerbot.EnableGreet` | `1` | `1` | Bots wave or say hello when passing players in towns and roads. |
| `AiPlayerbot.RandomBotShowHelmet` / `ShowCloak`| `1` | `1` | Renders helmets and cloaks on bots. |

---

## 3. Autonomous Services (Default OFF)

All autonomous services are fully bounded and disabled by default. Enable only the services you need:

| Service Flag | Default | Description |
| :--- | :---: | :--- |
| `AiPlayerbot.RandomBotLftEnabled = 1` | `0` | **LFT Dungeon Autofill:** When real players queue for Looking-For-Trouble dungeons and wait for missing roles (e.g. Tank or Healer), eligible bots fill the vacant slots and run the instance. |
| `AiPlayerbot.AhMarketEnabled = 1` | `0` | **Living Auction House:** Bots post gathered trade goods and bind-on-equip gear on the Auction House, and bid on/buyout items using real player pricing models. |
| `AiPlayerbot.RandomBotBgEnabled = 1` | `0` | **Battleground Auto-Queue:** Injects random bots into Warsong Gulch, Arathi Basin, and Alterac Valley when human players queue. |

---

## 4. Combat & Reaction Thresholds

Fine-tune how aggressively bots heal, rest, or drink:

| Setting | Default | Tuning Guidance |
| :--- | :---: | :--- |
| `AiPlayerbot.CriticalHealth` | `25` | Percent health considered an emergency. Triggers *Lay on Hands*, *Last Stand*, *Shield Wall*, or *Divine Shield*. |
| `AiPlayerbot.LowHealth` | `50` | Percent health triggering prioritized heavy heals (*Greater Heal*, *Healing Wave*). Increase to `65` for safer dungeon runs. |
| `AiPlayerbot.MediumHealth` | `70` | Threshold for maintenance heals (*Renew*, *Rejuvenation*, *Flash Heal*). |
| `AiPlayerbot.AlmostFullHealth` | `90` | Health ceiling above which bots stop casting heals to conserve mana. |
| `AiPlayerbot.LowMana` | `20` | Mana floor where casters switch to low-cost wanding or conserve mana. |
| `AiPlayerbot.MediumMana` | `50` | Threshold where bots consider conservative rotations. |

---

## 5. In-Game Live Toggles (On the Fly via `/tbm` or Chat)

You do not need to restart the server to adjust tactical behavior during gameplay. You can toggle these anytime:

### Tactical Gameplay Toggles (`.bot action <toggle>`)
- `.bot action aoe <on|off>` — Enables or disables Area-of-Effect abilities (crucial around crowd-controlled mobs).
- `.bot action pullback` — Orders the tank to pull the target and sprint back to your location.
- `.bot action focus` — Focuses party damage onto the mob marked with the Skull raid marker.
- `.bot action cc <mark>` — Assigns CC to a specific raid marker (e.g. Moon, Star).

### Strategy & Stance Toggles
Using `.bot strategy <+|-strategy>` or the `/tbm` addon:
- `+conserve mana` / `-conserve mana` — Restricts casters to basic, high-efficiency spells.
- `+loot` / `-loot` — Toggles whether bots run to loot corpses after combat.
- `+silent` / `-silent` — Silences bot chatter in party/say chat so they execute commands quietly.
- `+passive` / `-passive` — Halts all bot attacks; bots will only follow and hold fire.
- `.bot formation <arrow|line|circle|shield>` — Changes follow positioning around the leader.

## ManTech market preservation

`AiPlayerbot.AhMarketUseCMaNGOS = 1` selects the ported market and its
`ahbot.conf` settings (`AhBot.Enabled`, `AuctionHouseBot.*`). The existing
`AiPlayerbot.AhMarket*` settings describe the alternative module market and
are used only when this selector is zero. Restart after changing the selector.
The main configuration can set `AhBot.ConfigFile` to an explicit path.
CMaNGOS owner eligibility uses this module's verified random-account list.

### Migration travel preparation

`AiPlayerbot.GenerateTravelNodes` stays disabled by default. Enable it only for
an isolated preparation run with matching DBC, maps, vmaps and mmaps to generate
missing walking links. A populated cache still refreshes native taxi data and
walk/swim geometry. Generation failures propagate without clearing pending work
or declaring the cache saved. Missing/query-failed travel tables are not treated
as a successfully loaded empty dataset.

`AiPlayerbot.AsyncTravelPartitions` controls optional map-partition generation;
the default runs sequentially. Cost processing is bounded and nonrecursive.
Fish-location generation stays disabled because the host area-query capability
is not implemented. No movement-speed override is introduced by this port.


`AiPlayerbot.PathFailureRetryMs` defaults to 3000 and clamps to 250–30000 ms.
It delays repeated failed requests for the same destination cell, map, instance
and 64-bit transition generation. A changed destination/transfer or successful
path clears that failure. The delay does not alter physical movement speed.

### Bounded random population maintenance

`AiPlayerbot.RandomBotMaintenanceBatch` defaults to 128 candidates per service
cadence and clamps to 1–4096. `AiPlayerbot.RandomBotMaintenanceBudgetMs` defaults
to 2 ms and clamps to 1–1000. Recovery, strategy and gear work rotate through the
pool under both limits; at least one candidate advances per pass. An individual
action cannot be preempted, so this is a soft time bound. Cheap elapsed accounting
still scans the pool so deferred bots retain strategy/randomization timers.
Admission retains its separate existing limit. These limits do not certify
6,000 live bots or complete the parallel AI scheduling migration.


## ManTech population admission and durable targets

`AiPlayerbot.LoginDbQueueLimit` defaults to 256. Random admission and automatic
character creation stop admitting work when either the character or login DB's
query or callback queue reaches the limit. Zero explicitly disables this ceiling;
it does not change explicit player-owned bot commands.

Population bounds are reconciled on the service cadence, including a zero target.
The selected `bot_count` is stored through the persistent facade. Without automatic
creation, the current candidate count caps admissions without overwriting the
desired persisted target. Surplus removal shares bounded cadence work and preserves
owned, pinned, busy, queued/offered LFT, grouped, combat, BG, taxi, transport,
dungeon and teleporting bots. Pending additions count toward admission totals.
This bounds this service's work; it is not a measured 6000-bot performance claim.


## Background execution retry preservation

The existing `FailedActionRetryBase`, `FailedActionRetryMax`,
`FailedActionCacheTtl` and `FailedActionCacheMaxEntries` settings retain their
bounds and defaults (250 ms, 2,000 ms, 30 seconds and 64 entries). Zero base or
maximum disables retry memory. Only failed executions of autonomous non-combat
background actions back off. Usefulness, prerequisites and possibility are
evaluated normally, and alternative actions remain eligible. Impossible work
does not extend a retry deadline. Owner commands, owned bots, combat, reactions
and high-priority actions bypass the retry gate. Physical/resource changes,
master/control changes and native map-work generation changes clear stale
failures. The current power type is used, including rage and energy.


## Preserved movement diagnostics and activity control

`AiPlayerbot.BehaviorTrace` defaults off. Its map, X/Y and radius options select
the initial observation area. The existing bounded sampler tracks at most 12 bots,
emits at most 8,000 records over ten minutes and reserves periodic movement samples.
It observes journey/action/RPG/taxi outcomes without changing movement or eligibility.
Restart to begin another sample. Thorn diagnostics use the existing battleground
diagnostic admission and interval; they remain independent of general action logs.

Random-population maintenance restores the existing PID activity policy, with
P=0.05, I=0.001 and D=0.05. `DiffWithPlayer` and `DiffEmpty` supply its desired
world-update time. The controller uses elapsed seconds and publishes 0..100 percent
to the existing priority brackets. Human ownership, party and other critical
activity exemptions remain in `PlayerbotAI`. It does not change core tick rates.
`rndbot pid <p> <i> <d>` updates finite coefficients and resets controller history.
`rndbot stats` reports the current percentage. Tuning lasts until process restart.

AiPlayerbot.InstantRandomize (1) initializes eligible, uninitialized random
bots in the bounded maintenance pass, using AiPlayerbot.RandomBotMinLevel (1),
RandomBotMaxLevel, RandomBotMaxLevelChance (0.15), DisableRandomLevels and the
existing SyncLevelWithPlayers settings. A durable per-character level marker
prevents re-randomizing on later logins. This work is not done in the login
callback; at large populations it can take additional time to finish. Bounds
are clamped to the core's allowed levels. AiPlayerbot.RandomBotTeleportDistance
(1000 yards) limits explicit local grind relocation; it does not enable
automatic teleporting.

AiPlayerbot.DeleteRandomBotAccounts (0) is a destructive one-start rebuild
switch. When enabled, startup admits no random bots and the service deletes
one account per cadence through the native AccountMgr. It accepts only the
configured prefix followed by exactly six ASCII digits, aborting if another
name matches the prefix. Once the old account and character ownership has
disappeared from both databases, normal auto-creation may resume. Restore the
switch to 0 before the next restart or the replacement cohort will also be
deleted. It is disabled in the local operator configuration; adding the line
alone does not perform a reset.

AiPlayerbot.EnableActionLog controls the engine's per-action execution messages
as well as the existing detailed action logging. Its default is off. Bounded
BehaviorTrace remains a separate temporary diagnostic and does not require
unbounded action logs.

The native world-action queue has fixed bounds of 1024 pending requests, 8 per bot,
64 per tick and a 4 ms soft drain budget with minimum one-request progress. These
are internal bounds, not new configuration settings. Map execution remains gated;
the current world-owned AI loop executes its actions synchronously.

---

## 6. Diagnostic Logging

The native module layer (bot lifecycle, random-bot, Auction House, battleground queue, and LFT services — everything under `host/` and `runtime/`) has its own verbosity setting, independent of the core server's own `LogLevel`. This lets you trace what the module is doing without turning on the engine's full debug output, and vice versa.

```ini
[TortoiseBotsConf]
TortoiseBots.LogLevel = 2
```

| Level | Name | Shows |
| :---: | :--- | :--- |
| `0` | Minimal | Errors only. |
| `1` | Basic | One-off startup, shutdown, and diagnostic test results (e.g. `PendingAddRemoveTest`, `AutoTest`). |
| `2` | Detail (default) | Per-bot state transitions: session start/stop, add/remove, AH postings, BG queue entries. |
| `3` | Debug | Per-tick and per-packet traces. High volume — intended for short diagnostic sessions, not left on. |

Errors (`sLog.outError`) are always written regardless of this setting. The level is re-read on `.reload config`, so it can be raised or lowered without a server restart.

This setting is separate from the strategy AI's own action trace, which stays gated behind the `debug`/`debug action` bot strategies (`.bot strategy +debug`) rather than a server-wide config key.


## September 13 local population correction

The native service honors RandomBotUpdateInterval down to 100 ms. Login count
is per interval: 6 at 100 ms permits up to 60 admissions/second, subject to the
existing LoginDbQueueLimit. RandomBotsMaxCreatesPerInterval (10) and
RandomBotCreationBudgetMs (5) bound native character creation per interval.
An individual native creation or initialization cannot be preempted.
InstantRandomize initializes the requested 1–60 range through the existing
factory and then requests validated level-appropriate native relocation.
DeleteRandomBotAccounts force-deletes only the selected generated accounts'
characters through native Player deletion before native account deletion.
It refuses an account with an active network session. The local switch is armed
for the user's next manual startup; reset it to zero after completion.
The local runtime enables existing continent partitioning and uses activity
priorities with botActiveAlone=10, matching the requested CMaNGOS activity model.
Build/deployment only was requested; no new population/runtime acceptance run.
