//Modify Begin:2026-08-28 by Hui
#include <Framework/Diagnostics/DiagnosticsSession.h>

#include <Windows.h>

#include <d3d12.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace
{
    using FrameworkDiagnostics::RecordedDiagnosticEvent;
    using FrameworkDiagnostics::SessionStatus;

    std::string GetEnvironmentVariable(const char* name)
    {
        char* value = nullptr;
        size_t length = 0;
        _dupenv_s(&value, &length, name);
        const std::string result = value != nullptr ? value : "";
        std::free(value);
        return result;
    }

    bool IsEnabledValue(const std::string& value)
    {
        return !value.empty() && value != "0" && value != "off" && value != "false";
    }

    size_t ParsePositiveSize(const std::string& value, const size_t fallback)
    {
        if (value.empty())
        {
            return fallback;
        }
        char* parseEnd = nullptr;
        const unsigned long long parsed = std::strtoull(value.c_str(), &parseEnd, 10);
        return parseEnd != value.c_str() && *parseEnd == '\0' && parsed > 0
            ? static_cast<size_t>(parsed)
            : fallback;
    }

    std::string FormatUtc(const std::chrono::system_clock::time_point time)
    {
        const std::time_t timeValue = std::chrono::system_clock::to_time_t(time);
        std::tm utc{};
        gmtime_s(&utc, &timeValue);
        std::ostringstream output;
        output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return output.str();
    }

    std::string FormatFileTimestamp(const std::chrono::system_clock::time_point time)
    {
        const std::time_t timeValue = std::chrono::system_clock::to_time_t(time);
        std::tm local{};
        localtime_s(&local, &timeValue);
        std::ostringstream output;
        output << std::put_time(&local, "%Y%m%d-%H%M%S");
        return output.str();
    }

    std::string SanitizeName(std::string value)
    {
        for (char& character : value)
        {
            const bool valid =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_';
            if (!valid)
            {
                character = '_';
            }
        }
        return value.empty() ? "capture" : value;
    }

    const char* ToString(const DiagnosticTelemetrySeverity severity)
    {
        switch (severity)
        {
        case DiagnosticTelemetrySeverity::Trace: return "trace";
        case DiagnosticTelemetrySeverity::Info: return "info";
        case DiagnosticTelemetrySeverity::Warning: return "warning";
        case DiagnosticTelemetrySeverity::Error: return "error";
        case DiagnosticTelemetrySeverity::Fatal: return "fatal";
        default: return "unknown";
        }
    }

    const char* ToString(const SessionStatus status)
    {
        switch (status)
        {
        case SessionStatus::Running: return "running";
        case SessionStatus::Passed: return "passed";
        case SessionStatus::Failed: return "failed";
        case SessionStatus::Aborted: return "aborted";
        default: return "unknown";
        }
    }

    std::string EscapeJson(const std::string_view value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<uint32_t>(character) << std::dec;
                }
                else
                {
                    output << static_cast<char>(character);
                }
                break;
            }
        }
        return output.str();
    }

    void WriteJsonValue(std::ostream& output, const DiagnosticTelemetryValue& value)
    {
        std::visit([&output](const auto& typedValue)
        {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, bool>)
            {
                output << (typedValue ? "true" : "false");
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                output << '"' << EscapeJson(typedValue) << '"';
            }
            else
            {
                output << std::setprecision(15) << typedValue;
            }
        }, value);
    }

    void WriteEventJson(std::ostream& output, const RecordedDiagnosticEvent& event)
    {
        output << "{\"sequence\":" << event.Sequence
               << ",\"timestamp_ns\":" << event.TimestampNanoseconds
               << ",\"thread_id\":" << event.ThreadId << ",\"frame\":";
        if (event.Event.FrameIndex == DiagnosticTelemetryEvent::NoFrame)
        {
            output << "null";
        }
        else
        {
            output << event.Event.FrameIndex;
        }
        output << ",\"correlation_id\":" << event.Event.CorrelationId
               << ",\"severity\":\"" << ToString(event.Event.Severity) << '"'
               << ",\"category\":\"" << EscapeJson(event.Event.Category) << '"'
               << ",\"name\":\"" << EscapeJson(event.Event.Name) << '"'
               << ",\"fields\":[";
        for (size_t index = 0; index < event.Event.Fields.size(); ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            const DiagnosticTelemetryField& field = event.Event.Fields[index];
            output << "{\"name\":\"" << EscapeJson(field.Name) << "\",\"value\":";
            WriteJsonValue(output, field.Value);
            output << '}';
        }
        output << "]}";
    }

    bool CategoryStartsWith(const RecordedDiagnosticEvent& event, const std::string_view prefix)
    {
        return event.Event.Category.starts_with(prefix);
    }

    template<typename Writer>
    void WriteAtomically(const std::filesystem::path& path, const Writer& writer)
    {
        std::filesystem::path temporaryPath = path;
        temporaryPath += ".tmp";
        std::error_code error;
        std::filesystem::remove(temporaryPath, error);
        std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open diagnostics artifact: " + temporaryPath.string());
        }
        writer(output);
        output.flush();
        if (!output.good())
        {
            throw std::runtime_error("Failed to write diagnostics artifact: " + temporaryPath.string());
        }
        output.close();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
        if (error)
        {
            throw std::runtime_error("Failed to publish diagnostics artifact: " + path.string());
        }
    }

    void WriteFilteredEvents(
        const std::filesystem::path& path,
        const std::vector<RecordedDiagnosticEvent>& events,
        const std::function<bool(const RecordedDiagnosticEvent&)>& predicate)
    {
        WriteAtomically(path, [&events, &predicate](std::ostream& output)
        {
            size_t count = 0;
            for (const RecordedDiagnosticEvent& event : events)
            {
                count += predicate(event) ? 1u : 0u;
            }
            output << "{\"schema_version\":1,\"event_count\":" << count << ",\"events\":[";
            bool first = true;
            for (const RecordedDiagnosticEvent& event : events)
            {
                if (!predicate(event))
                {
                    continue;
                }
                if (!first)
                {
                    output << ',';
                }
                first = false;
                WriteEventJson(output, event);
            }
            output << "]}\n";
        });
    }

    std::string FieldToString(const RecordedDiagnosticEvent& event, const std::string_view name)
    {
        const auto field = std::ranges::find_if(event.Event.Fields, [name](const DiagnosticTelemetryField& candidate)
        {
            return candidate.Name == name;
        });
        if (field == event.Event.Fields.end())
        {
            return {};
        }
        return std::visit([](const auto& value)
        {
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, bool>)
            {
                return std::string(value ? "true" : "false");
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                return value;
            }
            else
            {
                std::ostringstream output;
                output << std::setprecision(15) << value;
                return output.str();
            }
        }, field->Value);
    }

    std::string TelemetryFieldToString(const DiagnosticTelemetryEvent& event, const std::string_view name)
    {
        const auto field = std::ranges::find_if(event.Fields, [name](const DiagnosticTelemetryField& candidate)
        {
            return candidate.Name == name;
        });
        if (field == event.Fields.end())
        {
            return {};
        }
        return std::visit([](const auto& value)
        {
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, bool>)
            {
                return std::string(value ? "true" : "false");
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                return value;
            }
            else
            {
                std::ostringstream output;
                output << std::setprecision(15) << value;
                return output.str();
            }
        }, field->Value);
    }

    bool IsHighFrequencyInformationalEvent(const DiagnosticTelemetryEvent& event)
    {
        if (event.Severity >= DiagnosticTelemetrySeverity::Warning)
        {
            return false;
        }
        if (event.Category == "profiler.cpu" ||
            event.Category == "render_graph.batch" ||
            event.Category == "descriptor.binding" ||
            event.Category == "render_graph.frame" ||
            event.Category == "command_queue.signal" ||
            event.Category == "command_queue.submission" ||
            event.Category == "command_queue.command_list" ||
            event.Category == "command_queue.wait" ||
            event.Category == "render_graph.queue.submission" ||
            event.Category == "render_graph.queue.wait")
        {
            return true;
        }
        return event.Category == "assertion" && event.Name == "render_graph_queue_lifetime_runtime";
    }

    std::string BuildHighFrequencySeriesKey(const DiagnosticTelemetryEvent& event)
    {
        std::string key = event.Category + "\x1f" + event.Name;
        for (const std::string_view fieldName : {
                 "queue",
                 "consumer_queue",
                 "producer_queue",
                 "render_graph.pass_correlation",
                 "set_index",
                 "bind_point",
                 "batch_index",
                 "recording_mode",
                 "parallel",
                 "cross_queue_transfer_count",
                 "missing_producer_signal_count",
                 "missing_consumer_wait_count",
                 "missing_retirement_fence_count" })
        {
            const std::string value = TelemetryFieldToString(event, fieldName);
            if (!value.empty())
            {
                key += "\x1e";
                key += fieldName;
                key += '=';
                key += value;
            }
        }
        return key;
    }

    std::string EscapeCsv(const std::string_view value)
    {
        std::string output = "\"";
        for (const char character : value)
        {
            output += character == '"' ? "\"\"" : std::string(1, character);
        }
        output += '"';
        return output;
    }

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
        {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1)
        {
            return {};
        }
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::string GetDredName(const char* nameA, const wchar_t* nameW)
    {
        if (nameA != nullptr && *nameA != '\0')
        {
            return nameA;
        }
        const std::string name = WideToUtf8(nameW);
        return name.empty() ? "<unnamed>" : name;
    }

    void WriteDredAllocations(
        std::ostream& output,
        const char* heading,
        const D3D12_DRED_ALLOCATION_NODE1* allocation)
    {
        output << heading << ":\n";
        uint32_t count = 0u;
        for (; allocation != nullptr && count < 128u; allocation = allocation->pNext, ++count)
        {
            output << "  [" << count << "] type=" << static_cast<uint32_t>(allocation->AllocationType)
                   << " name=" << GetDredName(allocation->ObjectNameA, allocation->ObjectNameW) << '\n';
        }
        if (allocation != nullptr)
        {
            output << "  <truncated after 128 allocations>\n";
        }
    }

    std::string BuildDredReport(ID3D12Device2& device, const std::string_view stage)
    {
        std::ostringstream output;
        const HRESULT removedReason = device.GetDeviceRemovedReason();
        output << "Stage=" << stage << '\n'
               << "DeviceRemovedReason=0x" << std::hex << static_cast<uint32_t>(removedReason) << std::dec << '\n';

        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        const HRESULT interfaceResult = device.QueryInterface(IID_PPV_ARGS(&dred));
        output << "DredInterface=0x" << std::hex << static_cast<uint32_t>(interfaceResult) << std::dec << '\n';
        if (FAILED(interfaceResult))
        {
            return output.str();
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
        const HRESULT breadcrumbsResult = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        output << "AutoBreadcrumbs=0x" << std::hex << static_cast<uint32_t>(breadcrumbsResult) << std::dec << '\n';
        if (SUCCEEDED(breadcrumbsResult))
        {
            uint32_t count = 0u;
            for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                node != nullptr && count < 128u;
                node = node->pNext, ++count)
            {
                output << "Breadcrumb[" << count << "] queue="
                       << GetDredName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW)
                       << " command_list=" << GetDredName(node->pCommandListDebugNameA, node->pCommandListDebugNameW)
                       << " completed=" << (node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0u)
                       << " count=" << node->BreadcrumbCount << '\n';
            }
            if (count == 128u)
            {
                output << "Breadcrumbs=<truncated after 128 nodes>\n";
            }
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
        const HRESULT pageFaultResult = dred->GetPageFaultAllocationOutput1(&pageFault);
        output << "PageFault=0x" << std::hex << static_cast<uint32_t>(pageFaultResult) << std::dec << '\n';
        if (SUCCEEDED(pageFaultResult))
        {
            output << "PageFaultVA=0x" << std::hex << pageFault.PageFaultVA << std::dec << '\n';
            WriteDredAllocations(output, "ExistingAllocations", pageFault.pHeadExistingAllocationNode);
            WriteDredAllocations(output, "RecentlyFreedAllocations", pageFault.pHeadRecentFreedAllocationNode);
        }
        return output.str();
    }
}

