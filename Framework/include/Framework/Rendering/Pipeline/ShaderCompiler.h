//Modify Begin:2026-07-30 by Hui
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ShaderCompileDefine
{
    std::string Name;
    std::string Value;
};

struct ShaderCompileRequest
{
    std::filesystem::path SourcePath;
    std::string EntryPoint = "main";
    std::string TargetProfile;
    std::vector<ShaderCompileDefine> Defines;
    std::vector<std::filesystem::path> IncludeDirectories;
    std::vector<std::wstring> Arguments;
};

class ShaderCompiler final
{
public:
    ShaderCompiler();
    ~ShaderCompiler();

    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    std::vector<uint8_t> Compile(const ShaderCompileRequest& request) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End
