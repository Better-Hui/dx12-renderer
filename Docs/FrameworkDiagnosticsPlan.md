# Framework Diagnostics, Automation, and Profiling

> Status: the baseline is implemented and validated with real Demo automation. RenderGraph pass scopes match actual SRV/UAV descriptor access to graph declarations, while DLSS/NGX and OIDN/CUDA native D3D12 boundaries explicitly report their accesses. Runtime invariants fail automation with exit code `24` when a cross-queue producer signal, consumer wait, state plan, or resource retirement is missing. Real `copy` and `oidn` scenarios cover Direct -> Copy -> Async Compute and Direct -> CUDA -> Direct respectively. Remaining work is strict attribution for RTAS, bindless, and global/default descriptors, plus whole-session background archival, compression, and retention policy.

## Goal

Build one machine-readable diagnostics path that a developer or coding agent can invoke without mouse/keyboard automation. A capture should explain what the renderer configured, recorded, submitted, waited for, executed, and read back, while keeping normal rendering overhead close to zero when disabled.

This complements PIX and RenderDoc rather than replacing them. PIX remains authoritative for the full GPU timeline and vendor-driver behavior; the Framework tool owns deterministic renderer state, assertions, automation, and portable artifacts.

## Ownership and layering

`Framework/Diagnostics` owns the capture session, artifact schema, automation runner, invariant evaluation, and lightweight profiler aggregation. `RaytracingDemo` only registers sample controls, observations, and scenarios.

Lower layers must not depend upward on Framework. `DX12Library` and `RenderGraph` should expose narrowly scoped typed snapshots or optional non-owning telemetry hooks for queue submission, fences, resource state, compiled batches, descriptors, and device removal. Framework adapters aggregate those records into a capture session.

```text
DX12Library telemetry/snapshots ----\
RenderGraph schedule/snapshots ------> Framework DiagnosticsSession -> artifacts
Framework feature providers --------/
RaytracingDemo scenarios/controls ---/
```

## Capture artifact contract

Every capture should create one self-contained directory under `Saved/Diagnostics`:

```text
manifest.json          schema, status, application, session, times, counts, metadata, and artifact table
summary.txt            concise human/agent-readable findings and failed invariants
events.jsonl           ordered CPU, command recording, submission, wait, signal, and error events
render_graph.json      passes, batches, queues, resource-state plans, and lifetime events
queue_submissions.json command-list type/order, waits, signals, and fence values
resources.json         resource identity, descriptions, state, and lifetime events
descriptors.json       allocations, descriptor-set revisions, and bound resource identity
performance_events.jsonl every unsampled CPU/GPU performance event for correlation by `RendererDiagnostics`
performance_frames.csv raw per-frame, per-scope CPU/GPU samples with thread, queue, scope hierarchy, and correlation
performance_summary.json per category/name/queue/scope-kind sample/frame counts and mean/min/P50/P95/max aggregates
timings.csv            compatibility alias for `performance_frames.csv`
assertions.json        structured pass/fail/unknown invariant results
reproduction.json      scenario, environment, and the applied control sequence
images/*.png           asynchronously captured image attachments listed by manifest
dred.txt               DRED breadcrumb/page-fault attachment on device removal
```

Large GPU resources are not dumped by default. The ordinary in-memory event buffer defaults to `65,536` entries. Normal high-frequency batch, descriptor, queue, and per-frame lifetime-success events are sampled as the first frame plus every 60 frames per semantic series; failed assertions, warnings, errors, and fatal events are never sampled. Performance events use a separate, bounded `262,144`-entry buffer: `profiler.*` bypasses ordinary sampling entirely, so `performance_frames.csv` retains every frame's raw sample. It is atomically exported only at Flush/finalization, never synchronously per frame. `RENDERER_DIAGNOSTICS_MAX_PERFORMANCE_EVENTS` overrides its capacity. The manifest records both `dropped_event_count` and `dropped_performance_event_count`; either makes `RendererDiagnostics inspect/diff` report incomplete with exit code `12`, so incomplete statistics cannot be treated as a clean performance result. `RENDERER_DIAGNOSTICS_SAMPLE_INTERVAL_FRAMES` affects only ordinary high-frequency telemetry, not performance samples.

## Automation contract

Framework provides a deterministic frame-step runner. Applications register named controls and observations; scenarios use those names instead of reaching into Demo members or injecting desktop input.

