# DX12 Renderer

> 一个基于 DirectX 12 的实验性渲染器仓库。它用于验证和学习渲染器架构、RenderGraph、光线追踪、Meshlet、场景导入，以及 CUDA/D3D12 互操作。

[English README](README.md) · [RaytracingDemo API 说明（英文）](Docs/RaytracingSampleApi.md)

## 这是什么项目

本仓库 fork 自 [Delt06/dx12-renderer](https://github.com/Delt06/dx12-renderer)，共同历史从上游提交 `1db3a62d3eb7b8e0ff228090cca8dcf8e7e6adc9` 开始。

上游代码提供了项目的基础。本 fork 在此之上继续做 Framework、RenderGraph 和 `RaytracingDemo` 的实验性扩展，重点是把一组可运行、可调试的渲染功能放在同一个 sample 中。它不是上游项目的官方后续版本，也不试图宣称自己的抽象、接口或性能优于成熟渲染框架。

如果你刚接触本仓库，建议从 `Demos/RaytracingDemo/` 开始：它是当前维护的主 sample，也是了解**本项目 API 如何使用**的入口。

## 本 fork 在上游基础上增加了什么

- **Framework 层接口**：围绕 `CommandContext` 提供 pipeline、descriptor set、bindless descriptor、DXR 和 mesh shader 等常用封装，sample 不需要把 raw D3D12 调用铺满业务代码。
- **RenderGraph**：提供资源读写声明、依赖排序、资源状态切换、外部 pass、GPU timing 记录和 CSV 导出。
- **显式异步计算**：pass 可以指定使用 `Direct` 或 `AsyncCompute` queue；RenderGraph 负责跨 queue 的 GPU fence 等待与资源交接。
- **光线追踪与降噪**：同一场景可在 inline ray query 和 shader-table DXR 之间切换，并可使用 NRD 或 SVGF。
- **场景工作流**：新增公共 `Scene` 和 `SceneImporter`，可以读取 Unity 文本序列化场景和本仓库使用的 JSON 场景描述。
- **Meshlet 实验路径**：包含 Meshlet 构建、GPU 资源以及 task shader / compute-indirect 两种 GBuffer 后端。
- **CUDA Bloom**：演示 D3D12 shared resource、shared fence 与 CUDA external semaphore 的互操作流程。
- **调试与分析工具**：集成 PIX event、RenderGraph timing history/CSV、运行时调试 UI 和 `UnitySceneDump`。

这些内容的定位是“可运行的工程实验和 API 示例”。其中不少封装仍在演进，尤其是异步计算、Meshlet 和场景导入；请把它们视为当前仓库的实现方式，而不是通用最佳实践。

## `RaytracingDemo`：本项目 API 的使用示例

`RaytracingDemo` 的目的不是展示一堆零散的 D3D12 调用，而是演示本项目 Framework 和 RenderGraph 的使用方式。通常，一个 pass 应通过 Framework 提交命令，并通过 RenderGraph 声明自己读取和写入的资源。

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

## 你可以从 sample 中看到的功能

| 方向 | 具体内容 |
| --- | --- |
| GBuffer | 常规 raster GBuffer，以及实验性的 Meshlet GBuffer 路径。 |
| 光线追踪 | inline ray query compute shader 与 shader-table DXR，可在运行时切换。 |
| 光照 | direct lighting 和 indirect lighting 分离，再进行 composite。 |
| 降噪 | NRD 与 SVGF 两条可选路径。 |
| Meshlet | task shader 和 compute-indirect 后端，以及 cluster 调试显示。 |
| CUDA 互操作 | 基于 shared resource / shared fence 的 Bloom 后处理。 |
| 性能分析 | PIX scope，以及 Direct、Compute queue 分别记录的 RenderGraph GPU timing / CSV。 |
| 场景导入 | `.unity` 文本场景和 JSON 场景导入，最终转换为材质、几何、Meshlet buffer 和 acceleration structure。 |

默认场景是 `Assets/Scenes/DefaultScene.json`。如需查看 Unity 场景转换后的信息，可以使用 `UnitySceneDump`。

## 环境与依赖

### 基础环境

- Windows 10/11，x64。
- Visual Studio 2022，以及 MSVC C++ desktop toolchain 和 Windows SDK。
- CMake 3.8 或更新版本，建议使用较新的 CMake。
- 支持 D3D12 的 GPU 与较新的驱动。DXR、mesh shader 和 CUDA 路径还取决于各自的硬件和驱动能力。
- **CUDA Toolkit 12.8**。当前 CMake 配置阶段会构建 CUDA interop/Bloom，因此即使运行时关闭 Bloom，配置时仍需要 CUDA。

### vcpkg 包

先安装以下依赖：

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh imgui meshoptimizer
```

配置时设置 `VCPKG_ROOT`，或手动指定 `CMAKE_TOOLCHAIN_FILE`。

### 随仓库提供或集成的组件

| 组件 | 位置 / 获取方式 | 用途 |
| --- | --- | --- |
| DirectX Agility SDK | `D3D12/` | 项目使用的 D3D12 Agility SDK 文件。 |
| DirectX Shader Compiler | 优先 `DXC/dxc.exe`，否则使用 Windows SDK 的 `dxc.exe` | 编译 ray tracing、task、mesh、compute 等 shader。 |
| WinPixEventRuntime | `WinPixEventRuntime/` | 向 PIX 写入 CPU/GPU event marker。 |
| NVIDIA NRD / NRI | `External/NRD/`、`External/NRI/` | 降噪路径及其依赖。 |
| Unity PluginAPI | `External/UnityPluginAPI/` | Unity-facing D3D12 互操作实验所需头文件。 |
| CUDA Driver API | CUDA Toolkit 12.8 | 编译 Bloom PTX，提供 `cuda.h` 与 `cuda.lib`。 |

## 构建

以下命令在仓库根目录执行，并将构建产物放到同级 `build` 目录：

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8'

cmake -S . -B ..\build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DDX12_RENDERER_CUDA_TOOLKIT_ROOT="$cudaRoot"

cmake --build ..\build --config Release --target RaytracingDemo UnitySceneDump
```

运行：

```powershell
& ..\build\bin\Release\RaytracingDemo.exe
```

查看 Unity 场景的转换结果：

```powershell
& ..\build\bin\Release\UnitySceneDump.exe 'C:\Scenes\Example.unity'
```

## 常用运行时开关

| 环境变量 | 示例 | 作用 |
| --- | --- | --- |
| `RAYTRACING_DEMO_SCENE` | `Assets\Scenes\DefaultScene.json` | 指定要加载的 JSON 或 Unity 场景。 |
| `RAYTRACING_DEMO_UNITY_SCENE` | `C:\Scenes\CornellBox.unity` | 兼容旧调用方式的 Unity 场景变量。 |
| `RAYTRACING_DEMO_MODE` | `shader-table` | 使用 shader-table DXR；默认是 inline ray query。 |
| `RAYTRACING_DEMO_DENOISER` | `nrd` 或 `svgf` | 选择降噪器。 |
| `RAYTRACING_DEMO_CUDA_BLOOM` | `1` | 开启 CUDA Bloom。 |
| `RAYTRACING_DEMO_MESHLET_GBUFFER` | `1` | 开启 Meshlet GBuffer。 |
| `RAYTRACING_DEMO_MESHLET_BACKEND` | `indirect` | Meshlet GBuffer 使用 compute-indirect；不设置时使用 task shader。 |

## 目录导航

| 路径 | 建议关注的内容 |
| --- | --- |
| `DX12Library/` | command queue/list、资源、同步、descriptor heap、application 和 swap chain 等底层 D3D12 封装。 |
| `Framework/` | 场景、几何、材质、pipeline/binding、光线追踪、降噪、Meshlet 和 CUDA 互操作。 |
| `RenderGraph/` | pass/resource 描述、依赖、状态切换、queue 同步与 timing。 |
| `Demos/RaytracingDemo/` | 当前维护的主 sample；优先从这里理解本项目 API。 |
| `Assets/` | 场景、纹理和运行时资源。 |
| `Docs/` | 更详细的架构与 API 说明。 |

## 当前边界与已知限制

- 仅支持 Windows / x64 / D3D12。
- `RaytracingDemo` 是集成 sample，不是 production renderer，也不承诺稳定的公开 API 兼容性。
- CUDA 在运行时可以关闭，但仍是当前构建流程的硬依赖；将它做成真正可选的模块仍需调整 build system。
- Async Compute 目前采用**显式指定**方式。RenderGraph 不会自动选择 queue、拆分 pass、优化 overlap，也还没有 Copy queue 调度器。
- 连续的 async pass 目前逐 pass 提交，尚未合并成更大的 compute segment。
- RenderGraph timing 分别记录每条 queue 上的 pass 时长；判断跨 queue overlap、wait 和 GPU bubble 时，请使用 PIX Timing Capture。
- Unity importer 是静态、有限的导入器；prefab / nested prefab、skinned mesh、`LODGroup` 与 asset-database cache 尚未覆盖。
- JSON 场景已经可用，但默认 sample 仍会追加 C++ stress-test spheres，用于制造稳定的渲染压力。
- Meshlet 路径是实验性 GBuffer 后端，不是完整的 visibility / streaming 系统，也不代表已达到最优 Meshlet 性能。
- 尚未接入 DLSS / Streamline。

## 进一步阅读与许可证

- [RaytracingDemo API 说明（英文）](Docs/RaytracingSampleApi.md)：介绍 Framework 用法、RenderGraph、场景导入、profiling 和功能边界。
- 使用或再分发本仓库前，请保留上游与第三方组件的声明，并审阅对应的许可证文件。
- 本 README 不引入替代性的仓库级许可证。
