//Modify Begin:2026-08-21 by Hui
#include "RendererDiagnosticsCommands.h"

#include "RendererDiagnosticsCapture.h"

#include <Framework/Diagnostics/AutomationRunner.h>
#include <Framework/Diagnostics/DiagnosticsSession.h>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

int RendererDiagnosticsTool::SelfTestCommand()
{
    const std::filesystem::path output = std::filesystem::temp_directory_path() /
        ("RendererDiagnosticsSelfTest-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    FrameworkDiagnostics::DiagnosticsSession session;
    FrameworkDiagnostics::DiagnosticsSessionOptions options;
    options.ApplicationName = "RendererDiagnosticsSelfTest";
    options.SessionName = "selftest";
    options.OutputDirectory = output;
    options.MaxEventCount = 1024u;
    if (!session.Begin(std::move(options))) throw std::runtime_error("DiagnosticsSession::Begin failed in selftest.");
    session.AddMetadata("env.RAYTRACING_DEMO_AUTOTEST", "stress");
    session.AddMetadata("executable_path", "RaytracingDemo.exe");
    session.Record("selftest.failure", "retained_error", DiagnosticTelemetrySeverity::Error, {
        { "message", std::string("Critical event retention probe.") },
    });
    for (uint64_t index = 0; index < 1100u; ++index)
    {
        session.Record("selftest.event", "bounded", DiagnosticTelemetrySeverity::Info, { { "index", index } });
    }
    session.Record("descriptor.binding", "escaped", DiagnosticTelemetrySeverity::Info, {
        { "text", std::string("quote=\" slash=\\ newline=\n") },
    });

    uint64_t controlValue = 0;
    int completionCode = -1;
    FrameworkDiagnostics::AutomationRunner successRunner;
    successRunner.RegisterControl("selftest.value", [&controlValue](const DiagnosticTelemetryValue& value, std::string& error)
    {
        const uint64_t* typedValue = std::get_if<uint64_t>(&value);
        if (typedValue == nullptr) { error = "Expected uint64."; return false; }
        controlValue = *typedValue;
        return true;
    });
    successRunner.RegisterObservation("selftest.value", [&controlValue](std::string&)
    {
        return std::optional<DiagnosticTelemetryValue>(controlValue);
    });
    FrameworkDiagnostics::AutomationScenario scenario;
    scenario.Name = "selftest-success";
    FrameworkDiagnostics::AutomationStep setStep;
    setStep.Kind = FrameworkDiagnostics::AutomationStepKind::SetControl;
    setStep.Name = "set";
    setStep.Target = "selftest.value";
    setStep.Value = uint64_t{ 7 };
    setStep.FrameCount = 1;
    FrameworkDiagnostics::AutomationStep assertStep;
    assertStep.Kind = FrameworkDiagnostics::AutomationStepKind::AssertObservation;
    assertStep.Name = "assert";
    assertStep.Target = "selftest.value";
    assertStep.Value = uint64_t{ 7 };
    scenario.Steps = { setStep, assertStep };
    FrameworkDiagnostics::AutomationRunnerOptions runnerOptions;
    runnerOptions.FinalizeDiagnosticsOnCompletion = false;
    if (!successRunner.Start(std::move(scenario), &session, runnerOptions,
        [&completionCode](const int code) { completionCode = code; }))
    {
        throw std::runtime_error("AutomationRunner::Start failed in selftest.");
    }
    for (uint64_t frame = 0; frame < 5u; ++frame) successRunner.Tick(frame, static_cast<double>(frame));
    if (completionCode != 0 || controlValue != 7u)
    {
        throw std::runtime_error("AutomationRunner success path failed in selftest.");
    }

    int invalidCode = -1;
    FrameworkDiagnostics::AutomationRunner invalidRunner;
    FrameworkDiagnostics::AutomationScenario invalidScenario;
    invalidScenario.Name = "selftest-invalid";
    FrameworkDiagnostics::AutomationStep invalidStep;
    invalidStep.Kind = FrameworkDiagnostics::AutomationStepKind::AssertObservation;
    invalidStep.Name = "missing";
    invalidStep.Target = "missing";
    invalidStep.Value = true;
    invalidScenario.Steps = { invalidStep };
    invalidRunner.Start(std::move(invalidScenario), nullptr, {},
        [&invalidCode](const int code) { invalidCode = code; });
    invalidRunner.Tick(0u, 0.0);
    if (invalidCode != static_cast<int>(FrameworkDiagnostics::AutomationExitCode::InvalidScenario))
    {
        throw std::runtime_error("AutomationRunner invalid-scenario exit code failed in selftest.");
    }

    int timeoutCode = -1;
    FrameworkDiagnostics::AutomationRunner timeoutRunner;
    timeoutRunner.RegisterObservation("pending", [](std::string&) -> std::optional<DiagnosticTelemetryValue>
    {
        return std::nullopt;
    });
    FrameworkDiagnostics::AutomationScenario timeoutScenario;
    timeoutScenario.Name = "selftest-timeout";
    FrameworkDiagnostics::AutomationStep timeoutStep;
    timeoutStep.Kind = FrameworkDiagnostics::AutomationStepKind::WaitObservation;
    timeoutStep.Name = "timeout";
    timeoutStep.Target = "pending";
    timeoutStep.Value = true;
    timeoutStep.TimeoutFrames = 1u;
    timeoutStep.TimeoutSeconds = 0.0;
    timeoutScenario.Steps = { timeoutStep };
    timeoutRunner.Start(std::move(timeoutScenario), nullptr, {},
        [&timeoutCode](const int code) { timeoutCode = code; });
    timeoutRunner.Tick(0u, 0.0);
    timeoutRunner.Tick(2u, 0.0);
    if (timeoutCode != static_cast<int>(FrameworkDiagnostics::AutomationExitCode::Timeout))
    {
        throw std::runtime_error("AutomationRunner timeout exit code failed in selftest.");
    }

    if (!session.Finalize(FrameworkDiagnostics::SessionStatus::Passed, "Selftest completed."))
    {
        throw std::runtime_error("DiagnosticsSession::Finalize failed in selftest.");
    }
    const Capture capture = LoadCapture(output);
    const bool bounded = capture.Events.size() == 1024u;
    const bool dropped = ToUint64(Find(capture.Manifest.AsObject(), "dropped_event_count")) > 0u;
    const bool reproduction = std::filesystem::is_regular_file(output / "reproduction.json");
    const bool descriptors = std::filesystem::is_regular_file(output / "descriptors.json");
    const bool retainedCriticalEvent = std::ranges::any_of(capture.Events, [](const Event& event)
    {
        return event.Name == "retained_error" && event.Severity == "error";
    });
    const bool result = bounded && dropped && retainedCriticalEvent && reproduction && descriptors;
    std::error_code cleanupError;
    std::filesystem::remove_all(output, cleanupError);
    std::cout << "{\"schema_version\":1,\"selftest\":" << (result ? "\"pass\"" : "\"fail\"")
              << ",\"bounded_buffer\":" << (bounded ? "true" : "false")
              << ",\"drop_accounting\":" << (dropped ? "true" : "false")
              << ",\"critical_event_retention\":" << (retainedCriticalEvent ? "true" : "false")
              << ",\"artifacts\":" << (reproduction && descriptors ? "true" : "false")
              << ",\"cleanup_error\":";
    if (cleanupError) WriteJsonString(std::cout, cleanupError.message());
    else std::cout << "null";
    std::cout << "}\n";
    return result ? 0 : 1;
}
//Modify End
