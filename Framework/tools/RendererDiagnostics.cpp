//Modify Begin:2026-08-21 by Hui
#include "RendererDiagnosticsCommands.h"
#include "RendererDiagnosticsJson.h"

#include <algorithm>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void ValidateArguments(
        const std::string_view command,
        const std::vector<std::string>& arguments,
        const std::initializer_list<std::string_view> valueOptions,
        const std::initializer_list<std::string_view> flags,
        const size_t minimumPositionals,
        const size_t maximumPositionals)
    {
        size_t positionalCount = 0;
        for (size_t index = 0; index < arguments.size(); ++index)
        {
            const std::string& argument = arguments[index];
            if (!argument.starts_with("--"))
            {
                ++positionalCount;
                continue;
            }
            if (std::ranges::find(flags, argument) != flags.end())
            {
                continue;
            }
            if (std::ranges::find(valueOptions, argument) == valueOptions.end())
            {
                throw std::runtime_error("Unknown option for " + std::string(command) + ": " + argument + ".");
            }
            if (index + 1u >= arguments.size() || arguments[index + 1u].starts_with("--"))
            {
                throw std::runtime_error(argument + " requires a value.");
            }
            ++index;
        }
        if (positionalCount < minimumPositionals || positionalCount > maximumPositionals)
        {
            throw std::runtime_error("Invalid positional argument count for " + std::string(command) + ".");
        }
    }
}

void RendererDiagnosticsTool::PrintUsage()
{
    std::cout
        << "RendererDiagnostics commands:\n"
        << "  run --exe <RaytracingDemo.exe> --scenario <name> [--output <dir>] [--set RAYTRACING_DEMO_NAME=value] [--timeout-seconds N] [--max-events N] [--streamline-interposer]\n"
        << "  inspect <capture> [--window N]\n"
        << "  query <capture> [--frame N] [--category prefix] [--name text] [--correlation N] [--severity level] [--field name=value] [--limit N] [--jsonl]\n"
        << "  diff <baseline> <current> [--regression-percent N] [--min-ms N]\n"
        << "  reproduce <capture> [--exe <path>] [--output <dir>] [--execute] [--timeout-seconds N] [--max-events N]\n"
        << "  selftest\n"
        << "Exit codes: 0 clean, 10 findings, 11 regression, 12 incomplete capture, 20-24 automation failure.\n";
}

int wmain(const int argc, wchar_t** argv)
{
    using namespace RendererDiagnosticsTool;
    try
    {
        if (argc < 2)
        {
            PrintUsage();
            return 2;
        }
        const std::string command = WideToUtf8(argv[1]);
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<size_t>((std::max)(argc - 2, 0)));
        for (int index = 2; index < argc; ++index) arguments.push_back(WideToUtf8(argv[index]));
        if (command == "run")
        {
            ValidateArguments(command, arguments,
                { "--exe", "--scenario", "--output", "--set", "--timeout-seconds", "--max-events" },
                { "--streamline-interposer" }, 0u, 0u);
            return RunCommand(arguments);
        }
        if (command == "inspect")
        {
            ValidateArguments(command, arguments, { "--window" }, {}, 1u, 1u);
            return InspectCommand(arguments);
        }
        if (command == "query")
        {
            ValidateArguments(command, arguments,
                { "--frame", "--category", "--name", "--correlation", "--severity", "--field", "--limit" },
                { "--jsonl" }, 1u, 1u);
            return QueryCommand(arguments);
        }
        if (command == "diff")
        {
            ValidateArguments(command, arguments, { "--regression-percent", "--min-ms" }, {}, 2u, 2u);
            return DiffCommand(arguments);
        }
        if (command == "reproduce")
        {
            ValidateArguments(command, arguments,
                { "--exe", "--output", "--timeout-seconds", "--max-events" },
                { "--execute" }, 1u, 1u);
            return ReproduceCommand(arguments);
        }
        if (command == "selftest")
        {
            ValidateArguments(command, arguments, {}, {}, 0u, 0u);
            return SelfTestCommand();
        }
        PrintUsage();
        return 2;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "{\"schema_version\":1,\"error\":";
        WriteJsonString(std::cerr, exception.what());
        std::cerr << "}\n";
        return 3;
    }
}
//Modify End
