# DX12 Renderer

> An experimental DirectX 12 renderer and Windows sample collection for exploring renderer architecture, RenderGraph execution, ray tracing, meshlets, and CUDA/D3D12 interop.

[中文文档](README.zh-CN.md) · [Architecture Overview](Docs/ArchitectureOverview.md) · [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md)

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
| Material shading | Framework-owned metallic/roughness GGX PBR evaluation, with an experimental `Stylized Comic` PBR-NPR variant selected by the sample UI. |
| ReSTIR DI | Inline ray-query direct-lighting sample with RIS, temporal/boiling/spatial resampling, stage-specific visibility and bias-correction settings, and final shading. |
| ReSTIR GI | Inline ray-query one-bounce indirect-lighting sample with initial BSDF sampling, temporal reuse, spatial reuse, Jacobian correction, and final visibility/shading. |
| Meshlets | Meshlet generation/GPU resources, task-shader and compute-indirect GBuffer backends, and incremental instance-buffer updates. |
| Denoising and interop | NRD/SVGF sample paths, RenderGraph-aware NRD resource-state handoff, and CUDA Bloom using D3D12 shared resources with external fence/semaphore synchronization. |
| Experimental frame features | Native NGX DLSS SR/DLAA plus Streamline Ray Reconstruction and Frame Generation integration. These paths are experimental and not validated for delivery. |
| Investigation | PIX scopes, RenderGraph timing history/CSV export, and runtime UI controls. |

## Repository layout

| Path | Purpose |
| --- | --- |
| `DX12Library/` | Low-level D3D12 wrappers: command queues/lists, resources, synchronization, descriptor heaps, application, and swap chain. |
| `Framework/` | Scene, geometry, materials, pipeline/binding abstractions, ray tracing, denoising, meshlets, and CUDA interop. |
| `RenderGraph/` | Pass/resource declarations, dependency ordering, resource-state planning, queue synchronization, and timing integration. |
| `Demos/RaytracingDemo/` | Primary maintained sample and the best entry point for current API usage. |
| `Demos/*` | Additional historical or focused samples. Useful references, but not the main integration target. |
| `Assets/` | Demo scenes, textures, and runtime sample assets. |
| `External/` | Third-party integrations. DLSS, NRI, and NRD are pinned Git submodules; each component retains its own license and redistribution terms. |
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

Queue submission and last-writer fence tracking live in `RenderGraphQueueScheduler`. The compiler emits immutable per-pass transition/aliasing plans; the executor records them into the owning command list. `CommandList` resolves each list's initial state against the shared `ResourceStateRegistry` in final submission order, so CPU recording order never changes GPU resource ordering.

`RenderGraphRoot` receives its device and queues from the application composition root. Its execution path is split into `RenderGraphCommandExecutor` for pass recording/submission and `RenderGraphProfiler` for optional per-queue GPU timestamps. `RaytracingDemo` follows the same boundary: `RaytracingDemoPassResources` supplies object references and `RaytracingDemoPassConfig` supplies explicit runtime configuration, so pass lambdas do not capture the whole demo or use friend access.

### Rendering features

The stress-sphere UI toggle exercises incremental scene mutation: meshlet geometry and existing BLAS data are reused while instance data and the TLAS are updated.

