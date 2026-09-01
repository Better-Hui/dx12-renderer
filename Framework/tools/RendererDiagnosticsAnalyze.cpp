//Modify Begin:2026-09-01 by Hui
#include "RendererDiagnosticsCommands.h"

#include "RendererDiagnosticsCapture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace RendererDiagnosticsTool;

    std::string Recommendation(const Event& event)
    {
        if (event.Category.starts_with("descriptor"))
        {
            return "Query descriptors.json around this sequence and verify resource identity, D3D12 flags, and table revision.";
        }
        if (event.Category.starts_with("command_queue"))
        {
            return "Query queue_submissions.json for the correlated fence and verify command-list type and producer/consumer waits.";
        }
        if (event.Category.starts_with("render_graph"))
        {
            return "Inspect render_graph.json for the correlated pass or batch and compare declared resources and queue assignment.";
        }
        if (event.Category.starts_with("device"))
        {
            return "Inspect DRED and the final queue/resource window; use PIX or RenderDoc for driver-side confirmation.";
        }
        if (event.Category.starts_with("automation") || event.Name.starts_with("automation"))
        {
            return "Run RendererDiagnostics reproduce, then diff the new capture against this capture.";
        }
        return "Query the same frame and correlation_id, then compare with a known-good baseline.";
    }

    std::string SuspectedDomain(const Event& event)
    {
        const std::string message = GetField(event, "message");
        if (message.find("RAYTRACING_DEMO_AUTOTEST") != std::string::npos ||
            message.find("automation") != std::string::npos)
        {
            return "automation_scenario";
        }
        if (event.Category.starts_with("descriptor")) return "descriptor_binding";
        if (event.Category.starts_with("command_queue")) return "queue_synchronization";
        if (event.Category.starts_with("render_graph")) return "render_graph_contract";
        if (event.Category.starts_with("device")) return "device_or_driver";
        if (event.Category.starts_with("automation") || event.Name.starts_with("automation"))
        {
            return "automation_scenario";
        }
        if (event.Name.find("active_pixel") != std::string::npos ||
            event.Name.find("dispatch") != std::string::npos)
        {
            return "compacted_dispatch";
        }
        return "renderer_invariant";
    }

    std::string Hypothesis(const Event& event)
    {
        const std::string message = GetField(event, "message");
        if (message.find("RAYTRACING_DEMO_AUTOTEST") != std::string::npos ||
            message.find("automation") != std::string::npos)
        {
            return "The requested automation scenario or one of its registered controls/observations is invalid.";
        }
        if (event.Category.starts_with("descriptor"))
        {
            return "A descriptor layout, table revision, resource identity, or view contract may not match the bound shader data.";
        }
        if (event.Category.starts_with("command_queue"))
        {
            return "A command-list type, submission order, fence signal, or cross-queue wait may violate the queue contract.";
        }
        if (event.Category.starts_with("render_graph"))
        {
            return "The compiled pass, resource-state plan, lifetime, or queue assignment may disagree with execution.";
        }
        if (event.Category.starts_with("device"))
        {
            return "The device was removed or the driver rejected submitted work; renderer-side evidence may require DRED or a GPU capture.";
        }
        if (event.Category.starts_with("automation") || event.Name.starts_with("automation"))
        {
            return "A registered control, observation, assertion, or bounded wait did not complete as the scenario specified.";
        }
        if (event.Name.find("active_pixel") != std::string::npos ||
            event.Name.find("dispatch") != std::string::npos)
        {
            return "The compacted active-pixel count and finalized indirect dispatch arguments may be inconsistent.";
        }
        return "A renderer invariant failed; use the correlated evidence window before assigning a concrete root cause.";
    }

    struct TimingStats
    {
        std::vector<double> Samples;

        [[nodiscard]] double Mean() const
        {
            if (Samples.empty()) return 0.0;
            double total = 0.0;
            for (const double sample : Samples) total += sample;
            return total / static_cast<double>(Samples.size());
        }

        [[nodiscard]] double Percentile(const double percentile) const
        {
            if (Samples.empty()) return 0.0;
            std::vector<double> sorted = Samples;
            std::ranges::sort(sorted);
            const size_t index = static_cast<size_t>(std::ceil(percentile * static_cast<double>(sorted.size()))) - 1u;
            return sorted[(std::min)(index, sorted.size() - 1u)];
        }
    };

    std::set<std::string> CollectPasses(const Capture& capture)
    {
        std::set<std::string> passes;
        for (const Event& event : capture.Events)
        {
            if (event.Category == "render_graph.pass") passes.insert(event.Name + "|" + GetField(event, "queue"));
        }
        return passes;
    }

    std::set<std::string> CollectFailedAssertions(const Capture& capture)
    {
        std::set<std::string> failures;
        for (const Event& event : capture.Events)
        {
            if (event.Category == "assertion" && GetField(event, "result") == "fail") failures.insert(event.Name);
        }
        return failures;
    }

    std::map<std::string, TimingStats> CollectTimings(const Capture& capture)
    {
        std::map<std::string, TimingStats> timings;
        for (const Event& event : capture.Events)
        {
            if (!event.Category.starts_with("profiler.")) continue;
            std::string value = GetField(event, "cpu_duration_ms");
            if (value.empty()) value = GetField(event, "gpu_delta_ms");
            const std::optional<double> milliseconds = ToDouble(value);
            if (!milliseconds.has_value()) continue;
            timings[event.Category + "|" + event.Name + "|" + GetField(event, "queue")]
                .Samples.push_back(*milliseconds);
        }
        return timings;
    }

    void WriteTimingHotspots(const Capture& capture)
    {
        struct TimingHotspot
        {
            std::string Scope;
            double Mean = 0.0;
            double P95 = 0.0;
            size_t SampleCount = 0;
        };

        std::vector<TimingHotspot> hotspots;
        for (const auto& [scope, stats] : CollectTimings(capture))
        {
            if (stats.Samples.empty())
            {
                continue;
            }
            hotspots.push_back({ scope, stats.Mean(), stats.Percentile(0.95), stats.Samples.size() });
        }
        std::ranges::sort(hotspots, [](const TimingHotspot& left, const TimingHotspot& right)
        {
            return std::tie(left.P95, left.Mean, left.SampleCount, left.Scope) >
                std::tie(right.P95, right.Mean, right.SampleCount, right.Scope);
        });

        constexpr size_t maximumHotspotCount = 16u;
        const size_t hotspotCount = (std::min)(hotspots.size(), maximumHotspotCount);
        std::cout << "{\"scope_count\":" << hotspots.size() << ",\"hotspots\":[";
        for (size_t index = 0; index < hotspotCount; ++index)
        {
            if (index != 0)
            {
                std::cout << ',';
            }
            const TimingHotspot& hotspot = hotspots[index];
            std::cout << "{\"scope\":";
            WriteJsonString(std::cout, hotspot.Scope);
            std::cout << ",\"mean_ms\":" << hotspot.Mean
                      << ",\"p95_ms\":" << hotspot.P95
                      << ",\"sample_count\":" << hotspot.SampleCount << '}';
        }
        std::cout << "]}";
    }

    template<typename Collection>
    std::vector<typename Collection::value_type> Difference(const Collection& lhs, const Collection& rhs)
    {
        std::vector<typename Collection::value_type> difference;
        std::ranges::set_difference(lhs, rhs, std::back_inserter(difference));
        return difference;
    }

    void WriteStringArray(const std::vector<std::string>& values)
    {
        std::cout << '[';
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (index != 0) std::cout << ',';
            WriteJsonString(std::cout, values[index]);
        }
        std::cout << ']';
    }
}

