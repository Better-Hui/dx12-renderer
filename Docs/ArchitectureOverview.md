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

## Build targets and source ownership

The maintained first-party CMake targets are `DX12Library`, `Framework`, `RenderGraph`, and `RaytracingDemo`. `Demos/Common` supplies shared entry-point support but is not a standalone target. Target-local source ownership follows the repository directories: Framework owns `Framework/include`, `Framework/shaders`, `Framework/src`, and `Framework/tools`; the other targets follow the same physical-tree rule for the directories they contain.

`CMakeIncludes/ProjectBase.cmake` applies `source_group(TREE ...)` for Visual Studio without assigning virtual MSBuild `Link` paths to project-owned files. This keeps Visual Studio filters and Rider's physical project view consistent, including CMake's automatically generated regeneration item for each target-local `CMakeLists.txt`. External implementation files may be hidden from a target view, but they must not be copied into `build/` or presented as if they were owned by that target. The generated build tree is disposable and never owns editable source.

## DX12Library

`DX12Library/` is the native D3D12 boundary. Its important responsibilities are:

- `D3D12DeviceContext`, `CommandQueue`, and `CommandList` wrap the D3D12 device and Direct, Compute, and Copy command queues.
- `CommandList` is restricted to command recording, barriers, descriptor staging, and command-list lifetime tracking. `ResourceUploader` owns staging uploads and resource replacement, while `MipGenerator` owns the reusable mip-generation pipeline.
- `Resource`, `Texture`, `Buffer`, structured/raw buffers, upload buffers, RTAS backing resources, and resource views own native D3D12 allocations.
- `DescriptorAllocator`, `DynamicDescriptorHeap`, and `FrameResourceRing` manage descriptor and per-frame lifetime. GPU-visible descriptor tables are built here rather than by demo code.
- `ResourceStateRegistry` and `ResourceStateTracker` provide native transition, UAV, and aliasing-barrier tracking.
- `GpuTimestampProfiler` supplies queue-local GPU timestamp queries.
- Window, swap-chain, PIX markers, and the Unity D3D12 interop boundary also live at this layer. Optional integrations can supply the generic `D3D12RuntimeLifecycle` contract for initialization before the first D3D12/DXGI call, device attachment immediately after creation, and shutdown. `CommandQueue` and `Window` always use the normal D3D12/DXGI creation APIs and never know about a concrete feature SDK.

This layer deliberately exposes D3D12 concepts. `Framework` is responsible for presenting a narrower renderer-facing API above it.

## Framework

`Framework/` contains reusable renderer building blocks. Its intent is that a demo supplies scene data and feature policy, while Framework owns repeated GPU setup and dispatch patterns.

### Pipeline and binding APIs

- `CommandContext` records raster, compute, mesh-shader, and DXR work through the same bind/dispatch style.
- Reflection builds `PipelineLayout`, `PipelineDescriptorPool`, `PipelineDescriptorSet`, and `PipelineBindingSet` from named shader bindings.
- `BindlessDescriptorHeap` keeps canonical descriptors in a CPU-only heap and mirrors them into fence-retired shader-visible frame pages. Materials keep stable descriptor indices; `CommandContext` stages the corresponding table in the selected page for direct heap indexing. This prevents CPU descriptor updates from overwriting descriptors still consumed by Direct or Async Compute GPU work.
- `ShaderVariantManager` compiles explicitly requested variants at startup, fingerprints sources/includes/defines, and caches bytecode. It is not runtime shader hot reload or exhaustive permutation generation.
- `SharedUploadBuffer`, transient descriptor allocation, `StructuredBuffer`, raw buffers, and `RWStructuredBuffer`-style UAV binding support common data-upload and compute workloads.
- `TextureLoader` owns DirectXTex/OpenEXR decoding and texture-cache lookup, then delegates GPU staging and optional mip generation to the focused low-level services.

### Geometry, ray tracing, and scenes

