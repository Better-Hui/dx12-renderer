//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <DX12Library/RenderTargetState.h>
#include <DX12Library/RootSignature.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct PipelineShaderBytecodeKey
{
    size_t Hash = 0;
    size_t SizeInBytes = 0;

    bool operator==(const PipelineShaderBytecodeKey& other) const
    {
        return Hash == other.Hash && SizeInBytes == other.SizeInBytes;
    }
};

struct RasterPipelineStateKey
{
    PipelineShaderBytecodeKey VertexShader;
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
    PipelineShaderBytecodeKey AmplificationShader;
//Modify End
    PipelineShaderBytecodeKey MeshShader;
//Modify End
    PipelineShaderBytecodeKey PixelShader;
    size_t LayoutHash = 0;
    RenderTargetState RenderTarget;
    size_t FixedFunctionStateHash = 0;

    bool operator==(const RasterPipelineStateKey& other) const
    {
        return VertexShader == other.VertexShader &&
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
            AmplificationShader == other.AmplificationShader &&
//Modify End
            MeshShader == other.MeshShader &&
//Modify End
            PixelShader == other.PixelShader &&
            LayoutHash == other.LayoutHash &&
            RenderTarget == other.RenderTarget &&
            FixedFunctionStateHash == other.FixedFunctionStateHash;
    }
};

struct ComputePipelineStateKey
{
    PipelineShaderBytecodeKey Shader;
    size_t LayoutHash = 0;
    size_t DefinesHash = 0;
    uint32_t ShaderModelMajor = 0;
    uint32_t ShaderModelMinor = 0;

    bool operator==(const ComputePipelineStateKey& other) const
    {
        return Shader == other.Shader &&
            LayoutHash == other.LayoutHash &&
            DefinesHash == other.DefinesHash &&
            ShaderModelMajor == other.ShaderModelMajor &&
            ShaderModelMinor == other.ShaderModelMinor;
    }
};

struct RayTracingPipelineStateKey
{
    PipelineShaderBytecodeKey ShaderLibrary;
    size_t LayoutHash = 0;
    size_t ExportsHash = 0;
    size_t HitGroupsHash = 0;
    uint32_t PayloadSizeInBytes = 0;
    uint32_t AttributeSizeInBytes = 0;
    uint32_t MaxTraceRecursionDepth = 0;
    uint32_t DescriptorCapacity = 0;

    bool operator==(const RayTracingPipelineStateKey& other) const
    {
        return ShaderLibrary == other.ShaderLibrary &&
            LayoutHash == other.LayoutHash &&
            ExportsHash == other.ExportsHash &&
            HitGroupsHash == other.HitGroupsHash &&
            PayloadSizeInBytes == other.PayloadSizeInBytes &&
            AttributeSizeInBytes == other.AttributeSizeInBytes &&
            MaxTraceRecursionDepth == other.MaxTraceRecursionDepth &&
            DescriptorCapacity == other.DescriptorCapacity;
    }
};

inline void PipelineHashCombine(size_t& seed, const size_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

template<typename T>
inline void PipelineHashValue(size_t& seed, const T& value)
{
    PipelineHashCombine(seed, std::hash<T>{}(value));
}

inline void PipelineHashBytes(size_t& seed, const void* data, const size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    PipelineHashCombine(seed, hash);
    PipelineHashCombine(seed, size);
}

inline void PipelineHashString(size_t& seed, const std::string_view value)
{
    PipelineHashBytes(seed, value.data(), value.size());
}

inline void PipelineHashWideString(size_t& seed, const std::wstring& value)
{
    PipelineHashBytes(seed, value.data(), value.size() * sizeof(wchar_t));
}

inline PipelineShaderBytecodeKey MakePipelineShaderBytecodeKey(const Microsoft::WRL::ComPtr<ID3DBlob>& shader)
{
    PipelineShaderBytecodeKey key;
    if (shader == nullptr)
    {
        return key;
    }

    key.SizeInBytes = shader->GetBufferSize();
    PipelineHashBytes(key.Hash, shader->GetBufferPointer(), shader->GetBufferSize());
    return key;
}

inline size_t MakePipelineRootSignatureHash(const RootSignature* rootSignature)
{
    if (rootSignature == nullptr)
    {
        return 0;
    }

    size_t seed = 0;
    const D3D12_ROOT_SIGNATURE_DESC1& desc = rootSignature->GetRootSignatureDesc();
    PipelineHashValue(seed, desc.Flags);
    PipelineHashValue(seed, desc.NumParameters);
    for (UINT i = 0; i < desc.NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1& parameter = desc.pParameters[i];
        PipelineHashValue(seed, parameter.ParameterType);
        PipelineHashValue(seed, parameter.ShaderVisibility);
        if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            PipelineHashValue(seed, parameter.DescriptorTable.NumDescriptorRanges);
            for (UINT rangeIndex = 0; rangeIndex < parameter.DescriptorTable.NumDescriptorRanges; ++rangeIndex)
            {
                const D3D12_DESCRIPTOR_RANGE1& range = parameter.DescriptorTable.pDescriptorRanges[rangeIndex];
                PipelineHashBytes(seed, &range, sizeof(range));
            }
        }
        else if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        {
            PipelineHashBytes(seed, &parameter.Constants, sizeof(parameter.Constants));
        }
        else
        {
            PipelineHashBytes(seed, &parameter.Descriptor, sizeof(parameter.Descriptor));
        }
    }

    PipelineHashValue(seed, desc.NumStaticSamplers);
    for (UINT i = 0; i < desc.NumStaticSamplers; ++i)
    {
        PipelineHashBytes(seed, &desc.pStaticSamplers[i], sizeof(D3D12_STATIC_SAMPLER_DESC));
    }
    return seed;
}

