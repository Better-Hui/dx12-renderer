//Modify Begin:2026-07-27 by BestHui
#include "ShaderVariant.h"

#include <algorithm>
#include <functional>

namespace
{
    template <typename T>
    void HashCombine(size_t& seed, const T& value)
    {
        seed ^= std::hash<T>{}(value) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    }

    std::vector<ShaderVariantDefine> SortedDefines(std::vector<ShaderVariantDefine> defines)
    {
        std::sort(
            defines.begin(),
            defines.end(),
            [](const ShaderVariantDefine& lhs, const ShaderVariantDefine& rhs)
            {
                if (lhs.Name == rhs.Name)
                {
                    return lhs.Value < rhs.Value;
                }
                return lhs.Name < rhs.Name;
            });
        return defines;
    }
}

bool ShaderVariantKey::operator==(const ShaderVariantKey& other) const
{
    if (CompiledFileName != other.CompiledFileName ||
        SourceFileName != other.SourceFileName ||
        EntryPoint != other.EntryPoint ||
        TargetProfile != other.TargetProfile ||
        ShaderModelMajor != other.ShaderModelMajor ||
        ShaderModelMinor != other.ShaderModelMinor ||
        HotReloadKey != other.HotReloadKey ||
        Defines.size() != other.Defines.size())
    {
        return false;
    }

    for (size_t i = 0; i < Defines.size(); ++i)
    {
        if (Defines[i].Name != other.Defines[i].Name ||
            Defines[i].Value != other.Defines[i].Value)
        {
            return false;
        }
    }

    return true;
}

size_t ShaderVariantKeyHasher::operator()(const ShaderVariantKey& key) const
{
    size_t seed = 0;
    HashCombine(seed, key.CompiledFileName);
    HashCombine(seed, key.SourceFileName);
    HashCombine(seed, key.EntryPoint);
    HashCombine(seed, key.TargetProfile);
    HashCombine(seed, key.ShaderModelMajor);
    HashCombine(seed, key.ShaderModelMinor);
    HashCombine(seed, key.HotReloadKey);
    for (const ShaderVariantDefine& define : key.Defines)
    {
        HashCombine(seed, define.Name);
        HashCombine(seed, define.Value);
    }
    return seed;
}

ShaderVariantKey ShaderVariantManager::CreateKey(const ShaderVariantDesc& desc)
{
    ShaderVariantKey key;
    key.CompiledFileName = desc.CompiledFileName;
    key.SourceFileName = desc.SourceFileName;
    key.EntryPoint = desc.EntryPoint;
    key.TargetProfile = desc.TargetProfile;
    key.ShaderModelMajor = desc.ShaderModelMajor;
    key.ShaderModelMinor = desc.ShaderModelMinor;
    key.Defines = SortedDefines(desc.Defines);
    key.HotReloadKey = desc.HotReloadKey;
    return key;
}

std::shared_ptr<ShaderBlob> ShaderVariantManager::LoadCompiledVariant(const ShaderVariantDesc& desc)
{
    ShaderVariantKey key = CreateKey(desc);
    const auto findResult = m_Cache.find(key);
    if (findResult != m_Cache.end())
    {
        return findResult->second;
    }

    auto shaderBlob = std::make_shared<ShaderBlob>(desc.CompiledFileName);
    m_Cache.emplace(std::move(key), shaderBlob);
    return shaderBlob;
}

void ShaderVariantManager::Invalidate(const ShaderVariantKey& key)
{
    m_Cache.erase(key);
}

void ShaderVariantManager::Clear()
{
    m_Cache.clear();
}
//Modify End
