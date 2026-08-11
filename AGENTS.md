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

### External dependency provisioning

- `External/DLSS` is the pinned `NVIDIA/DLSS` submodule at `v310.7.0`; use the official `include/` and `lib/Windows_x86_64/` layout from that checkout.
- `External/NRI` is the pinned `NVIDIA-RTX/NRI` submodule at `v180`, and `External/NRD` is the pinned upstream commit that reports NRD `4.17.4`.
- The root CMake config builds NRI and NRD source targets with only the D3D12 backend enabled. Framework links the `NRI`, `NRD`, and `NRDIntegration` targets; do not reintroduce direct `_Bin/*.lib` or `_Bin/*.dll` paths in Demo CMake.
- First configure may download NRI/NRD build-only dependencies into the build tree through upstream CMake `FetchContent`. These generated dependencies and NRD shader blobs must not be copied into the parent repository.
- `External/Streamline` remains a separately provisioned SDK package because its official source repository does not contain the runtime DLL set used by the current interposer integration.
- After a fresh clone, run `git submodule update --init --recursive` before configuring. Verify the pinned commits with `git submodule status`.

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

## Render Graph Recording

- `RenderGraphCompiler` produces the immutable `CompiledRenderGraph`: culled topological order, per-pass resource-state plan, render-target info, and execution batches. `RenderGraphRoot` only owns definitions/rebuilds; `RenderGraphCommandExecutor` records and submits.
- Async-compute ownership transfer is explicit in `PassResourceStatePlan::DirectPreamble`: the Compiler assigns alias barriers and async-output transitions to a direct-queue preamble, then the compute command list records only the remaining per-pass plan. Do not add execution-time `skip...` booleans to suppress duplicated barriers.
- `FrameContext` is read-only for pass code. Resource barriers, aliasing, clears, and queue dependencies are recorded from immutable plans; pass code must not infer or mutate graph state.
- A pass may enter a parallel recording batch only by explicitly calling `SetParallelRecordingEligible(true)`. GPU write/read or write/write dependencies do not prevent parallel CPU recording; only Direct queue, non-external status, and audited CPU-side thread safety are required.
- The Executor records one worker-owned direct `CommandList` per pass, including its own resource plan, then submits the closed lists in original topological order. `CommandList` resolves pending transitions through `ResourceStateRegistry` at ordered submission; `AliasingBarrierBeforeFirstUse()` queues aliasing before those first-use transitions. Each worker owns its command allocator, upload buffer, and dynamic descriptor heaps.
- `RenderGraphRoot::SetParallelDirectCommandRecording()` is an A/B switch for CPU profiling. It changes only recording strategy, not graph topology or GPU queue ordering; keep it enabled by default and disable it only to compare CPU recording cost.
- Do not mark a pass parallel-safe if it mutates a descriptor set, binding set, scene cache, or other CPU state shared with another candidate. Refactor that state into pass-owned data first.
- The current audited Inline Ray Query batch contains `Direct Lighting`, `Indirect Lighting`, and `Lighting Composite` while indirect async compute is disabled. Each uses a distinct mutable descriptor set. Async compute remains a separate queue-overlap feature and is intentionally not combined with same-queue recording batches.
- `BindlessDescriptorHeap` has a CPU canonical heap plus up to three shader-visible frame pages. Call `BeginFrame(directQueue, asyncComputeQueue)` before recording and `EndFrame(directFence, asyncComputeFence)` after submission; descriptor mutation is only legal outside that frame scope. A page is reused only after both queue fences complete.
- `BufferDescription` declares `BufferKind::Structured` or `BufferKind::Raw` and `BufferUsage`; do not infer a raw buffer from `stride == 1`. `RWStructuredBuffer<T>` maps to a `StructuredBuffer` created with `BufferUsage::UnorderedAccess`, then bound through `UnorderedAccessView`. UAV binding validates the D3D12 UAV resource flag, and structured UAV descriptors validate their byte stride.

## Device Context Ownership