| Feature | Demonstrated usage |
| --- | --- |
| Base resources | GBuffer-style normal/depth/material data, motion/world-position data, history/display resources, and raster or meshlet GBuffer generation. |
| Ray tracing | Inline ray-query compute shaders and shader-table DXR. |
| Material shading | Framework-owned GGX metallic/roughness PBR, plus the sample-selectable `Stylized Comic` PBR-NPR variant. |
| Lighting | Separate direct and indirect lighting producers followed by composition, including ReSTIR DI for direct lighting and ReSTIR GI for inline-ray-query indirect lighting. |
| Soft shadows | Precompiled hard/soft shader variants for directional and point lights; area lights retain their sampled emitter surface. |
| Denoising | Optional NRD or SVGF integration. |
| DLSS and Streamline | Experimental NGX DLSS SR/DLAA plus Streamline RR/FG resource preparation. Capability queries and startup configuration decide whether a path is available. |
| Meshlets | Task-shader and compute-indirect GBuffer backends with cluster debugging. |
| CUDA Bloom | External D3D12/CUDA post process with shared-resource and shared-fence synchronization. |
| Profiling | PIX scopes and RenderGraph GPU timestamp history exported as CSV. |
| Runtime scene changes | Incremental stress-instance add/remove without rebuilding all meshlet geometry or every BLAS. |

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
vcpkg install --triplet x64-windows assimp directxtex directxmesh imgui meshoptimizer
```

Set `VCPKG_ROOT` before configuring, or pass `CMAKE_TOOLCHAIN_FILE` explicitly.

### Checked-in and integrated dependencies

| Component | Location / provisioning | Role |
| --- | --- | --- |
| DirectX Agility SDK 1.619.5 | `D3D12/`, `External/AgilitySDK/include/` | Runtime redistributable and matching C++ headers for the SM6.8 baseline. |
| DirectX Shader Compiler | `DXC/dxc.exe` when present; otherwise a Windows SDK `dxc.exe` | Compiles ray-tracing, task, mesh, compute, and other sample shaders. |
| WinPixEventRuntime | `WinPixEventRuntime/` | PIX CPU/GPU event markers. |
| NVIDIA NRD / NRI | Git submodules at `External/NRD/` and `External/NRI/` | Denoising integration and its API layer. CMake builds the D3D12 libraries from the pinned upstream commits. |
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

The current pinned revisions are DLSS `v310.7.0`, NRI `v180`, and NRD `4.17.4`. NRI and NRD are built from source by CMake. Their upstream CMake files may download build-only dependencies such as D3D12 Memory Allocator, MathLib, and ShaderMake into the build directory; those files are not committed to this repository. Streamline remains a separately provisioned SDK package because its official source repository does not contain the runtime DLL set required by this sample.

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

## Startup shader compilation and variants

`RaytracingDemo` now requests its graphics, compute, mesh/task, and DXR shaders through the Framework `ShaderVariantManager`. This is a **startup-time** workflow, not runtime hot reload: edit an `.hlsl` or referenced `.hlsli`, launch the demo again, and the affected requested variants compile before their pipelines are created.

- In `auto` mode, the manager locates the development source root from `DX12_RENDERER_SHADER_SOURCE_ROOT`, the nearby `CMakeCache.txt`, or the current working tree.
- It recursively fingerprints the root source, resolved `#include` files, target profile, entry point, `Defines`, include directories, compiler arguments, and the local `dxcompiler.dll` identity.
- Cache entries are written beside the executable by default: `Saved/ShaderCache/<variant>.<fingerprint>.cso` plus a readable `.meta` dependency record.
- A matching fingerprint loads bytecode directly from disk. A changed source/include/define/profile compiles a new entry through in-process DXC.
- `off` disables source compilation and uses the packaged `Shaders/*.cso` fallback. If source is available and DXC reports an error, startup fails with DXC diagnostics instead of silently running stale bytecode.

Variants are **explicitly declared, automatically compiled, and automatically cached**. The system intentionally does not enumerate every theoretical `#define` combination: that would produce an unbounded permutation explosion. For example, `PathTracingPipelineController` declares Hard and `RAYTRACING_DEMO_SOFT_SHADOWS=1` variants from the same source file; no soft-shadow wrapper source is needed by the runtime path.

The startup compiler currently covers the shaders directly owned by `RaytracingDemo`. Framework shaders embedded as generated headers and legacy demos still use their existing build-time compilation paths.

## Current limitations

