//Modify Begin:2026-08-21 by Hui
#pragma once

#include <DX12Library/DiagnosticTelemetry.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace FrameworkDiagnostics
{
    class DiagnosticsSession;

    enum class AutomationStepKind : uint8_t
    {
        SetControl,
        WaitFrames,
        WaitObservation,
        AssertObservation,
        FlushCapture,
    };

    enum class AutomationComparison : uint8_t
    {
        Equal,
        NotEqual,
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual,
    };

    enum class AutomationExitCode : int
    {
        Success = 0,
        InvalidScenario = 20,
        ControlFailure = 21,
        ObservationFailure = 22,
        Timeout = 23,
        AssertionFailure = 24,
    };

    struct AutomationStep
    {
        AutomationStepKind Kind = AutomationStepKind::SetControl;
        std::string Name;
        std::string Target;
        DiagnosticTelemetryValue Value = uint64_t{ 0 };
        AutomationComparison Comparison = AutomationComparison::Equal;
        double Tolerance = 0.0;
        uint64_t FrameCount = 1;
        uint64_t TimeoutFrames = 600;
        double MinimumSeconds = 0.0;
        double TimeoutSeconds = 30.0;
    };

    struct AutomationScenario
    {
        std::string Name;
        std::vector<AutomationStep> Steps;
    };

    struct AutomationRunnerOptions
    {
        uint64_t DefaultSettleFrames = 1;
        double DefaultSettleSeconds = 0.0;
        bool FinalizeDiagnosticsOnCompletion = true;
    };

    class AutomationRunner final
    {
    public:
        using ControlSetter = std::function<bool(const DiagnosticTelemetryValue&, std::string&)>;
        using ObservationGetter = std::function<std::optional<DiagnosticTelemetryValue>(std::string&)>;
        using CompletionHandler = std::function<void(int)>;
        using LogHandler = std::function<void(std::string_view)>;

        bool RegisterControl(std::string name, ControlSetter setter);
        bool RegisterObservation(std::string name, ObservationGetter getter);
        void ClearRegistry();

        bool Start(
            AutomationScenario scenario,
            DiagnosticsSession* diagnostics,
            AutomationRunnerOptions options = {},
            CompletionHandler completionHandler = {},
            LogHandler logHandler = {});
        void Tick(uint64_t frameIndex, double totalSeconds);
        void Cancel(std::string reason);
        void FailNow(AutomationExitCode exitCode, std::string message);

        [[nodiscard]] bool IsRunning() const { return m_Running; }
        [[nodiscard]] bool IsCompleted() const { return m_Completed; }
        [[nodiscard]] bool HasFailed() const { return m_Failed; }
        [[nodiscard]] size_t GetStepIndex() const { return m_StepIndex; }
        [[nodiscard]] const std::string& GetFailureMessage() const { return m_FailureMessage; }

        static std::string ValueToString(const DiagnosticTelemetryValue& value);

    private:
        bool EvaluateObservation(const AutomationStep& step, bool failWhenFalse);
        bool HasStepTimedOut(const AutomationStep& step, uint64_t frameIndex, double totalSeconds) const;
        void BeginStep(const AutomationStep& step, uint64_t frameIndex, double totalSeconds);
        void CompleteStep(const AutomationStep& step);
        void CompleteScenario();
        void Fail(AutomationExitCode exitCode, std::string message, const AutomationStep* step = nullptr);
        void Log(std::string message) const;

        std::map<std::string, ControlSetter, std::less<>> m_Controls;
        std::map<std::string, ObservationGetter, std::less<>> m_Observations;
        AutomationScenario m_Scenario;
        AutomationRunnerOptions m_Options;
        DiagnosticsSession* m_Diagnostics = nullptr;
        CompletionHandler m_CompletionHandler;
        LogHandler m_LogHandler;
        size_t m_StepIndex = 0;
        uint64_t m_StepStartFrame = 0;
        double m_StepStartSeconds = 0.0;
        bool m_StepStarted = false;
        bool m_Running = false;
        bool m_Completed = false;
        bool m_Failed = false;
        std::string m_FailureMessage;
    };
}
//Modify End