Implemented operations:

- set a typed runtime control;
- wait for a frame count or named observation/predicate with frame and wall-time timeouts;
- evaluate typed observations, numeric tolerance, and structured assertions;
- flush a capture from a scenario step;
- emit a reproduction package and return a process exit code.

The developer tool is generated only with `DX12_RENDERER_BUILD_DEVELOPER_TOOLS=ON`:

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

`Framework/tools/` remains visible in the `Framework` project in both configurations; the option controls only whether the developer-tool targets are generated. The `core` scenario is a general rendering smoke test and does not assert compacted-only active-pixel readback. The focused `rtas` scenario enables dynamic vertex/transform updates, verifies in-place BLAS/TLAS refits and retirement, then verifies the restore frame. `dynamic-scene` additionally covers task-shader and compute-indirect Meshlet GBuffer plus emissive refresh and explicit skinned-update rejection. `meshlet-indirect` is the descriptor-expansion regression case. Use the `50 ms` interval shown above to retain a complete bounded capture. Run the `visual` scenario with `RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted` when validating active-pixel count and indirect-dispatch arguments.

Commands emit JSON or JSONL. `inspect` reports a verdict, capture completeness, suspected problem domain, hypothesis, relevance-sorted evidence, and suggested next query. `query` filters frame/category/name/correlation/severity/field and reports truncation. `diff` compares graph passes, newly failed assertions, and CPU/GPU mean/P95 with sample counts. The runner remains non-interactive and never synthesizes mouse or keyboard input.

## Implemented invariants and remaining work

Implemented:

- recording-batch queue homogeneity;
- submitted command-list/native-queue type compatibility;
- Direct/Compute/Copy submission, signal, wait, fence completion, and CPU-wait telemetry with queue+fence correlation IDs;
- RenderGraph pass, batch, resource, state-plan, and lifetime telemetry;
- descriptor allocation, descriptor-set revision, and resource-identity telemetry;
- thread-local `DiagnosticRenderPassScope` matching logical graph resource IDs and declared read/write access to actual SRV/UAV descriptor access; undeclared, permission-mismatched, or identity-mismatched access emits `render_graph_shader_access_declaration=result=fail`;
- explicit native-D3D12 observations for DLSS/NGX and OIDN readback/upload, while ReSTIR and Bloom continue through descriptor-path validation;
- compacted active-count/finalized indirect-dispatch consistency;
- runtime validation of cross-queue producer signals, consumer waits, state plans, and graph-resource retirement; the real `copy` scenario validates Direct -> Copy -> Async Compute -> Direct;
- Direct -> CUDA -> Direct shared-fence handoff validation for OIDN, including Direct signal, CUDA wait/signal, and Direct wait;
- reusable non-blocking `GpuReadbackBuffer` and `GpuReadbackTexture` ring-slot primitives, exercised by compacted active-pixel validation. `DiagnosticsImageCapture::Request()` submits an independent Direct-queue copy; `Poll()` completes it in a later frame, converts to RGBA8, computes mean/non-black metrics, and records `image.<name>` assertions. Up to two background PNG writers encode attachments. Automation defers terminal finalization until shutdown `Drain()` has completed the final readback and writer. OIDN uses D3D12 shared buffers/fences -> CUDA `Quality::Fast` -> D3D12 copy-back when available and retains readback -> CPU `Fast` -> upload as a fallback; OIDN automation records the selected backend and verifies static-result holding plus generation invalidation after camera motion;
- `DiagnosticsSession::AttachDeviceRemovalDred()` writes `dred.txt` with the removal HRESULT, at most 128 auto-breadcrumb nodes, and page-fault allocations, then registers it in the manifest;
- dynamic RTAS validation with ordinary/Meshlet vertex uploads, Meshlet bounds and instance updates, emissive-mesh refresh, dirty-BLAS refits, in-place TLAS updates, resource-retirement counters, restore-frame assertions, and an explicit skinned-update capability rejection;
- stable automation exit codes `20`-`24` for controls, observations, timeouts, and assertions.

Still required:

- strict attribution for RTAS, bindless tables, and global/default descriptors; the current scope covers enumerable SRV/UAV and native D3D12 boundaries;
- a generic Framework invariant for backend capability versus scheduled passes.
- whole-session background archival, compression, and retention policy; only image PNG attachments are currently written in the background.

The original full invariant set remains:

