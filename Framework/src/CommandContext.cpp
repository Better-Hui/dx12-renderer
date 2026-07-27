//Modify Begin:2026-07-27 by BestHui

#include <Framework/CommandContext.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/ComputeShader.h>
#include <Framework/PipelineDescriptorSet.h>
#include <Framework/PipelineLayout.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/RayTracingShader.h>
#include <Framework/Shader.h>
#include <Framework/UnorderedAccessView.h>

#include "RayTracingShaderInternal.h"

#include <cstring>

CommandContext::CommandContext(CommandList& commandList)
    : m_CommandList(commandList)
{
}

void CommandContext::BindPipeline(Shader& shader) const
{
    const auto device = Application::Get().GetDevice();
    const auto& renderTargetState = m_CommandList.GetLastRenderTargetState();
    const auto pipelineState = shader.GetPipelineState(device, renderTargetState);

    if (shader.m_UseReflectedRootSignature)
    {
        SetGraphicsRootSignature(*shader.m_RootSignature);
        shader.m_PipelineLayout->StageDefaultDescriptorTables(m_CommandList);
    }

    SetGraphicsPipelineState(pipelineState);
}

void CommandContext::BindPipeline(const ComputeShader& shader) const
{
    const auto device = Application::Get().GetDevice();
    const auto pipelineState = shader.GetPipelineState(device);

    SetComputeRootSignature(*shader.m_RootSignature);
    if (shader.m_UseReflectedRootSignature)
    {
        shader.m_PipelineLayout->StageDefaultDescriptorTables(m_CommandList);
    }
    SetComputePipelineState(pipelineState);
}

void CommandContext::BindPipeline(const RayTracingShader& shader) const
{
    SetRayTracingPipelineState(shader.m_Impl->PipelineState->GetStateObject(), shader.m_Impl->PipelineState->GetGlobalRootSignature());
}

void CommandContext::BindDescriptorSet(const PipelineDescriptorSet& descriptorSet, const PipelineBindPoint bindPoint) const
{
    for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
    {
        (void)boundResource;
        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics:
            ApplyGraphicsBinding(descriptorSet, rootParameterIndex);
            break;
        case PipelineBindPoint::Compute:
            ApplyComputeBinding(descriptorSet, rootParameterIndex);
            break;
        default:
            Assert(false, "Unsupported pipeline bind point.");
            break;
        }
    }
}

void CommandContext::SetGraphicsRootSignature(const RootSignature& rootSignature) const
{
    m_CommandList.SetGraphicsRootSignature(rootSignature);
}

void CommandContext::SetComputeRootSignature(const RootSignature& rootSignature) const
{
    m_CommandList.SetComputeRootSignature(rootSignature);
}

void CommandContext::SetGraphicsPipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const
{
    m_CommandList.SetPipelineState(pipelineState);
}

void CommandContext::SetComputePipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const
{
    m_CommandList.SetPipelineState(pipelineState);
}

void CommandContext::SetRayTracingPipelineState(
    const Microsoft::WRL::ComPtr<ID3D12StateObject>& stateObject,
    const RootSignature& globalRootSignature) const
{
    m_CommandList.SetRaytracingPipelineState(stateObject);
    m_CommandList.SetComputeRootSignature(globalRootSignature);
}

