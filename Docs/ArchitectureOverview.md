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

`Framework/tools/` is always shown in the `Framework` project as its physical source directory; project-owned tool files are never remapped through MSBuild `Link` metadata. `Framework/tools/CMakeLists.txt` creates the `RendererDiagnostics` and `UnitySceneDump` targets only when `DX12_RENDERER_BUILD_DEVELOPER_TOOLS=ON`. The option controls whether those targets build, not whether `Framework` exposes its real `tools/` tree, so the default solution keeps the tools visible without adding developer-tool projects.

## DX12Library

`DX12Library/` is the native D3D12 boundary. Its important responsibilities are:

- `D3D12DeviceContext`, `CommandQueue`, and `CommandList` wrap the D3D12 device and Direct, Compute, and Copy command queues.
- `CommandList` is restricted to ordinary command recording, descriptor staging, and command-list lifetime tracking; it exposes no transition/UAV/aliasing barrier methods. `CommandListInternalAccess` is the renderer-infrastructure barrier encoder used by RenderGraph plus audited upload, readback, mip, swapchain/present, shared-upload, and RTAS boundaries. `ResourceUploader` owns staging uploads and resource replacement, while `MipGenerator` owns the reusable mip-generation pipeline.
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

- `AutoExposure`, `ReSTIRDIPass`, `ReSTIRGIPass`, raster `Bloom`, `NRD`, `SVGF`, and `TAA` expose `AddPasses(RenderGraphBuilder&, Inputs)` and register their logical stages directly. The builder is a construction-time argument and is never retained by Framework.
- ReSTIR persistent reservoirs/history and Auto Exposure histogram/adaptation storage are imported into the graph with logical resource IDs. Framework still owns their allocations, while RenderGraph owns topology, state planning, UAV ordering, and queue hazards.
- Construction-scoped feature scratch textures are declared with `RenderGraphBuilder::CreateTexture()` and released to the composition root as graph texture descriptions. Raster Bloom uses this for its downsample/upsample pyramid; the resources are graph-owned and transient-aliasable. The allocator emits the alias barrier at the placed resource's first actual use and registers that resource as `COMMON` before the graph's first transition.
- `NRD` registers Prepare, native Denoise, and Composite. The SDK may manage temporary state inside the native segment, but graph resources retain their RG-declared boundary states and NRD does not hand-write their barriers. `SVGF` registers imported parity-aware temporal history, horizontal/vertical A-Trous, and Composite; `TAA` registers Resolve and History Copy around imported ping-pong history. Distinct logical read/write IDs keep the graph acyclic while the physical history mapping advances only between rendered frames.
- `DLSS` owns native NGX DLSS SR/DLAA evaluation and the experimental Streamline RR/FG frame-feature path. `RaytracingDemo` compiles `DLSS.cpp` and `StreamlineRuntime.cpp` as hidden external sources, so ordinary `Framework` consumers neither inherit the vendor SDK include paths nor link `sl.interposer.lib`, and CMake does not create an extra `FrameworkNvidiaFeatures` project. Framework's `StreamlineRuntime` performs `slInit` before D3D12 creation, calls `slSetD3DDevice` after device creation, owns capability queries, and requests generic presentation reconfiguration for Frame Generation. Automatic interposition owns queue/swap-chain interception; DX12Library never references Streamline or Frame Generation/Ray Reconstruction capability types. RR/FG have not completed supported-hardware validation.
- CUDA interop wraps shared D3D12 resources and external fence/semaphore synchronization. CUDA Bloom is the current consumer.
- `Framework/Diagnostics` owns machine-readable capture sessions, the typed event schema, bounded buffering, deterministic automation, and artifact export. `DX12Library` and `RenderGraph` only receive an optional non-owning telemetry sink and never depend upward on Framework; the Demo only registers its controls, observations, and scenarios.

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

