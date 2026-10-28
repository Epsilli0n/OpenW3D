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
- Westwood Online removed (client code, `WWOnline`, `wolapi`, `WOLBrowser`, NAT layer, embedded browser); stock `server.ini` files asking for WOL host on the LAN instead
- GameSpy SDK and CD key authentication removed; dedicated servers report to the community master servers with our own broadcaster in TT's format, including `SVER=OpenW3D/<version>` and an `OpenW3D` rule (see `docs/networking.md`)
- In-game internet server browser (Multiplay Internet > GameSpy > Join) that lists only servers the client can join, plus `--connect IP[:PORT]`, `--nickname` and `--password`
- Benchmarking switches: `W3D_VSYNC=0` and `W3D_RENDER_STATS=1` (D3D12 also logs draws, uploads and GPU frame time)
- FFmpeg and OpenAL installed through vcpkg (`dev/vcpkg-media`) in the `win` preset, with fallback to the stubs if missing
- Visual Studio 2026 x64 preset, and MSVC builds require the VS 2026 toolset
- Tests: D3D12 device (runs on WARP), D3D12 format conversion, D3DX replacement checks
- `dev/stage-run.ps1` stages a runnable folder against an existing Renegade install
- Fixes: version read from the latest `v*` tag, launcher builds without MFC, DxErr shim includes `stdio.h`, ATI back buffer workaround limited to D3D9
- CI: Windows jobs pinned to `windows-2025`, MSVC x64 built with FFmpeg and OpenAL, packaging and artifact upload only in the upstream repository
- `.gitignore` entries for AI agent and assistant artifacts
