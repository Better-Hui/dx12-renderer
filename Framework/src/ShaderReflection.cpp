#include "ShaderReflection.h"

//Modify Begin:2026-07-24 by BestHui

std::string ShaderReflection::GetBaseResourceName(const std::string& name)
{
    const size_t arraySuffix = name.find("[0]");
    if (arraySuffix == std::string::npos)
    {
        return name;
    }

    return name.substr(0, arraySuffix);
}

ShaderReflectionMetadata ShaderReflection::CollectShader(const Microsoft::WRL::ComPtr<ID3DBlob>& shader)
{
    const auto reflection = ShaderUtils::Reflect(shader);

    ShaderReflectionMetadata metadata;

    metadata.m_ConstantBuffers = ShaderUtils::GetConstantBuffers(reflection);
    for (size_t i = 0; i < metadata.m_ConstantBuffers.size(); ++i)
    {
        CacheResourceName(metadata.m_ConstantBuffers[i], i, metadata.m_ConstantBuffersNameCache);
    }

    metadata.m_ShaderResourceViews = ShaderUtils::GetShaderResourceViews(reflection);
    for (size_t i = 0; i < metadata.m_ShaderResourceViews.size(); ++i)
    {
        CacheResourceName(metadata.m_ShaderResourceViews[i], i, metadata.m_ShaderResourceViewsNameCache);
    }

    metadata.m_UnorderedAccessViews = ShaderUtils::GetUnorderedAccessViews(reflection);
    for (size_t i = 0; i < metadata.m_UnorderedAccessViews.size(); ++i)
    {
        CacheResourceName(metadata.m_UnorderedAccessViews[i], i, metadata.m_UnorderedAccessViewsNameCache);
    }

    return metadata;
}

ShaderReflectionMetadata ShaderReflection::CollectLibrary(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderLibrary)
{
    const auto reflection = ShaderUtils::ReflectLibrary(shaderLibrary);

    ShaderReflectionMetadata metadata;

    metadata.m_ConstantBuffers = ShaderUtils::GetConstantBuffers(reflection);
    for (size_t i = 0; i < metadata.m_ConstantBuffers.size(); ++i)
    {
        CacheResourceName(metadata.m_ConstantBuffers[i], i, metadata.m_ConstantBuffersNameCache);
    }

    metadata.m_ShaderResourceViews = ShaderUtils::GetShaderResourceViews(reflection);
    for (size_t i = 0; i < metadata.m_ShaderResourceViews.size(); ++i)
    {
        CacheResourceName(metadata.m_ShaderResourceViews[i], i, metadata.m_ShaderResourceViewsNameCache);
    }

    metadata.m_UnorderedAccessViews = ShaderUtils::GetUnorderedAccessViews(reflection);
    for (size_t i = 0; i < metadata.m_UnorderedAccessViews.size(); ++i)
    {
        CacheResourceName(metadata.m_UnorderedAccessViews[i], i, metadata.m_UnorderedAccessViewsNameCache);
    }

    return metadata;
}

//Modify End

