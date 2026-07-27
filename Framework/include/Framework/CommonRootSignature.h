#pragma once

#include <d3dx12.h>

#include <DX12Library/RootSignature.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Resource.h>
#include <DX12Library/DescriptorAllocation.h>

#include <vector>

#include "ShaderResourceView.h"
#include "UnorderedAccessView.h"
//Modify Begin:2026-07-27 by BestHui
#include "FrameworkDeprecated.h"
//Modify End

//Modify Begin:2026-07-21 by BestHui
class RayTracingAccelerationStructure;
//Modify End

class CommonRootSignature final : public RootSignature
{
public:
    explicit CommonRootSignature(const std::shared_ptr<Resource>& emptyResource);

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected Shader/ComputeShader bindings or PipelineDescriptorSet instead of direct CommonRootSignature binding.")
    void Bind(CommandList& commandList) const;

    FRAMEWORK_DEPRECATED("Use reflected Shader/ComputeShader named constant buffer binding or PipelineDescriptorSet instead.")
    void SetPipelineConstantBuffer(CommandList& commandList, size_t size, const void* data) const;
//Modify End

    template <typename T>
    void SetPipelineConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetPipelineConstantBuffer(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected Shader named constant buffer binding or PipelineDescriptorSet instead.")
    void SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    FRAMEWORK_DEPRECATED("Use reflected Shader named constant buffer binding or PipelineDescriptorSet instead.")
    void SetModelConstantBuffer(CommandList& commandList, size_t size, const void* data) const;
//Modify End

    template <typename T>
    void SetModelConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetModelConstantBuffer(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named constant buffer binding or PipelineDescriptorSet instead.")
    void SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const;
//Modify End

//Modify Begin:2026-07-23 by BestHui
//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named constant buffer binding or PipelineDescriptorSet instead.")
//Modify End
    void SetComputePipelineConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    template <typename T>
    void SetComputePipelineConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetComputePipelineConstantBuffer(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named constant buffer binding or PipelineDescriptorSet instead.")
//Modify End
    void SetComputeMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    template <typename T>
    void SetComputeMaterialConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetComputeMaterialConstantBuffer(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named constant buffer binding or PipelineDescriptorSet instead.")
//Modify End
    void SetComputeModelConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    template <typename T>
    void SetComputeModelConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetComputeModelConstantBuffer(commandList, sizeof(T), &data);
    }
//Modify End

    template <typename T>
    void SetGraphicsRootConstants(CommandList& commandList, const T& data) const
    {
        SetGraphicsRootConstants(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected Shader named resource binding or PipelineDescriptorSet instead of CommonRootSignature root constants.")
    void SetGraphicsRootConstants(CommandList& commandList, size_t size, const void* data) const;
//Modify End

    template <typename T>
    void SetComputeRootConstants(CommandList& commandList, const T& data) const
    {
        SetComputeRootConstants(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named resource binding or PipelineDescriptorSet instead of CommonRootSignature root constants.")
    void SetComputeRootConstants(CommandList& commandList, size_t size, const void* data) const;
//Modify End

    template <typename T>
    void SetComputeConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetComputeConstantBuffer(commandList, sizeof(T), &data);
    }

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use SetShaderResourceView/SetTexture by resource name or PipelineDescriptorSet instead.")
    void SetPipelineShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& srv) const;

    FRAMEWORK_DEPRECATED("Use SetShaderResourceView/SetTexture by resource name or PipelineDescriptorSet instead.")
    void SetMaterialShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& srv) const;

    FRAMEWORK_DEPRECATED("Use SetShaderResourceView/SetTexture by resource name or PipelineDescriptorSet instead.")
    void SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& srv) const;
//Modify End

//Modify Begin:2026-07-21 by BestHui
//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use reflected ComputeShader named acceleration structure binding or PipelineDescriptorSet instead.")
//Modify End
    void SetComputeAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const;
//Modify End

//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use SetUnorderedAccessView by resource name or PipelineDescriptorSet instead.")
    void SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& uav) const;

    FRAMEWORK_DEPRECATED("Use PipelineLayout default descriptor staging or named descriptor rebinding instead.")
    void UnbindMaterialShaderResourceViews(CommandList& commandList);
//Modify End

//Modify Begin:2026-07-23 by BestHui
//Modify Begin:2026-07-27 by BestHui
    FRAMEWORK_DEPRECATED("Use PipelineLayout default descriptor staging or named descriptor rebinding instead.")
//Modify End
    void UnbindComputeShaderResourceViews(CommandList& commandList);
//Modify End

    static constexpr UINT MATERIAL_REGISTER_SPACE = 0u;
    static constexpr UINT MODEL_REGISTER_SPACE = 1u;
    static constexpr UINT PIPELINE_REGISTER_SPACE = 2u;
    static constexpr UINT CONSTANTS_REGISTER_SPACE = 3u;
//Modify Begin:2026-07-23 by BestHui
    static constexpr UINT MATERIAL_SRVS_COUNT = 6u;
    static constexpr UINT PIPELINE_SRVS_COUNT = 1024u;
    static constexpr UINT UAVS_COUNT = 6u;
//Modify End
//Modify Begin:2026-07-21 by BestHui
    static constexpr UINT INLINE_RAYTRACING_REGISTER_SPACE = 4u;
    static constexpr UINT INLINE_RAYTRACING_ACCELERATION_STRUCTURE_REGISTER = 0u;
//Modify End

    struct RootParameters
    {
        enum
        {
            // sorted by the change frequency (high to low)
            Constants,
            ModelCBuffer,

            MaterialCBuffer,
            MaterialSRVs,

            PipelineCBuffer,
            PipelineSRVs,

            UAVs,

//Modify Begin:2026-07-21 by BestHui
            ComputeAccelerationStructure,
//Modify End

            NumRootParameters,
        };
    };

private:
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
    using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

    void CombineRootSignatureFlags(D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags, const std::vector<RootParameter>& rootParameters);
    bool CheckRootParametersVisiblity(const std::vector<RootParameter>& rootParameters, D3D12_SHADER_VISIBILITY param2);

    ShaderResourceView m_EmptySRV;
    UnorderedAccessView m_EmptyUAV;
    Texture m_NullTexture;
};