- `D3D12RenderContext` owns the per-device `D3D12DeviceContext`. It is the sole owner of the D3D12 device-facing `ResourceStateRegistry` and CPU-visible descriptor allocators; `Application` only forwards it for standalone composition.
- `CommandQueue` receives `D3D12DeviceContext`, not independent device/registry copies. It creates each `CommandList` from that same context, so queue submission state merges and resource registration always address one explicit device scope.
- `FrameworkDeviceContext` receives the same `D3D12DeviceContext` plus queues. It must not carry a descriptor-allocation callback that captures `Application`; use `FrameworkDeviceContext::AllocateDescriptors()` instead.
- `RootSignature` creation receives an explicit `ID3D12Device2&`. `GenerateMipsPso` receives an explicit device and owns its static CPU descriptor allocator. Binary shader loading uses `D3DReadFileToBlob` and must not access `Application` for a DXC library.
- `ResourceStateRegistry::SubmissionScope` serializes CPU-side final-state merging only. It is not a GPU synchronization primitive: cross-queue execution still requires the queue fence/wait plan emitted by RenderGraph.

## RaytracingDemo Responsibilities

`RaytracingDemo` is a sample-style demo, not just a one-off feature test. It should demonstrate the recommended API usage.

Device/queue work in demo feature code must use the injected `FrameworkDeviceContext`. `Application::Get()` is reserved for standalone demo lifecycle operations such as construction and process quit.

Current major pieces:

- `Scene` from Framework stores camera, objects, materials, lights, skybox.
- `UnitySceneImporter` parses Unity `.unity` scene into Framework `Scene`.
- `RaytracingDemoSceneResources` converts `Scene` to GPU-side textures/material buffers/geometry buffers/RTAS/meshlet buffers.
- `SceneLightManager` owns editable lights and GPU light buffers.
- `PathTracingPipelineController` owns inline ray query and DXR pipeline setup.
- RenderGraph schedules base resources, lighting, denoise, skybox, postprocess, overlays.
- CUDA bloom is an external pass/tool path and must not corrupt history resources.

## DLSS / Streamline

- **Status: experimental integration, not a completed feature.** Native NGX SR/DLAA and the Streamline RR/FG paths have build, startup, and automation safety coverage only. They have not been accepted through image-quality, stability, timing, or performance validation on supported RR/FG hardware. Runtime capability query is the final gate: the current RTX 2060 development adapter cannot support FG and currently reports RR unavailable. Do not claim DLSS, RR, or FG is usable, production-ready, or validated without supported-hardware evidence.
- `Framework/Rendering/Upscaling/DLSS` owns native NGX Super Resolution / DLAA and Streamline Ray Reconstruction / Frame Generation lifecycle, capability query, optimal render resolution, projection jitter, history reset, feature recreation, frame tokens, PCL markers, resource tagging, and evaluation. Do not put NGX or Streamline handles/calls in demo code.
- `RaytracingDemo` adapts RenderGraph resources only. `Passes/DLSSPass.cpp` supplies HDR `SceneColor`, `DepthBuffer`, UV-space `MotionVector`, and the display-resolution output. `Passes/DLSSRayReconstructionPreparationPass.cpp` writes the RR contract resource: `R16G16B16A16_FLOAT` world normal in `xyz` and linear roughness in `w`, then tags it as `kBufferTypeNormalRoughness` with `ePacked` mode.
- RR consumes noisy ray-traced `SceneColor`; when RR is active the regular NRD/SVGF pass is excluded from graph construction and its camera-side denoiser writes are disabled. Do not feed already denoised radiance to RR.
- `DLSSOutput`, `DLSSFinishedToken`, `DLSSNormalRoughness`, and `FrameGenerationHudLess` must be registered only for the graph topology that produces and consumes them. Registering unused transient resources corrupts RenderGraph lifetimes during graph destruction.
- The experimental FG path prepares Streamline constants/options before RenderGraph execution, produces tone-mapped HUD-less color, tags depth/motion/HUD-less resources before present, emits PCL submit/present markers, then lets the Streamline-proxied swapchain present inject generated frames. It requires a supported adapter/driver/OS configuration; do not force-enable it on unsupported hardware or infer that it works from a successful build.
- Recreate the native NGX feature only when mode or render/display resolution changes. Before releasing an already evaluated feature, flush the injected `FrameworkDeviceContext`; do not release an in-flight handle. Only commit a newly created handle after NGX creation succeeds.
- RR/FG require process startup with `--streamline-interposer`; the default path intentionally leaves device, queues, and swapchain unproxied. Runtime values: `RAYTRACING_DEMO_DLSS=off|dlaa|quality|balanced|performance|ultra-performance`, `RAYTRACING_DEMO_DLSS_RR=0|1`, and `RAYTRACING_DEMO_DLSS_FRAME_GENERATION=0|1`. The SDK runtime files live in `External/Streamline`; preserve its notices and deployed runtime files when distributing builds.

