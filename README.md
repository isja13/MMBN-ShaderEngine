# Mega Man Battle Network Legacy Collection `d3d11.dll` wrapper mod
![20260413054209_1](https://github.com/user-attachments/assets/0c65650e-6e1a-4ae2-9ad0-81330d86ecc4)

Based on xzn's Mega Man X Collection wrapper//

Features:

Working beta that//

- Let's you use [slang-shaders](https://github.com/libretro/slang-shaders) with Capcom's Mega Man Battle Network Legacy Collection.

Download from [here](https://github.com/isja13/MMBN-ShaderEngine/releases).

// As this is the working beta there are things to consider. With the current use of RetroArch backend for shader processing, the text layer uses a keying shader to remove the opaque background elements when recompositing. 
```bash
// KEY PS: compile inline HLSL
{
    static const char* key_ps_src =
        "Texture2D t0 : register(t0);\n"
        "SamplerState s0 : register(s0);\n"
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "    float4 c = t0.Sample(s0, uv);\n"
        "    float4 key = t0.Sample(s0, float2(0.0, 0.0));\n"
        "    float diff = length(c.rgb - key.rgb);\n"
        "    if (diff < 0.15) discard;\n"
        "    return float4(c.rgb, 1.0);\n"
        "}\n";
```
 //Resulting in either a slight aura of the background color around text, or false positive keying out of certain text layer elements on specific shader configs. This can be tuned to taste, just raise the value of 
 "(diff < 0.15)" to get less text aura, or lower it for less false positives in battle. 

 Future implementation list :
 - Text Aura threshhold fine control.
 - Up to 3 Shader presets loadable from start, hotkey swappable.
 - Toggles for shader path & widescreen mode.
 - Custom controller and keybinds in INI.
 - Support for screen modes other than "Max"

## Building from source

Using i686-w64-mingw64-gcc (cross compiling should work too):

```bash
# Download source
git clone https://github.com/isja13/MMBN-ShaderEngine.git
cd d3d11-mmbnlc
git submodule update --init --recursive

# Create symlinks and patch files
make prep

# Build the dll
make -j$(nproc) dll
```

Some options to pass to make

```bash
# disable optimizations and prevents stripping
make o3=0 dll

# disable lto (keep -O3)
make lto=0 dll
```

## Install

Copy `dxgi.dll`, `better-filters.ini`, and the `slang-shaders\` directory to your game folders, e.g.:

- `SteamLibrary\steamapps\common\MegaMan_BattleNetwork_LegacyCollection_Vol1\exe`
- `SteamLibrary\steamapps\common\MegaMan_BattleNetwork_LegacyCollection_Vol2\exe` 

## Configuration

`better-filters.ini` contains options to configure the mod.

```ini
; Log API calls to better-filters.log,
[logging]
; enabled=true
; hotkey_toggle=VK_CONTROL+O
; hotkey_frame=VK_CONTROL+P

[graphics]
enhanced=true
interp=true
;linear=true
                               _
slang_shader_gba=slang-shaders/custom/ScaleFx+LCD.slangp
                               ^
[Favorites]
custom/ScaleFx+LCD.slangp
custom/psp-freescale.slangp
custom/Xbrz-Free+LCD3x.slangp

[Smoothing]
scalefx/scalefx.slangp
xbrz/xbrz-freescale-multipass.slangp
presets/scalefx9-aa-blur-hazy-vibrance.slangp

[CRT]
crt/crt-royale.slangp
crt/crt-lottes.slangp
crt/crt-consumer.slangp

[LCD]
handheld/lcd3x.slangp
handheld/lcd1x_psp.slangp
handheld/lcd-shader.slangp

[VHS]
vhs/VHSPro.slangp
vhs/ntsc-vcr.slangp
vhs/vhs_and_crt_godot.slangp


/////////////////////////////////////////////////
;bezel/Mega_Bezel/Presets/MBZ_0_SMOOTH-ADV.slangp
;custom/Stock.slangp
;test/format.slangp
```

If all goes well you should now be able to start the game and see the overlay on top-left of the screen showing the status of the mod.

`better-filters.ini` can be edited and have its options applied while the game is running.

## License

Source code for this mod, without its dependencies, is available under MIT. Dependencies such as `RetroArch` are released under GPL.

- `RetroArch` is needed only for `slang_shader` support.
- `SPIRV-Cross` and `glslang` are used for `slang_shader` support.
- `HLSLcc` is used for debugging.

Other dependencies are more or less required:

- `minhook` is used for intercepting calls to `d3d11.dll`.
- `imgui` is used for overlay display.
- `smhasher` is technically optional. Currently used for identifying the built-in Type 1 filter shader.