- Meshlet construction and common mesh-shader data are under `Framework/Geometry` and `Framework/shaders/Meshlet`.
- `RayTracingAccelerationStructure`, `RayTracingShader`, and `RayTracingShaderTable` wrap BLAS/TLAS construction, ray-tracing pipelines, and shader-table dispatch. Scene mutation can add, remove, and update instances without rebuilding unrelated geometry.
- The shared `Scene` model carries camera, light, transform, PBR-material, and mesh data for the sample resource path.
- `SurfaceEmitter` defines the GPU representation and sampling data for rectangular area lights and emissive mesh surfaces. The scene adapter builds shared-geometry triangle CDF data plus per-instance data, avoiding one full light record per repeated triangle instance.

### Reusable rendering features

- `ReSTIRDIPass` owns the ReSTIR DI resource history, pipeline variants, and RIS, temporal, spatial, and final-shading dispatch sequence. A caller supplies the output, motion vectors, frame constants, and a scene-binding callback.
- `ReSTIRGIPass` owns packed GI reservoirs, pipeline variants, and initial-sampling, temporal, spatial, and final-shading dispatches. A caller supplies an indirect-lighting output, motion vectors, frame constants, and an inline-ray-query scene-binding callback.
- `Taa`, `NRD`, and `SVGF` provide temporal anti-aliasing and denoising integration. NRD reports its native state transitions back to RenderGraph through `RenderContext`.
- `DLSS` owns native NGX DLSS SR/DLAA evaluation and the experimental Streamline RR/FG frame-feature path. `RaytracingDemo` compiles `DLSS.cpp` and `StreamlineRuntime.cpp` as hidden external sources, so ordinary `Framework` consumers neither inherit the vendor SDK include paths nor link `sl.interposer.lib`, and CMake does not create an extra `FrameworkNvidiaFeatures` project. Framework's `StreamlineRuntime` performs `slInit` before D3D12 creation, calls `slSetD3DDevice` after device creation, owns capability queries, and requests generic presentation reconfiguration for Frame Generation. Automatic interposition owns queue/swap-chain interception; DX12Library never references Streamline or Frame Generation/Ray Reconstruction capability types. RR/FG have not completed supported-hardware validation.
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

- A pass explicitly chooses `Direct`, `AsyncCompute`, or `Copy` with `RenderPassQueue`; queue placement is not inferred automatically.
- `RenderGraphQueueScheduler` tracks a logical resource's last producer queue and submitted fence value. A dependent consumer receives a GPU-side wait before its work is submitted.
- `PassResourceStatePlan` stores immutable per-pass transition, UAV, aliasing, initialization, and async-handoff work. The executor records that plan in the command list that owns the pass; `CommandList` resolves initial transition states through the shared `ResourceStateRegistry` when lists are closed in final submission order.
- `RenderGraphPassBuilder::UseCopyQueue()` routes copy-compatible passes through the compiled plan, executor, queue scheduler, profiler, and transient retirement path. The maintained sample does not currently declare a Copy-queue pass.
- Transient resources retire against the actual Direct/Compute/Copy fence values of the frame. Aliasing is intentionally conservative: only same-queue lifetimes are reused; cross-queue aliasing remains disabled.

### Active-pixel compaction

When `RaytracingDemo` selects `CompactedIndirect`, the graph reads depth and appends valid geometry pixels to an active-pixel list with a global atomic counter. PT, ReSTIR DI, and ReSTIR GI Inline compute stages recover logical screen coordinates from that list and dispatch `ceil(activeCount / 64)` 64-thread groups. The DXR path uses a separate `D3D12_DISPATCH_RAYS_DESC` whose `Width` is `activeCount`. `ActivePixelCount` is therefore a valid-pixel count, not a ray count and not the Inline dispatch X value. Finalize, graph tokens, and readback validate that the count and indirect arguments agree.

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

### Scene importer contract

`SceneImporter::ImportFromFile()` is the format-neutral static entry point used by the demo. It dispatches `.unity` to the Unity YAML parser, `.json` to the project scene parser, and `.fbx` to the Assimp FBX scene importer. All three produce the same `Scene` contract:

```text
Scene
  -> nodes (local/world matrices and parent/child links)
  -> objects (node reference, mesh reference, stable submesh index, material index)
  -> materials (PBR factors, UV scale/offset, texture bindings)
  -> camera + directional/point/spot/area lights
```

