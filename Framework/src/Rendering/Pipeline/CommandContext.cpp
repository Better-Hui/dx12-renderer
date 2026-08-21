//Modify Begin:2026-08-21 by Hui

#include <Framework/Rendering/Pipeline/CommandContext.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandListInternalAccess.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <Framework/Rendering/RayTracing/RayTracingPipelineStateBuilder.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

namespace
{
    void LogMissingConstantBufferOnce(const std::string_view name)
    {
        static std::mutex mutex;
        static std::unordered_set<std::string> reported;
        const std::string key(name);
        std::lock_guard<std::mutex> lock(mutex);
        if (reported.insert(key).second)
        {
            const std::string message = "CommandContext: skipped missing constant buffer '" + key + "'.\n";
            OutputDebugStringA(message.c_str());
        }
    }

    void TransitionShaderResourceBinding(CommandList& commandList, const PipelineShaderResourceBinding& shaderResource)
    {
        if (shaderResource.Resource == nullptr || !shaderResource.Resource->AreAutoBarriersEnabled())
        {
            return;
        }
        if (shaderResource.NumSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            for (UINT i = 0; i < shaderResource.NumSubresources; ++i)
            {
                commandList.TransitionBarrier(*shaderResource.Resource, shaderResource.StateAfter, shaderResource.FirstSubresource + i);
            }
        }
        else
        {
            commandList.TransitionBarrier(*shaderResource.Resource, shaderResource.StateAfter);
        }
    }

