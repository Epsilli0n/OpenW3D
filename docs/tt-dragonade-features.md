# TT and Dragonade feature survey

What the Tiberian Technologies (TT) patch and Dragonade (DA) add to Renegade, what OpenW3D already has, and a proposed order for replicating the rest. Based on the local reference copies in `docs/extras/` (TT 4.8.4 public scripts source and Dragonade 1.11.1, not committed) and the TT 4.8 install, surveyed 2026-10-28.

## Sources and licences

- **TT public source** (`limited-tt-source-4.8.4`): scripts.dll (about 2,000 gameplay scripts), the SSGM server framework, 14 small server plugins and two tools. The TT engine itself (`tt.dll`, shipped as `bandtest.dll`) is closed. Its exports are declared in `scripts/engine_tt.h` and `engine_ttdef.h`, which show what the engine adds.
- **Dragonade** (`Dragonade-master`): a server mod built as a scripts.dll on top of a TT 4.8 server. Everything goes through TT engine hooks, plus some 32-bit binary patching.
- **Licences**: file headers in both are GPL v2 "or any later version" with a dynamic linking exception (Dragonade's `LICENSE` file is LGPL 2.1). Both are compatible with OpenW3D's GPL v3. Porting code is allowed as long as the copyright notices are kept. In practice most of it has to be reimplemented anyway, because it calls TT engine exports rather than engine internals.

## Why OpenW3D can do this more directly

TT and DA work from outside a closed engine: DA hooks TT exports and patches machine code, and TT keeps stock compatibility. OpenW3D owns the engine, the dedicated server and the client, and already uses its own network protocol (servers are filtered by the `OpenW3D` rule). So:

- Server features can be engine callbacks and plain C++ in `Code/Commando`, with no plugin ABI or address patching needed.
- Features that need the client (extra keys, HUD messages, dialogs, downloads) can add new network events, as long as client and server change together.

## What OpenW3D already has

- HUD scaling with resolution, desktop resolution on first run, widescreen dialog layout (TT has similar options).
- VSync off switch (`W3D_VSYNC=0`), D3D12 renderer.
- Server list reporting in TT's format, `gamespy_rules.ini` custom rules, in-game server browser, `--connect`.
- Ban list by nickname, IP and IP mask (`banlist.txt`), console `kick` and `ban`.
- Remote admin (RenRem protocol, `scontrol`), `msg` and `amsg` console commands, `donate` console command.
- Stock console: 207 commands, mostly debug ones (`consolefunction.cpp`).
- Mod packages (`modpackage.cpp`), but no automatic download.

## TT engine additions

### Client
- **Map and package downloader**: maps list the packages they need, the client fetches missing ones over HTTP from a repository URL the server sets. TT's most-used feature.
- **Extended key bindings** (`keys.cfg`): bind extra logical keys, which are sent to the server and trigger server-side actions (extra radio commands, taunts, vehicle lock, weapon drop, votes).
- **Server-driven UI**: menus and popups built by the server (labels, buttons, text, checkboxes, combo boxes, sliders, progress and health bars) with replies sent back; HUD overlay surfaces (lines, boxes, text); HUD colour changes; centred HUD messages; game hints with images; radar map changes; weapon and ammo grant notices.
- **Per-player presentation**: sounds, music, fog, sky colours, time of day, camera, screen fade, letterbox and string overrides sent to one player or one team.
- **Graphics**: MSAA and FXAA, higher-resolution and hardware shadows, a shader parameter channel from the server.
- **Anti-cheat**: data file hash whitelist (`anticheat.ini`), server-requested screenshots, serial hash and hardware ID in the join request, damage reported as ammo plus bone so the server can check it.
- **Gameplay engine**: air and naval factories with separate vehicle limits, tech levels, team credits, pathfinding queries, gravity and time scale, object visibility per player.
- Not visible in the source but in TT's feature list: borderless windowed mode, extra resolutions, 4K and HUD scaling, many crash fixes.

### Server (engine API used by SSGM, plugins and DA)
- **Hooks**: chat, host chat, player join and leave, level load (and pre-load), game over, per-frame think, object created, purchase (character, vehicle, powerup, with veto and "free" results), refill, radio, damage, console output, log file, key press, dialog reply.
- **Join filter**: pluggable checks on name, password, exe key, serial hash, hardware ID, client version and revision, with a refusal message shown to the client.
- **Player data**: IP, ping, bandwidth, client version, serial, hardware ID; kick with a reason; forward a player to another server.
- **Map rotation control**: read and change the rotation, reload the level, force one client to reload.
- **Messaging**: coloured chat to all, team or player; private pages; client console prints.

### SSGM (server framework in TT's public source)
- Loads plugin DLLs listed in `ssgm.ini` and passes them the hooks above.
- `ssgm.ini` switches: invincible buildings, force team, refill limit, free purchases, disable any building type or beacons, vehicle ownership and lock icons, extra kill messages, powerup and wreck timers, weather, spawn characters, weapon grants and drops, per-map overrides, preset disable lists.
- **Gamelog**: one line per event (`CREATED`, `DAMAGED`, `KILLED`, `DESTROYED`, `PURCHASED`, `CHAT`, `SCORE`, `WIN`, `CONFIG`), streamed over TCP with the renlog and console output. Server bots (BRenBot and similar) read this stream and send console commands back.
- Plugins: spawn kill protection, ban system, character refunds, crates, capture the flag, extra console commands (take credits or points, kill, give powerup, attach scripts, freeze), first blood bonus, mute, no-points mode, points distribution, random starting credits, sudden death, team swap requests, TeamSpeak channel moves.

## Dragonade features

### Framework
- Event system on top of TT's hooks, where handlers can block an action (chat, purchase, vehicle entry, weapon grant and so on), plus per-object observers and timers.
- Chat commands (`!command`) and key hooks with access levels (VIP, moderator levels, administrator) and chat type filters.
- Layered settings: `da.ini` general, then per map, then per game mode, reloaded every map or with `rehash`.
- Logging: TCP log on port 7025 with four line types (DA log with headers such as `_PURCHASE` and `_PLAYERKILL`, gamelog, renlog copy, console output); damage history per object.
- Connection filter: cleans nicknames (bad characters, length 20), blocks reserved names, handles reconnect "ghosts", and adds DA-specific refusals.

### Game modes
All Out War, Marathon (no time limit), Infantry only, Sniping, Shotgun Wars, Domination (capture and hold nodes, spawn at owned nodes). CTF, Deathmatch and Co-op are placeholders in DA (TT has a CTF plugin).

### Game features
- **Crates**: about 25 crate types (money, death, veteran, health, stealth, spy, UAV, ion storm, random character or vehicle, tank destroyer, EMP and more) with odds that change with score, time and destroyed buildings.
- **Loot**: dead players drop a backpack of their weapons, a DNA powerup (become their character) or a health/armor powerup; `!drop` to drop a weapon.
- **Vehicles**: purchase queue instead of a greyed-out menu, ownership (bind, lock, kick, free, sell), repairable vehicle shells, vehicle limit, theft messages, flip kill toggle.
- **Teams**: remix every N maps, swap sides, auto rebalance, request team change (`!rtc`), squads that stay together across maps with squad chat.
- **Economy and scoring**: points shared among everyone who damaged a kill, donate credits, character refunds, credits multiplier, refinery amounts, score counter toggles, purchasable weapons.
- **Messages**: advanced kill messages (weapon, headshot, squish, fall), kill streaks with sounds, building health reports to the team, beacon and C4 messages, chat sounds.
- **Rules**: spawn protection, infinite ammo or no reload, C4 limit, fake beacon blocking, invincible buildings, disable lists per preset or character, parachutes, veteran ranks that raise health and armor, single-player level cleaner.
- **GameSpy**: own responder, adds the game mode as `gametype`, one rule per active feature, and the player count in the title.

### Commands
- About 25 chat commands for players (`!donate`, `!rtc`, `!vq`, `!bind`/`!lock`/`!sell`, `!squad`, `!c4`, `!veteran`, `!unstuck`, `!killme`, `!taunt` and others).
- About 60 console commands for admins and bots: ban database (bans and exceptions), kick, mute, C4 and beacon locks, access levels and name tags, coloured messages, pages, centred HUD messages, remix, rebalance, swap, force team, reload level, rehash settings.

### Needs client support
Key hooks, emote icons over players and drops, HUD messages, per-player sounds, radar changes, server dialogs and the TT damage path need TT clients. Everything else works for any client.

## Proposed order for OpenW3D

1. **Server foundation**
   - Engine events in the server: chat (with `!command` handling), join and leave, level load, game over, purchase and refill (with veto), damage and kill, object created, think.
   - Join filter with clear refusal messages, nickname cleanup and reconnect handling.
   - Layered server settings file (general, per map, per mode) reloaded each map.
   - Structured log stream for bots: gamelog in SSGM's format over TCP, so BRenBot-style bots work unchanged.
   - Admin commands: kick and ban with reasons, ban database, mute, coloured messages, pages, team messages, remix, rebalance, swap, force team, reload level.
   - Slave servers with their own ports (already on the FDS list).
2. **Server gameplay features** (no client changes): points distribution, spawn protection, character refunds, donate and `!donate`, request team change, team rebalance, building health reports, advanced kill messages, vehicle and C4 limits, fake beacon blocking, refinery amounts, disable lists, infinite ammo, then crates, loot, vehicle queue and ownership, game modes (Marathon, Infantry, Sniping, Domination).
3. **Client and protocol features**:
   - Map and package downloader.
   - Extended key bindings sent to the server (radio, taunts, vehicle lock, votes).
   - Coloured and centred HUD messages, per-player and per-team sounds, emote icons.
   - Server-driven dialogs and HUD surfaces.
   - Server-authoritative damage checks.
4. **Client graphics and quality of life**: MSAA, borderless windowed mode, frame rate limit, more resolution options, HUD colour options.

The ~2,000 TT gameplay scripts (mostly for mods such as Red Alert: A Path Beyond) are out of scope for now; they matter once mods target OpenW3D.
