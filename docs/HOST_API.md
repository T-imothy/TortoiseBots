---
id: ref-host-api
title: Core Host Seams & Module API Contract
category: reference
summary: Technical specification of the generic C++ host seams, headless session lifecycle, packet bridge, and module integration contracts.
tags: [host-api, seams, c++, headless, packets, contract]
relates_to:
  - concept-architecture-invariants
  - concept-strategy-engine
---

# HOST_API — current TortoiseBots host contract

**Target:** Tortoise WoW 1.18.1 core
**Purpose:** describe the implemented generic core/module boundary used by TortoiseBots.

This file describes the current contract. Historical Phase 1 discovery and design
proposals remain available in Git history and are not active implementation
instructions.

## 1. Boundary rule

The core exposes generic capabilities. TortoiseBots assigns bot meaning to those
capabilities.

```text
Tortoise core
    -> session / lifecycle / packet / module primitives
TortoiseBots
    -> bot records / AI / commands / gameplay behavior
```

Normal gameplay systems should not require PlayerBots-specific state. Do not
reintroduce `WorldSession::GetBot()`, `WorldSession::SetBot()`, `m_bot`,
`sPlayerBotMgr`, `PlayerBotEntry`, or scattered bot checks in normal gameplay
code.

## 2. Compatible baseline

The supported local host boundary is validated against:

```text
Core candidate (#411 + #416): e63161c2da7f13ab25687ea389026aa2e3c97647
TortoiseBots tested code:      b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

Validated local core checkpoint:
`e63161c2da7f13ab25687ea389026aa2e3c97647` (corrected #411/#416 candidate).
It is based on the refreshed upstream `main` at `05912a49f7cd8f12afff04b3c37e6f852f981268`.

Upstream status:
generic Headless capability remains proposed in PR [#411](https://github.com/tortoise-wow/tortoise-wow/pull/411)
(`8037fc8`, based on refreshed upstream `main`). It is not yet merged.
The module-facing surface is the three `World` lifecycle calls plus
`SessionTransport` queries.

Generic participant primitives remain proposed in PR [#416](https://github.com/tortoise-wow/tortoise-wow/pull/416).
The corrected candidate is `e63161c`, based on the corrected #411 candidate,
and remains logically separate.

Compile-verified integration snapshot:

```text
Core:         e63161c2da7f13ab25687ea389026aa2e3c97647
TortoiseBots: b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

The exact tested core/module pair must be recorded whenever the core changes;
do not infer compatibility from a branch name.

## 3. Session transport

`WorldSession` distinguishes transport capability from gameplay identity:

```text
SessionTransport::Network
SessionTransport::Headless
```

Generic transport queries include `IsHeadless()` and
`HasNetworkTransport()`. TortoiseBots interprets a Headless session as
module-controlled; the core does not expose a bot object through
`WorldSession`.

Headless initialization uses the core's null/no-network anticheat path rather
than pretending a real socket exists.

## 4. Session registry and lifetime

The supported invariant is:

```text
one account
    +-- at most one active Network session
    +-- zero or more active Headless character sessions
```

Network sessions are account-keyed. Headless sessions are character-GUID keyed.
`World` owns both Network and Headless `WorldSession` lifetime; the
`HeadlessSessionMgr` is the only Headless owner.

The module-facing lifecycle is:

```text
World::StartHeadlessSession(accountId, characterGuid, locale, tag)
World::StopHeadlessSession(characterGuid, save)
World::GetHeadlessSessionState(characterGuid)
```

Start performs account, character, lock, ownership, duplicate, and live-player
validation before constructing or dispatching anything. Stop hides pending
cancellation, logout, deletion, and character-online cleanup. State hides the
pending/active maps and reports `NotFound`, `Pending`, `Loading`, or `Active`.
An active Headless session whose materialized player remains out of world for five seconds, while neither loading nor teleporting, is stopped and its character-online state is cleared. This generic recovery prevents a failed map or instance transfer from blocking a later Network reclaim.

Headless sessions never enter the account-keyed Network map and never own
`LoginDatabase` account `online` or `current_realm` state.

## 5. Async login dispatch

Async login state carries immutable identity instead of a retained raw session
pointer:

```text
accountId
characterGuid
SessionTransport
request generation/token
```

Completion resolves the appropriate registry and requires every identity field
to match:

```text
Network  -> account-keyed session
Headless -> character-GUID-keyed manager entry
```

The core dispatches exactly one normal `LoginQueryHolder` bundle per accepted
Start request and then calls the shared character materializer. TortoiseBots
does not queue, promote, or dispatch login.

## 6. Human reclaim

A real Network session takes precedence when the same character returns under
human control. The core performs normal session/player lifecycle work only
after proving the existing Headless entry, transport, character, and account
match. It then detaches and deletes that manager-owned Headless session;
TortoiseBots releases or rebinds its record/AI state as appropriate.

The durable master relationship remains module-owned. The core owns the
generic session/player lifecycle and the reclaim transfer.

## 7. Native lifecycle hooks

TortoiseBots integrates through the native module/script system rather than
hard-wired manager calls in unrelated core files.

