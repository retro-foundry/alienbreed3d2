# AB3D2 PBR texture pack

This directory is category-sorted and zip-ready. Every material has five PNGs:

- `_base_color.png` — sRGB colour and source alpha
- `_normal.png` — tangent-space normal (linear)
- `_metalness.png` — metalness (linear)
- `_roughness.png` — roughness (linear)
- `_emissive.png` — sRGB emission colour and source alpha

Keep each edited channel at the dimensions recorded in `materials.json`. Channels
listed in `generated_channels` are neutral placeholders awaiting artwork. The
build validates and embeds the exact PNG bytes in the runtime package; the game
decodes only materials required by the live scene. `materials.json` records the
source asset and renderer binding for every material.

The category directories are `walls`, `floors`, `weapons`, `vector_models`,
`enemies`, `billboards`, `effects`, `environment`, and `ui`. Each category is
flat, and every filename begins with its unique material name.

From the repository root, create an artist archive with:

```powershell
Compress-Archive assets/renderer_dxr/materials/* ab3d2-pbr-textures.zip
```
