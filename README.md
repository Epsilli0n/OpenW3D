# OpenW3D (Epsilli0n fork)

Changes added on top of [w3dhub/OpenW3D](https://github.com/w3dhub/OpenW3D) at `523ea385`:

- 64-bit Windows as the primary target: x64 builds now have sound and movies (FFmpeg and OpenAL replace the 32-bit-only Miles and Bink), the VS 2026 preset targets x64, and CI checks the x64 media build
- Native Direct3D 12 renderer backend (`W3D_RENDERER_D3D12`, on by default for MSVC Windows builds)
  - Device layer: flip-model swap chain, per-frame fences, upload ring, persistent shader-visible descriptor heaps, deferred resource release
  - Fixed-function pipeline emulated in a single HLSL shader (transforms, 4 lights, texgen, two texture stages with all D3DTOP ops, alpha test, fog), compiled at build time with dxc
  - Pixel format conversion for every D3D9-era format the engine uses, including DXT1-5
  - Render-to-texture, readback, surface copies, mip generation and TGA screenshots
  - Matches D3D9 pixel centres so UI and HUD stay sharp
  - Records only changed state per draw (root constants, pipeline cache, skipped redundant binds)
  - Not yet implemented: additional swap chains (editor only) and gamma ramp
- Renderer backend interface (`w3drender.h`) with a Direct3D 9 backend; all engine rendering goes through it and all D3DX use is confined to the D3D9 backend
- Renderer selection with `--renderer d3d9|d3d12` or `Renderer=` in `[RenderDevice]` of `openw3d.conf` (D3D9 is the default)
- HUD scales with screen resolution (2x at 1080p/1440p, 3x at 2160p, or `HUDScale` in `[RenderDevice]`), with sharp point-sampled bitmap fonts and a square reticle on wide screens
- Widescreen fixes: dialogs laid out in a centred 4:3 area, menu backdrop keeps its framing
- Defaults to the desktop resolution on first run instead of 800x600
- Online play rebuilt around the community master servers (details in `docs/networking.md`)
  - Westwood Online removed: WOL client code and dialogs, `WWOnline`, `wolapi` (including `WOLAPI.dll`), `WOLBrowser`, the NAT negotiation layer, the patch downloader and the embedded web browser (`W3D_BUILD_OPTION_WEBBROWSER`)
  - GameSpy SDK and GameSpy CD key authentication removed; bans still work by nickname and IP
  - Dedicated servers report to TT's seven community masters with our own broadcaster, in the format TT documents: full `info`, `rules` and `players` replies, `nextmap` and time fields, `SVER=OpenW3D/<version>`, and custom rules from `gamespy_rules.ini`
  - Servers also report an `OpenW3D` rule carrying the join compatibility key, so OpenW3D clients find each other's servers and skip TT servers they cannot join
  - In-game internet server browser (Multiplay Internet > GameSpy > Join) replaces the GameSpy Arcade launcher: lists joinable servers with map, players and ping, refreshes with Refresh or F5, prompts for passwords; `OPENW3D_MASTER_SERVERS` points it at other masters
  - `--connect IP[:PORT]`, `--nickname`, `--password` and `--internetserver` (the old `--gamespy-*` names still work); joining with an explicit port works again
  - `server.ini` files that ask for WOL (or set no `GameType`) host on the LAN instead of sitting idle
  - A dedicated server killed mid-game no longer makes the next client launch start hosting
  - Slave servers are disabled until each one gets its own port
  - Test: `commando_serverbrowser_tests` checks the list and query handling against a fake master and servers on loopback
- Benchmarking switches: `W3D_VSYNC=0` and `W3D_RENDER_STATS=1` (D3D12 also logs draws, uploads and GPU frame time)
- FFmpeg and OpenAL installed through vcpkg (`dev/vcpkg-media`) in the `win` preset, with fallback to the stubs if missing
- Visual Studio 2026 x64 preset, and MSVC builds require the VS 2026 toolset
- Tests: D3D12 device (runs on WARP), D3D12 format conversion, D3DX replacement checks
- `dev/stage-run.ps1` stages a runnable folder against an existing Renegade install
- Fixes: version read from the latest `v*` tag, launcher builds without MFC, DxErr shim includes `stdio.h`, ATI back buffer workaround limited to D3D9
- CI: Windows jobs pinned to `windows-2025`, MSVC x64 built with FFmpeg and OpenAL, packaging and artifact upload only in the upstream repository
- `.gitignore` entries for AI agent and assistant artifacts
