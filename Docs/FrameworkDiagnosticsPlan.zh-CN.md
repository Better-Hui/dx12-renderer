# Framework 诊断、自动化与 Profiler

> 状态：基础能力已落地并通过真实 Demo 自动化验证；受维护的 Copy queue、`meshlet-indirect` 与 `dynamic-scene` 场景已经覆盖真实跨 queue 同步、descriptor 敏感的 Meshlet cull、retirement 和原地 acceleration-structure update。GPU readback/image assertion、DRED attachment、后台写盘和 retention policy 仍待实现。

## 目标

建立一条机器可读的统一诊断路径，使开发者或编码 Agent 不需要注入鼠标键盘，就能回答渲染器配置了什么、录制了什么、向哪个 queue 提交了什么、等待了什么、GPU/CPU 花费如何，以及哪个约束失败。关闭时应接近零开销。

它用于补充 PIX 和 RenderDoc，而不是替代它们。完整 GPU 时间线与驱动行为仍以 PIX 为准；Framework 工具负责确定性的渲染器状态、断言、自动化和可移植诊断产物。

## 归属和分层

`Framework/Diagnostics` 负责 capture session、产物 schema、自动化 runner、约束检查和轻量 profiler 聚合。`RaytracingDemo` 只注册 sample control、observation 与 scenario。

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
manifest.json          schema、状态、应用、session、时间、计数、metadata 与产物表
summary.txt            供人和 Agent 快速阅读的结论、失败约束和下一步
events.jsonl           CPU、录制、提交、wait、signal 与错误的有序事件
render_graph.json      pass、batch、queue、resource state plan 与生命周期事件
queue_submissions.json command-list 类型/顺序、wait、signal 和 fence value
resources.json         resource identity、description、state 与生命周期事件
descriptors.json       allocation、descriptor-set revision 与绑定资源身份
timings.csv            关联后的 CPU scope 与各 queue GPU timestamp
assertions.json        结构化 pass/fail/unknown 结果
reproduction.json      scenario、环境与实际 control 变更序列
```

默认不导出大型 GPU resource。内存事件缓冲有明确上限，普通事件和已通过 assertion 在压力下可以被淘汰，但 error/fatal/failed/unknown assertion 会优先保留；manifest 记录 `dropped_event_count`。`RendererDiagnostics inspect` 对丢事件、非终态 capture 或 unknown assertion 返回 `incomplete` 和退出码 `12`，不会把证据缺失或未决约束误判为通过。命令行自动化默认上限为 262,144 条事件，可用 `--max-events` 调整。

## 自动化契约

Framework 已提供确定性的 frame-step runner。应用注册具名 control 和 observation；scenario 通过名字访问它们，不能直接访问 Demo 私有成员，也不能注入桌面输入。

当前支持：

- 设置 typed runtime control；
- 按帧数或具名 observation/predicate 等待，并带 frame/seconds 双重超时；
- 对 observation 做 typed 比较、数值容差和结构化 assertion；
- 在步骤中 flush capture；
- 输出可复现包并返回明确进程退出码。

Developer tool 仅在 `DX12_RENDERER_BUILD_DEVELOPER_TOOLS=ON` 时生成：

```text
RendererDiagnostics run --exe RaytracingDemo.exe --scenario stress --output Saved/Diagnostics/run-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario copy --output Saved/Diagnostics/copy-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario rtas --output Saved/Diagnostics/rtas-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario dynamic-scene --output Saved/Diagnostics/dynamic-scene-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
RendererDiagnostics run --exe RaytracingDemo.exe --scenario meshlet-indirect --output Saved/Diagnostics/meshlet-indirect-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_MESHLET_GBUFFER=1 --set RAYTRACING_DEMO_MESHLET_BACKEND=indirect --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
RendererDiagnostics inspect Saved/Diagnostics/run-001
RendererDiagnostics query Saved/Diagnostics/run-001 --frame 42 --category command_queue --limit 100
RendererDiagnostics diff baseline current
RendererDiagnostics reproduce Saved/Diagnostics/run-001 --execute
RendererDiagnostics selftest
```

`Framework/tools/` 在两种配置下都会继续显示在 `Framework` 工程中；开关只控制 developer-tool target 是否生成。`core` 是通用渲染 smoke test，不再执行只适用于 compacted dispatch 的 active-pixel readback assertion。专用 `rtas` 场景会启用动态顶点/变换更新，验证原地 BLAS/TLAS refit 与 retirement，再验证 restore frame。`dynamic-scene` 额外覆盖 task shader 与 compute-indirect Meshlet GBuffer、自发光刷新和显式拒绝 skinned update；`meshlet-indirect` 是 descriptor 扩容回归用例。为保留完整的有界 capture，应使用上例的 `50 ms` 间隔。验证 active-pixel 数量和间接派发参数时，应使用 `RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted` 运行 `visual` 场景。

所有命令输出 JSON 或 JSONL；`inspect` 会给出 verdict、capture 完整性、疑似问题域、假设、相关证据窗口和下一条查询建议。`query` 支持 frame/category/name/correlation/severity/field 过滤，并明确报告截断。`diff` 比较 graph pass、首次出现的失败 assertion 以及 CPU/GPU mean/P95，并携带样本数。runner 始终保持非交互，不合成鼠标或键盘输入。

## 已落地约束与待补项

- 已检查每个 recording batch 只包含一种 queue；
- 已检查 submitted command-list type 与 native queue 类型兼容；
- 已记录 Direct/Compute/Copy submission、signal、wait、fence completion 与 CPU wait，并使用 queue+fence 唯一 correlation ID；
- 已记录 RenderGraph pass/batch/resource/state-plan/lifecycle；
- 已记录 descriptor allocation、descriptor-set revision 与资源身份；
- 已检查 compacted active count 与 finalized indirect arguments/dispatch 的一致性；
- 已通过 Direct -> Copy -> Async Compute -> Direct 验证 producer fence、GPU wait、state plan、batch 和 retirement；
- 已具备可复用、非阻塞的 `GpuReadbackBuffer` 与 `GpuReadbackTexture` ring-slot 基元；compacted active-pixel 验证实际使用它们。OIDN 在可用时走 D3D12 shared buffer/fence → CUDA `Quality::Fast` → D3D12 copy-back，CUDA 初始化或 external-memory import 失败时才使用 HDR readback → CPU `Fast` → upload fallback；OIDN 自动化记录 backend，并验证静止结果保持以及相机移动后的 generation 作废；
- 已通过动态 RTAS 验证普通/Meshlet 顶点上传、Meshlet bounds 与 instance 更新、自发光 mesh refresh、dirty BLAS refit、原地 TLAS update、资源 retirement counter、restore frame，以及显式拒绝 skinned update 的 capability；
- 自动化 control、observation、timeout 和 assertion failure 使用稳定退出码 `20`–`24`。

仍待补齐：

- 验证 RenderGraph 实际访问完全符合声明 usage/state plan；
- 在受维护的 Copy 验证拓扑之外，继续扩大 cross-queue wait 与多 queue retirement 的通用覆盖；
- 将既有 texture/buffer readback 基元提升为 Diagnostics 自有的通用 request API、图像容差 assertion 与 capture attachment；
- device removal 时自动附加 DRED；
- backend capability 与实际调度 pass 的通用 Framework invariant。

最初规划的完整约束集合是：

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

当前通过 frame/pass/batch/submission/correlation ID 关联：

- CPU update、graph compile、pass recording、submission 和 present；
- worker 录制开始/结束与等待时间；
- Direct/Compute/Copy GPU timestamp；
- queue wait、signal、Framework 可见的 idle gap 与 fence completion；
- descriptor allocation/binding；upload/readback high-water mark 尚未统一接入。

如果多个 queue timestamp 没有校准到同一时钟，不能把它们伪装成全局 overlap 结论。需要完整跨 queue 时间线时，capture 应明确引导使用 PIX。

## 交付状态

1. [x] `Framework/Diagnostics` session、typed event schema、有界缓冲、JSONL/JSON/CSV sink 和 manifest。
2. [x] RenderGraph schedule/state/lifecycle、queue submission/fence 和 descriptor/resource telemetry。
3. [x] 具名 control/observation、确定性 scenario、timeout、assertion 与失败自动 finalize。
4. [x] `run`、`inspect`、`query`、`diff`、`reproduce`、`selftest` 命令行闭环。
5. [ ] 通用 GPU readback/image assertion、DRED attachment、后台写盘、压缩与保留策略。

关闭诊断时不得分配 GPU resource、插入 readback 或改变 pass topology。开启 capture 时必须限制内存，不得每条事件同步刷盘，并显式标记所有可能造成 GPU/CPU stall 的操作。
