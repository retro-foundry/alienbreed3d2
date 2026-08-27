# AB3D2 PBR texture pack

This directory is category-sorted and zip-ready. Every material has five PNGs:

- `_base_color.png` — sRGB colour and source alpha
- `_normal.png` — tangent-space normal (linear)
- `_metalness.png` — metalness (linear)
- `_roughness.png` — roughness (linear)
- `_emissive.png` — sRGB emission colour and source alpha

Keep each edited channel at the dimensions recorded in `materials.json`. Channels
listed in `generated_channels` are generated defaults awaiting artwork. Weapon
and vector-model materials use roughness 184/255 (the nearest PNG encoding of
0.72), metalness 0, and `specular_factor` 0.35 to preserve the proven source-
vector material response. World channels are centre-cropped and Lanczos-resized
to four times the authoritative AB3D2 logical source extent; unused packed-WAD
columns are not exported. Authored landmark registration is then applied to all
five channels where replacement artwork does not match source texel boundaries,
with the same encoded normal-Z floor used by the Q2 package. Native DXR filters
those maps within each source wall window. At scene load it crops the exact
`Draw_Wall` U/V window and generates a wall-only mip pyramid: base color and
emission average in linear light, normals are renormalized, and metalness and
roughness average linearly. Ray-cone LOD and trilinear sampling use that
isolated pyramid without changing the level-zero-only behavior of other
material classes. A wall binding's `v_period` selects
the exact packed-WAD interpretation used by its Draw_Wall record. Other
unauthored channels use the
neutral defaults listed in
the manifest. The
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
