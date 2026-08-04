#pragma once

//Modify Begin:2026-07-27 by BestHui

#include <Framework/Rendering/Pipeline/DescriptorLayout.h>
//Modify Begin:2026-07-27 by BestHui
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
//Modify End
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/RayTracing/RayTracingPipelineStateBuilder.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>

#include "RayTracingDispatchTables.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace RayTracingShaderInternal
{
    constexpr LPCWSTR DefaultRayGenerationShaderName = L"RayGen";
    constexpr LPCWSTR DefaultMissShaderName = L"Miss";
    constexpr LPCWSTR DefaultClosestHitShaderName = L"ClosestHit";
    constexpr LPCWSTR DefaultHitGroupName = L"HitGroup";

    inline bool IsDescriptorTableBinding(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
        case RayTracingShaderBindingType::TextureArray:
        case RayTracingShaderBindingType::VertexBufferArray:
        case RayTracingShaderBindingType::IndexBufferArray:
            return true;
        default:
            return false;
        }
    }

    inline const char* GetRayTracingBindingTypeName(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
            return "OutputTexture";
        case RayTracingShaderBindingType::AccelerationStructure:
            return "AccelerationStructure";
        case RayTracingShaderBindingType::ConstantBuffer:
            return "ConstantBuffer";
        case RayTracingShaderBindingType::StructuredBuffer:
            return "StructuredBuffer";
        case RayTracingShaderBindingType::TextureArray:
            return "TextureArray";
        case RayTracingShaderBindingType::VertexBufferArray:
            return "VertexBufferArray";
        case RayTracingShaderBindingType::IndexBufferArray:
            return "IndexBufferArray";
        default:
            return "Unknown";
        }
    }

    inline DescriptorBindingKind GetDescriptorBindingKind(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
            return DescriptorBindingKind::UnorderedAccessView;
        case RayTracingShaderBindingType::AccelerationStructure:
            return DescriptorBindingKind::AccelerationStructure;
        case RayTracingShaderBindingType::ConstantBuffer:
            return DescriptorBindingKind::ConstantBuffer;
        default:
            return DescriptorBindingKind::ShaderResourceView;
        }
    }
}

struct RayTracingShader::Impl
{
    Impl(
        FrameworkDeviceContext& deviceContext,
        const ShaderBlob& shaderLibrary,
        RayTracingPipelineDesc pipelineDesc);

    FrameworkDeviceContext& DeviceContext;
    RayTracingPipelineDesc Desc;
    std::shared_ptr<RayTracingPipelineState> PipelineState;
    PipelineLayout Layout;
//Modify Begin:2026-07-27 by BestHui
    mutable RayTracingDispatchTables DispatchTables;
//Modify End
    std::unordered_map<std::string, uint32_t> BindingIndicesByName;
};

struct RayTracingBindingSet::Impl
{
    explicit Impl(const RayTracingShader& shader);

    const RayTracingShaderBindingDesc& GetBinding(std::string_view name, RayTracingShaderBindingType expectedType) const;
    bool HasBinding(std::string_view name) const;
    const RayTracingShaderBindingDesc& GetShaderResourceBinding(std::string_view name) const;
    uint32_t GetBindingIndex(const RayTracingShaderBindingDesc& binding) const;
    void MarkDescriptorsDirty(const RayTracingShaderBindingDesc& binding);

    const RayTracingShader& Shader;
//Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorPool DescriptorPool;
//Modify End
    PipelineDescriptorSet DescriptorSet;
};

//Modify End
