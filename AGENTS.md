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
//Modify Begin:2026-07-30 by Hui
// Modified code...
//Modify End
```

- Never nest `//Modify Begin` / `//Modify End` blocks.
- When changing code that is already inside a marker, extend the existing outer block and update its date to the newest change; do not add an inner block.
- Every `//Modify Begin` must have exactly one matching `//Modify End`. Before committing, run a balance and nesting scan across first-party C++ and shader files.

CMake files must not contain author signatures, `# Modify` markers, or similar handoff annotations. Keep CMake changes readable through target names, standard commands, and short English comments only where the intent is not obvious.

Comments inside code must be English. Conversation with the user stays Chinese.

## Repository Shape

- Source root: `C:\Program Files\Unity\dx12-renderer-master\dx12-renderer-master`
- Build root: `C:\Program Files\Unity\dx12-renderer-master\build`
- Main validation target: `RaytracingDemo`
- Obsidian project note: `C:\Users\minghuidai\Documents\Obsidian Vault\渲染器\说明书.md`
- Update the Obsidian note after meaningful architecture or demo changes.

### External dependency provisioning

- `External/DLSS` is the pinned `NVIDIA/DLSS` submodule at `v310.7.0`; use the official `include/` and `lib/Windows_x86_64/` layout from that checkout.
- `External/ImGui` is the pinned `ocornut/imgui` submodule at `v1.91.9`. Compile its official sources directly; do not copy or patch ImGui implementation files under the build directory. Repository-specific numeric input behavior belongs in `Framework/UI/NumericWidgets`.
- `External/NRI` is the pinned `NVIDIA-RTX/NRI` submodule at `v180`, and `External/NRD` is the pinned upstream commit that reports NRD `4.17.4`.
- `External/OIDN` is the pinned [Open Image Denoise](https://github.com/OpenImageDenoise/oidn) submodule at `v2.5.1`. `CMakeIncludes/BuildOidn.cmake` builds its CPU and CUDA device modules in `build/ThirdParty/OIDN`, privately attached to `Framework` as a pre-build command so no OIDN solution project appears. It resolves `DX12_RENDERER_OIDN_CUDA_TOOLKIT_ROOT` from `CUDA_PATH` or the installed CUDA toolkit; the CUDA path requires a matching NVIDIA adapter and deploys `OpenImageDenoise_device_cuda.dll` alongside the core, CPU, and TBB runtime DLLs.
- The root CMake config builds NRI and NRD source targets with only the D3D12 backend enabled. Framework links the `NRI`, `NRD`, and `NRDIntegration` targets; do not reintroduce direct `_Bin/*.lib` or `_Bin/*.dll` paths in Demo CMake.
- First configure may download NRI/NRD build-only dependencies into the build tree through upstream CMake `FetchContent`. These generated dependencies and NRD shader blobs must not be copied into the parent repository.
- `External/Streamline` remains a separately provisioned SDK package because its official source repository does not contain the runtime DLL set used by the current interposer integration.
- After a fresh clone, run `git submodule update --init --recursive` before configuring. Verify the pinned commits with `git submodule status`.

### CMake Solution Hygiene

- Project-owned files must use the common `ProjectBase.cmake` physical-tree mapping so Visual Studio filters and Rider project views mirror paths relative to each target directory, such as `include/Framework/...`, `src/...`, `shaders/...`, and `tools/...`. Do not add manual groups that flatten or rename those directories. External implementation files compiled into a target may be hidden with standard MSBuild `Visible=false` metadata so they do not pollute the target's physical tree.
- Use `source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" ...)` to generate Visual Studio `.vcxproj.filters`, but do not assign per-source MSBuild `Link` metadata to project-owned files. CMake injects each target-local `CMakeLists.txt` as an unlinked `CustomBuild` item for automatic regeneration; mixing linked source files with that physical item makes Rider create an unwanted target-named child folder containing only `CMakeLists.txt`.
- Do not set `CMAKE_SUPPRESS_REGENERATION`, patch generated `.vcxproj`/`.filters`, or post-process Rider project files to hide a hierarchy problem. Preserve CMake's regeneration rule and fix the source list, target ownership, or physical-tree mapping that caused the mismatch.
- Set `HEADER_FILES`, `SOURCE_FILES`, shader lists, and `DX12_RENDERER_IDE_ONLY_FILES` before including `ProjectBase.cmake`. Only files physically below the current target directory belong in its tree mapping; do not give an external source a fake `include`, `src`, or `shaders` path.
- Keep `CMAKE_CURRENT_SOURCE_DIR` as the tree root passed to `source_group`. Do not use the repository root for a nested target: it exposes sibling targets inside that project and prevents Rider from collapsing the target-local common path.
- Framework's expected top-level project tree is exactly `include`, `shaders`, `src`, and `tools`. RaytracingDemo's is `include`, `shaders`, and `src`. Paths below those roots must mirror the repository directories rather than synthetic groups such as `Header Files`, `Source Files`, or duplicated target-name folders.
- Dear ImGui source remains under `External/ImGui`. Framework compiles those official files with `target_sources`, but marks them `Visible=false`; never copy, generate, or patch ImGui source under `build/Generated`, `_deps`, Framework, or the Demo merely to affect IDE grouping.
- `build` is disposable generated state, not a source location. It may contain CMake/MSBuild projects, compiled shader headers, isolated NRI/NRD build trees, libraries, binaries, and Rider's `.idea`; it must not become the canonical home of editable C++/CUDA/HLSL sources. After diagnosing stale output, preserve `.idea` only when the user wants Rider settings retained, remove the other generated entries, then configure from source again.
- The root project explicitly adds `Demos/RaytracingDemo`; do not glob or auto-discover every directory under `Demos`. `Demos/Common` is shared support code consumed by the maintained demo and is not a standalone solution target.
- Do not use `add_custom_target(...)` solely to model internal shader compilation, asset generation, or third-party prebuild work. Visual Studio generators emit every custom target as a visible solution project.
- Attach that work to its consuming target with `add_custom_command(TARGET <consumer> PRE_BUILD ...)` instead. Keep the invoked script's stamp/byproduct checks so this does not force unnecessary rebuilds.
- After changing solution-generation logic, regenerate and verify that `LearningDirectX12.sln` contains only `RaytracingDemo`, `DX12Library`, `Framework`, `RenderGraph`, and CMake's predefined targets. Confirm each project-owned source item uses its physical absolute path without child `<Link>` metadata, each target-local `CMakeLists.txt` remains a root-level `CustomBuild` regeneration item, `build/Demos` contains only `RaytracingDemo`, and `build/Generated/ImGui` plus `_deps/dx12_renderer_imgui-*` do not exist. Do not delete isolated stale `.vcxproj` files merely because they are no longer referenced by the solution; clean the build tree and regenerate instead.

`Demos` contains only the maintained `RaytracingDemo` and its shared `Common` support. Do not auto-discover or restore deleted historical demos in the root CMake configuration.

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

- The module dependency direction is `DX12Library <- RenderGraph <- Framework <- RaytracingDemo`. Reusable Framework features may register graph work, but RenderGraph must never depend on Framework.
- Framework features that own multiple logical stages expose `AddPasses(RenderGraphBuilder&, Inputs)`. Auto Exposure, ReSTIR DI/GI, raster Bloom, NRD, SVGF, OIDN, and TAA use this contract. The builder is construction-scoped: pass only the reference while building the graph and never store a pointer, reference, smart pointer, callback capture, or member that can reach it after `AddPasses` returns.
- Demo authors declare passes through `RenderGraphBuilder::AddPass<PassData>()`, `AddComputePass<PassData>()`, `AddCopyPass<PassData>()`, or `AddExternalPass<PassData>()`, then use `RenderGraphPassBuilder` for `ReadTexture`, `ReadBuffer`, `WriteTexture`, `WriteUav`, `ReadWriteUav`, `ReadExternal`, and `WriteExternal` instead of manually constructing naked input/output vectors. Queue type is fixed by the selected `Add*Pass` API. `PassData` is the immutable recording contract.
- Persistent Framework-owned history/scratch resources may enter the graph through `RenderGraphBuilder::ImportResource()`. Imported resources receive logical IDs for topology, culling, state planning, and queue hazard tracking, but remain externally allocated and never enter `ResourcePool` or transient alias allocation. SVGF/TAA use distinct logical read/write IDs for Framework-owned ping-pong histories; their physical resolvers must remain stable for the complete graph execution and advance only at a rendered-frame boundary.
- Graph-owned scratch uses `RenderGraphBuilder::CreateTexture()` with `Discard`. Any future graph-owned persistent history must use `ResourceInitAction::Preserve` plus a dedicated allocation. The Demo must merge `ReleaseTextureDescriptions()` into its graph resource descriptions after all Framework subgraphs have registered their passes.
- Raster Bloom registers `Bloom Prefilter`, every `Bloom Downsample`, every `Bloom Upsample`, and `Bloom Composite` as separate passes. Its graph-owned pyramid currently requests dedicated resources instead of transient-heap aliasing: repeated graph rebuilds with Bloom aliasing enabled reproducibly caused `DXGI_ERROR_DEVICE_HUNG`, so alias activation/state/placed-heap ordering must be fixed and stress-tested separately before removing that safety setting.
- OIDN is a static-image subgraph. On a matching CUDA adapter, `OIDN HDR Readback` copies converged HDR `HistoryColor` into an imported D3D12 shared input buffer under the graph `COPY_SOURCE`/`COPY_DEST` plan; the Direct queue signals a shared D3D12 fence, a background `std::jthread` invokes OIDN CUDA `RT` with `Quality::Fast`, and the Direct queue GPU-waits on the CUDA signal before `OIDN Result Upload` copies the shared output buffer into its imported persistent texture. No HDR pixels traverse CPU memory in this path. A CPU `Quality::Fast` readback/filter/upload path remains an explicit fallback if CUDA device or external-memory import is unavailable. `OIDN Composite` writes `SceneColor`; after upload the result is held while input stays static. Camera motion, a real render-input reset, selection changes, or resource recreation advance the generation and invalidate held/stale work. Do not read and later write the same logical SceneColor resource in the subgraph: this RenderGraph has no SSA versions and will correctly see that as a cycle once later passes also write SceneColor.
- Typed `PassData` must own its construction-time data or retain an explicitly shared immutable object. Never store a pointer/reference to a graph-builder local adapter such as `RaytracingDemoPassResources`; the graph outlives that stack frame. `RaytracingDemoPassResourcesSnapshot` intentionally stores a value snapshot for this reason.
- `RenderGraphCompiler` produces the immutable `CompiledRenderGraph`: culled topological order, per-pass resource-state plan, render-target info, and execution batches. `RenderGraphRoot` only owns definitions/rebuilds; `RenderGraphCommandExecutor` records and submits.
- The compiled queue schedule covers Direct, Async Compute, and Copy queues. Copy destinations must use `ResourceInitAction::CopyDestination` or `Preserve`; a Copy command list cannot record render-target clear/discard initialization.
- Async-compute ownership transfer is explicit in `PassResourceStatePlan::DirectPreamble`: the Compiler assigns alias barriers and async-output transitions to a direct-queue preamble, then the compute command list records only the remaining per-pass plan. Do not add execution-time `skip...` booleans to suppress duplicated barriers.
- Resources outside `ResourcePool` must be declared through `RenderPass::AddExternalResourceAccess()`. The Compiler places async-pass external transitions in `DirectPreamble`; pass lambdas may bind those resources but must not hand-write queue-state preparation callbacks or barriers.
- Current async external accesses are deliberately read-only. Declare async outputs as RenderGraph resources; do not claim or add async writes to externally owned resources until queue-aware external ownership transfer is designed and tested.
- `ResourceIds` is process-global only as a thread-safe diagnostic-name interner. It uses function-local lifetime, locks mutations/lookups, and returns resource names by value; do not reintroduce static mutable vectors/maps or borrowed name references.
- `FrameContext` is read-only for pass code. Resource barriers, aliasing, clears, and queue dependencies are recorded from immutable plans; pass code must not infer or mutate graph state.
- `CommandList::ClearUnorderedAccessUint()` records only the clear. If later work writes the same UAV, register the clear and the later write as separate passes (or otherwise declare an explicit graph WAW dependency) so the Compiler emits the required UAV ordering.
- A pass may enter a parallel recording batch only by explicitly calling `SetParallelRecordingEligible(true)`. GPU write/read or write/write dependencies do not prevent parallel CPU recording; only Direct queue, non-external status, and audited CPU-side thread safety are required.
- The Executor records one worker-owned direct `CommandList` per pass, including its own resource plan, then submits the closed lists in original topological order. `ResourceStateRegistry` resolves pending transitions at ordered submission; the internal barrier encoder queues aliasing before first-use transitions. Each worker owns its command allocator, upload buffer, and dynamic descriptor heaps.
- `CommandList` and `CommandContext` expose no transition/UAV/aliasing barrier API. Demo code and ordinary Framework algorithms must express synchronization through pass declarations. `CommandListInternalAccess` is renderer infrastructure only; the Framework `PRE_BUILD` ownership check runs `tools/VerifyRenderGraphOwnership.cmake` without creating another solution target, scans first-party DX12Library, RenderGraph, Framework, and Demo sources, and rejects direct barriers, low-level tracker access, Demo access to the bridge, descriptor-binding state policy, removed auto-barrier mechanisms, and stored Framework builder references. The exact non-RG system boundaries are upload, readback, mip generation, swapchain/present, shared upload, and RTAS construction; NRD is not a barrier allowlist exception.
- `RenderGraphRoot::SetParallelDirectCommandRecording()` is an A/B switch for CPU profiling. It changes only recording strategy, not graph topology or GPU queue ordering; keep it enabled by default and disable it only to compare CPU recording cost.
- Do not mark a pass parallel-safe if it mutates a descriptor set, binding set, scene cache, or other CPU state shared with another candidate. Refactor that state into pass-owned data first.
- The current audited Inline Ray Query batch contains `Direct Lighting`, `Indirect Lighting`, and `Lighting Composite` while indirect async compute is disabled. Each uses a distinct mutable descriptor set. Async compute remains a separate queue-overlap feature and is intentionally not combined with same-queue recording batches.
- `BindlessDescriptorHeap` has a CPU canonical heap plus up to three shader-visible frame pages. Call `BeginFrame(directQueue, asyncComputeQueue)` before recording and `EndFrame(directFence, asyncComputeFence)` after submission; descriptor mutation is only legal outside that frame scope. A page is reused only after both queue fences complete.
- `BufferDescription` declares `BufferKind::Structured` or `BufferKind::Raw` and `BufferUsage`; do not infer a raw buffer from `stride == 1`. `RWStructuredBuffer<T>` maps to a `StructuredBuffer` created with `BufferUsage::UnorderedAccess`, then bound through `UnorderedAccessView`. UAV binding validates the D3D12 UAV resource flag, and structured UAV descriptors validate their byte stride.

## Device Context Ownership

`RenderGraphRoot` exposes terminal Present variants and `ReadbackTexture` only. Generic graph-to-graph copy/draw callbacks are intentionally absent, and `VerifyRenderGraphOwnership.cmake` rejects their reintroduction; all new graph-internal work must be registered as a pass.

- `D3D12RenderContext` owns the per-device `D3D12DeviceContext`. It is the sole owner of the D3D12 device-facing `ResourceStateRegistry` and CPU-visible descriptor allocators; `Application` only forwards it for standalone composition.
- `CommandQueue` receives `D3D12DeviceContext`, not independent device/registry copies. It creates each `CommandList` from that same context, so queue submission state merges and resource registration always address one explicit device scope.
- `FrameworkDeviceContext` receives the same `D3D12DeviceContext` plus queues. It must not carry a descriptor-allocation callback that captures `Application`; use `FrameworkDeviceContext::AllocateDescriptors()` instead.
- `RootSignature` creation receives an explicit `ID3D12Device2&`. `GenerateMipsPso` receives an explicit device and owns its static CPU descriptor allocator. Binary shader loading uses `D3DReadFileToBlob` and must not access `Application` for a DXC library.
- `ResourceStateRegistry::SubmissionScope` serializes CPU-side final-state merging only. It is not a GPU synchronization primitive: cross-queue execution still requires the queue fence/wait plan emitted by RenderGraph.

## RaytracingDemo Responsibilities

`RaytracingDemo` is a sample-style demo, not just a one-off feature test. It should demonstrate the recommended API usage.

Device/queue work in demo feature code must use the injected `FrameworkDeviceContext`. `Application::Get()` is reserved for standalone demo lifecycle operations such as construction and process quit.

Current major pieces:

- `Scene` from Framework stores nodes, camera, objects, materials, lights, and skybox. `SceneImporter::ImportFromFile()` is the common `.unity` / project `.json` / `.fbx` entry point.
- `RaytracingDemoSceneResources` converts `Scene` to GPU-side textures/material buffers/geometry buffers/RTAS/meshlet buffers.
- `SceneLightManager` owns editable lights and GPU light buffers.
- `PathTracingPipelineController` owns inline ray query and DXR pipeline setup.
- Backend compatibility is defined by `RaytracingDemoFrameState::SupportsDirectLighting`, `SupportsIndirectLighting`, and `SupportsAsyncCompute`. The UI uses the same rules as RenderGraph: incompatible DXR selections remain selected but are reported by a persistent red warning, and a manual switch to DXR opens a one-shot modal with an Inline fallback. Runtime automation and startup configuration must not open the modal.
- `ActivePixelListController` owns the shared compacted ray-traced pixel list, compute indirect arguments, and diagnostics readback used by PT Direct, PT Indirect, ReSTIR DI, and ReSTIR GI.
- RenderGraph schedules base resources, lighting, denoise, skybox, postprocess, overlays.
- Framework raster Bloom is a RenderGraph subgraph registered through `Bloom::AddPasses`; the Demo only supplies logical inputs, output, runtime parameters, resolution expressions, and the selected pyramid depth.
- CUDA Bloom is a RaytracingDemo-specific external pass/tool path under `Demos/RaytracingDemo`; it must not be moved into Framework or corrupt history resources.

FBX scene import uses the same Assimp geometry flags in `SceneFbxImporter` and `ModelLoader`: validation, left-handed conversion, triangulation, invalid-data filtering, four-weight limiting, and 16-bit-safe large-mesh splitting. `SceneMeshReference::SubmeshIndex` maps to `MeshPrototype::m_SourceMeshIndex`; prefer this stable index before mesh-name lookup. External and embedded FBX textures both flow through `TextureLoader`. The importer preserves Spot Lights, but the current demo GPU lighting contract renders them as point-light fallbacks while retaining the original spot data. It selects one FBX camera, can generate a bounds-framing fallback camera, and does not claim advanced transparency/clearcoat/transmission or animation playback.

`PathTracingDispatchMode::CompactedIndirect` is an optional ray-traced pixel-dispatch mode. Its graph contract is `Base Resources -> Active Pixel Compaction -> Dispatch Finalize`, after which PT/ReSTIR and diagnostics readback consume the finalized data independently. Compaction keeps only `DepthTexture < 1.0f` pixels; Finalize writes `{ activePixelCount, D3D12_DISPATCH_ARGUMENTS }`, with indirect dispatch arguments starting after the count. All compacted compute passes must bind the current frame's scene `BindlessDescriptorHeap` before binding reflected descriptor sets. Do not restore the old `DynamicDescriptorHeap` submission path or duplicate the active-pixel implementation under the demo.

D3D12 graphics and compute root signatures are independent command-list state. Keep `m_GraphicsRootSignature`, `m_ComputeRootSignature`, and `m_DescriptorTableRootSignature` separate. A depth resource that is both a DSV and float SRV uses `R32_TYPELESS` as the resource format, `D32_FLOAT` as the DSV format, and `R32_FLOAT` as the SRV format.

## DLSS / Streamline

`BloomController` is the Demo-facing selector for the Framework raster Bloom subgraph and the Demo-owned CUDA external backend. The latter remains a CUDA interop path under `Demos/RaytracingDemo`; it must not move into Framework or corrupt history resources.

- **Status: experimental integration, not a completed feature.** Native NGX SR/DLAA and the Streamline RR/FG paths have build, startup, and automation safety coverage only. They have not been accepted through image-quality, stability, timing, or performance validation on supported RR/FG hardware. Runtime capability query is the final gate: the current RTX 2060 development adapter cannot support FG and currently reports RR unavailable. Do not claim DLSS, RR, or FG is usable, production-ready, or validated without supported-hardware evidence.
- `StreamlineRuntime` lives in Framework and implements the generic DX12Library `D3D12RuntimeLifecycle`: call `slInit` before the first D3D12/DXGI API, then `slSetD3DDevice` immediately after device creation. `RaytracingDemo` compiles `DLSS.cpp` and `StreamlineRuntime.cpp` as hidden external sources; the core `Framework` target must not link NGX or Streamline and no extra `FrameworkNvidiaFeatures` project may be created. The demo links `sl.interposer.lib`, so do not set `eUseManualHooking`, call `slUpgradeInterface`, or route device/queue/swap-chain creation through feature callbacks.
- Frame Generation and Ray Reconstruction capability/controller interfaces belong to Framework. Do not move them into DX12Library or expose them through `Application`; the demo composition root passes those services directly into `FrameworkDeviceContext`.
- `Framework/Rendering/Upscaling/DLSS` owns native NGX Super Resolution / DLAA and Streamline Ray Reconstruction / Frame Generation lifecycle, capability query, optimal render resolution, projection jitter, history reset, feature recreation, frame tokens, PCL markers, resource tagging, and evaluation. Do not put NGX or Streamline handles/calls in demo code.
- `RaytracingDemo` adapts RenderGraph resources only. `Passes/DLSSPass.cpp` supplies HDR `SceneColor`, `DepthBuffer`, UV-space `MotionVector`, and the display-resolution output. `Passes/DLSSRayReconstructionPreparationPass.cpp` writes the RR contract resource: `R16G16B16A16_FLOAT` world normal in `xyz` and linear roughness in `w`, then tags it as `kBufferTypeNormalRoughness` with `ePacked` mode.
- RR consumes noisy ray-traced `SceneColor`; when RR is active the regular NRD/SVGF pass is excluded from graph construction and its camera-side denoiser writes are disabled. Do not feed already denoised radiance to RR.
- `DLSSOutput`, `DLSSFinishedToken`, `DLSSNormalRoughness`, and `FrameGenerationHudLess` must be registered only for the graph topology that produces and consumes them. Registering unused transient resources corrupts RenderGraph lifetimes during graph destruction.
- The experimental FG path prepares Streamline constants/options before RenderGraph execution, produces tone-mapped HUD-less color, tags depth/motion/HUD-less resources before present, emits PCL submit/present markers, then lets the Streamline-proxied swapchain present inject generated frames. It requires a supported adapter/driver/OS configuration; do not force-enable it on unsupported hardware or infer that it works from a successful build.
- Recreate the native NGX feature only when mode or render/display resolution changes. Before releasing an already evaluated feature, flush the injected `FrameworkDeviceContext`; do not release an in-flight handle. Only commit a newly created handle after NGX creation succeeds.
- `RaytracingDemoRenderPipelineConfiguration` owns both graph topology and resource-affecting DLSS state (`DLSSMode`, render size, and display size). Any configuration change flushes outstanding GPU work, releases CUDA interop and the old NGX feature, then rebuilds the graph. Do not let dynamic RenderGraph texture recreation occur independently of NGX resource retirement.
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

- Transient-heap alias activation is not yet robust for raster Bloom's multi-level render-target chain during repeated RenderGraph rebuilds. Keep Bloom scratch resources dedicated until the ordering/state model is corrected; do not infer that all same-queue aliasing is safe from simpler graphs.
- `RaytracingDemoSceneResources` is still broad: textures, model loading, material buffer, geometry buffer, RTAS, meshlet buffers, and stress objects all live there.
- Meshlet path has both a task-shader backend and a compute-cull/`ExecuteIndirect` backend. It can still be slower than raster for a scene whose meshlet culling does not recover its dispatch and descriptor costs; measure before treating this as a regression.
- Descriptor binding must compare underlying `ID3D12Resource*`, not only wrapper object pointers, because buffers can be recreated inside stable wrapper objects.
- When GPU resources are recreated, old resources must be retired by fence, not destroyed immediately.

## Next Logical Work

Recommended next steps:

- Deepen the implemented Framework diagnostics contract by exposing its existing non-blocking `GpuReadbackBuffer`/`GpuReadbackTexture` primitives as generic Diagnostics request APIs with image assertions, then add DRED attachments, cross-queue/lifetime invariant closure, background writing, compression, and retention policy. The machine-readable session, RenderGraph/queue/resource/descriptor telemetry, named control/observation runner, structured assertions, CPU/GPU timing, query/diff/reproduction CLI, compacted active-pixel readback, and OIDN HDR readback are already implemented; do not list that baseline as future work.
- Keep C++20 as the project baseline and actively modernize suitable legacy C++11-style code with C++20 facilities. Keep modern HLSL features capability-gated by the Shader Model 6.8 baseline and hardware support.
- Continue reducing the remaining responsibilities in `RaytracingDemoSceneResources`; texture/material, geometry, meshlet, and RTAS builders already exist.
- Extend the existing task/mesh and compute-indirect meshlet backends only with measured visibility/LOD work; do not list their already implemented baseline as future work.
- Add sample-grade object picking and transform gizmo only after selection/render ID infrastructure is in place.
- Build Unity plugin external render context: wrap Unity device/queue/resources and run the internal graph without owning swapchain/present.
- Continue reducing direct D3D12 usage in demo code; keep low-level details inside Framework/DX12Library.

## Reliable Build And Runtime Validation

This machine does not put `cmake.exe` on `PATH`. Resolve it before building; the current Visual Studio installation provides:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$source = 'C:\Program Files\Unity\dx12-renderer-master\dx12-renderer-master'
$build = 'C:\Program Files\Unity\dx12-renderer-master\build'
$env:TrackFileAccess = 'false'
& $cmake -S $source -B $build -G 'Visual Studio 17 2022' -A x64
& $cmake --build $build --config Release --target RaytracingDemo --parallel
```

On this machine, MSBuild file tracking can leave `cl.exe` at zero CPU during compiler probes or compilation. Set `TrackFileAccess=false` in the environment before both configure and build so nested CMake/MSBuild work, including NRD's ShaderMake external project, inherits it. Passing only `-- /p:TrackFileAccess=false` fixes the outer MSBuild but does not propagate to nested builds. Do not create a `CodexMSVCProbeOverride.cmake`, force compiler identity variables, switch to Ninja, or patch generated `.vcxproj` files to hide this machine-specific issue.

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

Do not launch this automation with `Start-Process -WindowStyle Hidden`: a hidden D3D12 window may not enter the normal render/update loop, leaving `RuntimeAutomation.log` at only `Runtime automation started.`. Command-line automation may open the normal application window, but it must not inject mouse or keyboard input. Use a bounded wait and terminate a timed-out process instead of waiting indefinitely.

`RendererDiagnostics` is a developer-only target controlled by `DX12_RENDERER_BUILD_DEVELOPER_TOOLS`, which must remain `OFF` in the default configuration. Its `run`, `inspect`, `query`, `diff`, `reproduce`, and `selftest` commands emit JSON/JSONL for coding-agent use and never inject desktop input. `run` and executable reproduction default to a bounded 262,144-event buffer; use `--max-events` deliberately for unusually long captures. Always check the `inspect` verdict and exit code: `12` means the capture is incomplete because events were dropped, the manifest did not reach a terminal state, or an assertion remains unknown. Error/fatal and failed/unknown assertion events receive retention priority, but an incomplete capture cannot prove absence of failure. Exit `10` means findings, `11` means a baseline regression, and automation failures use `20`-`24`.

After every automated run, inspect `build\bin\Release\Saved\RuntimeAutomation.log`. A valid run must contain `Runtime automation completed.` and no newly written `build\bin\Release\DemoException.log` or `Saved\Diagnostics\WindowCallbackException.log`; stale diagnostic files may remain, so compare timestamps. `core` toggles RG timing/capture/export, soft shadows, stress spheres four times, meshlet enable/backend, every native DLSS SR/DLAA mode, the PBR/Stylized Comic material model, Inline/DXR backend, direct/indirect lighting including ReSTIR GI, parallel direct recording, async compute, skybox, and accumulation. It emits an immediate timing CSV after the ReSTIR GI state, then a final CSV. `core` does not run compacted-only active-pixel verification; use `visual` with `RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted` for that invariant. `stress` only performs the four stress-sphere transitions and is the fastest regression test for RTAS/meshlet resource updates.

`RAYTRACING_DEMO_AUTOTEST=visual` is the compacted-dispatch image regression. It exercises PT Direct, PT Indirect, ReSTIR DI, and ReSTIR GI without synthetic input, verifies that active count and dispatch arguments agree, reads back the presentation texture, rejects black/near-black images, and writes PNG files under `build\bin\Release\Saved\AutomationScreenshots`. Run it with `RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted` after changes to active-pixel compaction, descriptor binding, indirect dispatch, or ReSTIR compacted variants.

`RAYTRACING_DEMO_AUTOTEST=oidn` selects OIDN, sets static SPP to `2` while deliberately leaving manual accumulation disabled, and verifies automatic accumulation, asynchronous CUDA shared-memory filtering when available, result copy-back, static-result holding, immediate generation invalidation after a programmed camera move, and a second successful convergence before returning to NRD. The `oidn_async_pipeline` assertion records `backend=cuda` or `cpu_fallback`; the run must also finish with `oidn_motion_reset_immediate=pass` and `oidn_motion_invalidation=pass`, without a newly written `DemoException.log` or `WindowCallbackException.log`.

`RAYTRACING_DEMO_AUTOTEST=matrix` is the crash-regression matrix for discrete production rendering switches. It applies a complete legal state snapshot, waits for the next stable interval, then records `Begin matrix#...` and `Applied matrix#...` in the log. It covers raster/task-mesh/compute-indirect GBuffer, PBR/Stylized Comic material shading, Inline Ray Query/DXR, valid Direct and Indirect lighting techniques, async compute only on Inline, parallel direct recording, hard/soft shadows, stress-sphere add/remove, skybox, and accumulation. Inline cases additionally cover ReSTIR GI indirect lighting, while invalid `DXR + ReSTIR DI/GI` and `DXR + async compute` cases are deliberately excluded. The full matrix currently has 3840 cases; it defaults to a 250 ms stable interval. Use `RAYTRACING_DEMO_AUTOTEST_START_CASE=<one-based>` and `RAYTRACING_DEMO_AUTOTEST_MAX_CASES=<count>` to reproduce or smoke-test a bounded range, and set `RAYTRACING_DEMO_AUTOTEST_QUIT=1` to exit after completion. Numeric ImGui parameters and diagnostic texture views are intentionally outside this matrix because they require a separate parameter-quality test rather than a compatibility test.

The automatic test intentionally does not synthesize mouse input and does not replace visual validation. For camera, skybox, material, or UI changes, also launch the executable normally, keep it alive for several seconds, and take a screenshot without injecting desktop input. Do not conclude correctness from a successful build or a short process lifetime alone.

## Current RTAS And Dynamic-Scene Boundaries

- Initial scene construction uses `PREFER_FAST_TRACE | ALLOW_UPDATE` for both BLAS and TLAS.
- `RayTracingAccelerationStructure::UpdateInstance()` followed by `Update()` performs an in-place TLAS `PERFORM_UPDATE` only when instance count and mesh identity are unchanged. Existing BLAS are reused, so transform-only animation is the intended per-frame path.
- Adding/removing instances, or changing an instance mesh, cannot use that TLAS update. The implementation rebuilds the TLAS but only builds BLAS for previously unseen mesh objects. Pressure-sphere toggles deliberately use this path through stable instance handles; `Application::Flush()` is part of that safe resource-update operation, so its hitch must not be attributed to steady-state rendering.
- Vertex-deformed/skinned geometry does not yet have a BLAS refit path. Although initial BLAS are created with `ALLOW_UPDATE`, the current `Update()` method only refits the TLAS. Implement per-BLAS dirty tracking and `PERFORM_UPDATE` before claiming animated vertex support.
- Do not destroy replaced AS buffers immediately. They must stay alive until the submitting queue fence retires; infrastructure code performs this handoff through `CommandListInternalAccess::TrackObjectLifetime()`. Do not make raw lifetime tracking public on `CommandList` again.

## Scene Runtime State And Emissive Lights

- `Save Scene` writes `<source-scene>.runtime.json`; it does not rewrite the `.unity`/YAML source file. The overlay restores camera transform/FOV/near/far, sky ambient intensity, light-group enable flags, and all editable directional, point, and area-light parameters. Directional `angularRadius` and point `sourceRadius` are included.
- All finite area emitters use the Framework `SurfaceEmitter` contract: `SurfaceEmitterGeometryData`, `SurfaceEmitterTriangleData`, `SurfaceEmitterInstanceData`, and a per-geometry triangle CDF. Rectangular lights are two-triangle instances of one shared unit quad; emissive meshes reuse their local geometry/CDF across instances. The Direct Lighting CDF selects an emitter instance, then the shader selects a triangle from that geometry CDF.
- Pressure spheres therefore add `12,288` instances plus one shared `528`-triangle geometry/CDF instead of `12,288 * 528` flattened triangle lights. `SurfaceArea` is cached per instance so light-CDF rebuilds are linear in emitter count; the one-time pressure-sphere transition may still hitch because RTAS/meshlet/light resources are deliberately updated together.
