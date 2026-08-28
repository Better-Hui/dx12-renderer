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

`Framework/tools/` 始终作为 `Framework` 工程的真实物理目录显示，工具源码不会通过 MSBuild `Link` 映射到虚拟路径。`Framework/tools/CMakeLists.txt` 只负责在 `DX12_RENDERER_BUILD_DEVELOPER_TOOLS=ON` 时生成 `RendererDiagnostics` 和 `UnitySceneDump`；该开关控制是否构建工具 target，不控制 `Framework` 工程树是否显示 `tools/`。因此工具关闭时仍能在解决方案中看到磁盘上的完整目录，默认 solution 则不增加工具工程。

## DX12Library：D3D12 边界

`DX12Library/` 保留 D3D12 的原生概念，主要包含：

- `D3D12DeviceContext`、`CommandQueue`、`CommandList`：封装 device，以及 Direct、Compute、Copy 三类 command queue。
- `CommandList` 只保留普通命令录制、descriptor staging 与 command-list 生命周期跟踪，不再声明 transition/UAV/aliasing barrier 方法。barrier 编码位于 renderer-internal 的 `CommandListInternalAccess`，仅供 RenderGraph 和经过审计的 upload、readback、mip、swapchain/present、shared upload 与 RTAS 边界使用。`ResourceUploader` 负责 staging upload 和资源替换，`MipGenerator` 负责可复用的 mip 生成 pipeline。
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

- `AutoExposure`、`ReSTIRDIPass`、`ReSTIRGIPass`、Raster `Bloom`、`NRD`、`SVGF` 和 `TAA` 通过 `AddPasses(RenderGraphBuilder&, Inputs)` 直接注册逻辑阶段；Builder 只在构图调用期间传入，Framework 不保存它。
- ReSTIR 的 persistent reservoir/history 与 Auto Exposure 的 histogram/adaptation 资源以 logical imported resource 接入图。Framework 继续拥有物理 allocation，RenderGraph 负责拓扑、状态计划、UAV 顺序和跨 queue hazard。
- 构图期 feature scratch texture 通过 `RenderGraphBuilder::CreateTexture()` 声明，再由组合根合并进 graph texture description。Raster Bloom 的 downsample/upsample 金字塔使用该机制；纹理由 RG 管理并参与 transient aliasing。allocator 会在 placed resource 真正首次使用的位置写入 alias barrier，并在图的首次 transition 前将该资源登记为 `COMMON`。
- `NRD` 注册 Prepare、native Denoise 和 Composite；SDK 可管理 native 段内部的临时状态，但图资源在段边界保持 RG 声明状态，NRD 不手写图资源 barrier。`SVGF` 注册 imported 奇偶 temporal history、水平/垂直 A-Trous 和 Composite；`TAA` 围绕 imported ping-pong history 注册 Resolve 与 History Copy。不同的 logical read/write ID 让图保持无环，物理 history 映射只在 rendered-frame 之间推进。
- `DLSS` 管理 Native NGX DLSS SR/DLAA 评估与实验性 Streamline RR/FG frame-feature 路径。`RaytracingDemo` 将 `DLSS.cpp` 与 `StreamlineRuntime.cpp` 作为隐藏的外部源直接编译，因此普通 `Framework` 使用者不会继承厂商 SDK include 路径，也不会链接 `sl.interposer.lib`，同时 CMake 不会生成额外的 `FrameworkNvidiaFeatures` 工程。Framework 的 `StreamlineRuntime` 在创建 D3D12 设备前执行 `slInit`，设备创建后执行 `slSetD3DDevice`，并负责 capability query 与 Frame Generation 所需的通用 presentation 重建请求；queue/swap-chain 拦截完全交给自动 interposer。DX12Library 不再引用 Streamline，也不再定义 Frame Generation/Ray Reconstruction 能力接口。RR/FG 尚未完成支持硬件上的完整验证。
- CUDA interop 封装 shared D3D12 resource 与 external fence/semaphore 同步；当前 CUDA Bloom 使用此路径。
- `Framework/Diagnostics` 拥有机器可读 capture session、typed event schema、有界缓冲、确定性 automation runner 和产物导出。`DX12Library`/`RenderGraph` 只接收可选的 non-owning telemetry sink，不反向依赖 Framework；Demo 只注册自己的 control、observation 与 scenario。

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