Current adapters:

| Adapter | Responsibility |
| --- | --- |
| `BotHostAdapter` | startup, shutdown and world update |
| `BotSessionAdapter` | Headless session lifecycle |
| `BotPlayerAdapter` | player lifecycle/reclaim attachment |
| `BotChatAdapter` | native `.bot` command integration |
| `BotPacketAdapter` | packet bridge into Existing PlayerBots (primarily AzerothCore/mod-playerbots) |

The module should prefer an existing generic hook before requesting a new core
seam.

## 8. World update

Bot AI runs on the normal world/game thread. The core exposes a generic world
update listener mechanism, and `BotHostAdapter` drives `BotManager` / AI,
module-owned `PlayerConvenience`, `AhMarketService`, and
`BattlegroundQueueService` updates from that tick.

The core listener is generic; it does not call a PlayerBots singleton.

## 9. Ownership model

| Responsibility | Owner |
| --- | --- |
| Network `WorldSession` lifetime | Core `World` |
| Headless `WorldSession` lifetime | `World::HeadlessSessionMgr` |
| Pending Headless requests | `World::HeadlessSessionMgr` |
| Headless validation and async callback identity | `World::HeadlessSessionMgr` |
| Bot record lifecycle | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement semantics | Existing PlayerBots (primarily AzerothCore/mod-playerbots) actions/strategies |
| Short-lived player convenience state | `PlayerConvenience` |
| Durable master GUID | `BotRecord.masterGuid` |
| Live master pointer | `PlayerbotAI` |

A second owner for session lifetime, AI state, movement or master identity is an
architecture warning.

`BotRecord::InWorld` is bookkeeping, not command readiness. `BotManager` only
publishes a bot through its controllable/live snapshots when the Player has an
active module-owned Headless session, the `PlayerbotAIAdapter` is usable, and
the same `PlayerbotAI` is registered in `PlayerbotAIStorage`. An attach failure
marks the record for removal and stops the Headless session instead of leaving
an apparently online but inert bot.

## 10. Packet bridge

The core exposes generic packet send/receive hooks. `BotPacketAdapter` is the
module packet interpretation layer:

```text
Headless outgoing
    -> PlayerbotAI::HandleBotOutgoingPacket

Network master outgoing
    -> owned AIs HandleMasterOutgoingPacket

Network master incoming
    -> owned AIs HandleMasterIncomingPacket
```

No bot-specific opcode branches belong in core packet handlers.

Synthesized client packets (gameobject use, open/use item, chat) use the
core receive queues: headless sessions drain them via `CanProcessPackets:IsHeadless`
(core #475). This runs the canonical `ProcessPackets` wrapper (script receive
hooks, flood accounting, per-update cap, `ExecuteOpcode` teleport boundary).
Note: core stamps `packetTime` only for perflog profiling — it does not
`FillPacketTime`, so movement opcodes with `m_recvdTime == 0` remain subject to
`MovementHandler` reject-time drops. Chat command hardening (`ParseCommands` on
`.`/`!`) stays module-side until upstream chat hardening lands.

The recorded fixture exercised Headless outgoing delivery, Network-master
outgoing delivery and the existing group-invite Trigger -> Action acceptance
path. Real-client incoming delivery remains a separate manual-client acceptance
boundary.

## 11. Command contract

TortoiseBots owns the native `.bot` surface. Current commands include:

```text
add
remove
logout
roster
action attack|interrupt|stop|pull|pullback|come|stay|follow
action focus skull
action cc <raid-mark>  # star/circle/diamond/triangle/moon/square/cross/skull
action aoe [on|off]
follow
invite
uninvite
stay
guard
free
ready
attack
formation
pullback
summon
list
stats
status
command
help
```

`.bot roster` reads the requester's undeleted account characters and any
explicit cross-account ownership rows from the module-owned durable table. It
emits the stable six-field `TBM:ROSTER_BEGIN`, `TBM:ROSTER`, and
`TBM:ROSTER_END` system-message stream, followed by a separate
`TBM:CC_ASSIGN_BEGIN`, `TBM:CC_ASSIGN`, `TBM:CC_ASSIGN_END` stream for live
AI assignments. Keeping CC metadata separate means an older addon can still
consume the roster unchanged. The roster remains the source of truth for
offline and online owned rows; runtime `BotManager` records remain transient
Headless lifecycle state.

`.bot action` builds one request context from the requester's normal target and
group. Dynamic actions resolve to the targeted controllable owned bot or the
controllable party bots. Interrupt is an executor action: it probes the mature
class/pet action graph for a ready interrupt whose spell data can interrupt the
target's active cast, then executes it or queues the existing reach action.
Pull and Pullback both use the mature `PullStrategy`
but select different existing policy state: ordinary Pull removes `pull back`,
while Pullback enables its return-to-pull-position trigger. CC resolves a
requested raid mark and a suitable executor server-side. Targeting an owned bot
sets that bot's persistent `rti cc` preference; targeting an enemy (or an
existing group mark) lets the server select a capable executor and immediately
attempt the mature CC action. Executor discovery walks the registered mature
CC actions, so Hunter traps/beast control, Paladin Turn Undead, Rogue Sap, and
the other class actions remain eligible without a second class policy table.
Assignment is persisted even when the current marked creature is not legal for
the selected bot; the immediate cast is best-effort and normal AI fallback
remains available. Addon requests receive one structured
`TBM:ACTION_ACK` or `TBM:ACTION_ERR`; incidental mature-AI chat is suppressed
where the existing silent strategy supports it.

