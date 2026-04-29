# Tools Notes

## Q2RTX Lighting

Q2RTX does not behave like classic Quake 2 lightmap-only rendering. For visible RTX lighting, add emissive surfaces, not just `light` entities or baked RAD lightmaps.

For AB3D2 converted maps:

- Compile with ericw-tools 2.0 alpha/dev builds, not ericw 0.18.x.
- Use the Q2RTX target on all stages:

```powershell
build\bin\qbsp.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.map build\q2rtx_ericw\maps\level_a.bsp
build\bin\vis.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.bsp
build\bin\light.exe -q2rtx -basedir build\q2rtx_ericw build\q2rtx_ericw\maps\level_a.bsp
```

- The `-basedir` tree must look like `baseq2`: include `maps\`, `textures\ab3d2\`, `textures\sky.wal`, `pics\colormap.pcx`, `materials\`, and any `overrides\` emissive textures.
- Light-emitting brush faces need the Q2 `SURF_LIGHT` flag and a nonzero surface value. For example, `ab3d2/technolights` currently exports as flags `1`, value `900`.
- Q2RTX material definitions should match both texture naming forms:

```text
ab3d2/technolights,
textures/ab3d2/technolights:
    texture_emissive overrides/ab3d2_technolights_light.tga
    is_light 1
    synth_emissive 1
    emissive_threshold 0.0
    emissive_factor 200
    bsp_radiance 200
    base_factor 3
    correct_albedo 1
```

- Install the material files into `baseq2\materials\` and/or a map-specific file such as `baseq2\maps\level_a.mat`.
- Install explicit emissive textures under `baseq2\overrides\`. Relying on `synth_emissive` alone may leave dark AB3D2 textures visibly non-emissive.
- If strips are only visible during muzzle flashes, Q2RTX is seeing the geometry but not treating the material as emissive. Check the `.mat` path/name match and the `texture_emissive` override first.
- After changing `.mat` or `.tga` files, fully restart Q2RTX or reload the map from a fresh process so material definitions are re-read.
- A healthy ericw light pass for the test `level_a` neon strips reports nonzero surface lights, e.g. `207 surface light points in use`.

## Q2RTX PBR Texture Overrides

Q2RTX should use the upscaled hand-authored texture sheets in `textures_pbr\` for the in-game albedo, not the original low-resolution WAL pixels. The BSP still references `ab3d2/name` WAL texture names for compiler metadata, but the Q2RTX material file replaces those at runtime with `texture_base overrides/ab3d2/name.tga`.

Build the packed Q2RTX overrides from the sheet sources:

```powershell
python tools\build_q2rtx_pbr_from_sheets.py
```

This reads source sheets arranged either as:

- `ALBEDO NORMAL` / `METALNESS ROUGHNESS`
- or, for `gieger.png` and `steampunk.png`, vertical `ALBEDO`, `NORMAL`, `METALNESS`, `ROUGHNESS`

and writes:

- `q2rtx_pbr\baseq2\overrides\ab3d2\*.tga` upscaled albedo with roughness packed in alpha
- `q2rtx_pbr\baseq2\overrides\*.tga` flat fallback copies for Q2RTX's automatic override search
- `q2rtx_pbr\baseq2\textures\ab3d2\*.tga` editor/runtime-visible upscaled albedo path next to the original WAL names
- `q2rtx_pbr\baseq2\overrides\ab3d2\*_n.tga` normal RGB with metalness packed in alpha
- `q2rtx_pbr\baseq2\overrides\ab3d2\*_r.tga` and `*_m.tga` inspection maps
- `q2rtx_pbr\baseq2\overrides\ab3d2\*_light.tga` emissive overrides for known light textures
- `q2rtx_pbr\baseq2\materials\ab3d2_pbr.mat`
- `q2rtx_pbr\baseq2\maps\level_*.mat` map-specific material files so old partial map materials cannot override the upscaled base textures

Install into Q2RTX with:

```powershell
Copy-Item q2rtx_pbr\baseq2\overrides\ab3d2\*.tga "C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2\overrides\ab3d2" -Force
Copy-Item q2rtx_pbr\baseq2\overrides\*.tga "C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2\overrides" -Force
Copy-Item q2rtx_pbr\baseq2\textures\ab3d2\*.tga "C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2\textures\ab3d2" -Force
Copy-Item q2rtx_pbr\baseq2\materials\ab3d2_pbr.mat "C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2\materials" -Force
Copy-Item q2rtx_pbr\baseq2\maps\level_*.mat "C:\Program Files (x86)\Steam\steamapps\common\Quake II RTX\baseq2\maps" -Force
```

The generated material file matches both `ab3d2/name` and `textures/ab3d2/name`, assigns `texture_base` to the upscaled albedo override, assigns `texture_normals`, and lets Q2RTX read roughness from base alpha and metalness from normal alpha. Restart Q2RTX after installing so it reloads material definitions and override textures.
