# Networking notes

Working notes for the online/server cleanup. Kept up to date as the work lands so the history of decisions is easy to follow.

## Goals

- Remove Westwood Online (WOL). Its services are gone and its COM client (`WOLAPI.dll`) is 32-bit only.
- Remove the parts of GameSpy that died with the company: CD key authentication, GameSpy Arcade, MOTD and version checks.
- Keep the GameSpy query and reporting protocol. The community runs a replacement master server that speaks it, and the Tiberian Technologies (TT) patch, the W3D Hub launcher and our builds all use it.
- Show only servers our client can join, and make our servers visible to other OpenW3D clients through the same master.

## Community master server

Findings from the TT install (scripts 4.8 build b9000, `bandtest.dll` holds TT's GameSpy code) and live probes on 2026-10-27:

| Host | Address | Notes |
|---|---|---|
| `gamespy.tiberiantechnologies.org` | 51.77.36.222 | TT's first entry |
| `zenmasters.cncirc.net` | round robin of all four | |
| `renmaster.cncirc.net` | 209.141.35.144 | same box as `renmaster.cncnet.org` and `master-gsa.renlist.n00b.hk` |
| `renmaster-backup.cncirc.net` | 94.130.135.45 | |
| `renmaster-backup2.cncirc.net` | 51.77.36.222 | |
| `renmaster-backup3.cncirc.net` | 209.141.54.175 | |
| `renmaster-backup4.cncirc.net` | 209.141.35.144 | |

Protocol (GameSpy v0, game name `ccrenegade`):

- Servers send `\heartbeat\<queryport>\gamename\ccrenegade` to the master on UDP 27900. The master then queries the server's query port.
- Clients connect to the master on TCP 28900, receive `\basic\\secure\IGNORE`, send `\gamename\ccrenegade\enctype\0\validate\TVJFLK6J\final\\queryid\1.1\list\cmp\gamename\ccrenegade\where\\final\`, and get back 6 bytes per server (IPv4 plus big-endian query port) followed by `\final\`.
- Clients then send `\status\` to each server's query port over UDP and get `\key\value` pairs back, ending with `\final\`.

### TT's broadcaster spec

TT documents its broadcaster (added in TT 4.8 r8841) on its site, "GameSpy Protocol and New Broadcaster" (last updated 2024-11-21). A saved copy sits next to this file but is not committed. The points that matter for us:

- **Masters:** the seven hosts above, tried in that order, heartbeat on UDP 27900. The same hosts serve the list.
- **Framing:** each reply is `\key\value` pairs. A message can be split into parts. Every part carries `\queryid\<message>.<part>` and only the last part carries `\final\`. Parts should stay under about 400 characters.
- **Heartbeat:** `\heartbeat\<queryport>\gamename\ccrenegade\final\`, sent at least every 30 seconds. The master replies `\heartbeatresult\<code>\final\`, where 0 means listed. Other codes: 1 game name mismatch, 2 missing `mapname`, 3 missing `hostname`, 4 missing `hostport`, 5 banned name, 6 banned address, 7 duplicate server, 8 blocked map prefix, 9 blocked `gametype`, 10 invalid heartbeat or query reply, 11 server asked to be removed, 12 no reply to the master's query.
- **Queries a server answers:** `basic`, `info`, `rules`, `players`, `status` (all four combined) and `echo`.
  - `basic`: `gamename`, `gamever`.
  - `info`: `hostname`, `hostport`, `mapname`, `nextmap`, `gametype`, `numplayers`, `maxplayers`, `timeleft`, `timeelapsed`, `timelimit`. Times are `HH.mm.ss`. `gametype` is the map prefix, or the mod package name when one is loaded. Clients join at the address they queried plus `hostport`.
  - `rules`: `SVER`, `CSVR`, `DED`, `DG`, `password`, `TC`, `FF`. C&C mode adds `SC`, `BDEG`, `BPEG`. Custom rules come last. TT reports `SVER=TT/4.80-r8841`.
  - `players`: `team_tX`, `kills_tX`, `deaths_tX`, `score_tX` for each team (0 Nod, 1 GDI), then `player_X`, `kills_X`, `deaths_X`, `score_X`, team, `ping_X`, `time_X` for each player. X is the real player ID (1-127), so IDs can have gaps.
- **Custom rules:** a `gamespy_rules.ini` in the server folder with a `[GamespyRules]` section adds key/value pairs to `rules` (for example `Owner`, `Discord`, `IRC`).

### Gaps in our stock reporter (`GameSpy_QnR.cpp`)

- Default masters are `master.gamespy.com` and `master.udpsoft.com`, which no longer resolve.
- `info` cuts `hostname` to 25 characters and has no `nextmap`, `timeleft`, `timeelapsed` or `timelimit`.
- `rules` has no `SVER`, `BDEG` or `BPEG`, and sends a stock `BW` key.
- `players` sends nothing.
- It uses the GameSpy SDK's query/reporting code and CD key hooks (`gcd_init_qr`, `gcd_think`). The protocol above is small enough to implement directly.

About 30 servers were listed. Every TT server reports `SVER=TT/<version>` (or `Dragonade/<version> TT/<version>`), and nearly all report `gamever=838`, so `gamever` alone cannot tell builds apart.

## Telling our servers apart

TT changed the network protocol, so TT clients and OpenW3D builds cannot join each other's servers. OpenW3D servers report `SVER=OpenW3D/<version>`, following TT's `SVER` pattern so other listers show something sensible, plus a custom rule `OpenW3D=<network version>`. The OpenW3D server browser lists only servers that report that key with a network version matching its own. TT servers never send it, and custom rules are a documented part of TT's format.

## Dedicated server

- `renegadeserver --startserver server.ini` hosts in LAN mode when `GameType = LAN`. Ports: UDP 4848 (game), UDP 3373 (LAN broadcast), 63999 (remote admin, loopback unless `AllowRemoteAdmin`).
- Stock `server.ini` files say `GameType = WOL`. Before this work, 64-bit builds skipped WOL and the server sat idle with no game port.

## Progress log

- Branch `online/remove-wol-gamespy`.
- `server.ini` with `GameType = WOL`, or with no `GameType`, now hosts as LAN with a warning. Checked: a stock `server.ini` opens UDP 4848.
- `--gamespy-connect IP:PORT` parses the address correctly. Checked: the client joined a local dedicated server at `10.0.0.214:4848`.
- WOL removed in four commits:
  1. Auto start always uses the LAN game mode.
  2. Every `Find("WOL")` call site follows the non-WOL path. The WOL game mode is no longer registered, and the console commands `wol`, `pat`, `allow` and `page` are gone.
  3. The WOL client code is deleted (about 80 files in `Code/Commando`), along with the NAT negotiation layer, the patch download dialog and the embedded web browser (`W3D_BUILD_OPTION_WEBBROWSER`).
  4. `Code/WWOnline`, `Code/wolapi` and `Code/WOLBrowser` are deleted. The generic `RefCounted`, `RefPtr` and `WaitCondition` helpers moved to `Code/Commando`.
- Behaviour that changed along the way:
  - Slave servers are disabled. They only ran under WOL, which also gave each one its own port, so FDS work needs to give slaves their own ports.
  - wwnet drops `PACKETTYPE_FIREWALL_PROBE` packets.
  - Alternate skins unlock by date only.
  - The host options quick match, ladder and clan checkboxes are always off.
- Checked after each step: all targets build (including LevelEdit), unit tests pass, the dedicated server hosts with the stock `server.ini`, and the client joins it with `--gamespy-connect`.
- Next: GameSpy cleanup. Remove CD key authentication, write our own reporter for TT's broadcaster spec, add the `OpenW3D` rule, then drop the GameSpy SDK.
