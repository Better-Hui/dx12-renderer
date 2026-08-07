# DX12 Renderer

> An experimental DirectX 12 renderer and Windows sample collection for exploring renderer architecture, RenderGraph execution, ray tracing, meshlets, scene import, and CUDA/D3D12 interop.

[中文文档](README.zh-CN.md) · [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md)

## Fork and attribution

This repository is a fork of [Delt06/dx12-renderer](https://github.com/Delt06/dx12-renderer). The shared history begins at upstream commit `1db3a62d3eb7b8e0ff228090cca8dcf8e7e6adc9`.

The upstream renderer remains the foundation. This fork adds framework and sample experiments; it is not an official continuation of the upstream project and does not claim API or performance parity with other renderers. Preserve upstream notices and review the third-party license notices before redistribution.

## Extensions in this fork

| Area | Added or extended here |
| --- | --- |
| Framework API | `CommandContext`, reflection-driven pipeline layouts, named descriptor-set bindings, bindless descriptor submission, DXR helpers, and mesh-shader pipeline support. |
| RenderGraph | Logical resources, compiled resource-state plans, centralized resource-state tracking, native/external state handoff, per-queue GPU timestamps/CSV export, and explicit Direct/Async Compute queue synchronization. |
| RaytracingDemo | The maintained integration sample. It demonstrates this repository's framework APIs rather than treating raw D3D12 calls as normal sample-facing code. |
| Ray tracing | Runtime-selectable inline ray-query and shader-table DXR paths sharing the same scene/resource model. |
| ReSTIR DI | Inline ray-query direct-lighting sample with RIS, optional temporal reuse, optional spatial reuse, and final-shading visibility. |
| Scene workflow | Shared `Scene` data, Unity/JSON import, and incremental runtime instance add/remove used by the stress-scene controls. |
| Meshlets | Meshlet generation/GPU resources, task-shader and compute-indirect GBuffer backends, and incremental instance-buffer updates. |
| Denoising and interop | NRD/SVGF sample paths, RenderGraph-aware NRD resource-state handoff, and CUDA Bloom using D3D12 shared resources with external fence/semaphore synchronization. |
| Investigation | PIX scopes, RenderGraph timing history/CSV export, `UnitySceneDump`, and runtime UI controls. |

## Repository layout

| Path | Purpose |
| --- | --- |
| `DX12Library/` | Low-level D3D12 wrappers: command queues/lists, resources, synchronization, descriptor heaps, application, and swap chain. |
| `Framework/` | Scene, geometry, materials, pipeline/binding abstractions, ray tracing, denoising, meshlets, and CUDA interop. |
| `RenderGraph/` | Pass/resource declarations, dependency ordering, resource-state planning, queue synchronization, and timing integration. |
| `Demos/RaytracingDemo/` | Primary maintained sample and the best entry point for current API usage. |
| `Demos/*` | Additional historical or focused samples. Useful references, but not the main integration target. |
| `Assets/` | Demo scenes, textures, and runtime sample assets. |
| `External/` | Checked-in third-party integration packages and headers. |
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
auto pass = RenderGraph::RenderPass::Create(
    L"Example Compute",
    { { inputId, RenderGraph::InputType::NonPixelShaderResource } },
    { { outputId, RenderGraph::OutputType::UnorderedAccess } },
    execute,
    RenderGraph::RenderPassQueue::AsyncCompute);
```

Pass input/output declarations drive ordering, resource states, and cross-queue waits. The graph currently supports explicit `Direct` and `AsyncCompute` assignment, tracks each resource's producer queue and submitted fence value, and inserts GPU-side waits for dependent consumers. Inline-ray-query `Indirect Lighting` is the current async-compute sample path.

Queue submission and last-writer fence tracking live in `RenderGraphQueueScheduler`; barrier ownership lives in `RenderGraphResourceStateTracker`. Native integrations such as NRD can batch `ResourceStateTransition` requests through `RenderContext`, so native barriers and the graph's tracked state stay synchronized.

`RenderGraphRoot` receives its device and queues from the application composition root. Its execution path is split into `RenderGraphCommandExecutor` for pass recording/submission and `RenderGraphProfiler` for optional per-queue GPU timestamps. `RaytracingDemo` follows the same boundary: `RaytracingDemoPassResources` supplies object references and `RaytracingDemoPassConfig` supplies explicit runtime configuration, so pass lambdas do not capture the whole demo or use friend access.

### Scene import and rendering features

```cpp
const SceneImportResult result = SceneImporter::ImportFromFile(scenePath);
const Scene& scene = result.SceneData;
```

`SceneImporter` accepts Unity text-serialized `.unity` files and JSON scene files. `RaytracingDemoSceneResources` adapts the imported scene into textures, materials, geometry, meshlet buffers, and acceleration structures. The stress-sphere UI toggle exercises incremental scene mutation: meshlet geometry and existing BLAS data are reused while instance data and the TLAS are updated.

| Feature | Demonstrated usage |
| --- | --- |
| Base resources | GBuffer-style normal/depth/material data, motion/world-position data, history/display resources, and raster or meshlet GBuffer generation. |
| Ray tracing | Inline ray-query compute shaders and shader-table DXR. |
| Lighting | Separate direct and indirect lighting passes followed by composition. |
| Soft shadows | Precompiled hard/soft shader variants for directional and point lights; area lights retain their sampled emitter surface. |
| Denoising | Optional NRD or SVGF integration. |
| Meshlets | Task-shader and compute-indirect GBuffer backends with cluster debugging. |
| CUDA Bloom | External D3D12/CUDA post process with shared-resource and shared-fence synchronization. |
| Profiling | PIX scopes and RenderGraph GPU timestamp history exported as CSV. |
| Runtime scene changes | Incremental stress-instance add/remove without rebuilding all meshlet geometry or every BLAS. |

## Requirements

| Requirement | Notes |
| --- | --- |
| Platform | Windows 10/11, x64. This is a Windows/D3D12 project. |
| Toolchain | Visual Studio 2022 with the MSVC C++ desktop toolchain and a Windows SDK. |
| CMake | CMake 3.8 or newer; a recent CMake version is recommended. |
| GPU/driver | A D3D12 GPU/driver that reports **Shader Model 6.9**. DXR, mesh shaders, and CUDA paths need the relevant hardware/driver support. |
| CUDA | CUDA Toolkit **12.8** is currently required because `Framework` and `RaytracingDemo` build CUDA interop/Bloom. |

### Shader Model 6.9 baseline

The project compiles raster, compute, task/mesh, and DXR libraries with DXC at Shader Model 6.9 (`vs/ps/cs/as/ms/lib_6_9`). It ships the DirectX Agility SDK **1.619.5** runtime in `D3D12/` and its C++ headers in `External/AgilitySDK/include/`; CMake verifies the redistributable, and startup rejects drivers that do not report Shader Model 6.9.

### vcpkg packages

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh imgui meshoptimizer
```

Set `VCPKG_ROOT` before configuring, or pass `CMAKE_TOOLCHAIN_FILE` explicitly.

### Checked-in and integrated dependencies

| Component | Location / provisioning | Role |
| --- | --- | --- |
| DirectX Agility SDK 1.619.5 | `D3D12/`, `External/AgilitySDK/include/` | Runtime redistributable and matching C++ headers for the SM6.9 baseline. |
| DirectX Shader Compiler | `DXC/dxc.exe` when present; otherwise a Windows SDK `dxc.exe` | Compiles ray-tracing, task, mesh, compute, and other sample shaders. |
| WinPixEventRuntime | `WinPixEventRuntime/` | PIX CPU/GPU event markers. |
| NVIDIA NRD / NRI | `External/NRD/`, `External/NRI/` | Denoising integration and its API layer/runtime binaries. |
| Unity PluginAPI | `External/UnityPluginAPI/` | Headers for Unity-facing D3D12 interop experiments. |
| CUDA Driver API | CUDA Toolkit 12.8 | Builds Bloom PTX and provides `cuda.h` / `cuda.lib`. |

## Build and run

Run from the repository root. The example uses a sibling build directory.

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8'

cmake -S . -B ..\build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DDX12_RENDERER_CUDA_TOOLKIT_ROOT="$cudaRoot"
cmake --build ..\build --config Release --target RaytracingDemo UnitySceneDump

& ..\build\bin\Release\RaytracingDemo.exe
```

The demo loads the repository-local Unity YAML scene `Assets/Scenes/Sponza.unity` by default. The required Sponza model, textures, and Unity `.meta` files are included under `Assets/Models/Sponza`. JSON scenes remain supported for compatibility; use Unity text-serialized `.unity` scenes for new scene content when possible.

`Save Scene` writes camera, skybox, and light edits into a sibling `.runtime.json` state file. That state is reapplied for either JSON or Unity YAML scenes at the next launch, without mutating the source scene.

## Startup shader compilation and variants

`RaytracingDemo` now requests its graphics, compute, mesh/task, and DXR shaders through the Framework `ShaderVariantManager`. This is a **startup-time** workflow, not runtime hot reload: edit an `.hlsl` or referenced `.hlsli`, launch the demo again, and the affected requested variants compile before their pipelines are created.

- In `auto` mode, the manager locates the development source root from `DX12_RENDERER_SHADER_SOURCE_ROOT`, the nearby `CMakeCache.txt`, or the current working tree.
- It recursively fingerprints the root source, resolved `#include` files, target profile, entry point, `Defines`, include directories, compiler arguments, and the local `dxcompiler.dll` identity.
- Cache entries are written beside the executable by default: `Saved/ShaderCache/<variant>.<fingerprint>.cso` plus a readable `.meta` dependency record.
- A matching fingerprint loads bytecode directly from disk. A changed source/include/define/profile compiles a new entry through in-process DXC.
- `off` disables source compilation and uses the packaged `Shaders/*.cso` fallback. If source is available and DXC reports an error, startup fails with DXC diagnostics instead of silently running stale bytecode.

Variants are **explicitly declared, automatically compiled, and automatically cached**. The system intentionally does not enumerate every theoretical `#define` combination: that would produce an unbounded permutation explosion. For example, `PathTracingPipelineController` declares Hard and `RAYTRACING_DEMO_SOFT_SHADOWS=1` variants from the same source file; no soft-shadow wrapper source is needed by the runtime path.

The startup compiler currently covers the shaders directly owned by `RaytracingDemo`. Framework shaders embedded as generated headers and legacy demos still use their existing build-time compilation paths.

`UnitySceneDump` inspects a supported scene without launching the renderer:

```powershell
& ..\build\bin\Release\UnitySceneDump.exe 'C:\Scenes\Example.unity'
```

## Current limitations

- Only Windows/x64/D3D12 is supported.
- `RaytracingDemo` is a maintained integration sample, not a production renderer or public API compatibility promise.
- CUDA 12.8 is required at configure time even when CUDA Bloom is disabled at runtime; fully optional CUDA remains build-system work.
- Async compute is **explicitly assigned per pass**. The graph does not automatically choose queues, split passes, optimize overlap, or schedule a Copy queue.
- Consecutive async passes are currently submitted per pass rather than batch-scheduled as a larger compute segment.
- `RenderGraphRoot::Execute` is now a thin graph entry point; `RenderGraphCommandExecutor` owns pass recording/submission and `RenderGraphProfiler` owns optional direct/async timestamp lifetimes. Graph build/topology orchestration remains in `RenderGraphRoot`.
- Transient resources are retired using the actual Direct/Async Compute fence values recorded for the frame. Aliasing is deliberately conservative: resources used by different queues are not aliased until a more general multi-queue allocator is designed.
- The Framework and RenderGraph no longer query `Application::Get()` for device/queue state. The application composition root injects device, queues, and descriptor allocation; the legacy `DemoMain` startup code remains the only Framework-level application entry dependency.
- `RaytracingDemoSceneResources` exposes four internal builders for texture/material, geometry, meshlet, and RTAS resources. The facade remains sample-facing while scene mutation updates meshlet/TLAS instances incrementally.
- Per-queue RenderGraph timestamps are useful for pass duration; PIX Timing Capture is required to inspect cross-queue wall-clock overlap, waits, and GPU bubbles.
- The Unity importer is static and limited: prefab resolution, nested prefabs, skinned meshes, `LODGroup`, and an asset-database cache are not complete.
- JSON scene import exists, but the stress-test spheres are still defined by sample C++ rather than scene data. They are enabled by default and can be added or removed incrementally from the runtime UI.
- Meshlet rendering is an experimental GBuffer backend, not a complete visibility/streaming system or a claim of optimal meshlet performance.
- ReSTIR DI is currently an inline-ray-query direct-lighting sample. It uses uniform light selection, RIS, optional temporal reuse, and optional spatial reuse, but does not use light presampling or emissive geometry. Temporal/spatial reuse deliberately performs no visibility test; the final shading pass traces one shadow ray, so disocclusions and reuse can produce visible noise.
- ReSTIR DI currently uses hard-shadow direct-light sampling. It does not yet use the directional/point-light soft-shadow variants and falls back to standard direct lighting in shader-table DXR mode.
- Soft shadows currently use a fixed four-sample variant. Directional lights use angular radius, point lights use source radius, and adaptive sampling or quality presets are not implemented yet.
- Shader variants compile only during startup/pipeline creation. Runtime source hot reload, background compilation, and a project-wide variant manifest are not implemented.
- DLSS/Streamline is not currently integrated.

## Documentation and notices

- [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md) explains sample APIs, RenderGraph behavior, scene import, profiling, and boundaries.
- Keep maintained sample passes framework-facing; avoid raw D3D12 calls where an existing API covers the operation.
- This fork preserves upstream and third-party notices. Review the upstream project and vendored license files before use or redistribution; this README introduces no replacement repository-wide license.