FrameworkDiagnostics::DiagnosticsSession::~DiagnosticsSession()
{
    if (IsEnabled() && !IsFinalized())
    {
        Finalize(SessionStatus::Aborted, "Diagnostics session owner was destroyed before explicit completion.");
    }
}

bool FrameworkDiagnostics::DiagnosticsSession::Begin(DiagnosticsSessionOptions options)
{
    if (!options.Enabled)
    {
        return false;
    }
    options.MaxEventCount = (std::max<size_t>)(1024u, options.MaxEventCount);
    options.HighFrequencySampleIntervalFrames = (std::max)(uint64_t{ 1 }, options.HighFrequencySampleIntervalFrames);
    try
    {
        const std::filesystem::path outputDirectory = ResolveOutputDirectory(options);
        std::filesystem::create_directories(outputDirectory);
        {
            std::scoped_lock lock(m_Mutex);
            m_Options = std::move(options);
            m_OutputDirectory = outputDirectory;
            m_Metadata.clear();
            m_Attachments.clear();
            m_Events.clear();
            m_LastSampledFrames.clear();
            m_LastError.clear();
            m_FinalMessage.clear();
            m_Status = SessionStatus::Running;
            m_StartTime = std::chrono::steady_clock::now();
            m_StartUtc = FormatUtc(std::chrono::system_clock::now());
            m_EndUtc.clear();
        }
        m_CurrentFrameIndex.store(DiagnosticTelemetryEvent::NoFrame);
        m_NextSequence.store(1);
        m_DroppedEventCount.store(0);
        m_SampledEventCount.store(0);
        m_FailedAssertionCount.store(0);
        m_Finalized.store(false, std::memory_order_release);
        m_Enabled.store(true, std::memory_order_release);
        Record("session", "begin", DiagnosticTelemetrySeverity::Info, {
            { "application", m_Options.ApplicationName },
            { "session", m_Options.SessionName },
            { "max_events", static_cast<uint64_t>(m_Options.MaxEventCount) },
            { "high_frequency_sample_interval_frames", m_Options.HighFrequencySampleIntervalFrames },
        });
        return true;
    }
    catch (const std::exception& exception)
    {
        std::scoped_lock lock(m_Mutex);
        m_LastError = exception.what();
        m_Enabled.store(false, std::memory_order_release);
        return false;
    }
}

