# DerethFX

DerethFX is a tiny Direct3D 9 proxy for Asheron's Call. It keeps the old client recognizably Dereth while adding a sharper, moodier presentation through two layers:

- DerethFX: stable D3D9-side texture filtering and render-distance tuning.
- ReShade: an optional atmospheric post-process preset for color, sky depth, subtle pseudo-bloom, debanding, and gentle sharpening.

The current release favors stability over wild renderer hacks. The experimental water, dynamic-lighting, material, surface-detail, and fog override passes remain available in config, but the release preset keeps them off by default.

## Recommended Install

Use ReShade in front and DerethFX behind it:

```text
C:\Turbine\Asheron's Call\d3d9.dll            = ReShade's DX9 DLL
C:\Turbine\Asheron's Call\derethfx_d3d9.dll  = DerethFX DLL
C:\Turbine\Asheron's Call\derethfx.ini       = DerethFX config
C:\Turbine\Asheron's Call\ReShadePreset.ini  = included atmospheric preset
```

In `ReShade.ini`, enable proxy loading:

```ini
[PROXY]
EnableProxyLibrary=1
ProxyLibrary=.\derethfx_d3d9.dll
```

Set the preset path:

```ini
[GENERAL]
PresetPath=.\ReShadePreset.ini
```

The release ZIP includes `d3d9.dll` built by this project. Rename it to `derethfx_d3d9.dll` when using ReShade in front.

## DerethFX Defaults

The checked-in `derethfx.ini` starts with the stable core enabled:

```ini
[Effects]
textureDetail=1
extendRenderDistance=1
waterReflection=0
dynamicLighting=0
metalSheen=0
surfaceDetail=0
volumetricFog=0
suppressShadowPlanes=0
```

Press `F9` in-game to open the DerethFX config panel. `Save + Reload` writes `derethfx.ini` and refreshes most DerethFX settings without a client restart.

## ReShade Preset

`presets/ReShadePreset.ini` is the current atmospheric preset. It uses effects that were tested with the installed SweetFX/ReShade shader set:

- `LiftGammaGain`
- `Tonemap`
- `FakeHDR` as a stable pseudo-bloom/local contrast pass
- `Vibrance`
- `Technicolor2`
- `Deband` for smoother sky/cloud gradients
- `Curves`
- `Sepia`
- `Vignette`
- `FilmGrain` at very low strength to give skies and fog a little texture
- `CAS`

The goal is a warmer, more immersive night/storm look with more life in the sky texture while keeping UI text readable. `presets/DerethFX_Dream.ini` is a bolder variant with stronger sunset warmth, more bloom-like glow, and a touch more grain for testing screenshots.


## Mode HUD

DerethFX includes an optional click-through HUD overlay for challenge runs. Press `F10` in-game to cycle `Off -> Crawler -> Ironman`, or open the `F9` config panel and set the mode there. The HUD is visual only; it does not enforce rules or alter gameplay.

Config keys live under `[HUD]`:

```ini
[HUD]
enabled=0
mode=crawler
x=18
y=92
opacity=220
```
## Build

This project is built with CMake and MSVC as a 32-bit DLL.

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Build output:

```text
build/bin/Release/d3d9.dll
build/bin/Release/derethfx.ini
build/bin/Release/presets/ReShadePreset.ini
```

## Safety Notes

This is a graphics proxy experiment. It is meant for local visual work and debugging. It is not an anti-cheat bypass, packet tool, gameplay automation tool, or anything similar.

If a build looks wrong, close the client and remove or replace the local `d3d9.dll`/proxy files from the AC folder.
