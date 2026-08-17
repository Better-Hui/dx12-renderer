#pragma once

#include <filesystem>
#include <string_view>

//Modify Begin:2026-07-30 by Hui
class DiagnosticReporter
{
public:
    explicit DiagnosticReporter(std::filesystem::path outputDirectory = {});

    void Write(std::string_view reportName, std::string_view contents) const noexcept;

private:
    std::filesystem::path m_OutputDirectory;
};
//Modify End
