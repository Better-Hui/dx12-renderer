//Modify Begin:2026-08-21 by Hui
#pragma once

#include <DX12Library/DiagnosticTelemetry.h>
#include <DX12Library/GpuTimestampProfiler.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FrameworkDiagnostics
{
    enum class SessionStatus : uint8_t
    {
        Running,
        Passed,
        Failed,
        Aborted,
    };

    enum class AssertionResult : uint8_t
    {
        Passed,
        Failed,
        Unknown,
    };

    struct DiagnosticsSessionOptions
    {
        std::string ApplicationName = "Renderer";
        std::string SessionName = "capture";
        std::filesystem::path OutputDirectory;
        size_t MaxEventCount = 65536;
        bool Enabled = true;
    };

    struct RecordedDiagnosticEvent
    {
        uint64_t Sequence = 0;
        uint64_t TimestampNanoseconds = 0;
        uint64_t ThreadId = 0;
        DiagnosticTelemetryEvent Event;
    };

    class DiagnosticsSession final : public DiagnosticTelemetrySink
    {
    public:
        DiagnosticsSession() = default;
        ~DiagnosticsSession() override;

        DiagnosticsSession(const DiagnosticsSession&) = delete;
        DiagnosticsSession& operator=(const DiagnosticsSession&) = delete;

        bool Begin(DiagnosticsSessionOptions options);
        bool BeginFromEnvironment(std::string applicationName, bool forceEnable = false);
        bool Flush();
        bool Finalize(SessionStatus status, std::string message = {});

        void SetFrameIndex(uint64_t frameIndex) noexcept;
        void AddMetadata(std::string key, std::string value);
        void Record(
            std::string category,
            std::string name,
            DiagnosticTelemetrySeverity severity = DiagnosticTelemetrySeverity::Info,
            std::vector<DiagnosticTelemetryField> fields = {},
            uint64_t correlationId = 0) noexcept;
        void RecordAssertion(
            std::string name,
            AssertionResult result,
            std::string message,
            std::vector<DiagnosticTelemetryField> fields = {}) noexcept;
        void RecordGpuTimings(
            uint64_t frameIndex,
            std::string_view queueName,
            std::span<const GpuTimestampSample> samples) noexcept;
        void RecordTelemetry(DiagnosticTelemetryEvent event) noexcept override;

        [[nodiscard]] bool IsEnabled() const noexcept { return m_Enabled.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsFinalized() const noexcept { return m_Finalized.load(std::memory_order_acquire); }
        [[nodiscard]] uint64_t GetDroppedEventCount() const noexcept { return m_DroppedEventCount.load(); }
        [[nodiscard]] std::filesystem::path GetOutputDirectory() const;
        [[nodiscard]] std::string GetLastError() const;
        [[nodiscard]] std::vector<RecordedDiagnosticEvent> GetEventsSnapshot() const;

    private:
        bool ExportSnapshot(SessionStatus status, std::string_view message);
        static std::filesystem::path ResolveOutputDirectory(const DiagnosticsSessionOptions& options);

        mutable std::mutex m_Mutex;
        DiagnosticsSessionOptions m_Options;
        std::filesystem::path m_OutputDirectory;
        std::map<std::string, std::string> m_Metadata;
        std::deque<RecordedDiagnosticEvent> m_Events;
        std::chrono::steady_clock::time_point m_StartTime = {};
        std::string m_StartUtc;
        std::string m_EndUtc;
        std::string m_LastError;
        std::string m_FinalMessage;
        SessionStatus m_Status = SessionStatus::Running;
        std::atomic<uint64_t> m_CurrentFrameIndex = DiagnosticTelemetryEvent::NoFrame;
        std::atomic<uint64_t> m_NextSequence = 1;
        std::atomic<uint64_t> m_DroppedEventCount = 0;
        std::atomic_bool m_Enabled = false;
        std::atomic_bool m_Finalized = false;
    };
}
//Modify End
