//Modify Begin:2026-08-11 by Hui
#include <Automation/RuntimeAutomationController.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
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

void DemoAutomation::RuntimeAutomationController::Initialize(const TestSuites& testSuites)
{
    m_Steps.clear();
    m_LogPath.clear();
    m_StepIndex = 0;
    m_StepIntervalSeconds = 1.0;
    m_Enabled = false;
    m_QuitOnComplete = false;
    m_Completed = false;

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

    if (mode == "core")
    {
        m_Steps = testSuites.Core;
    }
    else if (mode == "stress")
    {
        m_Steps = testSuites.Stress;
    }
    else if (mode == "matrix")
    {
        m_Steps = testSuites.Matrix;
        if (stepMilliseconds.empty())
        {
            m_StepIntervalSeconds = 0.25;
        }

        const size_t startCase = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_START_CASE");
        if (startCase > 0)
        {
            const size_t firstStep = std::min(startCase - 1, m_Steps.size());
            m_Steps.erase(m_Steps.begin(), m_Steps.begin() + static_cast<std::ptrdiff_t>(firstStep));
        }

        const size_t maxCases = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_MAX_CASES");
        if (maxCases > 0 && m_Steps.size() > maxCases)
        {
            m_Steps.resize(maxCases);
        }
    }
    else if (mode == "restirgi-profile")
    {
        m_Steps = testSuites.ReSTIRGIProfile;
    }
    else if (mode == "restirgi-variants")
    {
        m_Steps = testSuites.ReSTIRGIVariants;
    }
    else if (mode == "restirdi-variants")
    {
        m_Steps = testSuites.ReSTIRDIVariants;
        if (stepMilliseconds.empty())
        {
            m_StepIntervalSeconds = 0.5;
        }

        const size_t startCase = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_START_CASE");
        if (startCase > 0)
        {
            const size_t firstStep = std::min(startCase - 1, m_Steps.size());
            m_Steps.erase(m_Steps.begin(), m_Steps.begin() + static_cast<std::ptrdiff_t>(firstStep));
        }

        const size_t maxCases = GetEnvironmentSize("RAYTRACING_DEMO_AUTOTEST_MAX_CASES");
        if (maxCases > 0 && m_Steps.size() > maxCases)
        {
            m_Steps.resize(maxCases);
        }
    }
    else if (mode == "visual")
    {
        m_Steps = testSuites.Visual;
    }
    else
    {
        throw std::runtime_error(
            "RAYTRACING_DEMO_AUTOTEST must be 'core', 'stress', 'matrix', 'restirgi-profile', 'restirgi-variants', 'restirdi-variants', or 'visual'.");
    }

    m_Enabled = !m_Steps.empty();
    m_LastStepTime = -m_StepIntervalSeconds;
    AppendLog("Mode=" + mode + ", step interval=" + std::to_string(m_StepIntervalSeconds) + " seconds.");
}

void DemoAutomation::RuntimeAutomationController::Update(
    const double totalTime,
    const ActionHandler& actionHandler,
    const CompletionHandler& completionHandler)
{
    if (!m_Enabled || m_Completed || totalTime - m_LastStepTime < m_StepIntervalSeconds)
    {
        return;
    }

    if (m_StepIndex >= m_Steps.size())
    {
        m_Completed = true;
        AppendLog("Runtime automation completed.");
        if (m_QuitOnComplete)
        {
            completionHandler();
        }
        return;
    }

    const Step& step = m_Steps[m_StepIndex++];
    AppendLog("Begin " + step.Name + ".");
    actionHandler(step.Action, step.Value);
    m_LastStepTime = totalTime;
    AppendLog("Applied " + step.Name + ".");
}

void DemoAutomation::RuntimeAutomationController::AppendDiagnosticLog(const std::string& message) const
{
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