void CommandContext::ApplyGraphicsBinding(const PipelineDescriptorSet& descriptorSet, const UINT rootParameterIndex) const
{
    const PipelineLayout& layout = descriptorSet.GetLayout();
    const PipelineDescriptorRangeDesc* range = layout.FindRangeByRootParameterIndex(rootParameterIndex);
    Assert(range != nullptr, "Pipeline descriptor set binding was not found.");

    const PipelineBoundResource* boundResource = descriptorSet.FindBoundResource(rootParameterIndex);
    if (boundResource == nullptr)
    {
        return;
    }

    if (range->Kind == DescriptorBindingKind::ConstantBuffer)
    {
        Assert(!boundResource->ConstantBufferData.empty(), "Pipeline constant buffer is not bound.");
        m_CommandList.SetGraphicsDynamicConstantBuffer(
            rootParameterIndex,
            boundResource->ConstantBufferData.size(),
            boundResource->ConstantBufferData.data());
        return;
    }

    Assert(range->Kind != DescriptorBindingKind::AccelerationStructure, "Graphics pipeline does not support acceleration structure root bindings.");

    if (range->Kind == DescriptorBindingKind::ShaderResourceView)
    {
        Assert(range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable, "Graphics root SRV bindings are not supported yet.");

        const size_t shaderResourceCount = boundResource->ShaderResources.size();
        for (UINT i = 0; i < static_cast<UINT>(shaderResourceCount); ++i)
        {
            if (i >= boundResource->ShaderResources.size() || !boundResource->ShaderResources[i].has_value())
            {
                continue;
            }

            const PipelineShaderResourceBinding& shaderResource = *boundResource->ShaderResources[i];
            Assert(shaderResource.Resource != nullptr, "Pipeline SRV resource is not bound.");
            const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = shaderResource.HasDesc ? &shaderResource.Desc : nullptr;
            if (shaderResource.Resource->AreAutoBarriersEnabled())
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.StateAfter,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
            else
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
        }
        return;
    }

    if (range->Kind == DescriptorBindingKind::UnorderedAccessView)
    {
        Assert(boundResource->UnorderedAccessView.has_value(), "Pipeline UAV resource is not bound.");
        const UnorderedAccessView& unorderedAccessView = *boundResource->UnorderedAccessView;
        if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        else
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
    }
}

