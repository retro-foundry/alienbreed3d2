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

