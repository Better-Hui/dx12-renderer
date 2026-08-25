# DX12 Renderer

> An experimental DirectX 12 renderer and Windows sample collection for exploring renderer architecture, RenderGraph execution, ray tracing, meshlets, and CUDA/D3D12 interop.

[中文文档](README.zh-CN.md) · [Architecture Overview](Docs/ArchitectureOverview.md) · [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md) · [Framework Diagnostics](Docs/FrameworkDiagnosticsPlan.md)

## Fork and attribution

This repository is a fork of [Delt06/dx12-renderer](https://github.com/Delt06/dx12-renderer). The shared history begins at upstream commit `1db3a62d3eb7b8e0ff228090cca8dcf8e7e6adc9`.

The upstream renderer remains the foundation. This fork adds framework and sample experiments; it is not an official continuation of the upstream project and does not claim API or performance parity with other renderers. Preserve upstream notices and review the third-party license notices before redistribution.

## Extensions in this fork

| Area | Added or extended here |
| --- | --- |
| D3D12 foundation | `DX12Library` extends the upstream wrappers around device contexts, queues, command lists, resource states, fences, descriptor allocation, swap chains, and application lifetime. `CommandList` records commands; focused uploader and mip-generator services own data preparation. Raw D3D12 ownership and synchronization live here rather than in feature code. |
| Framework API | `CommandContext`, reflection-driven pipeline layouts, named descriptor-set bindings, bindless descriptor submission, DXR helpers, mesh-shader pipeline support, and reusable rendering features provide the normal path for demo-facing code. |
| RenderGraph | Logical pass/resource declarations are compiled into immutable ordering, resource-state, aliasing, queue-dependency, and execution plans. The executor owns barriers, queue waits, submission, and optional per-queue timing instead of asking each demo pass to implement raw DX12 synchronization. |
| RaytracingDemo | The maintained integration sample. It demonstrates this repository's layered APIs: feature authors declare graph inputs/outputs and record through Framework, while raw `ID3D12Device`, command-list, root-signature, descriptor-heap, barrier, and fence work stays below the demo boundary unless an abstraction is genuinely missing. |
| Ray tracing | Runtime-selectable inline ray-query and shader-table DXR paths sharing the same scene/resource model; shader-table DXR does not execute ReSTIR DI/GI stages that currently exist only for Inline. |
| Compacted dispatch | Depth-filtered active-pixel list with atomic append; Inline compute uses `ceil(activeCount / 64)` thread groups, while compacted DXR uses `Width = activeCount`. |
| Material shading | Framework-owned metallic/roughness GGX PBR evaluation, with an experimental `Stylized Comic` PBR-NPR variant selected by the sample UI. |
| ReSTIR DI | Inline ray-query direct-lighting sample with RIS, temporal/boiling/spatial resampling, stage-specific visibility and bias-correction settings, and final shading. |
| ReSTIR GI | Inline ray-query one-bounce indirect-lighting sample with initial BSDF sampling, temporal reuse, spatial reuse, Jacobian correction, and final visibility/shading. |
| Meshlets | Meshlet generation/GPU resources, task-shader and compute-indirect GBuffer backends, plus declared dynamic transform/geometry synchronization. |
| Post process and interop | Framework-owned NRD/SVGF/TAA and raster-Bloom RenderGraph subgraphs, plus CUDA Bloom using D3D12 shared resources with external fence/semaphore synchronization. |
| Experimental frame features | Native NGX DLSS SR/DLAA plus Framework-owned Streamline Ray Reconstruction and Frame Generation integration. DX12Library exposes only a generic pre-device/post-device runtime lifecycle; queue and swap-chain creation remain ordinary D3D12/DXGI code while the linked Streamline interposer performs interception. These paths are experimental and not validated for delivery. |
| Diagnostics | PIX scopes, RenderGraph timing history/CSV, runtime UI controls, and an optional Framework-owned machine-readable capture/automation/query/diff/reproduction loop for developers and coding agents. |

## Repository layout

| Path | Purpose |
| --- | --- |
| `DX12Library/` | Low-level D3D12 wrappers: command queues/lists, resources, synchronization, descriptor heaps, application, and swap chain. |
| `Framework/` | Scene, geometry, materials, pipeline/binding abstractions, ray tracing, denoising, meshlets, and CUDA interop. |
| `RenderGraph/` | Pass/resource declarations, dependency ordering, resource-state planning, queue synchronization, and timing integration. |
| `CMakeIncludes/` | Shared target setup, third-party provisioning, shader build rules, and generated-project organization. |
| `Demos/Common/` | Shared standalone-demo entry-point support used by `RaytracingDemo`. |
| `Demos/RaytracingDemo/` | Primary maintained sample and the best entry point for current API usage. |
| `Assets/` | Demo scenes, textures, and runtime sample assets. |
| `External/` | Third-party integrations. Dear ImGui, DLSS, NRI, NRD, and [Intel Open Image Denoise (OIDN)](https://github.com/OpenImageDenoise/oidn) are pinned Git submodules; each component retains its own license and redistribution terms. |
| `Docs/` | Architecture and sample API notes. |

## What `RaytracingDemo` demonstrates

`RaytracingDemo` demonstrates **the APIs of this repository**. These abstractions are still evolving and are not claimed to be universally optimal renderer APIs. The sample shows the intended usage of the current code in this fork.

### Framework-facing command recording

```cpp
CommandContext commands(commandList);
commands.BindPipeline(computeShader);
commands.BindDescriptorSet(computeShader.GetDescriptorSet());
commands.Dispatch(groupCountX, groupCountY, 1);
```

The same style is used for raster, compute, mesh-shader, and DXR paths through `BindPipeline`, `BindDescriptorSet`, `Draw`, `Dispatch`, `DispatchMesh`, and `DispatchRays`. Root signatures, raw descriptor tables, and native descriptor heaps belong in lower layers unless the framework lacks a needed abstraction.

### RenderGraph and explicit async compute

```cpp
builder.AddComputePass<PassData>(
    L"Example Compute",
    [=](RenderGraph::RenderGraphPassBuilder& pass, PassData&) {
        pass.ReadTexture(inputId);
        pass.WriteUav(outputId);
    },
    [](const PassData&, const RenderGraph::RenderContext& context, CommandList& commandList) {
        // Record commands through Framework using resources resolved from context.
    });
```

Pass input/output declarations drive ordering, resource states, and cross-queue waits. `AddPass` creates a Direct pass; `AddComputePass` selects Async Compute when available and otherwise falls back to Direct; `AddCopyPass` requires a Copy queue; `AddExternalPass` is always Direct. The graph tracks each resource's producer queue and submitted fence value, and inserts GPU-side waits for dependent consumers. The unattended `Copy Queue Validation` path exercises `Direct HDR -> Copy -> Async Compute -> Direct` and asserts the producer fence, waits, state plan, submission batches, and resource-retirement fences. Inline-ray-query `Indirect Lighting` is the current production async-compute sample path.

Queue submission and last-writer fence tracking live in `RenderGraphQueueScheduler`. The compiler emits immutable per-pass transition/aliasing plans; the executor records them into the owning command list. `CommandList` resolves each list's initial state against the shared `ResourceStateRegistry` in final submission order, so CPU recording order never changes GPU resource ordering.

`RenderGraphRoot` receives its device and queues from the application composition root. Its execution path is split into `RenderGraphCommandExecutor` for pass recording/submission and `RenderGraphProfiler` for optional per-queue GPU timestamps. `RaytracingDemo` follows the same boundary: `RaytracingDemoPassResources` supplies object references and `RaytracingDemoPassConfig` supplies explicit runtime configuration, so pass lambdas do not capture the whole demo or use friend access.

Reusable Framework features register subgraphs through `AddPasses(RenderGraphBuilder&, Inputs)`. Auto Exposure, ReSTIR DI/GI, raster Bloom, NRD, SVGF, OIDN, and TAA all use this model; the builder exists only during graph construction and is never stored by Framework. Framework-owned persistent resources, including the SVGF/TAA ping-pong histories, are imported with distinct logical read/write graph IDs; their physical buffer resolves per frame while graph topology remains stable. Construction-scoped scratch textures use `RenderGraphBuilder::CreateTexture()` and `Discard`. `CommandList` and `CommandContext` expose no manual barrier API. The build-time ownership check scans first-party DX12Library, RenderGraph, Framework, and Demo sources and rejects ordinary-feature bypasses.

NRD is visible to the graph as `Prepare Inputs`, `Native Denoise`, and `Composite`. Native NRI/NRD recording may manage SDK-internal temporary states, but graph resources enter and leave that segment in the declared SRV/UAV states; NRD is not a barrier allowlist exception. SVGF registers imported parity-aware temporal history, horizontal/vertical A-Trous passes, and Composite. TAA registers Resolve and History Copy around imported ping-pong history; its physical read/write selection stays fixed for the complete graph execution and advances only at the rendered-frame boundary. A UAV clear no longer appends a hidden barrier; a later write to the same UAV must be represented by another pass or an explicit graph WAW dependency.

OIDN is a static-image path. Selecting it automatically enables effective accumulation, so the ordinary `Accumulation` checkbox does not need to be enabled. On a matching NVIDIA adapter, once accumulation reaches the selected static SPP, `OIDN HDR Readback` copies converged HDR `HistoryColor` into a D3D12 shared input buffer, the Direct queue signals an imported fence, and a background `std::jthread` runs the CUDA `RT` filter with `Quality::Fast`. The Direct queue then GPU-waits on the CUDA fence before `OIDN Result Upload` copies the shared output buffer to its persistent imported texture; no HDR image crosses CPU memory. CUDA initialization or external-memory import failure selects the explicit CPU `Quality::Fast` readback/filter/upload fallback. `OIDN Composite` writes `SceneColor`, and the result is held on every static frame. Camera motion, a real scene/input reset, algorithm changes, or resource recreation advance a generation, immediately invalidate the held result, and start new accumulation; stale work is discarded. `RAYTRACING_DEMO_AUTOTEST=oidn` records `backend=cuda` or `cpu_fallback` and validates static holding plus camera-motion invalidation without input injection.

Raster Bloom is visible to RenderGraph as `Bloom Prefilter`, per-level `Bloom Downsample`, per-level `Bloom Upsample`, and `Bloom Composite`. Its graph-owned pyramid participates in transient-heap aliasing. The allocator records each alias barrier at the physical resource's first use and registers the newly placed resource as `COMMON`, which removes the prior repeated-rebuild device hang without a dedicated-resource workaround.

### Rendering features

| Feature | Demonstrated usage |
| --- | --- |
| Base resources | GBuffer-style normal/depth/material data, motion/world-position data, history/display resources, and raster or meshlet GBuffer generation. |
| Scene assets | `SceneImporter::ImportFromFile()` accepts `.unity`, project `.json`, and `.fbx`. FBX import preserves node hierarchy, local/world transforms, PBR factors/maps, external or embedded textures, cameras, and directional/point/spot/area lights. |
| Ray tracing | Inline ray-query compute shaders and shader-table DXR, with a compatibility popup/red warning when the selected DXR configuration skips Inline-only stages. |
| Compacted dispatch | Active-pixel compaction, indirect compute/DXR dispatch finalization, and readback diagnostics for active count and dispatch arguments. |
| Material shading | Framework-owned GGX metallic/roughness PBR, plus the sample-selectable `Stylized Comic` PBR-NPR variant. |
| Lighting | Separate direct and indirect lighting producers followed by composition, including ReSTIR DI for direct lighting and ReSTIR GI for inline-ray-query indirect lighting. |
| Soft shadows | Precompiled hard/soft shader variants for directional and point lights; area lights retain their sampled emitter surface. |
| Denoising | Optional NRD/SVGF temporal integration plus CUDA OIDN `Quality::Fast` with D3D12 shared-memory interop for converged static accumulation; CPU `Fast` is fallback only. |
| Raster Bloom | Framework-owned RenderGraph subgraph with explicit prefilter, downsample, upsample, and composite stages. |
| DLSS and Streamline | Experimental NGX DLSS SR/DLAA plus Streamline RR/FG resource preparation. Capability queries and startup configuration decide whether a path is available. |
| Meshlets | Task-shader and compute-indirect GBuffer backends with cluster debugging. |
| CUDA Bloom | External D3D12/CUDA post process with shared-resource and shared-fence synchronization. |
| Profiling | PIX scopes and RenderGraph GPU timestamp history exported as CSV. |

### Scene import and direct FBX use

The sample resolves its scene through one static entry point:

```cpp
SceneImportOptions options;
options.GenerateFallbackCamera = true;
const SceneImportResult result = SceneImporter::ImportFromFile(scenePath, options);
```

`SceneImporter` dispatches by case-insensitive extension. Unity YAML and project JSON keep their existing asset conventions; FBX is imported as a complete scene through Assimp. A node that instances several meshes creates one `SceneObject` per submesh and stores the original Assimp mesh index in `SceneMeshReference::SubmeshIndex`, so the demo does not depend on duplicate or missing mesh names. Embedded FBX textures are decoded from memory by `TextureLoader` and participate in the same cache as file textures.

The importer preserves Spot Light data in `Scene`. The current sample lighting GPU contract has directional, point, and area producers only, so `SceneLightManager` temporarily adds a point-light fallback for a Spot Light while retaining the original spot data for scene round-tripping. Only the first active FBX camera is selected; `GenerateFallbackCamera` frames the imported world-space bounds when an asset has no camera. Transparency, clearcoat, transmission, animation playback, and other advanced material models remain outside this sample contract.

For a no-window parser check, enable the developer tool and run:

```text
UnitySceneDump <scene.unity|scene.json|scene.fbx> [--allow-missing-camera]
```

## Requirements

| Requirement | Notes |
| --- | --- |
| Platform | Windows 10/11, x64. This is a Windows/D3D12 project. |
| Toolchain | Visual Studio 2022 with the MSVC C++ desktop toolchain and a Windows SDK. |
| CMake | CMake 3.22 or newer; the current NRI/NRD source builds require this baseline. |
| GPU/driver | A D3D12 GPU/driver that reports **Shader Model 6.8**. DXR, mesh shaders, and CUDA paths need the relevant hardware/driver support. |
| CUDA | CUDA Toolkit **12.8** is currently required because `Framework` and `RaytracingDemo` build CUDA interop/Bloom. |

### Shader Model 6.8 baseline

The project compiles raster, compute, task/mesh, and DXR libraries with DXC at Shader Model 6.8 (`vs/ps/cs/as/ms/lib_6_8`). It ships the DirectX Agility SDK **1.619.5** runtime in `D3D12/` and its C++ headers in `External/AgilitySDK/include/`; CMake verifies the redistributable, and startup rejects drivers that do not report Shader Model 6.8.

### vcpkg packages

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh meshoptimizer
```

Set `VCPKG_ROOT` before configuring, or pass `CMAKE_TOOLCHAIN_FILE` explicitly.

### Checked-in and integrated dependencies

| Component | Location / provisioning | Role |
| --- | --- | --- |
| DirectX Agility SDK 1.619.5 | `D3D12/`, `External/AgilitySDK/include/` | Runtime redistributable and matching C++ headers for the SM6.8 baseline. |
| DirectX Shader Compiler | `DXC/dxc.exe` when present; otherwise a Windows SDK `dxc.exe` | Compiles ray-tracing, task, mesh, compute, and other sample shaders. |
| WinPixEventRuntime | `WinPixEventRuntime/` | PIX CPU/GPU event markers. |
| Dear ImGui | Git submodule at `External/ImGui/`, pinned to `v1.91.9` | Runtime debug UI. Framework compiles the official sources directly and keeps repository-specific numeric widget behavior in `Framework/UI/NumericWidgets`. |
| NVIDIA NRD / NRI | Git submodules at `External/NRD/` and `External/NRI/` | Denoising integration and its API layer. CMake builds the D3D12 libraries from the pinned upstream commits. |
| [Intel Open Image Denoise (OIDN)](https://github.com/OpenImageDenoise/oidn) | Git submodule at `External/OIDN/`, pinned to `v2.5.1` | Static-image denoiser. CMake builds CPU and CUDA device modules in an isolated tree attached privately to `Framework`; the demo deploys the OIDN CPU/CUDA runtime DLLs without adding an OIDN solution project. |
| NVIDIA DLSS SDK | Git submodule at `External/DLSS/` | Experimental native NGX SR/DLAA integration. The SDK's own license and notices are provided by the submodule. |
| NVIDIA Streamline | `External/Streamline/` | Experimental RR/FG integration and runtime interposer. Preserve `license.txt`, `nvngx_dlss.license.txt`, and `3rd-party-licenses.md`. |
| Unity PluginAPI | `External/UnityPluginAPI/` | Headers for Unity-facing D3D12 interop experiments. |
| CUDA Driver API | CUDA Toolkit 12.8 | Builds Bloom PTX and provides `cuda.h` / `cuda.lib`. |

## Build and run

### Git submodules

The repository stores links to upstream third-party repositories instead of copying their source or SDK files into the parent repository:

```powershell
git clone --recurse-submodules https://github.com/best-Hui/dx12-renderer.git
cd dx12-renderer
git submodule update --init --recursive
```

The current pinned revisions are Dear ImGui `v1.91.9`, DLSS `v310.7.0`, NRI `v180`, NRD `4.17.4`, and [OIDN `v2.5.1`](https://github.com/OpenImageDenoise/oidn/tree/v2.5.1). Dear ImGui, NRI, NRD, and OIDN CPU/CUDA modules build from source. OIDN remains isolated in `build/ThirdParty/OIDN`, requires oneTBB (`vcpkg install tbb:x64-windows`), the provisioned ISPC executable, and an installed CUDA toolkit for its CUDA module. `CMakeIncludes/BuildOidn.cmake` resolves the toolkit from `CUDA_PATH` or `DX12_RENDERER_OIDN_CUDA_TOOLKIT_ROOT`; the work remains a `Framework` pre-build command and does not create a solution project. NRI and NRD's upstream CMake files may download build-only dependencies such as D3D12 Memory Allocator, MathLib, and ShaderMake into the build directory; those files are not committed to this repository. Streamline remains a separately provisioned SDK package because its official source repository does not contain the runtime DLL set required by this sample.

Run from the repository root. The example uses a sibling build directory.

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8'

cmake -S . -B ..\build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DDX12_RENDERER_CUDA_TOOLKIT_ROOT="$cudaRoot"
cmake --build ..\build --config Release --target RaytracingDemo

& ..\build\bin\Release\RaytracingDemo.exe
```

### AI and automation diagnostics

`RendererDiagnostics` is a developer-only target and is disabled by default, so it does not change the normal solution project set. Opt in and build it explicitly:

```powershell
cmake -S . -B ..\build -DDX12_RENDERER_BUILD_DEVELOPER_TOOLS=ON
cmake --build ..\build --config Release --target RendererDiagnostics RaytracingDemo
```

The tool sources physically live under `Framework/tools/` and are always shown under that directory in the `Framework` project. `DX12_RENDERER_BUILD_DEVELOPER_TOOLS` controls whether the tool targets are generated and built; it does not move or hide the real source tree behind virtual folders.

The tool launches registered Demo scenarios without mouse or keyboard injection. JSON/JSONL output and stable exit codes support an agent loop of run, inspect, focused query, baseline diff, and reproduction:

```powershell
$tool = '..\build\bin\Release\RendererDiagnostics.exe'
$demo = '..\build\bin\Release\RaytracingDemo.exe'
$capture = '..\build\bin\Release\Saved\Diagnostics\stress-001'

& $tool run --exe $demo --scenario stress --output $capture
& $tool inspect $capture
& $tool query $capture --category command_queue --limit 100
& $tool diff <baseline-capture> $capture
& $tool reproduce $capture --execute
```

`inspect` distinguishes `passed`, `failed`, and `incomplete`. The event buffer is bounded; dropped events, a non-terminal manifest, or unknown assertions produce exit code `12` and partial confidence instead of treating missing evidence or unresolved invariants as a clean result. See [Framework Diagnostics](Docs/FrameworkDiagnosticsPlan.md).

### Generated Visual Studio and Rider layout

The generated projects intentionally mirror each target's physical source tree. `DX12Library`, `Framework`, `RenderGraph`, and `RaytracingDemo` show their target-local `CMakeLists.txt` at the project root, followed by the real `include/`, `shaders/`, `src/`, and optional `tools/` directories. CMake's `source_group(TREE ...)` supplies Visual Studio filters, while Rider reads the same unmodified physical source paths.

The build system deliberately does not assign MSBuild `Link` metadata to project-owned files. Mixing virtual linked paths with CMake's physical `CMakeLists.txt` regeneration item makes Rider create a duplicate target-named folder. Generated `.vcxproj`, `.filters`, shader outputs, dependency build trees, libraries, and executables belong under `build/`; editable C++, HLSL, and CUDA source must remain in the repository source tree. The build directory is disposable and should be regenerated from CMake rather than edited by hand.

## Startup shader compilation and variants

`RaytracingDemo` now requests its graphics, compute, mesh/task, and DXR shaders through the Framework `ShaderVariantManager`. This is a **startup-time** workflow, not runtime hot reload: edit an `.hlsl` or referenced `.hlsli`, launch the demo again, and the affected requested variants compile before their pipelines are created.

- In `auto` mode, the manager locates the development source root from `DX12_RENDERER_SHADER_SOURCE_ROOT`, the nearby `CMakeCache.txt`, or the current working tree.
- It recursively fingerprints the root source, resolved `#include` files, target profile, entry point, `Defines`, include directories, compiler arguments, and the local `dxcompiler.dll` identity.
- Cache entries are written beside the executable by default: `Saved/ShaderCache/<variant>.<fingerprint>.cso` plus a readable `.meta` dependency record.
- A matching fingerprint loads bytecode directly from disk. A changed source/include/define/profile compiles a new entry through in-process DXC.
- `off` disables source compilation and uses the packaged `Shaders/*.cso` fallback. If source is available and DXC reports an error, startup fails with DXC diagnostics instead of silently running stale bytecode.

Variants are **explicitly declared, automatically compiled, and automatically cached**. The system intentionally does not enumerate every theoretical `#define` combination: that would produce an unbounded permutation explosion. For example, `PathTracingPipelineController` declares Hard and `RAYTRACING_DEMO_SOFT_SHADOWS=1` variants from the same source file; no soft-shadow wrapper source is needed by the runtime path.

The startup compiler currently covers the shaders directly owned by `RaytracingDemo`. Framework shaders embedded as generated headers continue to use their build-time compilation path.

## Current limitations

- Only Windows/x64/D3D12 is supported.
- `RaytracingDemo` is a maintained integration sample, not a production renderer or public API compatibility promise.
- CUDA 12.8 is required at configure time even when CUDA Bloom is disabled at runtime; fully optional CUDA remains build-system work.
- Direct, Async Compute, and Copy queue placement is **explicitly assigned per pass**. The graph does not automatically choose queues, split passes, or optimize overlap. `Copy Queue Validation` is a maintained diagnostic sample path, not an automatic queue-placement policy.
- The compiler merges consecutive same-queue Async Compute/Copy passes into a non-direct recording/submission batch when their resource handoffs are compatible. Aliasing or a required intermediate Direct preamble still splits the batch.
- `RenderGraphRoot::Execute` is now a thin graph entry point; `RenderGraphCommandExecutor` owns pass recording/submission and `RenderGraphProfiler` owns optional Direct/Async Compute/Copy timestamp lifetimes. Graph build/topology orchestration remains in `RenderGraphRoot`.
- Transient resources are retired using the actual Direct/Async Compute/Copy fence values recorded for the frame. Aliasing is deliberately conservative: resources used by different queues are not aliased until a more general multi-queue allocator is designed.
- Raster Bloom scratch textures use transient aliasing again. Its alias barrier is emitted at the first real use, before the first transition, and the placed resource begins from `COMMON`; repeated headless rebuild stress validates this ordering.
- Device and queue state is injected through the application composition root for the current Framework and RenderGraph execution paths. Standalone application/window lifecycle code and a small set of legacy resource-wrapper compatibility paths still retain `Application` dependencies.
- `RaytracingDemoSceneResources` exposes four internal builders for texture/material, geometry, meshlet, and RTAS resources. The facade remains sample-facing.
- The `rtas` scenario covers the base dynamic-RTAS path. The stricter unattended `dynamic-scene` matrix keeps Meshlet GBuffer enabled for both task-shader and compute-indirect backends: it uploads the ordinary and compacted Meshlet vertices, recomputes conservative Meshlet bounds, updates transform/instance buffers, refits the dirty BLAS, updates the existing TLAS, and verifies the restore frame. An active emissive target also refreshes emissive-mesh surface-emitter data. Runtime skinning output is not implemented; skinned dynamic updates are explicitly reported as unsupported rather than silently falling back to stale Meshlet/RTAS data.
- `meshlet-indirect` is the focused unattended regression for the Compute Indirect backend. It requires Meshlet GBuffer + indirect backend with direct/indirect lighting and denoising disabled, then toggles stress instances. Its critical cull path binds the scene bindless descriptor heap before it binds the descriptor set; this prevents updated Meshlet transform SRVs from resolving through a stale heap/table after stress expansion.
- `ActivePixelCount` is the number of valid geometry pixels, not the number of rays. Inline compacted compute finalizes `{ ceil(activeCount / 64), 1, 1 }`; the UI separately reports launched compute threads and shader-table DXR ray-generation invocations. Padding threads are guarded out; the useful work reduction comes from removing full lighting/ray-query invocations for inactive pixels.
- Switching manually to shader-table DXR opens a compatibility popup when a selected lighting stage is Inline-only and keeps a red warning visible while the incompatible selection remains. Automated backend changes intentionally skip the modal popup so unattended tests cannot block.
- Per-queue RenderGraph timestamps are useful for pass duration; PIX Timing Capture is required to inspect cross-queue wall-clock overlap, waits, and GPU bubbles.
- Meshlet rendering is an experimental GBuffer backend, not a complete visibility/streaming system or a claim of optimal meshlet performance.
- `Stylized Comic` is an experimental stylized-PBR/PBR-NPR material evaluation. It retains metallic, roughness, and GGX material inputs while applying banded diffuse response, cool shadow tint, and graphic highlights. It is not a complete Spider-Verse reproduction: outlines, halftones, print misregistration, hatching, and temporal stylization are outside this material model.
- ReSTIR DI is an experimental inline-ray-query direct-lighting sample. Its light sampling, emissive surface emitters, temporal/spatial reuse, and visibility-test options continue to evolve; image quality, stability, and performance have not been accepted as an RTXDI-equivalent implementation.
- ReSTIR GI is an experimental inline-ray-query indirect-lighting sample adapted from the ReSTIR GI data flow in [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT). It currently targets one-bounce transport, uses persistent packed reservoirs, and has build/automation coverage only; visual quality, temporal stability, memory use, and performance still require target-hardware validation.
- Active-pixel compaction is implemented for PT Direct/Indirect and ReSTIR DI/GI compacted consumers. Its cost/benefit boundary still needs validation across active-pixel densities and target adapters.
- Soft shadows currently use a fixed four-sample variant. Directional lights use angular radius, point lights use source radius, and adaptive sampling or quality presets are not implemented yet.
- `RaytracingDemoShaderPipelineController` owns shader/pipeline bootstrap and reset. Topology-changing feature state, runtime parameters, and unattended automation remain in their respective controllers; runtime source hot reload, background compilation, and a project-wide variant manifest are not implemented.
- **DLSS, Ray Reconstruction, and Frame Generation are experimental and not validated for delivery.** Native NGX SR/DLAA is wired into the sample, and Streamline RR/FG integration is under active evaluation. RR/FG require an application restart with `--streamline-interposer`; this opt-in keeps the normal D3D12 device, queues, and swapchain free of Streamline proxies. The implementation has build/startup and automation safety coverage only. It has not completed image-quality, stability, timing, or performance validation on supported RR/FG hardware, and unknown functional or integration issues may remain. Runtime capability queries are authoritative: on the current RTX 2060 development machine, FG is unsupported by the adapter and RR reports unavailable for the active adapter. Do not treat any DLSS mode in this repository as guaranteed usable or production-ready.

## Documentation and notices

- [Architecture Overview](Docs/ArchitectureOverview.md) maps the DX12Library, Framework, RenderGraph, and RaytracingDemo responsibilities and data flow.
- [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md) explains sample APIs, RenderGraph behavior, profiling, and boundaries.
- [Framework Diagnostics](Docs/FrameworkDiagnosticsPlan.md) documents the implemented machine-readable capture, deterministic automation, agent-oriented evidence queries, invariants, reproduction, and profiling contract, plus the remaining DRED/readback/retention work.
- Keep maintained sample passes framework-facing; avoid raw D3D12 calls where an existing API covers the operation.
- This fork preserves upstream and third-party notices. Review the upstream project and vendored license files before use or redistribution; this README introduces no replacement repository-wide license.
- `External/DLSS/`, `External/NRI/`, `External/NRD/`, and the NVIDIA components used by `External/Streamline/` retain their upstream terms. [OIDN](https://github.com/OpenImageDenoise/oidn) is distributed upstream under the Apache License 2.0; preserve `External/OIDN/LICENSE.txt`. A Git submodule link does not transfer or replace those terms. Keeping an SDK under `External/` does not make it open source and does not grant a sublicense. Preserve all notices and licenses, do not treat this repository as a standalone SDK mirror, and obtain a legal/license review before publishing source, redistributing binaries, or making a commercial release that includes these components.
