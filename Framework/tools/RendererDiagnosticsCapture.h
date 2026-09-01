//Modify Begin:2026-09-01 by Hui
#pragma once

#include "RendererDiagnosticsJson.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RendererDiagnosticsTool
{
    struct Event
    {
        uint64_t Sequence = 0;
        std::optional<uint64_t> Frame;
        uint64_t CorrelationId = 0;
        std::string Severity;
        std::string Category;
        std::string Name;
        std::map<std::string, std::string, std::less<>> Fields;
        std::string RawJson;
    };

    struct Capture
    {
        std::filesystem::path Directory;
        JsonValue Manifest;
        std::vector<Event> Events;
        std::map<std::string, std::string, std::less<>> Metadata;
        std::map<std::string, std::string, std::less<>> ReproductionEnvironment;
    };

    Event ParseEvent(const std::string& line);
    Capture LoadCapture(std::filesystem::path path);
    std::string GetField(const Event& event, std::string_view name);
    bool IsFailure(const Event& event);

    std::optional<std::string> GetOption(
        const std::vector<std::string>& arguments,
        std::string_view name);
    bool HasFlag(const std::vector<std::string>& arguments, std::string_view name);
    uint64_t GetUnsignedOption(
        const std::vector<std::string>& arguments,
        std::string_view name,
        uint64_t fallback);
}
//Modify End