bool FrameworkDiagnostics::DiagnosticsSession::BeginFromEnvironment(
    std::string applicationName,
    const bool forceEnable)
{
    const std::string enabledValue = GetEnvironmentVariable("RENDERER_DIAGNOSTICS");
    DiagnosticsSessionOptions options;
    options.Enabled = forceEnable || IsEnabledValue(enabledValue);
    options.ApplicationName = std::move(applicationName);
    const std::string sessionName = GetEnvironmentVariable("RENDERER_DIAGNOSTICS_SESSION");
    if (!sessionName.empty())
    {
        options.SessionName = sessionName;
    }
    const std::string outputDirectory = GetEnvironmentVariable("RENDERER_DIAGNOSTICS_OUTPUT");
    if (!outputDirectory.empty())
    {
        options.OutputDirectory = std::filesystem::u8path(outputDirectory);
    }
    options.MaxEventCount = ParsePositiveSize(
        GetEnvironmentVariable("RENDERER_DIAGNOSTICS_MAX_EVENTS"),
        options.MaxEventCount);
    options.HighFrequencySampleIntervalFrames = ParsePositiveSize(
        GetEnvironmentVariable("RENDERER_DIAGNOSTICS_SAMPLE_INTERVAL_FRAMES"),
        options.HighFrequencySampleIntervalFrames);
    return Begin(std::move(options));
}