## External Present Processing

- RenderGraph::ExternalFrameProcessor is the typed boundary for SDKs that require resource preparation and lifecycle callbacks around Present. It owns required resource IDs plus Process, BeforePresent, and AfterPresent; do not add another group of independent callbacks to RenderGraphRoot.
- The DLSS Frame Generation adapter uses this contract. Demo code only supplies the graph-resource adapter and overlay; Streamline calls remain inside Framework/Rendering/Upscaling/DLSS.
- FrameworkDeviceContext receives FrameFeaturesRuntime for RR/FG capability queries and a FrameGenerationController for presentation reconfiguration. It must not store StreamlineRuntime directly or capture Application with an anonymous frame-generation lambda. Application remains the standalone controller because only it owns window swapchain destruction and recreation.

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
- Meshlet path has both a task-shader backend and a compute-cull/`ExecuteIndirect` backend. It can still be slower than raster for a scene whose meshlet culling does not recover its dispatch and descriptor costs; measure before treating this as a regression.
- Descriptor binding must compare underlying `ID3D12Resource*`, not only wrapper object pointers, because buffers can be recreated inside stable wrapper objects.
- When GPU resources are recreated, old resources must be retired by fence, not destroyed immediately.

## Next Logical Work

Recommended next steps:

- After the SM6.9/SER phase, keep C++20 as the project baseline and actively modernize suitable legacy C++11-style code with C++20 facilities. Keep modern HLSL features capability-gated by shader model and hardware support.
- Split `RaytracingDemoSceneResources` into clearer scene-to-GPU resource builders.
- Add task/amplification shader and GPU culling to the meshlet path.
- Add sample-grade object picking and transform gizmo only after selection/render ID infrastructure is in place.
- Build Unity plugin external render context: wrap Unity device/queue/resources and run the internal graph without owning swapchain/present.
- Continue reducing direct D3D12 usage in demo code; keep low-level details inside Framework/DX12Library.

## Reliable Build And Runtime Validation

This machine does not put `cmake.exe` on `PATH`. Resolve it before building; the current Visual Studio installation provides:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build C:\Program Files\Unity\dx12-renderer-master\build --config Release --target RaytracingDemo
```

For source edits, use `C:\Users\minghuidai\.codex\tools\Invoke-CodexApplyPatch.ps1` with a single-quoted PowerShell here-string. Do not pass a multiline patch through the `apply_patch.bat` wrapper, `cmd`, a pipeline, `Set-Content`, or a string replacement script. Always run `git diff --check` after editing and inspect focused diffs if the worktree already has pending changes.

`RaytracingDemo.exe` is a GUI application, so invoking it directly from PowerShell does not wait for it. Use a process handle and wait for the built-in automatic test to exit:

```powershell
$exe = 'C:\Program Files\Unity\dx12-renderer-master\build\bin\Release\RaytracingDemo.exe'
$env:RAYTRACING_DEMO_AUTOTEST = 'core'
$env:RAYTRACING_DEMO_AUTOTEST_QUIT = '1'
$env:RAYTRACING_DEMO_AUTOTEST_STEP_MS = '700'
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
if (-not $process.WaitForExit(45000)) { throw 'RaytracingDemo automation did not exit in 45 seconds.' }
if ($process.ExitCode -ne 0) { throw "RaytracingDemo failed with exit code $($process.ExitCode)." }
```

After every automated run, inspect `build\bin\Release\Saved\RuntimeAutomation.log`. A valid run must contain `Runtime automation completed.` and no newly written `build\bin\Release\DemoException.log`. `core` toggles RG timing/capture/export, soft shadows, stress spheres four times, meshlet enable/backend, Inline/DXR backend, direct/indirect lighting including ReSTIR GI, parallel direct recording, async compute, skybox, and accumulation. It emits an immediate timing CSV after the ReSTIR GI state, then a final CSV. `stress` only performs the four stress-sphere transitions and is the fastest regression test for RTAS/meshlet resource updates.

`RAYTRACING_DEMO_AUTOTEST=matrix` is the crash-regression matrix for discrete production rendering switches. It applies a complete legal state snapshot, waits for the next stable interval, then records `Begin matrix#...` and `Applied matrix#...` in the log. It covers raster/task-mesh/compute-indirect GBuffer, Inline Ray Query/DXR, valid Direct and Indirect lighting techniques, async compute only on Inline, parallel direct recording, hard/soft shadows, stress-sphere add/remove, skybox, and accumulation. Inline cases additionally cover ReSTIR GI indirect lighting, while invalid `DXR + ReSTIR DI/GI` and `DXR + async compute` cases are deliberately excluded. The full matrix currently has 1920 cases; it defaults to a 250 ms stable interval. Use `RAYTRACING_DEMO_AUTOTEST_START_CASE=<one-based>` and `RAYTRACING_DEMO_AUTOTEST_MAX_CASES=<count>` to reproduce or smoke-test a bounded range, and set `RAYTRACING_DEMO_AUTOTEST_QUIT=1` to exit after completion. Numeric ImGui parameters and diagnostic texture views are intentionally outside this matrix because they require a separate parameter-quality test rather than a compatibility test.

