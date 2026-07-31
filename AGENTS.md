# DX12 Renderer Agent Handoff

This file is for AI agents working on `dx12-renderer-master`. Keep it concise, factual, and current.

## Communication

- Always reply to the user in Simplified Chinese.
- Keep technical identifiers, file paths, CMake target names, shader profiles, API names, and code symbols in their original spelling.
- The user cares about renderer/framework architecture. Explain engine/rendering concepts from the DX12 pipeline, command recording, descriptor binding, shader reflection, RenderGraph, GPU resource lifetime, and Unity plugin integration angle.
- Before claiming behavior, inspect source first.

## Code Markers

All source changes in this repository must be wrapped with:

```cpp
//Modify Begin:2026-07-30 by BestHui
// Modified code...
//Modify End
```

For CMake:

```cmake
# Modify Begin:2026-07-30 by BestHui
# Modified code...
# Modify End
```

Comments inside code must be English. Conversation with the user stays Chinese.

## Repository Shape

- Source root: `C:\Program Files\Unity\dx12-renderer-master\dx12-renderer-master`
- Build root: `C:\Program Files\Unity\dx12-renderer-master\build`
- Main validation target: `RaytracingDemo`
- Obsidian project note: `C:\Users\minghuidai\Documents\Obsidian Vault\渲染器\说明书.md`
- Update the Obsidian note after meaningful architecture or demo changes.

Current tracked focus is bottom/framework/render graph/external libs/docs and `Demos/RaytracingDemo`. Old demos may still exist locally and should stay build-visible when present, but they are not the current correctness target.

## Build And Run

Use Release unless the user explicitly asks otherwise:

```powershell
cmake --build . --config Release --target RaytracingDemo
```

Useful runtime environment variables:

```powershell
$env:RAYTRACING_DEMO_MESHLET_GBUFFER='1'
$env:RAYTRACING_DEMO_MESHLET_DEBUG='1'
$env:RAYTRACING_DEMO_DENOISER='off'
$env:RAYTRACING_DEMO_MODE='shader-table'
$env:RAYTRACING_DEMO_UNITY_SCENE='C:\Program Files\Unity\MDR\ModernDeferredRenderer\project\ModernDeferredRenderer\Assets\Scenes\CornellBox.unity'
```

Validation rule: do not conclude a rendering path is correct only because it compiled. Run `RaytracingDemo.exe`, keep it alive for several seconds, and check `DemoException.log` if it exits.

Device removed examples:

```text
Stage=Run
0x887a0005 DXGI_ERROR_DEVICE_REMOVED
DeviceRemovedReason=0x887a0001 DXGI_ERROR_INVALID_CALL
```

Screenshot rule: prefer window-handle based capture first. `PrintWindow` often returns black for D3D12 swapchains; if it fails, use window rect `CopyFromScreen` without moving the mouse. If needed, temporarily set the window topmost with `SWP_NOACTIVATE`, then restore it.

## Current Architecture Snapshot

Intended API shape:

```cpp
CommandContext commandContext(cmd);
commandContext.BindPipeline(pipeline);
commandContext.BindDescriptorSet(bindingSet);
commandContext.Draw();
commandContext.Dispatch();
commandContext.DispatchMesh();
commandContext.DispatchRays(desc);
```

Main framework objects:

- `PipelineLayout`
- `PipelineDescriptorPool`
- `PipelineDescriptorSet`
- `PipelineBindingSet`
- `CommandContext`
- `Shader`
- `ComputeShader`
- `MeshShader`
- `RayTracingShader`
- `RayTracingBindingSet`
- `RayTracingDispatchTables`
- `RayTracingAccelerationStructure`

The migration goal is NRI-like contracts:

```text
device / queue / command buffer / descriptor pool / descriptor set / pipeline layout / pipeline state
```

Avoid adding new sample code that directly touches root signatures, raw descriptor heaps, raw descriptor tables, or D3D12 PSO internals unless it is truly inside the low-level wrapper.

## RaytracingDemo Responsibilities

`RaytracingDemo` is a sample-style demo, not just a one-off feature test. It should demonstrate the recommended API usage.

Current major pieces:

- `Scene` from Framework stores camera, objects, materials, lights, skybox.
- `UnitySceneImporter` parses Unity `.unity` scene into Framework `Scene`.
- `RaytracingDemoSceneResources` converts `Scene` to GPU-side textures/material buffers/geometry buffers/RTAS/meshlet buffers.
- `SceneLightManager` owns editable lights and GPU light buffers.
- `PathTracingPipelineController` owns inline ray query and DXR pipeline setup.
- RenderGraph schedules base resources, lighting, denoise, skybox, postprocess, overlays.
- CUDA bloom is an external pass/tool path and must not corrupt history resources.

