# RaytracingDemo API Guide

`RaytracingDemo` is the maintained integration sample for this repository. It demonstrates **this repository's current renderer and framework APIs**; it is not a claim that every wrapper is final or that this API shape is universally optimal.

For a layer-by-layer map of the codebase and data flow, see [Architecture Overview](ArchitectureOverview.md).

## Sample responsibility

The sample owns algorithms and sample policy: scene/camera/lights, RenderGraph topology, backend selection, runtime UI, and explicitly assigning an eligible pass to `RenderPassQueue::AsyncCompute`.

The sample should not normally own native descriptor heaps/tables, root-parameter indices, root-signature construction, raw PSO binding, DXR shader-table allocation, or manual CPU fence waits for graph dependencies. Those details belong to the framework and `DX12Library`.

## Command recording

Use `CommandContext` as the normal sample-facing command API:

```cpp
CommandContext commands(commandList);
commands.BindPipeline(shader);
commands.BindDescriptorSet(shader.GetDescriptorSet());
commands.Dispatch(groupX, groupY, groupZ);
```

Raster, compute, mesh shader, and DXR paths follow the same `BindPipeline` + `BindDescriptorSet` model before `Draw`, `Dispatch`, `DispatchMesh`, or `DispatchRays`. Add a framework capability when needed instead of exposing another raw D3D12 escape hatch in a pass.

## RenderGraph

Pass input/output declarations are the source of dependency ordering and resource-state planning.

```cpp
auto pass = RenderGraph::RenderPass::Create(
    L"Example Pass",
    { { inputId, RenderGraph::InputType::NonPixelShaderResource } },
    { { outputId, RenderGraph::OutputType::UnorderedAccess } },
    execute);
```

The main flow is:

```text
Base Resources
-> Direct Lighting
-> Indirect Lighting
-> Lighting Composite
-> optional Denoise
-> Skybox / post process
-> optional `BloomController`: Raster Bloom subgraph or CUDA external pass
-> Display / Overlay
-> Present
```

With HDR10 enabled, `Auto Exposure` produces exposure-adjusted linear `R16G16B16A16_FLOAT` display input instead of SDR gamma output. `PresentWithOverlayBlit()` then invokes the terminal HDR10 shader, which tone maps, converts Rec.709 primaries to Rec.2020, and ST.2084/PQ encodes into the `R10G10B10A2_UNORM` swapchain. The swapchain color space and HDR10 metadata are configured only after `IDXGIOutput6` reports an HDR10/PQ-capable active output. The runtime controls are `[Display] HDR10`, `RAYTRACING_DEMO_HDR10=0|1`, `--hdr10`, and the `Display Output` UI; unsupported output safely retains SDR and reports `presentation_hdr10_output` to Diagnostics. Native HDR10 presentation and experimental DLSS Frame Generation are currently mutually exclusive.

When the Direct Lighting UI selects ReSTIR DI on the inline ray-query backend, Framework registers a direct-light subgraph:

```text
Base Resources
-> ReSTIR DI Initial Sampling
-> ReSTIR DI Temporal Resampling
-> ReSTIR DI Boiling Filter
-> ReSTIR DI Spatial Resampling
-> ReSTIR DI Shade
-> Lighting Composite
```

`ReSTIRDIPass::AddPasses` registers these stages while the graph is being constructed. RenderGraph tracks and times every stage independently. Framework owns persistent reservoirs and shader variants but imports those allocations with logical IDs, allowing the compiler to plan barriers and hazards. The builder reference is never stored.

Raster Bloom follows the same construction-scoped registration rule:

```text
Bloom Prefilter
-> Bloom Downsample 1..N
-> Bloom Upsample N-1..0
-> Bloom Composite
```

`BloomController` is the Demo-facing backend selector. Its raster backend delegates to `Bloom::AddPasses`, which declares its pyramid with `RenderGraphBuilder::CreateTexture()`, so the Demo only supplies the source/output IDs, tokens, resolution expressions, parameters, and pyramid depth. The graph owns those textures, their lifetime, and their barriers. They participate in transient aliasing: the allocator writes the alias barrier at the new placed resource's first actual use and initializes its planned state as `COMMON`; the CUDA backend is unaffected and remains an explicit external queue/semaphore path.

### ReSTIR DI direct lighting