模块依赖方向是 `DX12Library <- RenderGraph <- Framework <- RaytracingDemo`。Framework 可以注册可复用子图，但 RenderGraph 不反向依赖 Framework。`VerifyRenderGraphOwnership` 会在 Framework 构建前扫描 DX12Library、RenderGraph、Framework 与 Demos 的一方源码，禁止普通算法直接 barrier、上层访问 `ResourceStateTracker`、Demo 引入内部桥、descriptor binding 携带资源状态、恢复已删除的 auto-barrier 机制，以及 Framework 保存 Builder 引用或指针。barrier bridge 白名单只保留 9 个明确文件边界，`CommandList.cpp`、`CommandContext.cpp` 和 NRD 均不在其中。

### Queue 与同步

- `AddPass`、`AddComputePass`、`AddCopyPass` 和 `AddExternalPass` 在创建 pass 时明确 queue 意图：分别为 Direct、Async Compute（不可用时回退 Direct）、Copy（必须可用）和 Direct external interop。
- `RenderGraphQueueScheduler` 保存每个逻辑 resource 的 last producer queue 与 submitted fence value；有依赖的 consumer 提交前会收到 GPU-side wait。
- `PassResourceStatePlan` 保存不可变的 per-pass transition、UAV、aliasing、初始化和 async handoff 工作。Executor 将该计划录入拥有该 pass 的 command list；各 list 在最终提交顺序中关闭时，`CommandList` 通过共享 `ResourceStateRegistry` 解析 transition 的初始状态。
- `ClearUnorderedAccessUint` 只录制 clear，不隐式追加 UAV barrier。同一资源后续继续写入时，clear 与写入必须拆成不同 pass 或通过声明形成显式 WAW 依赖，由 Compiler 安排 UAV 顺序。
- copy-compatible pass 可通过 `AddCopyPass()` 进入编译计划、Executor、QueueScheduler、Profiler 和 transient retirement 路径；受维护的 `Copy Queue Validation` 路径为 Direct HDR producer -> Copy queue -> Async Compute consumer -> Direct consumer，并通过 Diagnostics 断言 planned state、producer fence/wait、submission 和 retirement fence。
- Compiler 会把 queue 相同且 direct preamble/aliasing 关系兼容的连续 Async Compute/Copy pass 合并为 non-direct batch；不兼容的资源交接会形成新的 batch。
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

Demo 使用格式无关的静态入口 `SceneImporter::ImportFromFile()`：`.unity` 进入 Unity YAML parser，`.json` 进入项目场景 parser，`.fbx` 进入 Assimp FBX scene importer，受支持的 `.xml` 进入 Mitsuba XML importer。所有格式最终都生成同一份 `Scene`：

```text
Scene
  -> nodes（局部/世界矩阵、父子关系）
  -> objects（节点引用、mesh 引用、稳定子网格索引、材质索引）
  -> materials（PBR 因子、UV scale/offset、纹理绑定）
  -> camera + 平行光/点光/Spot/面积光
```

FBX 外部纹理在成功解析后保留文件路径；嵌入纹理复制到拥有所有权的 `SceneEmbeddedTexture`。Demo resource builder 通过 `TextureLoader` 对两种来源使用同一套 GPU 上传和缓存流程，因此不需要先把 FBX 转成 Unity 场景或项目 JSON。mesh 导入启用 Assimp 的结构校验、三角化、非法数据过滤、四骨骼权重限制和 16 位索引安全的大 mesh 拆分；非法面、索引和骨骼引用在运行时明确报错，而不是依赖只在 Debug 生效的 assert。

Mitsuba XML importer 是有意保持紧凑的兼容路径：会展开场景内 `<default>` 变量，支持 perspective sensor、OBJ shape 及其 `to_world` matrix、rectangle area emitter，以及顶层 `spot` emitter 的 `intensity`、`cutoffAngle`、`beamWidth` 和可选有限 `range`。它会转换到 DirectX 运行时的坐标约定：该反射会先把 Mitsuba sensor 的局部 `+Z` 视线轴映射为渲染器局部 `-Z`，再构造左手相机，以保留原始世界空间观察方向。其有意受限的 PBR 转换会展开 `twosided`、`mask`、`bumpmap`，按 diffuse/plastic/conductor/dielectric 映射金属度和粗糙度启发式，并导入常量 base color 与 `reflectance`、`diffuse_reflectance`、`base_color` 的 bitmap 绑定。它不复现完整 Mitsuba 的 transmission、alpha、bump/normal、spectral IOR 等高级 BSDF 语义。相对 OBJ 与纹理引用均限制在 XML 场景目录内。`Assets/Scenes/CountryKitchen/scene.xml` 是 Demo 的默认启动场景，并覆盖此路径；在 streaming 或 cooked geometry 落地前，其 295 个独立 OBJ 属于较重的启动路径。