## Meshlet / Mesh Shader State

Framework now has:

- `Framework/Geometry/Meshlet.h/.cpp`
- `Framework/Rendering/Pipeline/MeshShader.h/.cpp`
- `CommandList::DispatchMesh()`
- `CommandContext::BindPipeline(MeshShader&)`
- `CommandContext::DispatchMesh()`
- `RasterPipelineStateBuilder::WithMeshShaders()`

`RaytracingDemo` has:

- `Use Meshlet GBuffer`
- `Meshlet Backend`: `Task Shader` or `Compute Indirect`
- `Debug Meshlet Clusters`
- stress spheres above the main deferred scene

Meshlet is a GBuffer backend, not a second scene API. Framework `Scene` is the source data. `RaytracingDemoSceneResources` derives ordinary draw objects, RTAS instances, and `MeshletGeometrySet` GPU inputs from the same scene objects/materials/prototypes.

Current meshlet GBuffer backends:

- Task shader path: `GBuffer.task.as.hlsl + GBuffer.task.ms.hlsl + GBuffer.meshletindirect.ps.hlsl`.
- Compute-indirect path: `MeshletCull.cs.hlsl -> ExecuteIndirect -> GBuffer.meshletindirect.vs.hlsl + GBuffer.meshletindirect.ps.hlsl`.

The compute-indirect path must pass meshlet identity through root constants in the indirect command plus `D3D12_DRAW_ARGUMENTS`; do not rely on `SV_InstanceID` for meshlet instance identity.

Important meshlet bug found on 2026-07-30: do not use a UV sphere with a full duplicated pole ring as a DXR stress mesh. Degenerate pole triangles can produce invalid ray-hit data and trigger device removed. The current stress sphere prototype uses unique top/bottom vertices and non-degenerate triangles.

Important meshlet bug found on 2026-07-31: normal draw and meshlet must be built from the same `MeshPrototype`. Splitting built-in plane/cube/sphere creation between draw and meshlet paths can produce UV/winding differences. Also bind the same bindless descriptor heap for draw, task meshlet, and compute-indirect meshlet GBuffer paths.

`Demos/MeshletsDemo` is not a real mesh shader demo. It builds meshlets with `meshoptimizer`, runs compute culling, writes indirect draw commands, and draws through traditional VS/PS with `ExecuteIndirect`.

## Unity Plugin Direction

Long-term target: this renderer can run standalone or as a Unity native plugin.

Unity plugin mode should:

- Use Unity-provided `ID3D12Device`, queue, command list/event context, and resources.
- Not create its own device or swapchain.
- Treat Unity resources as external wrapped resources.
- Accept POD scene/render data from C# or native Unity plugin API.
- Run the internal render graph as a plugin render event/pass.
- Output one or more Unity-owned textures.

The bindless reference project at:

```text
C:\Users\minghuidai\Desktop\DX12BindlessUnity-master\DX12BindlessUnity-master\NativePlugin.Bindless\PluginSource\source
```

uses `IUnityGraphicsD3D12v7::GetDevice()` / `GetCommandQueue()`, Unity render events, and D3D12 hooks for root signatures, descriptor heaps, and command list descriptor table binding. It is useful as a reference for low-level Unity/D3D12 interop, but do not copy its hook-heavy design blindly into the core renderer.

## Current Known Risks

- Some old demos still use `CommonRootSignature`. Do not migrate them unless explicitly requested.
- `RaytracingDemoSceneResources` is still broad: textures, model loading, material buffer, geometry buffer, RTAS, meshlet buffers, and stress objects all live there.
- Meshlet path can be slower than raster because it currently lacks task shader/GPU culling/indirect dispatch. That is expected.
- Descriptor binding must compare underlying `ID3D12Resource*`, not only wrapper object pointers, because buffers can be recreated inside stable wrapper objects.
- When GPU resources are recreated, old resources must be retired by fence, not destroyed immediately.

## Next Logical Work

Recommended next steps:

- Split `RaytracingDemoSceneResources` into clearer scene-to-GPU resource builders.
- Add task/amplification shader and GPU culling to the meshlet path.
- Add sample-grade object picking and transform gizmo only after selection/render ID infrastructure is in place.
- Build Unity plugin external render context: wrap Unity device/queue/resources and run the internal graph without owning swapchain/present.
- Continue reducing direct D3D12 usage in demo code; keep low-level details inside Framework/DX12Library.