void FrameworkDiagnostics::DiagnosticsSession::SetFrameIndex(const uint64_t frameIndex) noexcept
{
    m_CurrentFrameIndex.store(frameIndex, std::memory_order_release);
}

void FrameworkDiagnostics::DiagnosticsSession::AddMetadata(std::string key, std::string value)
{
    if (!IsEnabled() || key.empty())
    {
        return;
    }
    std::scoped_lock lock(m_Mutex);
    m_Metadata[std::move(key)] = std::move(value);
}

bool FrameworkDiagnostics::DiagnosticsSession::RegisterAttachment(
    std::filesystem::path relativePath,
    std::string mediaType)
{
    if (!IsEnabled())
    {
        return false;
    }
    relativePath = relativePath.lexically_normal();
    if (relativePath.empty() || relativePath.is_absolute() ||
        relativePath.begin() == relativePath.end() || *relativePath.begin() == "..")
    {
        return false;
    }
    std::scoped_lock lock(m_Mutex);
    m_Attachments.insert_or_assign(std::move(relativePath), std::move(mediaType));
    return true;
}

void FrameworkDiagnostics::DiagnosticsSession::AttachDeviceRemovalDred(
    ID3D12Device2& device,
    std::string stage) noexcept
{
    if (!IsEnabled())
    {
        return;
    }
    try
    {
        const std::filesystem::path attachmentPath = "dred.txt";
        const std::filesystem::path outputPath = GetOutputDirectory() / attachmentPath;
        WriteAtomically(outputPath, [&device, &stage](std::ostream& output)
        {
            output << BuildDredReport(device, stage);
        });
        if (!RegisterAttachment(attachmentPath, "text/plain"))
        {
            throw std::runtime_error("Diagnostics DRED attachment path was rejected.");
        }
        Record(
            "device.removal",
            "dred_attached",
            DiagnosticTelemetrySeverity::Error,
            { { "attachment", attachmentPath.generic_string() }, { "stage", std::move(stage) } });
    }
    catch (const std::exception& exception)
    {
        Record(
            "device.removal",
            "dred_attachment_failed",
            DiagnosticTelemetrySeverity::Error,
            { { "message", std::string(exception.what()) }, { "stage", std::move(stage) } });
    }
}

