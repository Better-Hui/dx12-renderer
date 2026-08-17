# DX12 Renderer Architecture Overview

This document describes the current architecture of this fork. It is a map of the code that exists today, not a production-readiness claim or a promise of stable public APIs.

## Layering

```text
RaytracingDemo
    sample policy, scene selection, UI, RenderGraph topology
        |
Framework
    renderer-facing APIs and reusable rendering features
        |
RenderGraph
    pass dependencies, resource plans, recording and submission
        |
DX12Library
    native D3D12 resources, queues, synchronization and swap chain
```

Each layer may depend on the layer below it. New sample features should normally be expressed through `Framework` and `RenderGraph`, rather than adding raw D3D12 descriptor, root-signature, or fence code to a demo pass.

## DX12Library

`DX12Library/` is the native D3D12 boundary. Its important responsibilities are:

- `D3D12DeviceContext`, `CommandQueue`, and `CommandList` wrap the D3D12 device and Direct, Compute, and Copy command queues.
- `Resource`, `Texture`, `Buffer`, structured/raw buffers, upload buffers, RTAS backing resources, and resource views own native D3D12 allocations.
- `DescriptorAllocator`, `DynamicDescriptorHeap`, and `FrameResourceRing` manage descriptor and per-frame lifetime. GPU-visible descriptor tables are built here rather than by demo code.
- `ResourceStateRegistry` and `ResourceStateTracker` provide native transition, UAV, and aliasing-barrier tracking.
- `GpuTimestampProfiler` supplies queue-local GPU timestamp queries.
- Window, swap-chain, PIX markers, Streamline runtime startup, and the Unity D3D12 interop boundary also live at this layer.

This layer deliberately exposes D3D12 concepts. `Framework` is responsible for presenting a narrower renderer-facing API above it.

## Framework

`Framework/` contains reusable renderer building blocks. Its intent is that a demo supplies scene data and feature policy, while Framework owns repeated GPU setup and dispatch patterns.

### Pipeline and binding APIs

- `CommandContext` records raster, compute, mesh-shader, and DXR work through the same bind/dispatch style.
- Reflection builds `PipelineLayout`, `PipelineDescriptorPool`, `PipelineDescriptorSet`, and `PipelineBindingSet` from named shader bindings.
- `BindlessDescriptorHeap` keeps canonical descriptors in a CPU-only heap and mirrors them into fence-retired shader-visible frame pages. Materials keep stable descriptor indices; `CommandContext` stages the corresponding table in the selected page for direct heap indexing. This prevents CPU descriptor updates from overwriting descriptors still consumed by Direct or Async Compute GPU work.
- `ShaderVariantManager` compiles explicitly requested variants at startup, fingerprints sources/includes/defines, and caches bytecode. It is not runtime shader hot reload or exhaustive permutation generation.
- `SharedUploadBuffer`, transient descriptor allocation, `StructuredBuffer`, raw buffers, and `RWStructuredBuffer`-style UAV binding support common data-upload and compute workloads.

### Geometry, ray tracing, and scenes

- Meshlet construction and common mesh-shader data are under `Framework/Geometry` and `Framework/shaders/Meshlet`.
- `RayTracingAccelerationStructure`, `RayTracingShader`, and `RayTracingShaderTable` wrap BLAS/TLAS construction, ray-tracing pipelines, and shader-table dispatch. Scene mutation can add, remove, and update instances without rebuilding unrelated geometry.
- The shared `Scene` model carries camera, light, transform, PBR-material, and mesh data for the sample resource path.
- `SurfaceEmitter` defines the GPU representation and sampling data for rectangular area lights and emissive mesh surfaces. The scene adapter builds shared-geometry triangle CDF data plus per-instance data, avoiding one full light record per repeated triangle instance.

### Reusable rendering features

- `ReSTIRDIPass` owns the ReSTIR DI resource history, pipeline variants, and RIS, temporal, spatial, and final-shading dispatch sequence. A caller supplies the output, motion vectors, frame constants, and a scene-binding callback.
- `ReSTIRGIPass` owns packed GI reservoirs, pipeline variants, and initial-sampling, temporal, spatial, and final-shading dispatches. A caller supplies an indirect-lighting output, motion vectors, frame constants, and an inline-ray-query scene-binding callback.
- `Taa`, `NRD`, and `SVGF` provide temporal anti-aliasing and denoising integration. NRD reports its native state transitions back to RenderGraph through `RenderContext`.
- `DLSS` owns native NGX DLSS SR/DLAA setup and the experimental Streamline RR/FG integration boundary. RR/FG require startup interposition and have not completed supported-hardware validation.
- CUDA interop wraps shared D3D12 resources and external fence/semaphore synchronization. CUDA Bloom is the current consumer.

Framework modules are reusable building blocks, but their interfaces are still evolving. They should not be interpreted as a compatibility layer comparable to a mature public rendering SDK.

## RenderGraph

`RenderGraph/` converts logical pass/resource declarations into an executable plan.

```text
RenderPass declarations
    -> RenderGraphCompiler
    -> CompiledRenderGraph / RenderGraphExecutionPlan
    -> RenderGraphCommandExecutor
    -> D3D12 command queues
```

