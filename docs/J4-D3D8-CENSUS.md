# J4 — Direct3D 8 census of `sh3.exe` and gap analysis of the inherited shim

Date: 2026-11-13. Scope: milestone J4 of `PORTAGE-SH3-2026-11-11.md` §9, first half (census). Read-only analysis; no
code in `port/`, `recomp/`, `tools/` or `docker/` was touched and the game was not run.

**Evidence legend** (every claim carries one):

- **[S]** static fact read from `game/sh3.exe` (SHA-256 `8ff8f74806f55843…`, unchanged) with its VA.
- **[G]** fact read from the generated-code snapshot (`build/j4-scratch/gen-sh3-snapshot`, copy of `port/gen-sh3` taken at the start of the session). Used only to confirm that a function exists in the recompiled output; the census itself was done on the exe.
- **[C]** reading of the shim source in `port/src/d3d8/` (sh3-port `b2efae8`, no uncommitted change under `port/src/d3d8/` at reading time).
- **[L]** runtime log line from `build/j2/{run,ng1,ng2}/out.txt` (Linux host, Mesa llvmpipe — **not** Vita, **not** console). The brief says `l2/*/out.txt`; the directories are actually `j2/run`, `j2/ng1`, `j2/ng2`. `ng2/out.txt` was still growing while I read it (another session owns the run), so its counts below are "at read time".
- **[I]** inference. Always labelled; never to be quoted as a result.

Nothing here is a Vita or console result. The Vita column of every table is **inferred** from the shim source and from the installed vitaGL headers/archive.

## 0. Method and its limits

1. Disassembled every instruction listed in `re/out/functions-sh3.json` (8,145 functions) with Capstone; collected every `call dword ptr [reg+disp]` (4,016 sites) — COM calls go through the object's vtable, so `disp` = method index × 4.
2. Attributed each site to an object by tracing the register back to `mov reg,[0x72c610]` (device), `[0x72c660]` (IDirect3D8), the getter `0x416950` (returns the device) or a struct copy of the device pointer (`[esi+8]`). For offsets `< 0x74` only the first three attributions are trusted (those offsets collide with texture/surface/buffer/DirectX/CRI vtables). For offsets `>= 0x74` (device-only range) struct-copy attributions are accepted.
3. Recovered call arguments by walking back over the `push` sequence (immediates, `xor r,r`, simple register copies). `var` / `mem` means "not a constant at the call site".
4. Resource interfaces (texture, surface, vertex/index buffer, cube) share low vtable offsets with each other and with non-D3D objects, so section 2.3 is **inferred from offset + argument shape**, restricted to functions that also touch the device.
5. Direct-call reachability from game code (`< 0x620000`) was computed on the Ghidra function list; indirect calls through data vtables are invisible to it.

Reproduction scripts are in `build/j4-scratch/tools/` (`scan.py`, `calls.py`, `trace.py`, `args.py`, `args2.py`, `final.py`, `states.py`); raw tables in `build/j4-scratch/`. All counts are call **sites** in the binary, not call frequencies.

## 1. How SH3 reaches Direct3D

- Single D3D import: `d3d8.dll!Direct3DCreate8` at IAT `0x68a3d0`, reached through the thunk `0x661d12` [S]. Only one caller: `FUN_00418c00` at `0x418cce`, which stores the `IDirect3D8*` in global **`0x72c660`** [S]. The device is global **`0x72c610`**; `0x416950` is a getter that returns it [S]. The `D3DCAPS8` (212 bytes) lives at **`0x72c698`**, getter `0x416960` [S].
- Caps the game actually reads (probe `FUN_00402730`, `0x402730`) [S]: `MaxSimultaneousTextures >= 4` (`0x40273b`), `PixelShaderVersion >= 0xffff0101` (ps 1.1, `0x40274e`), `VertexShaderVersion >= 0xfffe0101` (else `SetRenderState(153 SOFTWAREVERTEXPROCESSING, 1)`, `0x402775`), `TextureCaps & 0x800` (**cube map**), then it creates 10 probe textures from the table at `0x6b07b8` (below) and checks `CheckDeviceFormat`/`CheckDepthStencilMatch` for D24S8 (`0x4b`). Other caps reads: `Caps2 & 0x80000` (`0x4192a5`, CANRENDERWINDOWED), `DevCaps & 0x10000` (`0x4195d2`, HWTRANSFORMANDLIGHT), `PresentationIntervals` (`0x418ddd`).
- Probe table `0x6b07b8` (10 × 0x18 B: w, h, usage, fmt, pool, is_cube) [S]: 512×1024, 512², 256², 128², 64², **62² (non-power-of-two)** all `A8R8G8B8` managed; render targets 512², 64², **62²** (usage 1, pool default); one **128² cube map** managed. The probe textures are created and released immediately (`0x40281c`/`0x402827`).
- `CreateDevice` (4 sites: `0x41962b`, `0x419668`, `0x4196a7`, `0x4196fa`) [S]; runtime: `CreateDevice 640x480 fmt=21 windowed=0 depth=1/75 flags=80` [L ng2:26] = A8R8G8B8 back buffer, auto depth/stencil **D24S8**, behaviour flags `0x80` = **MIXED_VERTEXPROCESSING**. The shim ignores the flags [C].
- **D3DX8 is statically linked** [S]: the exe contains `D3DXERR_*` tables (`0x69e58c`), "Shader Assembler Version 0.91" (`0x69f2a9`), `DisableD3DXPSGP` (`0x69ec0f`), a "d3d8.dll" string (`0x69e8c4`, used for the PSGP probe — runtime shows `LoadLibraryA("d3d8.dll") -> NULL` repeatedly [L ng2]). `D3DXAssembleShader` = `0x62ea78`; the game assembles its **own shader text at run time** and passes the token buffer (`GetBufferPointer`, vtable +0xc) to `CreateVertexShader`/`CreatePixelShader` (`0x45f7f0`, `0x463250`, `0x45f6c0`, `0x461f30`) [S]. Other game-called D3DX entry points: `0x628d56` (filter/mip generation, called from the texture loader `0x493e60`), `0x62710d`/`0x627186` (texture/surface helpers), `0x62a5f0…0x62cb38` (math). The D3DX interface classes at `0x63b7a6…0x63e737` (vtables `0x69ec24/50/78/9c`; render-to-surface/sprite-like: `CreateRenderTarget`, `SetViewport`, state blocks, `DrawIndexedPrimitive` with FVF `0x142`) have **no direct call path from game code** (10 of 67 functions in `0x63b000–0x63efff` are reachable; none of the constructors) [S+I]. Their ~240 device-call sites are therefore listed separately and treated as probably dead until a runtime trace proves otherwise.

## 2. (a) Methods called

### 2.1 `IDirect3D8` (global `0x72c660`) — 8 methods, all game-code, all already in the shim vtable [S][C]

| vtbl off | idx | method | sites | example | shim |
|---|---|---|---|---|---|
| 0x18 | 6 | GetAdapterModeCount | 1 | `0x419128` | implemented (4 modes) |
| 0x1c | 7 | EnumAdapterModes | 1 | `0x41914a` | implemented |
| 0x20 | 8 | GetAdapterDisplayMode | 2 | `0x418a6c`, `0x4193d5` | implemented (1024×768) |
| 0x24 | 9 | CheckDeviceType | 3 | `0x416c3b`, `0x418ade`, `0x41918c` | always OK |
| 0x28 | 10 | CheckDeviceFormat | 1 | `0x418f23` (loop) | OK except `P8`; ~40 formats probed at start [L ng2:50-93] |
| 0x30 | 12 | CheckDepthStencilMatch | 1 | `0x418f49` | always OK |
| 0x34 | 13 | GetDeviceCaps | 3 | `0x418d21`, `0x41928e`, `0x419383` | implemented (`d3d_fill_caps`) |
| 0x3c | 15 | CreateDevice | 4 | `0x41962b`… | implemented |

Not called: `RegisterSoftwareDevice`, `GetAdapterCount`, `GetAdapterIdentifier`, `GetAdapterMonitor`, `CheckDeviceMultiSampleType`.

### 2.2 `IDirect3DDevice8` (global `0x72c610`, getter `0x416950`, struct copies) — game code, 2,402 sites

Columns: sites with the device in the global / through the getter / through a struct copy, number of distinct functions, and the number of sites that sit in the (mostly unreachable) D3DX zone.

