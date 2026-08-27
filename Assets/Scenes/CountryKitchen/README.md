# Country Kitchen (Mitsuba scene)

This directory contains the Mitsuba conversion of **Country Kitchen**, the scene labelled `KITCHEN` in Figure 10 of *DSCombiner: Double Shrinkage for Combining Biased and Unbiased Monte Carlo Renderings* (ACM TOG 2025).

## Provenance

- Canonical Mitsuba download: <https://rgl.s3.eu-central-1.amazonaws.com/scenes/kitchen.zip>
- Mitsuba Gallery entry: <https://mitsuba.readthedocs.io/en/v3.6.3/src/gallery.html>
- Original artist: Jay-Artist, <https://www.blendswap.com/user/Jay-Artist>
- Original Blend Swap asset: <http://www.blendswap.com/blends/view/42851>
- Conversion and distribution: Benedikt Bitterli's rendering resources, <https://benedikt-bitterli.me/resources/>
- Downloaded archive SHA-256: `C9C3E104721ED08FACBDD8543572E44EF825CAE9CDF2405C7F4B13F5AB22F874`

## License and attribution

`LICENSE.txt` is the license notice distributed with the archive. It states that the scene is available under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). Preserve that file and credit Jay-Artist with the CC BY 3.0 link whenever the scene or a derivative is redistributed.

## Layout and runtime status

- `scene.xml` is the authoritative Mitsuba scene, including camera, lights, materials, and references to the assets below.
- `models/` contains the 295 source OBJ meshes; `textures/` contains their source textures.
- `TungstenRender.exr` and `TungstenRender.png` are the upstream reference renders.

`SceneImporter::ImportFromFile()` directly loads this Mitsuba XML package. It expands scene-local `<default>` variables, imports the perspective sensor, OBJ shapes with their `to_world` transforms, rectangle area emitters, and top-level spot emitters. Its deliberately bounded material conversion maps supported BSDF wrappers (`twosided`, `mask`, and `bumpmap`), diffuse/plastic/conductor/dielectric families, constant base colors, and bitmap base-color maps (`reflectance`, `diffuse_reflectance`, and `base_color`) into the runtime metallic/roughness PBR contract. This preserves the authored wood, food, towel, floor, and radio textures that the source package actually references. It is not a complete Mitsuba renderer: transmission, alpha, bump/normal evaluation, spectral IOR data, and other advanced BSDF semantics are currently ignored. Many cupboard and wall surfaces are intentionally plain because their source BSDFs use a constant `reflectance`, not a missing texture. OBJ references must remain relative to this scene directory, and this license notice must stay with the package.

This is the Demo default scene. The current runtime loads the 295 OBJ files independently and constructs their GPU/RTAS resources at startup, so its first initialization is intentionally a heavy research-scene path rather than a streaming or pre-cached production path.
