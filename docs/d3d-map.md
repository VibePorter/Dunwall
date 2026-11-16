# 🗺️ Dishonored's XDK Direct3D

The functions of the game's statically linked Direct3D (XDK 2.0.21076, program 0.0.3.17) that Renut hooks or calls,
and the device fields the port relies on. The table is `tools/d3d/dishonored_d3d_funcs.toml`, which
`tools/dishonored_renut_hooks.py` turns into the start-up witness header (`app/src/renut_engine/gpu/d3d/me2_hook_map.h`).

Each function was matched against Mass Effect 2's (Lazarus, an older XDK) by instruction shape and by its callers
(`tools/d3d/match_funcs.py`), then checked by hand; the note gives ME2's address, the match score and the evidence.

## Device

The device layout is the one of Sonic Adventure 2 / Metal Gear Rising (the same XDK), not Mass Effect 2's.

| | |
| :--- | :--- |
| Device pointer (`GDevice`) | `0x834CFE60` |
| `VdGlobalDevice` slot | `0x820007BC` |
| Device size (`CreateDevice`) | 24,704 bytes; side buffer +17136; thread ID +11016 |
| Present interval | +14000 |
| Displayed swaps | +16976 |
| Gamma ramp | +15408 |
| XDK Direct3D code | `0x82AF0000` - `0x82B10000` |

Shaders carry literal constants in their own GPU memory, loaded by `LOAD_ALU_CONSTANT` at the shader flush
(`sub_82B053F8` → `sub_82B05208`; pixel shader table +40 / base +24, vertex shader +872 / +32; entries are
`u16 register` (absolute, pixel = 256+), `u16 count` in dwords, `u32 offset`). The port copies them into the device's
constants before each draw (`dh_shader_constant_blocks`).

`D3DDevice_Resolve`'s `r8` is the destination slice or face, not the mip level.

## Functions

| Function | Address | Note |
| :--- | :---: | :--- |
| `AllocateInlineData` | `0x82B00738` | ME2 0x82A9FFC0; match 1.00 |
| `BeginRingBig` | `0x82B00140` | ME2 0x82A9F9E8; match 1.00 |
| `BlockOnFence` | `0x82B01830` | ME2 0x82AA0CA8; match 1.00 |
| `CBlocker_Check` | `0x82B05FC8` | ME2 0x82A8F9C8; match 1.00 |
| `D3DDevice_BeginIndexedVertices` | `0x82AFCC48` | ME2 0x82A9EAB8; match 0.96 |
| `D3DDevice_BeginTiling` | `0x82B0B3F8` | ME2 0x82AA4398; match 0.99 |
| `D3DDevice_BeginVertices` | `0x82AFC740` | ME2 0x82A9E5D8; match 0.86 |
| `D3DDevice_ClearF` | `0x82B099D8` | ME2 0x82AA2B30; match 0.82; BeginTiling calls it in ME2's slot |
| `D3DDevice_DrawIndexedVertices` | `0x82AFD5A0` | ME2 0x82A9F3C0; match 0.95 |
| `D3DDevice_DrawVertices` | `0x82AFD188` | ME2 0x82A9EFD0; match 0.95 |
| `D3DDevice_Resolve` | `0x82B0A120` | ME2 0x82AA31F8; match 0.67; game present 82776FB0: Sync -> Resolve -> Swap; tiling fn calls it 4x before EndTiling; li r10,8984 |
| `D3DDevice_Swap` | `0x82B03228` | ME2 0x82AA5238; match 0.66; stw 11056 (ME2 10928 +128), flags2 11069 /= 0x10; present-interval sync via helpers 82B02920/82B02868 |
| `D3DResource_Destroy` | `0x82AF7010` | ME2 0x82A8E7A0; match 0.61; first 67 ins identical shape, same multi-path BlockOnFence exits |
| `D3DVertexShader_Bind` | `0x82B05D78` | ME2 0x82AA7D78; match 0.91 |
| `DefaultViewport` | `0x82AF84F8` | ME2 0x82A9B0F0; match 1.00 |
| `DeferredWorkQueue` | `0x82B0E060` | ME2 0x82AAAD90; match 0.32; hardware init sub_82B135B8: same call slot (new call 82B06218 before it in XDK 21076) |
| `Direct3D_CreateDevice` | `0x82B063C8` | ME2 0x82A8FC18; match 1.00 |
| `EndTiling` | `0x82B0B750` | ME2 0x82AA46F0; match 1.00 |
| `HardwarePM4Snippets` | `0x82B060A0` | ME2 0x82A8FAA0; match 1.00 |
| `InitializeApiState` | `0x82B12AA0` | ME2 0x82AA82E8; match 0.70; hardware init sub_82B135B8: same call slot |
| `InitializePresentationParameters` | `0x82B12D80` | ME2 0x82AA85C8; match 0.66; hardware init sub_82B135B8: same call slot |
| `IsFencePending` | `0x82B01D28` | ME2 0x82AA10D8; match 1.00 |
| `KickOff` | `0x82B01730` | ME2 0x82AA0BA8; match 1.00 |
| `KickOffSegment` | `0x82B012F0` | ME2 0x82AA0950; match 1.00 |
| `NewSegment` | `0x82B008D8` | ME2 0x82AA0160; match 0.89 |
| `RingBufferDeviceAllocate` | `0x82B00508` | ME2 0x82A9FD90; match 1.00 |
| `RingCallback` | `0x82B01928` | ME2 0x82AA0DA0; MATCHER WRONG (was sub_82AF8360); callers: EndTiling + 1 other call it in ME2's slots; same prologue (5 saved args, rlwinm r5,8,26,31) |
| `SetRingBufferParameters` | `0x82B01DB8` | ME2 0x82AA1168; match 0.93 |
| `ShaderGprBuffers` | `0x82AFBD68` | ME2 0x82A92AC0; match 1.00 |
| `SynchronizeToPresentationInterval` | `0x82B024E0` | ME2 0x82AA4D00; match 0.97 |
| `VertexDeclaration_Init` | `0x82AFC568` | ME2 0x82A93650; match 1.00 |
| `WindowScissor` | `0x82AF86B0` | ME2 0x82A9B2A8; match 0.94 |
| `XGRegisterPixelShader` | `0x82AFC0A8` | ME2 0x82A92FB0; match 1.00 |
| `XGRegisterVertexShader` | `0x82AFC2A0` | ME2 0x82A93298; match 1.00 |
| `XenonGpu_Initialize` | `0x82B181C0` | ME2 0x82AAB0E0; match 0.93 |