| vtbl off | idx | method | game-code sites | via global `[0x72c610]` | via getter `0x416950` | via struct copy | functions | D3DX-zone sites (0x62..+) | example game call sites |
|---|---|---|---|---|---|---|---|---|---|
| 0xc | 3 | TestCooperativeLevel | 3 | 3 | 0 | 0 | 3 | 0 | `0x416ca8`, `0x416eb8`, `0x419975` |
| 0x10 | 4 | GetAvailableTextureMem | 1 | 1 | 0 | 0 | 1 | 0 | `0x5e2755` |
| 0x38 | 14 | Reset | 2 | 0 | 2 | 0 | 1 | 0 | `0x416eeb`, `0x416efb` |
| 0x3c | 15 | Present | 1 | 1 | 0 | 0 | 1 | 0 | `0x419963` |
| 0x40 | 16 | GetBackBuffer | 3 | 3 | 0 | 0 | 3 | 0 | `0x402ac2`, `0x402bf2`, `0x429574` |
| 0x50 | 20 | CreateTexture | 11 | 11 | 0 | 0 | 11 | 0 | `0x402cec`, `0x42723f`, `0x427ae1` |
| 0x58 | 22 | CreateCubeTexture | 2 | 2 | 0 | 0 | 2 | 0 | `0x4027d5`, `0x445e94` |
| 0x5c | 23 | CreateVertexBuffer | 5 | 5 | 0 | 0 | 4 | 0 | `0x4580df`, `0x45db06`, `0x45db39` |
| 0x60 | 24 | CreateIndexBuffer | 2 | 2 | 0 | 0 | 2 | 0 | `0x45db79`, `0x45dd85` |
| 0x64 | 25 | CreateRenderTarget | 2 | 2 | 0 | 0 | 2 | 0 | `0x41de6c`, `0x49fbd1` |
| 0x68 | 26 | CreateDepthStencilSurface | 3 | 3 | 0 | 0 | 3 | 0 | `0x402d32`, `0x48b545`, `0x5e27a4` |
| 0x6c | 27 | CreateImageSurface | 1 | 1 | 0 | 0 | 1 | 0 | `0x4041a4` |
| 0x70 | 28 | CopyRects | 2 | 2 | 0 | 0 | 2 | 0 | `0x403912`, `0x5f312c` |
| 0x7c | 31 | SetRenderTarget | 47 | 45 | 0 | 2 | 17 | 8 | `0x402a92`, `0x402ad9`, `0x402c08` |
| 0x80 | 32 | GetRenderTarget | 5 | 5 | 0 | 0 | 5 | 2 | `0x41de98`, `0x426403`, `0x429eae` |
| 0x84 | 33 | GetDepthStencilSurface | 7 | 7 | 0 | 0 | 7 | 2 | `0x41cbed`, `0x41dec7`, `0x41e30b` |
| 0x88 | 34 | BeginScene | 4 | 0 | 4 | 0 | 3 | 3 | `0x416dc1`, `0x41708b`, `0x4199ad` |
| 0x8c | 35 | EndScene | 2 | 0 | 2 | 0 | 2 | 2 | `0x417019`, `0x419943` |
| 0x90 | 36 | Clear | 8 | 7 | 0 | 1 | 7 | 0 | `0x402464`, `0x416f41`, `0x419724` |
| 0x94 | 37 | SetTransform | 9 | 8 | 0 | 1 | 6 | 10 | `0x46acfc`, `0x48bbb4`, `0x48bda9` |
| 0x98 | 38 | GetTransform | 6 | 6 | 0 | 0 | 6 | 0 | `0x48b942`, `0x48bef9`, `0x48c197` |
| 0xa0 | 40 | SetViewport | 0 | 0 | 0 | 0 | 0 | 6 |  |
| 0xa4 | 41 | GetViewport | 0 | 0 | 0 | 0 | 0 | 1 |  |
| 0xa8 | 42 | SetMaterial | 1 | 1 | 0 | 0 | 1 | 0 | `0x402653` |
| 0xc8 | 50 | SetRenderState | 960 | 950 | 0 | 10 | 73 | 37 | `0x402676`, `0x402775`, `0x404513` |
| 0xcc | 51 | GetRenderState | 1 | 1 | 0 | 0 | 1 | 0 | `0x5f6c63` |
| 0xd0 | 52 | BeginStateBlock | 7 | 7 | 0 | 0 | 1 | 5 | `0x41362e`, `0x413782`, `0x413825` |
| 0xd4 | 53 | EndStateBlock | 7 | 7 | 0 | 0 | 1 | 5 | `0x413774`, `0x413817`, `0x41384a` |
| 0xd8 | 54 | ApplyStateBlock | 1 | 1 | 0 | 0 | 1 | 9 | `0x413589` |
| 0xdc | 55 | CaptureStateBlock | 0 | 0 | 0 | 0 | 0 | 5 |  |
| 0xf4 | 61 | SetTexture | 131 | 129 | 0 | 2 | 59 | 3 | `0x40459e`, `0x413761`, `0x413804` |
| 0xfc | 63 | SetTextureStageState | 823 | 816 | 0 | 7 | 36 | 30 | `0x4026a8`, `0x40454b`, `0x40455f` |
| 0x100 | 64 | ValidateDevice | 1 | 1 | 0 | 0 | 1 | 0 | `0x413617` |
| 0x108 | 66 | SetPaletteEntries | 1 | 1 | 0 | 0 | 1 | 0 | `0x494464` |
| 0x110 | 68 | SetCurrentTexturePalette | 5 | 5 | 0 | 0 | 4 | 0 | `0x468dca`, `0x4dd9cd`, `0x5f570c` |
| 0x118 | 70 | DrawPrimitive | 3 | 3 | 0 | 0 | 3 | 0 | `0x4586f5`, `0x45905b`, `0x5f5728` |
| 0x11c | 71 | DrawIndexedPrimitive | 5 | 2 | 0 | 3 | 3 | 1 | `0x45f512`, `0x462dbb`, `0x462ef6` |
| 0x120 | 72 | DrawPrimitiveUP | 90 | 90 | 0 | 0 | 44 | 0 | `0x4046be`, `0x41bd07`, `0x41be0f` |
| 0x124 | 73 | DrawIndexedPrimitiveUP | 1 | 1 | 0 | 0 | 1 | 0 | `0x5f1d99` |
| 0x12c | 75 | CreateVertexShader | 3 | 3 | 0 | 0 | 3 | 0 | `0x45f844`, `0x4632ab`, `0x48b4dd` |
| 0x130 | 76 | SetVertexShader | 102 | 102 | 0 | 0 | 56 | 2 | `0x4045b1`, `0x416e25`, `0x416edc` |
| 0x138 | 78 | DeleteVertexShader | 2 | 2 | 0 | 0 | 1 | 0 | `0x45f8d0`, `0x45f913` |
| 0x13c | 79 | SetVertexShaderConstant | 76 | 75 | 0 | 1 | 16 | 0 | `0x4583e7`, `0x4586c4`, `0x4589f8` |
| 0x14c | 83 | SetStreamSource | 14 | 14 | 0 | 0 | 10 | 3 | `0x416e5b`, `0x458aa0`, `0x458bd8` |
| 0x154 | 85 | SetIndices | 7 | 7 | 0 | 0 | 6 | 2 | `0x416e47`, `0x45da59`, `0x45eee5` |
| 0x15c | 87 | CreatePixelShader | 3 | 3 | 0 | 0 | 3 | 0 | `0x4271f9`, `0x45f70d`, `0x461f7d` |
| 0x160 | 88 | SetPixelShader | 20 | 19 | 0 | 1 | 14 | 1 | `0x416e35`, `0x416ecc`, `0x42742e` |
| 0x168 | 90 | DeletePixelShader | 2 | 2 | 0 | 0 | 2 | 0 | `0x45f6a6`, `0x461f16` |
| 0x16c | 91 | SetPixelShaderConstant | 10 | 10 | 0 | 0 | 4 | 0 | `0x458f1c`, `0x458f59`, `0x459fc6` |
| | | **total** | **2402** | | | | | | |

Methods **never called from game code**: `QueryInterface`, `CaptureStateBlock`, `GetDisplayMode`, `CreateAdditionalSwapChain`, `SetCursor*`/`ShowCursor`, `SetGammaRamp`/`GetGammaRamp`, `CreateVolumeTexture`, `UpdateTexture`, `GetFrontBuffer`, `MultiplyTransform`, `SetLight`/`GetLight`/`LightEnable`, `SetClipPlane`, `GetMaterial`, `DeleteStateBlock`/`CreateStateBlock`, `ProcessVertices`, `GetVertexShader*`, `GetPixelShader*`, `GetVertexShaderConstant`/`GetPixelShaderConstant`, `GetStreamSource`/`GetIndices`, patches. **There is no fixed-function lighting, no hardware vertex blending, no texture transform matrices, no user clip planes** — lighting is entirely in the vertex shaders and in the CPU vertex colours.

Notable argument patterns [S] (first sites):

- `Present(0,0,0,0)` — `0x419963`, one site; `Clear` flags 7 (×2, `0x402464`, `0x48b2cd`), 3 (×2), 1 (×2), 4 (×2) with z=1.0 and stencil 0/0x10; `SetTransform` types `0x100` (WORLD) ×9, `2` (VIEW) ×6, `3` (PROJECTION) ×3; `SetViewport` only in the D3DX zone (6 sites) — the game never sets a viewport itself, it relies on render-target changes.
- `SetRenderTarget` 47 game sites (17 functions), all `(surface, depth surface | NULL)`; `GetBackBuffer(0,0,&s)` ×3; `GetRenderTarget`/`GetDepthStencilSurface` ×5/×7.
- `CopyRects` ×2 (`0x403912`, `0x5f312c`) with `(src, NULL, 0, dst, NULL)` = full-surface copy; runtime `[copyrects] 512x512 fmt 22 → 512x512` [L ng2].
- `CreateRenderTarget` (`0x41de6c` 64×64 `0x15`; `0x49fbd1`), `CreateDepthStencilSurface` (`0x402d32`, `0x48b545` 256×256 `0x4b`, `0x5e27a4`), `CreateImageSurface` (`0x4041a4`), `CreateCubeTexture(128, 1, 0, A8R8G8B8, MANAGED)` (`0x445e94`).
- `CreateVertexBuffer(len, usage 8 WRITEONLY, fvf 0, MANAGED)` ×4 (`0x4580df`, `0x45db06`, `0x45db39`, `0x45dd68`), one DEFAULT-pool `usage 0, 0x208` (`0x5f493f`); `CreateIndexBuffer(len, 8, 0x65 INDEX16, MANAGED)` ×2 (`0x45db79`, `0x45dd85`). **Index buffers are 16-bit only.**
- `DrawPrimitiveUP` 90 sites (primitive type / stride): TRIANGLESTRIP(5) with stride 0x18 ×29, 0x10 ×14, 0x14 ×12, 0x1c ×9, 0x20 ×6; TRIANGLELIST(4) 0xc ×6, 0x10 ×4; POINTLIST(1) 0x14 ×2; TRIANGLEFAN(6) 0x14 ×1. `DrawIndexedPrimitiveUP` ×1 (`0x5f1d99`: TRIANGLELIST, 12 verts, 10 prims, static data `0x71fa40`/`0x71fae0`, stride 0x14, INDEX16). `DrawPrimitive` ×3 (strips, shader path). `DrawIndexedPrimitive` ×5 (`0x45f512`, `0x463a12` strips; `0x462dbb`, `0x462ef6`, `0x463002` lists).
- Streams: `SetStreamSource` always stream 0, strides 0x24 (×3), 0x20 (×2), 0x18; `SetIndices` ×7. No multi-stream draw.
- Shader constants: `SetVertexShaderConstant(start,count)` — c0×1, c2×4 (view-proj), c6, c8×3 … up to **c93×3** (`m3x3 oT3,r4,c93` ⇒ c93..c95); peak register index 95 ⇒ fits the shim's `MAX_VSC 96`. `SetPixelShaderConstant` registers 0 (×3 from `0x6b56f4`), 1, 2, 3, count 1 — shim `pc[8]` is enough.
- `BeginStateBlock`/`EndStateBlock` ×7 each, all inside `FUN_00413590` (`0x41362e…0x413b55`), called once from `0x413bb0` (device init); `ApplyStateBlock` is called from the one-line wrapper `FUN_00413570` (`0x413589`) which indexes a block table at `0x72c12c` (-1 = none). That wrapper has **~66 call sites in ~20 render-setup functions** (`0x404440`, `0x468d20`, `0x4794b0`, `0x479e30`, `0x49fac0`, `0x4dd710`, `0x59bcc0`, `0x5d7290`, `0x5e82e0`, `0x5e84a0`, `0x5ed490`, `0x5f1c40`, `0x5f2d10`, `0x5f5620`, `0x5f7df0`, `0x5f80b0`, `0x5f8370`, `0x5f8f90`, `0x5fbd20`, `0x5fbf20`, `0x5fc710`, `0x5fce20`) [S]. See gap **G2**.

