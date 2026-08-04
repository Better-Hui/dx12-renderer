# RaytracingDemo API Guide

`RaytracingDemo` is the maintained integration sample for this repository. It demonstrates **this repository's current renderer and framework APIs**; it is not a claim that every wrapper is final or that this API shape is universally optimal.

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

### Explicit async compute

```cpp
RenderGraph::RenderPass::Create(
    L"Example Async Compute", inputs, outputs, execute,
    RenderGraph::RenderPassQueue::AsyncCompute);
```

Current behavior:

- Direct and Async Compute queues are created and profiled separately.
- `RenderGraphQueueScheduler` records the last writer queue and submitted fence value for each logical resource.
- `RenderGraphResourceStateTracker` owns the graph's current-state table and pending transition/UAV/aliasing barriers.
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

`SceneImporter::ImportFromFile` selects the parser from extension: `.unity` for Unity text scenes and `.json` for JSON scenes. The default demo scene is `Assets/Scenes/DefaultScene.json`.

`RaytracingDemoSceneResources::LoadScene` adapts a `Scene` into textures, materials, geometry, meshlet buffers, and RTAS. A fallback PBR-like material is used when an imported object lacks a supported material.

The default sample still appends C++ stress-test spheres for renderer load testing; the runtime scene is therefore not yet fully data-authored. The UI toggle uses incremental add/remove handles: static meshlet geometry and existing BLAS data are reused, while meshlet instance buffers and the TLAS are updated.

Current importer limits: no full prefab/nested-prefab support, no complete `SkinnedMeshRenderer` or `LODGroup`, no Unity asset-database cache/live sync, and no automatic coordinate-system conversion. `UnitySceneDump` inspects supported scenes without launching the renderer.

## Feature notes

- **Ray tracing:** inline ray query is the default path; shader-table DXR uses ray-generation, miss, and hit groups. Both share scene resources.
- **Meshlets:** task-shader and compute-indirect GBuffer backends plus cluster debugging. This is not yet a production visibility, streaming, residency, or LOD system.
- **CUDA Bloom:** imports shared D3D12 resources/fences once and uses timeline values for D3D12-to-CUDA and CUDA-to-D3D12 ordering. It must not overwrite history or overlay resources.
- **Denoising:** NRD and SVGF are selectable sample integrations. NRD reports native D3D12 state changes back to RenderGraph; history must reset after incompatible resolution, layout, or backend changes.

## Current API boundaries

- Descriptor sets are not a full NRI-style persistent GPU descriptor-set lifetime model; GPU-visible tables are still staged by the project descriptor-heap machinery.
- Sampler reflection/binding and root constants/root descriptors are not fully unified.
- Pipeline cache keys do not yet represent every raster, compute, and DXR state dimension.
- RenderGraph supports explicit async queue placement but no automatic scheduling or Copy-queue path.
- `RenderGraphRoot::Execute` still combines pass execution and profiler orchestration even though queue and resource-state ownership have been split into dedicated components.
- Transient-resource aliasing/lifetime planning is not yet fully queue-fence-aware.
- DLSS/Streamline is not integrated.

Prefer a small, explicit framework API addition plus a clear sample usage site over increasing low-level D3D12 exposure.