void FrameworkDiagnostics::DiagnosticsSession::Record(
    std::string category,
    std::string name,
    const DiagnosticTelemetrySeverity severity,
    std::vector<DiagnosticTelemetryField> fields,
    const uint64_t correlationId) noexcept
{
    DiagnosticTelemetryEvent event;
    event.Category = std::move(category);
    event.Name = std::move(name);
    event.Severity = severity;
    event.CorrelationId = correlationId;
    event.Fields = std::move(fields);
    RecordTelemetry(std::move(event));
}

void FrameworkDiagnostics::DiagnosticsSession::RecordAssertion(
    std::string name,
    const AssertionResult result,
    std::string message,
    std::vector<DiagnosticTelemetryField> fields) noexcept
{
    const char* resultName = result == AssertionResult::Passed
        ? "pass"
        : result == AssertionResult::Failed ? "fail" : "unknown";
    fields.insert(fields.begin(), { "message", std::move(message) });
    fields.insert(fields.begin(), { "result", std::string(resultName) });
    Record(
        "assertion",
        std::move(name),
        result == AssertionResult::Failed
            ? DiagnosticTelemetrySeverity::Error
            : result == AssertionResult::Unknown
                ? DiagnosticTelemetrySeverity::Warning
                : DiagnosticTelemetrySeverity::Info,
        std::move(fields));
}

void FrameworkDiagnostics::DiagnosticsSession::RecordGpuTimings(
    const uint64_t frameIndex,
    const std::string_view queueName,
    const std::span<const GpuTimestampSample> samples) noexcept
{
    for (const GpuTimestampSample& sample : samples)
    {
        DiagnosticTelemetryEvent event;
        event.Category = "profiler.gpu";
        event.Name = sample.Name;
        event.FrameIndex = frameIndex;
        event.Fields = {
            { "queue", std::string(queueName) },
            { "gpu_delta_ms", sample.MillisecondsFromPrevious },
            { "gpu_total_ms", sample.MillisecondsFromFrameStart },
            { "cpu_delta_ms", sample.CpuMillisecondsFromPrevious },
            { "cpu_total_ms", sample.CpuMillisecondsFromFrameStart },
        };
        RecordTelemetry(std::move(event));
    }
}

void FrameworkDiagnostics::DiagnosticsSession::RecordTelemetry(DiagnosticTelemetryEvent event) noexcept
{
    if (!IsEnabled() || IsFinalized())
    {
        return;
    }
    try
    {
        const bool failedAssertion = event.Category == "assertion" && std::ranges::any_of(
            event.Fields,
            [](const DiagnosticTelemetryField& field)
            {
                const std::string* value = std::get_if<std::string>(&field.Value);
                return field.Name == "result" && value != nullptr && *value == "fail";
            });
        if (event.FrameIndex == DiagnosticTelemetryEvent::NoFrame)
        {
            event.FrameIndex = m_CurrentFrameIndex.load(std::memory_order_acquire);
        }
        std::scoped_lock lock(m_Mutex);
        if (m_Finalized.load(std::memory_order_acquire))
        {
            return;
        }
        if (!failedAssertion && IsHighFrequencyInformationalEvent(event))
        {
            const std::string seriesKey = BuildHighFrequencySeriesKey(event);
            const auto previous = m_LastSampledFrames.find(seriesKey);
            const uint64_t frameIndex = event.FrameIndex;
            if (previous != m_LastSampledFrames.end() &&
                frameIndex >= previous->second &&
                frameIndex - previous->second < m_Options.HighFrequencySampleIntervalFrames)
            {
                m_SampledEventCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            m_LastSampledFrames.insert_or_assign(std::move(seriesKey), frameIndex);
        }

        RecordedDiagnosticEvent recorded;
        recorded.Sequence = m_NextSequence.fetch_add(1);
        recorded.TimestampNanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - m_StartTime).count());
        recorded.ThreadId = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        recorded.Event = std::move(event);
        if (m_Events.size() >= m_Options.MaxEventCount)
        {
            const auto removable = std::ranges::find_if(m_Events, [](const RecordedDiagnosticEvent& candidate)
            {
                return candidate.Event.Category != "assertion" &&
                    candidate.Event.Severity < DiagnosticTelemetrySeverity::Error;
            });
            if (removable != m_Events.end())
            {
                m_Events.erase(removable);
            }
            else
            {
                m_Events.pop_front();
            }
            m_DroppedEventCount.fetch_add(1);
        }
        m_Events.push_back(std::move(recorded));
        if (failedAssertion)
        {
            m_FailedAssertionCount.fetch_add(1, std::memory_order_release);
        }
    }
    catch (...)
    {
        m_DroppedEventCount.fetch_add(1);
    }
}