`Framework/Rendering/Lighting/ReSTIRDI.h` owns renderer-neutral settings and frame constants. `Framework/Rendering/Lighting/ReSTIRDIPass.h` owns persistent reservoir/history resources, shader variants, and the graph-registration contract. The sample supplies `DirectLighting`, tokens, motion vectors, and callbacks that declare and resolve its scene contract.

`ReSTIRDISettings` currently exposes:

- RIS candidate count and optional initial visibility.
- Temporal reuse, `Off` / `Basic` / `RayTraced` temporal bias correction, a 20-frame default history-length clamp, reprojection similarity tests, and permutation sampling.
- Boiling filtering within the temporal shader, after temporal resampling and before spatial resampling.
- Spatial reuse, `Off` / `Basic` / `Pairwise` / `RayTraced` spatial bias correction, neighbor/radius settings, disocclusion boosting, and normal/depth/material similarity tests.
- Final visibility, optional final-visibility reuse, and reuse age/distance limits.

The light domain contains directional, point, rectangular area, and scene-generated emissive surface emitters. The path remains inline-ray-query only; shader-table DXR continues to use the normal direct-lighting path.

Visibility is selected by the stage-specific settings above. In particular, enabling final visibility performs visibility testing for the selected final sample; temporal/spatial `RayTraced` correction modes can additionally trace during their resampling logic. Do not treat a stage switch as a guaranteed quality or performance improvement without a visual and timing A/B on the target adapter.

This is still an experimental ReSTIR DI implementation. Its sampling policy, quality, stability, and performance are not accepted as RTXDI-equivalent, and it does not yet provide a complete production light-presampling, dynamic-scene validation, or quality-tuning system.

### ReSTIR GI indirect lighting

When `Indirect Lighting` selects `ReSTIR GI` on the inline ray-query backend, Framework registers an indirect-light subgraph:

```text
Base Resources
-> ReSTIR GI Initial Sampling
-> ReSTIR GI Temporal Resampling
-> ReSTIR GI Spatial Resampling
-> ReSTIR GI Shade
-> Lighting Composite
```

`Framework/Rendering/Lighting/ReSTIRGI.h` owns renderer-neutral settings and frame constants. `Framework/Rendering/Lighting/ReSTIRGIPass.h` owns packed reservoir textures, shader variants, and `AddPasses`. The sample supplies `IndirectLighting`, graph tokens, motion vectors, and callbacks that declare and resolve its GBuffer/TLAS/bindless scene ABI. Ping-pong history is resolved from the runtime frame index without rebuilding or retaining the builder.

The implementation follows the ReSTIR GI data flow in [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT): initial secondary-hit sampling, temporal reuse with surface validation, spatial reuse with Jacobian correction, and final visibility evaluation. Each selected secondary vertex stores emitted radiance plus direct and bounded continuation-path radiance using the same estimator as ordinary indirect path tracing. The reservoir itself still represents one resampled secondary vertex, and the feature is Inline Ray Query only. The feature stores three `R32G32B32A32_UINT` textures per reservoir set for creation surface, secondary hit, and radiance/state; `Initial`, `Temporal`, and persistent `History` sets consume roughly 144 bytes per pixel before allocator overhead.

This is an experimental feature, not a complete ReSTIR PT implementation. It has no previous-frame TLAS, reuse of multi-bounce path segments, ReSTIR-N reservoir sets, or accepted image-quality/performance validation. Treat the UI controls as algorithm debugging parameters and validate temporal stability, memory use, and GPU cost on the target adapter.

### Explicit async compute

```cpp
RenderGraph::RenderPass::Create(
    L"Example Async Compute", inputs, outputs, execute,
    RenderGraph::RenderPassQueue::AsyncCompute);
```

Current behavior:

- Direct and Async Compute queues are created and profiled separately.
- `RenderGraphQueueScheduler` records the last writer queue and submitted fence value for each logical resource.
- `PassResourceStatePlan` describes transition/UAV/aliasing work per pass. The worker-owned command list records that plan, and its initial states are resolved against the shared `ResourceStateRegistry` when command lists are submitted in graph order.
- A cross-queue consumer submits the producer work, then receives a GPU-side fence wait.
- Direct-to-Compute transitions are recorded on Direct before submission; a Direct consumer of Compute output receives the reciprocal wait.
- Async inputs are limited to read-only classes; async outputs are tokens, UAVs, or copy destinations. Render targets, depth, and external passes remain Direct-only.
- Inline-ray-query `Indirect Lighting` is the sample's current Async Compute example.

