---
id: ref-provenance
title: Source Provenance & Behavior Harvesting Ledger
category: reference
summary: Append-oriented historical ledger tracking donor repositories, commit SHAs, ported translation units, and licensing attribution.
tags: [provenance, attribution, donor, lineage, history]
relates_to:
  - concept-donor-hierarchy
  - concept-architecture-invariants
---

# PROVENANCE — TortoiseBots behavior harvesting log

> **Append-oriented historical record.** This file is the source-lineage and
> validation ledger for imported/adapted behavior; it is not the current
> roadmap. Start with [PLAN.md](PLAN.md).

Record every substantial port/reimplementation here for attribution,
licensing, reasoning and local validation.

| Feature | Source project | Source commit | Source files | Ported / reimplemented | Reason | Local validation |
| --- | --- | --- | --- | --- | ---: | --- |
| SessionTransport Headless pattern (`SessionTransport::Headless`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()` with `NullSessionAnticheat`) | `Shyalya/tortoise-wow` (vendored `cmangos/playerbots` via `r-o-sh/tortoise-wow:playerbots-integration-gh`) and `tortoise-wow` core's existing `NullSessionAnticheat` | `shyalya-tortoise-wow@1f9497e` (checkpoint `0af2567` 2026-05-10, vendored `cmangos/playerbots@c33dfac`) / `Anticheat/Anticheat.h:143` (`NullSessionAnticheat`) | `Anticheat/Anticheat.h:143-209` (`NullSessionAnticheat`, `NullAnticheatLib`), `shyalya` host hooks for bot sessions (80-file surface) | Reimplemented as generic transport enum, not copied — core now distinguishes `Network` vs `Headless` via `SessionTransport`, module interprets `IsHeadless() == true` as bot. `InitHeadlessSession()` directly assigns `NullSessionAnticheat` (existing core precedent) | Harvest the null-transport precedent without inheriting Shyalya's 80-file host surface; keep host integration ≤5 files | `rg` audits for `GetBot/m_bot` remain clean; `BUILD_PLAYERBOTS=ON/OFF` matrix builds; Docker runtime spike: headless session survives `World::Update` (not deleted as disconnected), `HandlePlayerLogin` succeeds after queued `AddSession` + deferred `LoginPlayer`, bot enters world and re-enters after logout |
| `IWorldUpdateListener` generic world-tick registry (`RegisterWorldUpdateListener`, `GetPendingWorldListenerFactories`, `RegisterPendingWorldListeners` in `World::Update`) | `HardcodedEvents`/`ZoneScriptMgr::Update` pattern in `tortoise-wow/tortoise-wow` + `DiscordBot::RegisterHandlers` precedent in same core | `World.cpp:2448` (`World::Update`), `HardcodedEvents.h`, `ZoneScriptMgr.cpp:117` (`Update`), `World.cpp:2343` (`DiscordBot::RegisterHandlers`) | `World.h:889`, `World.cpp:2448`, `HardcodedEvents.h`, `ZoneScriptMgr` | Reimplemented as generic `IWorldUpdateListener` with explicit registration and pending-factory static initializers (no weak symbols, no `sBotHost` global) — inspirited by `DiscordBot`'s service registration but made generic | Bot AI must run on the world thread once per tick; no existing `WorldScript::OnUpdate` exists in MaNGOS `ScriptMgr`, so a single generic call site in `World::Update` is the correct seam | `World::Update` now calls listeners after `UpdateSessions`; `BotHostAdapter` receives tick, `BotManager` drives lifecycle; `rg -i PlayerBot` in `src/game` only shows `InitHeadlessSession` bridge |
| `BUILD_PLAYERBOTS` optional-module CMake wiring | `cmangos/mangos-classic` (`cmake/options.cmake: BUILD_PLAYERBOTS OFF`) and `mangoszero/server` (`CMakeLists.txt: PLAYERBOTS OFF`) | `cmangos-mangos-classic@9b682be`, `mangoszero-server@1817ae1` | `CMakeLists.txt`, `src/CMakeLists.txt`, `src/game/CMakeLists.txt`, `src/mangosd/CMakeLists.txt` | Reimplemented as explicit `option(BUILD_PLAYERBOTS OFF)` with `add_subdirectory(modules/TortoiseBots)` only when `ON`, and `target_link_libraries(mangosd tortoise_bots)` via `CMP0079` + `whole-archive` on Linux — no `FetchContent` auto-download, no `ENABLE_PLAYERBOTS` scattered defines | Keep `BUILD_PLAYERBOTS=OFF` first-class and `src/modules/TortoiseBots` absent/present matrix clean; harvest the option pattern without the `FetchContent` auto-clone | Matrix: `absent+OFF` OK, `present+OFF` OK (module present but not built), `present+ON` OK (module linked); `rg` audits clean |
| Headless queued-session lifecycle (`WorldSession::Update`, `CharacterScreenIdleKick`, queued add/remove) | `Shyalya`'s `NullSessionAnticheat` + `WorldSession::Update` null-socket handling (`WorldSession.cpp:163,383,736` already tolerates `m_Socket==nullptr` but deletes headless via `return false`) | `WorldSession.cpp:306,334,378`, `Handlers/CharacterHandler.cpp:548`, `World.cpp:283`, `LockedQueue.h` | `WorldSession.cpp`, `Handlers/CharacterHandler.cpp`, `World.cpp`, `LockedQueue.h` | Reimplemented: explicit `SessionTransport`, a one-pass `m_headlessLoginPending` keepalive, deferred `LoginPlayer` after queued `AddSession`, and generic pending-session inspection/cancellation. `BotManager` retains `Removing` records until cleanup. | The queued path must survive the first `UpdateSessions` pass without requiring synchronous insertion; immediate removal must cancel the queue entry instead of orphaning it. | Queued runtime spike passed; `PendingAddRemoveTest PASSED` with no active/pending session, player, or record; graceful shutdown clears both online flags. |
| World-owned Headless lifecycle façade (`StartHeadlessSession` / `StopHeadlessSession` / `GetHeadlessSessionState`) | `tortoise-wow/tortoise-wow` PR #411 refactor plus TortoiseBots packet transport identity correction | `1e7994934b864558e257dd1f375fbbdbbcebe95e` plus rebased #416 `58bcb1cf8ea7110561120ed47c3c9203f9338c5b`; TortoiseBots `73ce12958b933cb4e74f5ccaddb21819e7ed3573` | `HeadlessSessionMgr.{h,cpp}`, `World.{h,cpp}`, `WorldSession.{h,cpp}`, `Handlers/CharacterHandler.cpp`, `PlayerLoginQueryHolder.h`, `host/BotPacketAdapter.cpp` | Moved Headless validation, shared login dispatch, callback identity, update, reclaim, removal, and shutdown into the World-owned manager; migrated TortoiseBots to the three-call interface; kept packet dispatch keyed to transport identity rather than socket presence | Keep one concrete `WorldSession`, character-GUID Headless ownership, normal Network auth, bot-neutral core ownership, and valid synthetic Network packet fixtures | Docker core-only and synchronized static module builds reached `[100%] Built target mangosd`; `tools/verify_tortoise_surface.sh` passed; runtime `PendingAddRemoveTest PASSED`, AutoTest save/logout/relogin/cleanup PASSED, and PacketBridgeTest command-surface PASSED, while its synthetic group invite/accept check FAILED; no real-client path was run |
| Out-of-world Headless expiry and owner dungeon-exit recovery | Current generic Headless lifecycle and local observed instance-portal failure | Local reimplementation | `HeadlessSessionMgr.{h,cpp}`, `BotManager.*`, `BotPlayerAdapter.*` | Independently reimplemented; no donor code copied | A non-teleporting Headless player outside the world must not retain `characters.online=1` or block Network reclaim; a Network owner leaving a dungeon requests the existing safe summon for owned bots left inside | Cached module-enabled and module-disabled builds passed; real-client portal/reclaim check pending |

| Foundational Engine/AiObjectContext/Strategy/Trigger/Action/Value/ReactionEngine (Tortoise 1.18.1 baseline) | `Shyalya/tortoise-wow` (`playerbots-integration-gh` @ 1f9497e, vendored `cmangos/playerbots@c33dfac`) | `Shyalya` baseline provides the full `playerbot/strategy/Engine.{h,cpp}`, `AiObjectContext.{h,cpp}`, `AiObject.{h,cpp}`, `Strategy.{h,cpp}`, `Trigger.{h,cpp}`, `Action.{h,cpp}`, `Value.{h,cpp}`, `ReactionEngine.{h,cpp}`, `Queue/Event/Multiplier` etc, already translated for `MANGOSBOT_ZERO` (Vanilla 1.12/1.18.1) and core `WorldLocation`/`Position`/`Map` APIs | `ai/playerbot/strategy/Engine.*`, `AiObjectContext.*`, `AiObject.*`, `Strategy.*`, `Trigger.*`, `Action.*`, `Value.*`, `ReactionEngine.*`, `Queue.*`, `Event.*`, `AiObject.*` (full `ai/playerbot` tree, 82 top-level + 14 strategy core + 113 generic) | Copied verbatim as the Tortoise/Vanilla translation reference for the foundational runtime; `cmangos-compat-shim.h` and `botpch.h` already handle core `SpellEntry`/`ItemPrototype`/`MapStorage` translation, Vanilla `MANGOSBOT_ZERO` guards exclude `deathknight`/`TBC`/`WotLK` paths | Shyalya's `playerbots-integration-gh` is the only proven `1.18.1` PlayerBots that already runs on core `WorldLocation` (`mapId/x/y/z/o`), `Transport`/`GenericTransport`, `GuidSet`/`AreaTableEntry` etc; using it as the baseline avoids reinventing `Penqle` API translation and keeps `Headless`/`IsHeadless()` as the only host seam | `ai/` now contains the full Shyalya `playerbot` tree (204 generic files after modern layer); `CMakeLists.txt` now builds the real `Engine`/`AiObjectContext`/`Strategy` stack instead of the stub `EngineStub`/`AiObjectContextStub`; native linkage uses explicit `MODULE_TORTOISEBOTS=static` and `MANGOSBOT_ZERO` |
| Modern generic Base behavior (204 files, `Follow`/`Combat`/`Dead`/`Ranged`/`Melee` etc) | `mod-playerbots` `src/Ai/Base/Strategy` @ 5397110cba48 (merge #2661) + `Shyalya` `strategy/generic` @ 1f9497e | `mod-playerbots: src/Ai/Base/Strategy/*.{h,cpp}` (91 files, modern `FollowMasterStrategy` now `getDefaultActions()` + `InitTriggers` vs Shyalya's `InitNonCombatTriggers`/`InitCombatTriggers` + `NextAction::array`) / `shyalya: strategy/generic/*` (113 files, includes `BlackwingLair`/`Karazhan` dungeon strategies) | `ai/playerbot/strategy/generic/*` (204 files after modern layer; donor SHAs are retained here instead of backup copies) | Forward-ported the modern `mod-playerbots` generic set on top of Shyalya's Tortoise-translated generic; Vanilla/Tortoise `MANGOSBOT_ZERO` guards kept and expansion-only paths excluded | Modern class/combat/follow behavior remains attributable to the pinned donor commits; obsolete `*.shyalya.bak` copies were removed once this provenance record was complete |
| Modern class factory + per-class Strategy subfolders (9 Vanilla classes) | `mod-playerbots` `src/Ai/Class/{Warrior,Mage,Priest,Druid,Hunter,Rogue,Paladin,Shaman,Warlock}/AiObjectContext.*` + `Strategy/*.{h,cpp}` @ 5397110 + `Shyalya` `strategy/{warrior,mage,priest,...}/` @ 1f9497e | `mod-playerbots: src/Ai/Class/Warrior/{WarriorAiObjectContext.*,Strategy/*.cpp}` etc / `shyalya: strategy/warrior/{WarriorAiObjectContext.*,Arms/Fury/Protection/Tank...Strategy}` etc | `ai/playerbot/strategy/{warrior,mage,priest,druid,hunter,rogue,paladin,shaman,warlock}/*` | Forward-ported the nine Vanilla class contexts and strategy families; `AiFactory` now instantiates each native class context rather than silently falling back to the generic context | Fresh runtime attached real Warrior, Mage, Priest, and Hunter contexts; Warrior/Mage/Priest completed packet group journeys and Hunter completed via the deterministic existing-action diagnostic path |

Notes (updated 2026-08-22 — large-batch forward-port):

- Foundational runtime is now substantially real (not a stub): `ai/playerbot` contains the full Shyalya `playerbot` tree (Tortoise `1.18.1` translation, `MANGOSBOT_ZERO`) plus the modern `mod-playerbots@5397110` generic and nine class contexts. The obsolete `*.shyalya.bak` and `.orig` migration copies were removed after donor SHAs and source paths were recorded above. `CMakeLists.txt` builds the real `Engine`/`AiObjectContext`/`Strategy`/`Trigger`/`Action`/`Value`/`ReactionEngine`/`AiFactory` stack; `deathknight`/`WotLK` remains excluded via `MANGOSBOT_ZERO`.
- Host seam remains generic and minimal: `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `IWorldUpdateListener` (≤5 host files, verified via `rg -n -i 'PlayerBot|BotService|Headless' src/game` only shows the generic Headless manager/session code). No `IsBot()`/`GetBot()`/`m_bot`/`sPlayerBotMgr` reintroduced. Same-account `1 Network + N Headless` lifecycle is core-owned behind Start/Stop/State; `BotManager` retains only records and AI.
- Upstream licenses (GPL-2.0 for MaNGOS/CMaNGOS/Shyalya, GPL-2.0 for `mod-playerbots`) are preserved; headers retain original copyright/license and this file records donor SHAs/source files before migration copies were deleted. No `AzerothCore` `PlayerbotMgr`/`BotSession` ownership model is reintroduced.
- The broad donor tree is now wired into the active CMake source set: real `PlayerbotAI`/`AiFactory`, generic behavior, all nine Vanilla class folders, Value/Trigger/Action families, Travel, grouping, loot, quests, dungeon/raid and PvP families are compiled as one coherent batch rather than left dead in-tree. `MANGOSBOT_ZERO` filters expansion-only folders. Fresh runtime probes now cover the packet bridge and Warrior/Mage/Priest/Hunter class attachment/group journeys.
| Follow (dead-zone 1.5y, MoveFollow behind M_PI, public native target/moving-state restart guard, CanFollow guards) | `cmangos/playerbots` + `mangoszero/server` | `cmangos-playerbots@076045e` / `mangoszero-server@1817ae1` | `cmangos: playerbot/strategy/actions/FollowActions.cpp:36-90`; `mangoszero: src/modules/Bots/playerbot/strategy/actions/MovementActions.cpp:440-560`; local `ServerFacade.cpp` adapter over core `MotionMaster::GetCurrent()` | The real PlayerbotAI path uses `FollowMasterStrategy`/`FollowAction`; the Tortoise adapter reads the public native targeted-generator target and moving state instead of pretending core's private angle/offset fields are available. `BotController` retains only an intent/diagnostic record and is never a gameplay fallback after AI attachment | Keep 1.5y jitter-free follow without a second movement owner or re-entrant generator replacement | Cached ON/static build; preserved AI-enabled runtime packet journey exercised `follow chat shortcut`, group invite/accept, and cleanup without a movement-state crash |
| Warrior vertical slice | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | Focused `Engine`/`Queue`/`Trigger`/`Action`/`Value` primitives, generic `FollowMasterStrategy`/assist/combat/non-combat/dead strategies, and Warrior Arms/Fury/Protection strategy files | Ported/adapted focused family; unrelated expansion systems remain excluded by the CMake source set | Make one owned Tortoise bot use strategy-driven follow and combat before widening the donor tree | Cached Docker build: `Built target tortoise_bots`, `Built target mangosd`; runtime: same-account Headless Sagiroth + Dudette, `dps assist`, successful Warrior Heroic Strike spell 78, encounter end/follow resume, clean removal; `PlayerbotAIStorage` logout use-after-free fixed and revalidated |
| Broad Vanilla/Tortoise source-set checkpoint | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | `CMakeLists.txt` globs the real `PlayerbotAI`, generic, nine Vanilla class, Value/Trigger/Action, Travel, grouping, loot, quest, dungeon/raid, BG/PvP and economy families; `MANGOSBOT_ZERO` excludes Death Knight and other expansion-only paths | Adapted core naming and data shapes in the module-local compatibility layer; native loot ownership, area names/flags, channel wrappers and const loot-list views replace unsafe CMaNGOS member assumptions | Compile and stabilize the broad family without adding core `GetBot`/`m_bot` ownership | The first broad Docker pass reached the module compilation stage and exposed a small remaining core API family in `PlayerbotAI.cpp`; the correct upstream `module-system` host snapshot must also carry the documented generic Headless/session seams before a broad runtime claim is made |
| Sub-10 hunter "pet dead" suppression in `PetIsDeadValue` | `mod-playerbots` (mature behavior donor) | local `playerbots-references/mod-playerbots` `src/Ai/Base/Value/StatsValues.cpp:45-60` | `ai/playerbot/strategy/values/StatsValues.cpp:27-35` | Ported the level<10-hunter/mounted early-false rule with Turtle API spellings (`GetLevel`/`GetClass`/`IsMounted`); Shyalya has no equivalent (different AI lineage) | Pet-less lowbies with a stale `character_pet` row read "pet dead" and loop failing revive-pet casts (observed as `ACTION_LOOP` on a level-2 hunter) | Pending: module rebuild + live-issue clearance |
| Post-rez hopeless-death relocation (`RelocateHopelessBot`) | Inspired by `mod-playerbots` revive relocation (`RandomPlayerbotMgr::Revive` → `RandomTeleportGrindForLevel`); implemented natively on our revive path | local `playerbots-references/mod-playerbots` `src/Bot/RandomPlayerbotMgr.cpp` (`Revive`, `RandomTeleportGrindForLevel`) | `runtime/BotManager.{h,cpp}` (`RelocateHopelessBot`, shared `PickLevelFittingPoint` picker), `ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp` (both rez paths), `AiPlayerbot.RelocateHopelessDeaths` (default on) | Native conditional rescue instead of donor unconditional teleport: only random masterless ungrouped bots with 2+ deaths in a zone 5+ above their level relocate, reusing the login-scatter ±5 validated picker; corpse runs, master rezzes, BG/group flows untouched | Lowbies GY-locked in over-leveled zones (observed: level 5s dying in Northwind 25-30) | Pending: module rebuild + staged rescue verification |
| Buff & debuff triggers gated on trained spells (`BuffTrigger`/`MyBuffTrigger`/`DebuffTrigger`, class triggers) | Native hardening; no donor equivalent (donor triggers have the same hole, harmless there because donor bots are max-level) | local `playerbots-references/mod-playerbots` `src/Ai/Base/Trigger/GenericTriggers.cpp:159-167` (reference behavior) | `ai/playerbot/strategy/triggers/GenericTriggers.cpp`, `strategy/{priest,hunter,warrior,shaman,warlock,mage}/*` | `IsActive` returns false when `ai->HasSpell(spell)` is false: an untrained spell can never land its aura, so missing-aura triggers otherwise stay active forever and fail cast every tick | Observed `ACTION_LOOP`s on level 1-2 bots for inner fire, lightning shield, aspect of hawk, shocks, curses, bloodrage | Built into module; live verification |
| Auto-learn quest spells default on | Upstream mod-playerbots default (`AutoLearnQuestSpells=1`); Shyalya handled via ad-hoc class exceptions | upstream mod-playerbots `aiplayerbot.conf.dist` | `ai/playerbot/PlayerbotAIConfig.cpp`, `ai/playerbot/aiplayerbot.conf.dist.in`, `tortoise-docker-penqle` | Auto-learn quest-reward spells and items (stances, forms, totems, demon summons, hunter pet skills) on levelup while keeping trainer spells manual (gold/grind) | Bots cannot execute scripted class quest chains; quest rewards are required for basic class kit | Built into module; live verification |

## Native module-system checkpoint — 2026-08-24

Feature: Native module packaging, module-local runtime support, and broad Vanilla/Tortoise source selection

Source repository: local `tortoise-wow` checkout plus local TortoiseBots checkout

Source commit: core `73f32c063e6c4481a0415690896025178ca8076f` on branch `playerbots-integration-gh`; TortoiseBots `3484208` (`Finish native PlayerBots integration and playtest bridge`). This checkpoint is superseded by core `9487c5150a6553c665fafc1f4568669b8b00f011`; the core commits `133c6d19` and `9487c515` keep static-module include paths target-local and remove the stale `src/game/PlayerBots` common path.

Source files: `TortoiseBots.cmake`, `src/TortoiseBotsModule.cpp`, `host/*`, `runtime/*`, `ai/playerbot/*`, `strategy/{generic,druid,hunter,mage,paladin,priest,rogue,shaman,warlock,warrior}/*`, `strategy/{actions,triggers,values}/*`

Copied / ported / independently reimplemented: behavior was ported/adapted from local `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7caa943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; host lifecycle and BotManager ownership were independently reimplemented around generic `SessionTransport`/Headless APIs.

Reason: use existing combat/class/travel behavior without compiling donor manager, random-manager, login-manager, or second-session ownership into the target core.

Local validation: static `modules` target and `mangosd` link passed with `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, `MODULE_TORTOISEBOTS=static`; the complementary `BUILD_PLAYERBOTS=OFF`, `MODULES=disabled` `mangosd` build passed; the local runtime reached `TortoiseBots: native module loaded (AI enabled)` and `World server is up and running` after applying the three pending non-destructive world migrations required by the preserved database. Generated flags prove TortoiseBots definitions/includes/PCH are on `mod_tortoisebots_static`, not the combined `modules` target.

Explicit gaps at that earlier checkpoint: `AutoMaintenanceOnLevelupAction`, advanced `FishingAction`, `InventoryAction`/`TellEmblemsAction`, `NonCombatActions`, guardian-oriented `PetsAction`, `TellPvpStatsAction`, extended trade reporting, donor `TradeValues.cpp`, and expansion-only LFG/glyph/Karazhan/vehicle/Arena registrations remained excluded where they required WotLK/AzerothCore APIs or unsupported host data. Native loot, quest, inventory operations, travel, trade, class combat, pet-taming, and lockpicking paths are now compiled; no empty gameplay stubs were added.

Architecture note: core integration is generic Headless transport/session lifecycle plus ScriptMgr hooks. `MODULE_TORTOISEBOTS=static` selects the native module and does not pull the legacy vendored CMaNGOS tree; `BUILD_LEGACY_PLAYERBOTS` is a separate explicit escape hatch.

## Native runtime and Vanilla/Tortoise behavior checkpoint — 2026-08-24

Feature: Pet taming/control, lockpicking, bounded random-bot lifecycle, AH/economy pricing, named-location travel lookup, cache-safe startup, and native module SQL packaging

Source repository: local `TortoiseBots` checkout; host/runtime seam in the local `tortoise-wow` checkout

Source commit: TortoiseBots native stabilization work after `6a78b5c`; behavior references `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7c943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; required core seam `9487c5150a6553c665fafc1f4568669b8b00f011` on `playerbots-integration-gh`

Source files: `TameAction.*`, `UnlockItemAction.*`, `UnlockTradedItemAction.*`, `ChatActionContext.h`, `WorldPacketActionContext.h`, `runtime/RandomBotService.*`, `runtime/PlayerbotRuntimeFacade.cpp`, `ai/playerbot/RandomItemMgr.cpp`, `data/sql/{world,char}/*`, `TortoiseBots.cmake`, `host/BotHostAdapter.cpp`, `conf/tortoise_bots.conf.dist`

Copied / ported / independently reimplemented:

- Tame-beast behavior was independently reimplemented around the host's real `SPELL_EFFECT_TAMECREATURE` path; rename and abandon use the native `Pet`/`Player` APIs. The old WotLK pet-stable construction was not retained.
- Lockpicking was ported to `ItemPrototype`, `LockEntry`, `ITEM_DYNFLAG_UNLOCKED`, the native Pick Lock spell, and the native trade-slot path. The old AzerothCore `ItemTemplate`/extended trade wrappers were not retained.
- Random bots use a module-local, startup-loaded pool of pre-existing characters on the configured random-account prefix. `World` owns Headless/Network session lifetime; `BotManager` owns module records and AI adapters. Account/character creation and donor login managers remain intentionally outside the module.
- Random-bot buy/sell multipliers are now cached per character with the Existing Vanilla ranges, and named-location lookup uses the native `ai_playerbot_named_location` table instead of a compatibility no-op.
- Empty optional item/equipment caches are accepted without synchronous world-thread cache generation. Populated existing caches still load normally.
- Schema-only native migrations cover the tables queried by the active Vanilla/Tortoise AI initializer and per-bot state. Existing datasets remain deployable separately.

Reason: complete coherent Vanilla/Tortoise families without reintroducing donor manager/session ownership or making optional AI startup depend on a large synchronous cache write.

Local validation: ON/static `mangosd` build passed after the cache, config, SQL-install, installed-module-path, economy, recovery, packet, command, and class-context changes; OFF/disabled `mangosd` build passed and the ON/static configuration was restored. Docker runtime with AI enabled loaded the module, attached real Warrior/Mage/Priest/Hunter contexts, passed packet-bridge group invite/accept and cleanup journeys, and retained the earlier save/logout/relog spike evidence. The preserved DB was not reset; only additive missing schema migrations and disposable `TBPLAY` class fixtures were added. Random pool startup correctly reported zero candidates because no `RNDBOT*` accounts exist in the fixture.

Known scope gates: the physical tree and positive CMake graph contain no DK,
glyph, vehicle, Arena, Karazhan, or expansion-only donor families. Native
core LFG/meeting-stone behavior remains available; the module retains only
applicable group-role helpers and does not recreate the donor automatic queue.
The donor `PetsAction` guardian-control wrapper and post-Vanilla fishing
wrapper remain excluded because native pet, fishing, travel, loot, and
profession paths provide the applicable Vanilla/Tortoise behavior.
Account/character auto-creation remains the intentional random-bot product
gap; existing random characters, bounded login/logout, native TravelMgr
relocation, AI strategy rotation/recovery, gear refresh, and AH/economy pricing
are supported. Core `BattleGroundMgr` remains authoritative for Vanilla and
Tortoise battleground entries; the existing value compatibility view reads the
same native `battlemaster_entry` table once at AI startup and does not own
battleground state.

## Packet/config/static-isolation and playtest gate — 2026-08-24

Feature: generic packet/event bridge, safe multi-value configuration, isolated native static-module settings, existing command surface, random-bot debt cleanup, and fresh class journeys.

Source repository: TortoiseBots `phase4-follow@3484208`; required upstream core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`).

Source files: `host/BotPacketAdapter.*`, `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.cpp`, `runtime/PlayerbotRuntimeFacade.cpp`, `runtime/RandomBotService.*`, `commands/BotCommands.cpp`, `ai/playerbot/PlayerbotAIConfig.*`, `ai/playerbot/strategy/ValueMacros.h`, `ai/playerbot/AiFactory.cpp`, `TortoiseBots.cmake`, `conf/tortoise_bots.conf.dist`, and core `modules/CMakeLists.txt`/`Config`.

Copied / ported / independently reimplemented: the packet calls preserve the existing `PlayerbotAI` handlers but the mapping and ownership are independently implemented through `PlayerbotAIStorage` and `BotManager`; Core `Config::GetValues` is a generic core API over ACE configuration enumeration; static isolation is a generic per-static-module OBJECT-target mechanism.

Historical runtime evidence: fresh AI-enabled Docker runs emitted bot outgoing `SMSG_GROUP_INVITE`, master outgoing `SMSG_PARTY_COMMAND_RESULT`, and a synthetic master-incoming diagnostic, with existing group invite/accept success and cleanup. The same journey instantiated real `WarriorAiObjectContext`, `MageAiObjectContext`, `PriestAiObjectContext`, and `HunterAiObjectContext`; the Hunter no-network diagnostic used the existing `accept invitation` action directly after packet delivery because activity scheduling is intentionally not treated as human-client evidence. `Loading WorldBuffs` proves the real multi-value config reader. This evidence is retained for provenance, not final incoming-hook acceptance.

This checkpoint is historical and is superseded by the final correctness pass
below: the direct incoming diagnostic and Hunter direct-action diagnostic are
not final acceptance evidence.

Migration cleanup: removed `MinimalPlayerbotAI*`, `VerticalSlice*`, `cmangos-compat-shim.h.orig`, and all tracked `*.shyalya.bak` copies after retaining donor provenance above. The obsolete `BotController` has also been removed; `PlayerbotAI` is the sole gameplay update owner.

Intentional gaps only: random account/character auto-creation and the
post-Vanilla fishing wrapper remain outside the Vanilla/Tortoise product
surface. A real human-client journey was not claimed from the automated
server-side packet fixture; the preserved runtime is left AI-enabled and
ready for manual client playtesting.

## Final pre-playtest correctness pass — 2026-08-24

Feature: movement-mode coherence, strict packet-trigger acceptance, human
master reconnect rebinding, controller cleanup, canonical random timing key,
and concrete PlayerbotAI value null-safety.

Source repository: TortoiseBots `phase4-follow@e0da302058cb1e5021c92272bf22983b2b5ad073`; required
upstream core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`, with `73f32c063e6c4481a0415690896025178ca8076f` as the original seam commit).

Source files: `ai/playerbot/PlayerbotAI.{h,cpp}`,
`runtime/{BotManager,PlayerbotAIAdapter}.*`,
`host/BotPacketAdapter.*`, `commands/BotCommands.cpp`,
`ai/playerbot/PlayerbotAIConfig.cpp`,
`ai/playerbot/strategy/actions/CheckMountStateAction.cpp`,
`ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp`,
`ai/playerbot/AiFactory.cpp`, and `ai/playerbot/strategy/actions/AcceptInvitationAction.h`.

Copied / ported / independently reimplemented: movement transition
centralization and reconnect rebinding are independent module work around the
existing existing `FollowMasterStrategy`/`ChatShortcutActions` semantics; the
strict fixture is an independent runtime assertion over the existing packet
bridge; the null-safety changes are local defensive corrections.

Local validation: cached `BUILD_PLAYERBOTS=ON`, static native `mangosd` passed;
cached `BUILD_PLAYERBOTS=OFF`, `BUILD_LEGACY_PLAYERBOTS=OFF`, and
`MODULES=disabled` `mangosd` passed. The final AI-enabled Docker binary reached
world-ready, and the strict packet fixture passed automatic existing group
invite/accept plus cleanup without a direct accept-action fallback. The
fixture intentionally does not synthesize `CanPacketReceive`; a real client
incoming event remains the manual playtest gate. No client login was automated
in this pass at the user's request.

Remaining intentional gaps are unchanged: random account/character creation
and the post-Vanilla fishing wrapper. The next action is manual playtesting of
the real Network client path, including incoming packet delivery and
reconnect/reclaim behavior.

Core reproducibility: check out `playerbots-integration-gh` at
`9487c5150a6553c665fafc1f4568669b8b00f011` (parent
`133c6d19bf5898c1e4f5129b2890b1db89b17a07`). The final core commits remove
`MODULES_PUBLIC_INCLUDES` from the combined static archive target and remove
the stale `src/game/PlayerBots` common path, so unrelated static modules do not
inherit a selected module's include directories; the selected OBJECT target
still receives its own module settings. The configured
`Shyalya/tortoise-wow` fork rejected publication with HTTP 403, so this exact
local commit must be applied from a writable core fork/PR before reproducing
elsewhere.

## Narrow post-review cleanup pass — 2026-08-24

Feature: existing `.bot stay` anchors, existing-AI-authoritative reconnect,
durable random-bot master bind/clear, and corrected packet-fixture wording.

Source files: `commands/BotCommands.cpp`, `runtime/BotManager.{h,cpp}`,
`runtime/PlayerbotAIAdapter.{h,cpp}`, `ai/playerbot/PlayerbotAI.{h,cpp}`,
`ai/playerbot/strategy/actions/AcceptInvitationAction.h`,
`ai/playerbot/strategy/actions/LeaveGroupAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundJoinAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundTactics.cpp`, and the current
status/host-boundary documentation.

Copied / ported / independently reimplemented: native commands now reuse the
existing existing `StayChatShortcutAction`/`FollowChatShortcutAction` actions;
reconnect treats the existing existing strategy set as authoritative and only
applies the default when no movement strategy exists. `BindBotMaster` and
`ClearBotMaster` are small module-local lifecycle operations; they do not add
core fields or replace Headless sessions.

Local validation: cached ON/static `mangosd` passed after the coherent edit
batch; the earlier AI-enabled image reached world-ready and supplied the
pending add/remove and packet-bridge evidence. The final native image also
built, linked, installed, and reached world-ready with the preserved database,
but used the Docker wrapper's AI-off default, so it is not claimed as a fresh
AI fixture run after the final synthetic-headless-master authorization. No
database or Docker volume was reset. The real-client journey remains
intentionally unperformed in this pass.

## Final merge-hardening pass — 2026-08-24

Feature: current-scope ownership cleanup, controller removal, explicit native
source selection, optional-data startup safety, disposable-fixture guards,
packet lifetime safety, and diff hygiene.

Source repository: TortoiseBots `phase4-follow@7fd7a35`.
Required core remains `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.*`,
`runtime/PlayerbotRuntimeFacade.cpp`, `commands/BotCommands.cpp`,
`host/{BotHostAdapter,BotSessionAdapter}.cpp`, existing ownership-transition
actions, `host/BotPacketAdapter.cpp`, asynchronous packet delivery in
`ai/playerbot/PlayerbotAI.cpp`, the active compatibility shims,
`TortoiseBots.cmake`, `ai/playerbot/{TravelNode,RandomItemMgr}.cpp`, and
current host/provenance documentation.

Local validation: `git diff --check` passes; no active `BotController` or
legacy core ownership symbols remain; the final native image built, linked,
and installed successfully and its preserved Docker stack reached world-ready
with module SQL migrations applied. The AI-enabled runtime reached Headless AI
attachment and automatic invite acceptance, but the pre-catch packet run then
terminated on a malformed party packet before cleanup. The follow-up rebuild
containing the packet safety catch was intentionally stopped at the user's
request, so a fresh post-catch AI runtime pass remains unclaimed. No database
or Docker volume was reset.

## Vanilla/Tortoise source cleanup — 2026-08-24

Feature: subtractive product cleanup from the multi-expansion donor tree.

Source repository: TortoiseBots `cleanup/vanilla-tortoise` working branch.

Source commit: `322120ef1fe9b848cfe520ad58f6ff698fa801e9` (the cleanup commit;
this follow-up metadata commit records its exact SHA).

Source files: `TortoiseBots.cmake`, `ai/playerbot/*`, the nine class strategy
folders, `runtime/*`, `conf/*`, `data/sql/*`, `README.md`, and current module
documentation.

Copied / ported / independently reimplemented: no new gameplay was imported.
The cleanup removed physical Death Knight, donor manager/login/command-server,
test, glyph, rune-forging, vehicle, Arena, Karazhan, automatic donor-LFG,
advanced-fishing, and duplicate food/inventory families. It also removed
later-expansion registrations and class actions while retaining core-backed
Tortoise custom spells (Druid Eclipse/Tree of Life/Mangle, Shaman Bloodlust/
Earth Shield/Water Shield, Warrior Intervene), all nine Vanilla classes,
Vanilla raids, WSG/AB/AV tactics, native transport/taxi travel, and native
core LFG/meeting-stone/group-role concepts. The former
`RandomPlayerbotMgr` name was replaced by the narrow `RandomBotFacade`; native
`BotManager` and `RandomBotService` remain the ownership boundaries.

Reason: make the physical tree, active registrations, configuration, and
compatibility surface read as one Vanilla/Tortoise product rather than a donor
tree hidden behind subtractive CMake filters.

Local validation: the persistent sibling builder's cached `BUILD_PLAYERBOTS=ON`
and complementary `BUILD_PLAYERBOTS=OFF` `mangosd` targets both compiled and
linked successfully. The ON wrapper's optional install step reports only that
`realmd` is not built; the `mangosd` artifact is produced. The existing Docker
stack was restarted from that artifact without an image rebuild or data reset;
the native module loaded and the world server reached ready. No manual
gameplay test was run, as requested.

## Surgical dead-code follow-up — 2026-08-24

Feature: residual Vanilla/Tortoise cleanup after the main donor-tree removal.

Source repository: TortoiseBots `cleanup/vanilla-tortoise`.

Source commit: `3213a058931ec7b88c87322cb11f02df4ffed8e1` (follow-up cleanup
commit; this metadata commit records its exact SHA).

Copied / ported / independently reimplemented: no new gameplay was imported.
This pass removed the unreachable legacy movement body, the dead spell-click
path, fake gem/socket qualifier and weight compatibility, the WotLK DK quest
special case, unused DK talent enum slots, and adjacent no-op branches.

Reason: remove concrete dead remnants identified during review without
starting another broad cleanup audit.

Local validation: targeted static checks and one cached persistent
`BUILD_PLAYERBOTS=ON` native `mangosd` build; no runtime restart or gameplay
test was performed.

## Tortoise audit closure pass — 2026-08-24/25

Feature: close the module-owned Tortoise WoW 1.18.1 audit findings without
reintroducing core ownership coupling: effective configured SQL packaging, additive
schema repair, fail-closed startup caches, owner-input SQL safety, collection
mount lookup, later-expansion residue removal, and repeatable surface checks.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `7e08fc810060e77839d4f38c813cc7eba9b05737` (final verified
implementation snapshot; the later provenance/docs update is documentation-only;
core-backed gossip/taxi/loot adapters, custom-start re-enable, talent validation
fixes, dead-shim cleanup, and native command fixture implementation; implementation `b76b5f4bf236b4d1bf370f0997e88cf30fd33695`, `fix: bound
engine action logging`), on top of `b863c6eedd3514a525f40e243bb8a61b2244fbe8` (`fix:
remove unreachable engine test logging`), `89a5e645e1485bd2e35b4944e88fdadfc6c95d05`
(`fix: remove remaining expansion-only item branches`), `887a6673675d06d716acc713aaeed8dca05d7e9f`
(`build: report native module source identity`), `9605a73c9bc16f0bf4fb4e84bba974a70f68c735`
(`fix: disable fish cache rebuild on startup`), `a6ea16605fde1b77e396ca588e0b34ddb1978bd5`
(`fix: align movement and channel shims with Tortoise core`),
`7fa875a6c6bc51534b4a5a3f2f373f3dd7446208` (`fix: quarantine optional LLM
and stale tooling paths`), `2afd2d1` (`fix: match effective core SQL paths
and migration history`), and the preceding
`9db49df` and `3a96923` remediation commits.

Required target core: local `tortoise-wow`
`playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `TortoiseBots.cmake`, `README.md`,
`ai/playerbot/{PlayerbotAI,PlayerbotAIConfig,PlayerbotDbStore,PlayerbotFactory,RandomItemMgr,TravelMgr,TravelNode}.{cpp,h}`,
  the edited strategy/action/value/context files, `data/sql/{world,char}/*`,
`ai/playerbot/{ServerFacade.cpp,cmangos-compat-shim.h}`, the follow/movement
and range-trigger files, `ai/playerbot/aiplayerbot.conf.dist.in`,
`runtime/BotManager.cpp`, `conf/tortoise_bots.conf.dist`,
`tools/{analyze_quest_ledger.py,verify_tortoise_surface.sh}`, and
`docs/PLAYERBOTS_AUDIT.md`.

Copied / ported / independently reimplemented:

- No new upstream gameplay was copied in this pass.
- Existing existing behavior was kept where the local core data validates it;
  the factory collection-mount selection is an independent module adapter over
  the core `collection_mount` table and `MountManager` contract.
- Removed RTSC/SeeSpell/BossAura and later-ID branches are subtractive cleanup,
  not replacements with expansion behavior.
- Movement inspection now uses the local core's public targeted-generator
  target/current-motion contract; current follow/chase guards no longer depend
  on fabricated zero offsets or private donor fields. The chat-channel proxy
  delegates to the core's loaded `ObjectMgr` channel map.
- The dead `InstanceTemplate`, synthetic session-state, formation-slot,
  client-loot-type, group-roll, and donor `TransportAnimation` scaffolding was
  removed. Empty-path elevator generation now logs and skips explicitly because
  the pinned core has no transport-animation loader. Custom-start travel and
  death handling now use the core's Goblin/High Elf start rows after the actual
  runtime terrain/MMAP tiles were verified present. The exact High Elf VMap
  tile is absent and remains an explicit acceptance concern.
- Talent validation now sums all three trees, rejects missing prerequisites
  without dereferencing absent records, and initializes each DBC row's rank
  metadata independently.
- SQL changes are module-owned schema and additive compatibility migrations;
  the final `20260824090003_*` cleanup explicitly drops only obsolete,
  module-owned donor cache tables; no character state or database reset is
  involved.

Reason: the module must be a trustworthy Tortoise 1.18.1 foundation. Core
`LFTBotFill`, legacy `src/modules/PlayerBots`, and the remaining compatibility
fallback matrix are intentionally recorded as separate core/product follow-up,
not hidden inside this module PR.

Local validation:

- `tools/verify_tortoise_surface.sh` passed.
- `git diff --check` passed before commit.
- Cached `bash ../tortoise-docker-penqle/dev/build-playerbots` completed the
  static native module and `mangosd` link successfully. Its best-effort install
  phase reported only the sibling builder's absent `realmd` artifact.
- Cached `bash ../tortoise-docker-penqle/dev/build-off` completed the
  `BUILD_PLAYERBOTS=OFF`, `MODULES=disabled` `mangosd` build successfully.
- A disposable `git archive` of the tracked target core, with no
  `modules/TortoiseBots` checkout, configured as `modules: disabled (no
  modules found)` and built/linked `mangosd` successfully with both PlayerBots
  options off. The temporary archive/build directories were removed.
- The final incremental ON rebuild after the cache fail-closed and data-derived
  eligibility edits also linked successfully; the same optional `realmd`
  install warning remained.
- The final cached OFF rebuild after those edits completed `mangosd`
  successfully with `BUILD_PLAYERBOTS=OFF`, `BUILD_LEGACY_PLAYERBOTS=OFF`, and
  `MODULES=disabled`.
- A disposable MariaDB 11.4 container applied both configured `world` and
  `character` migration pairs twice; the final schemas had 32 scale columns,
  `template_changed`, and zero obsolete donor tables.
- The preserved Docker stack processed the configured lowercase module paths,
  applied both `20260824090003_*` cleanup migrations, reached AI-enabled
  world-ready, passed `PendingAddRemoveTest`, the six-step `AutoTest`, and the
  packet group invite/accept plus cleanup journey. No volume reset was used.
- The final ON rebuild after making the optional LLM generator inert by
  default, the complementary OFF rebuild, and a preserved-data server restart
  all passed. Startup no longer attempts to load the optional LLM prompt file;
  the module still reached AI-enabled world-ready with the empty-cache and
  direct-travel safeguards.
- The subsequent full cached ON rebuild after the movement/channel shim fix,
  the complementary OFF build, and a preserved-data restart also passed. The
  focused packet fixture then exercised `follow chat shortcut`, native group
  invite/accept, and cleanup on the updated binary without a movement-state
  crash. Startup retained the expected core warning that custom dungeon rows
  reference the missing `custom_dungeon_portal` script; no teleport behavior
  was invented in the module.
- The final fish-generation hardening rebuild passed both ON/OFF gates and a
  preserved-data restart. The current startup logged `No persisted fish
  locations; generation is disabled, using direct fishing fallback.` and did
  not log fish-grid generation or cache-save activity.
- The final expansion-residue pass removed the local-core-absent Mage mana-gem
  IDs `22044`/`33312`, Druid reagent IDs `22147`/`22148`, and post-60 lifetime
  formulas. The cached ON/OFF builds and preserved startup remained clean;
  local SQL confirmed `33312` is a non-mana item and the other three IDs are
  absent from the target item data.
- The unreachable `Engine::testMode` branch and its `test.log` writes were
  removed. The final ON/OFF builds and timestamp-scoped preserved restart
  reached world-ready with no test-file path or test-mode log activity.
- `Engine::LogAction` now uses bounded `vsnprintf` formatting, preventing
  long owner-controlled action names from overrunning its fixed log buffer.
- The corrected final packet fixture run passed the native command surface:
  `list`, `stats`, and owned-bot `follow` were dispatched through a synthetic
  `ChatHandler`, followed by group invite/accept and cleanup. This is runtime
  command-path evidence, not a real-client incoming-packet claim.
- The compatibility shim's ScriptDevAI-shaped gossip callback now delegates to
  core `sScriptMgr` creature-gossip registry; it no longer unconditionally
  returns false and discards core gossip behavior.
- The compatibility shim's taxi view now reads the live core
  `Player::GetTaxi().GetTaxiPath()` route for in-flight position reasoning, and
  loot status checks pass the native loot target into the core's ownership and
  condition evaluator. No focused taxi/loot gameplay journey is claimed from
  this compile/core-trace change.
- A forced CMake configure prints the supported builder's bind-mounted module
  root `/work/core/modules/TortoiseBots`, commit, and clean/dirty source state;
  Git's scoped safe-directory option avoids changing global configuration, and
  a dirty checkout is reported explicitly rather than being mistaken for an
  exact clean snapshot. This makes stale or locally modified module selection
  observable. The final implementation snapshot is `7e08fc8`.
- The real Tortoise client was launched under Wine through normal and
  software-forced rendering paths; both rendered black with no observable
  login UI in this environment, so no real-client `.bot` command journey is
  claimed.
- The final timestamp-scoped preserved-data restart of the updated binary
  reached native AI module load and world-ready. It showed the expected direct
  travel/fishing and empty-cache safeguards, emitted no `ai_playerbot_*` table
  DDL/DML, and retained the known core `custom_dungeon_portal` script warning
  for the audited custom-content gap. Taxi/loot interactions were
  not replayed as a ceremony; their module changes were compile- and
  core-API-traced.
- The updated runtime restart at `2026-08-25T01:41:31.245096072Z` reached
  native AI module load and world-ready with no talent-spec validation errors,
  no `ai_playerbot_*` table DDL/DML, and the expected missing-core
  `custom_dungeon_portal` warning. The current runtime data checks found the
  Goblin start `maps/0013245.map` + `mmaps/0013245.mmtile` and High Elf start
  `maps/0002536.map` + `mmaps/0002536.mmtile`; `vmaps/000_25_36.vmtile` is not
  present.
- The updated disposable packet fixture at `2026-08-25T01:48:13.297552013Z`
  passed native `list`/`stats`/`follow`, existing group invite/accept, and
  cleanup. Its temporary `PacketBridgeTest` enablement was restored to `0`,
  and the normal restart at `2026-08-25T01:48:57.408768993Z` reached
  world-ready.

## Final traced Tortoise compatibility closure — 2026-08-25

Feature: close the remaining module-owned Tortoise 1.18.1 compatibility
mismatches found by tracing active call sites against the pinned core: sparse
store bounds, core-defined custom races, path-filter fail-closed behavior,
native combat/interaction/auction/quest/skill semantics, later-ID cleanup,
localized names, factory class-spell initialization, native text-emote fallback,
loot status/roll state, and collection-mount caching.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `d672048e86b9effc36210d3e6d076741fbeccc7f` (final source snapshot;
the initial traced implementation is `0f97403df42ee98b5085040a9a066ddc64608623`,
followed by `f594fc1` removing the unreachable fish-cache generator and
`d672048` closing the remaining active emote, loot, locale, spell-error, and
collection-mount fallbacks).

Reference repositories and commits:

- Local core: `tortoise-wow@9487c5150a6553c665fafc1f4568669b8b00f011`
  (`playerbots-integration-gh`), used as the API/data authority; no core file
  was modified by this commit.
- Local CMaNGOS Classic PlayerBots host reference:
  `cmangos-mangos-classic@9b682be617ac61c127c23aa60d7b4ffbc0ce37e6`,
  specifically the `Player::learnClassLevelSpells` behavior used as intent for
  the module-local factory learner. The host implementation was not copied
  into the core.

Source files: `ai/cmangos-compat-shim.h`,
`ai/playerbot/{ChatHelper,PlayerbotAIConfig,PlayerbotFactory,TravelMgr,TravelNode,WorldPosition}.{cpp,h}`,
`ai/playerbot/strategy/{Value.cpp,values,actions,triggers,druid,rogue,warrior}/*`,
`runtime/PlayerbotRuntimeFacade.cpp`, and
`tools/verify_tortoise_surface.sh`.

Copied / ported / independently reimplemented:

- Store upper bounds, DBC race-name loading, locale-map formatting, path
  fail-closed guards, native wrapper substitutions, and local heal prediction
  are independently reimplemented against the pinned core APIs.
- Class trainer/quest spell initialization is a module-local port of the
  existing CMaNGOS behavior, narrowed to the core's `Quest`, `TrainerSpell`,
  `SpellMgr`, talent, and class/race contracts. It does not add a
  `PlayerBots`-specific core hook.
- Absent expansion IDs and invalid Tortoise branches are subtractive cleanup,
  validated against local DBC/SQL; no expansion behavior was introduced.

Reason: preserve Existing Vanilla behavior while ensuring that Tortoise custom
IDs, Goblin/High Elf data, localized content, and native core semantics are
not silently hidden behind donor-era constants or no-op compatibility methods.

Local validation: `tools/verify_tortoise_surface.sh`, `git diff --check`, and the
cached persistent `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, static
`mangosd` build/link passed at this source commit. The build compiled the
module and linked the final `mangosd`; no core, Docker, reference checkout, or
database reset was performed. Runtime restart evidence for this exact commit
is recorded in the audit after installation: the preserved stack restarted at
`2026-08-25T04:01:18.842808942Z` and reached world-ready at
`2026-08-25T04:01:47.286072886Z`, with
native module load, TalentSpecs load, direct travel/fishing fallback, and no
module-table DDL/DML. The restart also confirmed the pinned core's
unregistered-content script warnings; no module replacement was invented.
Disposable custom-start fixtures then passed native AutoTest: Goblin guid 7
passed from `04:10:16.382Z` through cleanup at `04:10:49.840Z`, and High Elf
guid 8 passed from `04:12:11.086Z` through cleanup at `04:12:44.559Z`. The
fixture rows/state were removed and `AutoTest` restored to `0`; the normal
restart at `04:14:19.562258645Z` reached world-ready at `04:14:40.631920103Z`.
No unrun gameplay acceptance is claimed for terrain movement/death, physical
collection-mount use, taxi, loot, or real-client packet delivery.

## Surface verifier fail-closed correction — 2026-08-25

Feature: make `tools/verify_tortoise_surface.sh` fail closed when its required
ripgrep dependency is unavailable, so the surface/audit gate cannot report a
false success after `rg` returns command-not-found.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `9e9567c996d1cbf5c2c3f5949453499589600d4e` (implementation
commit; the subsequent audit/provenance edit is documentation-only).

Source files: `tools/verify_tortoise_surface.sh`.

Copied / ported / independently reimplemented: independently implemented as a
single `command -v rg` prerequisite check before repository setup and all
`rg`-based checks. No compatibility stub, replacement search implementation,
or core change was added.

Reason: with `set -e` and `if rg ...; then` conditions, a missing `rg` can be
treated as an ordinary false condition and allow the final success message to
be printed. The verifier must fail closed so its audit/provenance evidence is
meaningful.

Local validation:

- Missing-ripgrep negative test:
  `env -i PATH=/tmp/tortoisewow-no-ripgrep /bin/bash tools/verify_tortoise_surface.sh`
  exited `1` and printed
  `TortoiseBots surface check failed: ripgrep (rg) is required to verify the
  Tortoise module surface`; it did not print `Tortoise WoW 1.18.1 module surface:
  OK`.
- Full verifier with ripgrep available:
  `bash tools/verify_tortoise_surface.sh` exited `0` and printed
  `Tortoise WoW 1.18.1 module surface: OK` on the current source tree.
- `git diff --check` exited `0` for the implementation correction.
- No C++ build, Docker image rebuild, database migration, gameplay fixture, or
  runtime test was run for this shell/docs-only change.

The earlier closure entries that record the verifier as passed are retained as
historical normal-environment evidence. They did not test the missing-rg path;
the explicit negative and positive results above are the authoritative
validation for this correction. F-03 and F-27 remain open core/data follow-ups;
this change does not add scripts, stubs, or module-side replacements for them.

## F-03/F-27 core integration closure — 2026-08-25

Feature: remove the remaining legacy PlayerBots-specific core product surface
and reconcile the locally provable Tortoise ScriptName mismatches.

Source repository: local `tortoise-wow` core plus its local Tortoise SQL;
TortoiseBots module checkout for the optional-module build.

Source commit: core
`7353989c94399f80572a2f8ec2eb73c63a6c79f8` on
`cleanup/f03-f27-code-freeze`; TortoiseBots code checkpoint
`07cf7976c546fac27083c7b46e73299c25b095f3` on the same-named branch, followed
by the final documentation commit.

Source files: core `CMakeLists.txt`, `src/game/{CMakeLists.txt,Chat,Handlers,
LFT,Objects,ScriptMgr.h,SessionTransport.h,SharedDefines.h,Spells,World*,
vmap}`, `src/mangosd/{CMakeLists.txt,Master.cpp,mangosd.conf.dist.in}`,
`src/scripts/{CMakeLists.txt,miscellaneous/random_scripts_1.cpp,
spells/spell_druid.cpp}`, `tools/vmap_assembler/CMakeLists.txt`, and
`sql/database_updates/world/20260825090000_world.sql`; module
`ai/playerbot/PlayerbotHelpMgr.cpp` and
`ai/playerbot/strategy/actions/DebugAction.cpp`.

Copied / ported / independently reimplemented:

- No upstream behavior was copied.
- F-03 is subtractive core cleanup plus replacement of account-name checks with
  the existing generic `Script_IsMachineDriven` capability. No new PlayerBots
  host seam was introduced.
- F-27 `npc_teslinah` is a registration of the existing local callback; it is
  not a new implementation. The `script_name='0'` migration is an independent
  data correction for an invalid placeholder. The remaining unregistered
  Tortoise names were deliberately not implemented because their behavior is not
  established by the pinned core/history.

Reason: keep Tortoise core generic and optional, make TortoiseBots the only
supported PlayerBots implementation, and avoid converting missing Tortoise
content into fake success paths.

Local validation:

- Cached native ON/static `mangosd` build passed with
  `BUILD_LEGACY_PLAYERBOTS=OFF`.
- Cached native module build passed with `BUILD_PLAYERBOTS=OFF`,
  `BUILD_LEGACY_PLAYERBOTS=OFF`, and `MODULE_TORTOISEBOTS=static`.
- Cached module-disabled build passed with `BUILD_PLAYERBOTS=OFF`,
  `BUILD_LEGACY_PLAYERBOTS=OFF`, and `MODULES=disabled`.
- Preserved Docker runtime applied migration
  `20260825090000_world` (hash
  `9DD6905D83E17F6D0BD08CABC5618BBD2A5AD513`), loaded the native module, and
  reached `World server is up and running`.
- Runtime query found zero literal `script_name='0'` rows across all seven
  ScriptMgr registry tables and retained two `npc_teslinah` rows.
- Startup no longer reports `0` or `npc_teslinah`; the remaining 17 warnings
  are recorded as unverified content gaps in `PLAYERBOTS_AUDIT.md`.

## Penqle #411/#416 compatibility and stack checkpoint — 2026-08-26

Feature: align the complete optional TortoiseBots stack with the Penqle
Headless/session surface from #411 and the generic character/LFT/BG surface
from #416, without adding PlayerBots concepts to core.

Source repositories and commits: tortoise-wow/tortoise-wow #411
`c37e28b632dee3c73896240c1b399fdbb7c35ef8`; generic core PR #416
`5261e5317c3115aa8a0b61d8eb9d85a79766be95`; TortoiseBots stack heads #37
`c2893d3cb3b561988cbc163009d58b91194ac5f9` through #42
`2abd4f7eea50c740597681b32b5af9bdab58e427`.

Source files: module-local `ai/playerbot/*`, `runtime/*`, `host/*`,
`commands/*`, and feature configuration/documentation. Core #416 adds only
bot-neutral public lifecycle/snapshot interfaces and a generic
`Player::GetHomeBindLocation()` getter over existing character state.

Copied / ported / independently reimplemented: donor API calls were replaced
with the target core's public APIs (native distances, loot/mail, gossip,
trainer, transport/spline, battleground, and packet handlers). Missing
behavior was fail-closed or routed through existing module facades; no private
BG/LFT/AH state, fake queue fallback, raw character SQL, or PlayerBots-aware
core coupling was added. The BG service now consumes #416's copy-only demand
snapshot and queues only for observed human demand.

Reason: preserve module ownership and core optionality while making the broad
Vanilla/Tortoise donor behavior compile against the actual target core API shape.

Local validation: `git diff --check`, Tortoise surface audit, parent-diff audits,
GitHub `CLEAN/MERGEABLE` audit for #37–#42, and cached integrated module target
passed. Full native ON/static `mangosd` linked with `BUILD_PLAYERBOTS=OFF`,
`BUILD_LEGACY_PLAYERBOTS=OFF`, `MODULES=static`,
`MODULE_TORTOISEBOTS=static`; complementary `MODULE_TORTOISEBOTS=OFF`
mangosd also linked. The installed ON binary accepted `--version`. No live
server/gameplay smoke test was run; this remains pre-merge evidence until the
actual core #411/#416 commits are merged and rebuilt.

## Cleaned merge candidate — 2026-08-29

Feature: remove the accidental PR #43 dependency and migrate the complete
module stack to the manager-owned Headless lifecycle.

Source repositories and commits:

- tortoise-wow/tortoise-wow PR #411 candidate `f62cd95b63439ebdc7915053016ffa2753f98121`
  (rebased on upstream `main` `05912a49f7cd8f12afff04b3c37e6f852f981268`).
- tortoise-wow/tortoise-wow PR #416 candidate
  `5368fda0d884b4ff91960772ebf8e3f65f991850`, based on the cleaned #411
  candidate.
- TortoiseBots tested code `c9643eb14eed09c21aea087950ffef5460f239b0`;
  subsequent changes in this branch are documentation-only.

The final module branch was reconstructed from the PR #42 tip. PR #43's
pullback/summon implementation is not present. The module now calls only the
generic core `StartHeadlessSession` / `StopHeadlessSession` /
`GetHeadlessSessionState` façade; it does not construct, dispatch, promote,
log out, or delete Headless `WorldSession` objects.

Local validation:

- `git diff --check` passed for the cleaned core and module candidates.
- `tools/verify_tortoise_surface.sh` passed.
- Docker native static build passed with `BUILD_PLAYERBOTS=OFF`,
  `MODULES=static`, and `MODULE_TORTOISEBOTS=static`, reaching
  `[100%] Built target mangosd`.
- Complementary module-disabled build passed with `BUILD_PLAYERBOTS=OFF`,
  `MODULES=disabled`, and `MODULE_TORTOISEBOTS=disabled`, reaching
  `[100%] Built target mangosd`.
- The enabled image loaded `TortoiseBots (AI enabled)` and reached
  `World server is up and running!` without fatal startup errors.
- The temporary runtime stack was stopped without resetting or removing
  database volumes.

No real-client login, reconnect/reclaim, stale-callback adversarial probe, or
live LFT/BG/AH gameplay acceptance is claimed by this checkpoint.

## Corrected core review follow-up — 2026-08-29

Core review fixes were applied after the previous candidate:

- removed the stale `src/game/PlayerBots` include from module build glue;
- restored full GPL headers on the new generic core headers;
- preserved the normal Network character-list count during extracted
  character creation and removed the unintended account-limit behavior;
- used `SessionTransport::Headless` for the transient character-creation
  helper;
- reattached the Player to the replacement Network session before destroying
  the manager-owned Headless session.

Corrected candidates:

```text
Core #411:   8037fc8cc4c8c5734aafb9dc43858205bcf9051f
Core #416:   e63161c2da7f13ab25687ea389026aa2e3c97647
Module:      b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

The corrected pair passed the native static and module-disabled Docker builds.
The corrected enabled image loaded TortoiseBots and reached
`World server is up and running!` without fatal startup errors. No database
volume reset was performed.

Real-client login/reconnect/reclaim and live LFT/BG/AH gameplay remain
unclaimed manual acceptance gates.

## Interrupt action shell — 2026-09-05

Feature: player-facing `.bot action interrupt` with server-side executor
selection and addon control.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, representative
  class interrupt strategy/action registrations under `src/Ai/Class/*` and
  `src/Ai/Base/Actions`/`Triggers`.
- `Shyalya/tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b` and
  `cmangos/playerbots@076045efa835da9aab7c943bca752aebe1baad`, used as
  behavior comparisons only.

Source files: `commands/BotCommandContext.{h,cpp}`,
`commands/BotCommands.cpp`, and the companion manager's
`Constants.lua`, `UI.lua`, `README.md`, and `tests/regression.lua`.

Copied / ported / independently reimplemented: no donor code was copied.
The module adds a thin capability probe over already compiled Vanilla/Tortoise
class and pet actions (`counterspell`, `silence`, `spell lock`, `kick`,
`pummel`, `shield bash`, `bash`, `hammer of justice`, `repentance`, `earth
shock`, and `death coil`). It validates the target's active cast and the
actual spell's interrupt metadata, chooses one owned executor, and delegates
casting/reach movement to existing PlayerbotAI actions. The addon sends one
`.bot action interrupt` intent and consumes the existing structured ACK/ERR
transport.

Reason: make interrupt a portable executor command without adding a second
combat engine, a class-to-spell policy table, or any core bot coupling.

Local validation: addon regression checks passed; cached native static
`mangosd` module build passed after the new command/context code; the native
runtime action path remains a real-client/gameplay acceptance gate.

## Per-bot CC mark assignment — 2026-09-05

Feature: generalized `cc <raid-mark>` assignment with a bot-scoped addon picker
and Party-tab visibility for the current assignment.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, `RtiAction`,
  `RtiCcValue`, `RtiCcTargetValue`, `CcTargetValue`, and CC trigger behavior.
- `MicroBot/CCP` v4.16 plus the local reverse-engineering notes in
  `playerbots-references/MicroBot Data/CCP-addon-reverse-engineer.md` and
  `microbot-wikidot-synthesis.md`, used for the per-companion `ccmark` UX and
  command semantics. MicroBot server code is closed; no binary or core patch
  was copied.

Source files: `commands/BotCommands.cpp`, `README.md`, `docs/HOST_API.md`,
`docs/PLAYER_CONTROL.md`, and the manager's `Constants.lua`, `Roster.lua`,
`Comms.lua`, `UI.lua`, `README.md`, `TortoiseBotsManager.toc`, and
`tests/regression.lua`.

Copied / ported / independently reimplemented: the module keeps the donor's
per-AI mark preference and mature CC target/trigger path, but independently
validates the eight Vanilla raid-mark names, persists the selected bot's
`rti cc` value through the existing `PlayerbotDbStore`, and emits a separate
`TBM:CC_ASSIGN_*` snapshot. CC executor discovery now queries explicit
capability metadata on the mature action graph, covering the registered Mage,
Warlock, Priest, Druid, Rogue, Hunter, and Paladin CC actions instead of a
parallel class table. The addon uses normal target selection, a compact
raid-icon picker, and server-owned assignment display; it does not add a core
bot-aware `Group::SetTargetIcon` seam.

Reason: support Circle-to-Warlock / Moon-to-Mage style assignments while
preserving the global core raid-icon slots and the Actions-vs-Roster boundary.

Validation: `lua5.1 tests/regression.lua .`,
`tools/verify_tortoise_surface.sh`, and the cached Docker builder's native
`mangosd` target passed. Runtime logout/relogin persistence and real-client CC
reapplication remain manual acceptance gates.

## Explicit combat engagement and pull completion — 2026-09-05

Feature: reliable party attack engagement for ranged/healing bots and explicit
pull-versus-pullback completion semantics.

Source repositories and commits:

- `cmangos/playerbots@076045efa835da9aab7c943bca752aebe1baad`,
  `AttackAction`, `PullAction`, `PullStrategy`, pull triggers, and return-position
  actions, used as the behavioral baseline.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  modern explicit/prioritized target handling, used as a behavior comparison.

Source files: `ai/playerbot/strategy/actions/AttackAction.cpp`,
`PositionAction.cpp`, `PullActions.cpp`, `generic/PullStrategy.{h,cpp}`,
`triggers/{GenericTriggers,PullTriggers}.cpp`,
`values/InvalidTargetValue.cpp`, and `commands/BotCommands.cpp`.

Copied / ported / independently reimplemented: the mature class rotations,
pull action selection, and movement actions remain in place. The local fix
independently invalidates cached target values when a player commits an attack,
allows that explicit target through the pre-threat validity window, runs a
normal first AI decision, and records when the selected melee/ranged pull
action actually succeeds. Pullback then returns to the requester position
captured at command time and retains that anchor until arrival; ordinary pull
hands control to normal combat after the pull action succeeds.

Reason: melee auto-attack created threat immediately and masked stale target
state, while ranged bots discarded the command target before their first spell
and the invalid-target action could starve healer actions. The donor pull state
also reused `RequestPull` after success, rearming its start phase, and could
erase the return anchor on timeout before the tank arrived.

Local validation: `tools/verify_tortoise_surface.sh`, `git diff --check`, and the
Docker release build of the native `mangosd` target passed. Focused real-client
attack/pullback acceptance remains a manual gate.

The follow-up explicit-engagement fix also adapts the donor prioritized-target
intent in the shared Tortoise target values: a valid command-only `explicit
attack target` remains ahead of DPS/AoE/tank assist selection until the command
target is no longer valid. Ordinary autonomous `attack target` state remains
outside that priority path. No donor target-value or command layer was copied
wholesale.

## Tactical action prerequisite handoff — 2026-09-06

Feature: align legacy Pullback with the Actions pull path and preserve mature
reach-then-cast behavior for explicit Interrupt and CC requests.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, used to confirm
  that interrupt/CC spell actions own their reach prerequisites and that
  command targeting should not invent a second movement policy.
- `Shyalya/tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, used only as
  the Tortoise/Vanilla behavior comparison for the existing PullStrategy and
  class action graph.

Source files: `commands/BotCommands.cpp`, `commands/BotCommandContext.*`, and
`ai/playerbot/strategy/Engine.*`.

Copied / ported / independently reimplemented: no donor code was copied. The
module now shares its existing stay/follow relaxation and first normal tick
between legacy and structured Pullback. When a direct tactical cast is out of
range, a thin module-owned queue entry lets the active mature Engine execute
the action's existing prerequisite/continuation chain; no class-to-range table
or command-side movement controller was added.

Reason: legacy Pullback could leave a tank held in stay/follow and wait for a
later tick, while direct Interrupt/CC execution bypassed prerequisites and
could lose the reach-then-cast action after the command returned.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed after the coherent edit batch.
No Docker/server/client gameplay run was performed; Pull/Pullback completion,
interrupt timing, pet behavior, and CC reapplication remain manual gates.

## Golden-party healer target reach — 2026-09-06

Feature: keep injured or dispellable party members discoverable until the
mature heal/cure action can run its existing reach prerequisite.

Source repository and commit:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Value/PartyMemberToHeal.cpp` and
  `Ai/Base/Value/PartyMemberToDispel.cpp`, used for the target-selection
  distance intent.

Copied / ported / independently reimplemented: the Tortoise values retain
their existing group, map, life-state, pet, and Vanilla spell checks. The heal
value now keeps candidates within twice the configured heal range so its
registered `reach party member to heal` prerequisite can close the gap; the
dispel value leaves range enforcement to the cure action while it queues that
same reach path. Incoming-damage prediction is clamped at zero so a lethal
preheal estimate cannot wrap unsigned health and suppress an emergency heal.
The unregistered forward-ported Priest strategy files also no longer carry the
expansion-only `divine hymn`/`hymn of hope` nodes; their group-heal fallbacks use
the registered Vanilla actions instead.

Reason: filtering at cast range made out-of-range party healing/dispelling
unreachable even though the mature action graph already owns movement and the
actual cast-range legality check. No command-side healer or new target-selection
system was added.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; the Golden Party healing, mana, dispel, and recovery checks
remain manual acceptance gates.

## Owned-party movement state — 2026-09-06

Feature: keep owner-controlled Follow/Stay/Come transitions coherent across
the PlayerbotAI reaction, combat, and non-combat engines, and fail a summon
cleanly when the core rejects its teleport.

Source repositories and commits:

- TortoiseBots' existing `PlayerbotAI::SetMovementStrategy` implementation,
  introduced in `f6a6c6b683a955d7747a0b4b91291631eb15a509`, is the local owner of
  cross-engine movement state.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Actions/ChatShortcutActions.cpp`, was consulted for the mature
  shortcut/anchor intent only.

Copied / ported / independently reimplemented: Follow and Stay shortcuts now
delegate their existing strategy changes through the local central setter,
while retaining formation, return-anchor, and position bookkeeping. Come/Hold
uses the same setter. The tactical relaxation helper clears a reaction-level
Stay copy introduced by that transition. Follow refreshes cached master/follow
targets after an ownership rebind. `PlayerConvenience` now removes a pending
summon immediately when `TeleportTo` rejects the request rather than treating
the unchanged position as arrival.

Reason: the public shortcuts previously changed only combat/non-combat state,
leaving a reaction-level Follow/Stay strategy or stale master-derived target to
fight the next command. The summon path could also enter its arrival phase
after a rejected teleport. No new movement controller or core bot seam was
added.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed for the movement change. No
Docker/server/client gameplay run was performed; Follow/Stay/Come/Summon and
repeated Golden Party transitions remain manual acceptance gates.

## Headless teleport acknowledgement — 2026-09-06

Feature: complete near/far Player teleports for module-owned Headless bots
before their normal AI update resumes.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Bot/PlayerbotMgr.cpp`, whose session loop calls
  `PlayerbotAI::HandleTeleportAck()` for teleporting bots.
- Tortoise `tortoise-wow` core's public `ObjectAccessor::FindPlayerNotInWorld`
  and `WorldSession` teleport-ack methods, used without a new core seam.

Copied / ported / independently reimplemented: `BotManager::UpdateBots` now
performs the donor manager's acknowledgement at the module world-thread
boundary, using the public not-in-world lookup because a far-teleporting Player
is intentionally absent from `FindPlayer`. The module also keeps AI paused for
the core's one-tick pending-far-teleport marker and waits for the near/far ACK
semaphore before acknowledging. Normal AI updates remain behind the existing
usability gate and are skipped during the transition.

Reason: Headless sessions have no client to send `MSG_MOVE_TELEPORT_ACK` or
worldport ACK packets. Without this module-owned replacement, following an
instance area trigger or another far movement could leave the Player in
teleport limbo and stop all subsequent movement/combat decisions.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; instance entry and cross-map movement remain manual gates.

## Party resurrection reach — 2026-09-06

Feature: keep a dead owned-party member selectable until the mature resurrection
action can reach the corpse and cast a Vanilla resurrection spell.

Source repository and commit:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Value/PartyMemberToResurrect.cpp` and
  `Ai/Base/Actions/ReachTargetActions.*`, used for the separation between
  corpse selection and cast-range movement.

Copied / ported / independently reimplemented: Tortoise keeps its existing
  group, map, corpse-state, resurrection-request, and duplicate-cast checks.
  The value no longer rejects a corpse solely for being outside spell range;
  the registered `reach party member to resurrect` prerequisite now owns that
  movement, and `ResurrectPartyMemberAction` requests it explicitly.

Reason: an out-of-range corpse disappeared before the existing cast action could
  queue movement, and the old prerequisite targeted the living-heal value rather
  than the corpse value. No new resurrection controller or expansion-only spell
  behavior was introduced.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; death, resurrection, and post-revive regroup remain manual
acceptance gates.

## Golden-party class compatibility audit — 2026-09-06

Feature: keep owned characters' saved talent builds intact and close a small
set of Vanilla/Tortoise strategy wiring defects found during the pre-playtest
class audit.

Source repositories and data:

- Tortoise 1.18.1 `Spell.dbc`, `Talent.dbc`, `TalentTab.dbc`, and
  `tw_world_spell_template.sql` were the authority for spell/talent IDs and
  availability.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70` was consulted for
  baseline Combat Rogue/Mage/Priest intent, including party-targeted Prayer of
  Healing; no later-expansion class subsystem was copied.
- Shyalya's Tortoise PlayerBots fork was used only as a comparison point. Its
  Cold Snap ID check and unconditional Defensive Tactics stance swap were not
  treated as compatibility evidence.

Copied / ported / independently reimplemented: module-owned bots now skip the
automatic `auto talents` action used during attachment and level-up, while an
explicit `talents ...` command remains available. The shipped alt-bot strategy
overrides are empty like the current donor defaults, so role/spec strategies
remain authoritative. Prayer of Healing now uses the existing AOE-heal target
and reach action and is wired into Holy's AOE strategy. The unverified
Tortoise-specific Defensive Tactics Berserker swap was removed because its
high-health trigger fought the normal Defensive Stance trigger (and was not
safe without the learned talent and shield). Cold Snap uses the learned named
spell and only fires when a known Frost cooldown is waiting; the low-level
Mana Gem helper now fails closed instead of reading an uninitialized ID.
Untalented low-level Rogues use the Combat fallback so the baseline Sinister
Strike rotation is available.

The follow-up talent-command path compares the learned talent topology before
and after a command. Only a changed topology updates the cached spec and calls
the existing `ResetStrategies()` once; query/list, invalid, and no-op commands
do not rebuild engines. Because the store persists complete `co`/`nc`/`dead`/
`react` strategy snapshots rather than deltas, a changed topology deletes those
strategy rows for every preset before the reset. Independent `value` rows are
retained, and loading a value-only preset leaves the newly rebuilt defaults in
place; future strategy saves create a fresh snapshot for the new build.

Reason: attachment-time talent mutation could rewrite an existing human build;
the inherited strategy config could replace Protection/Holy defaults with DPS
siblings; and several small Vanilla paths either used the wrong spell ID,
selected a self target for a group heal, or performed a stance swap without
the required Tortoise talent/equipment.

Local validation: Tortoise premade links for classes 1/4/5/8 were checked
against the current talent DBC (all generated links passed structural checks),
then `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh --core ../tortoise-wow` were run for the
implementation. No Docker/server/client gameplay run was performed; talent
preservation, class rotations, CC/AOE interaction, and dungeon healing remain
manual acceptance gates.

The Prayer of Healing trigger intentionally keeps the existing caster-centred
`AoeHealValue` activation count. The spell itself is target-centred, so a party
cluster 30–40 yards from the Priest can conservatively defer the group-heal
trigger; ordinary single-target healing/reach remains available and brings the
Priest into range. This is a manual edge-case check, not a reason to change the
generic value for every healer class.

`IsOwnedBot()` uses the module record plus `!BotManager::IsRandomBot()`. A
configured free-alt/always-online character is still an owned character and
therefore remains protected from automatic talent mutation; only the random
population identity opts into autonomous talent behavior.

## Combat: Target deduplication and AoE density clustering hardening (Issue #91) — 2026-09-08

Feature: Deduplicate hostile units in `AttackersValue` and harden `AoeCountValue::FindMaxDensity()` against false AoE clustering.

Source repository: `playerbots-references/shyalya-tortoise-wow` (API oracle for Tortoise runtime) and `playerbots-references/mod-playerbots` (behavior donor for target uniqueness and non-threat filtering).

Source files:
- `AttackersValue.cpp`: `AttackersValue::Calculate()`
- `AoeValues.cpp`: `AoeCountValue::FindMaxDensity()`, `AoePositionValue::Calculate()`

Copied / ported / independently reimplemented:
- In `AttackersValue::Calculate()`:
  - Resolved target duplication when `sPlayerbotAIConfig.shareTargets` is active: bot-specific targets (`current target`, `old target`, `attack target`, `pull target`) are collected into a `std::set<ObjectGuid>` before merging to prevent duplicate insertions for the same mob.
  - The merged list is validated and filtered through a distinctness filter (`std::set<ObjectGuid> seen`), enforcing the invariant that `attackers` contains strictly distinct hostile units.
  - The `getOne` qualifier (`attackers::1`) is now evaluated early and respected in the `shareTargets` path.
  - The fallback local target calculation path also guarantees distinctness via `seen.insert(target->getObjectGuid())`.
- In `AoeValues.cpp`:
  - `AoeCountValue::FindMaxDensity()` deduplicates incoming unit GUIDs into `std::set<ObjectGuid> uniqueUnits` before distance loops.
  - Cluster groups are stored in `std::map<ObjectGuid, std::set<ObjectGuid>> groups`, guaranteeing that duplicate creature GUIDs can never inflate cluster density.
  - `AoePositionValue::Calculate()` initializes bounding-box coordinates with a `first` guard, preventing reads of uninitialized stack variables when the first unit in a group is null.

Reason: Fix engine bug where single-mob pulls duplicated targets across shared party member lists, inflating density up to 3–4 and causing bots of all classes to prematurely cast expensive AoE abilities (Rain of Fire, Cleave, Thunder Clap, Blizzard).

Local validation:
- `tools/verify_tortoise_surface.sh`: OK
- `tools/verify_penqle_host_contract.sh --core ../tortoise-wow`: OK
- Standalone regression test suites (`tools/test_attackers_aoe_density.py` and `tools/test_attackers_aoe_density.cpp`): 6/6 tests PASS, proving:
  1. 1 mob with duplicate target references yields count = 1, density = 1, AoE inactive.
  2. 2 mobs with duplicate references yield count = 2 != 3+, AoE inactive.
  3. Real clustered 3 mobs yield count = 3, AoE eligible (>= 3).
  4. 3 spread mobs (> 2 * aoeRadius) yield density = 1, AoE inactive.
  5. Defensive FindMaxDensity with raw duplicate inputs returns count = 1.
  6. `attackers::1` qualifier returns exactly 1 distinct attacker.

## Class: Warlock Combat AI Overhaul (Issue #92) — 2026-09-08

Feature: Overhaul Warlock combat rotations, priorities, health/mana sustain, pet combat behavior, and AoE channel safeguards.

Source repository:
- `playerbots-references/mod-playerbots`: Behavior donor for `PetAttackTrigger`, `PetAttackAction`, and `RainOfFireChannelCheckTrigger`.
- `playerbots-references/shyalya-tortoise-wow`: Oracle for Tortoise 1.18.1 engine integration and pet spell autocast semantics.

Source files:
- `ai/playerbot/strategy/warlock/WarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/AfflictionWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/DemonologyWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/WarlockActions.h`
- `ai/playerbot/strategy/warlock/WarlockTriggers.h`
- `ai/playerbot/strategy/warlock/WarlockTriggers.cpp`
- `ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp`
- `ai/playerbot/strategy/actions/GenericActions.h`
- `ai/playerbot/strategy/actions/GenericActions.cpp`
- `ai/playerbot/strategy/actions/ActionContext.h`
- `ai/playerbot/strategy/actions/ChatActionContext.h`
- `ai/playerbot/strategy/triggers/GenericTriggers.h`
- `ai/playerbot/strategy/triggers/GenericTriggers.cpp`
- `ai/playerbot/strategy/triggers/TriggerContext.h`

Copied / ported / independently reimplemented:
- Life Tap Rebalancing & Safety Floor:
  - Rebalanced `life tap` priority in `WarlockStrategy::InitCombatTriggers` from `ACTION_HIGH + 3` (23.0f) to `ACTION_NORMAL` (10.0f). Prevents Life Tap from locking out primary combat DoTs (`curse of agony` at 12.0f, `corruption` at 11.0f) and AoE spells.
  - Guarded `LifeTapTrigger::IsActive()` and `CastLifeTapAction::isUseful()` with a configurable safety threshold (`health > sPlayerbotAIConfig.lowHealth`), breaking the death spiral while respecting server-configured bot health thresholds.
- Pet Combat Integration:
  - Implemented `PetAttackTrigger` and `PetAttackAction` using `CMSG_PET_ACTION` / `ACT_COMMAND` + `COMMAND_ATTACK`, registered in `GenericTriggers` and `GenericActions` for all pet classes.
  - Extracted side-effect-free shared engagement validation into `AttackAction::CanPetAttack(ai, pet, target)` to ensure both `AttackAction` and the new pet-command path strictly respect `WaitForAttackStrategy::ShouldWait`, combat `stay` range limits, passive stance, breakable/unbreakable CC, and damage immunity. Passive-to-defensive stance normalization is preserved strictly in execution paths (`AttackAction::Attack` and `PetAttackAction::Execute`).
  - Fixed dormant pet triggers in `WarlockPetStrategy::InitCombatTriggers` (`pet attack` at `ACTION_HIGH + 2`, `has aggro` -> `torment` at `ACTION_HIGH`).
  - Added non-combat `blood pact` party buff trigger for Imp.
  - Implemented `CastTormentAction`, `CastBloodPactAction`, and `CastFireboltAction` derived from `CastPetSpellAction`.
  - Fixed copy-paste bug across all three spec strategies (`AfflictionWarlockPetStrategy`, `DestructionWarlockPetStrategy`, `DemonologyWarlockPetStrategy`) where `InitCombatTriggers` inadvertently called `InitNonCombatTriggers` instead of `WarlockPetStrategy::InitCombatTriggers`.
- Affliction Sustain & Leveling Rotation:
  - Added `Drain Life` sustain triggers to `AfflictionWarlockStrategy`: `low health` (< 40%) -> `drain life` (`ACTION_HIGH`), `medium health` (< 70%) -> `drain life` (`ACTION_NORMAL + 2`), with `isUseful()` safety guard (< `almostFullHealth`).
  - Added early leveling `immolate` trigger (`ACTION_NORMAL`) to `AfflictionWarlockStrategy`.
  - Adjusted `DrainSoulTrigger::IsActive()` target health threshold from <= 15% to <= 25%, ensuring group kills register channel ticks in time to reap soul shards.
- AoE Rotation & Channel Interruption:
  - Implemented `RainOfFireChannelCheckTrigger`: detects active channeled Rain of Fire and activates if clustered enemies drop below 2 (`aoe count < 2`), triggering `cancel channel` (`ACTION_HIGH + 3`) to immediately save mana.
  - Lowered multi-dotting priorities in AoE strategies (`corruption on attacker`, `siphon life on attacker`, `curse of agony on attacker`) to `ACTION_HIGH - 1` (19.0f), allowing `rain of fire` (`ACTION_HIGH`, 20.0f) to reliably cast against 3+ grouped mobs.


## Class port Batch 1-3 (2026-09-08, uncommitted)
Feature: Talent prerequisite correctness + Arcane Power safety + Warrior Master Strike + Priest shields/Chastise
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (mature behavior donor)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime reference)
- `tortoise-wow` @ 9f778a73 (effective spell/template/script source)
Source files:
- `ai/playerbot/Talentspec.cpp`, `ai/playerbot/aiplayerbot.conf.dist.in`, `tools/talents/validate_presets.py`
- `ai/playerbot/strategy/mage/MageTriggers.h/.cpp`, `ai/playerbot/strategy/mage/MageActions.h`
- `ai/playerbot/strategy/warrior/WarriorActions.h`, `WarriorTriggers.h`, `WarriorAiObjectContext.cpp`, `ArmsWarriorStrategy.cpp`, `FuryWarriorStrategy.cpp`
- `ai/playerbot/strategy/priest/PriestActions.h`, `PriestStrategy.cpp`
- Core evidence: `src/game/Objects/Player.cpp` LearnTalent, `src/scripts/spells/spell_mage.cpp` (arcane power/rupture/icicles), `spell_warrior.cpp` (master strike), `spell_priest.cpp` (chastise/enlighten), `sql/base/tw_world_spell_template.sql`, `data/dbc/Talent.dbc`
Copied / ported / independently reimplemented:
- Talent DependsOnRank zero-based fix + DependsOnSpell talent check (independent fix from core semantics; validator mirrors ReadTalents).
- Arcane Power 70% mana gate (independent Tortoise safety; donor Wrath behavior unsafe, not ported).
- Master Strike action/trigger (independent Tortoise implementation; no donor counterpart).
- Weakened Soul 6788 guard (ported intent from mod-playerbots PriestActions.cpp).
- Hostile Chastise CC wiring (ported intent from Shyalya PriestStrategy CC).
Reason: P1 shared correctness + first class packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: validate_presets.py 242 links 0 failures; git diff --check; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 5-7 (2026-09-08, uncommitted)
Feature: shared cancel-channel registration + Mage Icicles/evocation checks + Hunter KC/Carve/Lacerate + Rogue generator/Surprise/Noxious/boost fix
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (donor intent; WotLK names rejected)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime parity)
- `tortoise-wow` @ 9f778a73 (spell_hunter.cpp, spell_rogue.cpp, spell_mage.cpp, Unit.cpp aura states, SpellMgr exclusivity, spell_template)
Source files:
- `ai/playerbot/strategy/actions/ActionContext.h` (cancel-channel creator)
- `ai/playerbot/strategy/mage/MageActions.h`, `MageTriggers.h/.cpp`, `MageAiObjectContext.cpp`, `FrostMageStrategy.cpp`, `MageStrategy.cpp`
- `ai/playerbot/strategy/hunter/HunterActions.h`, `HunterTriggers.h`, `HunterAiObjectContext.cpp`, `BeastMasteryHunterStrategy.cpp`, `SurvivalHunterStrategy.cpp`
- `ai/playerbot/strategy/rogue/RogueActions.h`, `RogueTriggers.h/.cpp`, `RogueAiObjectContext.cpp`, `CombatRogueStrategy.cpp`, `AssassinationRogueStrategy.cpp`
Copied / ported / independently reimplemented:
- CancelChannelAction registration (re-enables existing #92 RoF fix + druid/hunter/mage trees; class already vendored, zero creators found).
- Icicles/evocation channel checks (adapted from #92 RainOfFireChannelCheckTrigger pattern).
- Kill Command crit window via core CanCastSpell (casterAuraState 6), not DBC guessing; donor buff-model rejected.
- Carve below Multi-Shot (shared 10s category); Lacerate manual-only (Serpent churn avoidance).
- Surprise Attack reactive gate (mirrors local RiposteCastTrigger); Noxious Assault Combo-gated strike.
- CombatBoost adrenaline/blade flurry moved to combat triggers (was non-combat dead wiring).
Reason: Mage/Hunter/Rogue packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: git diff --check; validate_presets.py 242 links 0 failures; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 8-11 (2026-09-08, uncommitted)
Feature: Warlock DH/PO + Paladin HS/Bulwark/Exorcism + Druid Berserk/Swiftmend + Shaman 5 talents/Bloodlust
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (donor intent; WotLK names rejected)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime parity)
- `tortoise-wow` @ 9f778a73 (spell_warlock.cpp, spell_paladin.cpp, spell_druid.cpp, spell_shaman.cpp, spell_template)
Source files: warlock/, paladin/, druid/, shaman/ strategy dirs (actions/triggers/contexts/spec strategies listed in PROGRESS.md Batches 8-11).
Copied / ported / independently reimplemented:
- Dark Harvest 2-DoT gate + inverted cancel (independent; CD refund mechanic).
- Power Overwhelming explicit pet targeting (independent; core fallback analysis).
- Holy Strike/Bulwark actions (independent; verified template rows); Exorcism creature-type gate (vanilla-correct).
- Druid Berserk boost + Swiftmend HoT-gated pair (independent); NEW-stack rejection, Savage Bite rejection, Tree deferral (evidence-based).
- Shaman EQ/LS/Spirit Link/AS-pair/Bloodlust wiring (independent); totem churn claims rechecked and rebutted with source.
Reason: Warlock/Paladin/Druid/Shaman packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: git diff --check; validate_presets.py 242 links 0 failures; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batch 12 (2026-09-08, uncommitted)
Feature: five missing talent presets + generator + stance creator registration
Source repository:
- `tortoise-docker-penqle/data/dbc/Talent.dbc` + `TalentTab.dbc` (tree topology)
- `tortoise-wow/sql/base/tw_world_spell_template.sql` (talent spell names)
- `tortoise-wow` core (LearnTalent zero-based DependsOnRank semantics)
Source files: `tools/talents/dump_trees.py`, `tools/talents/build_missing_presets.py`, `ai/playerbot/aiplayerbot.conf.dist.in` (+5 specs), `ai/playerbot/strategy/warrior/WarriorStrategy.cpp`.
Copied / ported / independently reimplemented:
- Preset generator (independent; explicit acquisition orders, 297/297 links validate). Placements decoded from DBC, not skill-tab inference.
- Stance creator registration (independent correction of census misread; nodes were live, creators commented).
Reason: TALENT_BUILDS completion (27/27 specs) + Warrior tank/interrupt correctness.
Local validation: validate_presets.py 297 links 0 failures; git diff --check; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 12-13 (2026-09-08, uncommitted)
Feature: 5 missing presets + stance creators + 90-row family coverage
Source repository: Talent/TalentTab DBC + spell_template (names/topology).
Source files: `tools/talents/dump_trees.py`, `tools/talents/build_missing_presets.py`, `aiplayerbot.conf.dist.in` (+55 links), `strategy/warrior/WarriorStrategy.cpp`, `docs/class-port/*`.
Copied / ported / independently reimplemented: generator + builds (independent); stance creators (correction of census misread, nodes pre-existing live).
Reason: TALENT_BUILDS 27/27 + coverage completion.
Local validation: 297/297 links 0 failures; TSV column audit (90x16); diff --check; surface + host OK. No docker build (user-owned review).

## Class port Batch 14 (2026-09-08, uncommitted)
Feature: deferred Rogue four (Envenom/SoD/MfD/Smoke) + Ascendance
Source repository: `tortoise-wow` spell_template + Talent.dbc + spell_rogue.cpp.
Source files: rogue/ actions/triggers/context/Assassination/Subtlety strategies; priest/ actions/triggers/context/Holy boost.
Copied / ported / independently reimplemented: finisher/support slot decisions (independent from decoded mechanics); repaired two edit-placement breaks with diff verification.
Reason: close deferred Tortoise-talent gaps per census.
Local validation: git diff --check; validate_presets.py 297/0; verify_tortoise_surface.sh OK. No docker build (user-owned review).

## Class port Batch 15 (2026-09-08, uncommitted)
Feature: wiring audit gate + Ret/ready-check/master-target fixes + Elemental Mastery + naaru removal
Source repository: `tortoise-wow` (Engine::Init dual-path evidence); `playerbots-references/mod-playerbots` (bare-AoE donor semantics, deliberately not ported).
Source files: `tools/verify_action_trigger_wiring.py`; RetributionPaladinStrategy.cpp; WorldPacketActionContext.h; GenericTriggers.h/.cpp + TriggerContext.h; RacialsStrategy.cpp; shaman Elemental files.
Copied / ported / independently reimplemented: audit tool (independent); typo/registration fixes (independent); MasterTargetActiveTrigger (independent, from MasterTargetValue semantics).
Reason: reachability gate for all 28 profiles (a queued name without creator is a silent no-op).
Local validation: wiring gate exit 0 (live-missing=0); diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 16 (2026-09-08, uncommitted)
Feature: Tree of Life wiring + Conflagrate verification
Source repository: `tortoise-wow` spell_druid.cpp:570-579 + spell_warlock.cpp:475-510 + 45705 template row.
Source files: `strategy/druid/RestorationDruidStrategy.cpp` (tree maintain); Conflagrate paths unchanged (verified, not modified).
Copied / ported / independently reimplemented: Tree maintain (independent; restriction audit first).
Reason: close Tree design gap; verify Destruction policy.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 17 (2026-09-08, uncommitted)
Feature: Wolf aspect manual action + coverage integrity
Source files: hunter/ actions+context; docs/class-port/SPELL_COVERAGE.tsv.
Reason: last unresolved family row; oscillation analysis withheld automation.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 18 (2026-09-08, uncommitted)
Feature: Hunter pet attack parity + Wolf manual action
Source files: `strategy/hunter/HunterStrategy.cpp` (pet attack mirror of WarlockPetStrategy); HunterActions.h + HunterAiObjectContext.cpp (Wolf).
Reason: close Hunter pet-control gap with owned #92 machinery; Wolf without oscillation risk.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 19 (2026-09-08, uncommitted)
Feature: Daybreak fallback consumers + documented non-gates
Source files: `strategy/paladin/HolyPaladinStrategy.cpp` (FoL/HS fallbacks).
Reason: consume the Daybreak window when HL is unsuitable; Bloodlust-gate and Kick-reserve withheld for lack of evidence (documented).
Local validation: diff --check; presets 297/0; wiring 0; surface + host OK. No docker build (user-owned review).

## Class port Batches 19-20 (2026-09-08, uncommitted)
Feature: Daybreak fallbacks + full-diff review repairs + namespace-aware wiring gate
Source files: HolyPaladinStrategy.cpp; reviewer-found repairs across rogue/warrior/shaman/paladin/packet/generic/druid/warlock files; tools/verify_action_trigger_wiring.py.
Reason: consume Daybreak window robustly; eliminate silent no-ops module-wide.
Local validation: wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 21 (2026-09-08, uncommitted)
Feature: review-pass repairs + 7 gate-found fixes + Viper manual action
Source files: rogue/warrior/shaman/paladin/packet/generic/druid/warlock/hunter strategy files; tools/verify_action_trigger_wiring.py (namespace buckets + node check).
Reason: eliminate silent no-ops; keep manual paths for oscillation-constrained aspects.
Local validation: wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 22 (2026-09-08, uncommitted)
Feature: full working-diff self-review + repairs
Reason: edit-tool range edits silently dropped creator lines; systematic review is the backstop without compilation.
Local validation: raw git diff per file vs HEAD; wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 23 (2026-09-08, uncommitted)
Feature: preset/dispatch integration audit (read-only)
Reason: prove new presets resolve end-to-end without code changes.
Local validation: code-read evidence (config loader, factory roll, AiFactory tabs, update maps); full battery green. No docker build (user-owned review).
- Engine Bounded Failure Backoff + Transition Invalidation (issue #84):
  - Ported failure-cache intent from `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571` (`modules/mod-playerbots/src/playerbot/strategy/Engine.cpp` failure key/backoff/TTL/eviction, background-only gating, position-change clearing). Did NOT port its core-coupled transition generations (`GetMapWorkGeneration`/`GetTransitionGeneration` absent from Penqle core `9f778a73`); transitions tracked module-side and drained with local queue drain (no `Reset()`/`Init()` interplay, strategies/triggers untouched).
  - Transition signal is a generation counter on `PlayerbotAI` bumped by `HandleTeleportAck` (the single choke point `BotManager::UpdateBots` drives; ack ticks skip AI updates). Engines consume it at tick start before triggers, so even short same-map hops drain. `TransitionTracker` (map id + 3D 100yd jump detector + away/arrival edges + `NoteAway` on mid-walk breaks) backstops transfers the ack path never sees; walking zone lines never trips.
  - New header-only policy unit `ai/playerbot/strategy/ActionFailureBackoff.h` (pure std, no core types) wired into `Engine::{FailureKey,AllowBackgroundRetry,IsFailureBackedOff,RecordFailure,ClearActionFailures,RefreshFailureContext,DrainQueue}`; backoff gate sits before prerequisites/possibility so backed-off actions (including `!isPossible`) cost no work; explicit `ExecuteAction` path clears backoff (acts without delay by design). Gating additionally exempts owned bots (`IsOwnedBot`) per the issue's unowned-only scope.
  - Config `AiPlayerbot.FailedActionRetry{Base,Max,CacheTtl,CacheMaxEntries}` with Shyalya defaults (250/2000/30000/64); zero base/max disables.
  - Regression: `tools/test_engine_failure_backoff.cpp` (g++-compiled, 55 checks: growth/cap/saturation, success-clear, TTL prune, stalest-first eviction, disable, key separation, transition state machine incl. skipped-tick short teleport + 3D jump + NoteAway) + `tools/test_engine_walk_gating.py` (9 checks: gate ordering, drain paths, explicit-command clearing, ack bump, generation consumption, away marking).

## Economy: Native Auction Read Model and Personal Settlement (Issue #87) — 2026-09-09

Feature: Native auction read model with unit-price accuracy, bounded multi-faction cache, market-informed pricing, personal auction cancellation, and mail settlement safety.

Source repository:
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: Reference for auction price mirroring intent and `ItemUsageValue` market appraisal queries (`GetAHMedianBuyoutPricePerItem`, `GetAHListingLowestBuyoutPricePerItem`).
- `tortoise-wow` @ `bot-helpers` (`9f778a73`): Canonical core auction structures (`sAuctionHouseStore`, `AuctionHouseObject::GetAuctions()`, `sAuctionMgr.GetAItem()`, `WorldSession::HandleAuctionRemoveItem()`) and mail settlement APIs (`WorldSession::HandleMailTakeMoney`, `WorldSession::HandleMailTakeItem`).

Source files:
- `ai/playerbot/RandomBotFacade.h`
- `runtime/PlayerbotRuntimeFacade.cpp`
- `runtime/RandomBotService.cpp`
- `runtime/AhMarketService.cpp`
- `ai/playerbot/PlayerbotAIConfig.h`
- `ai/playerbot/PlayerbotAIConfig.cpp`
- `ai/playerbot/aiplayerbot.conf.dist.in`
- `ai/playerbot/strategy/values/ItemUsageValue.h`
- `ai/playerbot/strategy/values/ItemUsageValue.cpp`
- `ai/playerbot/strategy/actions/AhAction.h`
- `ai/playerbot/strategy/actions/AhAction.cpp`
- `ai/playerbot/strategy/actions/ChatActionContext.h`
- `ai/playerbot/strategy/triggers/ChatTriggerContext.h`
- `ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp`
- `ai/playerbot/strategy/actions/CheckMailAction.cpp`
- `ai/playerbot/strategy/actions/MailAction.cpp`

Copied / ported / independently reimplemented:
- Read model snapshotting & caching:
  - Reimplemented `RandomBotFacade::LoadAuctionPrices()` to iterate core `sAuctionHouseStore`, deduplicate visited `AuctionHouseObject` instances across faction IDs, resolve item counts via `sAuctionMgr.GetAItem()`, and populate an in-memory mirror bounded to 64 lowest unit-price entries per item template.
  - Added periodic world-thread refresh (`RefreshAuctionPrices`) throttled by `AiPlayerbot.AuctionPriceRefreshInterval` (default 60s, configurable 5-3600s), wired into `RandomBotService::Update`.
  - Added faction-scoped queries (`GetAhPrices(itemId, houseFaction)` and `GetAhPrices(itemId, Player* bot)`) respecting two-sided auction house rules (`AiPlayerbot.TwoSidedAuctionHouses`) or faction boundaries (Alliance sees Alliance+Neutral, Horde sees Horde+Neutral).
- Unit-price precision and appraisal:
  - `GetAHMedianBuyoutPricePerItem`, `GetAHListingLowestBuyoutPricePerItem`, and `DesiredPricePerItem` calculate unit buyout as `(float)buyout / (float)count`. If unit price is in `(0, 1)`, a positive sentinel of 1 copper is preserved so low-value stack listings are not rounded to 0 and mistaken for unlisted items.
  - Market posting in `AhMarketService` and `AhAction` queries `DesiredPricePerItem(bot, proto, count, undercutPercent)` before falling back to vendor price multiplier.
- Lifecycle actions & safety:
  - Implemented `AhCancelAction` (`ah cancel <id|item-name|all>`), dispatching canonical `WorldSession::HandleAuctionRemoveItem` packets with proper deposit forfeiture / item return semantics. Registered in trigger/action contexts and chat command handler.
  - Guarded `AhBidAction` against same-account bidding (`auction->ownerAccount == bot->GetSession()->GetAccountId()`).
  - Fixed mail settlement in `CheckMailAction.cpp`: excluded auction mails (`MAIL_STATIONERY_AUCTION` and non-normal message types) from unsolicited deletion, preventing loss of pending auction proceeds or returned items.
  - Fixed mail claiming in `MailAction.cpp` (`TakeMailProcessor`): money and items are claimed sequentially, and `RemoveMail` is called only after both money and items have been completely collected, eliminating gold/item destruction.

Reason: Fix Issue #87. Previously, `LoadAuctionPrices` was a clear-only stub causing all appraisal sites to return 0 and fall back to vendor sell prices. Bots could not cancel auctions, same-account bidding was unguarded, and mail handling contained bugs that could delete auction mails or destroy mail contents.

Local validation:
- Standalone C++ verification harness `tools/test_auction_read_model.cpp` (59 checks, 100% pass): tested empty mirror fallback, single-lot and multi-stack unit pricing, sub-copper preservation, 64-entry bounding, Alliance/Horde/Neutral scoping, two-sided config override, same-account bid rejection, cancel lifecycle, outbid refund conservation, buyout transfer conservation, deposit rules, hardcore dead-bot auction handling, and zero gold/item leakage.
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## Issue #88: Economy Synthetic AH Supply and Buyer Engine with Work Budgets — 2026-09-09

Feature: Time/operation-budgeted synthetic auction house generation and buyer engine, isolated synthetic inventory, item overrides and bans (`ahbot_items`), spend caps, same-account/outbid-self exclusions, and live telemetry/commands.

Source references:
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: Reference for ahbot generation sources, loot templates, and `ahbot_items` schema concept.
- `tortoise-wow` @ `bot-helpers` (`9f778a73`): Core auction house lifecycle, `sAuctionHouseStore`, `AuctionHouseObject`, `sAuctionMgr.GenerateAuctionID()`, and headless session auction packet handling.

Source files:
- `data/sql/char/20260906090000_char.sql`
- `ai/playerbot/PlayerbotAIConfig.h`
- `ai/playerbot/PlayerbotAIConfig.cpp`
- `ai/playerbot/aiplayerbot.conf.dist.in`
- `runtime/AhMarketService.h`
- `runtime/AhMarketService.cpp`
- `commands/BotCommands.cpp`
- `tools/test_synthetic_ah.cpp`

Copied / ported / independently reimplemented:
- Work budgets and phased state machine:
  - Implemented modular execution cycle (`Idle` -> `Gather` -> `Overrides` -> `Post` -> `Buy` -> `Expire` -> `Idle`) running within strict per-slice limits: `ahMarketBudgetUs` (default 2000 us) and `ahMarketMaxOperations` (default 32 ops).
  - Telemetry tracking slice durations, total listed/bought/expired counts, and budget overruns.
- Synthetic supply generation and pricing:
  - Generation sources cover creature loot templates (ranks 0..4), gathering professions (skinning, fishing, disenchanting, mining/herbs/chests), vendor inventories, and crafting recipes (`SPELL_EFFECT_CREATE_ITEM`).
  - Strict quality caps (`ahMarketMaxQuality`, default 4 / Epic), level caps (`ahMarketMaxLevel`, default 60), dynamic realm level clamping, and bound-item exclusion.
  - Value-based pricing bands with configurable variance (`ahMarketVariance`) and bid margins (`ahMarketBidMin`, `ahMarketBidMax`).
- Synthetic inventory isolation:
  - Synthetic items and auctions are tagged with `SYNTHETIC_OWNER_GUID = 0` and `SYNTHETIC_OWNER_ACCOUNT = 0`.
  - Synthetic items are tracked in `m_syntheticAuctions` and `m_syntheticItemGuids`.
  - Assertable property `AssertSyntheticIsolation(player, itemGuidLow)`: synthetic item LowGuids are never placed in real player or bot bags.
  - Unsold synthetic auctions expire cleanly by deleting from `item_instance` and memory without sending mail.
  - Purchases of synthetic items transfer items to buyers while money is sunk without paying non-existent sellers.
- Buyer engine and exclusions:
  - Evaluates auctions against fair market value and willingness threshold (`ahMarketBuyValue`, default 80%).
  - Enforces per-bot spend caps (`ahMarketMaxSpendPerBot`).
  - Excludes bidding on own auctions (`owner == buyer`), same-account listings (`ownerAccount == buyerAccount`), and outbidding self (`bidder == buyer`).
  - Buyer bots teleport to matching faction auctioneers and place bids/buyouts through canonical `HandleAuctionPlaceBid`.
- Overrides, blacklisting and live commands:
  - `ahbot_items` DB table (`20260906090000_char.sql`) provides per-item price overrides and blacklisting (`add_chance = 0` or `value = 0`).
  - In-game `.bot ah` / `.ahbot` commands: `status`, `reload`, `rebuild [all]`, `item <id> [value [chance [min [max]]]]`, and `item <id> reset` with administrator security checks.

Reason: Complete Issue #88. Complements the read model and personal settlement of PR #113 (Issue #87) by providing synthetic supply and demand for low-population realms without lag spikes, gold inflation, or item duplication.

Local validation:
- Standalone C++ verification harness `tools/test_synthetic_ah.cpp` (49 checks, 100% pass): verified phase transitions, work budget slicing, generation filtering and caps, pricing bands and variance, synthetic inventory isolation assertions, buyer engine policies and exclusions, override/ban behavior, safe expiration, and currency/item conservation.
- Standalone read model test `tools/test_auction_read_model.cpp` (59 checks, 100% pass).
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## 2026-09-09 — Travel route selection policies and navigation data import (Issue #86)

Target commit / PR: `feat/issue-86-travel-routes`

Source donor:
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelRoutePolicy.h`
- `playerbots-references/shyalya-tortoise-wow/tests/architecture/TravelRoutePolicyTest.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelNode.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelMgr.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_named_location.sql`

Files touched:
- `ai/playerbot/TravelRoutePolicy.h`
- `ai/playerbot/TravelNode.cpp`
- `ai/playerbot/TravelMgr.h`
- `ai/playerbot/TravelMgr.cpp`
- `tools/import_travel_nodes.py`
- `tools/test_travel_route_policy.cpp`
- `docs/PROVENANCE.md`

Copied / ported / independently reimplemented:
- Route weighting and travel policies (`TravelRoutePolicy.h`):
  - `GetTaxiRouteCost`: applies `PLAYERBOT_TAXI_ROUTE_DIVISOR` (450 * 8 = 3600) in `generateTaxiPaths` so discovered and affordable flight points are strongly preferred over continent-scale walking or swimming.
  - `GetWalkTravelTime`: models walking vs swimming with a 120-yard safe swim grace for short river crossings, combined with a 4.0x multiplier on sustained swimming so bots prioritize roads, bridges, and ferries over lengthy water crossings.
  - `GetStableRouteCostMultiplier`: deterministic pseudo-random 1.0..1.25 cost multiplier (up to 25% variation) keyed to party leader GUID (or bot GUID low) and spatial quantization. Ensures party members stay together while preventing different parties from marching single-file in identical lines.
  - `GetStableTravelSelectionSeed` / `MixTravelRouteSeed`: deterministic 32-bit avalanche hashing for party destination and point shuffling in `TravelMgr::GetPartitions`.
- Graph loading and startup decoupling:
  - Disentangled the historical conflation of online navmesh generation with DB cache loading in `TravelMgr::LoadQuestTravelTable()`. Persisted node, link, and path caches are loaded unconditionally from MariaDB (`ai_playerbot_travelnode`, `ai_playerbot_travelnode_link`, `ai_playerbot_travelnode_path`). If tables are empty, the system logs and degrades gracefully to direct movement and quest destinations.
  - Restored the post-load graph linking pass in `TravelNodeMap::generateAll()` (`calcMapOffset()`, `LoadMapTransfers()` for instance/portal triggers from `AreaTrigger.dbc`, `generateTaxiPaths()` for flight routes from `TaxiPath.dbc` / `TaxiNodes.dbc`, and reachability coverage warming).
- Navigation and fish location offline tooling (`tools/import_travel_nodes.py`):
  - Ingests and validates travel graph dumps (1,839 nodes, 6,200 links, 414,126 path points) and named fishing locations (54,038 spots).
  - Validates coordinate bounds and map IDs across all Classic/Tortoise WoW maps.
  - Produces clean, sanitized, high-performance replacement SQL migrations compatible with the canonical world schema (`20260824090000_world.sql`) and supports direct application via `--apply`.
- Route unreachable diagnostics:
  - Added explicit diagnostic logs (`sLog.outDetail`) when destination nodes are unreachable due to disconnected components, exhausted open lists, or missing start/end node associations, eliminating silent failure.

Reason: Groundwork for Issue #86 (keep #86 open for in-game client verification of live travel behavior). Enables autonomous bots to navigate intelligently via flight paths and roads while avoiding hazardous swimming and unnatural single-file party marching, backed by a supported offline import tool and graceful fallback.

Local validation:
- Standalone test suite `tools/test_travel_route_policy.cpp` (6 checks, 100% pass).
- Standalone regression test suites `tools/test_auction_read_model.cpp` (59 checks) and `tools/test_synthetic_ah.cpp` (53 checks).
- Offline import tool verification `python3 tools/import_travel_nodes.py --validate-only` (0 errors, 0 warnings across 1,839 nodes, 6,200 links, 414,126 path points, and 54,038 fish locations).
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## Issue #85: Earned Progression Loop (leveling, trainers, recruitment) — 2026-09-09

Feature: ding-time synthetic gear removed; initial gear seeding restricted to fresh pool bots via dual heuristic (process-local `seeded` stamp + persisted `GetTotalPlayedTime`); trainer travel gated on cheapest-affordable-spell instead of full-batch price; real-player master adoption purges travel/grind state and halts movement.

Source references:
- `playerbots-references/mod-playerbots`: `XpGainAction` never mints gear — bots keep earned equipment (behavioral reference for the ding-time removal).
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: `UpdateGearSpells` hooked into `RandomPlayerbotMgr`/`XpGainAction` as a headless-pool shortcut (kept only for initial seeding, not progression).
- `tortoise-wow` core `Player::GiveLevel`: unconditionally sends `SMSG_LEVELUP_INFO`, which already drives `auto talents` through the packet handlers — no new talent plumbing needed.

Source files:
- `ai/playerbot/strategy/actions/AutoLearnSpellAction.cpp`
- `ai/playerbot/strategy/actions/XpGainAction.cpp`
- `ai/playerbot/strategy/values/TravelValues.cpp`
- `ai/playerbot/PlayerbotAI.cpp`
- `runtime/BotManager.cpp`
- `runtime/PlayerbotAIAdapter.cpp`
- `runtime/RandomBotService.cpp`
- `runtime/GearSeedingGuard.h`
- `tools/test_progression_loop.cpp`

Copied / ported / independently reimplemented:
- Dinging expires the travel target (trainer re-evaluation) via `AutoLearnSpellAction::LearnSpells` and `XpGainAction` instead of minting gear; `auto talents` arrives via the existing levelup packet path.
- `NeedsInitialGearSeeding(playedTime, seededMark)`: seed only when both are zero. Veterans survive restarts via played time; repeats are suppressed via the stamp. Stamp is written after the attempt, never before.
- Trainer travel requires `free money for <budget> >= min trainable spell cost`; per-spell affordability at the trainer itself is unchanged (`TrainerAction` still skips overpriced spells).
- Master adoption (`PlayerbotAI.cpp` group adoption and `PlayerbotAIAdapter.cpp` rebind) runs `Reset(true)` + `StopMoving()` to purge orphan travel/grind goals and tether immediately.

Reason: Complete Issue #85 reqs 1-4: earned gear/trainer progression, restart persistence, immediate owner control on recruitment.

Local validation:
- Standalone harness `tools/test_progression_loop.cpp` (all checks pass): full seeding truth table and partial-purse trainer gating.
- `python3 tools/verify_action_trigger_wiring.py` (live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## ManTech CMaNGOS AHBot preservation (2026-09-12)

- Source repository: `T-imothy/tortoise-wow`.
- Source commit: `37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569` (includes CMaNGOS
  port `3d6f54b9`, bounded market `0c322d52`, and ownership fix `94c76b96`).
- Source files: `modules/mod-playerbots/src/ahbot/{AhBot.cpp,AhBot.h,MarketPolicy.h,ahbot.conf.dist.in}`.
- Ported the isolated market service and preserved its GPL attribution.
- Replaced the old bot-tree dependency with this module's existing
  `PlayerbotAIConfig::IsInRandomAccountList`; native core auction, loot,
  hardcore and work-budget APIs retain their contracts.
- Startup/update and command dispatch now belong to this module. One explicit
  controller selector prevents the two auction engines executing together.
- Validation: focused native market/ownership/dispatch regressions and module
  compilation are tracked in the core migration checklist; live DB/gameplay
  validation remains pending.

## ManTech taxi refresh preservation (2026-09-12)

- Source: `T-imothy/tortoise-wow` commit `258e6db5`, included in preserved
  baseline `37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569`.
- Files: `modules/mod-playerbots/src/playerbot/TravelNode.{cpp,h}`.
- Ported the native taxi-ID/geometry refresh and calculated-path cost reset;
  adapted the current module's `GetNode` spelling. Retained its startup caller.
- Validate missing path vectors, sparse null nodes, repeated refresh, unrelated
  edges and cached distance/time with `ModuleTaxiCacheRefreshTest`.
- This does not certify taxi handoff, destination selection, walking graph
  generation, or movement behavior; those remain separate migration work.

## Auction host compatibility and diagnostics (2026-09-12)

- Adapted module readers to the preserved ManTech core auction lock/snapshot
  contract instead of restoring the unsafe raw `GetAuctions()` accessor.
  Captured native buyout identity before deletion; snapshot appraisal uses
  saved item counts and retains native fields required by callers.
- Independently adapted command registration to existing native CommandScript
  and ModuleHandler facilities. Preserved administrator authorization for both
  the AH alias and SOAP; no new command dispatcher was added to the core.
- Ported the existing `BotActionLog_*` observation positions from ManTech
  baseline `37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569` to generic script callbacks;
  the module owns the adapter and logger. Documented per-effect apply events.
- Nine focused native-fragment regressions passed, including auction
  ownership/settlement, native permissions/dispatch, trainer, SOAP and module
  taxi refresh. The whole-module Release build linked successfully. Runtime
  validation and the remaining migration behaviors are separate gates.

### ManTech movement, Thorn and cached travel migration

- Source repository: T-imothy/tortoise-wow.
- Source baseline: 37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569; preserved Thorn
  behavior includes a80ca1d1 and its recorded predecessors.
- Source files: legacy playerbot BattleGroundTactics/Strategy/Triggers,
  FlagCarrierValue, MovementActions, RpgTaxiAction, CheckMountStateAction,
  TravelNode and WorldPosition. License notices remain with source files.
- Ported observable behavior: native Thorn objective/flag handling, travel mount
  preparation, single-owner spline handoff, native taxi checks and cache geometry.
  Queue integration and opt-in generation adapted to this module's services.
- Host gap: explicit coordinate queries implemented in the core's existing
  PathInfo rather than retaining no-op compatibility calls or copying a pathfinder.
- Validation: selected-module carrier/mount/taxi/path/dispatch tests; native
  coordinate paths with real Detour tiles; module generation/cost/cache tests.
  Runtime gameplay and parallel scheduler acceptance remain outstanding.


The same ManTech baseline supplies TryGroundTraversal and destination-scoped
path-failure retry intent. The new module uses its existing jump physics,
landing/collision checks and 64-bit transition generation; it does not inherit
the retired BG-specific logger. GroundTraversalTest now executes the selected
module; ModulePathRetryTest covers timer wrap and changed map/instance/epoch.
Scheduler trait intent comes from baseline PlayerbotScripts; human proximity
was corrected independently after tracing RandomBotFacade's bot-only roster.
ModuleHumanInterestTest covers humans versus bots, instance/camera visibility,
master/group responsiveness and network takeover.


Bounded population maintenance is independently adapted from the ManTech
requirement to limit world-owner maintenance without losing elapsed time or
starving a cohort. It retains this module's native RandomBotService and recovery
actions. ModulePopulationMaintenanceTest executes the selected implementation
with 6,000 fixtures, time/count budgets, fair wraparound, deferred timers and
removal during recovery. Fixture scale is not a live performance measurement.


## 2026-09-12 ManTech persistence, population and native-role checkpoint

Source: preserved `mantech-turtle` baseline
`37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569`, existing TortoiseBots facade/services,
and native core Player/HeadlessSession/LFT contracts. The changes adapt their
outcomes behind module hooks; they do not restore embedded-core PlayerBots.

- Durable facade values, legacy import preservation and atomic trade-discount
  cache/write ordering replace process-only state.
- Cadence target reconciliation, bounded surplus removal and DB queue admission
  headroom retain protected activity and one native lifecycle owner.
- Native role hooks and exclusive LFT fill avoid concurrent population controllers.
- A real isolated startup crash resolved to `PlayerbotAIStorage::GetAI` called
  by native machine-driven classification in `Player::Player`. Pointer lookup
  now requires only registry identity, never uninitialized player update fields.
- 82/82 architecture regressions passed (7.01 seconds), including actual selected
  module persistence/population/role/identity fragments. Release SOAP-on/LTO-on
  candidate build passed. SQL fixtures cover migration/replay. These tests do not
  certify parallel AI lifetime safety, live travel/BG behavior or production load.


## 2026-09-12 lifecycle and diagnostic follow-up

The isolated native module tests passed pending-add cancellation, login, save,
logout, re-login and final cleanup. Both module-enabled and disabled worlds reached
ready and shut down with exit 0. A deliberately connection-limited test DB now
causes clean expected startup failure instead of the earlier partial-pool crash.

The baseline PerformanceMonitor string-view API and `.perfmon` behavior are adapted
to this optional module. Added registry/counter locking, snapshots before report
output, 64-bit counters, monotonic timing, lazy bucket creation and balanced recursive
scopes. The native command registry retains authorization. Actual-source concurrent
counter/reset/report and command tests pass: 83/83 core regressions, 6.87 seconds.
The Windows wiring validator now normalizes path separators; its existing audited
dead-file classification produces 0 live missing creators rather than false errors.
The surface gate now requires preserved legacy tables and the one-time value import.


## 2026-09-12 active behavior and shared-state ports

Source contracts: preserved ManTech baseline
`37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569`, the selected TortoiseBots implementation,
and native core Map/GameObject/Player/session/command APIs. No new donor checkout
or unpinned remote behavior was imported.

Baseline fixes now execute in the selected module: exception-safe travel permits,
nonblocking travel reset and failed-result retry, 30-second pet-storage lookup
cache, Arathi GO-entry eligibility and per-AI capture throttle, corrected RPG
navigation endpoints and stale-target rejection, one-pass crowd occupancy, and
the broadcast-disable setting. Deferred travel preserves the module's existing
configuration. Native core has no waypoint-pause API; no no-op shim is claimed
as working NPC pause behavior.

Independent adaptations include packet snapshot dispatch/reentrant recovery,
stable shared-value owner and synchronization, GUID/generation objective caches,
one-time shared lookup publication, observability synchronization, and bounded
random-bot administration through the native module command registry and service.

Validation: 89 architecture tests passed after moving 20 existing source fragments
from the retired tree to selected-module implementations. Tests cover actual
native fragments plus deterministic fixtures; they do not establish parallel AI
ownership or client gameplay acceptance. A prior build sustained 30 native bots
for 120 seconds and shut down cleanly in the isolated database fixture.


The selected module already had ActionFailureBackoff and TransitionTracker;
the ManTech retry preservation adapts those native classes instead of importing
a second cache. Engine now evaluates prerequisites and possibility before
delaying failed background execution, keeps alternatives available, and clears
stale state on native map-work generation or current-power changes. Explicit
commands remain immediate. BotRetryIntegrationTest now executes the module's
actual Engine methods and cache; ModuleFailureBackoffTest exercises the native
header. The structural gate test was updated deliberately to match the user's
required baseline behavior. 90/90 architecture regressions passed in 8.14 seconds.


The capability/BG source audit also restored the baseline unknown-spell-data
guard, native map-template battleground classification, and proactive PvP after
noncombat strategy resets. NativeSpellCapabilityTest, ThornBotDiagnosticsTest's
map classification, and BattlegroundPlayerPresentationTest's factory fragment
now select the module. These repairs close source-level preservation gaps;
client combat/objective acceptance remains separate.


Objective memory uses the existing per-AI value context, shared by all BGTactics
action instances. Review found that action-local fields could disagree when
select/check/move actions ran on the same bot; ModuleBgObjectiveLifetimeTest now
explicitly covers shared same-AI identity as well as independent maps and bots.
The state retains only GUID, native map-work generation and selection time.

### Owner-deferred outgoing reactions and packet lifetime fence (2026-09-12)

- Feature: move existing outgoing spell, knockback, emote and chat reactions into
  the owning AI update; prevent enqueue from using an AI after registry removal.
- Source repository: current T-imothy/TortoiseBots migration working tree; native
  map generation contract from T-imothy/tortoise-wow ManTech baseline
  `37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569`.
- Source files: PlayerbotAI.cpp/.h, runtime/PlayerbotAIStorage.cpp/.h and
  host/BotPacketAdapter.cpp; native Player::GetMapWorkGeneration.
- Independently implemented module queue/fence around existing reaction bodies;
  no donor tree or new core API copied. The existing action packet queues do not
  cover these formerly immediate callbacks, hence the narrow owner FIFO.
- Local validation: 91 architecture tests passed, including concurrent producer,
  reentrancy, malformed event, map-transfer and removal fencing cases. Runtime
  evidence and remaining parallel ownership limitations are tracked separately.


### Per-AI delayed reply mailbox (2026-09-12)

- Feature: discard optional asynchronous replies after the originating AI ends.
- Source repository/files: current T-imothy/TortoiseBots working tree,
  PlayerbotAI.cpp/.h, SayAction.cpp and BotManager.cpp.
- Independently reimplemented the queue/lifetime boundary with standard weak
  ownership; existing native session opcode processing and chat filtering retained.
- Reason: a durable character GUID cannot distinguish successive AI lifetimes;
  the private native login request token is not a public module contract.
- Validation: native worker/drain regression added; 92 architecture tests pass.
  No live LLM endpoint was exercised.

### Preserved reply dataset and missing-category handling (2026-09-12)

- Feature: baseline reply catalog, probability defaults and safe absent-category lookup.
- Source repository: T-imothy/tortoise-wow.
- Source commit: `8415f1b9dc079f7207cfaf8589fa135ce19a1ada` (catalog's recorded commit).
- Source file: modules/mod-playerbots/sql/world/ai_playerbot_texts.sql.
- Source SHA-256: `8ab073b910b5245bffd18fc1f5c2f860992bc8ee9c662ba519fee9faa978a221`.
- Ported data: 1,937 reply rows and three probability defaults, GPL-2.0-or-later.
  Destructive donor DDL and generated help graphs omitted. The native module schema
  is retained; temporary staging makes additive seeding repeatable and preserves
  operator customization. Missing-category selection was independently corrected.
- Validation: 94 architecture tests, including selected-module reply selection;
  isolated MariaDB repeated imports preserve row counts, customized text/locales
  and probabilities, with test edits rolled back. Native migration/runtime rerun
  evidence is recorded in the migration checklist.

### Decision and reaction exception ownership (2026-09-12)

- Feature: recover the engine's active-decision flag and pending strategy rebuild
  after a tick unwinds; release popped nodes and generated descriptors on failure.
- Source repository/files: existing T-imothy/TortoiseBots migration working tree,
  ai/playerbot/strategy/Engine.cpp, ReactionEngine.cpp, Queue.cpp and Action.h.
- Independently corrected with standard C++ local ownership around existing
  behavior. No donor files copied and no new native-core interface introduced.
- Reason: the adapter's existing malformed-packet catch permits later ticks,
  while manual engine cleanup previously ran only on successful return.
- Validation: ModuleEngineRecoveryTest uses native method excerpts and injected
  failures; exact build/runtime evidence is in the host migration checklist.


### ManTech diagnostics, datasets and activity controller (2026-09-12)

- Diagnostic source: T-imothy/tortoise-wow at
  `00203fd8ff1de6415b546f0bf7c3babc28e69599`, preserved
  modules/mod-playerbots/src/playerbot/BotDiagnostics.{h,cpp} and observation sites.
  Ported bounded behavior/taxi/Thorn logging to the active module; reused native
  predicates, spline initialization checks and diagnostic budgets. Adapted the
  existing TravelTarget accessor; engine inspection remains const.
- Activity source: same pinned tree, RandomPlayerbotMgr.cpp `botPIDImpl` and
  `ScaleBotActivity`. Reimplemented the small controller within RandomBotService;
  retained proportional/integral/derivative policy and saturation anti-windup,
  using actual elapsed seconds and rejecting nonfinite tuning/arithmetic.
  No lifecycle manager, worker, core tick controller or donor tree was imported.
- Data source: T-imothy/tortoise-wow commit
  `8415f1b9dc079f7207cfaf8589fa135ce19a1ada`; exact files, SHA-256 hashes and
  DBC filtering are recorded in `reference/mantech-datasets.json`.
  GPL-2.0-or-later defaults are staged in temporary tables and inserted only for
  missing compatible keys. Operator data and unrelated classes remain untouched.
- Help: 22 independently authored native-module topics based on active command
  and class documentation. All internal help links resolve; old generated donor
  help graphs were not relabeled as supported module behavior.
- Verification: selected-module diagnostic tests, deterministic PID/command tests,
  and real MySQL 8 replay/customization checks. Exact current build/runtime results
  are recorded in the host migration checklist and local output reports.

### ManTech random administrator initialization and relocation (2026-09-12)

Source: T-imothy/tortoise-wow, preserved commit
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569, RandomPlayerbotMgr.cpp administrative
reset/init/teleport/rpg/grind behavior. Independently adapted to native
RandomBotService/RandomBotFacade/BotManager ownership. Persistent reset retains
temporary events; factory initialization retains configured level policies.
Relocation uses validated native TravelMgr destinations and native TeleportTo;
local grind intentionally has no global fallback. Busy/owned/leased bots are
protected. GUID plus login generation prevents stale queue operations.
Validation: ModulePersistentValuesTest, ModuleBotInitializationTest,
ModuleRandomRelocationTest, ModuleRandomAdminQueueTest and command permission
tests. Runtime validation is recorded separately; tests use extracted native
methods with host fixtures and do not establish live-client gameplay parity.

### Native diagnostics and world-buff cooldown ownership (2026-09-12)

Independently adapted within TortoiseBots from the current module implementation
at 0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 plus the preserved working changes.
The engine execution log now uses its existing EnableActionLog control. World-buff
summon cooldowns use a per-AI ManualSetValue instead of a static GUID map, preserving
cooldown sharing between action instances and completion clearing. Native fragment
regressions cover independent AI lifetimes, shared same-AI state and pending versus
completed regrouping. Cross-player world-buff operations still require the joined
world owner; this change alone does not establish parallel map safety.

### Non-random social view and channel audience preservation (2026-09-12)

Source: T-imothy/tortoise-wow, preserved ManTech baseline
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569, RandomPlayerbotMgr::OnPlayerLogin,
MovePlayerBot and PlayerbotAI::ChannelHasRealPlayer. Independently adapted to
native Network sessions and controlled BotRecords. The view previously contained
the autonomous pool, reversing friend/activity semantics. Channel membership now
uses Channel::HasMember, a generic const native query, and no longer treats any
online character as an audience or uses a layout cast. ModuleSocialPopulationTest
covers human/owned/controlled/autonomous/offline views, stale GUID resolution,
friend retention and channel/transport membership. This remains world-owner work.

### Travel query synchronization and bounded relocation (2026-09-12)

Source: active Sagiroth-derived TortoiseBots checkout at
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 plus this fork's native relocation port.
Independently corrected TravelMgr's shared lazy area cache, after tracing current
asynchronous GetPartitions consumers and recursive parent lookups. The native
relocation picker now filters innkeepers before terrain probes, samples without
replacement and validates the chosen local point's map/radius. Native-fragment
ModuleAreaLevelCacheTest and ModuleTeleportPickerTest pass. The 102-test candidate
ran with 30 disposable bots on shared local MySQL, restored the generated graph,
completed RPG and grind relocation for Tbplayone and shut down cleanly; artifact
identity and observations are in the runtime native-travel-runtime receipt.

### Immutable runtime item queries (2026-09-12)

Source: same active TortoiseBots checkout, RandomItemMgr.cpp. Independently changed
runtime cache misses from mutating operator[] to const lookup with immutable empty
values; startup builders retain ownership. This avoids inserting null item records,
invalid spec weights and empty level/category tables during query execution.
GetUpgradeList's <= comparator was not a strict weak ordering for equal weights;
it now uses <. ModuleItemCacheReadTest covers eight concurrent native query callers,
missing/populated values, unchanged nested table sizes and 64 equal-weight upgrades.
The separate unused GetUpgrade policy has not been rewritten by this change.

### Jump and queued requester lifetime (2026-09-12)

Source: active Sagiroth-derived TortoiseBots checkout
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02, PlayerbotAI jump/update code and
strategy Event/Trigger/Engine/ReactionEngine; native ManTech MapWorkStamp contract.
Independently adapted pending jump destinations to the native map generation and
corrected the wrapping millisecond deadline. ModuleJumpLifetimeTest exercises the
actual implementation for near/far/instance/removal invalidation, exact deadline,
timer wrap, ordered landing packets and a two-stage fall. The 104-test candidate
also ran 30 disposable bots, restored travel cache, completed RPG/grind relocation
and shut down cleanly (native-jump-runtime receipt); no real client jump capture.

Requester raw pointers persisted through both chat delay and external trigger
queues. Event now captures a revocable identity shared by copies; native module
logout/reclaim hooks invalidate it, and current ObjectAccessor lookup precedes use.
ChatCommandHolder and packet/chat/forced triggers capture before queuing. Engines
reject expired events before evaluation/execution, preserving automatic ownerless
events. ModuleEventOwnerLifetimeTest compiles actual Event implementation and
trigger/command-drain/dispatch fragments, including GUID/address reuse, logout,
copy/assignment, delayed cancellation and concurrent capture/revocation.

### Guild result checks and explicit action lifetime (2026-09-12)

Source: active Sagiroth-derived TortoiseBots checkout
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02; GuildAcceptAction,
GuildManagementActions, InviteToGroupAction, Engine explicit entry points and
PlayerbotAI::IsSafe. Compared with native GuildHandler acceptance/leave behavior
and existing Map/WorldSession ownership. Independently corrected null/superseded
invitation handling, observed native admission results, post-leave pointer lifetime,
explicit action RAII and same-map/session eligibility. ModuleGuildNativeResultTest
uses actual action bodies; ModuleEngineRecoveryTest now injects explicit command,
query and queue failures; ModuleNativeMapSafetyTest covers membership/transport
variants. These are deterministic native-fragment checks, not real-client guild
or map-parallel acceptance. The prior requester-lifetime binary also passed native
command/group/stranded-headless cleanup on verified disposable local fixtures
(native-packet-lifetime-runtime receipt).

### World action ownership and native commerce results (2026-09-12)

Sources inspected: active Sagiroth-derived TortoiseBots
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 group/guild/mail/AH/trade actions;
active Penqle-derived core 00203fd8ff1de6415b546f0bf7c3babc28e69599 native
GroupHandler and MailHandler. Independently implemented a bounded module world
action queue using revocable Event identities, explicit map/world scopes and the
existing post-join BotManager removal guard. No new core hook or legacy bot coupling.
Ported whole-action ownership boundaries and corrected observed native completion,
COD preflight and exception cleanup errors. ModuleWorldActionsTest exercises the
actual queue's lifetime, nested ordering, producer concurrency and bounds;
ModuleMailNativeResultTest and ModuleGroupNativeResultTest exercise actual native
action bodies against rejection, native mutation and lifetime fixtures. A prior
queue candidate passed the local native packet/group/queued-action/stranded-cleanup
runtime check (receipt native-world-action-runtime); that receipt does not certify
later source changes or a real network client. Full map AI activation is outstanding.

Guild AH purchases also use scoped ownership of the shared auction-action mutex,
including missing-house, missing-map, empty-needs and exceptional exits.
ModuleAuctionActionOwnerTest executes the native auction entry points with
contention and injected native/allocation failures; future actions remain able
to acquire the mutex after unwind.

Queued world actions also capture the existing native MapWorkStamp. Work admitted
before a near/far teleport, map/instance change or map ownership generation change
is discarded at the world drain, even if the same Player lifetime survives.
Admission rejects an already-transferring actor. ModuleWorldActionsTest covers
each identity mismatch using the actual core stamp and module queue.

Mail action dispatch propagates an empty/fully rejected collection as failure,
while still running the processor's report stage for partial results. The native
mail regression includes full rejection, empty filters and partial completion.

Guild command eligibility resolves the current native guild/member slot before
rank, permission or leader queries. Missing guild/member records fail closed;
guild joins resolve an inviter once and require its session. Actual helper-body
regressions cover missing guilds/members, denied rights, valid ranks and null
players. These remain world-owner queries, not concurrent lifetime guarantees.

Auction appraisal now publishes a complete immutable price snapshot atomically.
Readers retain that snapshot while selecting/copying listings; a refresh cannot
clear or mutate their backing storage. The one-argument query returns an owned
vector, matching filtered queries. Publication retains the existing per-item
64-listing bound, cheapest unit-price policy, faction filtering and native house
deduplication. Exceptions during refresh leave the previous published view intact.
Native source-fragment concurrency tests exercise refresh/read overlap and policy.

Deferred removal now uses a coalesced, thread-safe mailbox keyed by registered
character GUID and module record generation. Repeated requests for one incarnation
preserve any save=true request; older incarnations cannot overwrite or remove a
replacement login. Explicit map execution queues only intent and leaves registry,
lease and native session mutations to the joined world owner. World AI's existing
removal guard still marks Removing before returning. Drains run before/after the
world AI loop, with native session stop and reclaim behavior retained. The actual
queue and RemoveBot bodies are tested with concurrent map producers, generation
replacement, active AI stacks, native stop states and save coalescing.

Combat stuns no longer impersonate logout requests. The AI consults the native
session logout flag and pauses while its timer is pending. The mature chat logout
intent is consumed by BotManager outside the AI stack, with saved teardown behind
the removal guard; logout cancel can clear an intent that has not yet been consumed.
Native headless sessions now honor an actual elapsed logout timer on the world
packet owner. Map packet processing and ordinary stuns cannot expire the session.
This corrects the snapshot-candidate live fixture unexpectedly removing itself
after login (the failed native-snapshot-packet check remains failure evidence).

The existing default-off PacketBridgeTest now injects a transient stun into its
validated disposable master fixture before the first AI tick and requires that
the fixture survives to command testing without a native logout request. Only
the injected state is cleared, including early cleanup. Existing real crowd
control is left intact. This is a headless/native harness, not a network client.


### World decision continuations (2026-09-12)

Thirty social/commerce action classes now declare RequiresWorldOwner, inherited
by their variants. Decision and reaction engines test this before isUseful,
isPossible, multipliers, prerequisites or listeners for the selected action.
The existing bounded BotWorldActions queue resumes the same engine walk with its
original basket intact. Weak engine epochs cancel resets/destruction, weak request
tokens release the pending gate on rejection/discard, and the existing actor,
requester and MapWorkStamp checks cancel stale work. A combat/death engine change
supersedes an autonomous continuation. Queue backpressure leaves its basket for
retry, without manufacturing action failure or success.

Explicit commands return ACTION_RESULT_DEFERRED and execute their native action
on the joined world owner. The bool DoSpecificAction interface reports false until
there is an actual gameplay result; it cannot be used as an admission receipt.
Explicit execution only schedules success continuers after a successful result.
Direct bool Execute calls under map ownership fail without detached side effects;
the engine is the authoritative resumable entry point. Existing world execution
remains synchronous. No native map-AI hooks have been enabled yet.

Reaction selection reports its normal interruption step back to PlayerbotAI before
execution is admitted. World execution rechecks eligibility; resets and revoked
identities cannot start an old reaction. ModuleEngineRecoveryTest compiles the
actual decision, selection, reaction lifecycle and continuation functions and
exercises rejection, reset/destruction, owner changes, coalescing, exceptions,
queue discard, state changes, preserved prerequisites and completion ordering.
ModuleWorldActionsTest compiles the actual queue and checks that callbacks share
its lifetime/map bounds and never replay a named action as well.

The default-off native PacketBridgeTest also queues a disposable bot's group leave
under an explicit MapScope and separately checks DEFERRED admission with unchanged
group membership and later native removal from the group. This remains an internal
headless/synthetic-session harness, not real-client acceptance.

Source: active Sagiroth-derived TortoiseBots module at base
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02, Engine.cpp, ReactionEngine.cpp,
Action.h and the existing BotWorldActions queue. Independently adapted in the
module; no donor tree or new core hook. Remaining map activation gates include
trigger/value evaluation, AI prelude and cross-map shared-state ownership.


### Random maintenance group leadership (2026-09-12)

RandomBotUpdateAction now resolves the group leader once and treats a missing
module AI as a human leader. The prior mechanical host conversion applied GetAI
to `!GetGroupMaster()` and then dereferenced the nullable AI for the actual leader.
The action preserves the donor's human-led-group and nearby-player exclusions and
returns the actual ProcessBot result. It declares the world-owner domain because
maintenance can revive/repopulate a character. ModuleRandomUpdateLeaderTest compiles
the active action and covers human/AI/self-controlled/missing leaders, nearby
players, non-random actors, map rejection and native maintenance failure.

Source: active module RandomBotUpdateAction.h at base
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02, compared to the disabled ManTech donor's
RandomBotUpdateAction.h. Corrected host adaptation; no new core seam.


The same negated-pointer conversion was corrected in GoAction::TellStuck, four
stuck triggers and SecurityCheckAction. Their source donor predicates were
`!GetBotAI(player)`, not `GetBotAI(!player)`. Human-led groups now keep their
intended stuck-recovery exclusions, and random-bot loot security applies to human
masters again. Loot security resolves nullable master/leader sessions before rank
and guild queries and declares the world-owner domain. Actual native action tests
cover rank, guild, loot method/threshold, absent sessions and world ownership.
PlayerbotAIStorage explicitly deletes the bool lookup overload; the native lookup
declarations are compiled in ModuleAIIdentityTest with a static assertion that
Player*/ObjectGuid work and bool cannot compile. This prevents recurrence without
adding runtime branches or changing the core.


### Native administrative refresh (2026-09-12)

Refresh previously delegated only to PlayerbotFactory::Refresh, which supplies
consumables only when item cheats are enabled. The mature administrator behavior
also recovered dead bots, reset strategies, repaired durability, restored health,
mana/energy and PvP state, and replenished spending money. Those operations now use
the host's native APIs, retain disableRandomLevels/BG boundaries, and return a
real result to the administrative queue. Native resurrection refusal is honored
before corpse removal or strategy changes; ModifyMoney retains native hooks and
the money cap. Network/uncontrolled, transferring and map-owned calls are rejected.
The separate automatic Revive/ProcessBot policy has not been replaced by this edit.

Source: preserved ManTech donor RandomPlayerbotMgr.cpp::Refresh from baseline
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569, adapted into the active module's
PlayerbotRuntimeFacade.cpp. Native Player::ResurrectPlayer and ModifyMoney were
traced for host rejection and side effects. ModuleNativeRefreshTest compiles the
actual adapter and native money function and covers accepted/refused resurrection,
resources, corpse order, configuration/BG, owner rejection and money saturation.


### RPG inn travel cooldown (2026-09-12)

Accepted administrative RPG relocation now clears the old travel target and
restores its ten-minute cooldown after Reset. This preserves the inn dwell time
from the ManTech donor RandomPlayerbotMgr.cpp at baseline
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569. Native rejected teleports preserve the
existing target; grind relocations do not acquire the RPG delay. The module uses
its existing TravelMgr and TravelTarget APIs; there is no new core hook.
ModuleRandomRelocationTest covers rejection, reset ordering, RPG/grind distinction
and a missing optional travel target using the actual relocation function.

### World master reconciliation (2026-09-12)

The nonminimal decision postlude now uses ReconcileMasterAndPosture. Existing
world AI runs it synchronously. Explicit map execution submits one coalesced
continuation through the existing bounded BotWorldActions queue; it captures no
Player or AI pointer. Queue rejection/discard releases the pending token, and
actor lifetime/MapWorkStamp validation remains authoritative. The world phase
resolves current group membership before durable master bind/release, service
lease eviction, strategy reset, movement stop and native follow notifications.
Posture mirroring requires the existing same-map IsSafe predicate. Cross-map
master ownership remains valid; coordinates on different maps cannot impose
walking or sitting. Activity overrides now restore through exceptions as well as
normal returns and nested calls.

Source: active Sagiroth-derived PlayerbotAI.cpp at base
0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 and current native BotManager ownership
contracts. Independently adapted inside the module without a new core hook.
ModuleMasterReconciliationTest compiles the actual postlude and activity scope:
coalescing, rejected/discarded queues, changed leaders, transfer, failed native
release, repeated adoption, BG strategy repair, same/cross-map posture and nested
exception recovery. Joined map-AI activation remains gated on the remaining
packet/chat prelude, trigger/value and cross-map shared-state contracts.


### Background death recovery ownership (2026-09-12)

ProcessBot no longer calls RepopAtGraveyard on every maintenance pass for a dead
character. That shim repeatedly interrupted native ghost/corpse travel. Active
ReleaseSpiritAction, ReviveFromCorpseAction and SpiritHealerAction already own
release, waiting for a human resurrection, native corpse recovery and graveyard
fallback. Background maintenance now excludes dead, grouped, taxi, BG/queue,
transferring, logging-out, human-controlled and nearby-human characters, following
the preserved ManTech ProcessBot eligibility at baseline
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569. Expired-value maintenance stays on the
world owner. Ineligible results also suppress strategy/gear maintenance for that
slice. Administrative Revive remains a separate compatibility item.

ModuleNativeMaintenanceEligibilityTest compiles the actual facade function and
checks repeated dead/ghost passes, all eligibility gates, missing AI, map-owner
rejection and successful quiet-world cache cleanup. The existing 6000-candidate
service test retains budgeting, fairness and re-resolution after lifecycle change.
No native core function or new hook was added.


### Native TCP acceptance (2026-09-12)

The local protocol fixture completed native realm SRP authentication and realm
listing, encrypted-header world authentication, creation of two disposable human
warriors, character enumeration/login, an in-game .bot add command, native Who
containing the human and owned Headless bot, normal logout, and direct reclaim
of that bot into the same account's Network session. The module logged release
of AI control; both servers exited cleanly and all character online flags cleared.
Artifact-bound evidence is in the runtime reports directory. This is a real TCP
protocol client, not a graphical game-client playtest or proof of rendered
movement, transports, combat, custom encounters or scale.

The native protocol uses realm build 7272 (1.18.1) and world-auth build 5875.
RealmList.cpp and DBCStores.cpp are the respective authorities. The fixture's
first attempt incorrectly reused the realm build for world auth and was rejected;
its failed receipt is retained. The fixture was corrected; no server build,
authentication, addon or anticheat check was weakened. Temporary test configuration
and fixture credentials stay outside the source repository.


### Native resurrection rejection (2026-09-12)

Player::ResurrectPlayer deliberately refuses permanently dead hardcore characters
unless the caller explicitly supplies forceHc. Corpse reclaim and spirit-healer
handlers previously continued with corpse removal and durability loss after this
refusal. They now require IsAlive before those effects. The solo/AI dungeon-entry
release path similarly removes the corpse and returns through its alive teleport
only after acceptance; rejection falls through to the existing ghost graveyard
route. Forced hardcore recovery remains the existing separate native operation.
These are generic native lifecycle checks, with no bot branches or new hooks.

The module now returns failure from rejected corpse reclaim, spirit-healer and
repop operations before clearing corpse-run/death state, saving, relocating or
reporting recovery. NativeResurrectionResultTest compiles the actual reclaim and
spirit-resurrection functions and dungeon-entry recovery block; it checks normal,
BG, delayed/distant corpse, hardcore rejection and accepted cleanup ordering.


### Social activity snapshot (2026-09-12)

The existing post-map SyncNativePlayers publishes immutable activity facts:
whether a human/controlled population is online, its friend GUID union, and the
real-guild classification for referenced bot guilds. Explicit map execution reads
those values for empty-server/friend/guild priority and relation checks. Existing
world AI retains current native queries. A snapshot may lag a world transition by
one join; it affects activity priority only. Permission, membership and ownership
mutations still resolve current native state on the world owner.

The native PlayerSocial API previously exposed only single-GUID friendship
queries. Building the union with those queries would scan every player for every
bot. GetFriendGuids adds a generic value query over the existing friend flags,
used only on the native social owner; ignored-only entries are excluded. The
module publishes IDs and booleans with atomic shared_ptr replacement, retaining
no Player, Guild or social-list pointer. Each referenced guild is classified once
per publication; no database query or mutation is introduced.

This is an independent host adaptation of the active Sagiroth-derived module,
base 0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02, against native SocialMgr/GuildMgr.
No lifecycle hook or bot-specific core branch was added. Joined map AI remains
disabled while packet/chat prelude, trigger/value and other shared-state gates
are completed.


### Administrative native revival (2026-09-12)

The rndbot revive adapter now performs actual native recovery through Refresh,
reports refusal instead of completing a no-op graveyard release, and clears the
legacy dead/revive event markers only after accepted resurrection. Alive and BG
bots are excluded. Rescue uses the existing terrain-validated relocation service:
nearby grind for an unreleased corpse, level-fitting grind for a ghost. Existing
group/master, pin, lease, map, combat, taxi and destination checks still apply.
Failed rescue relocation preserves successful revival at the current location
and is logged separately; it is not mislabeled as a completed teleport.

Source: ManTech RandomPlayerbotMgr::Revive, RandomTeleport and
RandomTeleportForLevel at baseline 37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569.
The donor's live SetPosition loop used while probing nearby candidates was not
ported; it bypassed native movement/transfer ownership. Existing bounded validated
destination selection preserves the rescue intent. Automatic ProcessBot never
calls administrative revival and retains native death AI ownership.
ModuleNativeRefreshTest compiles both actual adapters, covering native refusal,
corpse/ghost selection, failed optional relocation, BG/alive/map exclusions,
event cleanup timing and repeated requests. No core hook or schema was added.


### Death recovery world ownership (2026-09-12)

Corpse revival and spirit-healer actions declare RequiresWorldOwner, inherited
by RepopAction. The existing engine continuation queue moves selection, native
resurrection, persistence, group changes and optional rescue to the post-map
world owner. Direct bool Execute callers reject map execution without side
effects; a pending engine action retains its separate deferred result. Corpse
movement remains a movement action. This follows native spirit-healer opcode
world ownership and the module rescue/group registry contracts; native corpse
reclaim remains a map-capable core handler. No core hook was added and parallel
AI dispatch remains gated by the remaining control/value ownership audit.

The default-off PacketBridgeTest also checks actual death through native
self-damage and RandomBotFacade::Revive on its disposable non-hardcore bot below
level 5. Temporary random eligibility is restored by record generation, including
unwind. Its level excludes rescue relocation; this validates recovery at the
current map, not destination selection or ordinary player corpse interaction.


### Outgoing notification execution domains (2026-09-12)

The outgoing AI mailbox retains native spell failure/delay and knockback on the
current map owner. An explicit MapScope no longer parses chat/emote or reads
foreign Player, guild or channel state. It keeps those notifications queued and
requests one coalesced post-map continuation through BotWorldActions. That world
continuation processes social notifications only; newly arrived movement stays
for the map owner. Ordinary world AI keeps its existing all-notification drain.
No native opcode routing or engine packet-action queue changed.

Each domain preserves FIFO, including reentrant arrivals and unexpected unwind.
Native generation still rejects obsolete spatial packets. Failed admission or a
discarded transfer continuation retains social packets in the same AI lifetime
and releases its retry token; no Player/AI pointer is retained by the callback.
ModuleOwnerPacketTest compiles the actual mailbox producer/drain and checks
domain separation, coalescing, rejection, discard/retry, transfer, malformed
events, mixed-domain exceptions and concurrent foreign-map chat producers.
This removes one prerequisite; the remaining control prelude and shared values
still block enabling joined map AI. No new host hook was introduced.


### World-owned guild decision values (2026-09-12)

Guild orders and sharing currently inspect native guild notes, all online guild
members' roles and inventories, and other AI contexts. Action deferral alone
cannot protect these reads: trigger/value evaluation precedes action selection.
The existing CalculatedValue cache has no owner dispatch; copying every guild
inventory every tick would add unrelated world scanning. The module therefore
adds an explicit WorldCalculatedValue specialization for copied decision facts.
It reuses BotWorldActions and existing value intervals. World callers calculate
synchronously. Map callers return the last completed result (empty before the
first completion) and coalesce one due world refresh. They do not advance cache
time merely because a refresh was requested. Existing dependent-value intervals
and one world join can delay observing a newly assigned order.

Only guild order, share list, craft/farm/quest-reward orders and the missing
reagent item-ID vector opt in. Map travel selection caches reagent IDs for five seconds, avoiding repeated
guild-wide inventory scans. World purchase callers retain a fresh calculation. Actual purchase/item-transfer actions retain native live
eligibility checks. No live Player/Unit/Item pointer is stored in these results.
Nested guild dependencies compute synchronously on the world owner. The callback
holds names and weak value epochs; Reset/destruction cancels obsolete work, and
native actor lifetime/map stamps handle logout/reclaim/transfer. Admission
failure or discarded work remains due for retry. World queue capacity and time
budgets are unchanged; no per-tick eager guild or database scan was added.

GuildShareTarget still requires separate live receiver identity review. This
change does not enable map AI or declare the rest of the value graph safe.
The donor guild parsing, role filters, deficit and choice algorithms remain in
GuildValues.cpp. This is an independently implemented module ownership adapter;
no new native host seam or schema was required.

ModuleWorldValueTest compiles the actual cache and base value implementation; it covers last-completed facts, nested dependencies, coalescing, admission rejection, discarded work, Reset and destruction/replacement.


### Guild sharing receiver identity (2026-09-12)

GuildShareTarget now contains a receiver GUID instead of a cached Player pointer.
Its decision calculation also uses WorldCalculatedValue because it inspects
another AI's inventory/role. The native world phase resolves current nearby
members; a stale nearest-player GUID cannot inspect a different live map.
GuildShareItemAction is world-owned and rejects direct map execution. It refreshes
the target/amount at execution and resolves the GUID through the sender's actual
native map, checking living, in-world, non-transferring, same-guild actors and
receiver AI availability before accessing inventory. Map triggers read copied
target facts; item mutations remain in the world phase.

The existing manual whole/partial item transfer is still awaiting replacement
by the native trade flow, including Turtle restrictions and timed acceptance.
This identity change alone is not complete guild-transfer acceptance.


### Native AI control phase (2026-09-12)

Reaction command parsing and internal chat replies/packet-trigger preparation
now enter the world phase explicitly. In MapScope they coalesce bounded native
world continuations, retaining only a method selector and retry token. The
existing BotWorldActions lifetime/map-generation validation resolves the current
AI at execution. Empty queues schedule no work; rejected/discarded requests stay
queued and retry. PacketHandlingHelper exposes a mutex-protected pending check.

World AI retains synchronous parsing and the prior command/reply/packet order.
Native logout still pauses decisions without taking session teardown into AI.
Map decisions continue locally; they observe prepared external triggers on a
subsequent map pass. Local spell/movement notifications use their separate map
mailbox drain. This does not execute the full AI loop on the world queue.
Cross-map master reads, other shared values and actions remain under audit;
joined map hooks are still disabled. No native hook or schema was added.


### Native guild item trades (2026-09-12)

Guild item sharing now creates a native trade offer and completes it through
HandleInitiateTradeOpcode, HandleBeginTradeOpcode, HandleSetTradeItemOpcode and
both native HandleAcceptTradeOpcode calls. Partial stacks use native SplitItem
in an empty validated inventory position, preserving the core's CloneItem
metadata and rejection/rollback behavior. The old manual ownership mutation,
CreateItem reconstruction and sender-only early save are removed. Native trade
retains faction, hardcore, binding, raid-item trading, bag-space, scam-prevention,
logging and persistence behavior. No native trade check or delay is bypassed.

NativeGuildTrades is a bounded module world-phase interaction owner (64 offers;
at most 8 completions/cancellations and a 4ms soft budget per update). An ordinary
map-stamped action continuation would discard a transferred request without
canceling its already-open trade, including a nearby same-map teleport. The
native player event processor also runs on maps. This service therefore uses
the existing post-map BotManager update/removal guard and checks pending offers
even when their map stamp changes, so it can cancel its own unchanged native
offer. It adds no core hook, thread, sleep or persistent parallel trade system.

Each offer carries revocable native Player identities, both map stamps, both
master GUIDs and the exact item/amount. Removed/reclaimed actors are left to
native logout cancellation; changed money/spells/items are not overwritten.
Map/ownership/guild/eligibility changes cancel only the unchanged owned offer.
Acceptance waits at least the native interval plus one second for its time_t
resolution. Completion requires the native trade to close and exact conserved
inventory deltas on both players. Shutdown cancels remaining unchanged offers.
An action success reports an offered gift; the separate completion log records
the actual transfer result. There is no success announcement before transfer.

Source: the pinned native core's TradeHandler.cpp, Player::SplitItem,
Player::RemoveFromWorld, and Item::CloneItem/CanBeTraded; guild sharing intent
comes from the active Sagiroth-derived GuildShareItemAction and GuildValues.
The service is independently implemented against native contracts. Its runtime
acceptance passed on candidate 7ada3310: partial four-item and whole six-item trades, both native completion receipts, temporary guild/item cleanup, and clean shutdown.


### Disposable native guild-trade diagnostic (2026-09-12)

The default-off PacketBridgeTest now places its verified disposable bots together
through native teleport/ack and stay actions, then creates TBPLAYNativeGift only
when neither actor has a guild and neither owns item 117. Native Guild::Create,
AddMember and a ten-item native inventory insertion prepare the fixture. The
actual NativeGuildTrades service must move four items through a partial native
split/trade, then the remaining six through a whole-stack native trade. The test
requires conserved 6/4 and 0/10 inventories, disbands only its created guild and
destroys only its introduced items, saving both inventories before continuing
stranded-session cleanup. Existing guilds/items cause rejection, not deletion.
Timeouts fail explicitly and attempt fixture cleanup. Python verifies the exact
TBPLAY account/characters before enabling the diagnostic; production defaults
remain off. New required receipts: native guild partial trade, whole trade and
cleanup. Native core trade restrictions remain enabled during validation.

ModuleNativeGuildTradesTest compiles the actual service with native-operation fixtures: delayed partial/whole transfers, metadata retention, item conservation, rejected initiation/splitting/set/accept, full bags, canceled acceptance, map/master changes, altered-trade preservation, shutdown, 64-offer admission and bounded per-tick progress. ModuleBotDispatchTest verifies trade callbacks share the native AI-removal guard.


### Party decision locality (2026-09-12)

Same-instance party decision port: active Sagiroth-derived value intent at module baseline 0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 retained, with the native PlayerbotAI::IsSafe seam applied before health/combat/position/AI reads. AoeHealValues, StatsValues, LineTargetValue, AttackerCountValues, CcTargetValue, AttackersValue, GrindTargetValue and TargetValue now reject foreign instances and transferring members. Near-leader uses one validated leader identity. GroupBoolCountValue previously returned zero at the first match (return count++); it now counts every eligible matching bot. Healer subgroup/range rules and offline/non-bot filtering remain intact. ModulePartyLocalityTest covers the actual guard and representative algorithms; this is not live dungeon acceptance.


### World readiness and guild metadata (2026-09-12)

GroupReadyValue retains its active donor calculation, now through WorldCalculatedValue<bool>; this preserves cross-map dungeon readiness intent instead of dropping remote dead party members. GuildMotdValue copies native Guild::GetMOTD during world evaluation, with the existing guild-meeting time parsing retained. WorldCalculatedValue callbacks now retain qualified context identity. Native guild diagnostic restores a non-hardcore ghost leader through native resurrection and moves the pair to the race/class data-defined starting position; the prior failure was an accurately rejected dead sender (saved PLAYER_FLAGS_GHOST and corpse row), not permission to weaken trade restrictions.


### Remaining world services (2026-09-12)

Automatic mail return: active Sagiroth-derived CheckMailAction intent at 0fb3bc0bff08f5a47d8f6c3e3fc2a9528f538c02 was to return unrequested items from online human senders. It reconstructed only the first attachment, then deleted mail/mail_items directly even for requested mail. The port delegates the complete return to Penqle-derived WorldSession::HandleMailReturnToSender (core 00203fd8ff1de6415b546f0bf7c3babc28e69599), retaining native mailbox, delivery, attachments, money, transaction and notification behavior. It skips hardcore senders because native return would drop those messages. ModuleAutomaticMailReturnTest validates action control and deletion lifetime; it is not a live mail-delivery test. World domains were also declared for remaining travel selection/request, guild petition/tabard, speech and gear actions after tracing their foreign-player, guild, shared text and database work.


### Native party gifts (2026-09-12)

Party gifts preserve GiveItemAction's active donor intent to supply bot party members with missing usable conjured food/water. The old direct inventory ownership mutation could bypass trade restrictions/persistence and dereference an item after a merge. OfferParty reuses the native guild gift mechanism with current native Group identity instead of guild identity. ModuleNativeGuildTradesTest now covers unguilded party acceptance, replacement/removal cancellation, and preserved inventories. Default-off PacketBridgeTest adds a three-item unguilded party trade after its guild checks, then native disband and item cleanup.


### Cross-map owner observations (2026-09-12)

Owner-state boundary port retains the active donor decision algorithms and native CalculatedValue/MemoryCalculatedValue semantics. MasterPositionValue uses a WorldCalculatedValue decorator around its existing MemoryCalculatedValue base, preserving change intervals/history used by movement triggers. FreeMoveCenterValue and RangeFilterValue retain copied GuidPosition results and cross-map reference intent. MasterNeedsQuestItemValue delegates existing ItemUsageValue::IsNeededForQuest on the world owner, and MasterTeleportingValue defers the foreign teleport read. PlayerbotAI logout cancellation uses native HandleLogoutCancelOpcode only after map work joins; a reset still cannot cancel a logout already due. Scoped master/local party checks protect consumable durations, stance orientation and trade enchant targets. Actual cache-history and cancellation regressions cover these boundaries; joined map hooks are not yet enabled.


### Follow domains and serial map probe (2026-09-12)

Cross-map follow preserves the active FollowAction/MovementAction::Follow behavior by declaring a world domain only for foreign or transferring follow targets. Native map-local follow is retained. FindCorpseAction and FleeToMasterAction need world ownership because their recovery/travel checks read other-map owner state. ChatCommandAction provides the common native world boundary for SQL/session/global command descendants; their existing entry processing was already world-owned. A temporary default-off PacketBridgeTest serial MapScope probe exercises the complete mature update with deferred controls, values and actions before switching the production scheduler; remove this temporary world-loop branch when native map hooks replace it.


### Native map AI ownership (2026-09-12)

The selected module now registers the existing OnAIUpdate and IsAIUpdateDue
player hooks. Native Map::UpdatePlayerAI owns GUID/generation validation,
foreground admission, bounded idle batches, elapsed clocks and joined map
lifetime. PlayerbotAIAdapter establishes MapScope and retains malformed-packet
containment; network ownership, transfers, missing engines and pending logout
reject AI execution. BotManager no longer executes individual AI on the world
loop. It retains native teleport acknowledgements, lifecycle reconciliation,
logout consumption and bounded world continuations/trades after maps join.

The active Sagiroth-derived UpdateAI performs packet/movement/reaction work
before its action-delay gate. IsAIUpdateDue therefore admits each usable AI to
the native budget instead of treating action delay as permission to skip that
work. Decisions still use their original delay. No new core hook or alternate
scheduler is introduced. The default-off packet diagnostic reports native map
AI only after execution through this hook; its transitional serial probe is
removed. Build/runtime acceptance is recorded separately with executable hashes.
Source: current native Map.cpp/ScriptObjects hooks and the pinned active
Sagiroth-derived PlayerbotAI::UpdateAI; independently implemented adapter seam.

Bot TellPlayer/TellPlayerNoFacing messages from map decisions now retain copied text/options and a revocable recipient Event, then run security, repeat suppression and delivery on the world owner. Admission does not report completed delivery. Facing requires the actual same map/instance. TellPlayer forwards ignoreSilent to the intended argument while retaining repeat suppression.


### Pending invitations survive strategy refresh (2026-09-13)

A real TCP regression showed the native server successfully inviting a bot,
then the initial `update pve strats` action rebuilding the graph before its
queued `accept invitation` ran. The packet event was consumed but the native
Player still held its pending Group invitation. GroupInvitationTrigger now
reads that existing native pointer as a boolean; it retains no Group object
and introduces no second invitation state. The unchanged world-owned
AcceptInvitationAction resolves the current inviter, checks security, and
uses native HandleGroupAcceptOpcode. Native accept, decline and cancellation
remove the trigger condition. ModulePendingGroupInviteTest covers graph
replacement, deferred decisions, cancellation and a new invitation. The failed
runtime trace is preserved in the local reports history; repeated live invite
acceptance must pass on the new artifact before this defect is marked resolved.
Source: current native GroupHandler.cpp and Player::GetGroupInvite, active
Engine::Init/Reset and AcceptInvitationAction; independently implemented trigger.


### Local native acceptance checkpoint (2026-09-13)

The native module and bots-disabled builds use separate output directories.
The selected module runs through the core's existing joined map hooks, with no
additional core hook for this dispatch change. The disabled legacy bot tree is
retained solely as source/reference and contributes no object to either local
build. Source histories and uncommitted work remain preserved.

The final pending-invitation trigger also excludes the native pending group's
leader. Regression coverage is 133 passing tests, including native pending
invitation survival across class-strategy rebuild, cancellation and leader
handling. The deployed candidate's artifact-bound receipts include 16 native
lifecycle/trade checks and 15 real TCP owner-journey checks: nine invitation
cycles, cross-continent teleport/ack/summon, logout and human reclaim. Earlier
artifact-bound tests separately cover real client movement, stay/follow and
native combat damage/kill. Exact hashes, population measurements and remaining
graphical/dungeon/BG/scale acceptance are recorded in the local output reports;
these source notes do not claim that every encounter or client presentation was
played through.


## Sagiroth main refresh, September 13

Integrated Sagiroth/TortoiseBots main through
d477d743a85c44bf1cefc7d4d120d84ba0511d02 from common base
1da06b8110dc0bffacb9124593237abb2e0cf7f6. Includes beginner beast grinding,
one-time persistent-level skill/profession seeding, low-level travel area gates,
challenge exclusions and lethal-killer tracking, external observability roster,
module verbosity and empty-chat guards. Three-way conflicts preserve ManTech
packet/lifetime queues, native guild trades, Thorn support and regression hooks.
Engine logging honors either explicit EnableActionLog or upstream debug strategy.
Local throughput/config/reset corrections are described in configuration-tuning.
Runtime testing was explicitly declined for this build by the user.
