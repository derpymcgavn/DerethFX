# DerethFX Target Look

Reference direction: a nostalgic Asheron's Call view with stronger cinematic atmosphere, but still recognizably old-DX9 and low-poly.

Near-term goals:
- Preserve stable baseline: texture filtering and extended distance on; broad dynamic lighting, surface detail, and fog override off by default.
- Metal should read as metal through material/specular tuning on small opaque object draws, not world-wide lighting.
- Keep UI/chat text legible and authentic. Avoid shader changes that blur, bloom, or recolor the 2D interface.
- Atmospheric goals for later passes: warm low sun highlights, purple/orange sky grade, subtle bloom on bright portals/lights, soft distance haze, and optional film grain.
- Avoid visible rectangular faux-lighting/shadow planes. Treat them as AC-native unless a narrow suppression rule proves safe.

Possible external/ReShade-style effects to approximate later:
- SSAO-like contact depth, subtle and radius-limited.
- Bloom with a high threshold and warm tint.
- Filmic tone mapping with mild saturation/vibrance.
- Purple/orange color matrix or LUT.
- Depth fog and god-ray-like light streaks only if they can avoid UI and alpha artifacts.
ReShade setup path:
- Keep DerethFX installed as `d3d9.dll` next to `acclient.exe`.
- Put ReShade's DX9 wrapper next to it as `reshade_d3d9.dll`.
- Set `[ReShade] chainLoad=1` in `derethfx.ini`, then relaunch the client.
- Prefer ReShade for full-screen post effects: bloom, tone mapping, vibrance/color matrix, subtle fog, and optional film grain.
- Keep DerethFX focused on stable game-side fixes: texture filtering, far plane extension, and narrowly targeted material tweaks.