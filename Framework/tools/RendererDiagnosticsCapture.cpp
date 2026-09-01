//Modify Begin:2026-09-01 by Hui
#include "RendererDiagnosticsCapture.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>

RendererDiagnosticsTool::Event RendererDiagnosticsTool::ParseEvent(const std::string& line)
{
    const JsonValue root = JsonParser(line).Parse();
    const JsonValue::Object& object = root.AsObject();
    Event event;
    event.Sequence = ToUint64(Find(object, "sequence"));
    if (const JsonValue* frame = Find(object, "frame");
        frame != nullptr && !std::holds_alternative<std::nullptr_t>(frame->Data))
    {
        event.Frame = ToUint64(frame);
    }
    event.CorrelationId = ToUint64(Find(object, "correlation_id"));
    if (const JsonValue* severity = Find(object, "severity")) event.Severity = ScalarToString(*severity);
    if (const JsonValue* category = Find(object, "category")) event.Category = ScalarToString(*category);
    if (const JsonValue* name = Find(object, "name")) event.Name = ScalarToString(*name);
    if (const JsonValue* fields = Find(object, "fields"))
    {
        for (const JsonValue& fieldValue : fields->AsArray())
        {
            const JsonValue::Object& field = fieldValue.AsObject();
            const JsonValue* fieldName = Find(field, "name");
            const JsonValue* value = Find(field, "value");
            if (fieldName != nullptr && value != nullptr)
            {
                event.Fields.insert_or_assign(ScalarToString(*fieldName), ScalarToString(*value));
            }
        }
    }
    event.RawJson = line;
    return event;
}

RendererDiagnosticsTool::Capture RendererDiagnosticsTool::LoadCapture(std::filesystem::path path)
{
    if (path.filename() == "manifest.json") path = path.parent_path();
    if (!std::filesystem::is_directory(path))
    {
        throw std::runtime_error("Capture directory does not exist: " + path.string());
    }
    Capture capture;
    capture.Directory = std::filesystem::absolute(path);
    capture.Manifest = JsonParser(ReadTextFile(capture.Directory / "manifest.json")).Parse();
    const JsonValue::Object& manifest = capture.Manifest.AsObject();
    if (ToUint64(Find(manifest, "schema_version")) != 1u)
    {
        throw std::runtime_error("Unsupported diagnostics schema version.");
    }
    if (const JsonValue* metadata = Find(manifest, "metadata"))
    {
        for (const auto& [key, value] : metadata->AsObject())
        {
            capture.Metadata[key] = ScalarToString(value);
        }
    }
    if (const JsonValue* artifacts = Find(manifest, "artifacts"))
    {
        for (const JsonValue& artifact : artifacts->AsArray())
        {
            const std::filesystem::path artifactPath =
                capture.Directory / std::filesystem::u8path(ScalarToString(artifact));
            if (!std::filesystem::is_regular_file(artifactPath))
            {
                throw std::runtime_error("Capture artifact is missing: " + artifactPath.string());
            }
        }
    }

    const auto loadEventFile = [&capture](const std::filesystem::path& eventPath, const bool required)
    {
        std::ifstream eventsFile(eventPath, std::ios::in | std::ios::binary);
        if (!eventsFile.is_open())
        {
            if (required)
            {
                throw std::runtime_error("Capture events.jsonl is missing.");
            }
            return;
        }
        std::string line;
        while (std::getline(eventsFile, line))
        {
            if (!line.empty()) capture.Events.push_back(ParseEvent(line));
        }
    };
    loadEventFile(capture.Directory / "events.jsonl", true);
    loadEventFile(capture.Directory / "performance_events.jsonl", false);
    std::ranges::sort(capture.Events, [](const Event& left, const Event& right)
    {
        return left.Sequence < right.Sequence;
    });

    const std::filesystem::path reproductionPath = capture.Directory / "reproduction.json";
    if (std::filesystem::is_regular_file(reproductionPath))
    {
        const JsonValue reproduction = JsonParser(ReadTextFile(reproductionPath)).Parse();
        if (const JsonValue* environment = Find(reproduction.AsObject(), "environment"))
        {
            for (const auto& [key, value] : environment->AsObject())
            {
                capture.ReproductionEnvironment[key] = ScalarToString(value);
            }
        }
    }
    return capture;
}

std::string RendererDiagnosticsTool::GetField(const Event& event, const std::string_view name)
{
    const auto field = event.Fields.find(name);
    return field != event.Fields.end() ? field->second : std::string();
}

bool RendererDiagnosticsTool::IsFailure(const Event& event)
{
    return event.Severity == "error" || event.Severity == "fatal" ||
        (event.Category == "assertion" && GetField(event, "result") == "fail");
}

std::optional<std::string> RendererDiagnosticsTool::GetOption(
    const std::vector<std::string>& arguments,
    const std::string_view name)
{
    for (size_t index = 0; index + 1u < arguments.size(); ++index)
    {
        if (arguments[index] == name) return arguments[index + 1u];
    }
    return std::nullopt;
}

bool RendererDiagnosticsTool::HasFlag(
    const std::vector<std::string>& arguments,
    const std::string_view name)
{
    return std::ranges::find(arguments, name) != arguments.end();
}

uint64_t RendererDiagnosticsTool::GetUnsignedOption(
    const std::vector<std::string>& arguments,
    const std::string_view name,
    const uint64_t fallback)
{
    const std::optional<std::string> value = GetOption(arguments, name);
    if (!value.has_value()) return fallback;
    uint64_t result = fallback;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), result);
    if (error != std::errc{} || end != value->data() + value->size())
    {
        throw std::runtime_error("Invalid unsigned option value for " + std::string(name) + ".");
    }
    return result;
}
//Modify End
