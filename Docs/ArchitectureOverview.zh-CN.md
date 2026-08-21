# DX12 Renderer 架构总览

本文梳理的是本 fork 当前已经存在的架构与技术点，用于快速定位代码和理解职责边界；它不代表 production 就绪，也不承诺公开 API 稳定。

## 分层原则

```text
RaytracingDemo
    sample 策略、场景选择、UI、RenderGraph 拓扑
        |
Framework
    面向渲染器的 API 与可复用功能模块
        |
RenderGraph
    pass 依赖、资源计划、命令录制与提交
        |
DX12Library
    原生 D3D12 资源、queue、同步与 swap chain
```

上层只依赖下层。新增 sample 功能时，应优先通过 `Framework` 和 `RenderGraph` 表达，不应在 demo pass 中直接铺开 D3D12 descriptor、root signature 或 CPU fence 逻辑。

## 构建 target 与源码归属

当前维护的第一方 CMake target 只有 `DX12Library`、`Framework`、`RenderGraph` 和 `RaytracingDemo`。`Demos/Common` 只提供公共程序入口，不会单独生成 target。每个 target 的源码归属严格对应仓库目录：例如 Framework 只拥有 `Framework/include`、`Framework/shaders`、`Framework/src` 和 `Framework/tools`，其他 target 也按自身磁盘目录组织。

`CMakeIncludes/ProjectBase.cmake` 使用 `source_group(TREE ...)` 为 Visual Studio 生成 filter，但不会给项目自身源码添加虚拟 MSBuild `Link` 路径。这样 Visual Studio 与 Rider 都以同一份真实目录为准，CMake 自动加入的 target-local `CMakeLists.txt` regeneration 条目也能停留在项目根节点。外部实现文件可以从 target 视图隐藏，但不能复制到 `build/`，也不能伪装成该 target 自己的 `include`、`src` 或 `shaders`。构建目录随时可以重新生成，不拥有任何需要编辑的源码。

## DX12Library：D3D12 边界

`DX12Library/` 保留 D3D12 的原生概念，主要包含：

- `D3D12DeviceContext`、`CommandQueue`、`CommandList`：封装 device，以及 Direct、Compute、Copy 三类 command queue。
- `CommandList` 只保留命令录制、barrier、descriptor staging 与 command-list 生命周期跟踪；`ResourceUploader` 负责 staging upload 和资源替换，`MipGenerator` 负责可复用的 mip 生成 pipeline。
- `Resource`、`Texture`、`Buffer`、structured/raw buffer、upload buffer 与 RTAS backing resource：管理原生 D3D12 allocation 和 resource view。
- `DescriptorAllocator`、`DynamicDescriptorHeap`、`FrameResourceRing`：管理 descriptor 以及逐帧资源寿命。GPU 可见 descriptor table 在此层完成，而不是由 demo 手写。
- `ResourceStateRegistry`、`ResourceStateTracker`：记录 transition、UAV 与 aliasing barrier。
- `GpuTimestampProfiler`：提供单条 queue 内的 GPU timestamp。
- window、swap chain、PIX marker 和 Unity D3D12 interop 边界同样位于这一层。可选集成只能通过通用 `D3D12RuntimeLifecycle` 契约完成首次 D3D12/DXGI 调用前初始化、设备创建后附着与关闭；`CommandQueue` 和 `Window` 始终调用普通 D3D12/DXGI 创建 API，不感知具体功能 SDK。

这层可以直接暴露 D3D12；`Framework` 的职责是向上提供更收敛的渲染器接口。

## Framework：可复用渲染功能

`Framework/` 的目标是让 demo 提供场景数据和功能策略，而把重复的 GPU 绑定、资源和 dispatch 逻辑收敛为通用模块。

### Pipeline 与资源绑定

