# Co-op M1, "Two Gordons, one Minecraft": design

- Date: 2026-10-29
- Branch: `main` (local commits only, never pushed)
- Status: design; the link id (Task 1 of the plan) is done (`66a775d`)
- Inputs: the co-op feasibility study (architecture (a), its section 2; M1, its section 3) and the M0 go/no-go run on
  two copies on one PC (`main` at `2721b60`). Where they disagree, M0 wins: no `coop 1`, no saves with two slots, no
  landmark transitions, no `127.0.0.1`.
- Plan: `docs/superpowers/plans/2026-10-29-coop-m1.md`

## Goal

Two players on `d1_trainstation_02`. The host types `hc_host d1_trainstation_02`: a two-slot Source listen server
starts, and the host's Minecraft opens its world to LAN on a fixed port. The guest types `connect <host's LAN or VPN
address>` in Half-Life 2's console, and the guest's hidden Minecraft joins the host's Minecraft world on its own.

### M1 is done when (the study's list, checked as two pairs on one PC)

1. Each player moves with their own Minecraft.
2. Each sees the other's Minecraft body (and no Source body for the other).
3. Blocks placed or broken by either appear for both.
4. A cell either one digs out of a wall is a hole both see and walk through.

M0's fixes come along: a respawned player is solid, the client predicts the server's walking speed, the weapon
sequence warnings are gone, players spawn next to a teammate instead of on one spot, and Valve's local-player
helpers return the host.

## Decisions

### Rulings already made

| # | Question | Decision |
|---|---|---|
| R1 | Architecture | (a). The host runs a Source listen server, on the `hl2dm` engine only. There is one Minecraft world: the host's integrated server, opened to LAN on a fixed port. A guest's hidden Minecraft joins it on its own when the guest's Source connects. Each machine keeps its own link between its Source client and its Minecraft client |
| R2 | Who leads | The host. With more than one slot, `UTIL_GetLocalPlayer` and `AI_GetSinglePlayer` return the host. HalfCraft's own server code gets a host-player helper and loops over players where the work is per player |
| R3 | Host setup | No `coop 1`: M0 found it breaks every map change. `maxplayers 2`, with `sv_lan 1` for tests. The engine sets `deathmatch 1` by itself and dead players respawn. The respawn clears `FSOLID_NOT_SOLID`. Players connect by LAN or VPN address, never `127.0.0.1`. VPN play needs `sv_lan 0`: documented, not tested |
| R4 | Testing | Two pairs on one PC, the user's choice: `-multirun` on the second Source copy, which has its own link id |
| R5 | Scope | One map. Saves, transitions, vehicles and fixes per map are M3; death means respawn. M0's fixes fold into M1 in M0's order: respawn solidity, prediction speed, weapon sequence spam, spawn placement, the `!player` and helper NULLs. Carrying state across map changes is out of scope |
| R6 | Players | Two for M1. N stays general wherever that costs nothing extra |
| R7 | Link id (Task 1, done: `66a775d`) | The id comes from env `HALFCRAFT_LINK` on both sides. Source also takes `-hc_link <id>` and exports it to its own environment, so the Minecraft it starts inherits it. With no id the names stay as they are today; with an id the mapping is `Local\HalfCraft_v1.<id>`, with `_frame` and `_minecraft` after it as today. Later tasks build on it unchanged |

### Decided here

| # | Question | Decision |
|---|---|---|
| D1 | Two Minecraft clients on one PC with one Microsoft account | Tests run two Loom development clients with offline names: `Gordon` for the host pair (`make mc-test`, as today) and `Gordon2` for the guest pair (a new `testGuestClient` run). The host's integrated server takes offline accounts (`setUsesAuthentication(false)` before `publishServer`) only when `FabricLoader.getInstance().isDevelopmentEnvironment()`. A release starts Minecraft through Prism with production Fabric, where that is false, so vanilla's online mode stays and each player needs their own account. No second account is needed for tests (findings below) |
| D2 | How the guest's Minecraft finds the host | client.dll knows it is a guest (`gpGlobals->maxClients > 1`, connected, net channel not loopback) and reads the host's IPv4 from `engine->GetNetChannelInfo()->GetAddress()`. It reads the Minecraft port from a replicated cvar `hc_coop_mc_port`, which the host's server.dll sets from its Minecraft's report. It writes both into its HostState. Its Minecraft joins `ip:port` |
| D3 | Which port the host opens | `hc_coop_port`, an archived client.dll cvar, default `25565`. The host's client.dll writes it into its HostState, and its Minecraft calls `publishServer(LAN, …, port)` |
| D4 | Protocol | HostState grows by `coopMode`, `coopIp` and `coopPort` (0x50 → 0x60; there is room up to 0x100). McState's 12 padding bytes at 0xE4 become `coopState` and `coopPort`, so its size stays 0xF0. New enums: `CoopMode` (solo, host, guest) and `CoopState` (idle, open, joining, joined, failed). New constant `kCoopRetryMs = 5000`. No new region |
| D5 | The 15 `getSingleplayerServer()` paths | M1 replaces 4: the teleport (`HostClient`), digging (`HostDigClient` ×2) and `hc_mc` (`HostCommands`). The 11 in `InputBridge` carry what server.dll sends, and only the host runs server.dll's game code, so they keep working for the host and never fire on a guest. They become per player in M2 (table below) |
| D6 | Minecraft server code that takes the first player | `players.getFirst()` becomes the host's player (`HostPlayers.host(server)`, the integrated server's owner by UUID). Vanilla moves a respawned `ServerPlayer` to the end of the list, so after the host died once in Minecraft the guest would come first. Only the host's death in Minecraft tells Half-Life (`ServerPlayerMixin`). Only the host's own teleports are marked in the host's JVM (`OwnTeleportMixin`); a guest's go to the guest in an S2C payload. Mobs and actors near any player are M2 |
| D7 | Collision for a guest's Minecraft | The host's server.dll streams collision around every in-game player's Minecraft position, the host's first. The host's Minecraft server records the raw collision messages per region and relays each guest the regions around that guest S2C, nearest first. The guest's `HostCollision` reads them with the same parsers as the ring and ignores its own ring while joined. A region whose bytes didn't change isn't sent again |
| D8 | Bodies | Each client hides the other players' Source models and their weapons while its own Minecraft is in its world. The other player's Minecraft body is already drawn (`AvatarExporter`) |
| D9 | Spawn | A player who spawns while a teammate is alive is put next to them: the host if alive, else the nearest living player. The candidates are a ring of 8 directions at 48, then 96 units, behind the teammate first. The first spot where the standing hull fits, with ground under it and a clear line from the teammate, wins; with none, the map's spawn point as today. The player gets the suit when another player wears it. Weapons aren't given (the guest can't use them before M2) |
| D10 | Players hurting each other | In Source they don't: `server_player_damage` drops a hit whose attacker is another player. Minecraft PvP stays vanilla (a question below) |
| D11 | Same build on both sides | The host's server.dll sets a replicated `hc_coop_layout` to the protocol hash (`proto::kLayout`, hex). A guest whose own hash differs disconnects and says why. A check of the whole build is left for later (a question below) |
| D12 | `hc_host` | `hc_host [map] [vpn]` (client.dll) runs `disconnect; wait; maxplayers 2; sv_lan 1; coop 0; map <map>` (the `wait` lets `disconnect` shut a running server down first: the engine refuses `maxplayers` while one runs, found in Task 3). With `vpn` it runs `sv_lan 0` instead of `sv_lan 1`. Without a map it takes the current level. It refuses on the 32-bit `hl2` engine. The `map` counts as one typed into the console, so no playthrough starts and nothing rolls back |
| D13 | Prediction speed | The client's `C_BaseHLPlayer::HandleSpeedChanges` (an empty virtual today) picks walk, normal or sprint speed as the server's `CHL2_Player::HandleSpeedChanges` does, the way `C_HL2MP_Player`'s copy does for HL2MP |
| D14 | Weapon sequence warnings | The warning in `C_BaseAnimating::DoAnimationEvents` is left out for a weapon carried by another player, as Valve's HL2MP build leaves it out (`#ifndef HL2MP`). This singleplayer code networks the view model's sequence on the weapon entity, so another player's weapon, drawn with its world model, always asks for a sequence that model doesn't have |
| D15 | Valve's helpers | `UTIL_GetLocalPlayer` returns `UTIL_PlayerByIndex(1)` on a listen server with more than one slot (NULL on a dedicated server), and `AI_GetSinglePlayer` returns `UTIL_GetLocalPlayer()`. `!player` already resolves to index 1, the host. `AI_IsSinglePlayer` stays as it is. The `Unable to find ai_goal_police target: !player` line at map start needs nothing: `CAI_PoliceGoal::GetTarget` tries again while its target is NULL (`ai_goal_police.cpp:60-72`) |
| D16 | Jumps landed elsewhere | server.dll calls client.dll's `HalfCraft_JumpLanded` only with one slot. With more, each client's own prediction runs `HalfCraftMove`, which tells its own client |
| D17 | A guest's `hc_mc` | Sent as a chat command (`connection.sendCommand`), with whatever permission the LAN world gives guests. The host's world gives guests commands only in a development client |
| D18 | Test tooling | One guest link id, `guest`. Each Source copy records its pid. `make cmd` reaches a copy by `WM_COPYDATA` to that pid's `Valve001` window, from either desktop. The guest copy logs to `console-guest.log` (`+con_logfile`, since `-condebug` would mix both copies into one file). The guest's Minecraft has its own run folder (`minecraft/run/test-guest`) and world (`HalfCraftTestGuest`), since two clients can't hold one world's `session.lock` |