`.bot command` delegates to `PlayerbotAI::HandleCommand` for Existing PlayerBots
(primarily AzerothCore/mod-playerbots) command behavior. Authorization uses
the normal account/GM policy implemented by the module/core boundary. Legacy
named commands remain available for CLI and macro compatibility.

## 12. Native module/build contract

The core consumes the repository at:

```text
modules/TortoiseBots/
```

`src/TortoiseBotsModule.cpp` is intentionally the only loader-recursed source.
The broader source graph is registered by `TortoiseBots.cmake`.

Normal native selection:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`BUILD_LEGACY_PLAYERBOTS` controls the separate legacy escape hatch; it is not
the native module selector.

Static module compile definitions/includes/PCH are isolated to the
TortoiseBots module target before it is folded into the combined modules
archive (local integration baseline; not yet upstream in #411 — separate
follow-up).

## 13. Configuration and database contract

The module owns:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
data/sql/world/
data/sql/char/
```

Schema belongs in migrations, not surprise runtime DDL. Missing optional data
should fail closed or use an explicit supported fallback. Expensive travel/cache
generation must not start implicitly on the world thread.

The inherited AI config is broader than the currently accepted Tortoise product;
a config key existing is not itself a support claim.

## 14. Tortoise data contract

Tortoise-specific legality/content should come from the target core/data where
possible:

- race/class legality from core player data;
- race/team identity from core data;
- start locations from `playercreateinfo`;
- Tortoise spells/talents/items from local DBC/SQL;
- collection mounts from the target mapping;
- LFG/meeting-stone and taxi behavior from native core APIs.

Do not replace target data with old Vanilla tables when the target already owns
the answer.

## 15. Unsupported capabilities

When a donor behavior has no meaningful equivalent in the pinned core, adapt it
to a real API, remove/disable it, or fail closed. Do not return fake success
only to satisfy a donor interface.

The completed audit removed or disabled several such compatibility surfaces;
evidence is preserved in Git history and `PROVENANCE.md`.

## 16. LFT queue integration (optional, default-off)

`LftBotFillService` observes the copy-only generic LFT API from core PR #416
and never owns `m_queue`, offers, groups, or a second queue.
The service actually uses only `GetQueuedPlayers`, `QueuePlayer`, `LeaveQueue`,
`IsQueued`, `IsInOffer`, and `AcceptOffer`; core retains all offer,
acceptance, cancellation, and group-formation semantics. `AcceptOffer` is
called only for module-owned Headless participants; humans still accept
through the native addon path.

Candidates are filtered in memory by team, hardcore state, group/live state,
role, and the authoritative `Soromeister/LFT` v0.0.3.3 `LFT.allDungeons`
dungeon `code`/`minLevel`/`maxLevel` range (exact code and normalized display-name
aliases; see `runtime/LftBotFillService.cpp:FindDungeonLevelRange`).
Instance names are normalized through the small module alias table; unknown,
corrupt, and absent (Tortoise-only/custom) ranges fail closed and are logged once. There is no average-human +/-5 approximation,
role hook, private-map access, addon-string injection, or DB query per tick.
Forced roles are cleared on pending exit paths, and reconciliation runs even
when the fill budget is zero.

Config: `AiPlayerbot.RandomBotLftEnabled=0`,
`AiPlayerbot.RandomBotLftUpdateInterval=15000`,
`AiPlayerbot.RandomBotLftMaxFillsPerInterval=1`.

## 17. AH market population (optional, default-off)

`AhMarketService` uses only the native auction transaction path: real bot
inventory items, `AhAction` pricing/usage values, `GetAuctionDeposit`,
`GetCheckedAuctionHouseForAuctioneer`, and
`WorldSession::HandleAuctionSellItem`. Core owns auction/item persistence,
deposits, limits, and ownership transfer. The service never writes auction
rows, fabricates items, or runs the donor `ahbot` thread/tables. No DB scan
per tick, no tick auction scan, no thread, no direct auction writes.

Auctioneer creature positions are captured once from the core object store,
validated for overworld/map/terrain/VMap ground and faction (no MMAP/pathfinding),
and used for a bounded teleport fallback before the native sell handler is invoked.
Active event-gated snapshot positions are not revalidated until restart/data reload.
A shared `try_lock`, 5..3600-second cadence, 1..5 batch cap, and per-bot attempt
cooldown bound world-thread work. Failed attempts are also rate-limited.

Fail-closed eligibility (world-thread read-only, no `m_queue` mutation): bots
with an active `PlayerbotAI` player master (`HasActivePlayerMaster`), any
grouped/manual-use bot (`Player::GetGroup`), LFT queued/in-offer
(`sLFTMgr.IsQueued`/`IsInOffer`, hard-requires core PR #416 `LFT/LFTMgr.h` — build fails with `#error` if absent, no silent fallback),
or inside a battleground/instance (`InBattleGround`/`InBattleGroundQueue`/
`Map::IsDungeon`/`IsBattleGround`) are never selected, posted, or teleported;
per-bot AH action stays independent and never pulls owned/party bots from players.

