# Framework Diagnostics, Automation, and Profiling

> Status: the baseline is implemented and validated with real Demo automation. The maintained Copy-queue, `meshlet-indirect`, and `dynamic-scene` scenarios cover real cross-queue synchronization, descriptor-sensitive Meshlet culling, retirement, and in-place acceleration-structure updates. Generic GPU readback/image assertions, DRED attachments, background writing, and retention policy remain future work.

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
timings.csv            correlated CPU scopes and per-queue GPU timestamps
assertions.json        structured pass/fail/unknown invariant results
reproduction.json      scenario, environment, and the applied control sequence
```

Large GPU resources are not dumped by default. The in-memory event buffer is bounded; ordinary events and passed assertions may be evicted under pressure while errors, fatal events, and failed/unknown assertions receive retention priority. The manifest records `dropped_event_count`. `RendererDiagnostics inspect` reports an incomplete verdict and exit code `12` for dropped events, a non-terminal capture, or unknown assertions, so missing evidence or unresolved invariants cannot be mistaken for a clean result. Tool-driven automation defaults to 262,144 events and accepts `--max-events`.

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
RendererDiagnostics run --exe RaytracingDemo.exe --scenario rtas --output Saved/Diagnostics/rtas-001
RendererDiagnostics run --exe RaytracingDemo.exe --scenario dynamic-scene --output Saved/Diagnostics/dynamic-scene-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
RendererDiagnostics run --exe RaytracingDemo.exe --scenario meshlet-indirect --output Saved/Diagnostics/meshlet-indirect-001 --set RAYTRACING_DEMO_AUTOTEST_STEP_MS=50 --set RAYTRACING_DEMO_MESHLET_GBUFFER=1 --set RAYTRACING_DEMO_MESHLET_BACKEND=indirect --set RAYTRACING_DEMO_DIRECT_LIGHTING=none --set RAYTRACING_DEMO_INDIRECT_LIGHTING=none --set RAYTRACING_DEMO_DENOISER=off
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
- compacted active-count/finalized indirect-dispatch consistency;
- Direct -> Copy -> Async Compute -> Direct validation with producer-fence, GPU-wait, state-plan, batch, and retirement assertions;
- reusable non-blocking `GpuReadbackBuffer` and `GpuReadbackTexture` ring-slot primitives, exercised by compacted active-pixel validation; OIDN uses D3D12 shared buffers/fences -> CUDA `Quality::Fast` -> D3D12 copy-back when available and retains readback -> CPU `Fast` -> upload as a fallback; OIDN automation records the selected backend and verifies static-result holding plus generation invalidation after camera motion;
- dynamic RTAS validation with ordinary/Meshlet vertex uploads, Meshlet bounds and instance updates, emissive-mesh refresh, dirty-BLAS refits, in-place TLAS updates, resource-retirement counters, restore-frame assertions, and an explicit skinned-update capability rejection;
- stable automation exit codes `20`-`24` for controls, observations, timeouts, and assertions.

Still required:

- complete validation of actual RenderGraph access against declared usage and state plans;
- generic coverage of cross-queue waits and multi-queue retirement beyond the maintained Copy validation topology;
- promote the existing texture/buffer readback primitives into Diagnostics-owned generic request APIs, image-tolerance assertions, and capture attachments;
- automatic DRED attachment on device removal;
- a generic Framework invariant for backend capability versus scheduled passes.

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

Separate queue timestamps must not be presented as a global GPU overlap truth unless they share a calibrated clock. The capture should link to a PIX workflow whenever full cross-queue timing is required.

## Delivery status

1. [x] `Framework/Diagnostics` session, typed event schema, bounded buffer, JSONL/JSON/CSV sinks, and manifest.
2. [x] RenderGraph schedule/state/lifetime, queue submission/fence, and descriptor/resource telemetry.
3. [x] Named controls/observations, deterministic scenarios, timeout/assertion handling, and automatic failure finalization.
4. [x] `run`, `inspect`, `query`, `diff`, `reproduce`, and `selftest` command loop.
5. [ ] Generic GPU readback/image assertions, DRED attachment, background writing, compression, and retention policy.

Disabled diagnostics must not allocate GPU resources, add readbacks, or change pass topology. Enabled capture must use bounded memory, avoid per-event disk flushing, and make every potentially stalling operation explicit.
