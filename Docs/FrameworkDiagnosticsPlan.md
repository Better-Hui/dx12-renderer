# Framework Diagnostics, Automation, and Profiling Plan

> Status: planned. This document defines the intended Framework contract; it does not describe a completed feature.

## Goal

Build one machine-readable diagnostics path that a developer or coding agent can invoke without mouse/keyboard automation. A capture should explain what the renderer configured, recorded, submitted, waited for, executed, and read back, while keeping normal rendering overhead close to zero when disabled.

This complements PIX and RenderDoc rather than replacing them. PIX remains authoritative for the full GPU timeline and vendor-driver behavior; the Framework tool owns deterministic renderer state, assertions, automation, and portable artifacts.

## Ownership and layering

`Framework/Diagnostics` should own the public service, capture session, artifact schema, automation runner, invariant evaluation, and lightweight profiler aggregation. `RaytracingDemo` should only register feature controls, scenarios, and sample-specific providers.

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
manifest.json          adapter, driver, resolution, scene/config hashes, build/commit
summary.txt            concise human/agent-readable findings and failed invariants
events.jsonl           ordered CPU, command recording, submission, wait, signal, and error events
render_graph.json      passes, resources, culled topology, batches, and queue assignment
queue_submissions.json command-list type/order, waits, signals, and fence values
resources.json         descriptions, state plans, ownership, aliasing, and retirement fences
descriptors.json       layouts, bound resource identity, and validation failures
timings.csv            correlated CPU scopes and per-queue GPU timestamps
assertions.json        structured pass/fail/unknown invariant results
screenshots/           optional presentation/readback images and image metrics
```

Large GPU resources should not be dumped by default. The manifest records stable names, dimensions, formats, hashes, and explicit opt-in attachments so captures remain bounded and safe to share.

## Automation contract

Framework should provide a deterministic frame-step runner. Applications register named controls and observations; scenarios use those names instead of reaching into Demo members or injecting desktop input.

Required operations:

- set a typed runtime control;
- wait for a frame count, fence, readback, or stable predicate with a timeout;
- begin/end a diagnostic or timing capture;
- request a texture/buffer readback;
- evaluate an invariant or numeric/image tolerance;
- emit a reproduction package and return a process exit code.

A command-line tool or environment entry point should support shapes such as:

```text
RendererDiagnostics run --scenario compacted-restir-gi --frames 120 --output Saved/Diagnostics/run-001
RendererDiagnostics inspect Saved/Diagnostics/run-001
RendererDiagnostics diff baseline current
```

The runner must remain non-interactive and must not synthesize mouse or keyboard input.

## First invariants

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

The profiler should correlate, using frame/pass/batch/submission identifiers:

- CPU update, graph compile, pass recording, submission, and present scopes;
- worker recording start/end and wait time;
- Direct/Compute/Copy GPU timestamps;
- queue waits, signals, idle gaps visible to the renderer, and fence completion;
- descriptor/upload/readback allocation and high-water marks.

Separate queue timestamps must not be presented as a global GPU overlap truth unless they share a calibrated clock. The capture should link to a PIX workflow whenever full cross-queue timing is required.

## Delivery phases

1. Add `Framework/Diagnostics` session, event schema, bounded ring buffer, JSONL/JSON/CSV sinks, and manifest.
2. Add RenderGraph compiled-schedule/submission snapshots and the first queue/resource invariants.
3. Replace Demo-specific automation plumbing with registered controls, observations, and deterministic scenarios.
4. Add optional readback/image assertions, capture diff, and failure reproduction packages.
5. Add background artifact writing and retention limits after correctness is established.

Disabled diagnostics must not allocate GPU resources, add readbacks, or change pass topology. Enabled capture must use bounded memory, avoid per-event disk flushing, and make every potentially stalling operation explicit.