### 已演示的渲染路径

- GBuffer：普通 raster、task/mesh shader，或 compute cull + `ExecuteIndirect`。
- 直接光：`None`、path tracing 或 inline ray-query ReSTIR DI；间接光：`None`、path tracing 或 inline ray-query ReSTIR GI。
- shader-table DXR 与 inline ray query 共用同一份 scene geometry、material、bindless texture、light buffer 和 acceleration structure。
- 无人值守的 `rtas` 场景覆盖基础动态 RTAS 路径；`dynamic-scene` 是当前完整矩阵：task shader 和 compute-indirect Meshlet GBuffer 均会通过图声明更新普通 vertex、compacted Meshlet vertex 与 bounds、transform/instance buffer、dirty BLAS 和既有 TLAS，随后验证 restore frame。自发光目标还会刷新 mesh surface-emitter 数据。runtime skinning output 当前明确不支持，绝不以过期数据静默 fallback。
- 平行光、点光、Spot Light、矩形面积光与自发光 surface emitter 都通过 GPU buffer 上传。平行光和点光的软阴影使用预编译 shader variant；Spot Light 在直接光采样中计算锥形衰减，矩形面积光直接采样发光面。
- 可选 NRD/SVGF、TAA、skybox、Framework Raster Bloom、CUDA Bloom、Native DLSS SR/DLAA、HDR10/PQ 呈现，以及实验性 Streamline RR/FG 围绕核心光照输出组合。

ReSTIR DI 由 Demo 调用 Framework 的 `ReSTIRDIPass::AddPasses`。Framework 分别注册 `Initial Sampling`、`Temporal Resampling`、`Boiling Filter`、`Spatial Resampling` 和 `Shade`，用 token 与 imported reservoir/history 连接。Demo 只提供 logical scene input 和运行时 resolver，不再调度内部阶段或编码 barrier。

ReSTIR GI 通过 `ReSTIRGIPass::AddPasses` 分别注册 `Initial Sampling`、`Temporal Resampling`、`Spatial Resampling` 和 `Shade`。imported ping-pong history 根据运行时 frame index 解析，不需要保存 Builder。Demo adapter 提供 GBuffer、TLAS、bindless scene data、直接光采样、自发光和环境光契约；该路径当前只支持 Inline Ray Query。

Raster Bloom 由 `Bloom::AddPasses` 注册 `Bloom Prefilter`、逐级 Downsample、逐级 Upsample 和 `Bloom Composite`。Demo 只提供 graph resource ID、分辨率表达式、运行时参数与金字塔层数，不再分配金字塔或编码 barrier。CUDA Bloom 继续保留为 Demo-owned external queue/semaphore 路径。

NRD 由 `NRD::AddPasses` 注册 `NRD Prepare Inputs`、`NRD Native Denoise` 和 `NRD Composite`。RenderGraph 在 native 段入口准备 noisy/input SRV 与 output UAV；NRI/NRD 使用 `restoreInitialState` 在 SDK 内部工作完成后恢复这些状态，因此 native 调用不会成为算法手写 barrier 的例外。

SVGF 由 `SVGF::AddPasses` 注册 parity-aware Temporal、每次迭代的 Horizontal/Vertical A-Trous 以及 Composite。奇偶 history color/moments 是 Framework-owned imported ping-pong texture；A-Trous 迭代数属于 Demo topology key，修改 UI 配置会重建图而不是只改变一个失效的运行时值。TAA 同样由 `TAA::AddPasses` 在 Framework-owned imported ping-pong history 上注册 Resolve 与 History Copy，首帧 history 无效时把 history 权重强制为 0，并只在 `OnRenderedFrame()` 中推进物理 history index，保证 imported resolver 在整次执行中稳定。

shader-table DXR 与 Inline 共用 scene/resource model，但当前 ReSTIR DI/GI 仅由 Inline 实现。手动切换到 DXR 时，若选择会跳过这些阶段，UI 会显示一次兼容性弹窗并持续显示红色警告；自动化切换不打开模态框。

### HDR10/PQ 呈现

HDR10 是可选的原生呈现路径，通过当前 `IDXGIOutput6` 输出检测能力。可用时 swapchain 使用 `R10G10B10A2_UNORM`、`DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`，并写入由显示器能力派生的 HDR10 metadata。RenderGraph 让 Auto Exposure 的输出保持为 `R16G16B16A16_FLOAT` 线性场景色；只有最终 presentation shader 会进行面向显示器的 tone map、Rec.709 到 Rec.2020 转换以及 ST.2084/PQ 编码，再写入 swapchain。这样既不会发生格式不匹配的直接 copy，也不会让中间图像处理 pass 承担 HDR 编码。