### 2.3 Resource interfaces (texture / surface / vertex-index buffer / cube) — inferred from offset + argument shape [I]

Restricted to functions that also call the device (168 functions). Totals in the whole exe include DirectInput/DirectSound/CRI vtable calls with the same offsets and cannot be separated statically.

| vtbl off | interpreted as | sites in D3D code | evidence |
|---|---|---|---|
| 0x04 / 0x08 | AddRef / Release (all D3D objects) | 1 / 119 (36 functions) | `call [r+8]` right after create (e.g. `0x402827`) |
| 0x20 | Surface::GetDesc / VB::GetType? | 2 | `0x403b65`.. |
| 0x24 | **Surface::LockRect(pLocked, pRect, flags)** | 6 | flags `0x10` (D3DLOCK_READONLY) ×3: `0x41e12a`, `0x49fe28`, `0x4a027e` — read-backs of a render-target surface; flags 0 at `0x403aaa` |
| 0x28 | Surface::UnlockRect | 5 | follows 0x24 |
| 0x2c | **VB/IB::Lock(off,size,&p,flags)** | 13 (all functions, not only D3D-restricted) | flags 0 at `0x4580f2`, `0x45db1a`, `0x45db50`, `0x45db8c`, `0x462949`, `0x46297f`, `0x464190`; **D3DLOCK_NOOVERWRITE (0x1000)** at `0x5f5893` (ring of dynamic buffers) |
| 0x30 | VB/IB::Unlock | 7 | |
| 0x34 | Texture::GetLevelCount | 1 | `0x4940xx` (loader) |
| 0x38 | Texture::GetLevelDesc(level, &desc) | 9 | `0x4039b2`, `0x41cbc5`, `0x41e28d`, … |
| 0x3c | **Texture::GetSurfaceLevel(level, &s)** | 31 (18 functions) | feeds `SetRenderTarget`/`CopyRects`/`LockRect` |
| 0x40 | **Texture::LockRect(level,&lr,rect,flags)** | 14 | flags 0 everywhere: `0x426f89`, `0x42705c`, `0x436134`, `0x4361d5`, `0x43634d`, `0x43646d`, `0x445ebf` (cube fill), `0x446489`, `0x461df2`, `0x493ef1`, `0x49401f`, `0x494066`, `0x5fa77f` |
| 0x44 | Texture::UnlockRect | 4+ | |
| 0x48 | Texture::AddDirtyRect | 3 (`0x4ee9xx`, `0x61f9af`) | the movie path (already handled in the shim: "never UnlockRect") |

Shim coverage: texture vtable (19 slots), surface (11), VB/IB (14), cube (19) all exist [C]. Missing/stub in the shim and **not called by SH3**: `UpdateTexture`, `GetFrontBuffer`, `CreateVolumeTexture`, `SetGammaRamp` — no gap.

## 3. (b) States and data formats

### 3.1 Render states set with a constant state id (game code, 959 sites with constant id) [S]

Values listed as `value×count`; `var` = not a constant at the call site (loops over a table).

| id | name | sites | functions | values seen (count) | first site |
|---|---|---|---|---|---|
| 7 | ZENABLE | 19 | 12 | 1×14, 0×3, var×2 | `0x41dcd6` |
| 9 | SHADEMODE | 5 | 4 | 2×3, 1×2 | `0x458937` |
| 14 | ZWRITEENABLE | 67 | 32 | 0×29, 1×29, var×9 | `0x404513` |
| 15 | ALPHATESTENABLE | 64 | 38 | 0×43, 1×13, var×8 | `0x404525` |
| 19 | SRCBLEND | 62 | 31 | 5×32, 2×12, 1×9, 6×3, 7×3, 9×2, 8×1 | `0x41c120` |
| 20 | DESTBLEND | 54 | 31 | 6×22, 2×15, 1×12, 5×2, 7×2, 8×1 | `0x41c132` |
| 22 | CULLMODE | 28 | 18 | 1×11, 2×9, 0×5, 3×3 | `0x4136af` |
| 23 | ZFUNC | 59 | 25 | 8×30, 4×27, 1×1, 2×1 | `0x41bccc` |
| 24 | ALPHAREF | 16 | 11 | 0×7, var×6, 0x80×2, 0x3b×1 | `0x41368b` |
| 25 | ALPHAFUNC | 28 | 11 | 5×12, 6×4, 2×3, 7×2, 4×2, 1×2, 3×2, 8×1 | `0x413679` |
| 27 | ALPHABLENDENABLE | 98 | 50 | 1×47, 0×40, var×11 | `0x404537` |
| 28 | FOGENABLE | 15 | 13 | 0×11, 1×3, var×1 | `0x41369d` |
| 29 | SPECULARENABLE | 2 | 2 | 0×2 | `0x413655` |
| 34 | FOGCOLOR | 2 | 2 | var×2 | `0x45841b` |
| 35 | FOGTABLEMODE | 4 | 4 | 0×4 | `0x458925` |
| 52 | STENCILENABLE | 73 | 31 | 0×36, 1×30, var×7 | `0x41c17a` |
| 53 | STENCILFAIL | 23 | 10 | 1×17, 2×6 | `0x41c23c` |
| 54 | STENCILZFAIL | 23 | 10 | 1×17, 2×6 | `0x41c24e` |
| 55 | STENCILPASS | 46 | 23 | 1×19, 2×10, 3×10, var×6, 7×1 | `0x41c22a` |
| 56 | STENCILFUNC | 35 | 17 | 8×18, 6×9, 3×7, 4×1 | `0x41c284` |
| 57 | STENCILREF | 32 | 18 | 0x80×8, 0x40×7, var×5, 1×4, 0×3, 0x20×2, 0xc0×1, 0x10×1, 0x11×1 | `0x41c296` |
| 58 | STENCILMASK | 28 | 14 | 0xffffffff×9, 0x80×6, 1×4, 0x40×3, 0x1f×3, 0x20×2, 0xc0×1 | `0x41c260` |
| 59 | STENCILWRITEMASK | 35 | 22 | 0xffffffff×9, 1×5, 0x40×4, 0x80×4, 0xfffffff×4, 0xe0×4, 0x1f×3, 0x20×2 | `0x41c272` |
| 60 | TEXTUREFACTOR | 42 | 18 | var×31, 0×8, 0xff000000×1, 0x800000×1, 0x7f808080×1 | `0x41bc96` |
| 137 | LIGHTING | 1 | 1 | 0×1 | `0x413643` |
| 153 | SOFTWAREVERTEXPROCESSING | 1 | 1 | 1×1 | `0x402775` |
| 168 | COLORWRITEENABLE | 61 | 22 | 15×29, 7×14, 8×14, 0×3, var×1 | `0x41c18f` |
| 171 | BLENDOP | 36 | 8 | 1×27, 3×4, 2×3, var×2 | `0x429ccf` |

Observations:

- Used heavily: **stencil** (73 sites on `STENCILENABLE`; ops 1 KEEP / 2 ZERO / 3 REPLACE / 7 INCR; funcs 8 ALWAYS, 6 NOTEQUAL, 3 EQUAL, 4 LESSEQUAL; masks `0x80`/`0x40`/`0x20`/`0xe0`/`0x1f` — multi-bit stencil values, not a single 1-bit flag) ⇒ a stencil buffer is mandatory and the shim's `GL_DEPTH24_STENCIL8` renderbuffer/vitaGL `STORE_DEPTH_STENCIL=1` build [C] is required.
- `COLORWRITEENABLE` 15/7/8/0: **alpha-only (8) and RGB-only (7) writes** ⇒ the destination alpha channel carries data (back buffer is A8R8G8B8 at `CreateDevice`); blend factors `DESTALPHA(7)`/`INVDESTALPHA(8)` are used (`0x41c120` family). The shim's colour targets are RGBA8 GL textures [C] so alpha exists.
- `BLENDOP` 2 (SUBTRACT) ×3 and 3 (REVSUBTRACT) ×4 are used.
- `CULLMODE` 0/1/2/3 all appear; shim maps NONE and CW/CCW [C].
- `FOGENABLE` 3 sites set to 1 but `FOGTABLEMODE` is always 0 and `FOGCOLOR` only `var` — fog is used with vertex fog (`oFog` in the VS fragments), `FOGSTART/END/DENSITY` never set.
- **Not set by game code**, only by the (unreached) D3DX zone: 128 WRAP0, 136 CLIPPING, 145 DIFFUSEMATERIALSOURCE, 151 VERTEXBLEND, 152 CLIPPLANEENABLE, 164 PATCHSEGMENTS, 167 INDEXEDVERTEXBLENDENABLE (`0x63bc5a…0x63bd2b`, `0x63e2a4…`).
- `TEXTUREFACTOR` is almost always `var` (31 of 42), dynamic per draw.

