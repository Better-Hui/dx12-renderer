//Modify Begin:2026-08-21 by Hui
#pragma once

#include <string>
#include <vector>

namespace RendererDiagnosticsTool
{
    int InspectCommand(const std::vector<std::string>& arguments);
    int QueryCommand(const std::vector<std::string>& arguments);
    int DiffCommand(const std::vector<std::string>& arguments);
    int RunCommand(const std::vector<std::string>& arguments);
    int ReproduceCommand(const std::vector<std::string>& arguments);
    int SelfTestCommand();
    void PrintUsage();
}
//Modify End
