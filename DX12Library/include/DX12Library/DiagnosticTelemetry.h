//Modify Begin:2026-08-21 by Hui
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class DiagnosticTelemetrySeverity : uint8_t
{
    Trace,
    Info,
    Warning,
    Error,
    Fatal,
};

using DiagnosticTelemetryValue = std::variant<bool, int64_t, uint64_t, double, std::string>;

struct DiagnosticTelemetryField
{
    std::string Name;
    DiagnosticTelemetryValue Value;
};

struct DiagnosticTelemetryEvent
{
    static constexpr uint64_t NoFrame = (std::numeric_limits<uint64_t>::max)();

    std::string Category;
    std::string Name;
    DiagnosticTelemetrySeverity Severity = DiagnosticTelemetrySeverity::Info;
    uint64_t FrameIndex = NoFrame;
    uint64_t CorrelationId = 0;
    std::vector<DiagnosticTelemetryField> Fields;
};

struct DiagnosticPerformanceScopeRecord
{
    uint64_t FrameIndex = DiagnosticTelemetryEvent::NoFrame;
    uint64_t CorrelationId = 0;
    uint64_t ScopeId = 0;
    uint64_t ParentScopeId = 0;
    uint64_t ScopeDepth = 0;
    std::string_view Name;
    std::string_view QueueName;
    std::string_view ScopeKind;
    double DurationMilliseconds = 0.0;
};

inline uint64_t MakeDiagnosticQueueFenceCorrelationId(
    const std::string_view queueName,
    const uint64_t fenceValue) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char character : queueName)
    {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    for (uint32_t byteIndex = 0; byteIndex < sizeof(fenceValue); ++byteIndex)
    {
        hash ^= static_cast<uint8_t>(fenceValue >> (byteIndex * 8u));
        hash *= 1099511628211ull;
    }
    return hash;
}

class DiagnosticTelemetrySink
{
public:
    virtual ~DiagnosticTelemetrySink() = default;
    virtual void RecordTelemetry(DiagnosticTelemetryEvent event) noexcept = 0;
    virtual void RecordPerformanceScope(DiagnosticPerformanceScopeRecord record) noexcept
    {
        RecordTelemetry({
            .Category = "profiler.cpu.scope",
            .Name = std::string(record.Name),
            .FrameIndex = record.FrameIndex,
            .CorrelationId = record.CorrelationId,
            .Fields = {
                { "queue", std::string(record.QueueName) },
                { "scope_kind", std::string(record.ScopeKind) },
                { "scope_id", record.ScopeId },
                { "parent_scope_id", record.ParentScopeId },
                { "scope_depth", record.ScopeDepth },
                { "cpu_duration_ms", record.DurationMilliseconds },
            },
        });
    }
};
//Modify End
