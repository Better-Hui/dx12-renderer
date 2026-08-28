#pragma once

//Modify Begin:2026-08-28 by Hui
#include <Framework/Diagnostics/AutomationRunner.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace DemoAutomation
{
    struct Step
    {
        uint32_t Action = 0;
        uint32_t Value = 0;
        std::string Control;
        std::string Name;
        FrameworkDiagnostics::AutomationStepKind Kind = FrameworkDiagnostics::AutomationStepKind::SetControl;
    };

    struct TestSuites
    {
        std::vector<Step> Core;
        std::vector<Step> Stress;
        std::vector<Step> MeshletIndirect;
        std::vector<Step> CopyQueue;
        std::vector<Step> Rtas;
        std::vector<Step> DynamicScene;
        std::vector<Step> OIDN;
        std::vector<Step> Matrix;
        std::vector<Step> ReSTIRGIProfile;
        std::vector<Step> ReSTIRGIVariants;
        std::vector<Step> ReSTIRDIVariants;
        std::vector<Step> Visual;
    };

    class RuntimeAutomationController final
    {
    public:
        using ActionHandler = std::function<void(uint32_t action, uint32_t value)>;
        using CompletionHandler = std::function<void(int exitCode)>;

        void Initialize(
            const TestSuites& testSuites,
            FrameworkDiagnostics::DiagnosticsSession* diagnostics,
            const ActionHandler& actionHandler,
            const CompletionHandler& completionHandler);
        void Update(uint64_t frameIndex, double totalTime);
        void AppendDiagnosticLog(const std::string& message) const;
        bool FailNow(FrameworkDiagnostics::AutomationExitCode exitCode, std::string message);
        [[nodiscard]] bool IsRunning() const { return m_Runner.IsRunning(); }
        [[nodiscard]] bool IsCompleted() const { return m_Runner.IsCompleted(); }
        [[nodiscard]] bool HasFailed() const { return m_Runner.HasFailed(); }
        [[nodiscard]] const std::string& GetFailureMessage() const { return m_Runner.GetFailureMessage(); }

    private:
        void AppendLog(const std::string& message) const;

        FrameworkDiagnostics::AutomationRunner m_Runner;
        FrameworkDiagnostics::DiagnosticsSession* m_Diagnostics = nullptr;
        std::filesystem::path m_LogPath;
        double m_StepIntervalSeconds = 1.0;
        double m_StepTimeoutSeconds = 30.0;
        bool m_QuitOnComplete = false;
    };
}
//Modify End