bool FrameworkDiagnostics::DiagnosticsSession::Flush()
{
    if (!IsEnabled())
    {
        return false;
    }
    SessionStatus status = SessionStatus::Running;
    std::string message;
    {
        std::scoped_lock lock(m_Mutex);
        status = m_Status;
        message = m_FinalMessage;
    }
    return ExportSnapshot(status, message);
}

bool FrameworkDiagnostics::DiagnosticsSession::Finalize(SessionStatus status, std::string message)
{
    if (!IsEnabled())
    {
        return false;
    }
    bool expected = false;
    if (!m_Finalized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return GetLastError().empty();
    }
    std::string finalMessage;
    {
        std::scoped_lock lock(m_Mutex);
        if (status == SessionStatus::Passed)
        {
            const bool containsFailure = std::ranges::any_of(m_Events, [](const RecordedDiagnosticEvent& event)
            {
                return event.Event.Severity >= DiagnosticTelemetrySeverity::Error ||
                    (event.Event.Category == "assertion" && FieldToString(event, "result") == "fail");
            });
            if (containsFailure)
            {
                status = SessionStatus::Failed;
                if (message.empty())
                {
                    message = "The capture contains an error, fatal event, or failed assertion.";
                }
            }
        }
        m_Status = status;
        m_FinalMessage = std::move(message);
        finalMessage = m_FinalMessage;
        m_EndUtc = FormatUtc(std::chrono::system_clock::now());
    }
    const bool result = ExportSnapshot(status, finalMessage);
    m_Enabled.store(false, std::memory_order_release);
    return result;
}

std::filesystem::path FrameworkDiagnostics::DiagnosticsSession::GetOutputDirectory() const
{
    std::scoped_lock lock(m_Mutex);
    return m_OutputDirectory;
}

std::string FrameworkDiagnostics::DiagnosticsSession::GetLastError() const
{
    std::scoped_lock lock(m_Mutex);
    return m_LastError;
}

std::vector<FrameworkDiagnostics::RecordedDiagnosticEvent>
FrameworkDiagnostics::DiagnosticsSession::GetEventsSnapshot() const
{
    std::scoped_lock lock(m_Mutex);
    return { m_Events.begin(), m_Events.end() };
}

std::filesystem::path FrameworkDiagnostics::DiagnosticsSession::ResolveOutputDirectory(
    const DiagnosticsSessionOptions& options)
{
    if (!options.OutputDirectory.empty())
    {
        return options.OutputDirectory;
    }
    const std::string directoryName =
        SanitizeName(options.ApplicationName) + "-" +
        SanitizeName(options.SessionName) + "-" +
        FormatFileTimestamp(std::chrono::system_clock::now()) +
        "-p" + std::to_string(GetCurrentProcessId());
    return std::filesystem::current_path() / "Saved" / "Diagnostics" / directoryName;
}