### Imported resources and native recording

Framework-owned persistent resources enter normal graph scheduling through `RenderGraphBuilder::ImportResource`. They receive logical IDs and participate in dependency, culling, state, UAV, and cross-queue hazard planning, but remain outside the transient resource pool.

Native integrations such as NRD and RTAS construction remain audited renderer-infrastructure boundaries. NRD declares its RG-visible inputs/outputs and restores the native snapshot state around SDK recording; the CUDA Bloom backend and Streamline remain explicit external queue/semaphore boundaries. `RenderGraphRoot` exposes terminal Present variants and `ReadbackTexture` only; generic graph-to-graph copy or draw callbacks do not exist, so new graph work must be declared as a pass. The dynamic RTAS upload pass declares ordinary vertex/index plus Meshlet vertex, bounds, transform, and instance writes as `COPY_DEST`; later Meshlet culling and RTAS refit declare the corresponding reads. The RTAS implementation only owns its backing/scratch state. Demo pass code cannot call transition/UAV/aliasing barriers, and `CommandList`/`CommandContext` expose no such API.

This is an explicit queue API, not an automatic multi-queue scheduler. It does not decide queue placement, split or batch passes, optimize overlap, or schedule a Copy queue. A pass may become slower when dependencies expose its compute tail or when graphics and compute contend for GPU execution/cache/bandwidth.

### Compacted ray-traced pixel dispatch

`RaytracingDemo` can build an active-pixel list before PT or ReSTIR work. The compaction shader reads the depth resource and uses a global `InterlockedAdd` counter to append each valid pixel's linear index. This is an atomic append, not a prefix-sum scan.

- `ActivePixelCount` counts valid geometry pixels; it does not count rays. One valid pixel can issue multiple visibility, soft-shadow, bounce, or ReSTIR rays.
- Inline compacted consumers use `D3D12_DISPATCH_ARGUMENTS = { ceil(ActivePixelCount / 64), 1, 1 }`. The UI labels this as compute dispatch groups and also shows the resulting launched thread count; the final group can contain padding threads guarded by the shader.
- Compacted shader-table DXR uses a separate `D3D12_DISPATCH_RAYS_DESC` with `Width = ActivePixelCount`, so DXR ray-generation invocations are one per active pixel. Do not interpret the compute dispatch X value as the DXR invocation count.
- Compaction adds a full-screen depth scan, atomic UAV writes, finalize work, barriers, and readback/diagnostic plumbing. Its gain comes from removing inactive-pixel lighting and ray-query work; it is not primarily a benefit from padding-thread early exits, and it can shrink when most pixels are active.

The runtime diagnostics distinguish `NotQueued`, `NotCompleted`, and `Completed` readback states. They validate that the active count and finalized indirect arguments agree without changing the rendered result.

### DXR backend compatibility

The shader-table backend currently supports the Path Tracing direct and indirect stages. ReSTIR DI and ReSTIR GI remain Inline Ray Query only, and Async Compute is meaningful only for the Inline backend. `RaytracingDemoFrameState::SupportsDirectLighting`, `SupportsIndirectLighting`, and `SupportsAsyncCompute` are the shared capability predicates used by graph construction and UI warnings.

When a user manually selects DXR while an Inline-only stage is selected, the UI opens a compatibility popup with the option to switch back to Inline or keep DXR, then leaves a red warning listing the skipped/ignored stages. Automated backend changes do not open the modal popup so unattended tests are not blocked.

## Profiling

RenderGraph timing captures queue-local GPU timestamp samples and exports CSV history. Use it for repeated fixed-scene A/B pass-duration measurements.

Do not infer global overlap from separate queue timestamp origins. Use PIX Timing Capture for the real queue timeline:

1. Capture the same scene, camera, resolution, and runtime state with Async Compute disabled and enabled.
2. Select the `Graphics` lane configuration and show `Direct Command Queue` plus `Compute Command Queue`.
3. Compare overlap, GPU-side waits, periods where both queues are idle, and CPU submission only when the GPU timeline indicates a bubble.

PIX is the authority for cross-queue wall-clock overlap; CSV is the lightweight repeatable companion measurement.

The Framework [Diagnostics contract](FrameworkDiagnosticsPlan.md) is now implemented as an optional path:

