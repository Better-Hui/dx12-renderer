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
| RenderGraph | Logical resources, resource-state plans, external passes, per-queue GPU timestamps/CSV export, and explicit Direct/Async Compute queue synchronization. |
| RaytracingDemo | The maintained integration sample. It demonstrates this repository's framework APIs rather than treating raw D3D12 calls as normal sample-facing code. |
| Ray tracing | Runtime-selectable inline ray-query and shader-table DXR paths sharing the same scene/resource model. |
| Scene workflow | Shared `Scene` data plus `SceneImporter` support for Unity text scenes and JSON scene descriptions. |
| Meshlets | Meshlet generation/GPU resources plus task-shader and compute-indirect GBuffer backends. |
| Denoising and interop | NRD/SVGF sample paths and CUDA Bloom using D3D12 shared resources with external fence/semaphore synchronization. |
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

### Scene import and rendering features

```cpp
const SceneImportResult result = SceneImporter::ImportFromFile(scenePath);
const Scene& scene = result.SceneData;
```

`SceneImporter` accepts Unity text-serialized `.unity` files and JSON scene files. `RaytracingDemoSceneResources` adapts the imported scene into textures, materials, geometry, meshlet buffers, and acceleration structures.

| Feature | Demonstrated usage |
| --- | --- |
| Base resources | GBuffer-style normal/depth/material data, motion/world-position data, history/display resources, and raster or meshlet GBuffer generation. |
| Ray tracing | Inline ray-query compute shaders and shader-table DXR. |
| Lighting | Separate direct and indirect lighting passes followed by composition. |
| Denoising | Optional NRD or SVGF integration. |
| Meshlets | Task-shader and compute-indirect GBuffer backends with cluster debugging. |
| CUDA Bloom | External D3D12/CUDA post process with shared-resource and shared-fence synchronization. |
| Profiling | PIX scopes and RenderGraph GPU timestamp history exported as CSV. |

## Requirements

| Requirement | Notes |
| --- | --- |
| Platform | Windows 10/11, x64. This is a Windows/D3D12 project. |
| Toolchain | Visual Studio 2022 with the MSVC C++ desktop toolchain and a Windows SDK. |
| CMake | CMake 3.8 or newer; a recent CMake version is recommended. |
| GPU/driver | D3D12-capable GPU and recent driver. DXR, mesh shaders, and CUDA paths need the relevant hardware/driver support. |
| CUDA | CUDA Toolkit **12.8** is currently required because `Framework` and `RaytracingDemo` build CUDA interop/Bloom. |

### vcpkg packages

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh imgui meshoptimizer
```

Set `VCPKG_ROOT` before configuring, or pass `CMAKE_TOOLCHAIN_FILE` explicitly.

### Checked-in and integrated dependencies

| Component | Location / provisioning | Role |
| --- | --- | --- |
| DirectX Agility SDK files | `D3D12/` | D3D12 Agility SDK files used by the project environment. |
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

The demo loads `Assets/Scenes/DefaultScene.json` by default.

| Variable | Example | Effect |
| --- | --- | --- |
| `RAYTRACING_DEMO_SCENE` | `Assets\Scenes\DefaultScene.json` | Loads a JSON or Unity scene file. |
| `RAYTRACING_DEMO_UNITY_SCENE` | `C:\Scenes\CornellBox.unity` | Compatibility variable for a Unity text scene. |
| `RAYTRACING_DEMO_MODE` | `shader-table` | Starts shader-table DXR; default is inline ray query. |
| `RAYTRACING_DEMO_DENOISER` | `nrd` or `svgf` | Selects a denoiser. |
| `RAYTRACING_DEMO_CUDA_BLOOM` | `1` | Enables CUDA Bloom. |
| `RAYTRACING_DEMO_MESHLET_GBUFFER` | `1` | Enables the meshlet GBuffer backend. |
| `RAYTRACING_DEMO_MESHLET_BACKEND` | `indirect` | Selects compute-indirect meshlet rendering; otherwise task shaders are used. |

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
- Per-queue RenderGraph timestamps are useful for pass duration; PIX Timing Capture is required to inspect cross-queue wall-clock overlap, waits, and GPU bubbles.
- The Unity importer is static and limited: prefab resolution, nested prefabs, skinned meshes, `LODGroup`, and an asset-database cache are not complete.
- JSON scene import exists, but the default sample still appends C++ stress-test spheres for renderer load testing.
- Meshlet rendering is an experimental GBuffer backend, not a complete visibility/streaming system or a claim of optimal meshlet performance.
- DLSS/Streamline is not currently integrated.

## Documentation and notices

- [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md) explains sample APIs, RenderGraph behavior, scene import, profiling, and boundaries.
- Keep maintained sample passes framework-facing; avoid raw D3D12 calls where an existing API covers the operation.
- This fork preserves upstream and third-party notices. Review the upstream project and vendored license files before use or redistribution; this README introduces no replacement repository-wide license.