The automatic test intentionally does not synthesize mouse input and does not replace visual validation. For camera, skybox, material, or UI changes, also launch the executable normally, keep it alive for several seconds, and take a screenshot without injecting desktop input. Do not conclude correctness from a successful build or a short process lifetime alone.

## Current RTAS And Dynamic-Scene Boundaries

- Initial scene construction uses `PREFER_FAST_TRACE | ALLOW_UPDATE` for both BLAS and TLAS.
- `RayTracingAccelerationStructure::UpdateInstance()` followed by `Update()` performs an in-place TLAS `PERFORM_UPDATE` only when instance count and mesh identity are unchanged. Existing BLAS are reused, so transform-only animation is the intended per-frame path.
- Adding/removing instances, or changing an instance mesh, cannot use that TLAS update. The implementation rebuilds the TLAS but only builds BLAS for previously unseen mesh objects. Pressure-sphere toggles deliberately use this path through stable instance handles; `Application::Flush()` is part of that safe resource-update operation, so its hitch must not be attributed to steady-state rendering.
- Vertex-deformed/skinned geometry does not yet have a BLAS refit path. Although initial BLAS are created with `ALLOW_UPDATE`, the current `Update()` method only refits the TLAS. Implement per-BLAS dirty tracking and `PERFORM_UPDATE` before claiming animated vertex support.
- Do not destroy replaced AS buffers immediately. They must stay alive until the submitting queue fence retires; `CommandList::TrackObject()` currently provides the command-list lifetime handoff.

## Scene Runtime State And Emissive Lights

- `Save Scene` writes `<source-scene>.runtime.json`; it does not rewrite the `.unity`/YAML source file. The overlay restores camera transform/FOV/near/far, sky ambient intensity, light-group enable flags, and all editable directional, point, and area-light parameters. Directional `angularRadius` and point `sourceRadius` are included.
- All finite area emitters use the Framework `SurfaceEmitter` contract: `SurfaceEmitterGeometryData`, `SurfaceEmitterTriangleData`, `SurfaceEmitterInstanceData`, and a per-geometry triangle CDF. Rectangular lights are two-triangle instances of one shared unit quad; emissive meshes reuse their local geometry/CDF across instances. The Direct Lighting CDF selects an emitter instance, then the shader selects a triangle from that geometry CDF.
- Pressure spheres therefore add `12,288` instances plus one shared `528`-triangle geometry/CDF instead of `12,288 * 528` flattened triangle lights. `SurfaceArea` is cached per instance so light-CDF rebuilds are linear in emitter count; the one-time pressure-sphere transition may still hitch because RTAS/meshlet/light resources are deliberately updated together.
