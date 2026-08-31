# Framework 诊断、自动化与 Profiler

> 状态：基础能力已落地并通过真实 Demo 自动化验证。RenderGraph pass scope 已把实际 SRV/UAV descriptor 访问与图声明对应；DLSS/NGX 和 OIDN/CUDA 的 native D3D12 边界也会显式上报访问。运行期会检查跨 queue producer signal、consumer wait、state plan 与资源 retirement，失败 assertion 会令 automation 以退出码 `24` 失败。真实 `copy` 与 `oidn` 场景已分别覆盖 Direct → Copy → Async Compute 和 Direct → CUDA → Direct。仍待完成的是 RTAS/bindless/global descriptor 的严格访问归因，以及完整 session 的后台归档、压缩与 retention policy。

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
images/*.png           异步采集的图像附件；manifest 列出相对路径
dred.txt               device removal 时的 DRED breadcrumb/page-fault 附件
```

默认不导出大型 GPU resource。内存事件缓冲默认上限为 `65,536`；正常高频的 CPU timing、batch、descriptor、queue 与逐帧 lifetime 成功事件按“每个语义序列首帧 + 每 60 帧”采样，失败 assertion、warning、error 和 fatal 永不采样。manifest 分别记录 `sampled_event_count` 与真正的 `dropped_event_count`，后者仍表示证据容量不足。`RENDERER_DIAGNOSTICS_SAMPLE_INTERVAL_FRAMES` 可覆盖采样间隔。`RendererDiagnostics inspect` 对丢事件、非终态 capture 或 unknown assertion 返回 `incomplete` 和退出码 `12`，不会把证据缺失或未决约束误判为通过。

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
RendererDiagnostics run --exe RaytracingDemo.exe --scenario oidn --output Saved/Diagnostics/oidn-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario rtas --output Saved/Diagnostics/rtas-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario dynamic-scene --output Saved/Diagnostics/dynamic-scene-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
RendererDiagnostics run --exe RaytracingDemo.exe --scenario meshlet-indirect --output Saved/Diagnostics/meshlet-indirect-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_MESHLET_GBUFFER=1 --set RAYTRACING_DEMO_MESHLET_BACKEND=indirect --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
RendererDiagnostics run --exe RaytracingDemo.exe --scenario visual --max-events 524288 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=700 --set RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted
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
- `DiagnosticRenderPassScope` 已将 pass 的逻辑资源声明与实际 SRV/UAV descriptor 访问匹配；未声明、权限不符或资源 identity 不符会产生 `render_graph_shader_access_declaration=result=fail`；
- DLSS/NGX、OIDN readback/upload 等不会经过 `CommandContext::SetDescriptorSet()` 的 native D3D12 边界已显式上报 read/write observation；ReSTIR、Bloom 继续由 descriptor 路径验证；
- 已检查 compacted active count 与 finalized indirect arguments/dispatch 的一致性；
- 已在每帧运行期检查跨 queue producer signal、consumer wait、state plan 和图资源 retirement；Direct → Copy → Async Compute → Direct 的真实 `copy` 场景已验证该链路；
- OIDN 的 D3D12 shared resource 在 Direct → CUDA → Direct handoff 中验证 Direct signal、CUDA wait/signal 与 Direct wait；
- 已具备可复用、非阻塞的 `GpuReadbackBuffer` 与 `GpuReadbackTexture` ring-slot 基元；compacted active-pixel 验证实际使用它们。`DiagnosticsImageCapture::Request()` 在 Direct queue 单独提交 copy，`Poll()` 在后续帧确认 fence、转换 RGBA8、计算均值/非黑像素比并记录 `image.<name>` assertion；PNG 最多两个后台 writer 并行写入。自动化 terminal finalize 延后至 shutdown 的 `Drain()`，确保最后一个异步图像结论和附件先写入 capture。OIDN 在可用时走 D3D12 shared buffer/fence → CUDA `Quality::Fast` → D3D12 copy-back，CUDA 初始化或 external-memory import 失败时才使用 HDR readback → CPU `Fast` → upload fallback；OIDN 自动化记录 backend，并验证静止结果保持以及相机移动后的 generation 作废；
- device removal 失败路径通过 `DiagnosticsSession::AttachDeviceRemovalDred()` 写入 `dred.txt`，包含 removal HRESULT、最多 128 个 auto-breadcrumb 和 page-fault allocation；该附件由 manifest 声明；
- 已通过动态 RTAS 验证普通/Meshlet 顶点上传、Meshlet bounds 与 instance 更新、自发光 mesh refresh、dirty BLAS refit、原地 TLAS update、资源 retirement counter、restore frame，以及显式拒绝 skinned update 的 capability；
- 自动化 control、observation、timeout 和 assertion failure 使用稳定退出码 `20`–`24`。

仍待补齐：

- RTAS、bindless table 和 global/default descriptor 的严格实际访问归因；现有 scope 已覆盖可枚举 SRV/UAV 与 native D3D12 边界；
- backend capability 与实际调度 pass 的通用 Framework invariant；
- 完整 capture session 的后台归档、压缩与 retention policy；当前仅图像 PNG 附件后台写入。

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
5. [x] Diagnostics 自有的通用 texture readback/image assertion、capture attachment、PNG 后台写入与 device-removal DRED attachment。
6. [x] 实际 SRV/UAV/native D3D12 资源访问对图声明的校验，以及跨 queue signal/wait/state-plan/retirement 的运行期 invariant；真实 ReSTIR、OIDN、DLSS、Bloom 与 Copy 场景均已验收。
7. [ ] RTAS/bindless/global descriptor 的严格访问归因，以及完整 session 的后台归档、压缩与 retention policy。

关闭诊断时不得分配 GPU resource、插入 readback 或改变 pass topology。开启 capture 时必须限制内存，不得每条事件同步刷盘，并显式标记所有可能造成 GPU/CPU stall 的操作。
