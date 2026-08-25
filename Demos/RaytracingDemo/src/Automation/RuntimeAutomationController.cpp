//Modify Begin:2026-08-21 by Hui
#include <Automation/RuntimeAutomationController.h>

#include <Framework/Diagnostics/DiagnosticsSession.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace
{
    std::string GetEnvironmentVariable(const char* variableName)
    {
        char* value = nullptr;
        size_t valueLength = 0;
        _dupenv_s(&value, &valueLength, variableName);
        const std::string result = value != nullptr ? value : "";
        std::free(value);
        return result;
    }

    size_t GetEnvironmentSize(const char* variableName)
    {
        const std::string value = GetEnvironmentVariable(variableName);
        if (value.empty())
        {
            return 0;
        }

        char* parseEnd = nullptr;
        const unsigned long long parsedValue = std::strtoull(value.c_str(), &parseEnd, 10);
        return parseEnd != value.c_str() && *parseEnd == '\0'
            ? static_cast<size_t>(parsedValue)
            : 0;
    }
}

void DemoAutomation::RuntimeAutomationController::Initialize(
    const TestSuites& testSuites,
    FrameworkDiagnostics::DiagnosticsSession* diagnostics,
    const ActionHandler& actionHandler,
    const CompletionHandler& completionHandler)
{
    if (m_Runner.IsRunning())
    {
        m_Runner.Cancel("Automation controller was reinitialized.");
    }
    m_Runner.ClearRegistry();
    m_Diagnostics = diagnostics;
    m_LogPath.clear();
    m_StepIntervalSeconds = 1.0;
    m_QuitOnComplete = false;

    const std::string mode = GetEnvironmentVariable("RAYTRACING_DEMO_AUTOTEST");
    if (mode.empty() || mode == "0" || mode == "off")
    {
        return;
    }

    m_LogPath = std::filesystem::current_path() / "Saved" / "RuntimeAutomation.log";
    std::filesystem::create_directories(m_LogPath.parent_path());
    std::ofstream(m_LogPath, std::ios::trunc) << "Runtime automation started." << '\n';

    const std::string stepMilliseconds = GetEnvironmentVariable("RAYTRACING_DEMO_AUTOTEST_STEP_MS");
    if (!stepMilliseconds.empty())
    {
        const double milliseconds = std::strtod(stepMilliseconds.c_str(), nullptr);
        if (milliseconds > 0.0)
        {
            m_StepIntervalSeconds = milliseconds / 1000.0;
        }
    }

    const std::string quitOnComplete = GetEnvironmentVariable("RAYTRACING_DEMO_AUTOTEST_QUIT");
    m_QuitOnComplete = !quitOnComplete.empty() && quitOnComplete != "0";

    std::vector<Step> steps;

    if (mode == "core")
    {
        steps = testSuites.Core;
    }
    else if (mode == "stress")
    {
        steps = testSuites.Stress;
    }
    else if (mode == "copy")
    {
        steps = testSuites.CopyQueue;
    }
    else if (mode == "rtas")
    {
        steps = testSuites.Rtas;
    }
    else if (mode == "matrix")
    {
        steps = testSuites.Matrix;
        if (stepMilliseconds.empty())
        {
            m_StepIntervalSeconds = 0.25;
        }

        const size_t startCase = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_START_CASE");
        if (startCase > 0)
        {
            const size_t firstStep = std::min(startCase - 1, steps.size());
            steps.erase(steps.begin(), steps.begin() + static_cast<std::ptrdiff_t>(firstStep));
        }

        const size_t maxCases = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_MAX_CASES");
        if (maxCases > 0 && steps.size() > maxCases)
        {
            steps.resize(maxCases);
        }
    }
    else if (mode == "restirgi-profile")
    {
        steps = testSuites.ReSTIRGIProfile;
    }
    else if (mode == "restirgi-variants")
    {
        steps = testSuites.ReSTIRGIVariants;
    }
    else if (mode == "restirdi-variants")
    {
        steps = testSuites.ReSTIRDIVariants;
        if (stepMilliseconds.empty())
        {
            m_StepIntervalSeconds = 0.5;
        }

        const size_t startCase = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_START_CASE");
        if (startCase > 0)
        {
            const size_t firstStep = std::min(startCase - 1, steps.size());
            steps.erase(steps.begin(), steps.begin() + static_cast<std::ptrdiff_t>(firstStep));
        }

        const size_t maxCases = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_MAX_CASES");
        if (maxCases > 0 && steps.size() > maxCases)
        {
            steps.resize(maxCases);
        }
    }
    else if (mode == "visual")
    {
        steps = testSuites.Visual;
    }
    else
    {
        throw std::runtime_error(
            "RAYTRACING_DEMO_AUTOTEST must be 'core', 'stress', 'copy', 'rtas', 'matrix', 'restirgi-profile', 'restirgi-variants', 'restirdi-variants', or 'visual'.");
    }

    AppendLog("Mode=" + mode + ", step interval=" + std::to_string(m_StepIntervalSeconds) + " seconds.");

    std::map<std::string, uint32_t, std::less<>> registeredActions;
    for (const Step& step : steps)
    {
        if (step.Kind != FrameworkDiagnostics::AutomationStepKind::SetControl)
        {
            continue;
        }
        const auto [existing, inserted] = registeredActions.emplace(step.Control, step.Action);
        if (!inserted && existing->second != step.Action)
        {
            throw std::logic_error("Automation control name maps to multiple actions: " + step.Control + ".");
        }
        if (!inserted)
        {
            continue;
        }
        m_Runner.RegisterControl(step.Control, [action = step.Action, actionHandler](
            const DiagnosticTelemetryValue& value,
            std::string& error)
        {
            const uint64_t* unsignedValue = std::get_if<uint64_t>(&value);
            if (unsignedValue == nullptr || *unsignedValue > (std::numeric_limits<uint32_t>::max)())
            {
                error = "Demo automation controls require an unsigned 32-bit value.";
                return false;
            }
            actionHandler(action, static_cast<uint32_t>(*unsignedValue));
            return true;
        });
    }

    FrameworkDiagnostics::AutomationScenario scenario;
    scenario.Name = mode;
    scenario.Steps.reserve(steps.size());
    for (const Step& step : steps)
    {
        FrameworkDiagnostics::AutomationStep runnerStep;
        runnerStep.Kind = step.Kind;
        runnerStep.Name = step.Name;
        runnerStep.Target = step.Control;
        runnerStep.Value = static_cast<uint64_t>(step.Value);
        runnerStep.FrameCount = 1;
        runnerStep.MinimumSeconds = step.Kind == FrameworkDiagnostics::AutomationStepKind::WaitFrames
            ? m_StepIntervalSeconds
            : 0.0;
        scenario.Steps.push_back(std::move(runnerStep));
    }

    FrameworkDiagnostics::AutomationRunnerOptions runnerOptions;
    runnerOptions.DefaultSettleFrames = 1;
    runnerOptions.DefaultSettleSeconds = m_StepIntervalSeconds;
    runnerOptions.FinalizeDiagnosticsOnCompletion = true;
    if (!m_Runner.Start(
        std::move(scenario),
        diagnostics,
        runnerOptions,
        [this, completionHandler](const int exitCode)
        {
            if (m_QuitOnComplete || exitCode != 0)
            {
                completionHandler(exitCode);
            }
        },
        [this](const std::string_view message)
        {
            AppendLog(std::string(message));
        }))
    {
        throw std::runtime_error("Failed to start Framework automation runner.");
    }
}

void DemoAutomation::RuntimeAutomationController::Update(
    const uint64_t frameIndex,
    const double totalTime)
{
    m_Runner.Tick(frameIndex, totalTime);
}

void DemoAutomation::RuntimeAutomationController::AppendDiagnosticLog(const std::string& message) const
{
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record(
            "automation.diagnostic",
            "message",
            DiagnosticTelemetrySeverity::Info,
            { { "message", message } });
    }
    AppendLog(message);
}

void DemoAutomation::RuntimeAutomationController::AppendLog(const std::string& message) const
{
    if (m_LogPath.empty())
    {
        return;
    }

    std::ofstream log(m_LogPath, std::ios::app);
    if (log.is_open())
    {
        log << message << '\n';
    }
}
//Modify End