int RendererDiagnosticsTool::InspectCommand(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) throw std::runtime_error("inspect requires a capture directory.");
    const Capture capture = LoadCapture(std::filesystem::u8path(arguments[0]));
    const uint64_t window = GetUnsignedOption(arguments, "--window", 3u);
    const auto& manifest = capture.Manifest.AsObject();
    std::vector<const Event*> failures;
    for (const Event& event : capture.Events) if (IsFailure(event)) failures.push_back(&event);

    const JsonValue* statusValue = Find(manifest, "status");
    const std::string status = statusValue != nullptr ? ScalarToString(*statusValue) : "unknown";
    const JsonValue* messageValue = Find(manifest, "message");
    const std::string sessionMessage = messageValue != nullptr &&
        !std::holds_alternative<std::nullptr_t>(messageValue->Data)
        ? ScalarToString(*messageValue)
        : std::string();
    const uint64_t droppedEventCount = ToUint64(Find(manifest, "dropped_event_count"));
    const uint64_t droppedPerformanceEventCount = ToUint64(Find(manifest, "dropped_performance_event_count"));
    const uint64_t unknownAssertionCount = ToUint64(Find(manifest, "unknown_assertion_count"));
    const bool terminalStatus = status == "passed" || status == "failed" || status == "aborted";
    const bool captureComplete = droppedEventCount == 0u && droppedPerformanceEventCount == 0u &&
        terminalStatus && unknownAssertionCount == 0u;
    const bool failed = !failures.empty() || status == "failed" || status == "aborted";
    const std::string verdict = failed ? "failed" : (captureComplete ? "passed" : "incomplete");

    std::cout << "{\"schema_version\":1,\"capture\":";
    WriteJsonString(std::cout, capture.Directory.string());
    std::cout << ",\"verdict\":";
    WriteJsonString(std::cout, verdict);
    std::cout << ",\"status\":";
    WriteJsonString(std::cout, status);
    std::cout << ",\"message\":";
    if (sessionMessage.empty()) std::cout << "null"; else WriteJsonString(std::cout, sessionMessage);
    std::cout << ",\"event_count\":" << capture.Events.size()
              << ",\"dropped_event_count\":" << droppedEventCount
              << ",\"dropped_performance_event_count\":" << droppedPerformanceEventCount
              << ",\"unknown_assertion_count\":" << unknownAssertionCount
              << ",\"capture_health\":{\"complete\":" << (captureComplete ? "true" : "false")
              << ",\"terminal_status\":" << (terminalStatus ? "true" : "false")
              << ",\"analysis_confidence\":\"" << (captureComplete ? "high" : "partial") << "\",\"issues\":[";
    bool hasCaptureIssue = false;
    if (droppedEventCount != 0u)
    {
        WriteJsonString(std::cout, "The bounded event buffer dropped " + std::to_string(droppedEventCount) +
            " earlier events; absence of evidence is not a clean result.");
        hasCaptureIssue = true;
    }
    if (droppedPerformanceEventCount != 0u)
    {
        if (hasCaptureIssue) std::cout << ',';
        WriteJsonString(std::cout, "The bounded performance buffer dropped " +
            std::to_string(droppedPerformanceEventCount) +
            " samples; performance statistics are incomplete.");
        hasCaptureIssue = true;
    }
    if (!terminalStatus)
    {
        if (hasCaptureIssue) std::cout << ',';
        WriteJsonString(std::cout, "The manifest has no terminal status; the capture may be interrupted or still being written.");
        hasCaptureIssue = true;
    }
    if (unknownAssertionCount != 0u)
    {
        if (hasCaptureIssue) std::cout << ',';
        WriteJsonString(std::cout, "One or more invariants are unknown; the capture cannot establish a clean result.");
    }
    std::cout << "]}"
              << ",\"performance\":";
    WriteTimingHotspots(capture);
    std::cout << ",\"finding_count\":" << failures.size() << ",\"findings\":[";
    for (size_t failureIndex = 0; failureIndex < failures.size(); ++failureIndex)
    {
        if (failureIndex != 0) std::cout << ',';
        const Event& failure = *failures[failureIndex];
        std::cout << "{\"sequence\":" << failure.Sequence << ",\"frame\":";
        if (failure.Frame.has_value()) std::cout << *failure.Frame; else std::cout << "null";
        std::cout << ",\"correlation_id\":" << failure.CorrelationId << ",\"category\":";
        WriteJsonString(std::cout, failure.Category);
        std::cout << ",\"name\":"; WriteJsonString(std::cout, failure.Name);
        std::cout << ",\"message\":"; WriteJsonString(std::cout, GetField(failure, "message"));
        std::cout << ",\"suspected_domain\":"; WriteJsonString(std::cout, SuspectedDomain(failure));
        std::cout << ",\"hypothesis\":"; WriteJsonString(std::cout, Hypothesis(failure));
        std::cout << ",\"recommendation\":"; WriteJsonString(std::cout, Recommendation(failure));
        std::cout << ",\"evidence\":[";
        struct EvidenceCandidate
        {
            const Event* Value = nullptr;
            uint64_t Distance = 0;
            bool SameCorrelation = false;
            bool SameFrame = false;
        };
        std::vector<EvidenceCandidate> evidence;
        for (const Event& candidate : capture.Events)
        {
            const uint64_t distance = candidate.Sequence > failure.Sequence
                ? candidate.Sequence - failure.Sequence
                : failure.Sequence - candidate.Sequence;
            const bool sameCorrelation = failure.CorrelationId != 0u &&
                candidate.CorrelationId == failure.CorrelationId;
            const bool sameFrame = failure.Frame.has_value() && candidate.Frame == failure.Frame;
            if (distance <= window || sameCorrelation || sameFrame)
            {
                evidence.push_back({ &candidate, distance, sameCorrelation, sameFrame });
            }
        }
        std::ranges::sort(evidence, [&failure](const EvidenceCandidate& lhs, const EvidenceCandidate& rhs)
        {
            const auto key = [&failure](const EvidenceCandidate& candidate)
            {
                return std::tuple(
                    candidate.Value == &failure ? 0 : 1,
                    candidate.SameCorrelation ? 0 : 1,
                    candidate.Distance,
                    candidate.SameFrame ? 0 : 1,
                    candidate.Value->Sequence);
            };
            return key(lhs) < key(rhs);
        });
        constexpr size_t MaximumEvidenceCount = 32u;
        const size_t evidenceCount = (std::min)(evidence.size(), MaximumEvidenceCount);
        for (size_t evidenceIndex = 0; evidenceIndex < evidenceCount; ++evidenceIndex)
        {
            if (evidenceIndex != 0) std::cout << ',';
            std::cout << evidence[evidenceIndex].Value->RawJson;
        }
        std::cout << "],\"evidence_count\":" << evidenceCount
                  << ",\"evidence_truncated\":" << (evidence.size() > evidenceCount ? "true" : "false") << '}';
    }
    std::cout << "],\"next_actions\":[";
    WriteJsonString(std::cout, "RendererDiagnostics query \"" + capture.Directory.string() + "\" --severity error");
    std::cout << ',';
    WriteJsonString(std::cout, "RendererDiagnostics diff <baseline> \"" + capture.Directory.string() + "\"");
    if (!capture.ReproductionEnvironment.empty())
    {
        std::cout << ',';
        WriteJsonString(std::cout, "RendererDiagnostics reproduce \"" + capture.Directory.string() + "\" --execute");
    }
    std::cout << "]}\n";
    if (failed) return 10;
    return captureComplete ? 0 : 12;
}

