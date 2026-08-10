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
-> optional CUDA Bloom external pass
-> Display / Overlay
-> Present
```

When the Direct Lighting UI selects ReSTIR DI on the inline ray-query backend, the graph contains one framework-backed direct-light pass:

```text
Base Resources
-> ReSTIR DI
   -> RIS
   -> Temporal Reuse + Boiling Filter
   -> Spatial Reuse
   -> Final Visibility / Shade
-> Lighting Composite
```

`ReSTIRDIPass` records the internal compute dispatches in the same command-list scope. RenderGraph therefore tracks the feature as one pass and reports one graph-level timing; its internal stages share the pass scope rather than becoming independently scheduled RenderGraph passes.

### ReSTIR DI direct lighting

`Framework/Rendering/Lighting/ReSTIRDI.h` owns renderer-neutral settings and frame constants. `Framework/Rendering/Lighting/ReSTIRDIPass.h` owns persistent reservoir/history resources, shader variants, and the RIS/temporal/spatial/final dispatch sequence. The sample creates one RenderGraph pass, supplies `DirectLighting` and motion vectors, and gives the framework pass a callback that binds the sample scene contract.

`ReSTIRDISettings` currently exposes:

- RIS candidate count and optional initial visibility.
- Temporal reuse, `Off` / `Basic` / `RayTraced` temporal bias correction, a 20-frame default history-length clamp, reprojection similarity tests, and permutation sampling.
- Boiling filtering within the temporal shader, after temporal resampling and before spatial resampling.
- Spatial reuse, `Off` / `Basic` / `Pairwise` / `RayTraced` spatial bias correction, neighbor/radius settings, disocclusion boosting, and normal/depth/material similarity tests.
- Final visibility, optional final-visibility reuse, and reuse age/distance limits.

The light domain contains directional, point, rectangular area, and scene-generated emissive surface emitters. The path remains inline-ray-query only; shader-table DXR continues to use the normal direct-lighting path.

Visibility is selected by the stage-specific settings above. In particular, enabling final visibility performs visibility testing for the selected final sample; temporal/spatial `RayTraced` correction modes can additionally trace during their resampling logic. Do not treat a stage switch as a guaranteed quality or performance improvement without a visual and timing A/B on the target adapter.

This is still an experimental ReSTIR DI implementation. Its sampling policy, quality, stability, and performance are not accepted as RTXDI-equivalent, and it does not yet provide a complete production light-presampling, dynamic-scene validation, or quality-tuning system.

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

### Native resource-state handoff

Native integrations that record barriers inside a normal RenderGraph pass must report the resulting state through `RenderContext`:

```cpp
const ResourceStateTransition transitions[] = {
    { texture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
};
context.TransitionResources(commandList, transitions);
```

NRD uses this path to batch its adapter/input/output transitions. Without a RenderGraph callback, the NRD wrapper retains a raw D3D12 fallback for standalone use. External passes such as CUDA Bloom remain explicit queue-boundary operations with their own shared-fence protocol.

This is an explicit queue API, not an automatic multi-queue scheduler. It does not decide queue placement, split or batch passes, optimize overlap, or schedule a Copy queue. A pass may become slower when dependencies expose its compute tail or when graphics and compute contend for GPU execution/cache/bandwidth.

## Profiling

RenderGraph timing captures queue-local GPU timestamp samples and exports CSV history. Use it for repeated fixed-scene A/B pass-duration measurements.

Do not infer global overlap from separate queue timestamp origins. Use PIX Timing Capture for the real queue timeline:

1. Capture the same scene, camera, resolution, and runtime state with Async Compute disabled and enabled.
2. Select the `Graphics` lane configuration and show `Direct Command Queue` plus `Compute Command Queue`.
3. Compare overlap, GPU-side waits, periods where both queues are idle, and CPU submission only when the GPU timeline indicates a bubble.

PIX is the authority for cross-queue wall-clock overlap; CSV is the lightweight repeatable companion measurement.

## Scene workflow

`SceneImporter::ImportFromFile` selects the parser from extension: `.unity` for Unity text scenes and `.json` for JSON scenes. The default demo scene is `Assets/Scenes/Sponza.unity`; its model, textures, and Unity `.meta` files are repository-local under `Assets/Models/Sponza`.

`RaytracingDemoSceneResources::LoadScene` adapts a `Scene` into textures, materials, geometry, meshlet buffers, acceleration structures, and emissive surface-emitter sampling data. The current renderer only has a PBR material path; an imported non-PBR material receives the fallback PBR-like material rather than a water, particle, or custom Unity shader implementation.

The default sample still appends C++ stress-test spheres for renderer load testing; the runtime scene is therefore not yet fully data-authored. The UI toggle uses incremental add/remove handles: static meshlet geometry and existing BLAS data are reused, while meshlet instance buffers and the TLAS are updated.

Current importer limits: no full prefab/nested-prefab support, no complete `SkinnedMeshRenderer` or `LODGroup`, no Unity asset-database cache/live sync, and no automatic coordinate-system conversion. `UnitySceneDump` inspects supported scenes without launching the renderer.

## Soft-shadow variants

`PathTracingPipelineController` selects either hard-shadow or soft-shadow precompiled shader artifacts for both inline ray-query compute and shader-table DXR. The runtime toggle changes pipeline variants; the shader does not branch on a soft-shadow boolean.

Directional lights use `DirectionalLightData::DirectionAndAngularRadius.w`. Point lights expose `PointLight::SourceRadius`, uploaded through `PointLightData::Attenuation.w`. Unity scenes can provide `m_ShadowAngle` / `m_ShadowRadius`, and JSON lights can provide `angularRadius` / `sourceRadius`. Area lights already sample their rectangular emitter surface and do not need a separate hard/soft branch.

The current soft variant uses four shadow samples. This is a sample-quality fixed preset rather than an adaptive production solution.

## Feature notes

- **Ray tracing:** inline ray query is the default path; shader-table DXR uses ray-generation, miss, and hit groups. Both share scene resources.
- **Meshlets:** task-shader and compute-indirect GBuffer backends plus cluster debugging. This is not yet a production visibility, streaming, residency, or LOD system.
- **Surface emitters:** rectangular area lights and emissive meshes are represented by reusable geometry-level triangle CDF data plus per-instance data, then uploaded with the other light buffers.
- **CUDA Bloom:** imports shared D3D12 resources/fences once and uses timeline values for D3D12-to-CUDA and CUDA-to-D3D12 ordering. It must not overwrite history or overlay resources.
- **Denoising:** NRD and SVGF are selectable sample integrations. NRD reports native D3D12 state changes back to RenderGraph; history must reset after incompatible resolution, layout, or backend changes.

## Current API boundaries

- Descriptor sets are not a full NRI-style persistent GPU descriptor-set lifetime model; GPU-visible tables are still staged by the project descriptor-heap machinery.
- Sampler reflection/binding and root constants/root descriptors are not fully unified.
- Pipeline cache keys do not yet represent every raster, compute, and DXR state dimension.
- Soft-shadow quality is currently fixed at four samples; no runtime quality presets or adaptive sampling are available.
- RenderGraph supports explicit async queue placement but no automatic scheduling or Copy-queue path.
- `RenderGraphRoot::Execute` is now a graph entry point. `RenderGraphCommandExecutor` owns pass recording/submission, while `RenderGraphProfiler` owns optional per-queue timestamp lifetime and markers.
- Transient resources are retired from actual Direct/Async Compute fence values. Aliasing is conservative and only combines lifetimes that are proven to use the same queue; cross-queue aliasing is intentionally disabled.
- Pass construction uses explicit `RaytracingDemoPassResources` and `RaytracingDemoPassConfig` rather than capturing `RaytracingDemo&` or using friend access.
- Scene-to-GPU conversion is organized by four builders: texture/material, geometry, meshlet, and RTAS. `RaytracingDemoSceneResources` remains the sample-facing facade.
- DLSS/Streamline integration is experimental. Native NGX SR/DLAA and tentative Streamline RR/FG paths are present, but they have not completed supported-hardware image-quality, stability, timing, or performance validation. Runtime capability queries gate RR/FG; the current RTX 2060 development machine reports RR unavailable and cannot support FG. Do not treat this sample path as a production-ready DLSS integration.
- RR/FG interposition is an explicit process-start choice: launch with `--streamline-interposer`. The default path keeps the native D3D12 device, queues, and swapchain unproxied; enabling RR/FG later requires a restart. The sample deploys a project-owned Streamline configuration with console logging disabled.

Prefer a small, explicit framework API addition plus a clear sample usage site over increasing low-level D3D12 exposure.
