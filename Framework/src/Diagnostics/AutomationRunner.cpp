//Modify Begin:2026-08-21 by Hui
#include <Framework/Diagnostics/AutomationRunner.h>

#include <Framework/Diagnostics/DiagnosticsSession.h>

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace
{
    bool IsNumeric(const DiagnosticTelemetryValue& value)
    {
        return !std::holds_alternative<std::string>(value);
    }

    long double ToNumber(const DiagnosticTelemetryValue& value)
    {
        return std::visit([](const auto& typedValue) -> long double
        {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, std::string>)
            {
                throw std::invalid_argument("String diagnostic values are not numeric.");
            }
            else
            {
                return static_cast<long double>(typedValue);
            }
        }, value);
    }

    bool CompareValues(
        const DiagnosticTelemetryValue& actual,
        const DiagnosticTelemetryValue& expected,
        const FrameworkDiagnostics::AutomationComparison comparison,
        const double tolerance)
    {
        if (IsNumeric(actual) && IsNumeric(expected))
        {
            const long double actualNumber = ToNumber(actual);
            const long double expectedNumber = ToNumber(expected);
            const long double difference = std::abs(actualNumber - expectedNumber);
            switch (comparison)
            {
            case FrameworkDiagnostics::AutomationComparison::Equal:
                return difference <= tolerance;
            case FrameworkDiagnostics::AutomationComparison::NotEqual:
                return difference > tolerance;
            case FrameworkDiagnostics::AutomationComparison::Less:
                return actualNumber < expectedNumber;
            case FrameworkDiagnostics::AutomationComparison::LessOrEqual:
                return actualNumber <= expectedNumber;
            case FrameworkDiagnostics::AutomationComparison::Greater:
                return actualNumber > expectedNumber;
            case FrameworkDiagnostics::AutomationComparison::GreaterOrEqual:
                return actualNumber >= expectedNumber;
            default:
                return false;
            }
        }

        if (!std::holds_alternative<std::string>(actual) || !std::holds_alternative<std::string>(expected))
        {
            return false;
        }
        const std::string& actualString = std::get<std::string>(actual);
        const std::string& expectedString = std::get<std::string>(expected);
        return comparison == FrameworkDiagnostics::AutomationComparison::Equal
            ? actualString == expectedString
            : comparison == FrameworkDiagnostics::AutomationComparison::NotEqual && actualString != expectedString;
    }
}

bool FrameworkDiagnostics::AutomationRunner::RegisterControl(std::string name, ControlSetter setter)
{
    if (name.empty() || !setter)
    {
        return false;
    }
    return m_Controls.emplace(std::move(name), std::move(setter)).second;
}

bool FrameworkDiagnostics::AutomationRunner::RegisterObservation(std::string name, ObservationGetter getter)
{
    if (name.empty() || !getter)
    {
        return false;
    }
    return m_Observations.emplace(std::move(name), std::move(getter)).second;
}

void FrameworkDiagnostics::AutomationRunner::ClearRegistry()
{
    if (m_Running)
    {
        throw std::logic_error("Cannot clear the automation registry while a scenario is running.");
    }
    m_Controls.clear();
    m_Observations.clear();
}

bool FrameworkDiagnostics::AutomationRunner::Start(
    AutomationScenario scenario,
    DiagnosticsSession* diagnostics,
    AutomationRunnerOptions options,
    CompletionHandler completionHandler,
    LogHandler logHandler)
{
    if (m_Running || scenario.Name.empty() || scenario.Steps.empty())
    {
        return false;
    }
    m_Scenario = std::move(scenario);
    m_Diagnostics = diagnostics;
    m_Options = options;
    m_CompletionHandler = std::move(completionHandler);
    m_LogHandler = std::move(logHandler);
    m_StepIndex = 0;
    m_StepStarted = false;
    m_Running = true;
    m_Completed = false;
    m_Failed = false;
    m_FailureMessage.clear();

    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record("automation.scenario", "begin", DiagnosticTelemetrySeverity::Info, {
            { "scenario", m_Scenario.Name },
            { "step_count", static_cast<uint64_t>(m_Scenario.Steps.size()) },
        });
        for (const auto& [name, setter] : m_Controls)
        {
            (void)setter;
            m_Diagnostics->Record("automation.registry.control", "registered", DiagnosticTelemetrySeverity::Info, {
                { "name", name },
            });
        }
        for (const auto& [name, getter] : m_Observations)
        {
            (void)getter;
            m_Diagnostics->Record("automation.registry.observation", "registered", DiagnosticTelemetrySeverity::Info, {
                { "name", name },
            });
        }
    }
    Log("Runtime automation started. Scenario=" + m_Scenario.Name + ".");
    return true;
}