int RendererDiagnosticsTool::QueryCommand(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) throw std::runtime_error("query requires a capture directory.");
    const Capture capture = LoadCapture(std::filesystem::u8path(arguments[0]));
    const auto frameFilter = GetOption(arguments, "--frame");
    const auto categoryFilter = GetOption(arguments, "--category");
    const auto nameFilter = GetOption(arguments, "--name");
    const auto correlationFilter = GetOption(arguments, "--correlation");
    const auto severityFilter = GetOption(arguments, "--severity");
    const auto fieldFilter = GetOption(arguments, "--field");
    const size_t limit = static_cast<size_t>(GetUnsignedOption(arguments, "--limit", 1000u));
    std::vector<const Event*> matches;
    size_t totalMatchCount = 0;
    for (const Event& event : capture.Events)
    {
        if (frameFilter.has_value() && (!event.Frame.has_value() || std::to_string(*event.Frame) != *frameFilter)) continue;
        if (categoryFilter.has_value() && !event.Category.starts_with(*categoryFilter)) continue;
        if (nameFilter.has_value() && event.Name.find(*nameFilter) == std::string::npos) continue;
        if (correlationFilter.has_value() && std::to_string(event.CorrelationId) != *correlationFilter) continue;
        if (severityFilter.has_value() && event.Severity != *severityFilter) continue;
        if (fieldFilter.has_value())
        {
            const size_t separator = fieldFilter->find('=');
            if (separator == std::string::npos) throw std::runtime_error("--field requires name=value.");
            const auto field = event.Fields.find(fieldFilter->substr(0, separator));
            if (field == event.Fields.end() || field->second != fieldFilter->substr(separator + 1u)) continue;
        }
        ++totalMatchCount;
        if (matches.size() < limit) matches.push_back(&event);
    }
    const bool jsonLines = HasFlag(arguments, "--jsonl");
    if (!jsonLines)
    {
        std::cout << "{\"schema_version\":1,\"match_count\":" << totalMatchCount
                  << ",\"returned_count\":" << matches.size()
                  << ",\"truncated\":" << (matches.size() < totalMatchCount ? "true" : "false")
                  << ",\"events\":[";
    }
    for (size_t index = 0; index < matches.size(); ++index)
    {
        if (!jsonLines && index != 0) std::cout << ',';
        std::cout << matches[index]->RawJson;
        if (jsonLines) std::cout << '\n';
    }
    if (!jsonLines) std::cout << "]}\n";
    return 0;
}