- `CommandContext` 以统一的 bind/dispatch 风格录制 raster、compute、mesh shader 和 DXR 命令。
- Shader reflection 构建 `PipelineLayout`、`PipelineDescriptorPool`、`PipelineDescriptorSet` 和 `PipelineBindingSet`，通过资源名字完成绑定。
- `BindlessDescriptorHeap` 将 canonical descriptor 保存在 CPU-only heap，再镜像到按 fence 退休的 shader-visible frame page。材质保存稳定的 descriptor index；`CommandContext` 在当前 page 中准备使用 direct heap indexing 的 descriptor table。这样 CPU 更新 descriptor 时不会覆盖仍被 Direct 或 Async Compute GPU 工作读取的页。
- `ShaderVariantManager` 在启动期编译代码显式请求的变体，指纹覆盖源码、include、宏和编译参数并缓存字节码。它不是运行时热更，也不会枚举所有理论宏组合。
- `SharedUploadBuffer`、临时 descriptor 分配、`StructuredBuffer`、raw buffer 与 `RWStructuredBuffer` 风格 UAV 绑定覆盖常用数据上传和 compute 场景。
- `TextureLoader` 负责 DirectXTex/OpenEXR 解码和 texture cache 查询，再把 GPU staging 与可选 mip 生成委托给职责单一的底层服务。

### 几何、光追与场景

- Meshlet 构建与 mesh shader 公共数据位于 `Framework/Geometry` 和 `Framework/shaders/Meshlet`。
- `RayTracingAccelerationStructure`、`RayTracingShader`、`RayTracingShaderTable` 封装 BLAS/TLAS、ray tracing pipeline 和 shader-table dispatch。场景变化可增删、更新 instance，不必重建无关几何。
- 公共 `Scene` 保存 sample 资源路径需要的 camera、light、transform、PBR material 与 mesh 数据。
- `SurfaceEmitter` 定义矩形面积光和自发光 mesh 的 GPU 表示与采样数据。场景适配器构建共享 geometry 的 triangle CDF 与每实例数据，避免为每个重复实例的每个三角形存储完整灯光记录。

### 通用渲染模块

- `ReSTIRDIPass` 拥有 ReSTIR DI 的 history、pipeline variant，以及 RIS、temporal、spatial、final shading 的 dispatch 序列。调用方提供输出、motion vector、frame constant 和场景绑定回调。
- `ReSTIRGIPass` 拥有 packed GI reservoir、pipeline variant，以及 initial sampling、temporal、spatial、final shading 的 dispatch 序列。调用方提供间接光输出、motion vector、frame constant 和 Inline Ray Query 场景绑定回调。
- `Taa`、`NRD`、`SVGF` 是抗锯齿和降噪模块；NRD 会通过 `RenderContext` 把 native 状态变化回报给 RenderGraph。
- `DLSS` 管理 Native NGX DLSS SR/DLAA 评估与实验性 Streamline RR/FG frame-feature 路径。`RaytracingDemo` 将 `DLSS.cpp` 与 `StreamlineRuntime.cpp` 作为隐藏的外部源直接编译，因此普通 `Framework` 使用者不会继承厂商 SDK include 路径，也不会链接 `sl.interposer.lib`，同时 CMake 不会生成额外的 `FrameworkNvidiaFeatures` 工程。Framework 的 `StreamlineRuntime` 在创建 D3D12 设备前执行 `slInit`，设备创建后执行 `slSetD3DDevice`，并负责 capability query 与 Frame Generation 所需的通用 presentation 重建请求；queue/swap-chain 拦截完全交给自动 interposer。DX12Library 不再引用 Streamline，也不再定义 Frame Generation/Ray Reconstruction 能力接口。RR/FG 尚未完成支持硬件上的完整验证。
- CUDA interop 封装 shared D3D12 resource 与 external fence/semaphore 同步；当前 CUDA Bloom 使用此路径。

Framework 模块以复用为目标，但接口仍在演进，不能把它们理解为成熟公共渲染 SDK 的兼容层。

## RenderGraph：从声明到执行

`RenderGraph/` 把逻辑 pass/resource 声明编译成可执行计划：

```text
RenderPass 声明
    -> RenderGraphCompiler
    -> CompiledRenderGraph / RenderGraphExecutionPlan
    -> RenderGraphCommandExecutor
    -> D3D12 command queue
```

编译阶段负责 pass culling、依赖排序、resource-state plan、transient lifetime plan 和 execution batch；`RenderGraphCommandExecutor` 负责录制和提交；`RenderGraphProfiler` 负责可选的 Direct/Compute timestamp 与 CSV history。