- `DiagnosticsSession` owns the typed, frame/sequence/correlation/thread-aware event buffer and exports the manifest, summary, domain snapshots, timings, assertions, and reproduction recipe.
- `Application`, the three `CommandQueue` instances, and `RenderGraphRoot` accept the same optional non-owning telemetry sink. Disabled checks occur before timing or diagnostic string construction on hot paths.
- `RaytracingDemo` creates the session from environment state, attaches it to Application and RenderGraph, and registers sample-specific controls, observations, and scenarios through the Framework `AutomationRunner`.
- `RendererDiagnostics` runs scenarios without desktop input injection and exposes JSON/JSONL `inspect`, `query`, `diff`, and `reproduce` operations for a developer or coding agent.
- A bounded capture that dropped events is explicitly `incomplete`; every assertion result plus error/fatal records receive retention priority, but a partial capture cannot prove the absence of a problem.

`GpuReadbackBuffer` and non-blocking ring-slot `GpuReadbackTexture` are Framework primitives. The Demo uses them for compacted active-pixel validation. `DiagnosticsImageCapture` is the Framework-owned generic texture request: it records a separate Direct-queue copy, polls its fence in later frames, converts to RGBA8, records an `image.<name>` assertion from mean/non-black metrics, and writes a capture-local PNG attachment on up to two background workers. Automation finalizes after shutdown `Drain()` has resolved the last request. Device-removal failures attach `dred.txt` with the removal HRESULT, breadcrumbs, and page-fault allocations. OIDN instead prefers D3D12 shared buffers/fences -> CUDA `Quality::Fast` -> D3D12 copy-back on a matching NVIDIA adapter, with HDR readback -> CPU `Fast` -> upload retained only as a fallback. Whole-session background archival, compression, retention policy, actual shader-access validation, and broader cross-queue/lifetime invariants remain future work.

When the Demo selects OIDN, it automatically treats accumulation as active even if the manual accumulation option is off. After the configured static SPP it copies converged HDR into a D3D12 shared input buffer, signals the CUDA interop fence, runs the CUDA `RT` filter at `Quality::Fast` on a background worker, GPU-waits for its shared output fence, and composites the persistent result on subsequent static frames. The noisy live result is not allowed to replace an uploaded result. The CPU `Fast` path is fallback-only. Camera motion, a render-input reset, denoiser selection, or resource recreation advances the OIDN generation and invalidates the held image immediately; the next static interval builds a new result. The `oidn` automation scenario checks the static hold, motion-reset boundary, and reports `backend=cuda` or `cpu_fallback`.

## Soft-shadow variants

`PathTracingPipelineController` selects either hard-shadow or soft-shadow precompiled shader artifacts for both inline ray-query compute and shader-table DXR. The runtime toggle changes pipeline variants; the shader does not branch on a soft-shadow boolean.

Directional lights use `DirectionalLightData::DirectionAndAngularRadius.w`. Point lights expose `PointLight::SourceRadius`, uploaded through `PointLightData::Attenuation.w`. Area lights already sample their rectangular emitter surface and do not need a separate hard/soft branch.

The current soft variant uses four shadow samples. This is a sample-quality fixed preset rather than an adaptive production solution.

## Feature notes

- **Ray tracing:** inline ray query is the default path; shader-table DXR uses ray-generation, miss, and hit groups. Both share scene resources.
- **Root descriptors:** D3D12 root descriptors write a buffer GPU virtual address directly into a root-signature slot and are useful for buffer CBV/SRV/UAV bindings, but they are not the texture binding path and were used only as a diagnostic A/B during the compact-dispatch investigation.
- **Meshlets:** task-shader and compute-indirect GBuffer backends plus cluster debugging. The dynamic-scene matrix updates transforms, compacted vertices, and conservative bounds before BLAS/TLAS refit; the compute cull binds the scene bindless heap before descriptor-set binding so stress-driven descriptor changes remain valid. This is not yet a production visibility, streaming, residency, or LOD system.
- **Surface emitters:** rectangular area lights and emissive meshes are represented by reusable geometry-level triangle CDF data plus per-instance data, then uploaded with the other light buffers.
- **Raster Bloom:** Framework registers explicit prefilter, per-level downsample, per-level upsample, and composite graph passes. Its graph-owned scratch pyramid uses transient aliasing; its alias barrier is emitted at first use and the new placed resource starts from `COMMON`.
- **CUDA Bloom:** imports shared D3D12 resources/fences once and uses timeline values for D3D12-to-CUDA and CUDA-to-D3D12 ordering. It must not overwrite history or overlay resources.
- **Denoising:** NRD and SVGF are selectable sample integrations. NRD reports native D3D12 state changes back to RenderGraph; history must reset after incompatible resolution, layout, or backend changes.