The dependency direction is `DX12Library <- RenderGraph <- Framework <- RaytracingDemo`. Framework may register reusable subgraphs, but RenderGraph never depends upward on Framework. Before Framework builds, `VerifyRenderGraphOwnership` scans first-party DX12Library, RenderGraph, Framework, and Demo sources. It rejects ordinary-feature barriers, upper-layer `ResourceStateTracker` access, Demo inclusion of the internal bridge, resource-state policy in descriptor binding, reintroduced auto-barrier mechanisms, and stored Framework builder references. The bridge allowlist contains only nine exact boundary files; `CommandList.cpp`, `CommandContext.cpp`, and NRD are not exceptions.

### Queues and synchronization

- `AddPass`, `AddComputePass`, `AddCopyPass`, and `AddExternalPass` make queue intent explicit at pass creation: Direct, Async Compute (with Direct fallback when unavailable), Copy (required), and Direct external interop respectively.
- `RenderGraphQueueScheduler` tracks a logical resource's last producer queue and submitted fence value. A dependent consumer receives a GPU-side wait before its work is submitted.
- `PassResourceStatePlan` stores immutable per-pass transition, UAV, aliasing, initialization, and async-handoff work. The executor records that plan in the command list that owns the pass; `CommandList` resolves initial transition states through the shared `ResourceStateRegistry` when lists are closed in final submission order.
- `ClearUnorderedAccessUint` records only the clear and never appends a hidden UAV barrier. A later write to the same resource must be a separate pass or otherwise form an explicit graph WAW dependency so the compiler owns UAV ordering.
- `AddCopyPass()` routes copy-compatible passes through the compiled plan, executor, queue scheduler, profiler, and transient retirement path. `Copy Queue Validation` is the maintained sample path: Direct HDR producer -> Copy queue -> Async Compute consumer -> Direct consumer, with Diagnostics assertions for planned states, producer fence/waits, submissions, and retirement fences.
- The compiler merges consecutive same-queue Async Compute/Copy passes into a non-direct batch when their Direct preambles and aliasing relationships are compatible; incompatible resource handoffs start a new batch.
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
- The unattended `rtas` scenario covers the base dynamic-RTAS path. `dynamic-scene` is the full current matrix: task-shader and compute-indirect Meshlet GBuffer each update ordinary vertices, compacted Meshlet vertices and bounds, transform/instance buffers, dirty BLAS, and the existing TLAS through declared graph accesses, then verify a restore frame. An emissive target refreshes mesh-surface emitter data. Runtime skinning output remains explicitly unsupported, never an implicit stale-data fallback.
- Directional, point, rectangular area, and emissive surface-emitter data upload through GPU buffers. Directional and point soft shadows use precompiled shader variants; rectangular area lights sample their emitter surface.
- Optional NRD/SVGF, TAA, skybox, Framework raster Bloom, CUDA Bloom, native DLSS SR/DLAA, and experimental Streamline RR/FG paths compose around the core lighting outputs.

For ReSTIR DI, the Demo calls Framework `ReSTIRDIPass::AddPasses`. Framework registers `Initial Sampling`, `Temporal Resampling`, `Boiling Filter`, `Spatial Resampling`, and `Shade` as separate graph passes connected by tokens and imported reservoir/history resources. The Demo supplies logical scene inputs and runtime resolvers; it does not schedule the internal stages or encode their barriers.

For ReSTIR GI, `ReSTIRGIPass::AddPasses` registers `Initial Sampling`, `Temporal Resampling`, `Spatial Resampling`, and `Shade` separately. Imported ping-pong history resources resolve from the runtime frame index without storing the graph builder. The Demo adapter supplies its GBuffer, TLAS, bindless scene data, direct-light sampling, emission, and environment contract; the feature is Inline Ray Query only.

For raster Bloom, `Bloom::AddPasses` registers `Bloom Prefilter`, one pass per downsample level, one pass per upsample level, and `Bloom Composite`. The Demo supplies graph resource IDs, resolution expressions, runtime parameters, and pyramid depth; it does not allocate the pyramid or encode its barriers. CUDA Bloom remains a separate Demo-owned external queue/semaphore path.