## Investigation results

### Two Minecraft clients, one account

- **This repo's test clients are offline.** `make mc-test` runs `test_session.py minecraft`, which runs
  `gradle runTestClient`. That is Loom's `testClient` run, which inherits `client`:
  `--username ${HALFCRAFT_USERNAME:-Gordon}`, `-Dhalfcraft.quitWithHost=false`, run folder `run/test`, world
  `HalfCraftTest`. `make mc-run` is `runClient` in `run/`. Loom's `launch.cfg` sets `fabric.development=true` and passes
  no `--uuid` and no real access token. Vanilla's `Main` then makes the profile id with
  `UUIDUtil.createOfflinePlayerUUID(username)`. Two dev clients with different names are therefore two offline players
  with different UUIDs. (The player's own game goes through Prism and a Microsoft account, `tools/game/run_minecraft.py`.
  That file is untracked and not part of this work.)
- **An integrated server opened to LAN always checks authentication, whatever its own client is.**
  `IntegratedServer.initServer()` calls `setUsesAuthentication(true)` unconditionally (26.3 bytecode: `iconst_1;
  invokevirtual setUsesAuthentication`). `ServerLoginPacketListenerImpl.handleHello` skips authentication only for
  `Connection.isMemoryConnection()`, which is the host's own client, or when `usesAuthentication()` is false. A joining
  dev client is therefore sent an authenticating `ClientboundHelloPacket`, and its session join with the dev token
  fails. `publishServer(scope, guestCommands, port)` doesn't touch `onlineMode`, and `setUsesAuthentication(boolean)` is
  public on `MinecraftServer`. One branch of `handleHello` comes first, though: a connection whose requested name equals
  the owner's (`equalsIgnoreCase`) gets the owner's profile with no authentication at all. So over a VPN with
  `sv_lan 0`, anyone who logs in under the host's name gets in as the host (and kicks them: the next bullet). That is
  vanilla's behaviour.
