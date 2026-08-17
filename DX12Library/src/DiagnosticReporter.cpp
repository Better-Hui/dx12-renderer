#include "DX12LibPCH.h"
#include "DiagnosticReporter.h"

#include <cctype>
#include <fstream>
#include <string>

namespace
{
//Modify Begin:2026-07-30 by Hui
std::filesystem::path ResolveDiagnosticDirectory(std::filesystem::path outputDirectory)
{
    if (!outputDirectory.empty())
    {
        return outputDirectory;
    }

    std::error_code error;
    const std::filesystem::path workingDirectory = std::filesystem::current_path(error);
    if (!error)
    {
        return workingDirectory / "Saved" / "Diagnostics";
    }

    const std::filesystem::path temporaryDirectory = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::path{} : temporaryDirectory / "DX12Renderer" / "Diagnostics";
}

std::string SanitizeReportName(const std::string_view reportName)
{
    std::string result;
    result.reserve(reportName.size());
    for (const char character : reportName)
    {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        result.push_back(
            std::isalnum(unsignedCharacter) != 0 || character == '-' || character == '_'
                ? character
                : '_');
    }

    return result.empty() ? "Diagnostic" : result;
}
//Modify End
}

//Modify Begin:2026-07-30 by Hui
DiagnosticReporter::DiagnosticReporter(std::filesystem::path outputDirectory)
    : m_OutputDirectory(ResolveDiagnosticDirectory(std::move(outputDirectory)))
{
}

void DiagnosticReporter::Write(const std::string_view reportName, const std::string_view contents) const noexcept
{
    try
    {
        if (m_OutputDirectory.empty())
        {
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(m_OutputDirectory, error);
        if (error)
        {
            return;
        }

        std::ofstream output(
            m_OutputDirectory / (SanitizeReportName(reportName) + ".log"),
            std::ios::out | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    catch (...)
    {
    }
}
//Modify End
