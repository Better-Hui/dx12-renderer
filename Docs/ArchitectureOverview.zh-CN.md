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

## DX12Library：D3D12 边界

`DX12Library/` 保留 D3D12 的原生概念，主要包含：

- `D3D12DeviceContext`、`CommandQueue`、`CommandList`：封装 device，以及 Direct、Compute、Copy 三类 command queue。
- `Resource`、`Texture`、`Buffer`、structured/raw buffer、upload buffer 与 RTAS backing resource：管理原生 D3D12 allocation 和 resource view。
- `DescriptorAllocator`、`DynamicDescriptorHeap`、`FrameResourceRing`：管理 descriptor 以及逐帧资源寿命。GPU 可见 descriptor table 在此层完成，而不是由 demo 手写。
- `ResourceStateRegistry`、`ResourceStateTracker`：记录 transition、UAV 与 aliasing barrier。
- `GpuTimestampProfiler`：提供单条 queue 内的 GPU timestamp。
- window、swap chain、PIX marker、Streamline runtime 启动和 Unity D3D12 interop 边界同样位于这一层。

这层可以直接暴露 D3D12；`Framework` 的职责是向上提供更收敛的渲染器接口。

## Framework：可复用渲染功能

`Framework/` 的目标是让 demo 提供场景数据和功能策略，而把重复的 GPU 绑定、资源和 dispatch 逻辑收敛为通用模块。

### Pipeline 与资源绑定

- `CommandContext` 以统一的 bind/dispatch 风格录制 raster、compute、mesh shader 和 DXR 命令。
- Shader reflection 构建 `PipelineLayout`、`PipelineDescriptorPool`、`PipelineDescriptorSet` 和 `PipelineBindingSet`，通过资源名字完成绑定。
- `BindlessDescriptorHeap` 将 canonical descriptor 保存在 CPU-only heap，再镜像到按 fence 退休的 shader-visible frame page。材质保存稳定的 descriptor index；`CommandContext` 在当前 page 中准备使用 direct heap indexing 的 descriptor table。这样 CPU 更新 descriptor 时不会覆盖仍被 Direct 或 Async Compute GPU 工作读取的页。
- `ShaderVariantManager` 在启动期编译代码显式请求的变体，指纹覆盖源码、include、宏和编译参数并缓存字节码。它不是运行时热更，也不会枚举所有理论宏组合。
- `SharedUploadBuffer`、临时 descriptor 分配、`StructuredBuffer`、raw buffer 与 `RWStructuredBuffer` 风格 UAV 绑定覆盖常用数据上传和 compute 场景。

### 几何、光追与场景

- Meshlet 构建与 mesh shader 公共数据位于 `Framework/Geometry` 和 `Framework/shaders/Meshlet`。
- `RayTracingAccelerationStructure`、`RayTracingShader`、`RayTracingShaderTable` 封装 BLAS/TLAS、ray tracing pipeline 和 shader-table dispatch。场景变化可增删、更新 instance，不必重建无关几何。
- 公共 `Scene` 和 `SceneImporter` 同时读取 Unity 文本序列化的 `.unity` YAML 与 JSON，统一得到 camera、light、transform、PBR material 与 mesh 数据。
- `SurfaceEmitter` 定义矩形面积光和自发光 mesh 的 GPU 表示与采样数据。场景适配器构建共享 geometry 的 triangle CDF 与每实例数据，避免为每个重复实例的每个三角形存储完整灯光记录。

### 通用渲染模块

- `ReSTIRDIPass` 拥有 ReSTIR DI 的 history、pipeline variant，以及 RIS、temporal、spatial、final shading 的 dispatch 序列。调用方提供输出、motion vector、frame constant 和场景绑定回调。
- `ReSTIRGIPass` 拥有 packed GI reservoir、pipeline variant，以及 initial sampling、temporal、spatial、final shading 的 dispatch 序列。调用方提供间接光输出、motion vector、frame constant 和 Inline Ray Query 场景绑定回调。
- `Taa`、`NRD`、`SVGF` 是抗锯齿和降噪模块；NRD 会通过 `RenderContext` 把 native 状态变化回报给 RenderGraph。
- `DLSS` 管理 Native NGX DLSS SR/DLAA 与实验性 Streamline RR/FG 的边界。RR/FG 需要启动期 interposer，尚未完成支持硬件上的完整验证。
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

- pass 通过 `RenderPassQueue` 显式指定 `Direct` 或 `AsyncCompute`；系统不会自动推断 placement。
- `RenderGraphQueueScheduler` 保存每个逻辑 resource 的 last producer queue 与 submitted fence value；有依赖的 consumer 提交前会收到 GPU-side wait。
- `PassResourceStatePlan` 保存不可变的 per-pass transition、UAV、aliasing、初始化和 async handoff 工作。Executor 将该计划录入拥有该 pass 的 command list；各 list 在最终提交顺序中关闭时，`CommandList` 通过共享 `ResourceStateRegistry` 解析 transition 的初始状态。
- 底层具备 Copy queue，但 RenderGraph 尚未调度 Copy-queue pass。
- transient resource 按本帧实际的 Direct/Compute fence 延迟退休。aliasing 目前是保守的：仅复用可证明在同一 queue 上的 lifetime，跨 queue aliasing 仍禁用。

