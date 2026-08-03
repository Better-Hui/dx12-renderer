# DX12 Renderer

This repository is a DirectX 12 renderer playground focused on a reusable rendering framework and a ray tracing sample.

The repository intentionally tracks only the core libraries, framework code, render graph, documentation, external integration headers, and `RaytracingDemo`. Older sample demos may still exist locally for reference, but they are not part of the tracked project anymore.

## Repository Layout

- `DX12Library`: low-level DirectX 12 wrappers for device resources, command lists, descriptors, synchronization, textures, buffers, and swap chain integration.
- `Framework`: higher-level rendering framework built on top of `DX12Library`.
- `RenderGraph`: render graph execution, pass dependency metadata, logical resources, and GPU timing support.
- `Demos/RaytracingDemo`: the current sample project. It demonstrates raster base resources, inline ray tracing, DXR ray tracing, denoising, CUDA post processing interop, ImGui controls, and the recommended framework-facing APIs.
- `Docs`: API notes and sample guidance.
- `External`: third-party integration files that are required by the framework or sample.

## RaytracingDemo

`RaytracingDemo` is the main sample and should be treated as the reference for new renderer experiments.

It currently demonstrates:

- Base resource generation for GBuffer, motion vectors, world position, scene color, history color, and display color.
- Inline ray tracing through compute shaders.
- Standard DXR pipeline support with ray generation, miss, closest-hit shaders, shader table dispatch, and per-pass bindings.
- Direct and indirect lighting passes.
- Optional accumulation.
- Optional denoising through NRD or SVGF.
- CUDA post-processing interop for bloom.
- ImGui runtime controls.
- GPU timing output for render graph passes.

The intended usage style is:

- Build pipeline state and pipeline layout from shader reflection and explicit pipeline descriptors.
- Bind resources by semantic name through framework binding sets or command context APIs.
- Avoid direct D3D12 calls in sample pass code unless the framework lacks the needed abstraction.

## Unity Scene Parser

The framework includes a lightweight Unity scene parser:

- Header: `Framework/include/Framework/Scene/SceneImporter.h`
- Source: `Framework/src/Scene/SceneYamlParser.cpp`
- Tool: `Framework/tools/UnitySceneDump.cpp`

The parser reads Unity text-serialized `.unity` scenes and extracts:

- Game objects and active state.
- Transform hierarchy with local and world transforms.
- Cameras.
- Lights.
- Mesh references.
- Renderer material references.
- Material asset names and shader references.

Unity uses Y-up world space. The parser preserves Unity coordinates as-is; renderer-side import code should explicitly decide whether to keep Unity space or convert to another convention.

Example:

```powershell
UnitySceneDump.exe "C:\Program Files\Unity\MDR\ModernDeferredRenderer\project\ModernDeferredRenderer\Assets\Scenes\CornellBox.unity"
```

Current scope is static parsing. The API is structured so later work can add scene change detection, asset database caching, prefab resolution, and live Unity plugin updates.

## Build

Generate or open the CMake build with Visual Studio or Rider. The repository currently expects a Windows DirectX 12 development environment.

Typical build command:

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "C:\Program Files\Unity\dx12-renderer-master\build" --config Release --target RaytracingDemo UnitySceneDump
```

Required components include:

- Visual Studio 2022 C++ toolchain.
- Windows SDK with DXC.
- CUDA Toolkit for CUDA interop paths.
- vcpkg dependencies used by the original framework, including `assimp`, `DirectXTex`, `DirectXMesh`, `meshoptimizer`, and `imgui`.
- NRD/NRI runtime libraries under `External`.
- Unity PluginAPI headers under `External/UnityPluginAPI`.

## Documentation

- `Docs/RaytracingSampleApi.md`: current sample API and renderer architecture notes.

New renderer experiments should keep this documentation updated when the recommended API shape changes.