## Current API boundaries

- Descriptor sets are not a full NRI-style persistent GPU descriptor-set lifetime model; GPU-visible tables are still staged by the project descriptor-heap machinery.
- Sampler reflection/binding and root constants/root descriptors are not fully unified.
- Pipeline cache keys do not yet represent every raster, compute, and DXR state dimension.
- HDR10 presentation is a display-output capability, not a complete HDR image-quality solution. It requires Windows HDR plus a compatible current output; display calibration, local adaptation, wide-gamut asset workflow, and HDR visual acceptance are not yet complete.
- Soft-shadow quality is currently fixed at four samples; no runtime quality presets or adaptive sampling are available.
- RenderGraph supports explicit Direct/Async Compute/Copy queue placement but no automatic queue selection. The maintained `Copy Queue Validation` path covers Direct HDR -> Copy -> Async Compute -> Direct and validates the required producer fences, GPU waits, state plan, batches, and retirement fences.
- `RenderGraphRoot::Execute` is now a graph entry point. `RenderGraphCommandExecutor` owns pass recording/submission, while `RenderGraphProfiler` owns optional per-queue timestamp lifetime and markers.
- Transient resources are retired from actual Direct/Async Compute fence values. Aliasing is conservative and only combines lifetimes that are proven to use the same queue; cross-queue aliasing is intentionally disabled.
- Raster Bloom scratch participates in transient aliasing. Repeated headless rebuild stress validates the first-use alias activation/state ordering.
- Pass construction uses explicit `RaytracingDemoPassResources` and `RaytracingDemoPassConfig` rather than capturing `RaytracingDemo&` or using friend access.
- Scene-to-GPU conversion is organized by four builders: texture/material, geometry, meshlet, and RTAS. `RaytracingDemoSceneResources` remains the sample-facing facade.
- Scene loading uses the static `SceneImporter::ImportFromFile()` dispatcher for `.unity`, project `.json`, `.fbx`, and supported Mitsuba `.xml`. FBX nodes, transforms, material factors/maps, external/embedded textures, cameras, and directional/point/spot/area lights are normalized into `Scene`; `SceneMeshReference::SubmeshIndex` is preferred over mesh-name matching when the demo selects a prototype. Mitsuba XML imports the perspective sensor, OBJ geometry and transforms, rectangle area emitters, and top-level spot emitters. Its supported BSDF properties and base-color texture bindings map to per-BSDF internal PBR materials; only unsupported or unresolved references use the default PBR fallback.
- `SceneImportOptions::GenerateFallbackCamera` is intended for direct FBX assets without a camera. `RequireCamera` remains available for strict tools/tests. The importer selects one active camera, and Spot Lights flow through `LightingGpuResources` into Inline PT/ReSTIR DI/GI direct-light sampling rather than a point-light fallback. Advanced transparency/clearcoat/transmission and animation playback are not implied by the FBX importer.
- `UnitySceneDump <scene.{unity,json,fbx,xml}> [--allow-missing-camera]` is a no-window developer-tool smoke test that calls the already-existing `SceneImporter::ImportFromFile()` entry point. It is not part of, or a dependency of, the Unity, FBX, or Mitsuba XML importer implementations.
- DLSS/Streamline integration is experimental. Native NGX SR/DLAA and tentative Streamline RR/FG paths are present, but they have not completed supported-hardware image-quality, stability, timing, or performance validation. Runtime capability queries gate RR/FG; the current RTX 2060 development machine reports RR unavailable and cannot support FG. Do not treat this sample path as a production-ready DLSS integration.
- RR/FG interposition is an explicit process-start choice: launch with `--streamline-interposer`. The default path keeps the native D3D12 device, queues, and swapchain unproxied; enabling RR/FG later requires a restart. The sample deploys a project-owned Streamline configuration with console logging disabled.

Prefer a small, explicit framework API addition plus a clear sample usage site over increasing low-level D3D12 exposure.
