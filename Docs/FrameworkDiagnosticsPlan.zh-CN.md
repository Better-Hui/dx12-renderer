# Framework 诊断、自动化与 Profiler 规划

> 状态：规划中。本文定义预期的 Framework 契约，不代表功能已经实现。

## 目标

建立一条机器可读的统一诊断路径，使开发者或编码 Agent 不需要注入鼠标键盘，就能回答渲染器配置了什么、录制了什么、向哪个 queue 提交了什么、等待了什么、GPU/CPU 花费如何，以及哪个约束失败。关闭时应接近零开销。

它用于补充 PIX 和 RenderDoc，而不是替代它们。完整 GPU 时间线与驱动行为仍以 PIX 为准；Framework 工具负责确定性的渲染器状态、断言、自动化和可移植诊断产物。

## 归属和分层

`Framework/Diagnostics` 负责公开服务、capture session、产物 schema、自动化 runner、约束检查和轻量 profiler 聚合。`RaytracingDemo` 只注册 sample 控制项、场景和 sample-specific provider。

底层不能反向依赖 Framework。`DX12Library` 与 `RenderGraph` 只暴露范围受控的 typed snapshot 或可选、非 owning telemetry hook，用于 queue submission、fence、资源状态、compiled batch、descriptor 和 device removal；Framework adapter 把这些记录聚合到 capture session。

```text
DX12Library telemetry/snapshot ----\
RenderGraph schedule/snapshot ------> Framework DiagnosticsSession -> 诊断产物
Framework feature provider --------/
RaytracingDemo scenario/control ----/
```

## Capture 产物契约

每次 capture 在 `Saved/Diagnostics` 下生成一个自包含目录：

```text
manifest.json          GPU/驱动、分辨率、场景/配置 hash、build/commit
summary.txt            供人和 Agent 快速阅读的结论、失败约束和下一步
events.jsonl           CPU、录制、提交、wait、signal 与错误的有序事件
render_graph.json      pass、资源、裁剪后拓扑、batch 和 queue 分类
queue_submissions.json command-list 类型/顺序、wait、signal 和 fence value
resources.json         description、state plan、ownership、aliasing、retirement fence
descriptors.json       layout、实际绑定资源身份和校验失败
timings.csv            关联后的 CPU scope 与各 queue GPU timestamp
assertions.json        结构化 pass/fail/unknown 结果
screenshots/           可选 presentation/readback 图片与图像指标
```

默认不导出大型 GPU resource。manifest 记录稳定名称、尺寸、格式、hash 和显式 opt-in attachment，使 capture 有界且便于分享。

## 自动化契约

Framework 提供确定性的 frame-step runner。应用注册具名 control 和 observation；scenario 通过名字访问它们，不能直接访问 Demo 私有成员，也不能注入桌面输入。

必须支持：

- 设置 typed runtime control；
- 按帧数、fence、readback 或稳定 predicate 等待，并带超时；
- 开始/结束诊断或 timing capture；
- 请求 texture/buffer readback；
- 检查 invariant、数值容差或图像容差；
- 输出可复现包并返回明确进程退出码。

命令行或环境入口建议支持：

```text
RendererDiagnostics run --scenario compacted-restir-gi --frames 120 --output Saved/Diagnostics/run-001
RendererDiagnostics inspect Saved/Diagnostics/run-001
RendererDiagnostics diff baseline current
```

runner 必须保持非交互，不得合成鼠标或键盘输入。

## 第一批约束检查

- 每个 recording batch 只能包含一种 queue；
- 每个 CommandList 类型必须与实际 native queue 兼容；
- 跨 queue consumer 必须等待对应 producer fence；
- RenderGraph 实际访问必须符合声明 usage 和 state plan；
- descriptor 绑定资源及所需 D3D12 flag 在 bind 时有效；
- transient/replaced resource 在所有 queue fence 完成前不能退休；
- compacted active count 必须与 finalized indirect arguments 一致；
- backend capability 必须与实际进入图的 pass 一致；
- 必需 readback 必须在超时前进入终态；
- device removal 必须记录 DRED 和最后一段有界事件窗口。

## Profiler 模型

通过 frame/pass/batch/submission ID 关联：

- CPU update、graph compile、pass recording、submission 和 present；
- worker 录制开始/结束与等待时间；
- Direct/Compute/Copy GPU timestamp；
- queue wait、signal、Framework 可见的 idle gap 与 fence completion；
- descriptor/upload/readback 分配和 high-water mark。

如果多个 queue timestamp 没有校准到同一时钟，不能把它们伪装成全局 overlap 结论。需要完整跨 queue 时间线时，capture 应明确引导使用 PIX。

## 交付阶段

1. 实现 `Framework/Diagnostics` session、事件 schema、有界 ring buffer、JSONL/JSON/CSV sink 和 manifest。
2. 接入 RenderGraph compiled schedule/submission snapshot，以及第一批 queue/resource invariant。
3. 用具名 control、observation 和确定性 scenario 替换 Demo-specific automation plumbing。
4. 增加可选 readback/image assertion、capture diff 和失败复现包。
5. 正确性稳定后再增加后台写盘和保留策略。

关闭诊断时不得分配 GPU resource、插入 readback 或改变 pass topology。开启 capture 时必须限制内存，不得每条事件同步刷盘，并显式标记所有可能造成 GPU/CPU stall 的操作。
