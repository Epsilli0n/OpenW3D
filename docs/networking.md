# Networking notes

How OpenW3D finds and reports internet servers, and the history of the WOL/GameSpy cleanup.

## Summary

- Westwood Online (WOL) is gone from the code base. Its services shut down, and its COM client was 32-bit only.
- GameSpy the company is gone, but the GameSpy v0 server list protocol lives on: the community runs replacement master servers, and the Tiberian Technologies (TT) patch, the W3D Hub launcher and OpenW3D all use them.
- OpenW3D servers report to those masters with our own broadcaster (`Code/Commando/GameSpy_QnR.cpp`), in the format TT documents.
- The OpenW3D client lists only servers it can join (`Code/Commando/serverbrowser.cpp`, dialog in `dlginternetserverlist.cpp`), using the `OpenW3D` rule described below.
- There is no GameSpy SDK and no CD key authentication any more.

## Community master servers

The masters our broadcaster and browser use by default, in TT's order (from TT 4.8 b9000 and live probes on 2026-10-27):

| Host | Address |
|---|---|
| `gamespy.tiberiantechnologies.org` | 51.77.36.222 |
| `zenmasters.cncirc.net` | round robin of the four below |
| `renmaster.cncirc.net` | 209.141.35.144 (also `renmaster.cncnet.org`, `master-gsa.renlist.n00b.hk`) |
| `renmaster-backup.cncirc.net` | 94.130.135.45 |
| `renmaster-backup2.cncirc.net` | 51.77.36.222 |
| `renmaster-backup3.cncirc.net` | 209.141.54.175 |
| `renmaster-backup4.cncirc.net` | 209.141.35.144 |

Server owners can change the heartbeat list with `HeartBeatServers` in `server.ini`. Clients can point the browser elsewhere with the `OPENW3D_MASTER_SERVERS` environment variable (comma separated `host[:port]`, TCP list port, default 28900).

## Protocol

Game name `ccrenegade` (`ccrenegadedemo` for the multiplayer demo build).