FBX import keeps external texture paths when they resolve and copies embedded textures into an owned `SceneEmbeddedTexture` payload. The demo resource builder decodes either source through `TextureLoader`, so an FBX does not need to be converted to a Unity scene or project JSON before loading. Mesh loading uses Assimp validation, triangulation, invalid-data filtering, four-weight limiting, and 16-bit-safe large-mesh splitting; runtime checks reject invalid faces, indices, and bone references instead of relying on debug-only assertions.

### Render paths demonstrated

- GBuffer generation through ordinary raster, task/mesh shaders, or compute culling plus `ExecuteIndirect`.
- Direct lighting selected as `None`, path tracing, or inline-ray-query ReSTIR DI; indirect lighting selected as `None`, path tracing, or inline-ray-query ReSTIR GI.
- Shader-table DXR and inline ray query share the same scene geometry, materials, bindless textures, light buffers, and acceleration structures.
- Directional, point, rectangular area, and emissive surface-emitter data upload through GPU buffers. Directional and point soft shadows use precompiled shader variants; rectangular area lights sample their emitter surface.
- Optional NRD/SVGF, TAA, skybox, CUDA Bloom, native DLSS SR/DLAA, and experimental Streamline RR/FG paths compose around the core lighting outputs.

For ReSTIR DI, the graph contains one `ReSTIR DI` pass. The pass calls Framework `ReSTIRDIPass::Execute`, which records RIS, temporal resampling, boiling filtering inside the temporal shader, spatial resampling, and final visibility/shading dispatches in one command-list scope. This keeps reusable history/pipeline ownership in Framework while the demo still owns graph-level data flow and scene binding.

For ReSTIR GI, the graph instead selects one `ReSTIR GI` indirect-lighting producer. It calls Framework `ReSTIRGIPass::Execute`, which records initial BSDF sampling, temporal reservoir reuse, spatial reservoir reuse, and final visibility/shading in one command-list scope. The demo adapter supplies its GBuffer, TLAS, bindless scene data, direct-light sampling, emission, and environment contract; the feature is Inline Ray Query only.

Shader-table DXR and Inline share the scene/resource model, but ReSTIR DI/GI are currently Inline-only. A manual switch to DXR opens a compatibility popup when the selected configuration would skip those stages and keeps a red warning visible; automated backend changes intentionally avoid the modal popup.

### Diagnostics and automation

- Runtime UI groups technique selection, scene/light controls, denoising, upscaling, stress content, and debugging controls.
- `RAYTRACING_DEMO_AUTOTEST=core`, `stress`, or `matrix` runs non-interactive startup/feature-toggle coverage. It is a crash/regression smoke test, not a substitute for visual validation.
- RenderGraph timestamp CSV is useful for repeatable pass timing; PIX is required for full queue timelines.
- The current paths are Demo-specific and do not yet provide one machine-readable explanation of graph schedule, queue submissions, resource/descriptor state, assertions, readbacks, and reproduction metadata. The next tooling priority is the planned Framework-owned contract in [Framework Diagnostics, Automation, and Profiling Plan](FrameworkDiagnosticsPlan.md).

## Current boundaries

- The repository targets Windows/x64/D3D12 with Shader Model 6.8.
- Explicit async compute can reduce GPU wall time only when dependencies and hardware allow overlap; fence waits, cache pressure, and bandwidth contention can make it slower.
- Meshlets are an experimental GBuffer backend, not a complete visibility, streaming, residency, or LOD system.
- FBX scene import is now part of the Framework `SceneImporter` contract, but it intentionally selects one active camera and supports a practical PBR subset. Spot Lights are preserved in `Scene` while the current sample GPU lighting path renders them through a point-light fallback. Transparency, clearcoat, transmission, animation playback, and dynamic skinned-scene updates still require separate contracts.
- ReSTIR DI/GI, CUDA Bloom, DLSS SR/DLAA, Streamline RR/FG, and Unity interop are engineering experiments. They require per-hardware functional, image-quality, stability, memory, and performance validation before any delivery use.

For sample-facing API examples and detailed feature limitations, see [RaytracingDemo API Guide](RaytracingSampleApi.md).
