---
id: guide-player-controls
title: Available Bot Commands & Addon Controls
category: guides
summary: Complete and authoritative reference of all currently implemented .bot commands, tactical actions, and addon transport protocols.
tags: [guide, commands, player-controls, addon, tactics, cli]
relates_to:
  - guide-getting-started
  - class-overview
  - concept-strategy-engine
---

# Available Bot Commands & Addon Controls

TortoiseBots provides a native, intent-driven command suite (`.bot`) that powers both the **[TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager)** (`/tbm`) addon and standard in-game chat commands.

All commands require the requesting player to be an in-game character owning the target bot (same account) or a GameMaster.

---

## 1. Tactical Party Actions (`.bot action <intent>`)

The modern control plane operates on **player intent**. Instead of micromanaging each bot, you issue high-level tactical commands; the server resolves target scope and selects appropriate bot executors:

| Command | Target Required | What It Does |
| :--- | :--- | :--- |
| `.bot action attack` | Hostile Target | Controllable party bots engage your current target immediately. |
| `.bot action interrupt` | Casting Hostile | Evaluates party bots and orders a capable bot in range with a ready interrupt (e.g. *Kick*, *Pummel*, *Earth Shock*, *Counterspell*) to interrupt the cast. |
| `.bot action stop` | None | Clears combat queues and stops current attacks. |
| `.bot action pull` | Hostile Target | Directs the party tank to pull your target with ranged attack/taunt while other bots hold damage until threat is established. |
| `.bot action pullback` | Hostile Target | Tank pulls the target and sprints back to the group's current coordinates. |
| `.bot action come` | None | All bots sprint directly to the player's exact coordinates. |
| `.bot action stay` *(or `hold`)* | None | Bots halt at their current position and hold ground. |
| `.bot action follow` | None | Bots break current movement and resume tight follow formation behind the leader. |
| `.bot action focus skull` | Enemy / None | Sets or targets the **Skull** raid icon; orders all party DPS bots to focus fire on that target. |
| `.bot action cc <mark>` | Marked Mob | Orders a capable bot (Mage *Polymorph*, Rogue *Sap*, Warlock *Seduce*, Priest *Shackle*, Druid *Hibernate*) to CC the target. |
| `.bot action aoe <on\|off>` | None | Toggles whether DPS bots cast high-damage AoE abilities (useful to toggle OFF around CC targets). |
| `.bot action ready` | None | Initiates a group ready check across all party bots. |

---

## 2. Roster & Lifecycle Commands

These commands manage the login, party membership, and presence of your owned bots:

| Command | Syntax | What It Does |
| :--- | :--- | :--- |
| **Roster Snapshot** | `.bot roster` | Returns an authoritative snapshot of all owned characters on your account and their online/party state (emits structured `TBM:ROSTER`). |
| **Login Bot** | `.bot add <Name>` | Logs in an owned character from your account as a headless bot. |
| **Logout Bot** | `.bot remove <Name>` *(or `logout`)* | Cleanly logs out an active headless bot on your account. |
| **Invite to Party** | `.bot invite <Name>` | Sends a party invite to an online bot on your account. |
| **Uninvite from Party**| `.bot uninvite <Name>` *(or `kick`)*| Removes an owned bot from your group. |
| **Summon** | `.bot summon [Name]` | Teleports your owned party bot(s) safely to your location out of combat. |

---

## 3. Direct Party & Movement Commands