    void TransitionUnorderedAccessView(CommandList& commandList, const UnorderedAccessView& unorderedAccessView)
    {
        if (unorderedAccessView.m_Resource == nullptr || !unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            return;
        }
        if (unorderedAccessView.m_NumSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            for (UINT i = 0; i < unorderedAccessView.m_NumSubresources; ++i)
            {
                commandList.TransitionBarrier(*unorderedAccessView.m_Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, unorderedAccessView.m_FirstSubresource + i);
            }
        }
        else
        {
            commandList.TransitionBarrier(*unorderedAccessView.m_Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    void InsertDescriptorSetOutputBarriersImpl(CommandList& commandList, const PipelineDescriptorSet& descriptorSet)
    {
        for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
        {
            (void)rootParameterIndex;
            if (boundResource.UnorderedAccessView.has_value() &&
                boundResource.UnorderedAccessView->m_Resource != nullptr &&
                boundResource.UnorderedAccessView->m_Resource->AreAutoBarriersEnabled())
            {
                commandList.UavBarrier(*boundResource.UnorderedAccessView->m_Resource);
            }
        }
    }

    uint64_t HashDescriptorSet(const PipelineDescriptorSet& descriptorSet)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto combine = [&hash](const uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        combine(descriptorSet.GetSetIndex());
        for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
        {
            combine(rootParameterIndex);
            combine(boundResource.ConstantBufferData.size());
            combine(reinterpret_cast<uintptr_t>(boundResource.UnorderedAccessViewResourceIdentity));
            combine(reinterpret_cast<uintptr_t>(boundResource.AccelerationStructure));
            for (const auto& shaderResource : boundResource.ShaderResources)
            {
                combine(shaderResource.has_value()
                    ? reinterpret_cast<uintptr_t>(shaderResource->ResourceIdentity)
                    : 0u);
            }
            if (const PipelineDescriptorTableAllocation* allocation =
                descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                combine(allocation->GetRevision());
            }
        }
        return hash;
    }

    bool ShouldEmitDescriptorSetTelemetry(
        const PipelineBindPoint bindPoint,
        const PipelineDescriptorSet& descriptorSet)
    {
        static std::mutex mutex;
        static std::map<std::pair<uintptr_t, uint32_t>, uint64_t> revisions;
        const uint64_t revision = HashDescriptorSet(descriptorSet);
        std::lock_guard lock(mutex);
        const auto key = std::pair(
            reinterpret_cast<uintptr_t>(&descriptorSet),
            static_cast<uint32_t>(bindPoint));
        const auto existing = revisions.find(key);
        if (existing != revisions.end() && existing->second == revision)
        {
            return false;
        }
        revisions.insert_or_assign(key, revision);
        return true;
    }

    const char* GetBindPointName(const PipelineBindPoint bindPoint)
    {
        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics: return "Graphics";
        case PipelineBindPoint::Compute: return "Compute";
        case PipelineBindPoint::RayTracing: return "RayTracing";
        default: return "Unknown";
        }
    }

    void EmitDescriptorSetTelemetry(
        const PipelineBindPoint bindPoint,
        const PipelineDescriptorSet& descriptorSet)
    {
        FrameworkDeviceContext& deviceContext = descriptorSet.GetLayout().GetDeviceContext();
        if (!deviceContext.HasDiagnosticTelemetrySink() ||
            !ShouldEmitDescriptorSetTelemetry(bindPoint, descriptorSet))
        {
            return;
        }

        std::vector<DiagnosticTelemetryField> fields = {
            { "bind_point", std::string(GetBindPointName(bindPoint)) },
            { "set_index", static_cast<uint64_t>(descriptorSet.GetSetIndex()) },
            { "resource_descriptor_offset", static_cast<uint64_t>(descriptorSet.GetResourceDescriptorOffset()) },
            { "sampler_descriptor_offset", static_cast<uint64_t>(descriptorSet.GetSamplerDescriptorOffset()) },
            { "bound_root_parameter_count", static_cast<uint64_t>(descriptorSet.GetBoundResources().size()) },
        };
        size_t bindingIndex = 0;
        for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
        {
            const std::string prefix = "binding." + std::to_string(bindingIndex++);
            fields.push_back({ prefix + ".root_parameter", static_cast<uint64_t>(rootParameterIndex) });
            fields.push_back({ prefix + ".srv_count", static_cast<uint64_t>(boundResource.ShaderResources.size()) });
            fields.push_back({ prefix + ".has_uav", boundResource.UnorderedAccessView.has_value() });
            fields.push_back({ prefix + ".constant_buffer_bytes", static_cast<uint64_t>(boundResource.ConstantBufferData.size()) });
            fields.push_back({ prefix + ".has_acceleration_structure", boundResource.AccelerationStructure != nullptr });
            fields.push_back({
                prefix + ".uav_resource_identity",
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(boundResource.UnorderedAccessViewResourceIdentity)),
            });
            if (const PipelineDescriptorTableAllocation* allocation =
                descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                fields.push_back({ prefix + ".table_heap_offset", static_cast<uint64_t>(allocation->HeapOffset) });
                fields.push_back({ prefix + ".table_descriptor_count", static_cast<uint64_t>(allocation->GetNumHandles()) });
                fields.push_back({ prefix + ".table_revision", allocation->GetRevision() });
            }
            for (size_t resourceIndex = 0; resourceIndex < boundResource.ShaderResources.size(); ++resourceIndex)
            {
                const auto& shaderResource = boundResource.ShaderResources[resourceIndex];
                if (shaderResource.has_value())
                {
                    fields.push_back({
                        prefix + ".srv." + std::to_string(resourceIndex) + ".resource_identity",
                        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(shaderResource->ResourceIdentity)),
                    });
                }
            }
        }
        deviceContext.RecordDiagnosticTelemetry({
            .Category = "descriptor.binding",
            .Name = "set_descriptor_set",
            .CorrelationId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&descriptorSet)),
            .Fields = std::move(fields),
        });
    }

}

CommandContext::CommandContext(CommandList& commandList)
    : m_CommandList(commandList)
{
}

void CommandContext::SetPipelineLayout(const PipelineBindPoint bindPoint, const PipelineLayout& pipelineLayout) const
{
    const RootSignature* rootSignature = pipelineLayout.GetRootSignature();
    Assert(rootSignature != nullptr, "Pipeline layout does not have a root signature.");

    if (bindPoint == PipelineBindPoint::Graphics)
    {
        SetGraphicsRootSignature(*rootSignature);
    }
    else
    {
        SetComputeRootSignature(*rootSignature);
    }
    m_DescriptorAllocator.ResetTransientBindings();
    (void)pipelineLayout;
}