void FrameworkDiagnostics::AutomationRunner::Tick(const uint64_t frameIndex, const double totalSeconds)
{
    if (!m_Running || m_Completed || m_Failed)
    {
        return;
    }
    if (m_StepIndex >= m_Scenario.Steps.size())
    {
        CompleteScenario();
        return;
    }

    const AutomationStep& step = m_Scenario.Steps[m_StepIndex];
    if (!m_StepStarted)
    {
        BeginStep(step, frameIndex, totalSeconds);
        if (!m_Running)
        {
            return;
        }
    }

    if (HasStepTimedOut(step, frameIndex, totalSeconds))
    {
        Fail(
            AutomationExitCode::Timeout,
            "Automation step timed out: " + step.Name + ".",
            &step);
        return;
    }

    switch (step.Kind)
    {
    case AutomationStepKind::SetControl:
        if (frameIndex - m_StepStartFrame >= (std::max)(step.FrameCount, m_Options.DefaultSettleFrames) &&
            totalSeconds - m_StepStartSeconds >= (std::max)(step.MinimumSeconds, m_Options.DefaultSettleSeconds))
        {
            CompleteStep(step);
        }
        break;
    case AutomationStepKind::WaitFrames:
        if (frameIndex - m_StepStartFrame >= step.FrameCount &&
            totalSeconds - m_StepStartSeconds >= step.MinimumSeconds)
        {
            CompleteStep(step);
        }
        break;
    case AutomationStepKind::WaitObservation:
        if (EvaluateObservation(step, false))
        {
            CompleteStep(step);
        }
        break;
    case AutomationStepKind::AssertObservation:
        if (EvaluateObservation(step, true))
        {
            CompleteStep(step);
        }
        break;
    case AutomationStepKind::FlushCapture:
        if (m_Diagnostics != nullptr && !m_Diagnostics->Flush())
        {
            Fail(AutomationExitCode::AssertionFailure, "Diagnostics capture flush failed.", &step);
            return;
        }
        CompleteStep(step);
        break;
    default:
        Fail(AutomationExitCode::InvalidScenario, "Automation step has an unknown kind.", &step);
        break;
    }
}

void FrameworkDiagnostics::AutomationRunner::BeginStep(
    const AutomationStep& step,
    const uint64_t frameIndex,
    const double totalSeconds)
{
    m_StepStarted = true;
    m_StepStartFrame = frameIndex;
    m_StepStartSeconds = totalSeconds;
    Log("Begin " + step.Name + ".");
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record("automation.step", "begin", DiagnosticTelemetrySeverity::Info, {
            { "scenario", m_Scenario.Name },
            { "step_index", static_cast<uint64_t>(m_StepIndex) },
            { "step", step.Name },
            { "target", step.Target },
        });
    }
    if (step.Kind != AutomationStepKind::SetControl)
    {
        return;
    }

    const auto control = m_Controls.find(step.Target);
    if (control == m_Controls.end())
    {
        Fail(AutomationExitCode::InvalidScenario, "Unregistered automation control: " + step.Target + ".", &step);
        return;
    }
    std::string error;
    try
    {
        if (!control->second(step.Value, error))
        {
            Fail(
                AutomationExitCode::ControlFailure,
                error.empty() ? "Automation control rejected its value: " + step.Target + "." : error,
                &step);
            return;
        }
    }
    catch (const std::exception& exception)
    {
        Fail(
            AutomationExitCode::ControlFailure,
            "Automation control '" + step.Target + "' threw: " + exception.what(),
            &step);
        return;
    }
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record("automation.control", "set", DiagnosticTelemetrySeverity::Info, {
            { "control", step.Target },
            { "value", ValueToString(step.Value) },
            { "step_index", static_cast<uint64_t>(m_StepIndex) },
        });
    }
}

bool FrameworkDiagnostics::AutomationRunner::EvaluateObservation(
    const AutomationStep& step,
    const bool failWhenFalse)
{
    const auto observation = m_Observations.find(step.Target);
    if (observation == m_Observations.end())
    {
        Fail(AutomationExitCode::InvalidScenario, "Unregistered automation observation: " + step.Target + ".", &step);
        return false;
    }

    std::string error;
    std::optional<DiagnosticTelemetryValue> actual;
    try
    {
        actual = observation->second(error);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
    }
    if (!actual.has_value())
    {
        if (failWhenFalse)
        {
            Fail(
                AutomationExitCode::ObservationFailure,
                error.empty() ? "Automation observation is unavailable: " + step.Target + "." : error,
                &step);
        }
        return false;
    }

    const bool passed = CompareValues(actual.value(), step.Value, step.Comparison, step.Tolerance);
    if (passed)
    {
        if (m_Diagnostics != nullptr)
        {
            m_Diagnostics->RecordAssertion(
                "automation." + step.Name,
                AssertionResult::Passed,
                "Observation matched the expected value.",
                {
                    { "observation", step.Target },
                    { "expected", ValueToString(step.Value) },
                    { "actual", ValueToString(actual.value()) },
                });
        }
        return true;
    }
    if (failWhenFalse)
    {
        Fail(
            AutomationExitCode::AssertionFailure,
            "Automation assertion failed for '" + step.Target + "': expected=" +
            ValueToString(step.Value) + ", actual=" + ValueToString(actual.value()) + ".",
            &step);
    }
    return false;
}

