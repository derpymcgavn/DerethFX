# Progress Notes

## Working Build Line

The experiment has gone through several staged DLLs while testing live against `acclient.exe`.

The important milestone builds:

- v7: stable device proxy and frame logging.
- v8: sampled busy frames to understand world/UI draw boundaries.
- v9: UI classifier started separating overlay draws cleanly.
- v10: world texture detail pass with anisotropic filtering and mip bias.
- v11: water draw diagnostics.
- v12-v14: water reflection experiments, including one hilarious over-broad mask that made terrain chunks look translucent.
- v15: tuned water mask; subtle but safer.
- v16: scoped dynamic lighting pass with ambient/specular/headlight-style fill.

## Known Good Direction

The proxy approach is viable. The device wrapper is much more useful than pure vtable patching, and the UI/world split is good enough to keep experimenting.

The config system is now in place. `derethfx.ini` covers effect toggles, texture detail strength, water mask bounds/strength, dynamic lighting values, and normal-vs-debug logging. Hardcoded discovery values are still the defaults, but the fun knobs finally live somewhere friendlier than a rebuild.

## Known Risks

- Water detection is still heuristic.
- Fixed-function texture stages can be fragile across client scenes.
- Too-broad alpha matching can affect random world props.
- Dynamic lighting is intentionally conservative but may need per-scene tuning.
- Logging now defaults quieter, but debug mode can still get loud fast when draw sampling or water diagnostics are enabled.
## Current Head

- Added `derethfx.ini` with toggles for texture detail, water reflection, dynamic lighting, and logging.
- Moved the v16 tuning constants into config-backed defaults.
- CMake now copies the sample INI beside the Release DLL so the output folder is install-ready-ish.
- Normal logging is quieter; debug logging keeps the frame summaries, draw samples, and water candidate traces for tuning sessions.

## Atmosphere Pass

- Added experimental fog shaping through D3D9 render-state overrides: range fog, EXP2 table fog, softer density, and farther fog end.
- Added projection far-plane extension for the device wrapper. This can reduce renderer-side clipping, but AC's own world/object culling may still decide what exists.
- Water sample tuning moved to `strength=0.65` after the first config build looked a little too electric in open water.
## F9 Config Panel

- Added a first-pass Win32 config panel on `F9`.
- The panel can toggle major effects and edit water strength, mip bias, far plane, and fog scales.
- `Save + Reload` writes `derethfx.ini` and refreshes the in-memory config immediately, so most tuning no longer needs a client restart.
## Alpha Light Protection

- Added an alpha-light protection pass so soft alpha billboard/light draws do not get texture-detail sharpening or the extra dynamic light pass.
- Added subtle configurable dynamic-light flicker and direction motion for less static lighting without making transparent quads louder.
- Current tuning favors crisper terrain and duller material shine: anisotropy 16, mip bias -0.85, water 0.55, specular 0.45.
## Surface Detail Pass

- Added an experimental generated grayscale detail texture pass on opaque world draws only.
- The pass skips UI, water, soft alpha billboards, and shader draws.
- Raised controlled specular response for metal/purple armor while keeping alpha-card protection active and hard alpha-test disabled.
## ReShade Atmospheric Release

- Switched the recommended stack to ReShade in front and DerethFX behind it as `derethfx_d3d9.dll`.
- Added an atmospheric ReShade preset using stable SweetFX effects: LiftGammaGain, Tonemap, FakeHDR, Technicolor2, Deband, Curves, Sepia, Vignette, and CAS.
- Kept DerethFX defaults conservative: texture detail and render distance on; water, dynamic lighting, metal sheen, surface detail, fog override, and shadow-plane suppression off.
- The active preset adds sky/cloud contrast and smoother gradients without the rectangle artifacts seen from broader bloom/DPX experiments.