The compiler performs pass culling, dependency ordering, resource-state planning, transient-lifetime planning, and execution-batch construction. `RenderGraphCommandExecutor` records and submits the compiled work. `RenderGraphProfiler` owns optional Direct/Compute timestamp allocation and CSV history.

### Queues and synchronization

- A pass explicitly chooses `Direct` or `AsyncCompute` with `RenderPassQueue`; queue placement is not inferred automatically.
- `RenderGraphQueueScheduler` tracks a logical resource's last producer queue and submitted fence value. A dependent consumer receives a GPU-side wait before its work is submitted.
- `PassResourceStatePlan` stores immutable per-pass transition, UAV, aliasing, initialization, and async-handoff work. The executor records that plan in the command list that owns the pass; `CommandList` resolves initial transition states through the shared `ResourceStateRegistry` when lists are closed in final submission order.
- The low-level Copy queue exists, but RenderGraph does not yet schedule Copy-queue passes.
- Transient resources retire against the actual Direct/Compute fence values of the frame. Aliasing is intentionally conservative: only same-queue lifetimes are reused; cross-queue aliasing remains disabled.

### Recording model

The compiler groups consecutive explicitly parallel-safe Direct passes into recording batches, including passes with GPU resource dependencies. `RenderGraphTaskScheduler` supplies persistent workers and drains already accepted work during shutdown; every worker uses an exclusive command allocator/list and temporary descriptor allocation. The compiler's stable topological order is retained for submission, and pending aliasing barriers are emitted before their first-use transitions. A bindless frame page is retained by both Direct and Async Compute fences, so descriptor-table mirroring never overwrites a page still referenced by GPU work. Parallel recording therefore lowers CPU recording cost without making one Direct queue execute GPU work in parallel.

Use PIX Timing Capture to evaluate queue overlap, GPU waits, and CPU/GPU bubbles. RenderGraph CSV timing is intentionally queue-local and is best used for repeated fixed-scene A/B measurements.

## RaytracingDemo

`Demos/RaytracingDemo/` is the maintained integration sample. It is intentionally where feature selection, UI, scene choice, and graph topology live; reusable GPU mechanisms should stay below it.

### Scene resource path

```text
Scene
    -> RaytracingDemoSceneResources
       -> texture/material builder
       -> geometry builder
       -> meshlet builder
       -> RTAS builder
    -> GPU scene buffers and bindless textures
```

`RaytracingDemoSceneResources` is a sample-facing facade over these four builders. It provides incremental stress-instance add/remove: shared geometry and existing BLAS data stay intact while meshlet instance data and the TLAS are updated.

### Render paths demonstrated

- GBuffer generation through ordinary raster, task/mesh shaders, or compute culling plus `ExecuteIndirect`.
- Direct lighting selected as `None`, path tracing, or inline-ray-query ReSTIR DI; indirect lighting selected as `None`, path tracing, or inline-ray-query ReSTIR GI.
- Shader-table DXR and inline ray query share the same scene geometry, materials, bindless textures, light buffers, and acceleration structures.
- Directional, point, rectangular area, and emissive surface-emitter data upload through GPU buffers. Directional and point soft shadows use precompiled shader variants; rectangular area lights sample their emitter surface.
- Optional NRD/SVGF, TAA, skybox, CUDA Bloom, native DLSS SR/DLAA, and experimental Streamline RR/FG paths compose around the core lighting outputs.

For ReSTIR DI, the graph contains one `ReSTIR DI` pass. The pass calls Framework `ReSTIRDIPass::Execute`, which records RIS, temporal resampling, boiling filtering inside the temporal shader, spatial resampling, and final visibility/shading dispatches in one command-list scope. This keeps reusable history/pipeline ownership in Framework while the demo still owns graph-level data flow and scene binding.

For ReSTIR GI, the graph instead selects one `ReSTIR GI` indirect-lighting producer. It calls Framework `ReSTIRGIPass::Execute`, which records initial BSDF sampling, temporal reservoir reuse, spatial reservoir reuse, and final visibility/shading in one command-list scope. The demo adapter supplies its GBuffer, TLAS, bindless scene data, direct-light sampling, emission, and environment contract; the feature is Inline Ray Query only.

### Diagnostics and automation

- Runtime UI groups technique selection, scene/light controls, denoising, upscaling, stress content, and debugging controls.
- `RAYTRACING_DEMO_AUTOTEST=core`, `stress`, or `matrix` runs non-interactive startup/feature-toggle coverage. It is a crash/regression smoke test, not a substitute for visual validation.
- RenderGraph timestamp CSV is useful for repeatable pass timing; PIX is required for full queue timelines.

## Current boundaries

- The repository targets Windows/x64/D3D12 with Shader Model 6.8.
- Explicit async compute can reduce GPU wall time only when dependencies and hardware allow overlap; fence waits, cache pressure, and bandwidth contention can make it slower.
- Meshlets are an experimental GBuffer backend, not a complete visibility, streaming, residency, or LOD system.
- ReSTIR DI/GI, CUDA Bloom, DLSS SR/DLAA, Streamline RR/FG, and Unity interop are engineering experiments. They require per-hardware functional, image-quality, stability, memory, and performance validation before any delivery use.

For sample-facing API examples and detailed feature limitations, see [RaytracingDemo API Guide](RaytracingSampleApi.md).