### 3.2 Texture-stage states (game code, 816 sites with constant type) [S]

| id | name | sites | functions | values seen (count) | first site |
|---|---|---|---|---|---|
| 1 | COLOROP | 145 | 30 | 1×66, 2×54, 4×10, 5×8, 10×5, 7×2 | `0x4135a0` |
| 2 | COLORARG1 | 78 | 26 | 2×52, 3×17, 1×3, 5×2, 0×2, var×2 | `0x4135b4` |
| 3 | COLORARG2 | 22 | 13 | 3×8, 1×6, 0×4, 0x23×2, 2×2 | `0x413949` |
| 4 | ALPHAOP | 149 | 30 | 2×70, 1×66, 4×8, var×4, 5×1 | `0x4135c8` |
| 5 | ALPHAARG1 | 77 | 26 | 2×43, 3×26, 1×2, 5×2, 0×2, var×2 | `0x4135dc` |
| 6 | ALPHAARG2 | 5 | 5 | 0×3, 1×1, 3×1 | `0x413b1a` |
| 11 | TEXCOORDINDEX | 46 | 14 | 0×33, 1×8, var×3, 2×1, 3×1 | `0x4136c3` |
| 13 | ADDRESSU | 71 | 24 | 1×36, 3×33, 4×2 | `0x404573` |
| 14 | ADDRESSV | 72 | 24 | 1×36, 3×34, 4×2 | `0x404587` |
| 15 | BORDERCOLOR | 2 | 1 | 0×2 | `0x41efd1` |
| 16 | MAGFILTER | 70 | 24 | 2×55, 1×15 | `0x40454b` |
| 17 | MINFILTER | 64 | 21 | 2×51, 1×13 | `0x413727` |
| 18 | MIPFILTER | 10 | 4 | 2×10 | `0x40455f` |
| 24 | TEXTURETRANSFORMFLAGS | 1 | 1 | 0×1 | `0x4136d7` |
| 28 | RESULTARG | 4 | 1 | 5×2, 1×2 | `0x41f099` |

Stage index distribution at the 853 sites: stage 0 ×508, stage 1 ×202, stage 2 ×40, stage 3 ×20, variable ×83 [S]. Reading:

- COLOROP values 1 DISABLE / 2 SELECTARG1 / 4 MODULATE / 5 MODULATE2X / **10 SUBTRACT** / **7 ADD**; ALPHAOP 1/2/4/5; args DIFFUSE(0) CURRENT(1) TEXTURE(2) TFACTOR(3) TEMP(5) and `0x23` = TFACTOR|ALPHAREPLICATE. `RESULTARG = TEMP(5)` ×2 (`0x41f099`, `0x41f36e`) — one function writes to the temp register.
- `TEXCOORDINDEX` only 0–3 (no `D3DTSS_TCI_*` generation flags) and `TEXTURETRANSFORMFLAGS` only DISABLE ⇒ no texgen, no texture matrices.
- Addressing: WRAP(1) ×36/×36, CLAMP(3) ×33/×34, **BORDER(4) ×2/×2** (`BORDERCOLOR` = 0). Filters: MAG/MIN POINT(1) and LINEAR(2); `MIPFILTER` LINEAR ×10. No anisotropy.
- No bump-env, no `MAXMIPLEVEL`, no `MIPMAPLODBIAS`.

### 3.3 Vertex formats and primitive types [S]

- FVFs passed to `SetVertexShader` (102 game sites): `0x104` XYZRHW|TEX1 ×33, shader handle (`ret`/`mem`) ×~25, `4` XYZRHW ×16, `0x44` XYZRHW|DIFFUSE ×14, `0x144` XYZRHW|DIFFUSE|TEX1 ×11, `0x204` XYZRHW|TEX2 ×6, `2` XYZ ×6, `0x42` XYZ|DIFFUSE ×5, `0x12` XYZ|NORMAL ×2, `0x142` XYZ|DIFFUSE|TEX1 (D3DX zone). XYZRHW = HUD/2-D/post effects; XYZ|DIFFUSE|TEX1 only in the D3DX zone.
- Vertex declarations used with shaders (3 `CreateVertexShader` sites) [S]: (A) `0x6b4940`: `v0 POSITION FLOAT3, v3 NORMAL FLOAT3, v4 (PSIZE slot) FLOAT2 = texcoord, v5 DIFFUSE D3DCOLOR`, stream 0; (B) table `0x6b6e48[idx]` of decl+text pairs (`0x6b5728`: `v0 FLOAT3, v3 FLOAT3, v4 FLOAT2`; `0x6b573c`: `v0 FLOAT3, v1 BLENDWEIGHT FLOAT3, v2 BLENDINDICES D3DCOLOR, v3 FLOAT3, v4 FLOAT2` = the 4-bone skinned format, 32-byte stride); (C) `0x48b430`: declaration built on the stack, tokens `0x20000000, 0x40020000, 0x40040001, 0xffffffff` = stream 0, `v0 FLOAT3`, `v1 D3DCOLOR` (used with the standalone dp4 VS `0x6b84c8`). **No `UBYTE4`, `SHORT2/4`, `TESSELLATOR` or `CONST` tokens** in the three declarations. The shim's `decl_elems` handles FLOAT1-4/D3DCOLOR/UBYTE4/SHORT2/4 [C] — superset.
- Primitive types: POINTLIST ×2 (`DrawPrimitiveUP`), TRIANGLELIST, TRIANGLESTRIP (dominant), TRIANGLEFAN ×1; no LINELIST/LINESTRIP (shim maps them anyway).

### 3.4 Textures, pools, usage, formats [S][L]

- `CreateTexture` 11 game sites. Formats: `A8R8G8B8 (0x15)` everywhere except `DXT5 (0x35545844)` at `0x427b34` and the loader `0x494130` which passes the format of the decoded `.tex` page (`[esi+8]`, usage 0, **levels from the file, pool MANAGED**; loader `FUN_00493e60` converts P8 (`0x29`) and calls D3DX filtering). Runtime formats actually uploaded in the title + intro run: `fmt=21` (A8R8G8B8), `fmt=22` (X8R8G8B8, only the 512² copy targets), `fmt=894720068` (DXT5), all `lv=0` [L ng2]; sizes 512×1024 (×2), 512×512 (×36), 256², 128² (**≈2 uploads per frame, 76,236 `[tex] upload` lines at read time, 128×128 DXT5 and A8R8G8B8** — unexplained, see G13), 64².
- Render targets (usage 1, pool DEFAULT): 4 sizes from the table at `0x6b1434` = 64², 512², 256², 256² (`0x427ccc`, `0x427d1a`, creating both an RT texture and a managed twin), a screen-size RT texture `[0x72bfd0]×[0x72bfd4]` at `0x402cec` (+ matching depth `0x402d32`), 256² RT at `0x48b1c0` (+ D24S8 `0x48b545`), `0x44645c` 64² managed, `0x5e277e`/`0x5e27a4` RT + depth of variable size (`[esi+0x71e640]`). Runtime: `[rt] tex_gl 512x512 usage=1 ×4, 256x256 usage=1 ×3`, `FBO 640x480 (slot 0)`, `512x512 (slot 1-3)`, `256x256 (slot 4-6)`, **`FBO 1x1 (slot 7)`** [L ng2]. Depth surfaces `D24S8 (0x4b)` 256², 512², variable.
- The startup format enumeration queries (and the shim answers OK to) `R8G8B8, A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, R3G3B2, A8, A8R3G3B2, X4R4G4B4, A8P8, P8, L8, A8L8, A4L4, V8U8, L6V5U5, X8L8V8U8, Q8W8V8U8, V16U16, W11V11U10, UYVY, YUY2, DXT1-5, D16_LOCKABLE, D32, D15S1, D24S8, D16, D24X8, D24X4S4` [L ng2:50-93]. P8 → NOTAVAILABLE (game then converts), everything else → OK, so the game is free to pick bump/luminance formats. In the title + New-Game intro run (`ng2`) it used only A8R8G8B8/X8R8G8B8/DXT5 [L]. Which format a given room texture ends up in beyond that is **unknown** (see G14).
- `.tex` pages: `GB.tex` = 6×512² + 1×512×1024 A8R8G8B8 = 2,097,152 px = **8 MiB per room family at RGBA8** [J3 notes in `DATA-FORMATS-SH3.md`, `HANDOFF.md`]; inner `.map` pages 512² (fmt 0x20 or 0x10).
- Locks: texture `LockRect` flags always 0 (write path, no DISCARD/READONLY); surface `LockRect` with READONLY ×3 (readbacks); VB Lock flags 0 and one NOOVERWRITE site (`0x5f5893`). No `D3DLOCK_DISCARD`, no `DYNAMIC` usage anywhere (`usage` only 0, 1, 8).
- Back buffer: 640×480 `A8R8G8B8`, windowed 0, single back buffer, depth `D24S8`. The shim substitutes 960×544 on Vita [C: `modes[]`, `rt_apply_res`] — the game never reads the display mode after `CreateDevice`.

## 4. (c) Shaders

### 4.1 Where the text lives [S]