### Queue 与同步

- pass 通过 `RenderPassQueue` 显式指定 `Direct`、`AsyncCompute` 或 `Copy`；系统不会自动推断 placement。
- `RenderGraphQueueScheduler` 保存每个逻辑 resource 的 last producer queue 与 submitted fence value；有依赖的 consumer 提交前会收到 GPU-side wait。
- `PassResourceStatePlan` 保存不可变的 per-pass transition、UAV、aliasing、初始化和 async handoff 工作。Executor 将该计划录入拥有该 pass 的 command list；各 list 在最终提交顺序中关闭时，`CommandList` 通过共享 `ResourceStateRegistry` 解析 transition 的初始状态。
- copy-compatible pass 可通过 `RenderGraphPassBuilder::UseCopyQueue()` 进入编译计划、Executor、QueueScheduler、Profiler 和 transient retirement 路径；当前主 sample 尚未声明 Copy-queue pass。
- transient resource 按本帧实际的 Direct/Compute/Copy fence 延迟退休。aliasing 目前是保守的：仅复用可证明在同一 queue 上的 lifetime，跨 queue aliasing 仍禁用。

### Active Pixel Compaction

当 `RaytracingDemo` 选择 `CompactedIndirect` 时，图先读取深度并通过全局原子计数把有效几何像素追加到 active-pixel list。PT、ReSTIR DI 和 ReSTIR GI 的 Inline compute 阶段根据该列表恢复逻辑屏幕坐标，只派发 `ceil(activeCount / 64)` 个 64-thread groups；DXR 阶段使用独立的 `D3D12_DISPATCH_RAYS_DESC`，其 `Width` 等于 `activeCount`。因此 `ActivePixelCount` 是有效像素数，不是光线数量，也不等于 Inline dispatch X。Finalize、token 和 readback 会校验 count 与 indirect arguments 的一致性。

### 多线程命令录制

Compiler 会把连续、显式声明为 parallel-safe 的 Direct pass 组成 recording batch；即使这些 pass 存在 GPU resource 依赖也可以并行录制。`RenderGraphTaskScheduler` 提供常驻 worker，并在关闭时 drain 已接受任务；每个 worker 独占 command allocator/list 和临时 descriptor 分配。提交始终采用 Compiler 生成的稳定拓扑顺序，pending aliasing barrier 会先于首次使用的 transition 写入。bindless frame page 同时由 Direct 与 Async Compute fence 退休，因此 descriptor table 镜像不会覆盖仍被 GPU 引用的页。因此它降低的是 CPU recording 成本，而不是让同一 Direct queue 的 GPU 工作并行执行。

观察 direct/compute overlap、GPU wait 和 CPU/GPU bubble 时，应使用 PIX Timing Capture。RenderGraph CSV 记录的是每条 queue 局部时间，更适合固定场景下反复 A/B。

## RaytracingDemo：集成 sample

`Demos/RaytracingDemo/` 是当前维护的主 sample。它承载功能选择、UI、场景选择和图拓扑；可复用的 GPU 机制应继续下沉到 Framework。

### 场景资源数据流

```text
Scene
    -> RaytracingDemoSceneResources
       -> texture/material builder
       -> geometry builder
       -> meshlet builder
       -> RTAS builder
    -> GPU scene buffer 与 bindless texture
```

`RaytracingDemoSceneResources` 是四个 builder 的 sample-facing facade。

### SceneImporter 契约

Demo 使用格式无关的静态入口 `SceneImporter::ImportFromFile()`：`.unity` 进入 Unity YAML parser，`.json` 进入项目场景 parser，`.fbx` 进入 Assimp FBX scene importer。三种格式最终都生成同一份 `Scene`：

```text
Scene
  -> nodes（局部/世界矩阵、父子关系）
  -> objects（节点引用、mesh 引用、稳定子网格索引、材质索引）
  -> materials（PBR 因子、UV scale/offset、纹理绑定）
  -> camera + 平行光/点光/Spot/面积光
```