- **One Microsoft account on two clients kicks the host.** Both clients carry the same UUID, and login calls
  `PlayerList.disconnectAllPlayersWithProfile(uuid)`, which disconnects the player already there (the host's own) with
  `multiplayer.disconnect.duplicate_login`.
- **The chosen approach** (D1) is offline names in development only. It needs no second account and leaves the release's
  online mode alone. `enforceSecureProfile()` is false on an integrated server, so offline guests aren't refused for
  chat signing.

### The host's address and port

- client.dll has what it needs. `IVEngineClient::GetNetChannelInfo()` gives the server's address as `"a.b.c.d:port"`
  (`INetChannelInfo::GetAddress`, `inetchannelinfo.h:46`). `IsLoopback()` (`:52`) is true for the listen server's own
  client: with two slots even the host's client goes over a loopback channel. `gpGlobals->maxClients` exists on the
  client too. Source 2013's `netadr_t` is IPv4, so an IPv4 field loses nothing.
- Replicated cvars defined in code that both dlls compile already exist in this tree (the patch's
  `sk_npc_dmg_strider_to_plr`, `FCVAR_REPLICATED`, in shared `hl2_gamerules.cpp`). The server's value reaches every
  client. To be sure a value is sent even when it is a cvar's default, both co-op cvars default to empty or 0 and the
  server sets them.
- The Minecraft port can't come from the guest's side. Only the host's Minecraft knows whether `publishServer`
  succeeded, so it reports `coopState` and `coopPort` in McState, and the host's server.dll copies them into
  `hc_coop_mc_port`. The study's suggestion holds: the net channel's address plus a replicated cvar.

### `getSingleplayerServer()` and the first-player loops

| Where | What | M1 |
|---|---|---|
| `HostClient.java:321` | Half-Life's teleport of the player, on the server | **C2S `halfcraft:teleport`** |
| `HostDigClient.java:60` | dig only with an integrated server | **also when `halfcraft:dig_open` can be sent** |
| `HostDigClient.java:121` | open the dug cell on the server | **C2S `halfcraft:dig_open`** → `HostDig.open` (it already checks reach, building rights, loaded chunk and digOff) |
| `HostCommands.java:26` | `hc_mc` as the player | **chat command on a guest** (D17) |
| `InputBridge.java:83` | `kInHurtMob` | host only (server.dll sends it); per player in M2 |
| `InputBridge.java:84, 86` | `kInBlast`, `kInBulletHit` | host only; world-wide already |
| `InputBridge.java:90-92` | the gravity gun's held blocks | host only; the guest's gravity gun in M2 |
| `InputBridge.java:114, 136, 148` | checkpoints, restores, map entered, and the pending ones | host only; no saves with two slots (M0); M3 |
| `InputBridge.java:167, 181` | hurts and heals | host only; per player in M2 |
| `HostMobs.java:82, 170` | the mob table follows the first player; Source's hits on mobs take the first player | **the host's player**; any player in M2 |
| `HostCombat.java:91` | the actors' level is the first player's | **the host's player's level** |
| `HostBlockDamage.java:147` | the mirror level is the first player's | **the host's player's level** |
| `ServerPlayerMixin.java:32` | any player's death tells Half-Life | **only the host's** |
| `OwnTeleportMixin.java:36` | any player's own teleports are marked in the host's JVM | **the host's there; a guest's by S2C `halfcraft:own_teleport`** |

## Architecture

```
 HOST PAIR (no link id)                                         GUEST PAIR (link id "guest")
 hl2mp_win64: listen server, maxplayers 2  <------ UDP 27015 ------  hl2mp_win64 -multirun -hc_link guest (client)
   server.dll: collision around BOTH players --+                        client.dll: HostState coopMode=guest,
     sets hc_coop_mc_port, hc_coop_layout      |  (replicated cvars)    coopIp=<host>, coopPort=hc_coop_mc_port
   client.dll: HostState coopMode=host,        |                              || guest link
     coopPort=hc_coop_port (25565)             | host link                    ||
        ||                                     v                              ||
   Minecraft "Gordon" + integrated server  <---------- TCP 25565 --------  Minecraft "Gordon2" (no world of its own
     opened to LAN on 25565                                                 while joined)
     CollisionRelay: kColClear/Tris/Void/Region ------- S2C halfcraft:collision ----> HostCollision (relayed)
     HostDig.open, HostTeleports.byHost    <---- C2S halfcraft:dig_open, halfcraft:teleport ----
     OwnTeleportMixin (a guest's own teleports) ------ S2C halfcraft:own_teleport ---->
```

What M1 adds, by side:

- **Source, Valve code (hl2dm patch):** the respawn clears `FSOLID_NOT_SOLID` and calls a hook; the spawn spot is
  placed by a hook; client prediction picks the server's speed; another player's weapon doesn't warn about sequences;
  `UTIL_GetLocalPlayer` and `AI_GetSinglePlayer` return the host; other players' bodies and weapons ask a hook whether to
  draw.
- **Source, HalfCraft code:** `hc_host`, `hc_debug_players`, the co-op cvars, the HostState co-op fields, the layout
  check, the host helper with per-player puppet grace, host-only routes, collision around every player, spawn
  placement.
- **Minecraft:** `HostPlayers`, `CoopPlan` and `CoopClient` (open, join, leave), `CoopNet` (payloads), `CollisionRelay`,
  and `HostCollision` fed by the relay.
- **Tooling:** a second pair on one PC (`LINK=guest` for `make run`, `make cmd`, `make mc-test` and `wait_log.py`).

## Data flow

1. **Hosting.** The host types `hc_host d1_trainstation_02`. The engine drops the menu's background server, takes 2
   slots, `sv_lan 1`, `coop 0`, and loads the map; it sets `deathmatch 1` itself. The host's client.dll now sees
   `maxClients 2`, connected, loopback, and writes `coopMode = host`, `coopPort = 25565` into its HostState. Its
   Minecraft has its own world open. In a development client it turns authentication off, then calls
   `publishServer(LAN, guestCommands = development, 25565)`, and reports `coopState = open`, `coopPort = 25565`.
   server.dll copies that into `hc_coop_mc_port` and sets `hc_coop_layout` at level load. The host's collision consumer
   records every collision message for the relay from now on. If it saw no `kColClear` since recording began, it asks
   server.dll for a full resend once (`kEvCollisionLost`).
2. **Joining.** The guest types `connect 192.168.2.31` and the engine connects. client.dll sees `maxClients 2`,
   connected, not loopback. It checks `hc_coop_layout` against its own hash: on a mismatch it disconnects and says why.
   It parses the address into `coopIp` and writes `coopMode = guest`, `coopIp`, `coopPort = hc_coop_mc_port` (0 until the
   host's world is open: the guest's Minecraft waits). The guest's Minecraft leaves its own world (saving it), sets
   `HostCollision` to relayed (clearing it), and connects to `192.168.2.31:25565` as `Gordon2`. It reports `joining`,
   then `joined`. The host's world gives the newcomer the starter and builder kits, as for any player.
3. **Arrival.** A new player object on the guest makes `HostClient` teleport it to the guest's Source position: the
   local `setPos` plus C2S `halfcraft:teleport`, which the host's server runs as `HostTeleports.byHost(teleportTo)`.
   `holdUntilReady` holds the guest until the relayed regions under it are known. Meanwhile the host's server.dll has
   added the guest's Source position as a streaming centre, and the relay sends the regions nearest the guest first.
4. **Each frame.** Each client.dll writes its own HostState and reads its own McState. Each usercmd carries that
   client's Minecraft position (`hc_origin`, as today), and the server's `HalfCraftMove` moves each player and notes that
   player's puppet time. server.dll streams collision around every player into the host's ring. The relay sends each
   guest its changed regions, at most 32 a tick.
5. **Blocks.** Minecraft's own multiplayer: a block either player places or breaks reaches both clients, and each
   client's `WorldExporter` draws it in its own Half-Life.
6. **Digging.** The guest attacks a diggable face (`HostRay` over the relayed triangles). When the cracks finish, it
   sends C2S `halfcraft:dig_open`, and the host's server runs `HostDig.open`. The `halfcraft:dug` attachment syncs to
   both clients, and each client's `kRenDug` cuts the hole in its own view. The host's client hands the cell to the
   host's server.dll over the bridge (as today). That cuts the collision and streams the changed regions again, and the
   relay sends them to the guest, so both walk through. The host digs as today.
7. **Death.** Source damage on the guest stays Source's (M2 links it). A dead player respawns when the player presses a
   key (`deathmatch 1`; this game's rules are singleplayer's, so nothing respawns a player by itself: `mp_forcerespawn`
   is multiplayer-only): the respawn hook makes them solid, and the spawn hook puts them next to a teammate with the
   suit. Their client.dll sees the jump and teleports its Minecraft. The host's death in Minecraft kills the host's
   Source player (as today), who then respawns the same way. A guest's death in Minecraft respawns them in Minecraft;
   their client.dll takes it as Minecraft's own jump, Source refuses a jump that doesn't fit, and Minecraft is resynced
   to where Source has the guest.
8. **Leaving.** When the guest's Source disconnects, its client.dll writes `solo`. Its Minecraft leaves the host's
   world, clears the relayed collision, and opens its own world again. The host's relay forgets the player on
   disconnect. When the host quits, the guest's Source drops with "Server shutting down" and the same thing follows.

## What's per player

| What | M1 | Later |
|---|---|---|
| Movement from Minecraft (`hc_origin`, `hc_flags`, `HalfCraftMove`) | per player (already) | |
| Puppet grace (`server_player_puppeted`) | **per player** | |
| Collision streaming (`CollisionStreamer` centres) | **every player, the host's first** | |
| Collision into Minecraft | host: the ring; guests: the S2C relay | |
| Dig PVS (`server_add_dig_pvs`) | per player (already) | |
| Weapons following Minecraft's hand (`HC_CMD_WEAPONS`) | per player (already) | |
| Teleports into Minecraft | per machine; a guest's go C2S | |
| Digging | per player (C2S) | |
| Dug cells and blocks in each view | per machine (Minecraft's sync) | |
| Spawn, respawn, solidity | **per player** | carried state: M3 |
| Hurts, heals and health mirroring (`server_player_damage`, `Vitals`) | **host only** | M2 |
| HUD hiding, use/holding, pushes and impulses, `player_speedmod`, hazards, debug target | **host only** | M2 |
| Weapon table, actor table, mob table, event attacker | host only | M2 |
| Solids and dug cells reaching server.dll (NPCs, bullets, collision cuts) | from the host's client: near the host only | M2, from the Minecraft server |
| Death in Minecraft → Half-Life | **host only** | M2 |
| Checkpoints and rollback | none (two slots can't save) | M3 |

## Error handling

- **The host's Minecraft can't open its world** (port taken, I/O error). `publishServer` returns false.
  `CoopClient` logs `HalfCraft: couldn't open the world to LAN on port <p>; trying again in 5 s` and reports `failed`,
  then tries again after `kCoopRetryMs`. server.dll keeps `hc_coop_mc_port` at 0, so guests wait. The host's client.dll
  logs each McState state change once (`co-op: minecraft's world is open on port 25565`, `… couldn't open …`).
- **A guest's join fails or is dropped** (refused, timed out, kicked, a duplicate login, a failed authentication on a
  release). `CoopClient` takes the `DisconnectedScreen`, logs its reason, reports `failed`, goes back to the title
  screen, and tries again every `kCoopRetryMs` while Source is still connected. `MirrorWorld` doesn't open the guest's
  own world while the HostState says guest.
- **A different build on each side.** A guest whose `hc_coop_layout` differs disconnects from Source with
  `co-op: the host runs another HalfCraft build (protocol <theirs>, this one <ours>)`. A Minecraft mod that differs is
  refused by Fabric's own registry sync, with Fabric's message.
- **A collision message over the payload limit.** A custom payload holds at most 1 MiB, so `halfcraft:collision` is
  registered with Fabric's `registerLarge` (it splits larger ones; the cap here is 8 MiB). A recorded message that
  wouldn't fit even so is never sent: the relay logs it once with its region, and the guest's hold times out there (6 s,
  as today). Relayed bytes go into 8-byte-aligned memory before parsing, since a `byte[]` segment can't take `JAVA_INT`
  reads.
- **Bad C2S input.** A teleport with a coordinate that isn't finite or lies outside ±3·10⁷ is dropped and logged once.
  `HostDig.open` refuses as it does today.
- **`hc_host` misused.** On `hl2` it prints `hc_host: co-op needs half-life 2: deathmatch's engine (hl2dm)`. A map name
  that isn't letters, digits and `_` prints the usage line.
- **Connecting to `127.0.0.1`.** The engine ignores it (M0). `hc_host` also prints a line saying guests connect to
  this PC's LAN or VPN address (the engine's own `Network: IP <ip>` line and `status` show it), never `127.0.0.1`.

## Testing

- **Offline:** the core tests (`make core-test`: host commands, co-op mode, address parsing, spawn offsets, harvest
  order), JUnit (`HostPlayers`, `CoopPlan`, the payloads, the relay store, relayed collision), `make lint`,
  `make build` (both engines) and `make tidy`.
- **In game, two pairs on one PC** (R4), in a test session on HalfCraftTest:
  - Host pair: no link id, `make mc-test` (`Gordon`), `make run`.
  - Guest pair: `LINK=guest`, `make mc-test LINK=guest` (`Gordon2`, `minecraft/run/test-guest`, world
    `HalfCraftTestGuest`), `make run LINK=guest` (`-multirun -hc_link guest`, `console-guest.log`).
  - The guest connects to the PC's LAN address from the host's `Network: IP <ip>, mode MP` line, never `127.0.0.1`.
  - Commands go to a copy by pid (`make cmd [LINK=guest]`), and logs are read with `wait_log.py hl2dm`, `hl2dm-guest`,
    `mc-test` and `mc-test-guest`.
  - The Source-only fixes (respawn, prediction, sequences, spawn, helpers) run with Minecraft off, as M0 did.
  - The last task checks "done when" end to end.
- **Not tested:** VPN play (`hc_host <map> vpn`, i.e. `sv_lan 0`; Windows Firewall for `hl2mp_win64.exe` and
  `javaw.exe` on the VPN adapter, often a Public network; anyone who joins under the host's Minecraft name, case aside,
  is taken for the host, as vanilla's login has it), two PCs, a release's Prism instance as host or guest, and
  more than two players.

## Out of scope

- M2: per-player health, hurts and heals through the Minecraft server, keyed by player; the guest's weapon table from
  networked data; event attackers per player; actors and mobs near any player; solids and dug cells for server.dll
  from the Minecraft server; the guest's gravity gun, screenshot key, HUD and pushes.
- M3: saves and a rollback policy, transitions (`changelevel2` is refused with two slots; a plain changelevel with
  carried state), vehicles, per-map fixes, carrying suit, weapons, health and armour across maps.
- M4: a server from a folder.
- Steam networking or e4mc instead of a VPN; a launcher option in `HalfCraft.exe` for hosting or joining.
- Co-op on the `hl2` engine.
- A whole-build version check (D11 checks the protocol hash only).

## Risks

- **`deathmatch 1` under singleplayer rules** may switch on multiplayer-only paths in Valve's code beyond the respawn.
  M0 played for 27 minutes without trouble, but nothing more was checked. The acceptance run watches for it.
- **Prediction now runs for the host too.** `HalfCraftMove` already runs in client prediction, but it was written while
  singleplayer didn't predict the player (`hc_movement.cpp:210`). Re-predicted commands call
  `client_jump_landed_elsewhere` again, which must be harmless (the plan's Review Focus).
- **Load on one PC:** two Source copies (M0: fine) plus two Minecraft development clients and two Gradle runs.
- **Two Gradle runs of one project at once** (`runTestClient` and `runTestGuestClient`). Running a client and a
  server from one project at the same time is common practice. The tooling task checks it first and falls back to
  starting the second client's JVM from Loom's launch
  configuration, with no go-ahead from the user: it is dev tooling only.

## Questions for the user

Nothing waits on these: each has the default the plan uses.

1. **Minecraft port.** The default is fixed `25565`, which `hc_coop_port` changes. Should HalfCraft use a less common port
   by default, in case a Minecraft server already runs on 25565?
2. **Offline accounts.** The default allows them only in development clients; a release keeps online mode, so each
   player needs their own Minecraft account (vanilla's login also lets anyone who joins under the host's own name in
   as the host, without authentication, whatever the mode). Should a release also offer an offline LAN mode, as e4all
   does?
3. **Friendly fire.** By default the players can't hurt each other in Source, and Minecraft PvP stays on as in any LAN
   world. Should Minecraft PvP be off too?
4. **The guest's Minecraft commands.** By default a guest's `hc_mc` runs as a chat command, and a guest may run commands
   only in development. Should a release's guests get them too?
5. **Respawn loadout.** By default a respawned player gets the suit (if a teammate wears it), no weapons, and lands
   next to a teammate. Is that right until M2 brings weapons, or should respawn copy the host's weapons now?
6. **The guest's own world.** By default a guest's own Minecraft world stays untouched while they play in the host's,
   and their Minecraft goes back to it when they disconnect. OK?
7. **Version check.** By default only the protocol hash is compared. Should a whole-build check (the version string)
   come before VPN play with friends?
8. **The guest's HUD in M1.** By default the guest sees Half-Life's own health beside Minecraft's hearts, since the two
   aren't linked before M2. Should it be hidden instead?
9. **`hc_host` without a map.** By default it restarts the current map with two slots, or prints its usage line at the
   menu. Should it default to a fixed map instead?
10. **Respawn by itself.** By default a dead player respawns when they press a key, as Half-Life 2's singleplayer rules
    have it (nothing respawns a player on its own: `mp_forcerespawn` is multiplayer-only). Should they respawn by
    themselves after a few seconds? That is a hook in Task 4's respawn code.