No per-tick AH/DB scan or new AH-specific core seam is required.
`AiPlayerbot.AhMarketEnabled=0` remains the default; the feature also requires
`RandomBotAutologin=1`.

## 18. Random-bot auto-create (optional, default-off)

`RandomBotService` discovers existing `RNDBOT*` characters; with
`AiPlayerbot.RandomBotAutoCreate=1` (default `0`, one character per
`RandomBotUpdateInterval`, world-thread) it creates the bounded deficit toward
`MinRandomBots`/`MaxRandomBots` through `AccountMgr::CreateAccount` (random
12-character alphanumeric password, hashed and never logged) and the generic
synchronous `CharacterCreation::CreateCharacter` seam (core PR #416). Core owns account/character persistence and validation; the module
never writes `account`/`characters` rows directly, uses no DB worker or donor
creation loop, and does no per-tick `LIKE` scan. Because `LoginDatabase` queues
account creation asynchronously after `AllowAsyncTransactions` (separate from core PR #416),
the service remembers exactly one successful account name whose id is not
immediately visible, retries that same name with bounded/log-throttled cadence
while continuing the existing-account selection path and without allocating
another fresh account (log once after prolonged unresolved period), and does not
allocate orphan accounts. DBC `ChrRaces`/`ChrClasses` and `PlayerInfo`
(`playercreateinfo`) are intersected before selection; permanent failures (mixed,
limit, materialization) are remembered, transient failures (`CHAR_CREATE_ERROR`,
dynamic `CHAR_CREATE_DISABLED`/`PVP_TEAMS_VIOLATION` via faction-balance, and
`LoginDatabase` allocation) back off with 60s throttling, and transient name
collisions (`CHAR_CREATE_NAME_IN_USE`/`CHAR_NAME_RESERVED`/`CHAR_NAME_PROFANE`/
`CHAR_CREATE_FAILED`) are retried silently with another candidate, so a healthy
account is not permanently poisoned by a single bad name or temporary balance
state. Created GUIDs enter the existing Headless candidate/login path.

## 19. Battleground auto-queue (optional, default-off)

`BattlegroundQueueService` provides bounded, demand-aware WSG/AB/AV participation
for live Headless random bots through the existing native
`WorldSession::HandleBattlemasterJoinOpcode` (guid 1337) for join and the existing
native `WorldSession::HandleBattleFieldPortOpcode` action 0 (`CMSG_BATTLEFIELD_PORT`
mapId+0, fail-closed `GetBattleGroundTemplate`/`GetMapId` validation) for
master-reclaim leave. Demand is read from the copy-only generic
`BattleGroundMgr::GetQueuedParticipants` snapshot (core PR #416): no human
waiting participant means no bot is queued, and a non-empty bucket selects its
queue type/bracket and underrepresented team. The core remains the owner of
queue state, invites, and port events; the module never mutates
`m_BattleGroundQueues`, calls `BattleGroundQueue::RemovePlayer` directly, owns a
second queue, starts a worker thread, or writes queue structures. Candidates are
selected in memory and checked for the native level bracket, queue slots,
alive/idle state, deserter/taxi/combat status, and active human master;
reconcile is guarded by `InBattleGround`, `(guid, queueType)` ownership,
`HasActivePlayerMaster` and `InBattleGroundQueueForBattleGroundQueueType` with
fail-closed map validation. AV is always queued solo and success is verified
after the native handler; WSG/AB group joins require every member to be a
service-owned Headless bot. Cadence and per-interval budget are clamped and the
setting defaults off (`RandomBotBgEnabled=0`). Requires core PRs #411 and #416.

## 20. New core seam test

Before adding another core seam, establish that:

1. the behavior cannot live entirely inside TortoiseBots;
2. no current generic hook/API exposes it;
3. the proposed seam is a real generic core concept, not a bot special case.

If the design would make PlayerBots-specific checks spread through normal
core gameplay code, redesign it.

## 21. Historical closure

F-03/F-27 closure and validation boundary are recorded in `PLAN.md` §6.1 and `PROVENANCE.md`; full historical audit evidence is preserved in Git history. This contract covers only the current host API.

## ManTech migration host additions (candidate; 2026-09-12)

The ManTech target retains generic map-owner AI scheduling, auction snapshot
ownership and priority network login queries. Full scheduling adaptation is
still pending; a successful build is not performance parity with the old fork.

- `CommandScript::GetCommands` plus `ChatCommand::ModuleHandler` register `.bot`
  and `.ahbot` through native security/RBAC/console handling. The module no
  longer intercepts `.bot` before core authorization. The AH handler checks
  the native registered AH command permission again for the `.bot ah` alias and SOAP.
- `AuctionHouseAdapter` copies native entries while `GetLock()` is held.
  Background paging uses `GetAuctionsSnapshotPage`; lookups that require a
  live entry retain the same lock through native mutation. Read snapshots do
  not grant permission to mutate: bid/removal still use the native handlers.
- `UnitScript::OnDamageAttempt`, `OnAuraHolderAttempt`, `OnAuraHolderRemoval`
  expose values at native observation points; `AllSpellScript::OnCastAttempt`
  and `OnCastFinished` supply diagnostic values without changing spell control.
  `BotCombatTelemetry` owns bot meaning and logging; the core remains optional.

Validation receipts and the exact candidate state are tracked in the core's
`docs/MODULAR_MIGRATION_CHECKLIST.md`. Production has not been changed.

### ManTech native coordinate path queries (2026-09-12)

The migration core implements `PathInfo(mapId, instanceId)` for requests with
explicit start/end vectors. It uses the same native Detour algorithm and
per-thread map query as unit-owned paths, without creating a fake Player.
Coordinate queries exclude steep polygons, reject absent maps/tiles and invalid
coordinates, and cannot force a failed path into a direct shortcut. The instance
argument does not select a second navmesh: native terrain meshes are map-scoped.
Null-unit construction without a map remains invalid. Area-cost overlays and
fish-area queries are still unsupported; their generation remains disabled.

WorldPosition loads the requested tiles, uses the live unit only on its own map,
and checks native path status. Travel generation is opt-in. Ground movement
dispatch hands precomputed points to MotionMaster::MovePath as its sole owner;
it no longer overwrites a point generator with an independent spline.

### Scheduler classification port

BotPlayerAdapter now registers the existing IsMachineDriven and
IsUpdateCritical hooks. Only a headless character with attached module AI is
machine-driven; network takeover wins even while an adapter still exists.
Human masters, nearby network players/camera viewpoints, human party members
and transfers request responsiveness. The core still owns immediate
combat/taxi/BG/packet classification and bounded trait caching.

This restores map/player scheduling classification, not parallel AI dispatch.
AI remains world-owner-driven until shared BotManager records, packet/action
state and cross-map mutation contracts are adapted. No OnAIUpdate hook has been
enabled prematurely, and no global lock is presented as performance parity.

### Native network reclaim notification

The ManTech core now emits the existing PlayerScript::OnReleaseToClient hook
after headless reclaim preconditions pass and before replacing/deleting the old
session. BotPlayerAdapter forwards it to BotManager::ReleaseToClient, which
unpublishes/destroys module AI and releases module activity leases. The observer
must not stop/delete the core-owned session; native HeadlessSessionMgr retains
transfer and deletion ownership. Invalid reclaim attempts emit no notification.
NativeHeadlessReclaimTest verifies this ordering and rejection boundaries with
the production reclaim implementation and deterministic session fixtures.


## ManTech persistence, roles and construction lifetime (2026-09-12)

The native machine-driven hook can run inside `Player::Player`, before update
fields and the GUID exist. `PlayerbotAIStorage::GetAI(Player*)` is a pointer-only
registry lookup; it must not fall back to dereferencing a GUID. Explicit GUID
consumers use the GUID overload. SetAI publishes both indexes; removal may occur
after player destruction and never dereferences its pointer key. Mutex protection
of indexes does not extend the returned AI object's lifetime or authorize parallel
AI execution.

Existing IsAIControlled/IsManagedBot/GetBotRoles/GetAllowedRoles/SetForcedRole and
HasAIFollowers hooks expose attached headless AI. Network ownership wins. Native
LFT role checks receive current/forced roles; actual human clients retain their
interactive role check. Contradictory forced roles may reselect a reachable premade
random-bot talent path; owned bots and unreachable/low-level roles preserve talents.
The native core LFT filler takes priority over the optional module filler; the
module cancels its pending fill when it does not own that service.

Persistent facade values load once from module-owned `ai_playerbot_values` during
enabled initialization. Missing/unreadable storage disables module initialization.
Cached reads do not query SQL. Accepted asynchronous writes and cache publication
are ordered by the facade mutex; a later DB worker failure is reported by native
DB logging, without an invented transactional rollback guarantee.


## Performance diagnostics ownership

Metric indexes use a registry mutex; samples/reset use per-entry counter mutexes.
PrintStats copies counters under those locks and releases them before formatting
or logging. Entries remain stable until monitor destruction. Counters use 64 bits,
timing uses a monotonic clock, and finishing an existing operation balances its
per-AI nesting stack even if monitoring was disabled meanwhile. Identical nested
names remove only their own frame. Existing/new map buckets initialize lazily.
Disabled starts accept string views to retain the baseline's allocation avoidance.
These locks protect diagnostics, not the AI objects or other shared module state.

The native module command registry now owns `.perfmon` at moderator rank, with
console support and the actual registered command's RBAC check. Supported arguments
are reset, toggle, or the tick/stack/map report options. Native command dispatch
owns the configuration control phase. The unused legacy ChatHandler symbol remains
a bots-disabled compatibility stub; no duplicate handler definition was introduced.


## Packet, shared-value and administrative ownership

PacketHandlingHelper swaps a pending batch under its mutex, then releases the
mutex before invoking AI handlers. Reentrant producers are accepted for the next
batch. Retry order is preserved, and exceptions restore unprocessed work before
propagating; the throwing packet is consumed. This prevents queue-lock deadlocks;
it does not make arbitrary AI handlers safe to execute concurrently.

SharedObjectContext owns a stable helper AI and its value context. Registry
creation and Get/Set evaluation occur under one recursive mutex, including nested
shared lookups. Pointer-valued, one-time caches remain immutable to consumers.
Chat lookup construction, mount publication, guild lookups, custom-strategy cache
changes and ready-check initialization are synchronized. Observability acquires
state before socket ownership; recursive state calls are supported. None of these
locks replace player lifetime or world/map ownership barriers.

Arathi objectives retain GUID plus map-work generation, resolving only through
the bot's current map. Death, world removal, changed generation and missing game
objects invalidate cached selection. No objective cache keeps a GameObject pointer.

Random admin commands resolve the registered `rndbot` entry at administrator
rank. RandomBotService stores GUID/action requests and executes count/time-bounded
batches on the world owner. It rechecks random/headless control and teleport state
before calling existing native recovery or logout paths. Shutdown clears requests.

Travel search permits return on every exit, including exceptions. Reset keeps a
running async future alive without joining the world thread; request actions do
not replace pending work. Completed results are consumed only through PREPARE.
Failed results leave PREPARE and clear the native retry suppression. Deferred work
is consumable immediately and can be discarded without executing it on reset.
Final AI destruction still joins owned running work before dependent state dies.

### Outgoing packet ownership (2026-09-12)

`BotPacketAdapter` submits all three packet directions through
`PlayerbotAIStorage::QueuePacket`. The registry mutex covers lookup and enqueue,
so `RemoveAI` finishes active producers before the adapter deletes its AI. Queue
operations under that lock never execute actions, parse packets, or send replies.
Raw `GetAI` lookups elsewhere still depend on the native lifecycle phase barrier;
this change does not turn raw pointers into lifetime leases.

Outgoing spell failure/delay, knockback, emote and chat notifications are copied
into a per-AI FIFO. `UpdateAI` drains a finite snapshot before its decision delay,
on the current world owner after native map jobs join. Reentrant sends wait for
the next batch. Malformed payloads discard only their own event; an unexpected
exception preserves the remaining FIFO ahead of new arrivals.

Spell and movement notifications originate on the target's native map owner and
capture `Player::GetMapWorkGeneration` there. Dispatch rejects them if the player
transferred, is out of world, or has a pending teleport. Cross-map chat/emote
producers do not read Player state or AI contexts. Ordinary opcode action queues
retain their existing filtering/retry behavior. Each AI lifetime owns its queue;
logout destroys pending reactions instead of delivering them to a relogged bot.

`ModuleOwnerPacketTest` executes the selected module's enqueue/drain methods for
FIFO, reentrancy, malformed/unexpected exceptions, native generation changes,
teleports and 4,000 concurrent chat events. `ModuleAIIdentityTest` exercises all
three delivery directions and removal blocked behind an active enqueue. These
tests do not prove parallel action/strategy execution: world-owned services,
cross-bot actions and remaining mutable caches still require adaptation before
native map AI dispatch is enabled. No new core hook is introduced.


### Delayed reply lifetime (2026-09-12)

LLM reply workers now publish into a weak per-AI mailbox, replacing the
process-wide GUID-addressed queue. The worker waits on futures and reply pacing
without owning an AI, Player or WorldSession. When the AI is released, queued
replies are discarded; a later login of the same character creates a different
mailbox. The native login request token remains private to HeadlessSessionMgr;
this module change requires no new host hook or native session identifier.

The current AI owner drains replies before its decision delay and queues native
client opcodes only while it is still the registered headless AI. Network reclaim
rejects delivery. Malformed chat and command-like replies are discarded; valid
packet read positions are restored before native dispatch. Mailbox synchronization
protects packet data only. Optional LLM network calls are not required for gameplay.

ModuleDelayedReplyTest uses the actual worker/drain bodies to cover deferred
publication, blocked futures at AI destruction, replacement AI lifetimes, normal
delivery, human reclaim and command filtering. The architecture suite now passes
92 tests (8.35s). This does not exercise a live LLM provider or prove that all bot
actions are ready to execute concurrently on separate maps.

### Runtime-discovered lifecycle and catalog gaps (2026-09-12)

The isolated PacketBridgeTest initially passed commands and group invitation but
failed stranded-session recovery. `HeadlessSessionMgr::Update` had no implementation
of the documented five-second grace period. The native registry now accumulates
out-of-world elapsed time after packet dispatch, resets it during loading/teleport
or recovery, and routes an expired session through its existing erase/destroy/save/
online-clear path. The saturated timer cannot overflow on a large world diff.
This is generic headless lifecycle work; there is no new bot-specific host hook.

NativeHeadlessStrandedTest exercises the actual Update body for the exact deadline,
loading, pending transfers, packet-driven recovery, missing players and native stop
requests. ModuleReplySelectionTest covers a missing reply category without random
index underflow. Reply and probability lookups no longer insert missing map keys.
All 94 architecture tests passed (8.05s).

The runtime also exposed an empty reply catalog. Module migration
`20260912091000_world.sql` ports 1,937 reply rows and three probability defaults from
the preserved Tortoise dataset. It stages data in temporary tables and fills missing
entries without replacing existing replies, translations or operator probabilities.
The isolated database check passed repeated application and customization preservation
and rolled back its test edits. The donor's destructive table definitions and
generated help graphs are excluded; this is reply data, not a claim that all mature
bot datasets/help content have been migrated.

The first failed runtime receipt is retained. The checklist records the rerun for
the corrected artifact when available; unit tests do not supersede the runtime gate.


### Restored diagnostic and activity behavior

Bounded movement/taxi and Thorn diagnostic observations now execute in the active
module. TravelTarget's existing `getPosition` API is used; engine/action and cached
activity inspection is read-only. No core hook was added. The three diagnostic
regressions now extract TortoiseBots instead of the old tree.

RandomBotService owns the activity PID on its existing world maintenance cadence.
It publishes into the module's existing priority-bracket selection instead of the
temporary constant 100 percent. Native core map scheduling is unchanged. This
restores activity throttling, not parallel map execution. The existing human/party
priority exemptions remain authoritative.

### Administrative request incarnation

BotManager assigns a monotonically increasing generation to each accepted native
login record. Queued random admin requests retain GUID plus this generation;
removal/reclaim/re-login cancels stale work even when the GUID is reused. This is
module queue identity, separate from the host login token. Initialization and
relocation remain in world-owned bounded maintenance and use existing factory,
TeleportTo, home-bind and activity-lease APIs. No host hook was added.

### Active lifecycle regression ownership

ModuleBotDispatchTest executes BotManager::UpdateBots with reentrant removals,
near/far/pending teleports and exception unwinding. NativeCharacterMaterializationTest
executes the generic CharacterCreation materialization/save/publication tail:
failed creation/save never publishes a player identity or invokes creation hooks,
and the transient Player is destroyed before its Headless session. These replace
the retired holder/future-provisioning fixtures. The architecture test CMake file
no longer reads any mod-playerbots source.

### Social population and channel membership (2026-09-12)

RandomBotFacade's read-only population view contains current Network players and
owned/controlled native companions. Autonomous random bots are excluded. This
preserves the old non-random population contract used by friend activity, guild
chat and relation checks. Population admission still uses BotManager/RandomBotService.
The view is refreshed on the world owner after maps join; consumers resolve GUIDs
again before touching Player/social state.

The native Channel API now exposes HasMember(ObjectGuid), a const read delegating
to its existing membership predicate. The core previously kept both Channel's
membership map and Player's joined-channel list private, so the module could not
answer a membership query through a supported API. ChannelHasRealPlayer uses this
query and current Network sessions, without creating/joining a channel or emitting
not-member packets. No PlayerBots type or hook was added to the core. Querying is
world-owner work; HasMember does not promise concurrent mutation safety.
The unused ChannelAcces layout mirror has been removed.

### Pending movement and requester lifetime (2026-09-12)

Pending jump/knockback landings capture the existing native MapWorkStamp: map,
instance and Player map-work generation. A near/far teleport, instance change or
removed player invalidates that landing before relocation or movement cleanup.
Deadline comparison handles the 32-bit millisecond timer wrapping. No new core
hook was required. This fences the existing jump simulation; it does not replace
native movement handlers or prove client-side jump presentation.

Event requester identities are captured while their Player is live. The module
keeps a GUID, original pointer for comparison only and revocable shared identity;
getOwner resolves ObjectAccessor before returning a live pointer. Before-logout,
logout and release-to-client hooks revoke all copies and remove the registry slot.
GUID/address reuse cannot revive old events. Registry locking covers capture and
revocation only. Lookup/consumption requires the existing world/map lifetime barrier;
the token is not a cross-thread Player lifetime lock.

ChatCommandHolder and external packet/chat/forced triggers capture this identity
at enqueue, not at eventual dispatch. Expired owners are rejected before command
parsing, queue admission and action/reaction execution, so null cannot silently
change the requester to the bot's current master. Autonomous ownerless events
remain valid. Reset clears each external trigger's saved event. The packet trigger
uses its inherited triggered flag for forced-event consistency.

### Native social outcomes and explicit action ownership (2026-09-12)

Guild acceptance checks that the invitation still names the inviter's existing
guild. Native faction/admission rejection does not write a talent note or success
event; membership must be established in that exact guild first. Nearby management
requires a live guild/member and skips members of other guilds. Guild leave honors
native rejection and snapshots diagnostic text before the native operation may
destroy the guild. Group join/invite rejects missing requester/session identities.

Explicit ExecuteAction, QueueAction and CanExecuteAction paths now retain scoped
ownership of transient action nodes through failed initialization/evaluation and
execution, matching the normal decision loop. Expired requester identities reject
before explicit evaluation or queue insertion. Capability queries for missing
actions return false. Queue duplicate merge semantics remain unchanged.

PlayerbotAI::IsSafe requires live world membership on the same native Map object;
transferring players and targets without their own current session are rejected.
This fixes the old OR expression that could admit a transferring target when the
session had no player. It is an eligibility query under the owning lifetime barrier,
not a lock or a license to dereference an arbitrary retained pointer.

### Bounded world-action ownership (2026-09-12)

`BotWorldActions` provides a module-owned post-map queue. Its FIFO holds at most
1024 requests globally and 8 per actor. Each world tick drains at most 64 requests
with a 4 ms soft budget (one request can exceed it). The drain runs inside
BotManager's deferred-removal guard; native gameplay executes outside the queue
mutex. Native eligibility is evaluated again, and nested actions execute in order
on that same world owner. Exceptions restore the execution scope. Malformed packet
actions are discarded and logged; unexpected exceptions propagate through the
existing owner recovery path. Logout, client reclaim and shutdown invalidate work.
Actor and requester identities use the revocable Event lifetime contract.

`Defer` is conditional on an explicit host `MapScope`; default world AI execution
remains synchronous. `Enqueue` explicitly requests delayed execution and returns
admission, not gameplay completion. `WorldScope` records an already-established
world owner for synchronous native commands such as immediate group invitation.
Neither scope locks players or makes a caller safe. The engine continuation contract below handles selected-action eligibility and
completion; remaining trigger/prelude/cross-map ownership gates still apply.

Whole-action boundaries now cover group invite/join/accept/leave/LFG, guild
management, AH listing/bidding/cancellation and guild purchases, mailbox collection
and sending, trade/status and RPG trade/enchant, petition signing, quest sharing,
ready checks, instance reset, hire and cross-player world-buff application. They
retain selection, native handlers and post-handler checks together, rather than
deferring only the mutation while reporting completion early. This is preparation
for joined map AI dispatch: that dispatch remains disabled until the remaining
prelude, decision evaluation and cross-map shared-state contracts are ported.

Native invite actions check the resulting pending group identity, including
battleground original groups. Rejected group leave does not clear durable master
ownership or strategies. Auction try-lock ownership is scoped through exceptions.
Mail collection resolves the native first-attachment result before reporting it,
stops on rejection and formats items before the handler can merge/delete them.
Money collection works without free bag slots. Unsellable COD items are rejected
before inventory removal. Unknown mail subcommands cannot expand the static table.
RPG enchant requires an actual partner AI before invoking its action and rechecks
the partner trade record.

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
acceptance is pending until the new build and disposable trade fixtures pass.


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

Party healing, combat, crowd-control assistance, grinding and line-follow decisions require the existing IsSafe actual-map/session gate before reading another member's gameplay state. Equal map IDs or coordinates do not establish instance ownership. Group readiness across maps remains a separate world-phase migration item.


### World readiness and guild metadata (2026-09-12)

Group readiness now calculates on the world owner, retaining the existing cross-map dungeon-death and recovery decisions as a copied boolean. Guild meeting travel reads a copied guild MOTD through the same world-value boundary. Qualified values retain their complete context key when deferred; callbacks resolve the correct independent cache entry.


### Remaining world services (2026-09-12)

Automatic check-mail now uses native HandleMailReturnToSender with a discovered mailbox and copied message IDs. Native code owns all attachments, money, persistence and deletion. Requested/auction/future/empty/deleted messages and hardcore senders remain untouched; rejected native returns retain the message. Up to eight eligible return attempts run per action. Travel selection/request, petitions/tabards, speech and gear updates join the world owner before eligibility/listeners and also guard direct Execute entry.


### Native party gifts (2026-09-12)

The existing NativeGuildTrades service also offers same-party gifts through OfferParty. It retains a copied native group ID, checks current membership and native map/master incarnations at completion, and uses the same native split/initiate/set/accept/cancel handlers. Guild gifts still require matching nonzero guild IDs. GiveItemAction delegates whole-stack conjured food/water gifts rather than manually changing item ownership; one accepted stack is offered per action.


### Cross-map owner observations (2026-09-12)

Master teleport state, master position history, free-movement center, qualified range filters and master quest-item needs now use copied world-calculated values. WorldCalculatedValue can wrap MemoryCalculatedValue without discarding its change-time/delta/protected-value contract; native memory bookkeeping runs with the world calculation. GetGroupMaster revalidates its fallback through GetLiveMaster and resolves the group leader once. Combat/reset logout cancellation uses coalesced world continuations, rechecks native logout state and retains due-logouts on reset. Same-map gates precede remote master movement/enchant/stance reads.


### Follow domains and serial map probe (2026-09-12)

FollowAction chooses its domain from the current follow target: a same-map target stays on the map owner; a foreign/transferring target resumes the original travel behavior on the world owner. Direct MovementAction::Follow cannot copy a foreign Unit position in MapScope. Flee-to-master, corpse recovery and the ChatCommandAction family require world ownership for their cross-map/native session work. Default-off PacketBridgeTest now wraps complete AI updates in MapScope while retaining serial world execution, exercising deferred rules before native map scheduling is enabled.


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
