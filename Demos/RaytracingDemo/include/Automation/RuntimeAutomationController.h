#pragma once

//Modify Begin:2026-08-11 by Hui
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
        std::string Name;
    };

    struct TestSuites
    {
        std::vector<Step> Core;
        std::vector<Step> Stress;
        std::vector<Step> Matrix;
        std::vector<Step> ReSTIRGIProfile;
        std::vector<Step> ReSTIRGIVariants;
        std::vector<Step> ReSTIRDIVariants;
    };

    class RuntimeAutomationController final
    {
    public:
        using ActionHandler = std::function<void(uint32_t action, uint32_t value)>;
        using CompletionHandler = std::function<void()>;

        void Initialize(const TestSuites& testSuites);
        void Update(double totalTime, const ActionHandler& actionHandler, const CompletionHandler& completionHandler);
        void AppendDiagnosticLog(const std::string& message) const;

    private:
        void AppendLog(const std::string& message) const;

        std::vector<Step> m_Steps;
        std::filesystem::path m_LogPath;
        size_t m_StepIndex = 0;
        double m_LastStepTime = 0.0;
        double m_StepIntervalSeconds = 1.0;
        bool m_Enabled = false;
        bool m_QuitOnComplete = false;
        bool m_Completed = false;
    };
}
//Modify End