Direct command shortcuts that operate on your targeted bot or all party bots:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot follow` | `[Name]` | Orders targeted bot (or all party bots) to follow the requester. |
| `.bot stay` | `[Name]` | Orders targeted bot (or all party bots) to stay at current location. |
| `.bot guard` | `[Name]` | Orders bot to guard its current position and engage nearby threats. |
| `.bot free` | `[Name]` | Releases bot from stay/guard back to free autonomous movement. |
| `.bot ready` | None | Checks if party bots are ready (health/mana full, buffs active). |
| `.bot attack` | `[Name]` | Orders targeted bot to attack your current hostile target. |
| `.bot pullback` | `[Name]` | Dispatches pullback maneuver on the specified bot or designated tank. |
| `.bot formation` | `<arrow\|queue\|near\|line\|circle\|shield>` | Sets the geometric follow formation around the party leader. |

---

## 4. Diagnostics & Inspection Commands

Commands for checking bot state, lifecycle, and fleet metrics:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot list` | None | Lists all online bots currently owned by your account (Name, lifecycle, random flag, AI status). |
| `.bot status` | `<Name>` | Displays detailed status of an owned bot: lifecycle (`starting`, `in world`, `removing`), AI attachment, active movement strategy (`follow`, `stay`, `guard`, `free`, `custom`), and owner. |
| `.bot stats` | None | Summarizes owned bot fleet: total online, random bots, and bots with active AI attached. |
| `.bot lease` | `[status]` | Reports autonomous activity lease counts (Idle, Grinding, Trading, LftQueued, BgQueued, PlayerMaster) and lists active lease timers. |

---

## 5. Mature AI Command Delegation & Whispers

In addition to high-level `.bot action` party commands, you can delegate commands directly to a specific bot either via chat whispers (`/w <BotName> <command>`) or through the `.bot command` CLI proxy:

```text
.bot command <BotName> <command>
# Example: .bot command Dudette trainer
```

### Comprehensive Whisper Command Cheat-Sheet

| Category | Command / Whisper | What It Does |
| :--- | :--- | :--- |
| **Inventory & Bags** | `c` / `items` / `inv` | Lists bag items and free bag slot count in chat. |
| | `e <item>` / `equip <item>` | Equips the specified item link or item name from bags. |
| | `ue <slot>` / `unequip <slot>` | Unequips gear from the specified equipment slot into bags. |
| | `u <item>` / `use <item>` | Uses an item from inventory (potions, quest items, bandages). |
| | `drop <item>` | Destroys an item from inventory to free up bag space. |
| **Vendors & Repairs** | `s [gray\|all\|<item>]` | Sells gray junk items or specific items when targeted vendor is open. |
| | `b <item>` | Purchases the specified item from the open merchant window. |
| | `bb <item>` | Buys back an accidentally sold item from the vendor. |
| | `repair` | Repairs damaged equipment at a blacksmith / armorer NPC. |
| | `t` / `trade` | Initiates or accepts a direct trade window with the player. |
| **Quests & NPCs** | `q` / `quests` | Prints active quests and current objective completion status. |
| | `accept` | Accepts an offered quest from a nearby quest giver. |
| | `talk` | Interacts / gossips with the current NPC target. |
| | `r [choice]` / `reward [choice]` | Selects the quest reward and turns in a completed quest. |
| | `share` | Shares eligible quests from the bot's quest log with party members. |
| **Training & Skills** | `trainer` | Automatically learns all currently available spells and ranks from a class trainer! |
| | `talents` | Prints spent talent points and tree distribution. |
| | `spells` | Lists known spells and highest learned spell ranks. |
| **Travel & Life** | `home` | Interacts with an innkeeper to bind the bot's Hearthstone. |
| | `taxi <destination>` | Purchases a flight path to the specified flight master destination. |
| | `release` | Releases spirit after dying. |
| | `corpse run` | Paths as a ghost back to the corpse location and resurrects. |
| | `revive` | Resurrects immediately at the Spirit Healer (with resurrection sickness). |
| **Combat Overrides** | `flee` / `runaway` | Drops combat anchors and retreats toward the master/tank. |
| | `tank attack` | Forces the party tank to prioritize and taunt your target. |
| | `pet` | Commands pet behavior (`pet follow`, `pet stay`, `pet attack`). |
| | `buff` | Prompts the bot to re-cast missing class buffs on party members. |
| | `grind` | Toggles solo autonomous grinding mode for the bot. |
| | `reset` | Flushes AI action queues and resets combat strategy states. |