- every recording batch contains one queue type;
- every submitted command list type is compatible with its native queue;
- cross-queue consumers have the required producer fence wait;
- RenderGraph accesses match declared resource usage and planned states;
- descriptor resources and required D3D12 flags are valid at bind time;
- transient and replaced resources are not retired before all queue fences complete;
- compacted active count and finalized indirect arguments agree;
- selected backend capabilities match the passes that were actually scheduled;
- required readbacks reach a terminal state before timeout;
- device removal captures DRED and the last bounded event window.

## Profiler model

The profiler currently correlates frame/pass/batch/submission/correlation identifiers:

- CPU update, graph compile, pass recording, submission, and present scopes;
- worker recording start/end and wait time;
- Direct/Compute/Copy GPU timestamps;
- queue waits, signals, idle gaps visible to the renderer, and fence completion;
- descriptor allocation/binding; unified upload/readback high-water marks remain pending.

### Debug CPU performance scopes

`DX12Library/PerformanceScope.h` provides `DX12_CPU_PERFORMANCE_SCOPE(...)`. It is a nested RAII CPU scope that writes a `profiler.cpu.scope` event at C++ scope exit with `frame`, `queue`, `scope_kind`, `scope_id`, `parent_scope_id`, `scope_depth`, `correlation_id`, and `cpu_duration_ms`. RenderGraph uses it around `RenderGraph.Execute`, Direct/Async Compute/Copy pass recording, and parallel Direct workers; the Demo also wraps `RaytracingDemo.RenderGraph.Execute`, separating the full CPU block from individual recording passes.

The macro creates a real object only under `_DEBUG` or an explicit developer profiling build configured with `-DDX12_RENDERER_ENABLE_PERFORMANCE_SCOPES=ON`. The latter permits runnable validation when the DLSS SDK cannot link the MSVC Debug CRT; it is not a shipping configuration. Default `Release`/`RelWithDebInfo` builds expand it to `static_cast<void>(0)`: arguments are not evaluated, no clock is read, no allocation occurs, and no telemetry is written. This is compile-time removal rather than a runtime switch. Debug/explicit profiling Demo sessions automatically enable Diagnostics and GPU timestamp capture. CPU scopes and GPU timestamps enter the dedicated performance stream: `performance_frames.csv` is raw per-frame/per-scope data, `performance_summary.json` aggregates mean/min/P50/P95/max per scope, and `performance_events.jsonl` retains raw JSONL that can be correlated with normal events by sequence/frame/correlation; `timings.csv` remains a compatibility alias. `RendererDiagnostics inspect <capture>` emits mean/P95-sorted `performance.hotspots`, while `diff` compares CPU/GPU mean/P95 across complete samples. Any performance-buffer loss explicitly marks the capture incomplete, so a partial summary is never a performance conclusion.

CPU scopes measure wall-clock time spent in CPU recording/scheduling code, not GPU execution time. GPU pass duration remains D3D12 timestamp-query data. PIX remains the tool for calibrated cross-queue timelines, wave/occupancy, or driver-level investigation.

Separate queue timestamps must not be presented as a global GPU overlap truth unless they share a calibrated clock. The capture should link to a PIX workflow whenever full cross-queue timing is required.

## Delivery status

1. [x] `Framework/Diagnostics` session, typed event schema, bounded buffer, JSONL/JSON/CSV sinks, and manifest.
2. [x] RenderGraph schedule/state/lifetime, queue submission/fence, and descriptor/resource telemetry.
3. [x] Named controls/observations, deterministic scenarios, timeout/assertion handling, and automatic failure finalization.
4. [x] `run`, `inspect`, `query`, `diff`, `reproduce`, and `selftest` command loop.
5. [x] Diagnostics-owned texture readback/image assertions, capture attachments, background PNG writing, and device-removal DRED attachments.
6. [x] Actual SRV/UAV/native-D3D12 access validation against graph declarations and runtime cross-queue signal/wait/state-plan/retirement invariants, verified in real ReSTIR, OIDN, DLSS, Bloom, and Copy paths.
7. [ ] Strict RTAS/bindless/global-descriptor access attribution and whole-session background archival, compression, and retention policy.

Disabled diagnostics must not allocate GPU resources, add readbacks, or change pass topology. Enabled capture must use bounded memory, avoid per-event disk flushing, and make every potentially stalling operation explicit.