void CommandContext::SetDescriptorPool(const PipelineDescriptorPool& descriptorPool) const
{
    m_DescriptorPool = &descriptorPool;
}

void CommandContext::SetDescriptorSet(
    const PipelineBindPoint bindPoint,
    const PipelineDescriptorSetBindDesc& descriptorSetDesc) const
{
    Assert(descriptorSetDesc.DescriptorSet != nullptr, "Pipeline descriptor set bind desc has no descriptor set.");
    const PipelineDescriptorSet& descriptorSet = *descriptorSetDesc.DescriptorSet;
    if (const PipelineDescriptorPool* descriptorPool = descriptorSet.GetDescriptorPool())
    {
        SetDescriptorPool(*descriptorPool);
    }
    Assert(
        descriptorSetDesc.SetIndex == descriptorSet.GetSetIndex(),
        "Pipeline descriptor set bind desc set index does not match the allocated descriptor set.");
    Assert(
        descriptorSetDesc.SetIndex < MaxDescriptorSetSlots,
        "Pipeline descriptor set index exceeds CommandContext descriptor set slots.");
    m_DescriptorSets[descriptorSetDesc.SetIndex] = &descriptorSet;
    SetDescriptorSet(bindPoint, descriptorSet);
}

void CommandContext::SetDescriptorSet(const PipelineBindPoint bindPoint, const PipelineDescriptorSet& descriptorSet) const
{
    EmitDescriptorSetTelemetry(bindPoint, descriptorSet);
    std::set<UINT> appliedRootParameters;
    for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
    {
        (void)boundResource;
        appliedRootParameters.insert(rootParameterIndex);
        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics:
            ApplyGraphicsBinding(descriptorSet, rootParameterIndex);
            break;
        case PipelineBindPoint::Compute:
        case PipelineBindPoint::RayTracing:
            ApplyComputeBinding(descriptorSet, rootParameterIndex);
            break;
        default:
            Assert(false, "Unsupported pipeline bind point.");
            break;
        }
    }

    for (const PipelineDescriptorRangeDesc& range : descriptorSet.GetLayout().GetDesc().DescriptorRanges)
    {
        if (range.BindingMode != PipelineDescriptorBindingMode::DescriptorTable ||
            appliedRootParameters.find(range.RootParameterIndex) != appliedRootParameters.end())
        {
            continue;
        }

        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics:
            ApplyGraphicsBinding(descriptorSet, range.RootParameterIndex);
            break;
        case PipelineBindPoint::Compute:
        case PipelineBindPoint::RayTracing:
            ApplyComputeBinding(descriptorSet, range.RootParameterIndex);
            break;
        default:
            Assert(false, "Unsupported pipeline bind point.");
            break;
        }
    }
}

void CommandContext::SetPipeline(Shader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Graphics;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto& device = shader.GetDeviceContext().GetDevice();
    const auto& renderTargetState = m_CommandList.GetLastRenderTargetState();
    const auto pipelineState = shader.GetPipelineState(device, renderTargetState);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Graphics, *shader.GetPipelineLayout());
    }
    else
    {
        SetGraphicsRootSignature(shader.GetRootSignature());
        shader.StageDefaultDescriptorTables(m_CommandList);
    }

    SetGraphicsPipelineState(pipelineState);
}

void CommandContext::SetPipeline(MeshShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Graphics;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto& device = shader.GetDeviceContext().GetDevice();
    const auto& renderTargetState = m_CommandList.GetLastRenderTargetState();
    const auto pipelineState = shader.GetPipelineState(device, renderTargetState);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Graphics, *shader.GetPipelineLayout());
    }
    else
    {
        SetGraphicsRootSignature(shader.GetRootSignature());
    }

    SetGraphicsPipelineState(pipelineState);
}

void CommandContext::SetPipeline(const ComputeShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Compute;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto& device = shader.GetDeviceContext().GetDevice();
    const auto pipelineState = shader.GetPipelineState(device);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Compute, *shader.GetPipelineLayout());
    }
    else
    {
        SetComputeRootSignature(shader.GetRootSignature());
        shader.StageDefaultDescriptorTables(m_CommandList);
    }
    SetComputePipelineState(pipelineState);
}

