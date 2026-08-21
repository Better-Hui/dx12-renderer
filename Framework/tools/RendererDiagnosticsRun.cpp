//Modify Begin:2026-08-21 by Hui
#include "RendererDiagnosticsCommands.h"

#include "RendererDiagnosticsCapture.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace RendererDiagnosticsTool;

    struct LaunchResult
    {
        DWORD ExitCode = 0;
        bool TimedOut = false;
    };

    LaunchResult LaunchProcess(
        const std::filesystem::path& executable,
        const std::map<std::string, std::string, std::less<>>& environment,
        const bool useStreamlineInterposer,
        const uint64_t timeoutSeconds)
    {
        if (!std::filesystem::is_regular_file(executable))
        {
            throw std::runtime_error("Executable does not exist: " + executable.string());
        }
        for (const auto& [name, value] : environment)
        {
            const std::wstring wideName = Utf8ToWide(name);
            const std::wstring wideValue = Utf8ToWide(value);
            if (!SetEnvironmentVariableW(wideName.c_str(), wideValue.c_str()))
            {
                throw std::runtime_error("Failed to set child environment variable: " + name);
            }
        }
        std::wstring commandLine = L"\"" + std::filesystem::absolute(executable).wstring() + L"\"";
        if (useStreamlineInterposer) commandLine += L" --streamline-interposer";
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::wstring workingDirectory = std::filesystem::absolute(executable).parent_path().wstring();
        if (!CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            workingDirectory.c_str(),
            &startupInfo,
            &processInfo))
        {
            throw std::runtime_error("CreateProcessW failed with error " + std::to_string(GetLastError()) + ".");
        }
        CloseHandle(processInfo.hThread);
        const DWORD maximumDword = (std::numeric_limits<DWORD>::max)();
        const DWORD timeoutMilliseconds = timeoutSeconds >= maximumDword / 1000u
            ? maximumDword - 1u
            : static_cast<DWORD>(timeoutSeconds * 1000u);
        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMilliseconds);
        LaunchResult result;
        if (waitResult == WAIT_TIMEOUT)
        {
            result.TimedOut = true;
            TerminateProcess(processInfo.hProcess, 23u);
            WaitForSingleObject(processInfo.hProcess, 5000u);
            result.ExitCode = 23u;
        }
        else if (waitResult != WAIT_OBJECT_0)
        {
            CloseHandle(processInfo.hProcess);
            throw std::runtime_error("WaitForSingleObject failed while running renderer automation.");
        }
        else if (!GetExitCodeProcess(processInfo.hProcess, &result.ExitCode))
        {
            CloseHandle(processInfo.hProcess);
            throw std::runtime_error("GetExitCodeProcess failed for renderer automation.");
        }
        CloseHandle(processInfo.hProcess);
        return result;
    }

    std::filesystem::path DefaultOutputPath(const std::string_view session)
    {
        return std::filesystem::current_path() / "Saved" / "Diagnostics" /
            (std::string(session) + "-" + std::to_string(GetCurrentProcessId()));
    }
}

int RendererDiagnosticsTool::RunCommand(const std::vector<std::string>& arguments)
{
    const auto executableOption = GetOption(arguments, "--exe");
    const auto scenarioOption = GetOption(arguments, "--scenario");
    if (!executableOption.has_value() || !scenarioOption.has_value())
    {
        throw std::runtime_error("run requires --exe and --scenario.");
    }
    const std::filesystem::path output = GetOption(arguments, "--output").has_value()
        ? std::filesystem::u8path(*GetOption(arguments, "--output"))
        : DefaultOutputPath(*scenarioOption);
    const uint64_t maximumEventCount = GetUnsignedOption(arguments, "--max-events", 262144u);
    if (maximumEventCount == 0u) throw std::runtime_error("--max-events must be greater than zero.");
    std::map<std::string, std::string, std::less<>> environment = {
        { "RAYTRACING_DEMO_AUTOTEST", *scenarioOption },
        { "RAYTRACING_DEMO_AUTOTEST_QUIT", "1" },
        { "RENDERER_DIAGNOSTICS", "1" },
        { "RENDERER_DIAGNOSTICS_SESSION", *scenarioOption },
        { "RENDERER_DIAGNOSTICS_OUTPUT", std::filesystem::absolute(output).string() },
        { "RENDERER_DIAGNOSTICS_MAX_EVENTS", std::to_string(maximumEventCount) },
    };
    for (size_t index = 0; index + 1u < arguments.size(); ++index)
    {
        if (arguments[index] != "--set") continue;
        const std::string& assignment = arguments[index + 1u];
        const size_t separator = assignment.find('=');
        if (separator == std::string::npos || !assignment.starts_with("RAYTRACING_DEMO_"))
        {
            throw std::runtime_error("--set only accepts RAYTRACING_DEMO_NAME=value.");
        }
        environment[assignment.substr(0, separator)] = assignment.substr(separator + 1u);
    }
    const LaunchResult result = LaunchProcess(
        std::filesystem::u8path(*executableOption),
        environment,
        HasFlag(arguments, "--streamline-interposer"),
        GetUnsignedOption(arguments, "--timeout-seconds", 120u));
    const bool captureAvailable = std::filesystem::is_regular_file(output / "manifest.json");
    std::cout << "{\"schema_version\":1,\"command\":\"run\",\"output\":";
    WriteJsonString(std::cout, std::filesystem::absolute(output).string());
    std::cout << ",\"exit_code\":" << result.ExitCode
              << ",\"timed_out\":" << (result.TimedOut ? "true" : "false")
              << ",\"capture_available\":" << (captureAvailable ? "true" : "false")
              << ",\"max_event_count\":" << maximumEventCount << "}\n";
    if (result.ExitCode == 0u && !captureAvailable) return 3;
    return static_cast<int>(result.ExitCode);
}

