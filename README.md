# Mega Man Battle Network Legacy Collection `d3d11.dll` wrapper mod
<img width="2560" height="1440" alt="20260422095622_1" src="https://github.com/user-attachments/assets/4224b649-ffc7-4f05-8e19-0619ffa3b380" />
^^^crt-royale

API Middleware Based on xzn's Mega Man X Collection wrapper//
            [custom shaders & graphic effects]

Features:
- Proper Scaling across various screen modes and resolutions.
- Let's you cycle up to 3 `RetroArch` [slang-shaders](https://github.com/libretro/slang-shaders) presets at a time with Capcom's Mega Man Battle Network Legacy Collection.
- Updated Text Blending for crisper letters, with customizable sharpness, font size, & contrast to pair with shaders.
- Fully configurable config with User inputs, hotkeys, & Xinput controller binds.
- Widescreen mode toggle.
  

Download from [here](https://github.com/isja13/MMBN-ShaderEngine/releases).

![20260413054209_1](https://github.com/user-attachments/assets/0c65650e-6e1a-4ae2-9ad0-81330d86ecc4)
^^^scalefx+lcd
~===========================================================================~

# Usage
Replaces the game's built in filter option with active `slang` preset file path in `.ini`
<img width="2560" height="1440" alt="20260420053737_1" src="https://github.com/user-attachments/assets/f2ebc321-c04c-4e84-a1b3-5df47a19ad61" />
xbrz-freescale-multipass^^^

Control with hotkeys, or configure custom binds//
~By default:

`ShaderToggle` - "`" or L.stick      (universal mod-wide killswitch and A/B toggle)

`Previous/Next` - "1/2" or R.stick     (cycle through selected presets in real time)

`Widescreen` - "3" [must bind pad]     (toggles 16:9 screen fill, when in Max size)

`Text Aura` - "-/+"      ^^^        (Universal Font Size)

`Sharpness` 0 "[/]"       ^         (Text Bold/Contrast)

// Because the game binds it's own controller buttons, I don't know what inputs a given user can have free so gamepad bindings must be handled by individual config.
<img width="2560" height="1440" alt="20260420054116_1" src="https://github.com/user-attachments/assets/3aaa5348-c97b-43c7-990c-07a978372c22" />
crt-lottes widescreen^^^

<img width="2560" height="1440" alt="20260422014425_1" src="https://github.com/user-attachments/assets/8efc9b04-b93b-4009-8a48-e46ff78bf5f1" />
clearer default text^^^

<img width="777" height="390" alt="before" src="https://github.com/user-attachments/assets/7cccb31d-3b75-40f6-900c-cdab7664e3be" />
before^^^
<img width="840" height="395" alt="after" src="https://github.com/user-attachments/assets/108de98b-52a9-4f6c-b428-a87c04cfdab4" />
after^^^

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

Copy `dxgi.dll`, `shader-engine.ini`, and the `slang-shaders\` directory to your game folders, e.g.:

- `SteamLibrary\steamapps\common\MegaMan_BattleNetwork_LegacyCollection_Vol1\exe`
- `SteamLibrary\steamapps\common\MegaMan_BattleNetwork_LegacyCollection_Vol2\exe` 

## Configuration

`shader-engine.ini` contains options to configure the mod.

```ini
;================================================
[graphics]
enhanced=true
linear=false
interp=true
                             _
slang_shader_1=slang-shaders/crt/crt-royale.slangp
                             ^
slang_shader_2=slang-shaders/custom/ScaleFx+LCD.slangp
                             ^
slang_shader_3=slang-shaders/crt/crt-lottes.slangp
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

;================================================

[Keybinds]
hotkey_shader_toggle=VK_OEM_3
hotkey_increase_text_aura=VK_OEM_MINUS
hotkey_decrease_text_aura=VK_OEM_PLUS
hotkey_increase_sharpness=VK_OEM_6
hotkey_decrease_sharpness=VK_OEM_4
hotkey_shader_prev=1
hotkey_shader_next=2
hotkey_widescreen=3

[Controller]
hotkey_shader_toggle_pad=XINPUT_LS
;hotkey_increase_text_aura_pad=XINPUT_B
;hotkey_decrease_text_aura_pad=XINPUT_Y
;hotkey_widescreen_pad=XINPUT_RT
;hotkey_shader_prev_pad=XINPUT_LT
hotkey_shader_next_pad=XINPUT_RS

[Defaults]
shader_toggle=TRUE
key_threshold=0.07
key_sharpness=4.0
widescreen=FALSE

;================================================
;logs to 'shader-engine.log'
[logging]  
; enabled=true
; hotkey_toggle=VK_CONTROL+O
; hotkey_frame=VK_CONTROL+P
;custom/Stock.slangp
;test/format.slangp
;================================================
```

If all goes well you should now be able to start the game and see the overlay on top-left of the screen showing the status of the mod.
<img width="456" height="125" alt="Overlay" src="https://github.com/user-attachments/assets/6a48d93f-316c-4619-8e6f-aad9e9066713" />

`shader-engine.ini` can be edited & have its options applied while the game is running.

## License

Source code for this mod, without its dependencies, is available under MIT. Dependencies such as `RetroArch` are released under GPL.

- `RetroArch` is needed only for `slang_shader` support.
- `SPIRV-Cross` and `glslang` are used for `slang_shader` support.
- `HLSLcc` is used for debugging.

Other dependencies are more or less required:

- `minhook` is used for intercepting calls to `d3d11.dll`.
- `imgui` is used for overlay display.
- `smhasher` is used for identifying key shaders.