void CommandContext::SetPipeline(const RayTracingShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::RayTracing;
    m_HasBoundPipeline = true;

    const RayTracingPipelineState& pipelineState = shader.GetPipelineState();
    SetRayTracingPipelineState(pipelineState.GetStateObject(), pipelineState.GetGlobalRootSignature());
    SetPipelineLayout(PipelineBindPoint::RayTracing, shader.GetPipelineLayout());
    m_BoundRayTracingShader = &shader;
}

void CommandContext::BindPipeline(Shader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(MeshShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(const ComputeShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(const RayTracingShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindBindlessDescriptorHeap(
    BindlessDescriptorHeap& bindlessDescriptorHeap) const
{
    m_CommandList.BindExternalDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        bindlessDescriptorHeap.GetResourceDescriptorHeap());
    m_DescriptorAllocator.SetBindlessDescriptorHeap(&bindlessDescriptorHeap);
}

void CommandContext::BindDescriptorSet(const PipelineDescriptorSetBindDesc& descriptorSetDesc) const
{
    Assert(m_HasBoundPipeline, "A pipeline must be bound before binding a descriptor set.");
    SetDescriptorSet(m_BoundPipelineBindPoint, descriptorSetDesc);
}

void CommandContext::BindDescriptorSet(const PipelineDescriptorSet& descriptorSet) const
{
    BindDescriptorSet(PipelineDescriptorSetBindDesc{ descriptorSet.GetSetIndex(), &descriptorSet });
}

void CommandContext::SetConstantBuffer(Shader& shader, const std::string_view name, const size_t size, const void* data) const
{
    if (!shader.HasConstantBuffer(std::string(name)))
    {
        LogMissingConstantBufferOnce(name);
        return;
    }
    shader.m_DescriptorSet->SetConstantBufferData(name, data, size);
}

void CommandContext::SetShaderResourceView(Shader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(name, 0u, shaderResourceView);
}

void CommandContext::SetShaderResourceView(Shader& shader, const std::string_view name, const uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(name, arrayIndex, shaderResourceView);
}

void CommandContext::SetShaderResourceViews(Shader& shader, const std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const
{
    shader.m_DescriptorSet->SetShaderResourceViews(name, shaderResourceViews);
}

void CommandContext::SetShaderResource(Shader& shader, const std::string_view name, const Resource& resource, const D3D12_RESOURCE_STATES stateAfter) const
{
    shader.m_DescriptorSet->SetShaderResource(name, 0u, resource, stateAfter);
}

void CommandContext::SetStructuredBuffer(Shader& shader, const std::string_view name, const StructuredBuffer& buffer) const
{
    shader.m_DescriptorSet->SetStructuredBuffer(name, buffer);
}

void CommandContext::SetTexture(Shader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(shader, name, shaderResourceView);
}

void CommandContext::SetTexture(Shader& shader, const std::string_view name, const std::shared_ptr<Resource>& texture) const
{
    SetTexture(shader, name, ShaderResourceView(texture));
}

void CommandContext::SetConstantBuffer(MeshShader& shader, const std::string_view name, const size_t size, const void* data) const
{
    shader.m_DescriptorSet->SetConstantBufferData(name, data, size);
}

void CommandContext::SetShaderResourceView(MeshShader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(name, 0u, shaderResourceView);
}

void CommandContext::SetShaderResourceView(MeshShader& shader, const std::string_view name, const uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(name, arrayIndex, shaderResourceView);
}

void CommandContext::SetShaderResourceViews(MeshShader& shader, const std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const
{
    shader.m_DescriptorSet->SetShaderResourceViews(name, shaderResourceViews);
}

void CommandContext::SetShaderResource(MeshShader& shader, const std::string_view name, const Resource& resource, const D3D12_RESOURCE_STATES stateAfter) const
{
    shader.m_DescriptorSet->SetShaderResource(name, 0u, resource, stateAfter);
}

void CommandContext::SetStructuredBuffer(MeshShader& shader, const std::string_view name, const StructuredBuffer& buffer) const
{
    shader.m_DescriptorSet->SetStructuredBuffer(name, buffer);
}

void CommandContext::SetTexture(MeshShader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(shader, name, shaderResourceView);
}

void CommandContext::SetTexture(MeshShader& shader, const std::string_view name, const std::shared_ptr<Resource>& texture) const
{
    SetTexture(shader, name, ShaderResourceView(texture));
}

void CommandContext::SetStructuredBuffer(const ComputeShader& shader, const std::string_view name, const StructuredBuffer& buffer) const
{
    shader.m_DescriptorSet->SetStructuredBuffer(
        name,
        buffer,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void CommandContext::SetConstantBuffer(const ComputeShader& shader, const std::string_view name, const size_t size, const void* data) const
{
    if (!shader.HasConstantBuffer(std::string(name)))
    {
        LogMissingConstantBufferOnce(name);
        return;
    }
    shader.m_DescriptorSet->SetConstantBufferData(name, data, size);
}

void CommandContext::SetShaderResourceView(const ComputeShader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(
        name,
        0u,
        shaderResourceView,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void CommandContext::SetShaderResourceView(const ComputeShader& shader, const std::string_view name, const uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    shader.m_DescriptorSet->SetShaderResourceView(
        name,
        arrayIndex,
        shaderResourceView,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void CommandContext::SetShaderResource(
    const ComputeShader& shader,
    const std::string_view name,
    const uint32_t arrayIndex,
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter) const
{
    shader.m_DescriptorSet->SetShaderResource(name, arrayIndex, resource, stateAfter);
}

void CommandContext::SetShaderResourceViews(const ComputeShader& shader, const std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const
{
    shader.m_DescriptorSet->SetShaderResourceViews(
        name,
        shaderResourceViews,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void CommandContext::SetTexture(const ComputeShader& shader, const std::string_view name, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(shader, name, shaderResourceView);
}

void CommandContext::SetTexture(const ComputeShader& shader, const std::string_view name, const std::shared_ptr<Resource>& texture) const
{
    SetTexture(shader, name, ShaderResourceView(texture));
}

void CommandContext::SetUnorderedAccessView(const ComputeShader& shader, const std::string_view name, const UnorderedAccessView& unorderedAccessView) const
{
    shader.m_DescriptorSet->SetUnorderedAccessView(name, unorderedAccessView);
}

void CommandContext::SetAccelerationStructure(const ComputeShader& shader, const std::string_view name, const RayTracingAccelerationStructure& accelerationStructure) const
{
    shader.m_DescriptorSet->SetAccelerationStructure(name, accelerationStructure);
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

void CommandContext::StageDefaultDescriptorTable(
    const PipelineBindPoint bindPoint,
    const PipelineDescriptorSet& descriptorSet,
    const UINT rootParameterIndex) const
{
    if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
    {
        m_DescriptorAllocator.StageDescriptorTable(m_CommandList, bindPoint, rootParameterIndex, *allocation);
    }
}

bool CommandContext::TryApplyDescriptorTableBinding(
    const PipelineBindPoint bindPoint,
    const PipelineDescriptorSet& descriptorSet,
    const PipelineDescriptorRangeDesc& range,
    const PipelineBoundResource& boundResource,
    const UINT rootParameterIndex) const
{
    const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex);
    if (allocation == nullptr)
    {
        return false;
    }

    switch (range.Kind)
    {
    case DescriptorBindingKind::ShaderResourceView:
        for (const auto& shaderResource : boundResource.ShaderResources)
        {
            if (!shaderResource.has_value())
            {
                continue;
            }

            Assert(shaderResource->Resource != nullptr, "Pipeline SRV resource is not bound.");
            if (shaderResource->AutoTransition && shaderResource->Resource->AreAutoBarriersEnabled())
            {
                TransitionShaderResourceBinding(m_CommandList, *shaderResource);
            }
        }
        break;
    case DescriptorBindingKind::UnorderedAccessView:
        Assert(boundResource.UnorderedAccessView.has_value(), "Pipeline UAV resource is not bound.");
        Assert(boundResource.UnorderedAccessView->m_Resource != nullptr, "Pipeline UAV resource is not bound.");
        if (boundResource.UnorderedAccessView->m_Resource->AreAutoBarriersEnabled())
        {
            TransitionUnorderedAccessView(m_CommandList, *boundResource.UnorderedAccessView);
        }
        break;
    default:
        return false;
    }

    m_DescriptorAllocator.StageDescriptorTable(m_CommandList, bindPoint, rootParameterIndex, *allocation);
    return true;
}

void CommandContext::ApplyGraphicsBinding(const PipelineDescriptorSet& descriptorSet, const UINT rootParameterIndex) const
{
    const PipelineLayout& layout = descriptorSet.GetLayout();
    const PipelineDescriptorRangeDesc* range = layout.FindRangeByRootParameterIndex(rootParameterIndex);
    Assert(range != nullptr, "Pipeline descriptor set binding was not found.");

    const PipelineBoundResource* boundResource = descriptorSet.FindBoundResource(rootParameterIndex);
    if (boundResource == nullptr)
    {
        if (range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable)
        {
            StageDefaultDescriptorTable(PipelineBindPoint::Graphics, descriptorSet, rootParameterIndex);
        }
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

        if (TryApplyDescriptorTableBinding(
                PipelineBindPoint::Graphics,
                descriptorSet,
                *range,
                *boundResource,
                rootParameterIndex))
        {
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
            if (shaderResource.AutoTransition && shaderResource.Resource->AreAutoBarriersEnabled())
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
        if (TryApplyDescriptorTableBinding(
                PipelineBindPoint::Graphics,
                descriptorSet,
                *range,
                *boundResource,
                rootParameterIndex))
        {
            return;
        }
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
        if (range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable)
        {
            StageDefaultDescriptorTable(PipelineBindPoint::Compute, descriptorSet, rootParameterIndex);
        }
        return;
    }

    if (range->Kind == DescriptorBindingKind::ConstantBuffer)
    {
        Assert(!boundResource->ConstantBufferData.empty(), "Pipeline constant buffer is not bound.");
        auto allocation = CommandListInternalAccess::AllocateTransientUpload(
            m_CommandList,
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

        if (TryApplyDescriptorTableBinding(
                PipelineBindPoint::Compute,
                descriptorSet,
                *range,
                *boundResource,
                rootParameterIndex))
        {
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
            if (shaderResource.AutoTransition && shaderResource.Resource->AreAutoBarriersEnabled())
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
        if (TryApplyDescriptorTableBinding(
                PipelineBindPoint::Compute,
                descriptorSet,
                *range,
                *boundResource,
                rootParameterIndex))
        {
            return;
        }
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

void CommandContext::InsertDescriptorSetOutputBarriers(const PipelineDescriptorSet& descriptorSet) const
{
    InsertDescriptorSetOutputBarriersImpl(m_CommandList, descriptorSet);
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

void CommandContext::DrawIndexed(
    const uint32_t indexCount,
    const uint32_t instanceCount,
    const uint32_t startIndex,
    const int32_t baseVertex,
    const uint32_t startInstance) const
{
    m_CommandList.DrawIndexed(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void CommandContext::ExecuteIndirect(
    const IndirectCommandSignature& commandSignature,
    const IndirectCommandExecutionDesc& executionDesc) const
{
    Assert(executionDesc.ArgumentBuffer != nullptr, "Indirect execution requires an argument buffer.");
    Assert(executionDesc.MaxCommandCount > 0u, "Indirect execution requires a positive maximum command count.");
    m_CommandList.ExecuteIndirect(
        commandSignature.GetD3D12CommandSignature(),
        commandSignature.GetD3D12ExecutionArgumentType(),
        executionDesc.MaxCommandCount,
        *executionDesc.ArgumentBuffer,
        executionDesc.ArgumentBufferOffset,
        executionDesc.CountBuffer,
        executionDesc.CountBufferOffset);
}

void CommandContext::DrawIndirect(
    const IndirectCommandSignature& commandSignature,
    const IndirectCommandExecutionDesc& executionDesc) const
{
    const IndirectArgumentType argumentType = commandSignature.GetExecutionArgumentType();
    Assert(
        argumentType == IndirectArgumentType::Draw || argumentType == IndirectArgumentType::DrawIndexed,
        "DrawIndirect requires a draw or indexed-draw command signature.");
    ExecuteIndirect(commandSignature, executionDesc);
}

void CommandContext::DispatchIndirect(
    const IndirectCommandSignature& commandSignature,
    const IndirectCommandExecutionDesc& executionDesc) const
{
    Assert(
        commandSignature.GetExecutionArgumentType() == IndirectArgumentType::Dispatch,
        "DispatchIndirect requires a dispatch command signature.");
    ExecuteIndirect(commandSignature, executionDesc);
}

void CommandContext::DispatchMeshIndirect(
    const IndirectCommandSignature& commandSignature,
    const IndirectCommandExecutionDesc& executionDesc) const
{
    Assert(
        commandSignature.GetExecutionArgumentType() == IndirectArgumentType::DispatchMesh,
        "DispatchMeshIndirect requires a mesh-dispatch command signature.");
    ExecuteIndirect(commandSignature, executionDesc);
}

void CommandContext::DispatchRaysIndirect(
    const IndirectCommandSignature& commandSignature,
    const IndirectCommandExecutionDesc& executionDesc) const
{
    Assert(m_BoundRayTracingShader != nullptr, "Ray tracing pipeline must be bound before DispatchRaysIndirect.");
    Assert(
        commandSignature.GetExecutionArgumentType() == IndirectArgumentType::DispatchRays,
        "DispatchRaysIndirect requires a ray-dispatch command signature.");
    Assert(executionDesc.MaxCommandCount == 1u, "DispatchRaysIndirect supports exactly one dispatch description.");
    Assert(executionDesc.CountBuffer == nullptr, "DispatchRaysIndirect does not support a count buffer.");
    ExecuteIndirect(commandSignature, executionDesc);
}

D3D12_DISPATCH_RAYS_DESC CommandContext::BuildDispatchRaysArguments(
    const RayTracingDispatchDesc& dispatchDesc) const
{
    Assert(m_BoundRayTracingShader != nullptr, "Ray tracing pipeline must be bound before building indirect dispatch arguments.");
    return m_BoundRayTracingShader->BuildDispatchDesc(
        dispatchDesc.PassName,
        dispatchDesc.Width,
        dispatchDesc.Height,
        dispatchDesc.Depth);
}

void CommandContext::ClearUnorderedAccessUint(const Resource& resource, const UINT values[4]) const
{
    m_CommandList.ClearUnorderedAccessUint(resource, values);
}

void CommandContext::DispatchMesh(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
    m_CommandList.DispatchMesh(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
    m_CommandList.Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::BindDescriptorSet(const RayTracingBindingSet& bindingSet) const
{
    const RayTracingShader& shader = bindingSet.GetShader();
    const PipelineDescriptorSet& descriptorSet = bindingSet.GetDescriptorSet();
    Assert(descriptorSet.GetAccelerationStructure() != nullptr, "Ray tracing acceleration structure is not bound.");

    if (m_BoundRayTracingShader != &shader)
    {
        SetPipeline(shader);
    }
    SetDescriptorPool(bindingSet.GetDescriptorPool());
    SetDescriptorSet(PipelineBindPoint::RayTracing, PipelineDescriptorSetBindDesc{ descriptorSet.GetSetIndex(), &descriptorSet });
}

void CommandContext::DispatchRays(const RayTracingDispatchDesc& dispatchDesc) const
{
    Assert(m_BoundRayTracingShader != nullptr, "Ray tracing pipeline must be bound before DispatchRays.");
    const D3D12_DISPATCH_RAYS_DESC d3d12DispatchDesc = m_BoundRayTracingShader->BuildDispatchDesc(
        dispatchDesc.PassName,
        dispatchDesc.Width,
        dispatchDesc.Height,
        dispatchDesc.Depth);
    m_CommandList.DispatchRays(d3d12DispatchDesc);
}

void CommandContext::InsertDescriptorSetOutputBarriers(const RayTracingBindingSet& bindingSet) const
{
    InsertDescriptorSetOutputBarriers(bindingSet.GetDescriptorSet());
}

//Modify End
