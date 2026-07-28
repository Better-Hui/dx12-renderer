//Modify Begin:2026-07-27 by BestHui
#pragma once

#include "ShaderBlob.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderVariantDefine
{
    std::string Name;
    std::string Value;
};

struct ShaderVariantDesc
{
    std::wstring CompiledFileName;
    std::wstring SourceFileName;
    std::string EntryPoint = "main";
    std::string TargetProfile;
    uint32_t ShaderModelMajor = 6;
    uint32_t ShaderModelMinor = 6;
    std::vector<ShaderVariantDefine> Defines;
    std::string HotReloadKey;
};

struct ShaderVariantKey
{
    std::wstring CompiledFileName;
    std::wstring SourceFileName;
    std::string EntryPoint;
    std::string TargetProfile;
    uint32_t ShaderModelMajor = 6;
    uint32_t ShaderModelMinor = 6;
    std::vector<ShaderVariantDefine> Defines;
    std::string HotReloadKey;

    bool operator==(const ShaderVariantKey& other) const;
};

struct ShaderVariantKeyHasher
{
    size_t operator()(const ShaderVariantKey& key) const;
};

class ShaderVariantManager final
{
public:
    static ShaderVariantKey CreateKey(const ShaderVariantDesc& desc);
    std::shared_ptr<ShaderBlob> LoadCompiledVariant(const ShaderVariantDesc& desc);
    void Invalidate(const ShaderVariantKey& key);
    void Clear();

private:
    std::unordered_map<ShaderVariantKey, std::shared_ptr<ShaderBlob>, ShaderVariantKeyHasher> m_Cache;
};
//Modify End
