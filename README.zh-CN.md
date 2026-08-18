# DX12 Renderer

> 一个基于 DirectX 12 的实验性渲染器仓库。它用于验证和学习渲染器架构、RenderGraph、光线追踪、Meshlet，以及 CUDA/D3D12 互操作。

[English README](README.md) · [架构总览](Docs/ArchitectureOverview.zh-CN.md) · [RaytracingDemo API 说明（英文）](Docs/RaytracingSampleApi.md)

## 这是什么项目

本仓库 fork 自 [Delt06/dx12-renderer](https://github.com/Delt06/dx12-renderer)，共同历史从上游提交 `1db3a62d3eb7b8e0ff228090cca8dcf8e7e6adc9` 开始。

上游代码提供了项目的基础。本 fork 在此之上继续做 Framework、RenderGraph 和 `RaytracingDemo` 的实验性扩展，重点是把一组可运行、可调试的渲染功能放在同一个 sample 中。它不是上游项目的官方后续版本，也不试图宣称自己的抽象、接口或性能优于成熟渲染框架。

如果你刚接触本仓库，建议从 `Demos/RaytracingDemo/` 开始：它是当前维护的主 sample，也是了解**本项目 API 如何使用**的入口。

## 本 fork 在上游基础上增加了什么

- **底层 D3D12 封装**：`DX12Library` 在上游基础上扩展了 device context、queue、command list、resource state、fence、descriptor allocation、swap chain 和 application 生命周期封装。`CommandList` 只负责命令录制，数据上传与 mip 生成由独立服务承担；raw D3D12 对象所有权与同步逻辑应留在这一层，而不是散落到功能代码中。
- **Framework 层接口**：围绕 `CommandContext` 提供 pipeline、descriptor set、bindless descriptor、DXR、mesh shader 和通用 rendering feature 等封装，demo 作者通常通过这些接口完成资源绑定和命令录制。
- **RenderGraph**：pass 只需声明逻辑资源读写；Compiler 将其编译为不可变的排序、resource state、aliasing、queue dependency 与 execution plan，Executor 统一处理 barrier、queue wait、提交和可选的分 queue GPU timing。demo pass 不需要自行处理 raw DX12 同步。
- **显式异步计算**：pass 可以指定使用 `Direct` 或 `AsyncCompute` queue；RenderGraph 负责跨 queue 的 GPU fence 等待与资源交接。
- **光线追踪与降噪**：同一场景可在 inline ray query 和 shader-table DXR 之间切换，并可使用 NRD 或 SVGF。
- **材质着色模型**：默认 GGX 金属度/粗糙度 PBR 评估已归入 Framework；sample UI 可选择实验性的 `Stylized Comic` 风格化 PBR-NPR 变体。
- **Meshlet 实验路径**：包含 Meshlet 构建、GPU 资源、task shader / compute-indirect 两种 GBuffer 后端，以及只更新实例数据的增量路径。
- **ReSTIR DI 直接光**：提供 RIS、时域/Boiling/空域复用、各阶段的可见性与 bias correction 配置，以及最终着色。
- **ReSTIR GI 间接光**：基于 Inline Ray Query，提供初始 BSDF 采样、时域复用、空域复用、Jacobian correction 和最终可见性/着色。
- **CUDA Bloom**：演示 D3D12 shared resource、shared fence 与 CUDA external semaphore 的互操作流程。
- **实验性帧特性**：接入 Native NGX DLSS SR/DLAA，以及 Framework 管理的 Streamline Ray Reconstruction 和 Frame Generation。DX12Library 只暴露通用的设备创建前/后 runtime 生命周期；queue 与 swap chain 仍走普通 D3D12/DXGI 创建路径，由链接的 Streamline interposer 完成拦截。这些路径尚未完成可交付验证。
- **调试与分析工具**：集成 PIX event、RenderGraph timing history/CSV 和运行时调试 UI。

这些内容的定位是“可运行的工程实验和 API 示例”。其中不少封装仍在演进，尤其是异步计算和 Meshlet；请把它们视为当前仓库的实现方式，而不是通用最佳实践。

## `RaytracingDemo`：本项目 API 的使用示例

`RaytracingDemo` 的目的不是展示一堆零散的 D3D12 调用，而是演示本项目 Framework 和 RenderGraph 的使用方式。通常，一个 pass 应通过 Framework 提交命令，并通过 RenderGraph 声明自己读取和写入的资源；`ID3D12Device`、command list、root signature、descriptor heap、barrier 和 fence 等底层细节应留在 demo 边界之下，只有现有抽象确实无法覆盖需求时才向下扩展。

例如，compute pass 的命令录制使用 `CommandContext`：

```cpp
CommandContext commands(commandList);
commands.BindPipeline(computeShader);
commands.BindDescriptorSet(computeShader.GetDescriptorSet());
commands.Dispatch(groupCountX, groupCountY, 1);
```

同样的接口风格覆盖 raster、compute、mesh shader 和 DXR：`BindPipeline`、`BindDescriptorSet`、`Draw`、`Dispatch`、`DispatchMesh`、`DispatchRays`。当已有 Framework 封装可用时，维护中的 sample pass 应优先使用它；只有 Framework 目前没有覆盖需求时，才在底层使用 raw D3D12。

RenderGraph pass 通过输入、输出和 queue 类型描述工作：

```cpp
auto pass = RenderGraph::RenderPass::Create(
    L"Example Compute",
    { { inputId, RenderGraph::InputType::NonPixelShaderResource } },
    { { outputId, RenderGraph::OutputType::UnorderedAccess } },
    execute,
    RenderGraph::RenderPassQueue::AsyncCompute);
```

输入/输出声明会参与依赖分析与资源状态安排。`AsyncCompute` 表示“这个 pass 明确请求 compute queue”，不是自动性能优化开关：RenderGraph 会处理必要的 GPU fence 等待，但不会自动判断哪个 pass 最适合异步、拆分 pass，或保证一定产生 direct/compute overlap。

当前 queue 提交、last-writer 和跨 queue fence 由 `RenderGraphQueueScheduler` 管理。Compiler 为每个 pass 生成不可变的 transition/aliasing 计划，Executor 把它录入所属的 command list；`CommandList` 在最终提交顺序中通过共享 `ResourceStateRegistry` 解析每条 list 的初始状态。因此 CPU 录制先后不会改变 GPU 的资源状态与执行顺序。

`RenderGraphRoot` 的 device 和 queue 由应用组合根显式注入。执行路径已经拆为 `RenderGraphCommandExecutor`（pass 录制与提交）和 `RenderGraphProfiler`（可选的 Direct/Async Compute GPU timing）。`RaytracingDemo` 也遵循同一边界：`RaytracingDemoPassResources` 提供 pass 所需对象，`RaytracingDemoPassConfig` 提供显式运行时配置，因此 pass lambda 不再捕获整个 Demo，也不依赖 `friend` 访问私有成员。

## 你可以从 sample 中看到的功能

| 方向 | 具体内容 |
| --- | --- |
| GBuffer | 常规 raster GBuffer，以及实验性的 Meshlet GBuffer 路径。 |
| 光线追踪 | inline ray query compute shader 与 shader-table DXR，可在运行时切换。 |
| 材质着色 | Framework 统一的 GGX 金属度/粗糙度 PBR，以及 sample 可选的 `Stylized Comic` 风格化 PBR-NPR 变体。 |
| 光照 | direct lighting 与 indirect lighting 分离后再 composite；直接光可选 ReSTIR DI，Inline Ray Query 间接光可选 ReSTIR GI。 |
| 软阴影 | 平行光和点光源使用预编译 Hard/Soft Shader 变体；面积光继续采样真实发光面。 |
| 降噪 | NRD 与 SVGF 两条可选路径；NRD 的 native 状态变化会回写 RenderGraph。 |
| DLSS 与 Streamline | 实验性的 NGX DLSS SR/DLAA 与 Streamline RR/FG 资源准备路径；是否可用由启动配置和运行时 capability query 决定。 |
| Meshlet | task shader 和 compute-indirect 后端，以及 cluster 调试显示。 |
| CUDA 互操作 | 基于 shared resource / shared fence 的 Bloom 后处理。 |
| 性能分析 | PIX scope，以及 Direct、Compute queue 分别记录的 RenderGraph GPU timing / CSV。 |

## 环境与依赖

### 基础环境

- Windows 10/11，x64。
- Visual Studio 2022，以及 MSVC C++ desktop toolchain 和 Windows SDK。
- CMake 3.22 或更新版本；当前 NRI/NRD 源码构建以此为最低版本。
- 支持 **Shader Model 6.8** 的 D3D12 GPU 与驱动。DXR、mesh shader 和 CUDA 路径还取决于各自的硬件和驱动能力。
- **CUDA Toolkit 12.8**。当前 CMake 配置阶段会构建 CUDA interop/Bloom，因此即使运行时关闭 Bloom，配置时仍需要 CUDA。

### Shader Model 6.8 基线

项目中的 raster、compute、task/mesh 和 DXR library shader 均由 DXC 按 Shader Model 6.8 编译，即 `vs/ps/cs/as/ms/lib_6_8`。仓库在 `D3D12/` 随附 DirectX Agility SDK **1.619.5** 运行时，在 `External/AgilitySDK/include/` 随附匹配的 C++ 头文件；CMake 会检查这套 redist，启动时还会拒绝不支持 Shader Model 6.8 的驱动。

### vcpkg 包

先安装以下依赖：

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh meshoptimizer
```

配置时设置 `VCPKG_ROOT`，或手动指定 `CMAKE_TOOLCHAIN_FILE`。

### 随仓库提供或集成的组件

| 组件 | 位置 / 获取方式 | 用途 |
| --- | --- | --- |
| DirectX Agility SDK 1.619.5 | `D3D12/`、`External/AgilitySDK/include/` | SM6.8 基线所需的运行时 redist 和匹配的 C++ 头文件。 |
| DirectX Shader Compiler | 优先 `DXC/dxc.exe`，否则使用 Windows SDK 的 `dxc.exe` | 编译 ray tracing、task、mesh、compute 等 shader。 |
| WinPixEventRuntime | `WinPixEventRuntime/` | 向 PIX 写入 CPU/GPU event marker。 |
| Dear ImGui | `External/ImGui/` Git submodule，固定为 `v1.91.9` | 运行时调试 UI；Framework 直接编译官方源码，项目特有的数值控件行为位于 `Framework/UI/NumericWidgets`。 |
| NVIDIA NRD / NRI | `External/NRD/`、`External/NRI/` Git submodule | 降噪路径及其 API 层；CMake 会从固定的上游提交构建 D3D12 库。 |
| NVIDIA DLSS SDK | `External/DLSS/` Git submodule | 实验性的 Native NGX SR/DLAA 集成；许可证和 notice 由该 submodule 自身提供。 |
| NVIDIA Streamline | `External/Streamline/` | 实验性的 RR/FG 集成与 runtime interposer；需保留 `license.txt`、`nvngx_dlss.license.txt` 和 `3rd-party-licenses.md`。 |
| Unity PluginAPI | `External/UnityPluginAPI/` | Unity-facing D3D12 互操作实验所需头文件。 |
| CUDA Driver API | CUDA Toolkit 12.8 | 编译 Bloom PTX，提供 `cuda.h` 与 `cuda.lib`。 |

## 构建

### 获取 Git submodule

父仓库只保存第三方仓库的固定提交，不再把 Dear ImGui、DLSS、NRI、NRD 的源码或 SDK 文件复制进父仓库：

```powershell
git clone --recurse-submodules https://github.com/best-Hui/dx12-renderer.git
cd dx12-renderer
git submodule update --init --recursive
```

当前固定版本为：Dear ImGui `v1.91.9`、DLSS `v310.7.0`、NRI `v180`、NRD `4.17.4`。Dear ImGui、NRI 和 NRD 会由 CMake 从源码构建；Dear ImGui 直接使用 `External/ImGui` 中的官方源码，不会复制或改写到 build 目录。NRI 和 NRD 的官方 CMake 在首次配置时可能把 D3D12 Memory Allocator、MathLib、ShaderMake 等仅用于构建的依赖下载到 build 目录；这些文件不会提交到本仓库。Streamline 暂时仍使用单独提供的 SDK 包，因为它的官方源码仓库不包含本 sample 所需的完整 runtime DLL 集合。

以下命令在仓库根目录执行，并将构建产物放到同级 `build` 目录：

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8'

cmake -S . -B ..\build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DDX12_RENDERER_CUDA_TOOLKIT_ROOT="$cudaRoot"

cmake --build ..\build --config Release --target RaytracingDemo
```

运行：

```powershell
& ..\build\bin\Release\RaytracingDemo.exe
```

### Visual Studio 与 Rider 工程层级

CMake 生成的工程会保持各 target 的真实源码目录。`DX12Library`、`Framework`、`RenderGraph` 和 `RaytracingDemo` 的项目根节点下，先显示该 target 自己的 `CMakeLists.txt`，其余内容继续按照磁盘上的 `include/`、`shaders/`、`src/`、`tools/` 层级展开。Visual Studio 使用 `source_group(TREE ...)` 生成的 filter；Rider 则直接读取同一组未经虚拟重映射的物理路径。

本项目不会给自身源码添加 MSBuild `Link` 元数据。若普通源码使用虚拟 `Link`，而 CMake 自动注入的 regeneration `CMakeLists.txt` 仍使用物理路径，Rider 就会额外生成一个与 target 同名的目录。`build/` 只保存 `.sln`、`.vcxproj`、shader 产物、第三方构建树、库和可执行文件，不是源码目录；其中内容出现陈旧或混乱时，应清理后重新运行 CMake，不能直接修改生成工程来掩盖问题。

## 启动期 Shader 编译与变体

`RaytracingDemo` 现在通过 Framework 的 `ShaderVariantManager` 请求自身的 Raster、Compute、Task/Mesh 和 DXR Shader。这是**启动期编译**，不是运行中的 Shader 热更新：修改某个 `.hlsl`，或它引用的 `.hlsli`，重新启动 demo 后，相关变体会在创建 pipeline 前自动编译。

- `auto` 模式下，系统依次尝试 `DX12_RENDERER_SHADER_SOURCE_ROOT`、构建目录附近的 `CMakeCache.txt`、当前工作目录来定位源码根目录。
- 指纹会递归覆盖根源码、可解析的 `#include`、target profile、entry point、`Defines`、include 目录、编译参数和本机 `dxcompiler.dll` 标识。
- 默认缓存路径是 exe 同级的 `Saved/ShaderCache`。每个条目包含 `<variant>.<fingerprint>.cso`，以及便于排查依赖关系的 `.meta` 文件。
- 指纹没有变化时直接读取缓存；源码、include、宏定义或 profile 发生变化时，系统通过进程内 DXC 新编译一个条目。
- `off` 会关闭源码编译，只读取随程序带来的 `Shaders/*.cso`。如果源码可用但 DXC 编译失败，启动会带着 DXC 诊断信息失败，而不是悄悄继续使用旧字节码。

这里的“自动管理变体”是：**变体由代码显式声明，系统自动编译和缓存**；不是枚举所有理论上的 `#define` 组合。后者会带来不可控的排列组合爆炸。当前 `PathTracingPipelineController` 就从同一份 Shader 源码声明 Hard 与 `RAYTRACING_DEMO_SOFT_SHADOWS=1` 两个变体，运行时不再依赖软阴影 wrapper 文件。

目前启动期编译已经覆盖 `RaytracingDemo` 直接拥有的 Shader。Framework 中需要生成 C++ header 的 Shader 仍维持构建期编译。

## 目录导航

| 路径 | 建议关注的内容 |
| --- | --- |
| `DX12Library/` | command queue/list、资源、同步、descriptor heap、application 和 swap chain 等底层 D3D12 封装。 |
| `Framework/` | 场景、几何、材质、pipeline/binding、光线追踪、降噪、Meshlet 和 CUDA 互操作。 |
| `RenderGraph/` | pass/resource 描述、依赖、状态切换、queue 同步与 timing。 |
| `CMakeIncludes/` | 公共 target 配置、第三方依赖、Shader 构建规则和 IDE 工程组织。 |
| `Demos/Common/` | `RaytracingDemo` 使用的独立程序公共入口支持，不单独生成 target。 |
| `Demos/RaytracingDemo/` | 当前维护的主 sample；优先从这里理解本项目 API。 |
| `Assets/` | 场景、纹理和运行时资源。 |
| `External/` | 第三方源码与 SDK；其中部分目录是固定提交的 Git submodule。 |
| `Docs/` | 更详细的架构与 API 说明。 |

## 当前边界与已知限制

- 仅支持 Windows / x64 / D3D12。
- `RaytracingDemo` 是集成 sample，不是 production renderer，也不承诺稳定的公开 API 兼容性。
- CUDA 在运行时可以关闭，但仍是当前构建流程的硬依赖；将它做成真正可选的模块仍需调整 build system。
- Direct、Async Compute 和 Copy queue 都由 pass **显式指定**。RenderGraph 不会自动选择 queue、拆分 pass 或优化 overlap；Copy queue 已具备底层调度能力，但当前主 sample 还没有对应 pass。
- 连续的 async pass 目前逐 pass 提交，尚未合并成更大的 compute segment。
- `RenderGraphRoot::Execute()` 现在只是图执行入口；pass 录制/提交由 `RenderGraphCommandExecutor` 负责，Direct/Async Compute/Copy 的可选 timestamp 生命周期由 `RenderGraphProfiler` 负责。Root 仍负责图构建和拓扑编排。
- transient resource 会按本帧实际记录的 Direct/Async Compute/Copy fence 做延迟退休。aliasing 仍采取保守策略：不同 queue 使用的资源不会互相 alias，后续再设计更一般的多 queue allocator。
- 当前 Framework 和 RenderGraph 的执行路径由应用组合根显式注入 device、queue 和 descriptor 分配器。独立运行时的 application/window 生命周期，以及少量 legacy resource-wrapper 兼容路径，仍保留 `Application` 依赖。
- `RaytracingDemoSceneResources` 内部已拆成 texture/material、geometry、meshlet、RTAS 四个 builder；facade 仍是 sample 层入口。
- RenderGraph timing 分别记录每条 queue 上的 pass 时长；判断跨 queue overlap、wait 和 GPU bubble 时，请使用 PIX Timing Capture。
- Meshlet 路径是实验性 GBuffer 后端，不是完整的 visibility / streaming 系统，也不代表已达到最优 Meshlet 性能。
- `Stylized Comic` 是实验性的风格化 PBR/PBR-NPR 材质评估：它保留金属度、粗糙度和 GGX 材质输入，同时加入分段漫反射、冷色阴影和图形化高光；这不是完整的 Spider-Verse 复刻，线稿、网点、套印、hatching 与时间风格化仍不属于该材质模型。
- ReSTIR DI 是实验性的 inline ray-query 直接光 sample。它的光源采样、自发光表面发射体、时空复用和可见性测试选项仍在演进；图像质量、稳定性和性能均未作为等价 RTXDI 的实现完成验收。
- ReSTIR GI 是实验性的 inline ray-query 间接光 sample，参考 [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT) 中 ReSTIR GI 的数据流实现。当前目标是 one-bounce transport，使用持久化 packed reservoir；现阶段只有构建与自动化覆盖，画质、时域稳定性、显存占用和性能仍需要在目标硬件上验收。
- 软阴影当前使用固定 4 次采样的变体：平行光读取 angular radius，点光源读取 source radius；自适应采样和质量档位尚未实现。
- Shader 变体只在启动期或创建 pipeline 时编译；运行期源码热更新、后台编译和项目级 variant manifest 还没有实现。
- **DLSS、Ray Reconstruction 和 Frame Generation 均为实验性功能，尚未完成可交付验证。** sample 已接入 Native NGX SR/DLAA，并在评估 Streamline RR/FG。RR/FG 需要带 `--streamline-interposer` 重启程序；这是有意的启动期 opt-in，默认 D3D12 device、queue 和 swapchain 不会经过 Streamline proxy。当前只有 build、startup 与自动化安全覆盖，尚未在支持 RR/FG 的硬件上完成功能正确性、画质、稳定性、timing 和性能验证，仍可能存在未知问题。最终是否可用以运行时 capability query 为准：当前 RTX 2060 开发机不支持 FG，且当前 adapter 上 RR 报告不可用。不要把仓库中的任意 DLSS 模式视为保证可用或可直接交付的功能。

## 进一步阅读与许可证

- [架构总览](Docs/ArchitectureOverview.zh-CN.md)：按 DX12Library、Framework、RenderGraph、RaytracingDemo 分层说明职责、数据流和当前技术边界。
- [RaytracingDemo API 说明（英文）](Docs/RaytracingSampleApi.md)：介绍 Framework 用法、RenderGraph、profiling 和功能边界。
- 使用或再分发本仓库前，请保留上游与第三方组件的声明，并审阅对应的许可证文件。
- 本 README 不引入替代性的仓库级许可证。
- `External/DLSS/`、`External/NRI/`、`External/NRD/` 以及 `External/Streamline/` 中的 NVIDIA 组件均保留上游条款；Git submodule 链接不会转移或替换这些条款。SDK 放在 `External/` 不代表它变成开源，也不代表本仓库向任何人授予 NVIDIA SDK 的再许可；请保留全部 notice 与 license，不要把本仓库当作可独立分发的 SDK 镜像。在公开源码、再分发二进制或包含这些组件的商业发布前，应单独完成法务/许可证审查。