---

## 6. Auction House Management (`.bot ah` / `.ahbot`)

*(GameMaster / Administrator only)*

Manage and monitor the autonomous synthetic Auction House engine in real time:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot ah status` | None | Displays synthetic AH engine status, inventory size, active listings, and telemetry. |
| `.bot ah reload` | None | Reloads price overrides and blacklist filters from the `ahbot_items` DB table. |
| `.bot ah rebuild` | `[all]` | Triggers an immediate market evaluation pass. Passing `all` expires active unbid synthetic listings to refresh the market. |
| `.bot ah item` | `<id>` | Checks current blacklist status or active price overrides for an item ID. |
| `.bot ah item` | `<id> reset` | Removes any custom price override for an item ID, restoring formula pricing. |
| `.bot ah item` | `<id> <value> [chance] [min] [max]` | Sets custom price (copper), posting chance (%), and stack bounds. Passing `0 0` **blacklists** the item from being posted. |

## ManTech CMaNGOS auction commands

With `AiPlayerbot.AhMarketUseCMaNGOS = 1`, both `.ahbot` and `.bot ah` retain
the CMaNGOS command service: `status [all]`, `reload`, `rebuild [all]`,
and `item <id> [reset|<value> <chance> <min> <max>]`. Use `help` for syntax.
The existing module administrator gate applies before either controller runs.
Rebuilds coalesce and preserve existing bids by default; reload and override
edits are rejected while accepted work is active. Console/SOAP calls continue
through the native command security layer.

ManTech permission detail: `.ahbot` and `.bot ah` require the core's
`SEC_ADMINISTRATOR` by default, with native RBAC/command overrides respected, including authenticated SOAP callers without
a live Player. Ordinary bot commands remain available at player rank subject
to the module's existing ownership checks. Native command registration handles
RBAC, console eligibility and command dispatch before handlers run.


## Bot performance diagnostics for administrators

`.perfmon toggle` enables/disables bot profiling, `.perfmon reset` clears its
counters, and `.perfmon [tick] [stack] [map]` writes the report to the server log.
The registered command defaults to moderator access and supports the server console;
normal command permissions and RBAC still apply. Profiling remains optional.


## Random population administration

`.rndbot` is a native administrator command and is also available from the
console or a console-ranked SOAP account. The registered command's RBAC policy
is checked before dispatch. `.rndbot stats` reports population and queued work;
`.rndbot update` requests the next population reconciliation pass.

`.rndbot refresh|upgrade|revive|change_strategy|remove <name-prefix|all>` queues
the operation for matching live random bots. Owned bots are excluded. Work runs
in bounded batches during the world-owned maintenance phase; pending requests
are deduplicated and revalidated by GUID and login generation before execution. Reclaimed, removed or
teleporting bots are skipped. Refresh/upgrade can alter equipment and abilities;
remove logs bots out through the native lifecycle. Normal population policy may
later replace a removed bot. These commands do not change the configured target.

`rndbot init <name-prefix|all>` queues native factory initialization. It respects
configured level bounds, maximum-level probability, human-level synchronization
and DisableRandomLevels. Busy, grouped, human-controlled and real-guild bots are
excluded. Initialization can change levels, equipment and abilities.

`rndbot teleport|rpg|grind <name-prefix|all>` queues native relocation for eligible
ungrouped random bots. Teleport selects a level-fitting grind destination; grind
restricts the search to RandomBotTeleportDistance; rpg selects a friendly inn and
updates the home bind only after native teleport acceptance. Every point must
pass area-level, faction, terrain and navmesh checks. Missing destinations leave
the bot in place. Explicit admin requests work independently of automatic login
teleports. These operations do not force grouped, leased, pinned, dead or busy
bots to move. Unlike the old global fallback, local grind remains local.

`rndbot reset` resets persistent random-population events except temporary
membership, clears pending admin work and requests target reconciliation after
the database accepts the write. It does not delete characters or accounts.
Invalid commands report an error without changing the population.


`rndbot pid <p> <i> <d>` restores administrative tuning of the world-owned
activity controller. It requires exactly three finite numbers and normal rndbot
permissions; invalid or disabled-module requests do not mutate the controller.
Retuning clears accumulated history. `rndbot stats` includes the current activity
percentage. Whisper `help` opens the native module help catalog.

`rndbot diff` reports native average/maximum world update time and current activity
targets. `rndbot diff <player-ms> <empty-ms>` changes both positive integer targets
for this process; malformed, zero, negative or overflowing values change neither.
Initialization also excludes pinned bots and bots reserved by another service.
The retired `clean map` operation is intentionally unavailable: it detached
threads to unload map/vmap data while native owners could still be using it.
Native map lifetime and cleanup remain authoritative. The old login-manager debug
toggle has no native manager counterpart; use `.bot stats` and headless lifecycle
logs for current state.

Random-bot RPG relocation filters destinations for innkeeper metadata before
spending its bounded terrain validation budget. Relocation samples up to 32
distinct destinations and eight distinct points per destination. Local grinding
checks the selected spawn's map and distance as well as the destination filter,
so a nearby spawn does not authorize teleporting to another distant spawn of
the same creature. Native terrain, navigation and faction checks still apply.

Delayed commands are canceled if their requester logs out or the character is
released back to client control. They are not replayed for a later login or
reinterpreted as a command from the bot's current master. Teleporting a jumping
bot cancels its previous pending landing rather than applying old coordinates.

Guild join completion is reported only after the native guild admits the bot.
Stale or superseded invitations cannot accept a different guild, and a rejected
guild leave does not report success. Nearby guild management stays within the
bot's current guild and requires current membership records.

Group invitation commands retain immediate native invite/accept behavior. Rejected
invites and group leaves are not reported as completed bot ownership transitions.
Mailbox collection reports only money/attachments accepted by the native handler;
failed COD or inventory checks leave the attachment available. A bot can collect
money with full bags. Unsellable items stay in inventory when a COD sale is refused.

Combat stuns no longer impersonate logout requests. The AI consults the native
session logout flag and pauses while its timer is pending. The mature chat logout
intent is consumed by BotManager outside the AI stack, with saved teardown behind
the removal guard; logout cancel can clear an intent that has not yet been consumed.
Native headless sessions now honor an actual elapsed logout timer on the world
packet owner. Map packet processing and ordinary stuns cannot expire the session.
This corrects the snapshot-candidate live fixture unexpectedly removing itself
after login (the failed native-snapshot-packet check remains failure evidence).


### RPG inn travel cooldown (2026-09-12)

Accepted administrative RPG relocation now clears the old travel target and
restores its ten-minute cooldown after Reset. This preserves the inn dwell time
from the ManTech donor RandomPlayerbotMgr.cpp at baseline
37aee50d6bfbf9194dd5e3c79a156d9bfcb4f569. Native rejected teleports preserve the
existing target; grind relocations do not acquire the RPG delay. The module uses
its existing TravelMgr and TravelTarget APIs; there is no new core hook.
ModuleRandomRelocationTest covers rejection, reset ordering, RPG/grind distinction
and a missing optional travel target using the actual relocation function.


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

Automatic `check mail` returns complete unrequested item messages through the native mailbox handler. Requested and auction mail are retained; full return failures leave the message untouched. Guild/travel/speech/gear actions execute after map work joins when invoked from map decisions.


### Follow domains and serial map probe (2026-09-12)

Cross-map following resumes on the world thread, while nearby follow decisions remain on their native map owner. Commands and corpse recovery retain the native world entry point.

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