void CommandContext::ApplyComputeBinding(const PipelineDescriptorSet& descriptorSet, const UINT rootParameterIndex) const
{
    const PipelineLayout& layout = descriptorSet.GetLayout();
    const PipelineDescriptorRangeDesc* range = layout.FindRangeByRootParameterIndex(rootParameterIndex);
    Assert(range != nullptr, "Pipeline descriptor set binding was not found.");

    const PipelineBoundResource* boundResource = descriptorSet.FindBoundResource(rootParameterIndex);
    if (boundResource == nullptr)
    {
        return;
    }

    if (range->Kind == DescriptorBindingKind::ConstantBuffer)
    {
        Assert(!boundResource->ConstantBufferData.empty(), "Pipeline constant buffer is not bound.");
        auto allocation = m_CommandList.AllocateInUploadBuffer(
            boundResource->ConstantBufferData.size(),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        std::memcpy(allocation.Cpu, boundResource->ConstantBufferData.data(), boundResource->ConstantBufferData.size());
        m_CommandList.SetComputeRootConstantBufferView(rootParameterIndex, allocation.Gpu);
        return;
    }

    if (range->Kind == DescriptorBindingKind::AccelerationStructure)
    {
        const RayTracingAccelerationStructure* accelerationStructure = boundResource->AccelerationStructure != nullptr ?
            boundResource->AccelerationStructure :
            descriptorSet.GetAccelerationStructure();
        Assert(accelerationStructure != nullptr && accelerationStructure->IsBuilt(), "Pipeline acceleration structure is not bound.");
        m_CommandList.SetComputeRootShaderResourceView(rootParameterIndex, accelerationStructure->GetGpuVirtualAddress());
        return;
    }

    if (range->Kind == DescriptorBindingKind::ShaderResourceView)
    {
        if (range->BindingMode == PipelineDescriptorBindingMode::RootDescriptor)
        {
            Assert(boundResource->StructuredBufferResource != nullptr, "Pipeline root SRV structured buffer is not bound.");
            if (boundResource->StructuredBufferResource->AreAutoBarriersEnabled())
            {
                m_CommandList.TransitionBarrier(*boundResource->StructuredBufferResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            m_CommandList.SetComputeRootShaderResourceView(
                rootParameterIndex,
                boundResource->StructuredBufferResource->GetD3D12Resource()->GetGPUVirtualAddress());
            return;
        }

        const size_t shaderResourceCount = boundResource->ShaderResources.size();
        for (UINT i = 0; i < static_cast<UINT>(shaderResourceCount); ++i)
        {
            if (i >= boundResource->ShaderResources.size() || !boundResource->ShaderResources[i].has_value())
            {
                continue;
            }

            const PipelineShaderResourceBinding& shaderResource = *boundResource->ShaderResources[i];
            Assert(shaderResource.Resource != nullptr, "Pipeline SRV resource is not bound.");
            const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = shaderResource.HasDesc ? &shaderResource.Desc : nullptr;
            if (shaderResource.Resource->AreAutoBarriersEnabled())
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.StateAfter,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
            else
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
        }
        return;
    }

    if (range->Kind == DescriptorBindingKind::UnorderedAccessView)
    {
        Assert(boundResource->UnorderedAccessView.has_value(), "Pipeline UAV resource is not bound.");
        const UnorderedAccessView& unorderedAccessView = *boundResource->UnorderedAccessView;
        if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        else
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
    }
}

void CommandContext::StageDynamicDescriptors(
    const D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType,
    const UINT rootParameterIndex,
    const UINT offset,
    const UINT numDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE baseDescriptor) const
{
    m_CommandList.StageDynamicDescriptors(descriptorHeapType, rootParameterIndex, offset, numDescriptors, baseDescriptor);
}

void CommandContext::TransitionShaderResource(const Resource& resource) const
{
    if (resource.AreAutoBarriersEnabled())
    {
        m_CommandList.TransitionBarrier(resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}

void CommandContext::TransitionUnorderedAccess(const Resource& resource) const
{
    if (resource.AreAutoBarriersEnabled())
    {
        m_CommandList.TransitionBarrier(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}

void CommandContext::UavBarrier(const Resource& resource) const
{
    m_CommandList.UavBarrier(resource);
}

void CommandContext::Draw(
    const uint32_t vertexCount,
    const uint32_t instanceCount,
    const uint32_t startVertex,
    const uint32_t startInstance) const
{
    m_CommandList.Draw(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandContext::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
    m_CommandList.Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::DispatchRays(
    RayTracingBindingSet& bindingSet,
    std::string_view passName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    RayTracingBindingSet::Impl& bindingImpl = bindingSet.GetImpl();
    const RayTracingShader::Impl& shaderImpl = bindingImpl.GetShaderImpl();
    const RayTracingPipelineDesc& desc = shaderImpl.Desc;
    const RayTracingShaderPassDesc& pass = bindingImpl.DispatchTables.ResolvePass(desc, passName);
    Assert(!pass.RayGenerationShader.empty(), "Ray tracing pass requires a ray generation shader.");
    Assert(bindingImpl.DescriptorSet.GetAccelerationStructure() != nullptr, "Ray tracing acceleration structure is not bound.");

    bindingImpl.DispatchTables.EnsureBuilt(*shaderImpl.PipelineState, pass);
    bindingImpl.DescriptorTable.EnsureBuilt(desc, bindingImpl.DescriptorSet);

    bindingImpl.DescriptorTable.TransitionResources(*this, desc, bindingImpl.DescriptorSet);
    BindPipeline(bindingImpl.Shader);
    bindingImpl.DescriptorTable.Stage(*this, desc);

    for (uint32_t bindingIndex = 0; bindingIndex < desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[bindingIndex];
        if (RayTracingShaderInternal::IsDescriptorTableBinding(binding.Type))
        {
            continue;
        }

        ApplyComputeBinding(bindingImpl.DescriptorSet, bindingIndex);
    }

    DispatchRays(bindingImpl.DispatchTables.BuildDispatchDesc(width, height, depth));
    bindingImpl.DescriptorTable.InsertOutputBarriers(*this, desc, bindingImpl.DescriptorSet);
}

void CommandContext::DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc) const
{
    m_CommandList.DispatchRays(dispatchRaysDesc);
}

//Modify End