- **Vertex shaders are composed at run time** from header + fragment strings and assembled by D3DX (`0x62ea78`, 4 KB composition buffer: `mov eax,0x1004; call 0x663d50` at `0x45f936`). Two composers: **A** `FUN_0045f930` (callers `0x458c30` = room section draw, `0x459c00`; selectors → mixed-radix index into a 6-D cache at `0x848c48`, so each combination is assembled and created once; header `0x6b4958` + fog fragment + N lights + colour/texcoord fragments from the tables `0x6b561c`/`0x6b5638`/`0x6b5688`–`0x6b56cc`) and **B** `FUN_00463400` (callers `0x45ed40`, `0x4628d0`, `0x463640` = model draw; tables `0x6b6e48…0x6b6eb4`, `0x6b6cc4…0x6b6cfc`). Plain (non-composed) VS: `0x6b5568`, `0x6b5758`, `0x6b57b8` (skinned + lit, 550 B), `0x6b59e0`, `0x6b6af4`, `0x6b6b28`, `0x6b6b70` (skinned), and a standalone `0x6b84c8` (dp4 + `oD0` + fog, created at `0x48b43c`).
- Runtime: **≥100 and <150 `CreateVertexShader` calls** in the title + New-Game intro [L ng1:5399, ng2:5406; the shim logs #1-3 and every 50th, #150 is absent], and **no `[vs] traduction partielle` / `unsupported opcode` line** in any of the three logs [L]. The number of pixel shaders created is **not logged** (unknown).
- **Pixel shaders** are 32 fixed texts in two pointer tables (`0x68f7ac` ×14 and `0x68ffb8` ×16 + the tail entries, 32 pointers in total, found by scanning the data sections), plus the dependent-read shader `0x6b1278` (`FUN_00427190`, `0x4271a8`, the only direct code reference to a pixel-shader text; the other 32 are reached through the tables); created at `0x4271f9`, `0x45f70d`, `0x461f7d`, deleted at `0x45f6a6`, `0x461f16`. All are `ps.1.1`.
- Total texts found: **33 pixel shaders, 9 whole vertex shaders, 35 headerless vertex fragments** (42 strings starting with `vs.1.1`/`ps.1.1` plus the fragments at `0x6b49c0…0x6b6a88`; the fragment list is the set referenced by the pointer tables, a stray fragment in an unreferenced table would be missed). Every text is dumped in `build/j4-scratch/shaders/` and `build/j4-scratch/vsfrag.json`.

### 4.2 Vertex fragment catalogue (what the composers glue together) [S]

| fragment (VA) | purpose (reading of the text) |
|---|---|
| `0x6b4958` / `0x6b5758` | header A: `m4x3 r2,v0,cW; m3x3 r3,v3,cW; mov r2.w,c0.x; m4x4 r11,r2,c2; mov oPos,r11; mov oT0.xy,v4` (world c8 or c48) |
| `0x6b49c0`, `0x6b4a08`, `0x6b5a28`, `0x6b5a70` | linear fog (`rcp/mad/min/max oFog`), `…a08/a70` variants multiply by a second distance term (height/range fog) |
| `0x6b4ae8`, `0x6b4ba0`, `0x6b4cc8`, `0x6b4e78` | 1/2/3/4 point lights, family A (`sub r4,r2,c16..; dp3; rsq; m3x3 oT3,r4,c80; …`) — **emits `oT3` = light-to-vertex vector rotated by c80..82** |
| `0x6b5090`, `0x6b5170` | spot-light variants A (c32/33 positions, c36 axis) |
| `0x6b5b48`, `0x6b5c10`, `0x6b5d48`, `0x6b5ef0`, `0x6b6108`, `0x6b6218`, `0x6b63d0`, `0x6b6628`, `0x6b6908` | families B (1–4 lights c16.., spot c32.., last one `m3x3 oT3,r4,c93`, `mul oD1.rgb,c7,r8.x`) |
| `0x6b4aac`, `0x6b5b14` | hemispheric/ambient (`m3x3 r9,r3,c72`, `m3x4 r9,-r3,c8; m4x3 r0,r9,c12`) |
| `0x6b5304`, `0x6b5338`, `0x6b537c`, `0x6b69dc`, `0x6b69ec` | colour combine into `oD0`/`oD1` (`min oD0.rgb,r0,c0.x`, `mul r0,r0,c7; add r0,r0,c6`) |
| `0x6b5398` | per-vertex env/specular coordinates: `m3x3 r5,r4,c60; … mov oT1.xy,r5; max oD0.a,…` |
| `0x6b544c`, `0x6b550c`, `0x6b552c`, `0x6b5544` | texcoord generation: `mad oT0.xy,v4.xy,c58.z,c58.xy` etc. |
| `0x6b5470` | **box-projected UV for stage 2** (`m4x3 r1,v0,c64; max r4,v3,-v3; sge r5.xyzw,r4.xxyy,r4.yzxz; …; mad oT2.xy,…`) — the only `sge` |
| `0x6b6a2c` | `m3x2 r4.xy,r3,c28; add oT1.xy,r4.xy,c28.ww` — the only `m3x2` |
| `0x6b6a58`, `0x6b6a88` | second UV set + `oD1.a` fade |
| whole VS `0x6b57b8` / `0x6b6b70` | **4-bone skinning**: `mul r1,v2.zyxw,c0.w; dp3 r0.w,v1.xyz,c0.x; …; mov a0.x,r1.x; m4x3 r2,v0,c[a0.x+48]; m3x3 r3,v3,c[a0.x+48]; … ×4; dp3/rsq/mul renormalise` (palette base c48; `b6b70` omits the normal) |

### 4.3 Instruction census

**Vertex shader texts (44 incl. fragments):** `mov mul dp3 add mad rsq sub min max m4x3 m3x3 m4x4 rcp dp4 sge m3x4 m3x2`; destination registers `rN`, `oPos`, `oT0..oT3`, `oD0`, `oD1`, `oFog`, `a0`; write masks `.x .y .z .w .xy .xz .zw .yw .xyz .xyzw .rgb .a`; source swizzles `.x .y .z .w .xy .xz .yw .zw .xyz .xyzw .zyxw .xxzz .xxxz .xxxy .xxyw .xxzw .yyww .xxyy .yzxz …`; **negated sources** (25), **relative addressing `c[a0.x+48]`** (12), constants c0–c2, c6–c8, c12, c16–c48, c52–c54, c56–c58, c60, c63, c64, c68, c70–c72, c76, c79–c80, **c93**; temps r0–r11. No `lit`, `dst`, `expp`, `logp`, `frc`, `mova`, `slt`, `def`.

**Pixel shader texts (33):** `tex mov mul add mad lrp cnd def texreg2ar texreg2gb`; modifiers `_x2 ×17`, `_x4 ×4`, `_d2 ×5`, `_sat ×5`; destination masks `.rgb`, `.a`; source `.a` replicate (27), source negate (14), **co-issue `+` (26)**; writes to `tN` registers (e.g. `mul_x2 t3.rgb,t3,v1`); reads `v0`, `v1`, `c0..c3`; texture stages t0–t3.

### 4.4 Support by `shader_translate.c` (**[C] static reading; the translator was not run on SH3 tokens**)

| item | translator | verdict |
|---|---|---|
| VS: `mov add sub mul mad rcp rsq dp3 dp4 min max sge frc` + `m4x4 m4x3 m3x4 m3x3 m3x2` | cases 1-24 | **supported** |
| VS: `lit dst expp logp slt` | `slt`, `expp`, `logp` supported, `lit`/`dst` → `err 2` | not used by SH3 |
| VS: relative `c[a0.x+N]`, `mov a0.x` | per-`mov a0` int `A0_n` (works around a shark limitation) | **supported**; used by both skinned VS (4 `mov a0.x` each) — performance on GXM dynamic uniform indexing **unknown** |
| VS: write masks incl. non-contiguous `.xz .zw .yw`, swizzles `.zyxw`, negate | `mask()`/`swz()`/`neg` | supported |
| VS: `oFog`, `oD0/1`, `oT0..3`, `oPos` | emitted; D3D z remap `z*2-w`; colours clamped | supported (only `oT0..oT3` reach the fragment stage) |
| VS: `m3x3`/`m4x3`/`m3x4` write **w = 0.0** | all four rows built, rows beyond `rows` set to `0.0`; D3D leaves the untouched components unchanged | **unknown / cosmetic**: fragments such as `m3x3 oT3,…` and `m4x3 r1,v0,c64` never read the unwritten w, but `m4x3 r0,r9,c12` (`0x6b5b14`) feeds `add`/`mad` on `r0` whose w is accumulated later — verify on composed text |
| VS: D3DCOLOR inputs (`v5` diffuse, `v2` bone indices, `v1` in the standalone VS) | `bgra` mask from the declaration, `aV%d.zyxw`; attribute uploaded as normalised `GL_UNSIGNED_BYTE` only for type 4 (`draw.c:1183-1185`) | supported — the translator reproduces D3D's RGBA delivery, and the skinning VS then re-applies `.zyxw` to get the byte order back (`mul r1,v2.zyxw,c0.w`) |
| PS: `tex tN` | `texture2D(sN, vTN.xy)` | supported for N ≤ 3 |
| PS: `tex tN` on a **cube texture** (3-component lookup) | not representable | **GAP G1** — see below |
| PS: `texreg2ar/gb` | cases 69/70 | supported |
| PS: `mov add sub mad mul dp3 dp4 lrp cnd cmp def` | cases 1-9, 18, 80, 81, 88 | **supported** |
| PS: `_x2 _x4 _d2 _sat` | `shift==1/2/15`, `sat` | supported; `_x8 _d4` not handled — not used |
| PS: source modifier negate; `.a` replicate | `mod==1`, `.wwww` | supported |
| PS: co-issue `+` | the flag is ignored and instructions run in order | **equivalent here** — every co-issued pair writes disjoint channels (`.rgb` then `.a`) |
| PS: write to `tN`, registers `r0 r1`, `t0..t3` | declared and used | supported |
| PS constants | `def cN` → `pdN`, `pc[8]` uniform | supported |
| PS: count | `pss[256]` handles | 33 texts ⇒ fine |
| VS: count/cache | `vss[1024]`, handles recycled via `DeleteVertexShader` (2 sites) | ≥100 live in the intro; real peak **unknown** |

Pixel-shader catalogue (per text; address in `.rdata`):

| # | VA | lines | texture stages | constants / regs read | notable features |
|---|---|---|---|---|---|
| 0 | `0x68f300` | 2 | t0 | - |  |
| 1 | `0x68f31c` | 3 | t0 | -, v0 | co-issue |
| 2 | `0x68f350` | 6 | t0, t3 | -, v0, v1 | _sat; _x2; reads v1; co-issue; **t3 = cube sample?** |
| 3 | `0x68f3b0` | 10 | t0, t1, t3 | -, v0, v1 | _sat; _x2; reads v1; co-issue; **t3 = cube sample?** |
| 4 | `0x68f448` | 5 | t2 | c3 | co-issue |
| 5 | `0x68f498` | 8 | t0, t2 | c0, c2, c3 | def c0; _x2; co-issue; negated src |
| 6 | `0x68f528` | 10 | t0, t2, t3 | c2, c3, v0, v1 | _sat; _d2; _x2; reads v1; co-issue; negated src; **t3 = cube sample?** |
| 7 | `0x68f5c8` | 6 | t0, t2 | c2, c3 | _x2; co-issue; negated src |
| 8 | `0x68f630` | 9 | t0, t2, t3 | c2, c3, v0, v1 | _sat; _d2; _x2; reads v1; co-issue; negated src; **t3 = cube sample?** |
| 9 | `0x68f6c0` | 4 | t0 | c2, c3 | _x2; _x4; co-issue |
| 10 | `0x68f708` | 2 | t0 | c2 | _x2 |
| 11 | `0x68f728` | 4 | t0 | c2, c3 | _x2; co-issue |
| 12 | `0x68f770` | 2 | t0 | c2 | _x2 |
| 13 | `0x68f790` | 2 | t0 | - |  |
| 14 | `0x68f8c8` | 3 | t0 | -, v0 | co-issue |
| 15 | `0x68f8f8` | 6 | t0, t3 | -, v0, v1 | _sat; _x2; reads v1; co-issue; **t3 = cube sample?** |
| 16 | `0x68f958` | 6 | t0, t2 | -, v0, v1 | reads v1; co-issue; negated src |
| 17 | `0x68f9b8` | 6 | t0, t2 | -, v0, v1 | lrp; reads v1; co-issue |
| 18 | `0x68fa18` | 6 | t0, t2 | -, v0, v1 | reads v1; co-issue; negated src |
| 19 | `0x68fa78` | 6 | t0, t2 | -, v0, v1 | lrp; reads v1; co-issue |
| 20 | `0x68fad8` | 6 | t0, t2 | c2, c3, v0 | _x4; co-issue; negated src |
| 21 | `0x68fb38` | 6 | t0, t2 | c2, c3, v0 | _x4; co-issue; negated src |
| 22 | `0x68fb98` | 6 | t0, t2 | c2, c3, v0 | _d2; _x2; co-issue; negated src |
| 23 | `0x68fc00` | 6 | t0, t2 | c2, c3, v0 | _d2; _x2; co-issue; negated src |
| 24 | `0x68fc68` | 5 | t0 | c3, v0 | _x2; co-issue |
| 25 | `0x68fcb8` | 5 | t0, t1 | c1, v0 | co-issue |
| 26 | `0x68fd00` | 8 | t0, t1, t2 | c1, v0, v1 | reads v1; co-issue; negated src |
| 27 | `0x68fd78` | 8 | t0, t1, t2 | c1, v0, v1 | lrp; reads v1; co-issue |
| 28 | `0x68fdf0` | 8 | t0, t1, t2 | c1, c2, c3, v0 | _x4; co-issue; negated src |
| 29 | `0x68fe70` | 7 | t0, t1, t2 | c1, c2, v0 | _d2; _x2; co-issue; negated src |
| 30 | `0x68fedc` | 4 | t0 | c0, c1, c2 | cnd |
| 31 | `0x68ff1c` | 2 | t0 | - |  |
| 32 | `0x6b1278` | 4 | t0, t1, t2 | - | dependent read (texreg2ar/gb) |

**Inference [I]**: the five pixel shaders that sample `t3` (#2, #3, #6, #8, #15) are the dynamic-light shaders; their coordinate set comes from `oT3` = a **3-component vector** produced by `m3x3 oT3,r4,c80`/`c93`, and the game creates a **128² cube map** (`0x445e94`, filled at `0x445e40`, `LockRect` at `0x445ebf`), whose `D3DCAPS8.TextureCaps & CUBEMAP` it requires. In ps 1.1, `tex t3` on a cube-map stage is a cube lookup. Together these make the "light cookie" a cube-map attenuation/gobo lookup. Runtime proof of the binding: the shim logs `SetTexture: unsupported texture type (cube/volume?)` once in `ng1` and `ng2` [L] — the game **does** bind the cube in the intro, and the shim stores `NULL` for that stage [C: `device.c` `M(SetTexture)`].

## 5. (d) Gap table — shim `port/src/d3d8/` vs what SH3 does

Severity: **blocking** = wrong or missing output expected in ordinary scenes; **cosmetic** = visible difference only; **unknown** = needs a run or a closer read. "Vita" column is about vitaGL/GXM and is inferred.

| # | gap | evidence | severity | note |
|---|---|---|---|---|
| G1 | **Cube texture cannot be bound or sampled.** `SetTexture` of a cube stores `NULL`; programs only declare `sampler2D sN` and `ps_translate` emits `texture2D` for every `tex tN`. | game: `CreateCubeTexture` `0x445e94`, caps probe requires CUBEMAP, 5/33 PS read `t3`, VS fragments write `oT3` as vec3 [S]; shim: `device.c` `M(SetTexture)`, `resource.c` cube comment "rendering from them is not implemented" [C]; log "unsupported texture type" [L ng2:5270] | **blocking** (dynamic-light look, flashlight/lamps) | vitaGL archive exports `gpu_alloc_cube_texture`, `gpu_alloc_compressed_cube_texture`, `GL_SAMPLER_CUBE`, Cg `samplerCUBE`/`textureCube` [I from `libvitaGL.a`/`vitaGL.h`] — support exists but is **untested** here |
| G2 | **State blocks have the wrong semantics.** `EndStateBlock`/`CreateStateBlock` snapshot the *whole* current state (all 256 rs, all tss, VS, PS, all 96 VS constants); `ApplyStateBlock` restores *all* of it. Real D3D8 stores and re-applies only the states recorded between Begin and End. | game: 7 blocks created at init (`0x413590`), applied through `0x413570` from ~66 sites in ~20 render-setup functions [S]; shim `device.c:246-290` [C] (`recording` flag is set but never used to filter) | **blocking until checked** (every Apply clobbers VS/PS/constants/blend/cull set after block creation) | cheap fix: record the set of touched indices while `recording`; run a test where a shader is set before an Apply |
| G3 | Checked and **not a gap**: SH3's `COLOROP 10 SUBTRACT`, `7 ADD`, `0x23 TFACTOR|ALPHAREPLICATE`, `RESULTARG=TEMP`, `FILLMODE`/`LIGHTING`/`SWVP` writes | TSS/RS tables [S]; shim `op_expr`/`arg_expr` handle ops 2-26, `a & 0x20`, `tmp`; the other states are no-ops in a shader-only pipeline [C] | none | listed so that the check is on record |
| G4 | `ADDRESSU/V = BORDER (4)` mapped to `CLAMP_TO_EDGE`; `BORDERCOLOR` ignored | TSS rows 13/14/15 [S]; `apply_sampler` [C] | cosmetic | only 2 sites; border colour is 0 |
| G5 | `SHADEMODE` (rs 9, values 1 FLAT ×2, 2 GOURAUD ×3) not applied | RS table [S]; `apply_states` has no rs[9] [C] | cosmetic / unknown | which draws use FLAT unknown; GXM flat varying support unverified |
| G7 | Pixel shader `v1` (specular) is read by 11/33 shaders as a colour **and** as an alpha factor (`v1.a`); VS writes `oD1.rgb`/`oD1.a` separately | PS #16-19, #26-27 [S]; `vD1` varying exists [C] | unknown | `vD1` clamp [0,1] — matches D3D |
| G8 | **VS partial-write semantics** (`m3x3`/`m4x3` zero the unwritten w; D3D keeps the old value) | §4.4 | unknown / cosmetic | needs one differential run |
| G9 | **Texture storage policy on Vita**: A8R8G8B8 pages > 256 px with alpha are stored 4444 (opaque 565) after ordered dithering; only textures ≤ 256 px keep RGBA8 (`SH4_ALPHA32_MAX`, `tfmt`). The atlas pages are 512² and 512×1024 and are `A8R8G8B8` in D3D. | `resource.c` `tfmt` selection [C]; log: only `lv=0` uploads, 512² ×36, 512×1024 ×2 [L ng2] | cosmetic (quality) / unknown (alpha-test edges: `ALPHAREF` 0x3b / 0x80 on 4-bit alpha) | Valentin allowed reduced texture quality (HANDOFF 2026-11-11) |
| G10 | **Mip handling**: Vita path skips levels above `SH4_TEXMAX` (256); all uploads observed are single-level, so this is a no-op for SH3 so far | [C], [L] | none now | recheck if room `.tex` pages arrive with mip chains |
| G11 | **Programmable-pipeline count**: ≥100 distinct VS created in the intro, shader programs are linked lazily (`get_prog`), each VS×PS pair is a separate GL program; only the first 3 programs are logged | `get_prog` [C]; `CreateVertexShader #100` [L] | **unknown, high risk on Vita** (ShaccCg runtime compile hitches) | the SH4 port has a shader bank/prewarm (`shader_bank.c`, `shader_ready.c`, `sh4prep`) — SH3 needs its own offline combination enumeration (composer selector spaces are bounded) |
| G12 | Surface read-back (`LockRect READONLY` on RT/back-buffer surfaces, 3 game sites + `CopyRects` ×2) goes through `surf_readback` / pending copy | `0x41e12a`, `0x49fe28`, `0x4a027e` [S]; `resource.c` [C] | unknown (perf) | what triggers them (screenshot, thumbnails, post effect?) not traced |
| G13 | **128×128 texture uploaded ~2×/frame** (DXT5 and A8R8G8B8) | 76,236 upload lines in `ng2` [L]; creation sites `0x427b34` (DXT5 `mem×mem`), table `0x6b1434` [S] | unknown (perf; DXT5 is software-decoded on Vita by default, `SH4_HWDXT` off) | not explained; might be the cube faces or a per-frame procedural map |
| G14 | `CheckDeviceFormat` answers OK to ~40 formats (bump, luminance, FOURCC YUY2/UYVY, D16…) | log [L] | unknown | only needed if the game chooses them; the 1.5-room evidence shows 21/22/DXT5 only |
| G15 | Back buffer: game asks 640×480 A8R8G8B8 D24S8; shim provides its own resolution/colour target and the depth buffer shared across targets (`depth_rb`) | `[rt] color 640x480 fmt=22`, `[rt] depth 640x480` [L], [C] | none for correctness | Vita screen 960×544; scaling already part of the SH4 pipeline |
| G16 | `GetAvailableTextureMem` returns 128 MiB constant (`0x5e2755`) | [C],[S] | unknown | the game may budget its texture cache from it; value on Vita should reflect vitaGL budget |
| G17 | D3DX-zone clusters (`0x63bb56`, `0x63e0d6`, `0x63c500`, `0x63ca24`, …) call `SetViewport`, `SetRenderState 128-167`, vertex-blend, state blocks, `DrawIndexedPrimitive` with FVF `0x142` | §1 [S] | unknown (probably dead) | no direct call path; verify with a one-run trace of the host build |
| G18 | `rt_unimpl` / GDI is outside this document; `LoadLibraryA("d3d8.dll") -> NULL` repeated ~140 times is harmless (PSGP probe) | [L] | n/a | |

## 6. (e) Plan for the Vita translator work

Ordered by value and dependency; effort is ideal focused days for one person with the Linux/llvmpipe host build and Vita3K+ as test bench. **None of these have been done; estimates are guesses.**

1. **J4.0 — Offline harness for the translators (1 d).** Build `shader_translate.c` with `SH4_TRANSLATOR_STANDALONE` (already supported) plus a 150-line `vs.1.1`/`ps.1.1` text→token assembler (only the instruction set of §4.3 is needed), and feed it all 33 PS texts, 8 whole VS and the composer outputs for every selector combination. Goal: zero `err`, compile-check the GLSL (glslang or the vitaGL/ShaccCg path in Docker). *Risk*: ShaccCg only runs on a Vita (no x86 build in the SDK I could find on this machine) — then compile-check is by loading in Vita3K+, which proves nothing about the real GPU.
2. **J4.1 — Enumerate the VS combination space (1–2 d).** The composers' selector ranges are bounded by the tables at `0x6b5638…`, `0x6b56a0…`, `0x6b6e48…`; enumerate them from the exe (or log every `CreateVertexShader` source in the host run, one extra `SH3_TRACE_SHADER` line) to size the shader bank and decide whether runtime compile is acceptable on Vita. *Risk*: G11, the largest unknown for frame-time stability.
3. **J4.2 — State blocks (0.5 d).** Fix G2 (record the touched states), then re-run the intro and compare screenshots against the existing `ng1/ng2` ones (they should change only where blocks matter).
4. **J4.3 — Cube-map sampling (2–3 d).** (a) `SetTexture` accepts `K_CUBE`; (b) upload the six `Texture` faces already allocated by `cube_create` into a `GL_TEXTURE_CUBE_MAP` (RGBA8 128² ⇒ 6×64 KiB, or 565 ⇒ 6×32 KiB); (c) `ps_translate` needs the sampler kind per stage: add `samplerCube sN` + `textureCube(sN, vTN.xyz)` and key the program cache on "stage N is cube"; the VS translator already forwards `oT3`'s xyz in `vT3` (4-vector varying). (d) Interim fallback: bind a 1×1 white 2D texture to hide the artefact. *Risks*: vitaGL cube support is unverified on this vitaGL revision (`2bdbe89…`); GXM cube textures need power-of-two edge (128 ok) and are not mip-chained in this path.
5. **J4.4 — Skinned VS check (1 d).** The two skinned shaders use 4 relative constant loads each; confirm on the host build that `A0_n` ints give identical output to a straight CPU reference of the same formula, then measure on Vita3K. Mitigation if GXM indexing is slow: unroll with a `vec4 c[96]` → `texture`-fetched palette or CPU skinning for far characters. *Risk*: dynamic indexing cost, vertex uniform buffer limits.
6. **J4.5 — Texture storage (2–4 d).** Decide per page class: room atlas pages (512², 512×1024, `fmt` 0x20/0x10 sources), UI pages (`pic.arc`), movie texture. The current policy stores >256 px alpha textures as 4444, which collapses alpha-tested foliage. Options: keep RGBA8 for pages that are alpha-tested, 565 for the rest, DXT recompression for opaque pages (hardware DXT is lab-only in the SH4 code). *Budget*: `GB.tex` = 8 MiB RGBA8 per room family ⇒ 4 MiB at 16 bit; add inner pages, TR pages, characters (`.mdl` 512×1024 atlas = 2 MiB RGBA8 / 1 MiB 16-bit each), RTs (512² ×4, 256² ×3 ≈ 5 MiB RGBA8). *Risk*: texture memory is risk n°1 of the study; the guest budget for SH3 is still unmeasured.
7. **J4.6 — Remaining cosmetic states (1 d)**: border addressing (G4), flat shading (G5), `TEXTUREFACTOR` as a uniform array; verify `BLENDOP` SUBTRACT/REVSUBTRACT and `COLORWRITEENABLE=8` on vitaGL (alpha-only write mask).
8. **J4.7 — Scene comparison (open-ended).** The study's criterion "set of scenes compared image by image" needs a **reference renderer**. There is no D3D8 oracle on this machine (no Wine, game is Windows-only). Needs either screenshots taken on Valentin's Windows machine from the unmodified exe, or accept visual review only. **Decision for Valentin.**

Critical path to a first lit room: J4.2 → J4.1 → J4.3; J4.4 and J4.5 can run alongside. A rough total of 8–14 working days to reach "first room, lit, plausible on Vita3K+" — a guess, not a measurement.

### Vita / vitaGL risk list [I unless noted]

- **Cube textures**: `gpu_alloc_cube_texture` exists in the installed archive; behaviour with `glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X…)` and with FBO/mip chains is untested.
- **Dynamic uniform indexing** `c[A0_n+48]` in a vertex shader (skinning).
- **Varyings**: the shim exports `vD0 vD1 vT0..vT3 vFog` (7 vec4); SH3 needs all of them (oT3, oD1).
- **Runtime shader compile**: ≥100 VS × PS combinations (G11).
- **Render targets**: up to 512×1024 colour + D24S8 shared depth; the shim already pools FBOs by size (up to 32 slots); the `1x1` FBO (slot 7) exists in the host log.
- **Stencil with multi-bit values** (masks 0xe0/0x1f) requires the 8-bit stencil buffer retained (`STORE_DEPTH_STENCIL=1`, the Docker image already builds that) [C].
- **Texture memory**: see J4.5; Vita's CDRAM/LPDDR split and the 4096-per-side limit are fine for 512×1024.
- **NPOT probe textures** (62², managed + RT): created and released at once (`0x40281c…0x402827`); the shim allocates GL storage lazily, so the probes should not reach the GPU — **unverified**.
- **Main-thread cost of the 128² per-frame uploads** (G13).

## 7. Open questions (not answered by this pass)

1. What fills the 128×128 DXT5/A8R8G8B8 texture twice per frame?
2. Which draws use `SHADEMODE FLAT`, `COLORWRITEENABLE 8`, `BLENDOP SUBTRACT`? (value-only, not call-path-traced.)
3. The pixel-shader creation count and the exact selector spaces of the two VS composers.
4. Whether any code path reaches the D3DX interface classes at `0x63b7a6…`.
5. Whether `ApplyStateBlock` clobbering (G2) is visible in the intro screenshots already (they look right in `ng2/shot_*.png`? — not inspected; the brief allows reading only the logs).
6. Source of truth for "scenes compared": reference captures (J4.7).

## 8. Reproducibility

- Snapshot: `build/j4-scratch/gen-sh3-snapshot` (copied at session start; **not used for the numbers** — the census was done on the exe; the snapshot is kept so that function names can be cross-checked without racing the regenerating session).
- Scripts: `build/j4-scratch/tools/{scan,calls,trace,args,args2,final,states,names}.py`; raw outputs `insns.pkl`, `vcalls.pkl`, `trace.pkl`, `recs.pkl`, `recs2.pkl`, `devrows.pkl`, `shaders.json`, `vsfrag.json`, `t_dev.md`, `t_rs.md`, `t_tss.md`, `t_ps.md`.
- Everything in `build/` is gitignored; this document is the only artefact meant for `sh3-port/docs/`.

## 9. Follow-up (2026-11-13): G13 diagnosis and G9 sizing

Evidence tags as in the header. **[O]** = runtime oracle: the `sh3-nx` Linux build (same exe, its own recompiler and D3D8 layer, **not** this shim, not Vita) driven under gdb with Present-wrapper (`sub_00419920`) entries as the frame counter; script and log in `build/j4-scratch/g13/` (`g13.py`, `run3.log`). The oracle was run for 6,000 frames of the title/attract sequence only.

### 9.1 G13 — what the 128×128 texture is

**Verdict: it is real game behaviour (the guest regenerates random noise into the texture every frame), not a shim artefact.** The shim performs about one upload per regenerated texture, which is what the game asks for.

What it is [S]:

- `FUN_00434ee0` (called once, from `FUN_00436910`) creates a set of **procedural "layer" textures**: nine `128×128 A8R8G8B8` (`0x427ac0`, CreateTexture usage 0, managed), one **`128×128 DXT5`** (`0x427b10`, `CreateTexture(…, 0x35545844, MANAGED)`), one `256×256 A8R8G8B8`. Each helper also `malloc`s a system staging buffer (`w*h*4`, or `w*h` for DXT5) and stores `{texture*, buffer*}` in a record table at `0x72daa8…0x72df68` (stride 0x60).
- Per frame, `FUN_00685780` (the main frame function) calls **`FUN_00437190`**, which loops over 5 layer records (stride 0x118 from `0x72dab0`) and calls `FUN_00436dd0` for each enabled layer, then a second pass for the layer at `0x72da58`. For a layer whose descriptor has flags `0x1300000` it runs the **generator** through `FUN_00436840`: `FUN_00436180` (DXT5 blocks, descriptor type `0xaa/0xab`), `FUN_00436430` / `FUN_00436310` / `FUN_00435e80` (other formats). Each generator does `LockRect(0, &lr, rect, 0)` (`0x4361d5`, `0x43646d`, …), fills the whole level, `UnlockRect` — hence one dirty upload per generation in the shim (`resource.c` `tex UnlockRect` sets the dirty bit).
- **Content**: `FUN_00436180` fills the 16-byte DXT5 blocks from a Numerical-Recipes LCG (`x = x*0x19660d + 0x3c6ef35f`, `0x436232`). Alpha endpoints are the constants `0x01` and `0x10` (`mov word [edx-4], 0x1001`, `0x4362ae`), alpha indices random (masks `0x9249/0x2492/0x4924`), colour endpoints `0x0000` and `0x18C3` (type `0xaa`) or `0x8410` (other types), colour indices random with only the 0/1 selectors (mask `0x5555`). So each texel is one of two fixed dark/grey colours with a random alpha of at most 16/255. [I] This is a **full-screen film-grain / noise overlay** (sub-7 % alpha speckle regenerated every frame); the draw that consumes it was not traced.

Rates:

- [O] in 6,000 title/attract frames: `FUN_00437190` ran 5,999 times (once per frame, as expected); the DXT5 generator on descriptor `0x72dc28` (type `0xaa`) 2,752 times (0.46 per frame), the type-4 generator on `0x72dbc8` 869 times (0.14 per frame), one more layer (`0x72dab0`, type 7) 45 times.
- [L] shim host run `build/j2c/walk2` (in-game, frames 7,100→8,000): +900 DXT5 and +900 A8R8G8B8 128² uploads, i.e. **exactly 1.0 + 1.0 per frame**; over the whole run 35,278 128² uploads (18,172 DXT5, 17,377 A8R8G8B8 at the time of counting) . The title/attract rate (0.6) and the in-game rate (2.0) differ because the number of enabled layers differs by scene [I]; nothing indicates redundant lock/unlock without data change — the generators always rewrite every block.

Cost and options (all for Vita, **not measured**):

- Each DXT5 upload goes through the shim's software path (`hw_dxt` is off on Vita, lab-only because of the transfer-swizzle corruption), i.e. DXT5 decode of 16,384 texels + 4444/dither conversion, and each A8R8G8B8 one the same conversion, ≈ 32 K texels per frame plus two `glTexImage2D` on the consumer thread.
- Cheap fixes, in order of safety: (1) special-case "dynamic" textures (re-locked on consecutive frames) — keep them RGBA8 (skip the 4444 dither; 128² = 64 KiB) and decode DXT5 with a tight NEON/straight-line path; (2) upload the noise **every second frame** (grain at 30 Hz is hard to tell from 60 Hz; changes the look slightly, needs Valentin's approval); (3) enable the hardware-DXT upload for this one 16 KiB texture once the swizzle issue is solved; (4) drop the layer when the frame budget is exceeded. None of these needs a guest change.

### 9.2 G9 — texture memory per room area

Method: offline, `tools/j3_arc.py` (`load_all`, `walk_subs`) on `sh3-nx/game/data` (same data as the oracle): for every one of the **287 rooms** in the 20 `bg*` archives, GB pages (`<area>GB.tex`) + the room's inner container (offset at `.map` +0x10) are summed; the player is `pcchr/pl/chhaa.mdl` (texture container at `+0xc1b50`: 512² + 512² + 512×1024 + 256², all fmt 0x18 = **4.25 MiB RGBA8**). Pages: 1,831 of 512², 19 of 1024×512, 12 of 512×1024; source formats 0x20 (1,951 pages) and 0x10 (38 pages, 16-bit A1R5G5B5). **Assumption [I]: GB and the room's inner pages are resident together** (the `.map` page rule selects among GB, TR and inner objects per section); TR pages are not counted (only `cc` rooms have a separate TR, see J3 notes), nor are enemies, weapons, the UI atlases (`pic.arc`) or the movie texture.

Per-area (RGBA8): GB = 8.0 MiB for most areas (`cc` 5, `hp`/`th` 3, `tg` 11), inner pages 3.6–12 MiB average per room.

| scope | RGBA8 (×4 B/px) | RGBA4 / RGB565 (×2) | DXT5 (×1) | DXT1 (×0.5) |
|---|---|---|---|---|
| room (GB + inner), min / median / p90 / max | 6.0 / 15.0 / 15.0 / **20.0** (`tgf1`) MiB | 3.0 / 7.5 / 7.5 / 10.0 | 1.5 / 3.75 / 3.75 / 5.0 | 0.75 / 1.9 / 1.9 / 2.5 |
| + Heather (`chhaa.mdl`, 4.25 MiB RGBA8) median / worst | **19.2 / 24.2** | 9.6 / 12.1 | 4.8 / 6.1 | 2.4 / 3.0 |
| + render targets seen in the host log (512² ×4, 256² ×3, 128²/64² small; ≈ 6.8 MiB RGBA8) | ≈ 26 / 31 | ≈ 13 / 15 | ≈ 7 / 9 | ≈ 5 / 6 |

Budget implied by the build (`port/CMakeLists.txt`, `port/src/vita/main.c`) [C]: `SH4_GUEST_MB=200` (the guest block actually obtained is `min(200, user free − SH4_VGL_RAM_MB − SH4_SPARE_MB)`), `SH4_VGL_RAM_MB=32` (vitaGL's RAM pool: `vglInitExtended` leaves `size_user − 32 MiB` free), `SH4_SPARE_MB=12`, host heap 24 MiB. A Vita3K+ boot of the SH3 VPK by another session logged (`build/vita3k-scratch/pref/ux0/data/sh3/sh4log.txt`, **emulator-reported, not console**): startup `user free 237,568 KB, cdram 114,688 KB, phycont 26,624 KB`; `guest memory: 188 MB obtained`; after `vglInitExtended` `cdram 0 KB, phycont 0 KB` (vitaGL takes all of CDRAM + phycont). From the HANDOFF, image + BSS need ~114 MB of the guest block, so the guest heap is ≈ 74 MB (188 MB case) to 86 MB (200 MB case).

Which formats fit [I]:

- **GPU side (vitaGL CDRAM ≈ 112 MiB + phycont ≈ 26 MiB + the 32 MiB RAM pool)**: even RGBA8 fits — a typical room with Heather and the render targets is ≈ 26 MiB, the worst ≈ 31 MiB, and ≈ 55–60 MiB if two rooms are momentarily resident across a transition. These are far below 112 MiB, but they share the pool with vitaGL's vertex/index rings, framebuffers and driver memory, whose sizes for SH3 are unmeasured, and the real console's CDRAM figure is unmeasured (only the emulator number exists). So **the GPU-side size does not force 16-bit storage; the current "alpha textures above 256 px → 4444" policy is a quality loss that the budget does not require** (to be confirmed with `SH4_VRAMLOG=1` on a device). Keep RGBA8 for the 512² pages with alpha tests (`ALPHAREF` 0x3b / 0x80), 565 for opaque ones, DXT1/5 only where banding is acceptable.
- **Guest side is the binding constraint**: the shim keeps a **system-memory copy of every locked managed texture at the D3D format until the texture is released** (`resource.c` `surf_mem` allocates on `LockRect`; `g_mem_tex` is only decremented on release, line 884) [C]. For SH3 that is the full RGBA8 size regardless of the GPU format: **≈ 15–20 MiB per room + 4.25 MiB for Heather = 19–24 MiB, i.e. 26–32 % of a 74 MB guest heap (22–28 % of 86 MB)**, on top of the ~9 MiB `.map` buffers the game reads per room. SH3 never reads a texture back (all texture `LockRect` flags are 0, no READONLY on textures [S]), except the per-frame noise textures of §9.1 and the movie texture. A shim change that **drops the guest copy after the upload for textures that are not re-locked within a few frames** would free most of that (≈ 19–24 MiB) at no GPU cost; storing DXT-recompressed copies would cut it to ¼. This is a design suggestion, not implemented or measured.

Updates to the gap table: **G13** → explained (real, per-frame procedural noise; keep, make the upload cheap); **G9** → GPU-side quality reduction is not required by size; the guest-side system copy is the cost to attack.