bool FrameworkDiagnostics::DiagnosticsSession::ExportSnapshot(
    const SessionStatus status,
    const std::string_view message)
{
    try
    {
        std::vector<RecordedDiagnosticEvent> events;
        std::map<std::string, std::string> metadata;
        std::map<std::filesystem::path, std::string> attachments;
        DiagnosticsSessionOptions options;
        std::filesystem::path outputDirectory;
        std::string startUtc;
        std::string endUtc;
        {
            std::scoped_lock lock(m_Mutex);
            events.assign(m_Events.begin(), m_Events.end());
            metadata = m_Metadata;
            attachments = m_Attachments;
            options = m_Options;
            outputDirectory = m_OutputDirectory;
            startUtc = m_StartUtc;
            endUtc = m_EndUtc;
        }
        std::filesystem::create_directories(outputDirectory);

        WriteAtomically(outputDirectory / "events.jsonl", [&events](std::ostream& output)
        {
            for (const RecordedDiagnosticEvent& event : events)
            {
                WriteEventJson(output, event);
                output << '\n';
            }
        });
        WriteFilteredEvents(outputDirectory / "render_graph.json", events, [](const RecordedDiagnosticEvent& event)
        {
            return CategoryStartsWith(event, "render_graph");
        });
        WriteFilteredEvents(outputDirectory / "queue_submissions.json", events, [](const RecordedDiagnosticEvent& event)
        {
            return CategoryStartsWith(event, "command_queue") || CategoryStartsWith(event, "render_graph.queue");
        });
        WriteFilteredEvents(outputDirectory / "resources.json", events, [](const RecordedDiagnosticEvent& event)
        {
            return CategoryStartsWith(event, "resource") || CategoryStartsWith(event, "render_graph.resource");
        });
        WriteFilteredEvents(outputDirectory / "descriptors.json", events, [](const RecordedDiagnosticEvent& event)
        {
            return CategoryStartsWith(event, "descriptor");
        });
        WriteFilteredEvents(outputDirectory / "assertions.json", events, [](const RecordedDiagnosticEvent& event)
        {
            return event.Event.Category == "assertion";
        });

        WriteAtomically(outputDirectory / "reproduction.json", [&](std::ostream& output)
        {
            output << "{\"schema_version\":1,\"environment\":{";
            bool firstEnvironment = true;
            for (const auto& [key, value] : metadata)
            {
                if (!key.starts_with("env."))
                {
                    continue;
                }
                if (!firstEnvironment)
                {
                    output << ',';
                }
                firstEnvironment = false;
                output << '"' << EscapeJson(key.substr(4)) << "\":\"" << EscapeJson(value) << '"';
            }
            output << "},\"controls\":[";
            bool firstControl = true;
            for (const RecordedDiagnosticEvent& event : events)
            {
                if (event.Event.Category != "automation.control" || event.Event.Name != "set")
                {
                    continue;
                }
                if (!firstControl)
                {
                    output << ',';
                }
                firstControl = false;
                output << "{\"sequence\":" << event.Sequence
                       << ",\"frame\":";
                if (event.Event.FrameIndex == DiagnosticTelemetryEvent::NoFrame)
                {
                    output << "null";
                }
                else
                {
                    output << event.Event.FrameIndex;
                }
                output << ",\"name\":\"" << EscapeJson(FieldToString(event, "control"))
                       << "\",\"value\":\"" << EscapeJson(FieldToString(event, "value")) << "\"}";
            }
            output << "],\"source_session\":\"" << EscapeJson(options.SessionName) << "\"}\n";
        });

        WriteAtomically(outputDirectory / "timings.csv", [&events](std::ostream& output)
        {
            output << "sequence,frame,category,name,queue,cpu_duration_ms,gpu_delta_ms,gpu_total_ms,cpu_delta_ms,cpu_total_ms\n";
            for (const RecordedDiagnosticEvent& event : events)
            {
                if (!CategoryStartsWith(event, "profiler."))
                {
                    continue;
                }
                output << event.Sequence << ',';
                if (event.Event.FrameIndex != DiagnosticTelemetryEvent::NoFrame)
                {
                    output << event.Event.FrameIndex;
                }
                output << ',' << EscapeCsv(event.Event.Category)
                       << ',' << EscapeCsv(event.Event.Name)
                       << ',' << EscapeCsv(FieldToString(event, "queue"))
                       << ',' << FieldToString(event, "cpu_duration_ms")
                       << ',' << FieldToString(event, "gpu_delta_ms")
                       << ',' << FieldToString(event, "gpu_total_ms")
                       << ',' << FieldToString(event, "cpu_delta_ms")
                       << ',' << FieldToString(event, "cpu_total_ms") << '\n';
            }
        });

        size_t failedAssertions = 0;
        size_t unknownAssertions = 0;
        size_t errorEvents = 0;
        for (const RecordedDiagnosticEvent& event : events)
        {
            errorEvents += event.Event.Severity >= DiagnosticTelemetrySeverity::Error ? 1u : 0u;
            if (event.Event.Category == "assertion")
            {
                failedAssertions += FieldToString(event, "result") == "fail" ? 1u : 0u;
                unknownAssertions += FieldToString(event, "result") == "unknown" ? 1u : 0u;
            }
        }

        WriteAtomically(outputDirectory / "summary.txt", [&](std::ostream& output)
        {
            output << "Status: " << ToString(status) << '\n'
                   << "Application: " << options.ApplicationName << '\n'
                   << "Session: " << options.SessionName << '\n'
                   << "Events: " << events.size() << '\n'
                   << "Dropped events: " << m_DroppedEventCount.load() << '\n'
                   << "Sampled informational events: " << m_SampledEventCount.load() << '\n'
                   << "Failed assertions: " << failedAssertions << '\n'
                   << "Unknown assertions: " << unknownAssertions << '\n'
                   << "Error or fatal events: " << errorEvents << '\n';
            if (!message.empty())
            {
                output << "Message: " << message << '\n';
            }
            if (failedAssertions != 0)
            {
                output << "Next: run RendererDiagnostics inspect on this capture for correlated evidence.\n";
            }
            else if (errorEvents != 0)
            {
                output << "Next: inspect error/fatal events and queue_submissions.json.\n";
            }
            else if (unknownAssertions != 0)
            {
                output << "Next: resolve unknown assertions before treating this capture as a clean result.\n";
            }
            else
            {
                output << "Next: compare this capture with a known-good baseline.\n";
            }
        });

        WriteAtomically(outputDirectory / "manifest.json", [&](std::ostream& output)
        {
            output << "{\"schema_version\":1"
                   << ",\"status\":\"" << ToString(status) << '"'
                   << ",\"application\":\"" << EscapeJson(options.ApplicationName) << '"'
                   << ",\"session\":\"" << EscapeJson(options.SessionName) << '"'
                   << ",\"start_utc\":\"" << EscapeJson(startUtc) << '"'
                   << ",\"end_utc\":";
            if (endUtc.empty())
            {
                output << "null";
            }
            else
            {
                output << '"' << EscapeJson(endUtc) << '"';
            }
            output << ",\"event_count\":" << events.size()
                   << ",\"dropped_event_count\":" << m_DroppedEventCount.load()
                   << ",\"sampled_event_count\":" << m_SampledEventCount.load()
                   << ",\"failed_assertion_count\":" << failedAssertions
                   << ",\"unknown_assertion_count\":" << unknownAssertions
                   << ",\"error_event_count\":" << errorEvents
                   << ",\"message\":";
            if (message.empty())
            {
                output << "null";
            }
            else
            {
                output << '"' << EscapeJson(message) << '"';
            }
            output << ",\"metadata\":{";
            bool first = true;
            for (const auto& [key, value] : metadata)
            {
                if (!first)
                {
                    output << ',';
                }
                first = false;
                output << '"' << EscapeJson(key) << "\":\"" << EscapeJson(value) << '"';
            }
            const std::array<std::string_view, 9u> standardArtifacts = {
                "summary.txt", "events.jsonl", "render_graph.json", "queue_submissions.json",
                "resources.json", "descriptors.json", "timings.csv", "assertions.json", "reproduction.json",
            };
            output << "},\"artifacts\":[";
            bool firstArtifact = true;
            const auto writeArtifact = [&output, &firstArtifact](const std::string_view artifact)
            {
                if (!firstArtifact)
                {
                    output << ',';
                }
                firstArtifact = false;
                output << '\"' << EscapeJson(artifact) << '\"';
            };
            for (const std::string_view artifact : standardArtifacts)
            {
                writeArtifact(artifact);
            }
            for (const auto& [attachmentPath, mediaType] : attachments)
            {
                (void)mediaType;
                writeArtifact(attachmentPath.generic_string());
            }
            output << "]}\n";
        });
        return true;
    }
    catch (const std::exception& exception)
    {
        std::scoped_lock lock(m_Mutex);
        m_LastError = exception.what();
        return false;
    }
}
//Modify End