FBX 外部纹理在成功解析后保留文件路径；嵌入纹理复制到拥有所有权的 `SceneEmbeddedTexture`。Demo resource builder 通过 `TextureLoader` 对两种来源使用同一套 GPU 上传和缓存流程，因此不需要先把 FBX 转成 Unity 场景或项目 JSON。mesh 导入启用 Assimp 的结构校验、三角化、非法数据过滤、四骨骼权重限制和 16 位索引安全的大 mesh 拆分；非法面、索引和骨骼引用在运行时明确报错，而不是依赖只在 Debug 生效的 assert。

### 已演示的渲染路径

- GBuffer：普通 raster、task/mesh shader，或 compute cull + `ExecuteIndirect`。
- 直接光：`None`、path tracing 或 inline ray-query ReSTIR DI；间接光：`None`、path tracing 或 inline ray-query ReSTIR GI。
- shader-table DXR 与 inline ray query 共用同一份 scene geometry、material、bindless texture、light buffer 和 acceleration structure。
- 平行光、点光、矩形面积光与自发光 surface emitter 都通过 GPU buffer 上传。平行光和点光的软阴影使用预编译 shader variant；矩形面积光直接采样发光面。
- 可选 NRD/SVGF、TAA、skybox、CUDA Bloom、Native DLSS SR/DLAA，以及实验性 Streamline RR/FG 围绕核心光照输出组合。

ReSTIR DI 在图中只有一个 `ReSTIR DI` pass。该 pass 调用 Framework 的 `ReSTIRDIPass::Execute`，在同一个 command-list scope 内依次录制 RIS、temporal（其中 shader 内包含 boiling filter）、spatial、final visibility/shading。这样 history 和 pipeline 归 Framework 所有，而 demo 仍负责图级数据流和 scene binding。

ReSTIR GI 同样只作为一个 `ReSTIR GI` 间接光 producer 进入图。它调用 Framework 的 `ReSTIRGIPass::Execute`，在同一个 command-list scope 内依次录制初始 BSDF 采样、temporal reservoir reuse、spatial reservoir reuse 和最终可见性/着色。Demo adapter 提供 GBuffer、TLAS、bindless scene data、直接光采样、自发光和环境光契约；该路径当前只支持 Inline Ray Query。

shader-table DXR 与 Inline 共用 scene/resource model，但当前 ReSTIR DI/GI 仅由 Inline 实现。手动切换到 DXR 时，若选择会跳过这些阶段，UI 会显示一次兼容性弹窗并持续显示红色警告；自动化切换不打开模态框。

### 调试和自动化

- 运行时 UI 按技术选择、场景/光源、降噪、upscaling、压力内容与调试控制分类。
- `RAYTRACING_DEMO_AUTOTEST=core`、`stress`、`matrix` 可以进行非交互启动与功能组合 smoke test。它用于发现崩溃/回归，不能替代画面验收。
- RenderGraph timestamp CSV 用于可重复的 pass timing；完整 queue 时间线应看 PIX。
- 当前路径仍是 Demo-specific，尚不能用一个机器可读 capture 同时解释 graph schedule、queue submission、resource/descriptor state、约束、readback 和复现元数据。下一工具优先级是 [Framework 诊断、自动化与 Profiler 规划](FrameworkDiagnosticsPlan.zh-CN.md) 中定义的 Framework-owned 契约。

## 当前边界

- 平台是 Windows/x64/D3D12，Shader Model 6.8 为基线。
- Async Compute 只有依赖和硬件允许 overlap 时才可能降低 GPU wall time；额外 fence、cache 与带宽竞争也可能令它变慢。
- Meshlet 是实验性的 GBuffer backend，不是完整的 visibility、streaming、residency 或 LOD 系统。
- FBX 场景导入已经进入 Framework `SceneImporter` 契约，但当前只选择一个活动相机并支持实用的 PBR 子集。Spot Light 会保留在 `Scene` 中，而 sample 当前 GPU 光照路径通过点光 fallback 显示；透明、clearcoat、transmission、动画播放和动态蒙皮场景更新仍需要单独契约。
- ReSTIR DI/GI、CUDA Bloom、DLSS SR/DLAA、Streamline RR/FG、Unity interop 都是工程实验；用于交付前必须逐硬件完成正确性、画质、稳定性、显存和性能验证。

使用示例和更细的功能边界见 [RaytracingDemo API Guide](RaytracingSampleApi.md)。