`NRD::AddPasses` registers `NRD Prepare Inputs`, `NRD Native Denoise`, and `NRD Composite`. RenderGraph prepares noisy/input SRVs and the output UAV at the native boundary. NRI/NRD uses `restoreInitialState` to restore those states after its internal work, so native recording is not an ordinary-feature barrier exception.

`SVGF::AddPasses` registers parity-aware Temporal, a Horizontal/Vertical pair for every A-Trous iteration, and Composite. Its color/moment history is Framework-owned imported ping-pong storage. The A-Trous iteration count is part of the Demo topology key, so a UI change rebuilds the graph instead of changing a stale runtime value. `TAA::AddPasses` similarly registers Resolve and History Copy over Framework-owned imported ping-pong storage, forces the history weight to zero until the first history capture completes, and advances its physical history index only in `OnRenderedFrame()` so imported resolvers remain stable throughout execution.

Shader-table DXR and Inline share the scene/resource model, but ReSTIR DI/GI are currently Inline-only. A manual switch to DXR opens a compatibility popup when the selected configuration would skip those stages and keeps a red warning visible; automated backend changes intentionally avoid the modal popup.

### Diagnostics and automation

- Runtime UI groups technique selection, scene/light controls, denoising, upscaling, stress content, and debugging controls.
- `RAYTRACING_DEMO_AUTOTEST=core`, `stress`, or `matrix` is registered by the Demo as a named Framework automation scenario. It changes state through controls and observations without desktop input injection. These remain crash/regression smoke tests rather than visual acceptance.
- The optional `RendererDiagnostics` developer tool provides `run`, `inspect`, `query`, `diff`, `reproduce`, and `selftest`. One capture correlates RenderGraph schedule/state/lifetime, queue submissions/fences, resource/descriptor identity, assertions, CPU/GPU timing, and reproduction metadata.
- `inspect` emits an agent-oriented JSON verdict, capture completeness, suspected domain, hypothesis, correlated evidence, and suggested next action. Dropped events or a non-terminal capture are `incomplete`, never a clean pass.
- RenderGraph timestamp CSV supports repeatable pass timing. Per-queue timestamps are not a calibrated global overlap timeline; full queue timing and driver behavior still require PIX or RenderDoc.

## Current boundaries

- The repository targets Windows/x64/D3D12 with Shader Model 6.8.
- Explicit async compute can reduce GPU wall time only when dependencies and hardware allow overlap; fence waits, cache pressure, and bandwidth contention can make it slower.
- Meshlets are an experimental GBuffer backend, not a complete visibility, streaming, residency, or LOD system.
- FBX scene import is now part of the Framework `SceneImporter` contract, but it intentionally selects one active camera and supports a practical PBR subset. Spot Lights are preserved in `Scene` while the current sample GPU lighting path renders them through a point-light fallback. Transparency, clearcoat, transmission, animation playback, and runtime skinning output still require separate contracts; the dynamic-scene capability reports skinned updates as unsupported until its GPU output can drive raster, Meshlet, and BLAS data together.
- `GpuReadbackBuffer` and non-blocking ring-slot `GpuReadbackTexture` are available to Framework features; compacted active-pixel validation and OIDN use them in production sample paths. OIDN automatically accumulates when selected, holds the uploaded static result, and advances its generation on camera/render-input changes before rebuilding. Diagnostics still needs its own generic request API, image assertions, DRED attachments, background writing, compression, and retention policy. High-event captures can be large; use `--max-events` and `dropped_event_count` to reason about evidence completeness.
- ReSTIR DI/GI, CUDA Bloom, DLSS SR/DLAA, Streamline RR/FG, and Unity interop are engineering experiments. They require per-hardware functional, image-quality, stability, memory, and performance validation before any delivery use.

For sample-facing API examples and detailed feature limitations, see [RaytracingDemo API Guide](RaytracingSampleApi.md).