### 多线程命令录制

Compiler 会把连续、显式声明为 parallel-safe 的 Direct pass 组成 recording batch；即使这些 pass 存在 GPU resource 依赖也可以并行录制。`RenderGraphTaskScheduler` 提供常驻 worker，并在关闭时 drain 已接受任务；每个 worker 独占 command allocator/list 和临时 descriptor 分配。提交始终采用 Compiler 生成的稳定拓扑顺序，pending aliasing barrier 会先于首次使用的 transition 写入。bindless frame page 同时由 Direct 与 Async Compute fence 退休，因此 descriptor table 镜像不会覆盖仍被 GPU 引用的页。因此它降低的是 CPU recording 成本，而不是让同一 Direct queue 的 GPU 工作并行执行。

观察 direct/compute overlap、GPU wait 和 CPU/GPU bubble 时，应使用 PIX Timing Capture。RenderGraph CSV 记录的是每条 queue 局部时间，更适合固定场景下反复 A/B。

## RaytracingDemo：集成 sample

`Demos/RaytracingDemo/` 是当前维护的主 sample。它承载功能选择、UI、场景选择和图拓扑；可复用的 GPU 机制应继续下沉到 Framework。

### 场景与资源数据流

```text
Unity YAML 或 JSON
    -> SceneImporter
    -> Scene
    -> RaytracingDemoSceneResources
       -> texture/material builder
       -> geometry builder
       -> meshlet builder
       -> RTAS builder
    -> GPU scene buffer 与 bindless texture
```

`RaytracingDemoSceneResources` 是四个 builder 的 sample-facing facade。压力球的增删会复用共享 geometry 和已有 BLAS，只更新 meshlet instance 数据与 TLAS。

### 已演示的渲染路径

- GBuffer：普通 raster、task/mesh shader，或 compute cull + `ExecuteIndirect`。
- 直接光：`None`、path tracing 或 inline ray-query ReSTIR DI；间接光：`None`、path tracing 或 inline ray-query ReSTIR GI。
- shader-table DXR 与 inline ray query 共用同一份 scene geometry、material、bindless texture、light buffer 和 acceleration structure。
- 平行光、点光、矩形面积光与自发光 surface emitter 都通过 GPU buffer 上传。平行光和点光的软阴影使用预编译 shader variant；矩形面积光直接采样发光面。
- 可选 NRD/SVGF、TAA、skybox、CUDA Bloom、Native DLSS SR/DLAA，以及实验性 Streamline RR/FG 围绕核心光照输出组合。

ReSTIR DI 在图中只有一个 `ReSTIR DI` pass。该 pass 调用 Framework 的 `ReSTIRDIPass::Execute`，在同一个 command-list scope 内依次录制 RIS、temporal（其中 shader 内包含 boiling filter）、spatial、final visibility/shading。这样 history 和 pipeline 归 Framework 所有，而 demo 仍负责图级数据流和 scene binding。

ReSTIR GI 同样只作为一个 `ReSTIR GI` 间接光 producer 进入图。它调用 Framework 的 `ReSTIRGIPass::Execute`，在同一个 command-list scope 内依次录制初始 BSDF 采样、temporal reservoir reuse、spatial reservoir reuse 和最终可见性/着色。Demo adapter 提供 GBuffer、TLAS、bindless scene data、直接光采样、自发光和环境光契约；该路径当前只支持 Inline Ray Query。

### 调试和自动化

- 运行时 UI 按技术选择、场景/光源、降噪、upscaling、压力内容与调试控制分类。
- `Save Scene` 会将 camera、skybox、light-group 开关和 directional/point/area light 状态写到 `<source scene>.runtime.json`；原始 Unity/JSON 场景不变。`Save Camera` 是单独的 Unity 场景 camera 写回路径。
- `RAYTRACING_DEMO_AUTOTEST=core`、`stress`、`matrix` 可以进行非交互启动与功能组合 smoke test。它用于发现崩溃/回归，不能替代画面验收。
- RenderGraph timestamp CSV 用于可重复的 pass timing；完整 queue 时间线应看 PIX。

## 当前边界

- 平台是 Windows/x64/D3D12，Shader Model 6.9 为基线。
- Async Compute 只有依赖和硬件允许 overlap 时才可能降低 GPU wall time；额外 fence、cache 与带宽竞争也可能令它变慢。
- Meshlet 是实验性的 GBuffer backend，不是完整的 visibility、streaming、residency 或 LOD 系统。
- 场景导入有意保持有限：完整 prefab/nested prefab、skinned mesh、`LODGroup`、asset database live sync 与完整非 PBR material 都不在当前范围内。
- ReSTIR DI/GI、CUDA Bloom、DLSS SR/DLAA、Streamline RR/FG、Unity interop 都是工程实验；用于交付前必须逐硬件完成正确性、画质、稳定性、显存和性能验证。

使用示例和更细的功能边界见 [RaytracingDemo API Guide](RaytracingSampleApi.md)。