- Only Windows/x64/D3D12 is supported.
- `RaytracingDemo` is a maintained integration sample, not a production renderer or public API compatibility promise.
- CUDA 12.8 is required at configure time even when CUDA Bloom is disabled at runtime; fully optional CUDA remains build-system work.
- Async compute is **explicitly assigned per pass**. The graph does not automatically choose queues, split passes, optimize overlap, or schedule a Copy queue.
- Consecutive async passes are currently submitted per pass rather than batch-scheduled as a larger compute segment.
- `RenderGraphRoot::Execute` is now a thin graph entry point; `RenderGraphCommandExecutor` owns pass recording/submission and `RenderGraphProfiler` owns optional direct/async timestamp lifetimes. Graph build/topology orchestration remains in `RenderGraphRoot`.
- Transient resources are retired using the actual Direct/Async Compute fence values recorded for the frame. Aliasing is deliberately conservative: resources used by different queues are not aliased until a more general multi-queue allocator is designed.
- Device and queue state is injected through the application composition root for the current Framework and RenderGraph execution paths. Standalone application/window lifecycle code and a small set of legacy resource-wrapper compatibility paths still retain `Application` dependencies.
- `RaytracingDemoSceneResources` exposes four internal builders for texture/material, geometry, meshlet, and RTAS resources. The facade remains sample-facing while scene mutation updates meshlet/TLAS instances incrementally.
- Per-queue RenderGraph timestamps are useful for pass duration; PIX Timing Capture is required to inspect cross-queue wall-clock overlap, waits, and GPU bubbles.
- Meshlet rendering is an experimental GBuffer backend, not a complete visibility/streaming system or a claim of optimal meshlet performance.
- `Stylized Comic` is an experimental stylized-PBR/PBR-NPR material evaluation. It retains metallic, roughness, and GGX material inputs while applying banded diffuse response, cool shadow tint, and graphic highlights. It is not a complete Spider-Verse reproduction: outlines, halftones, print misregistration, hatching, and temporal stylization are outside this material model.
- ReSTIR DI is an experimental inline-ray-query direct-lighting sample. Its light sampling, emissive surface emitters, temporal/spatial reuse, and visibility-test options continue to evolve; image quality, stability, and performance have not been accepted as an RTXDI-equivalent implementation.
- ReSTIR GI is an experimental inline-ray-query indirect-lighting sample adapted from the ReSTIR GI data flow in [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT). It currently targets one-bounce transport, uses persistent packed reservoirs, and has build/automation coverage only; visual quality, temporal stability, memory use, and performance still require target-hardware validation.
- Soft shadows currently use a fixed four-sample variant. Directional lights use angular radius, point lights use source radius, and adaptive sampling or quality presets are not implemented yet.
- Shader variants compile only during startup/pipeline creation. Runtime source hot reload, background compilation, and a project-wide variant manifest are not implemented.
- **DLSS, Ray Reconstruction, and Frame Generation are experimental and not validated for delivery.** Native NGX SR/DLAA is wired into the sample, and Streamline RR/FG integration is under active evaluation. RR/FG require an application restart with `--streamline-interposer`; this opt-in keeps the normal D3D12 device, queues, and swapchain free of Streamline proxies. The implementation has build/startup and automation safety coverage only. It has not completed image-quality, stability, timing, or performance validation on supported RR/FG hardware, and unknown functional or integration issues may remain. Runtime capability queries are authoritative: on the current RTX 2060 development machine, FG is unsupported by the adapter and RR reports unavailable for the active adapter. Do not treat any DLSS mode in this repository as guaranteed usable or production-ready.

## Documentation and notices

- [Architecture Overview](Docs/ArchitectureOverview.md) maps the DX12Library, Framework, RenderGraph, and RaytracingDemo responsibilities and data flow.
- [RaytracingDemo API Guide](Docs/RaytracingSampleApi.md) explains sample APIs, RenderGraph behavior, profiling, and boundaries.
- Keep maintained sample passes framework-facing; avoid raw D3D12 calls where an existing API covers the operation.
- This fork preserves upstream and third-party notices. Review the upstream project and vendored license files before use or redistribution; this README introduces no replacement repository-wide license.
- `External/DLSS/`, `External/NRI/`, `External/NRD/`, and the NVIDIA components used by `External/Streamline/` retain their upstream terms. A Git submodule link does not transfer or replace those terms. Keeping an SDK under `External/` does not make it open source and does not grant a sublicense. Preserve all notices and licenses, do not treat this repository as a standalone SDK mirror, and obtain a legal/license review before publishing source, redistributing binaries, or making a commercial release that includes these components.
