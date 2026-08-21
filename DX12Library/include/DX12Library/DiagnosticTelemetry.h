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
};
//Modify End