inline size_t MakePipelineInputLayoutHash(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout)
{
    size_t seed = 0;
    PipelineHashValue(seed, inputLayout.size());
    for (const D3D12_INPUT_ELEMENT_DESC& element : inputLayout)
    {
        PipelineHashString(seed, element.SemanticName != nullptr ? element.SemanticName : "");
        PipelineHashValue(seed, element.SemanticIndex);
        PipelineHashValue(seed, element.Format);
        PipelineHashValue(seed, element.InputSlot);
        PipelineHashValue(seed, element.AlignedByteOffset);
        PipelineHashValue(seed, element.InputSlotClass);
        PipelineHashValue(seed, element.InstanceDataStepRate);
    }
    return seed;
}

inline size_t MakePipelineDescriptorCapacityHash(const std::vector<uint32_t>& descriptorCapacities)
{
    size_t seed = 0;
    PipelineHashValue(seed, descriptorCapacities.size());
    for (const uint32_t capacity : descriptorCapacities)
    {
        PipelineHashValue(seed, capacity);
    }
    return seed;
}

namespace std
{
    template<>
    struct hash<PipelineShaderBytecodeKey>
    {
        size_t operator()(const PipelineShaderBytecodeKey& key) const
        {
            size_t seed = 0;
            PipelineHashValue(seed, key.Hash);
            PipelineHashValue(seed, key.SizeInBytes);
            return seed;
        }
    };

    template<>
    struct hash<RasterPipelineStateKey>
    {
        size_t operator()(const RasterPipelineStateKey& key) const
        {
            size_t seed = 0;
            PipelineHashCombine(seed, hash<PipelineShaderBytecodeKey>{}(key.VertexShader));
            PipelineHashCombine(seed, hash<PipelineShaderBytecodeKey>{}(key.PixelShader));
            PipelineHashValue(seed, key.LayoutHash);
            PipelineHashCombine(seed, hash<RenderTargetState>{}(key.RenderTarget));
            PipelineHashValue(seed, key.FixedFunctionStateHash);
            return seed;
        }
    };

    template<>
    struct hash<ComputePipelineStateKey>
    {
        size_t operator()(const ComputePipelineStateKey& key) const
        {
            size_t seed = 0;
            PipelineHashCombine(seed, hash<PipelineShaderBytecodeKey>{}(key.Shader));
            PipelineHashValue(seed, key.LayoutHash);
            PipelineHashValue(seed, key.DefinesHash);
            PipelineHashValue(seed, key.ShaderModelMajor);
            PipelineHashValue(seed, key.ShaderModelMinor);
            return seed;
        }
    };

    template<>
    struct hash<RayTracingPipelineStateKey>
    {
        size_t operator()(const RayTracingPipelineStateKey& key) const
        {
            size_t seed = 0;
            PipelineHashCombine(seed, hash<PipelineShaderBytecodeKey>{}(key.ShaderLibrary));
            PipelineHashValue(seed, key.LayoutHash);
            PipelineHashValue(seed, key.ExportsHash);
            PipelineHashValue(seed, key.HitGroupsHash);
            PipelineHashValue(seed, key.PayloadSizeInBytes);
            PipelineHashValue(seed, key.AttributeSizeInBytes);
            PipelineHashValue(seed, key.MaxTraceRecursionDepth);
            PipelineHashValue(seed, key.DescriptorCapacity);
            return seed;
        }
    };
}
//Modify End