int RendererDiagnosticsTool::ReproduceCommand(const std::vector<std::string>& arguments)
{
    if (arguments.empty()) throw std::runtime_error("reproduce requires a capture directory.");
    const Capture capture = LoadCapture(std::filesystem::u8path(arguments[0]));
    std::optional<std::string> executableOption = GetOption(arguments, "--exe");
    if (!executableOption.has_value())
    {
        const auto executable = capture.Metadata.find("executable_path");
        if (executable != capture.Metadata.end()) executableOption = executable->second;
    }
    if (!executableOption.has_value())
    {
        throw std::runtime_error("Capture has no executable_path metadata; pass --exe explicitly.");
    }
    const auto automationMode = capture.Metadata.find("automation_mode");
    if (automationMode == capture.Metadata.end() || automationMode->second.empty() || automationMode->second == "off")
    {
        throw std::runtime_error("Capture was not created by a reproducible automation scenario.");
    }
    const std::string sourceSession = automationMode->second;
    const std::filesystem::path output = GetOption(arguments, "--output").has_value()
        ? std::filesystem::u8path(*GetOption(arguments, "--output"))
        : DefaultOutputPath("reproduce-" + sourceSession);
    std::map<std::string, std::string, std::less<>> environment = capture.ReproductionEnvironment;
    environment["RAYTRACING_DEMO_AUTOTEST"] = sourceSession;
    environment["RAYTRACING_DEMO_AUTOTEST_QUIT"] = "1";
    environment["RENDERER_DIAGNOSTICS"] = "1";
    environment["RENDERER_DIAGNOSTICS_SESSION"] = "reproduce-" + sourceSession;
    environment["RENDERER_DIAGNOSTICS_OUTPUT"] = std::filesystem::absolute(output).string();
    const uint64_t maximumEventCount = GetUnsignedOption(arguments, "--max-events", 262144u);
    if (maximumEventCount == 0u) throw std::runtime_error("--max-events must be greater than zero.");
    environment["RENDERER_DIAGNOSTICS_MAX_EVENTS"] = std::to_string(maximumEventCount);
    const bool streamline = capture.Metadata.contains("command_line") &&
        capture.Metadata.at("command_line").find("--streamline-interposer") != std::string::npos;

    const bool execute = HasFlag(arguments, "--execute");
    if (!execute)
    {
        std::cout << "{\"schema_version\":1,\"command\":\"reproduce\",\"executable\":";
        WriteJsonString(std::cout, *executableOption);
        std::cout << ",\"output\":"; WriteJsonString(std::cout, std::filesystem::absolute(output).string());
        std::cout << ",\"scenario\":"; WriteJsonString(std::cout, sourceSession);
        std::cout << ",\"execute\":false,\"max_event_count\":" << maximumEventCount << "}\n";
        return 0;
    }

    const LaunchResult result = LaunchProcess(
        std::filesystem::u8path(*executableOption),
        environment,
        streamline,
        GetUnsignedOption(arguments, "--timeout-seconds", 120u));
    const bool captureAvailable = std::filesystem::is_regular_file(output / "manifest.json");
    std::cout << "{\"schema_version\":1,\"command\":\"reproduce\",\"executable\":";
    WriteJsonString(std::cout, *executableOption);
    std::cout << ",\"output\":"; WriteJsonString(std::cout, std::filesystem::absolute(output).string());
    std::cout << ",\"scenario\":"; WriteJsonString(std::cout, sourceSession);
    std::cout << ",\"execute\":true,\"exit_code\":" << result.ExitCode
              << ",\"timed_out\":" << (result.TimedOut ? "true" : "false")
              << ",\"capture_available\":" << (captureAvailable ? "true" : "false")
              << ",\"max_event_count\":" << maximumEventCount << "}\n";
    if (result.ExitCode == 0u && !captureAvailable) return 3;
    return static_cast<int>(result.ExitCode);
}
//Modify End
