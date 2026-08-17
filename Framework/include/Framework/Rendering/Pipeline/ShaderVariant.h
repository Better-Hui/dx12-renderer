//Modify Begin:2026-07-30 by Hui
#pragma once

#include <Framework/Rendering/Pipeline/ShaderBlob.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderVariantDefine
{
    std::string Name;
    std::string Value;

    bool operator==(const ShaderVariantDefine& other) const = default;
};

enum class ShaderVariantCompilationMode
{
    Auto,
    Disabled,
    Force,
};

struct ShaderVariantDesc
{
    std::wstring CompiledFileName;
    std::wstring SourceFileName;
    std::string EntryPoint = "main";
    std::string TargetProfile;
    std::vector<ShaderVariantDefine> Defines;
    std::vector<std::filesystem::path> IncludeDirectories;
    std::vector<std::wstring> CompilerArguments;
    std::string DebugName;
};

struct ShaderVariantManagerConfig
{
    std::filesystem::path SourceRoot;
    std::filesystem::path CacheRoot;
    std::vector<std::filesystem::path> IncludeDirectories;
    std::vector<std::wstring> CompilerArguments;
    ShaderVariantCompilationMode CompilationMode = ShaderVariantCompilationMode::Auto;
};

struct ShaderVariantKey
{
    std::wstring CompiledFileName;
    std::wstring SourceFileName;
    std::string EntryPoint;
    std::string TargetProfile;
    std::vector<ShaderVariantDefine> Defines;
    std::vector<std::filesystem::path> IncludeDirectories;
    std::vector<std::wstring> CompilerArguments;

    bool operator==(const ShaderVariantKey& other) const;
};

struct ShaderVariantKeyHasher
{
    size_t operator()(const ShaderVariantKey& key) const;
};

class ShaderCompiler;

class ShaderVariantManager final
{
public:
    ShaderVariantManager();
    explicit ShaderVariantManager(ShaderVariantManagerConfig config);
    ~ShaderVariantManager();

    ShaderVariantManager(const ShaderVariantManager&) = delete;
    ShaderVariantManager& operator=(const ShaderVariantManager&) = delete;

    static ShaderVariantManagerConfig CreateDefaultConfig();
    static ShaderVariantKey CreateKey(const ShaderVariantDesc& desc);

    std::shared_ptr<ShaderBlob> GetOrCompile(const ShaderVariantDesc& desc);
    std::shared_ptr<ShaderBlob> LoadCompiledVariant(const ShaderVariantDesc& desc);
    void Invalidate(const ShaderVariantKey& key);
    void Clear();

private:
    std::filesystem::path ResolveSourcePath(const ShaderVariantDesc& desc) const;

    ShaderVariantManagerConfig m_Config;
    std::unique_ptr<ShaderCompiler> m_Compiler;
    std::unordered_map<ShaderVariantKey, std::shared_ptr<ShaderBlob>, ShaderVariantKeyHasher> m_Cache;
};
//Modify End
