# Framework Diagnostics, Automation, and Profiling

> Status: the baseline is implemented and validated with real Demo automation. Generic GPU readback/image assertions, DRED attachments, background writing, and retention policy remain future work.

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
RendererDiagnostics inspect Saved/Diagnostics/run-001
RendererDiagnostics query Saved/Diagnostics/run-001 --frame 42 --category command_queue --limit 100
RendererDiagnostics diff baseline current
RendererDiagnostics reproduce Saved/Diagnostics/run-001 --execute
RendererDiagnostics selftest
```

`Framework/tools/` remains visible in the `Framework` project in both configurations; the option controls only whether the developer-tool targets are generated. The `core` scenario is a general rendering smoke test and does not assert compacted-only active-pixel readback. Run the `visual` scenario with `RAYTRACING_DEMO_RAY_TRACING_DISPATCH=compacted` when validating active-pixel count and indirect-dispatch arguments.

Commands emit JSON or JSONL. `inspect` reports a verdict, capture completeness, suspected problem domain, hypothesis, relevance-sorted evidence, and suggested next query. `query` filters frame/category/name/correlation/severity/field and reports truncation. `diff` compares graph passes, newly failed assertions, and CPU/GPU mean/P95 with sample counts. The runner remains non-interactive and never synthesizes mouse or keyboard input.

## Implemented invariants and remaining work

Implemented:

- recording-batch queue homogeneity;
- submitted command-list/native-queue type compatibility;
- Direct/Compute/Copy submission, signal, wait, fence completion, and CPU-wait telemetry with queue+fence correlation IDs;
- RenderGraph pass, batch, resource, state-plan, and lifetime telemetry;
- descriptor allocation, descriptor-set revision, and resource-identity telemetry;
- compacted active-count/finalized indirect-dispatch consistency;
- stable automation exit codes `20`-`24` for controls, observations, timeouts, and assertions.

Still required:

- closed-loop validation of cross-queue producer/consumer waits;
- complete validation of actual RenderGraph access against declared usage and state plans;
- multi-queue retirement validation for transient/replaced resources;
- generic texture/buffer readback, image tolerance, and capture attachments;
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