**Heartbeat (server to master, UDP 27900):** `\heartbeat\<queryport>\gamename\ccrenegade\final\`, at least every 30 seconds; we send every 25. Add `\statechanged\1` after a map change and `\statechanged\2` when shutting down. The master queries the server back and answers `\heartbeatresult\<code>\final\`:

| Code | Meaning | Code | Meaning |
|---|---|---|---|
| 0 | listed | 7 | duplicate server |
| 1 | game name mismatch | 8 | blocked map prefix |
| 2 | missing `mapname` | 9 | blocked `gametype` |
| 3 | missing `hostname` | 10 | invalid heartbeat or query reply |
| 4 | missing `hostport` | 11 | server asked to be removed |
| 5 | banned name | 12 | no reply to the master's query |
| 6 | banned address | | |

**List (client to master, TCP 28900):** the master greets with `\basic\\secure\<6 character challenge>`. Send `\gamename\ccrenegade\enctype\0\validate\TVJFLK6J\final\\queryid\1.1\list\cmp\gamename\ccrenegade\where\\final\` and read 6 bytes per server (IPv4, then the big-endian query port) until a trailing `\final\`. Address bytes can be `0x5C`, so only the trailing `\final\` marks the end.

**Queries (to the server's query port, UDP, default 25300):** `\basic\`, `\info\`, `\rules\`, `\players\`, `\status\` (all four) and `\echo\<text>`. Replies are `\key\value` pairs, split into parts under about 400 characters. Each part ends `\queryid\<message>.<part>`, and the last ends `\final\queryid\<message>.<part>` (single backslash, as live TT servers send it).

- `basic`: `gamename`, `gamever` (838, the stock network version; TT reports the same).
- `info`: `hostname`, `hostport` (the game port to join, at the address that was queried), `mapname`, `nextmap`, `gametype` (mod package name, or the map prefix such as `C&C`), `numplayers`, `maxplayers`, `timeleft`, `timeelapsed`, `timelimit` (times as `HH.mm.ss`).
- `rules`: `SVER`, `CSVR`, `DED`, `DG`, `password`, `TC`, `FF`, and in C&C mode `SC`, `BDEG`, `BPEG`, then `OpenW3D`, then custom rules from `gamespy_rules.ini` (`[GamespyRules]` section in the server folder).
- `players`: `team_tN`, `kills_tN`, `deaths_tN`, `score_tN` per team (0 Nod, 1 GDI), then `player_N`, `kills_N`, `deaths_N`, `score_N`, `team_N`, `ping_N`, `time_N` per player, where N is the real player ID (IDs can have gaps).

## Telling OpenW3D servers apart

TT changed the network protocol, so TT clients and OpenW3D cannot join each other's servers, and `gamever` is 838 on both. OpenW3D servers report:

- `SVER=OpenW3D/<build version>`, so other listers show what the server runs.
- `OpenW3D=<exe key>`: `cNetwork::Get_Exe_Key()` as 8 hex digits. It is the same key the server checks when a client connects (it covers the network version, the strings.tdb version and the data files CRC), so a matching value means the client can join.

The browser keeps only servers whose `OpenW3D` value equals its own. TT servers never send the key. On 2026-10-28 the masters listed 28 servers, none of them OpenW3D.

## Dedicated server

- `renegadeserver --startserver server.ini`. `GameType = LAN` hosts on the LAN only, `GameType = GameSpy` also reports to the masters. `WOL`, or no `GameType`, is treated as `LAN` with a warning.
- `--internetserver INI` (old name `--gamespyserver`) is `--startserver` plus master reporting.
- Ports: UDP 4848 game (`Port`/`GameSpyGamePort`), UDP 25300 query (`GameSpyQueryPort`), UDP 3373 LAN broadcast, UDP 63999 remote admin (loopback unless `AllowRemoteAdmin`). The query socket binds to `PreferredGameSpyNic` or `--ip` when set.
- To be listed from behind a router, forward UDP 25300 and 4848. A heartbeat sent from a home connection without forwarding got no `heartbeatresult` back.
- The client and server share `openw3d.conf`. `AutoRestartFlag` there is only honoured when a server settings file is in use. Client and server use separate single-instance locks, so both can run at once.
- The server name comes from `bGameTitle` (or `wGameTitle`, which wins) in the game settings file named by `Config`, not from a `GameTitle` key.
- Double-clicked, the server opens its own console. Started from a terminal, it uses that terminal.
- Confirmed end to end on 2026-10-28: a `GameType = GameSpy` server behind a router with UDP 4848 and 25300 forwarded got `heartbeat result 0 (listed)`, appeared on `renmaster.cncirc.net` and `gamespy.tiberiantechnologies.org`, showed up in the in-game browser, and the client joined it from the list with its nickname; chat worked both ways.

## Client

- `--connect IP[:PORT]`, `--nickname NAME`, `--password PASSWORD` join a server directly. The old `--gamespy-connect`, `--gamespy-netplayername` and `--gamespy-password` names still work.
- Multiplay Internet > GameSpy > Join opens the server browser.

## Known gaps and follow-up work

- Slave servers are disabled (`SlaveMasterClass::Startup_Slaves`). They only ran under WOL, which also gave each slave its own port.
- Only one machine has tested the full loop so far: an OpenW3D server and client behind the same router (NAT loopback), not two separate machines.
- Menus still say "GameSpy" in places. The quick match, ladder and clan host options are always off but still shown.
- The old `banlist.txt` hash column is parsed and ignored; hash-only bans no longer match.

## History

Work done on branch `online/remove-wol-gamespy` (October 2026), one commit per step:

1. `server.ini` asking for WOL hosts as LAN; `--gamespy-connect IP:PORT` parses the address.
2. WOL removed: auto start uses LAN only, every `Find("WOL")` call site follows the non-WOL path, about 80 WOL files are deleted along with the NAT layer, patch download and embedded browser, then `Code/WWOnline`, `Code/wolapi` and `Code/WOLBrowser`. `RefCounted`, `RefPtr` and `WaitCondition` moved to `Code/Commando`.
3. CD key authentication removed. The challenge packet type and net class IDs stay in their enums so later values do not shift.
4. Own broadcaster written to TT's spec, then the GameSpy SDK dropped (`wwcommon` now links `ws2_32` itself).
5. `--connect`, `--nickname`, `--password`, `--internetserver` added.
6. Server browser backend with loopback tests (`commando_serverbrowser_tests`), then the dialog.
7. A leftover auto restart flag no longer turns a client launch into a dedicated server.
8. First real listing and join. Fixes found on the way: the server console works when started from a terminal, client and server no longer block each other's launch, and the nickname reaches the server when joining from the list.