bool FrameworkDiagnostics::AutomationRunner::HasStepTimedOut(
    const AutomationStep& step,
    const uint64_t frameIndex,
    const double totalSeconds) const
{
    const bool frameTimeout = step.TimeoutFrames != 0 && frameIndex - m_StepStartFrame > step.TimeoutFrames;
    const bool wallTimeout = step.TimeoutSeconds > 0.0 && totalSeconds - m_StepStartSeconds > step.TimeoutSeconds;
    return frameTimeout || wallTimeout;
}

void FrameworkDiagnostics::AutomationRunner::CompleteStep(const AutomationStep& step)
{
    Log("Applied " + step.Name + ".");
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record("automation.step", "complete", DiagnosticTelemetrySeverity::Info, {
            { "scenario", m_Scenario.Name },
            { "step_index", static_cast<uint64_t>(m_StepIndex) },
            { "step", step.Name },
        });
    }
    ++m_StepIndex;
    m_StepStarted = false;
}

void FrameworkDiagnostics::AutomationRunner::CompleteScenario()
{
    m_Running = false;
    m_Completed = true;
    Log("Runtime automation completed.");
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->Record("automation.scenario", "complete", DiagnosticTelemetrySeverity::Info, {
            { "scenario", m_Scenario.Name },
            { "step_count", static_cast<uint64_t>(m_Scenario.Steps.size()) },
        });
        if (m_Options.FinalizeDiagnosticsOnCompletion)
        {
            m_Diagnostics->Finalize(SessionStatus::Passed, "Automation scenario completed successfully.");
        }
    }
    if (m_CompletionHandler)
    {
        m_CompletionHandler(static_cast<int>(AutomationExitCode::Success));
    }
}

void FrameworkDiagnostics::AutomationRunner::Fail(
    const AutomationExitCode exitCode,
    std::string message,
    const AutomationStep* step)
{
    m_Running = false;
    m_Failed = true;
    m_FailureMessage = std::move(message);
    Log("Runtime automation failed: " + m_FailureMessage);
    if (m_Diagnostics != nullptr)
    {
        std::vector<DiagnosticTelemetryField> fields = {
            { "scenario", m_Scenario.Name },
            { "step_index", static_cast<uint64_t>(m_StepIndex) },
            { "exit_code", static_cast<int64_t>(exitCode) },
        };
        if (step != nullptr)
        {
            fields.push_back({ "step", step->Name });
            fields.push_back({ "target", step->Target });
        }
        m_Diagnostics->RecordAssertion(
            "automation.scenario",
            AssertionResult::Failed,
            m_FailureMessage,
            std::move(fields));
        if (m_Options.FinalizeDiagnosticsOnCompletion)
        {
            m_Diagnostics->Finalize(SessionStatus::Failed, m_FailureMessage);
        }
        else
        {
            m_Diagnostics->Flush();
        }
    }
    if (m_CompletionHandler)
    {
        m_CompletionHandler(static_cast<int>(exitCode));
    }
}

void FrameworkDiagnostics::AutomationRunner::Cancel(std::string reason)
{
    if (!m_Running)
    {
        return;
    }
    m_Running = false;
    m_Failed = true;
    m_FailureMessage = reason.empty() ? "Automation scenario was cancelled." : std::move(reason);
    Log("Runtime automation cancelled: " + m_FailureMessage);
    if (m_Diagnostics != nullptr)
    {
        m_Diagnostics->RecordAssertion(
            "automation.cancelled",
            AssertionResult::Unknown,
            m_FailureMessage,
            { { "scenario", m_Scenario.Name } });
        if (m_Options.FinalizeDiagnosticsOnCompletion)
        {
            m_Diagnostics->Finalize(SessionStatus::Aborted, m_FailureMessage);
        }
    }
}

void FrameworkDiagnostics::AutomationRunner::Log(std::string message) const
{
    if (m_LogHandler)
    {
        m_LogHandler(message);
    }
}

std::string FrameworkDiagnostics::AutomationRunner::ValueToString(const DiagnosticTelemetryValue& value)
{
    return std::visit([](const auto& typedValue)
    {
        using ValueType = std::decay_t<decltype(typedValue)>;
        if constexpr (std::is_same_v<ValueType, bool>)
        {
            return std::string(typedValue ? "true" : "false");
        }
        else if constexpr (std::is_same_v<ValueType, std::string>)
        {
            return typedValue;
        }
        else
        {
            std::ostringstream output;
            output << typedValue;
            return output.str();
        }
    }, value);
}
//Modify End