可由 `[Display] HDR10`、`RAYTRACING_DEMO_HDR10=0|1`、`--hdr10` 或 `Display Output` UI 请求。显示器没有开启 Windows HDR/PQ 或不支持时，渲染器保持既有 SDR swapchain，并通过 Diagnostics 的 `presentation_hdr10_output` 报告 fallback。由于当前没有验证 Streamline-proxied HDR swapchain，DLSS Frame Generation 与这条原生 HDR10 路径互斥。HDR 只扩展显示能力，不会自动修正场景光照单位、曝光行为或刻意保持简单的 tone mapper。

### 调试和自动化

- 运行时 UI 按技术选择、场景/光源、降噪、upscaling、压力内容与调试控制分类。
- `RAYTRACING_DEMO_AUTOTEST=core`、`stress`、`matrix` 由 Demo 注册为具名 Framework automation scenario；它们通过 control/observation 切换状态，不注入桌面输入。smoke test 用于发现崩溃/回归，不能替代画面验收。
- 可选的 `RendererDiagnostics` developer tool 提供 `run`、`inspect`、`query`、`diff`、`reproduce`、`selftest`。单个 capture 同时关联 RenderGraph schedule/state/lifecycle、queue submission/fence、resource/descriptor identity、assertion、CPU/GPU timing 和复现元数据。
- `inspect` 输出面向 AI 的 JSON verdict、capture 完整性、疑似问题域、假设、相关证据和下一条建议；丢事件或非终态 capture 是 `incomplete`，不会被当作 clean pass。
- RenderGraph timestamp CSV 用于可重复的 pass timing；不同 queue 的 timestamp 不是校准后的全局 overlap 时间线，完整 queue 时间线和驱动行为仍应看 PIX/RenderDoc。

## 当前边界

- 平台是 Windows/x64/D3D12，Shader Model 6.8 为基线。
- Async Compute 只有依赖和硬件允许 overlap 时才可能降低 GPU wall time；额外 fence、cache 与带宽竞争也可能令它变慢。
- Meshlet 是实验性的 GBuffer backend，不是完整的 visibility、streaming、residency 或 LOD 系统。
- HDR10 同时要求 Windows HDR 已开启、HDR 显示器存在且当前输出兼容；SDR 始终是安全 fallback。初版 HDR 映射刻意保守（Reinhard 后进入 PQ）；显示器校准、局部适应、wide-gamut 资产管理与 HDR 画质验收仍是后续工作。
- FBX 场景导入已经进入 Framework `SceneImporter` 契约，但当前只选择一个活动相机并支持实用的 PBR 子集。Spot Light 会经 `Scene`、`LightingGpuResources`、相机计数进入 Inline PT/ReSTIR DI/GI 的直接光采样，不再走点光 fallback；透明、clearcoat、transmission、动画播放和 runtime skinning output 仍需要单独契约。在 skinned 输出能同时驱动 raster、Meshlet 和 BLAS 前，dynamic-scene capability 会明确报告 skinned update 不支持。
- Framework feature 已可使用 `GpuReadbackBuffer` 和非阻塞 ring-slot `GpuReadbackTexture`；compacted active-pixel 验证仍使用它们。`DiagnosticsImageCapture` 以独立 Direct copy、跨帧 fence `Poll()`、RGBA8 统计和最多两个后台 PNG writer 形成通用 texture request/image assertion/attachment 闭环；terminal finalize 在 shutdown `Drain()` 后执行，避免最后一帧异步图像缺失。device removal 会附加包含 HRESULT、breadcrumb 和 page-fault allocation 的 `dred.txt`。OIDN 在匹配 NVIDIA adapter 上改用 D3D12 shared buffer/fence 与 CUDA `Quality::Fast`，不再把 HDR 图像读回 CPU；仅在 CUDA 初始化或 external-memory import 失败时退回 readback/CPU `Fast`。高事件量 capture 可能很大，应使用 `--max-events` 和 `dropped_event_count` 判断证据完整性；剩余 Diagnostics 工作是实际 shader access 对图声明校验、更多 cross-queue/lifetime invariant，以及完整 session 的后台归档、压缩和 retention policy。
- ReSTIR DI/GI、CUDA Bloom、DLSS SR/DLAA、Streamline RR/FG、Unity interop 都是工程实验；用于交付前必须逐硬件完成正确性、画质、稳定性、显存和性能验证。

使用示例和更细的功能边界见 [RaytracingDemo API Guide](RaytracingSampleApi.md)。
