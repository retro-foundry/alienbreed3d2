# Tools Notes

## Q2RTX Lighting

Q2RTX does not behave like classic Quake 2 lightmap-only rendering. The
project's Q2RTX package uses explicit emissive surfaces and the committed PBR
material catalog rather than relying on baked RAD lightmaps.

Run the complete pipeline from the repository root:

```powershell
python tools\build_q2rtx.py --install --smoke-test --launch --level level_a
```

The pipeline extracts all AB3D2 WALs, writes compiler-only `sky` and `skip`
metadata WALs, packs Q2RTX materials, runs ericw-tools `qbsp`, `vis`, and
`light` with `-q2rtx`, validates every BSP as Quake II `IBSP` version 38, and
installs only the generated AB3D2 runtime files. `ab3d2/technolights` carries
Q2 `SURF_LIGHT` value `900`; its explicit material emission is also `900`.
The source floor tile `ab3d2/floor_0101` is the other proven emitter, at `200`.

## Q2RTX PBR Texture Overrides

The package builder reads `assets/renderer_dxr/materials/materials.json` and its
committed channel images. Those files retain the original source bindings and
the authored `textures_pbr` sheets. Every channel is center-cropped to the
matching logical WAL aspect ratio, omitting packed three-texel-word padding,
and written at four times the WAL size. Explicit source-landmark registration
is shared by all five channels where authored artwork crosses a source window
boundary, including the Level A `chevrondoor` jamb.
Q2RTX roughness is packed into base alpha, and metalness into normal alpha.
The explicit `.mat` entries bind both `ab3d2/name` and
`textures/ab3d2/name`, so no automatic texture-name substitution is required.

The generated package, compiler logs, maps, BSPs, materials, and hash manifest
are under `build\q2rtx`. Restart Q2RTX after changing installed `.mat` or `.tga`
files so it reloads the material definitions.
