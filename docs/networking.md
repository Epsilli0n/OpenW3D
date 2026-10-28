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

About 30 servers were listed. Every TT server reports `SVER=TT/<version>` (or `Dragonade/<version> TT/<version>`), and nearly all report `gamever=838`, so `gamever` alone cannot tell builds apart.

## Telling our servers apart

TT changed the network protocol, so TT clients and OpenW3D builds cannot join each other's servers. OpenW3D servers report an extra rules key, `OpenW3D=<network version>`. The OpenW3D server browser lists only servers that report that key with a network version matching its own. TT servers never send it, and TT's browser ignores keys it does not know.

## Dedicated server

- `renegadeserver --startserver server.ini` hosts in LAN mode when `GameType = LAN`. Ports: UDP 4848 (game), UDP 3373 (LAN broadcast), 63999 (remote admin, loopback unless `AllowRemoteAdmin`).
- Stock `server.ini` files say `GameType = WOL`. Before this work, 64-bit builds skipped WOL and the server sat idle with no game port.

## Progress log

- Branch `online/remove-wol-gamespy`.