int RendererDiagnosticsTool::DiffCommand(const std::vector<std::string>& arguments)
{
    if (arguments.size() < 2u) throw std::runtime_error("diff requires baseline and current capture directories.");
    const Capture baseline = LoadCapture(std::filesystem::u8path(arguments[0]));
    const Capture current = LoadCapture(std::filesystem::u8path(arguments[1]));
    const double threshold = ToDouble(GetOption(arguments, "--regression-percent").value_or("10")).value_or(10.0);
    const double minimumMs = ToDouble(GetOption(arguments, "--min-ms").value_or("0.1")).value_or(0.1);
    const auto addedPasses = Difference(CollectPasses(current), CollectPasses(baseline));
    const auto removedPasses = Difference(CollectPasses(baseline), CollectPasses(current));
    const auto newAssertions = Difference(CollectFailedAssertions(current), CollectFailedAssertions(baseline));
    const auto baselineTimings = CollectTimings(baseline);
    const auto currentTimings = CollectTimings(current);

    struct Regression
    {
        std::string Key;
        double Baseline = 0.0;
        double Current = 0.0;
        double Percent = 0.0;
        double BaselineP95 = 0.0;
        double CurrentP95 = 0.0;
        size_t BaselineSamples = 0;
        size_t CurrentSamples = 0;
    };
    std::vector<Regression> regressions;
    for (const auto& [key, currentStats] : currentTimings)
    {
        const auto previous = baselineTimings.find(key);
        if (previous == baselineTimings.end() || previous->second.Samples.empty() || currentStats.Samples.empty()) continue;
        const double baselineMean = previous->second.Mean();
        const double currentMean = currentStats.Mean();
        if (baselineMean < minimumMs) continue;
        const double percent = (currentMean - baselineMean) * 100.0 / baselineMean;
        if (percent >= threshold)
        {
            regressions.push_back({ key, baselineMean, currentMean, percent,
                previous->second.Percentile(0.95), currentStats.Percentile(0.95),
                previous->second.Samples.size(), currentStats.Samples.size() });
        }
    }
    std::ranges::sort(regressions, [](const Regression& lhs, const Regression& rhs) { return lhs.Percent > rhs.Percent; });

    const uint64_t baselineDropped = ToUint64(Find(baseline.Manifest.AsObject(), "dropped_event_count"));
    const uint64_t currentDropped = ToUint64(Find(current.Manifest.AsObject(), "dropped_event_count"));
    const uint64_t baselinePerformanceDropped = ToUint64(
        Find(baseline.Manifest.AsObject(), "dropped_performance_event_count"));
    const uint64_t currentPerformanceDropped = ToUint64(
        Find(current.Manifest.AsObject(), "dropped_performance_event_count"));
    const bool comparisonComplete = baselineDropped == 0u && currentDropped == 0u &&
        baselinePerformanceDropped == 0u && currentPerformanceDropped == 0u;
    std::cout << "{\"schema_version\":1,\"baseline\":"; WriteJsonString(std::cout, baseline.Directory.string());
    std::cout << ",\"current\":"; WriteJsonString(std::cout, current.Directory.string());
    std::cout << ",\"comparison_complete\":" << (comparisonComplete ? "true" : "false")
              << ",\"baseline_dropped_event_count\":" << baselineDropped
              << ",\"current_dropped_event_count\":" << currentDropped
              << ",\"baseline_dropped_performance_event_count\":" << baselinePerformanceDropped
              << ",\"current_dropped_performance_event_count\":" << currentPerformanceDropped;
    std::cout << ",\"graph\":{\"added_passes\":"; WriteStringArray(addedPasses);
    std::cout << ",\"removed_passes\":"; WriteStringArray(removedPasses);
    std::cout << "},\"new_failed_assertions\":"; WriteStringArray(newAssertions);
    std::cout << ",\"timing_regressions\":[";
    for (size_t index = 0; index < regressions.size(); ++index)
    {
        if (index != 0) std::cout << ',';
        const Regression& regression = regressions[index];
        std::cout << "{\"scope\":"; WriteJsonString(std::cout, regression.Key);
        std::cout << ",\"baseline_mean_ms\":" << regression.Baseline
                  << ",\"current_mean_ms\":" << regression.Current
                  << ",\"delta_percent\":" << regression.Percent
                  << ",\"baseline_p95_ms\":" << regression.BaselineP95
                  << ",\"current_p95_ms\":" << regression.CurrentP95
                  << ",\"baseline_sample_count\":" << regression.BaselineSamples
                  << ",\"current_sample_count\":" << regression.CurrentSamples << '}';
    }
    std::cout << "],\"regression_count\":" << (newAssertions.size() + regressions.size()) << "}\n";
    if (!newAssertions.empty() || !regressions.empty()) return 11;
    return comparisonComplete ? 0 : 12;
}
//Modify End
