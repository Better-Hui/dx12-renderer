#pragma once

//Modify Begin:2026-07-24 by Hui

#include <DX12Library/ShaderUtils.h>

#include <d3d12.h>
#include <wrl.h>

#include <map>
#include <string>
#include <vector>

struct ShaderReflectionMetadata
{
    using NameCacheMap = std::map<std::string, size_t>;

    std::vector<ShaderUtils::ConstantBufferMetadata> m_ConstantBuffers{};
    NameCacheMap m_ConstantBuffersNameCache{};

    std::vector<ShaderUtils::ShaderResourceViewMetadata> m_ShaderResourceViews{};
    NameCacheMap m_ShaderResourceViewsNameCache{};

    std::vector<ShaderUtils::UnorderedAccessViewMetadata> m_UnorderedAccessViews{};
    NameCacheMap m_UnorderedAccessViewsNameCache{};

//Modify Begin:2026-07-27 by Hui
    std::vector<ShaderUtils::SamplerMetadata> m_Samplers{};
    NameCacheMap m_SamplersNameCache{};
//Modify End
};

class ShaderReflection
{
public:
    static ShaderReflectionMetadata CollectShader(const Microsoft::WRL::ComPtr<ID3DBlob>& shader);
    static ShaderReflectionMetadata CollectLibrary(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderLibrary);

private:
    template <typename Metadata>
    static void CacheResourceName(const Metadata& metadata, size_t index, ShaderReflectionMetadata::NameCacheMap& cache)
    {
        cache.emplace(metadata.Name, index);

        const std::string baseName = GetBaseResourceName(metadata.Name);
        if (baseName != metadata.Name)
        {
            cache.emplace(baseName, index);
        }
    }

    static std::string GetBaseResourceName(const std::string& name);
};

//Modify End
