# DX12 Renderer

> 一个面向 Windows 的实验性 DirectX 12 渲染器与 sample 集合，用于探索渲染器架构、RenderGraph、光线追踪、Meshlet、场景导入以及 CUDA/D3D12 互操作。

[English](README.md) · [RaytracingDemo API 指南](Docs/RaytracingSampleApi.md)

## Fork 来源与署名

本仓库 fork 自 [Delt06/dx12-renderer](https://github.com/Delt06/dx12-renderer)，共同历史起点为上游提交 `1db3a62d3eb7b8e0ff228090cca8dcf8e7e6adc9`。

本仓库以该上游渲染器为基础，增加了 Framework 与 sample 实验。它不是上游项目的官方续作，也不宣称与其他渲染器具有 API 或性能上的对等性。使用或再分发时请保留上游声明，并审阅第三方组件的许可证文件。

## 本 Fork 的扩展内容

| 方向 | 本仓库新增或扩展的内容 |
| --- | --- |
| Framework API | `CommandContext`、基于反射的 pipeline layout、按名称绑定的 descriptor set、bindless descriptor 提交、DXR helper 与 mesh shader pipeline 支持。 |
| RenderGraph | 逻辑资源、资源状态计划、external pass、分 queue GPU timestamp/CSV 导出，以及显式 Direct/Async Compute queue 同步。 |
| RaytracingDemo | 当前维护的集成 sample。它演示本项目 Framework API 的用法，而非把 raw D3D12 当作 sample 层常规接口。 |
| 光线追踪 | 可运行时切换的 inline ray query 与 shader-table DXR，共享同一场景/资源模型。 |
| 场景工作流 | 公共 `Scene` 和 `SceneImporter`，支持 Unity 文本场景与 JSON 场景描述。 |
| Meshlet | Meshlet 生成/GPU 资源，以及 task shader 与 compute-indirect GBuffer 后端。 |
| 降噪与互操作 | NRD/SVGF sample 路径，以及基于 D3D12 shared resource 和 external fence/semaphore 的 CUDA Bloom。 |
| 分析工具 | PIX scope、RenderGraph timing history/CSV 导出、`UnitySceneDump` 与运行时 UI。 |

## 仓库结构

| 路径 | 职责 |
| --- | --- |
| `DX12Library/` | 底层 D3D12 封装：command queue/list、资源、同步、descriptor heap、application 与 swap chain。 |
| `Framework/` | 场景、几何体、材质、pipeline/binding 抽象、光线追踪、降噪、Meshlet 与 CUDA 互操作。 |
| `RenderGraph/` | pass/resource 声明、依赖排序、资源状态计划、queue 同步和 timing 集成。 |
| `Demos/RaytracingDemo/` | 当前主要维护的 sample，也是理解本项目当前 API 用法的最佳入口。 |
| `Demos/*` | 其他历史或专项 sample；仍可参考，但不是当前主要集成目标。 |
| `Assets/` | demo 场景、纹理和运行时资源。 |
| `External/` | 随仓库集成的第三方包与头文件。 |
| `Docs/` | 架构和 sample API 说明。 |

## `RaytracingDemo` 演示的 API 与特性

`RaytracingDemo` 演示的是**本项目的 API 用法**。这些封装仍在演进中，不宣称是通用意义上的最优渲染器 API；sample 的目标是展示当前本仓库推荐的使用方式。

### Framework 层命令录制

```cpp
CommandContext commands(commandList);
commands.BindPipeline(computeShader);
commands.BindDescriptorSet(computeShader.GetDescriptorSet());
commands.Dispatch(groupCountX, groupCountY, 1);
```

同样的风格用于 raster、compute、mesh shader 与 DXR：`BindPipeline`、`BindDescriptorSet`、`Draw`、`Dispatch`、`DispatchMesh`、`DispatchRays`。root signature、raw descriptor table 和 native descriptor heap 应留在底层，除非 Framework 尚缺所需抽象。

### RenderGraph 与显式 async compute

```cpp
auto pass = RenderGraph::RenderPass::Create(
    L"Example Compute",
    { { inputId, RenderGraph::InputType::NonPixelShaderResource } },
    { { outputId, RenderGraph::OutputType::UnorderedAccess } },
    execute,
    RenderGraph::RenderPassQueue::AsyncCompute);
```

pass 输入/输出声明驱动排序、资源状态与跨 queue wait。当前支持显式 `Direct` / `AsyncCompute`，记录资源生产 queue 和已提交 fence value，并为有依赖的消费者插入 GPU-side wait。inline ray-query 下的 `Indirect Lighting` 是当前 async compute sample 路径。

### 场景导入与渲染特性

```cpp
const SceneImportResult result = SceneImporter::ImportFromFile(scenePath);
const Scene& scene = result.SceneData;
```

`SceneImporter` 接受 Unity 文本序列化 `.unity` 与 JSON 场景文件。`RaytracingDemoSceneResources` 将其转换为 texture、material、geometry、meshlet buffer 和 acceleration structure。

| 特性 | sample 演示内容 |
| --- | --- |
| Base resources | GBuffer 风格 normal/depth/material 数据、motion/world-position、history/display resource，以及 raster 或 meshlet GBuffer。 |
| 光线追踪 | inline ray-query compute shader 与 shader-table DXR。 |
| 光照 | 分离 direct / indirect lighting，随后 composite。 |
| 降噪 | 可选 NRD 或 SVGF。 |
| Meshlet | task shader 与 compute-indirect GBuffer，以及 cluster debug。 |
| CUDA Bloom | 采用 shared-resource/shared-fence 同步的 external D3D12/CUDA 后处理。 |
| Profiling | PIX scope 和 RenderGraph GPU timestamp history/CSV。 |

## 环境要求

| 要求 | 说明 |
| --- | --- |
| 平台 | Windows 10/11，x64；本仓库是 Windows/D3D12 项目。 |
| 工具链 | Visual Studio 2022、MSVC C++ desktop toolchain 与 Windows SDK。 |
| CMake | CMake 3.8 或更新版本；推荐较新版本。 |
| GPU/驱动 | D3D12 GPU 和较新驱动；DXR、mesh shader、CUDA 路径需要各自的硬件/驱动支持。 |
| CUDA | 当前 CMake graph 会构建 CUDA interop/Bloom，因此要求 **CUDA Toolkit 12.8**。 |

### vcpkg 依赖

```powershell
vcpkg install --triplet x64-windows assimp directxtex directxmesh imgui meshoptimizer
```

配置前设置 `VCPKG_ROOT`，或显式传入 `CMAKE_TOOLCHAIN_FILE`。

### 随仓库集成的依赖

| 组件 | 路径 / 获取方式 | 作用 |
| --- | --- | --- |
| DirectX Agility SDK 文件 | `D3D12/` | 项目环境使用的 D3D12 Agility SDK 文件。 |
| DirectX Shader Compiler | 优先 `DXC/dxc.exe`；否则 Windows SDK 的 `dxc.exe` | 编译 ray tracing、task、mesh、compute 等 shader。 |
| WinPixEventRuntime | `WinPixEventRuntime/` | PIX CPU/GPU event marker。 |
| NVIDIA NRD / NRI | `External/NRD/`、`External/NRI/` | 降噪集成及其 API 层/运行时二进制。 |
| Unity PluginAPI | `External/UnityPluginAPI/` | Unity-facing D3D12 interop 实验的头文件。 |
| CUDA Driver API | CUDA Toolkit 12.8 | 编译 Bloom PTX，提供 `cuda.h` / `cuda.lib`。 |

## 构建与运行

在仓库根目录执行。示例使用同级 build 目录：

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8'

cmake -S . -B ..\build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DDX12_RENDERER_CUDA_TOOLKIT_ROOT="$cudaRoot"
cmake --build ..\build --config Release --target RaytracingDemo UnitySceneDump

& ..\build\bin\Release\RaytracingDemo.exe
```

demo 默认加载 `Assets/Scenes/DefaultScene.json`。

| 变量 | 示例 | 作用 |
| --- | --- | --- |
| `RAYTRACING_DEMO_SCENE` | `Assets\Scenes\DefaultScene.json` | 加载 JSON 或 Unity 场景。 |
| `RAYTRACING_DEMO_UNITY_SCENE` | `C:\Scenes\CornellBox.unity` | Unity 文本场景兼容变量。 |
| `RAYTRACING_DEMO_MODE` | `shader-table` | 启动 shader-table DXR；默认是 inline ray query。 |
| `RAYTRACING_DEMO_DENOISER` | `nrd` 或 `svgf` | 选择降噪器。 |
| `RAYTRACING_DEMO_CUDA_BLOOM` | `1` | 开启 CUDA Bloom。 |
| `RAYTRACING_DEMO_MESHLET_GBUFFER` | `1` | 开启 meshlet GBuffer。 |
| `RAYTRACING_DEMO_MESHLET_BACKEND` | `indirect` | 选择 compute-indirect meshlet；否则为 task shader。 |

```powershell
& ..\build\bin\Release\UnitySceneDump.exe 'C:\Scenes\Example.unity'
```

## 当前局限性

- 仅支持 Windows/x64/D3D12。
- `RaytracingDemo` 是维护中的集成 sample，不是 production renderer，也不构成公开 API 兼容性承诺。
- 即使运行时关闭 CUDA Bloom，配置阶段仍要求 CUDA 12.8；将 CUDA 完全可选化仍是 build-system 工作。
- async compute **由 pass 显式指定**；RenderGraph 不会自动选 queue、拆分 pass、优化 overlap 或调度 Copy queue。
- 连续 async pass 当前按 pass 提交，尚未合并为更大的 compute segment。
- 分 queue RG timestamp 适合 queue 内 pass 时长；分析跨 queue wall-clock overlap、wait 和 GPU bubble 仍需 PIX Timing Capture。
- Unity importer 是静态且有限的：prefab/nested prefab、skinned mesh、`LODGroup` 和 asset-database cache 尚未完成。
- JSON 场景导入已具备，但默认 sample 仍会追加 C++ stress-test spheres 进行 renderer 负载测试。
- Meshlet 渲染是实验性 GBuffer 后端，不是完整 visibility/streaming 系统，也不宣称已达到最优 meshlet 性能。
- 尚未接入 DLSS/Streamline。

## 文档与声明

- [RaytracingDemo API 指南](Docs/RaytracingSampleApi.md) 说明 sample API、RenderGraph、场景导入、profiling 与边界。
- 维护中的 sample pass 应优先使用 Framework-facing API，而非在已有 API 覆盖的地方直接调用 raw D3D12。
- 本 fork 保留上游和第三方声明；使用或再分发前请审阅上游项目和 vendored dependency 的许可证文件。本 README 不引入替代性的仓库级许可证。
