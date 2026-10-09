# DerethFX

DerethFX is a tiny Direct3D 9 proxy for Asheron's Call that tries to make Dereth feel a little less like it has been trapped in a dusty old graphics drawer for twenty-five years.

It is not trying to replace the game client, rewrite the renderer, or pretend AC is suddenly a modern engine. The goal is simpler and more fun: sneak into the D3D9 path, understand what the client is drawing, and add tasteful little upgrades without wrecking the UI, the vibe, or the weird old magic that makes AC feel like AC.

Right now this is very much a lab project. It works, it is funny, it is promising, and occasionally it turns hills into glass squares when we get too brave. That is part of the ride.

## What Works Now

DerethFX currently builds a 32-bit `d3d9.dll` proxy that can sit next to `acclient.exe`.

Current progress:

- Loads cleanly as a local D3D9 proxy for the AC client.
- Forwards to the real Windows `d3d9.dll`.
- Wraps `IDirect3DDevice9` so we can inspect and gently modify draw calls.
- Separates most UI/overlay drawing from world drawing.
- Adds world texture detail:
  - anisotropic filtering
  - sharper mip bias
  - UI-safe draw filtering
- Adds tuned water experimentation:
  - found water-ish draw signatures
  - proved reflection-style overlays can work
  - tuned the mask back down after the first broad pass made terrain props look cursed
- Adds a fixed-function dynamic lighting pass:
  - ambient lift
  - specular/material boost
  - directional fill light
  - state restore after each draw
- Logs useful frame counters like `detail`, `waterFx`, and `lightFx` so we can see what the proxy is actually touching.

## Current Vibe

The current build is aiming for:

- sharper world textures
- better armor/material highlights
- a little more scene depth
- subtle water sheen
- no broken UI

The UI staying readable matters. AC's interface is already doing enough ancient wizard work on its own.

## What This Is Not Yet

This is not DLSS, RTX Remix, real bump mapping, or proper screen-space reflections yet.

Those are bigger beasts. DLSS especially wants things the old AC renderer does not naturally hand us, like motion vectors, jitter, depth handling, and NVIDIA NGX integration. Maybe someday. Not today.

The current approach is fixed-function D3D9 state work plus careful draw classification. It is the "make the old renderer behave a little nicer" stage.

## Build

This project is built with CMake and MSVC as a 32-bit DLL.

Example:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The output DLL is:

```text
build/bin/Release/d3d9.dll
```

## Local Install

Back up any existing `d3d9.dll` in the Asheron's Call client folder, then copy the built `d3d9.dll` next to `acclient.exe`.

Typical local path:

```text
C:\Turbine\Asheron's Call\d3d9.dll
```

Launch the game normally. DerethFX writes a local diagnostic log named:

```text
ac_d3d9_proxy.log
```

## Safety Notes

This is a graphics proxy experiment. It is meant for local visual work and debugging. It is not an anti-cheat bypass, packet tool, gameplay automation tool, or anything like that.

If a build looks wrong, close the client and remove or replace `d3d9.dll` from the AC folder.

## Roadmap

Near-term:

- Add an `.ini` config file for effect strength and toggles.
- Add a safe/release logging mode so the log does not get noisy.
- Add hot reload or at least a simple restart-friendly config workflow.
- Tune water so it feels reflective without catching unrelated alpha surfaces.
- Tune dynamic lighting so it adds depth without making everything shiny plastic.

Later:

- A real post-process pass for contrast, sharpening, and maybe light bloom.
- Better water shimmer/specular using a more direct render pass.
- Optional NVIDIA-style sharpening/upscaling experiments.
- Investigate whether useful depth information can be captured safely.
- Maybe, eventually, a serious DLSS/NGX research branch if the render data exists.

## The Spirit Of It

Asheron's Call already has atmosphere. DerethFX is not here to sand that off. It is here to make the old stones, water, armor, rain, portals, and weird late-night dungeon lighting hit a little harder.

Make it prettier. Keep it Dereth.