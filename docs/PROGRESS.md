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

The most promising next step is a config system. Hardcoded constants made sense while discovering the render path, but tuning is going to be much faster with an INI file.

## Known Risks

- Water detection is still heuristic.
- Fixed-function texture stages can be fragile across client scenes.
- Too-broad alpha matching can affect random world props.
- Dynamic lighting is intentionally conservative but may need per-scene tuning.
- Logging should be reduced before any normal-use build.