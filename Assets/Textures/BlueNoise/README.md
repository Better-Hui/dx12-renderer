# Fast spatiotemporal blue-noise masks

These DDS files are exported from the UE engine content assets
FastBlueNoise_scalar_128x128x64 and FastBlueNoise_vec2_128x128x64.
The source assets are the engine implementation of the
Spatiotemporal Blue Noise Masks construction by Wolfe et al. (2022), as
referenced by Epic's BlueNoise.ush and the NVIDIA real-time blue-noise
rendering notes.

The runtime layout is a 128 x 8192 2D atlas: each 128-row band is one of
64 temporal slices. The shaders use integer Load at mip 0 and treat both
textures as linear data, not sRGB color.

Keep the upstream UE/Epic and paper attribution with any redistribution of
these assets.

The shader access contract is centralized in
`Framework/shaders/Common/BlueNoise.hlsli`:

- `FrameworkBlueNoiseScalar` and `FrameworkBlueNoiseVec2` read an explicit
  pixel/frame coordinate.
- `FrameworkSampleStbnScalar` and `FrameworkSampleStbnVec2` apply a stable
  salt-dependent tile/frame offset for independent sample dimensions.

The demo-side lifetime and loading contract is
`Demos/RaytracingDemo/include/Scene/RaytracingDemoBlueNoiseResources.h`.
Path tracing and all ReSTIR DI/GI stages bind these resources through the
shared `RaytracingDemoPassBindings` helper rather than owning separate noise
textures or hash-noise implementations.

Path tracing seeds its `Random01` stream from the STBN pixel/frame coordinate
and advances every dimension through `FrameworkSampleStbnScalar`; there is no
PCG fallback for later dimensions. ReSTIR and other temporal stages use the
same STBN coordinate contract directly.
