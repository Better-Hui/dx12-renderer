#include "DX12LibPCH.h"

#include "CommandList.h"

#include "ConstantBuffer.h"
#include "D3D12DeviceContext.h"
#include "DynamicDescriptorHeap.h"
#include "IndexBuffer.h"
#include "RenderTarget.h"
#include "Resource.h"
#include "ResourceStateTracker.h"
#include "RootSignature.h"
#include "UploadBuffer.h"
#include "VertexBuffer.h"

#include <d3d12.h>

//Modify Begin:2026-08-25 by Hui
namespace
{
    std::atomic_uint64_t g_NextCommandListStableId = 1u;
}

CommandList::CommandList(
    const D3D12_COMMAND_LIST_TYPE type,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_D3d12CommandListType(type)
    , m_StableId(g_NextCommandListStableId.fetch_add(1u, std::memory_order_relaxed))
    , m_DeviceContext(std::move(deviceContext))
    , m_Device(m_DeviceContext != nullptr ? m_DeviceContext->GetDevice() : nullptr)
    , m_ResourceStateRegistry(m_DeviceContext != nullptr ? m_DeviceContext->GetResourceStateRegistry() : nullptr)
{
    Assert(m_DeviceContext != nullptr, "D3D12 device context is null.");
    Assert(m_Device != nullptr, "D3D12 device is null.");
    Assert(m_ResourceStateRegistry != nullptr, "Resource state registry is null.");

    ThrowIfFailed(m_Device->CreateCommandAllocator(m_D3d12CommandListType, IID_PPV_ARGS(&m_D3d12CommandAllocator)));

    ThrowIfFailed(m_Device->CreateCommandList(0, m_D3d12CommandListType, m_D3d12CommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_D3d12CommandList)));

    ThrowIfFailed(m_D3d12CommandList.As(&m_D3d12CommandList5));
    ThrowIfFailed(m_D3d12CommandList.As(&m_D3d12CommandList6));

    const std::wstring commandListName = L"CommandList #" + std::to_wstring(m_StableId);
    const std::wstring allocatorName = commandListName + L" Allocator";
    ThrowIfFailed(m_D3d12CommandList->SetName(commandListName.c_str()));
    ThrowIfFailed(m_D3d12CommandAllocator->SetName(allocatorName.c_str()));

    m_PUploadBuffer = std::make_unique<UploadBuffer>(m_Device);

    m_PResourceStateTracker = std::make_unique<ResourceStateTracker>(m_ResourceStateRegistry);

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        const uint32_t numDescriptorsPerHeap =
            i == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? 8192u : 1024u;
        m_DynamicDescriptorHeaps[i] = std::make_unique<DynamicDescriptorHeap>(
            m_Device,
            static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i),
            numDescriptorsPerHeap);
        m_DescriptorHeaps[i] = nullptr;
    }
}
//Modify End

CommandList::~CommandList() = default;

//Modify Begin:2026-08-24 by Hui
void CommandList::ExecuteExternalCommandRecording(
    const std::function<void(ID3D12GraphicsCommandList2&)>& recordCommands)
{
    Assert(static_cast<bool>(recordCommands), "External command-recording callback is empty.");

    FlushResourceBarriers();
    CommitStagedDescriptors();
    try
    {
        recordCommands(*m_D3d12CommandList.Get());
    }
    catch (...)
    {
        InvalidateCachedNativeState();
        throw;
    }

    InvalidateCachedNativeState();
}

void CommandList::FlushResourceBarriers()
{
    m_PResourceStateTracker->FlushResourceBarriers(*this);
}
//Modify End

//Modify Begin:2026-07-30 by Hui
void CommandList::NotifyResourceState(
    const Resource& resource,
    const D3D12_RESOURCE_STATES state,
    const UINT subresource)
{
    const ComPtr<ID3D12Resource> d3d12Resource = resource.GetD3D12Resource();
    Assert(d3d12Resource != nullptr, "Cannot notify the state of an uninitialized resource.");
    m_PResourceStateTracker->NotifyResourceState(d3d12Resource.Get(), state, subresource);
}

void CommandList::TrackResourceState(
    const ComPtr<ID3D12Resource> resource,
    std::shared_ptr<ResourceStateRegistration> stateRegistration)
{
    Assert(resource != nullptr, "Cannot track a null D3D12 resource state.");
    Assert(stateRegistration != nullptr, "D3D12 resource has no state registration.");
    TrackObject(resource);
    m_TrackedResourceStateRegistrations.push_back(std::move(stateRegistration));
}

void CommandList::RetireResourceState(const ComPtr<ID3D12Resource> resource)
{
    Assert(resource != nullptr, "Cannot retire a null D3D12 resource state.");
    TrackResourceState(
        resource,
        m_ResourceStateRegistry->AcquireResource(resource.Get(), D3D12_RESOURCE_STATE_COMMON));
}

void CommandList::RetireResource(Resource& resource)
{
    const ComPtr<ID3D12Resource> d3d12Resource = resource.GetD3D12Resource();
    if (d3d12Resource == nullptr)
    {
        return;
    }

    TrackResourceState(d3d12Resource, resource.GetStateRegistration());
}
//Modify End

void CommandList::CommitStagedDescriptors()
{
    CommitStagedDescriptorsForDraw();
    CommitStagedDescriptorsForDispatch();
}

//Modify Begin:2026-08-12 by Hui
void CommandList::CommitStagedDescriptorsForDraw()
{
    for (uint32_t heapIndex = 0; heapIndex < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++heapIndex)
    {
        m_DynamicDescriptorHeaps[heapIndex]->CommitStagedDescriptorsForDraw(*this);
    }
}

void CommandList::CommitStagedDescriptorsForDispatch()
{
    for (uint32_t heapIndex = 0; heapIndex < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++heapIndex)
    {
        m_DynamicDescriptorHeaps[heapIndex]->CommitStagedDescriptorsForDispatch(*this);
    }
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::CopyResource(const Resource& dstRes, const Resource& srcRes)
{
    CopyResource(dstRes.GetD3D12Resource(), srcRes.GetD3D12Resource());
}

void CommandList::CopyResource(
    const ComPtr<ID3D12Resource> dstRes,
    const ComPtr<ID3D12Resource> srcRes)
{
    Assert(dstRes != nullptr, "Copy destination resource must not be null.");
    Assert(srcRes != nullptr, "Copy source resource must not be null.");
    FlushResourceBarriers();

    m_D3d12CommandList->CopyResource(dstRes.Get(), srcRes.Get());

    TrackObject(dstRes);
    TrackObject(srcRes);
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::CopyBufferRegion(
    const Resource& destination,
    const uint64_t destinationOffset,
    const ComPtr<ID3D12Resource> source,
    const uint64_t sourceOffset,
    const uint64_t sizeInBytes)
{
    Assert(source != nullptr, "Copy source buffer must not be null.");
    Assert(destination.GetD3D12Resource() != nullptr, "Copy destination buffer must not be null.");

    FlushResourceBarriers();

    m_D3d12CommandList->CopyBufferRegion(
        destination.GetD3D12Resource().Get(),
        destinationOffset,
        source.Get(),
        sourceOffset,
        sizeInBytes);
    TrackObject(source);
    TrackResource(destination);
}

void CommandList::CopyBufferToReadback(
    const Resource& source,
    const uint64_t sourceOffset,
    const ComPtr<ID3D12Resource> destination,
    const uint64_t destinationOffset,
    const uint64_t sizeInBytes)
{
    Assert(source.GetD3D12Resource() != nullptr, "Copy source buffer must not be null.");
    Assert(destination != nullptr, "Readback destination buffer must not be null.");

    FlushResourceBarriers();

    m_D3d12CommandList->CopyBufferRegion(
        destination.Get(),
        destinationOffset,
        source.GetD3D12Resource().Get(),
        sourceOffset,
        sizeInBytes);
    TrackResource(source);
    TrackObject(destination);
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::ResolveSubresource(const Resource& dstRes, const Resource& srcRes, const uint32_t dstSubresource,
    const uint32_t srcSubresource)
{
    FlushResourceBarriers();

    m_D3d12CommandList->ResolveSubresource(dstRes.GetD3D12Resource().Get(), dstSubresource,
        srcRes.GetD3D12Resource().Get(), srcSubresource,
        dstRes.GetD3D12ResourceDesc().Format);

    TrackResource(srcRes);
    TrackResource(dstRes);
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::SetShadingRateImage(const Resource& resource)
{
    const auto d3d12Resource = resource.GetD3D12Resource();
    TrackObject(d3d12Resource);

    m_D3d12CommandList5->RSSetShadingRateImage(d3d12Resource.Get());
}
//Modify End

void CommandList::ResetShadingRateImage()
{
    m_D3d12CommandList5->RSSetShadingRateImage(nullptr);
}

void CommandList::SetShadingRate(const D3D12_SHADING_RATE& shadingRate, const D3D12_SHADING_RATE_COMBINER* combiners)
{
    m_D3d12CommandList5->RSSetShadingRate(shadingRate, combiners);
}

UploadBuffer::Allocation CommandList::AllocateInUploadBuffer(const size_t bufferSize, const size_t alignment)
{
    return m_PUploadBuffer->Allocate(bufferSize, alignment);
}

void CommandList::SetPrimitiveTopology(const D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const
{
    m_D3d12CommandList->IASetPrimitiveTopology(primitiveTopology);
}

//Modify Begin:2026-08-24 by Hui
void CommandList::ClearTexture(const Texture& texture, const float clearColor[4])
{
    m_D3d12CommandList->ClearRenderTargetView(texture.GetRenderTargetView(), clearColor, 0, nullptr);

    TrackResource(texture);
}
//Modify End

void CommandList::ClearTexture(const Texture& texture, const ClearValue& clearValue)
{
    ClearTexture(texture, clearValue.GetColor());
}

//Modify Begin:2026-08-24 by Hui
void CommandList::ClearDepthStencilTexture(const Texture& texture, const D3D12_CLEAR_FLAGS clearFlags,
    const float depth, const uint8_t stencil)
{
    m_D3d12CommandList->ClearDepthStencilView(texture.GetDepthStencilView(), clearFlags, depth, stencil, 0, nullptr);

    TrackResource(texture);
}
//Modify End

void CommandList::SetGraphicsDynamicConstantBuffer(const uint32_t rootParameterIndex, const size_t sizeInBytes,
    const void* bufferData) const
{
    const auto heapAllocation = m_PUploadBuffer->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    memcpy(heapAllocation.Cpu, bufferData, sizeInBytes);

    m_D3d12CommandList->SetGraphicsRootConstantBufferView(rootParameterIndex, heapAllocation.Gpu);
}

void CommandList::SetComputeDynamicConstantBuffer(uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData) const
{
    const auto heapAllocation = m_PUploadBuffer->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    memcpy(heapAllocation.Cpu, bufferData, sizeInBytes);

    m_D3d12CommandList->SetComputeRootConstantBufferView(rootParameterIndex, heapAllocation.Gpu);
}

void CommandList::SetGraphics32BitConstants(const uint32_t rootParameterIndex, const uint32_t numConstants,
    const void* constants)
{
    m_D3d12CommandList->SetGraphicsRoot32BitConstants(rootParameterIndex, numConstants, constants, 0);
}

void CommandList::SetCompute32BitConstants(const uint32_t rootParameterIndex, const uint32_t numConstants,
    const void* constants)
{
    m_D3d12CommandList->SetComputeRoot32BitConstants(rootParameterIndex, numConstants, constants, 0);
}

//Modify Begin:2026-08-24 by Hui
void CommandList::SetVertexBuffer(const uint32_t slot, const VertexBuffer& vertexBuffer)
{
    const auto vertexBufferView = vertexBuffer.GetVertexBufferView();

    m_D3d12CommandList->IASetVertexBuffers(slot, 1, &vertexBufferView);

    TrackResource(vertexBuffer);
}

void CommandList::SetIndexBuffer(const IndexBuffer& indexBuffer)
{
    const auto indexBufferView = indexBuffer.GetIndexBufferView();

    m_D3d12CommandList->IASetIndexBuffer(&indexBufferView);

    TrackResource(indexBuffer);
}
//Modify End

void CommandList::SetGraphicsDynamicStructuredBuffer(const uint32_t slot, const size_t numElements,
    const size_t elementSize,
    const void* bufferData) const
{
    const size_t bufferSize = numElements * elementSize;
    const auto heapAllocation = m_PUploadBuffer->Allocate(bufferSize, elementSize);
    memcpy(heapAllocation.Cpu, bufferData, bufferSize);
    m_D3d12CommandList->SetGraphicsRootShaderResourceView(slot, heapAllocation.Gpu);
}

void CommandList::SetViewport(const D3D12_VIEWPORT& viewport)
{
    SetViewports({ viewport });
}

void CommandList::SetViewports(const std::vector<D3D12_VIEWPORT>& viewports)
{
    assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_D3d12CommandList->RSSetViewports(static_cast<UINT>(viewports.size()), viewports.data());
}

void CommandList::SetScissorRect(const D3D12_RECT& scissorRect)
{
    SetScissorRects({ scissorRect });
}

void CommandList::SetScissorRects(const std::vector<D3D12_RECT>& scissorRects)
{
    assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_D3d12CommandList->RSSetScissorRects(static_cast<UINT>(scissorRects.size()), scissorRects.data());
}

void CommandList::SetPipelineState(const ComPtr<ID3D12PipelineState>& pipelineState)
{
    m_D3d12CommandList->SetPipelineState(pipelineState.Get());

    TrackObject(pipelineState);
}

//Modify Begin:2026-08-20 by Hui
void CommandList::SetGraphicsRootSignature(const RootSignature& rootSignature)
{
    const auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if (m_DescriptorTableRootSignature != d3d12RootSignature)
    {
        for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            m_DynamicDescriptorHeaps[i]->ParseRootSignature(rootSignature);
        }
        m_DescriptorTableRootSignature = d3d12RootSignature;
    }

    if (m_GraphicsRootSignature != d3d12RootSignature)
    {
        m_GraphicsRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetGraphicsRootSignature(m_GraphicsRootSignature);

        TrackObject(m_GraphicsRootSignature);
    }
}

void CommandList::SetComputeRootSignature(const RootSignature& rootSignature)
{
    const auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if (m_DescriptorTableRootSignature != d3d12RootSignature)
    {
        for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            m_DynamicDescriptorHeaps[i]->ParseRootSignature(rootSignature);
        }
        m_DescriptorTableRootSignature = d3d12RootSignature;
    }

    if (m_ComputeRootSignature != d3d12RootSignature)
    {
        m_ComputeRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetComputeRootSignature(m_ComputeRootSignature);

        TrackObject(m_ComputeRootSignature);
    }
}

void CommandList::SetGraphicsAndComputeRootSignature(const RootSignature& rootSignature)
{
    const auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if (m_DescriptorTableRootSignature != d3d12RootSignature)
    {
        for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            m_DynamicDescriptorHeaps[i]->ParseRootSignature(rootSignature);
        }
        m_DescriptorTableRootSignature = d3d12RootSignature;
    }

    if (m_GraphicsRootSignature != d3d12RootSignature)
    {
        m_GraphicsRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetGraphicsRootSignature(m_GraphicsRootSignature);
    }
    if (m_ComputeRootSignature != d3d12RootSignature)
    {
        m_ComputeRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetComputeRootSignature(m_ComputeRootSignature);
    }

    TrackObject(d3d12RootSignature);
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::SetShaderResourceView(const uint32_t rootParameterIndex, const uint32_t descriptorOffset,
    const Resource& resource,
    const UINT firstSubresource, const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    m_DynamicDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
        rootParameterIndex, descriptorOffset, 1, resource.GetShaderResourceView(srv));
    TrackResource(resource);
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::SetUnorderedAccessView(const uint32_t rootParameterIndex, const uint32_t descriptorOffset,
    const Resource& resource,
    const UINT firstSubresource, const UINT numSubresources,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc)
{
    Assert(resource.SupportsUnorderedAccess(), "Cannot bind a resource without unordered-access usage as a UAV.");
    const auto uav = resource.GetUnorderedAccessView(uavDesc);
    m_DynamicDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
        rootParameterIndex, descriptorOffset, 1, uav);
    TrackResource(resource);
}
//Modify End

//Modify Begin:2026-07-21 by Hui
void CommandList::SetGlobalTexture(
    const uint32_t rootParameterIndex,
    const uint32_t descriptorOffset,
    const Resource& texture,
    const UINT firstSubresource,
    const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    SetShaderResourceView(
        rootParameterIndex,
        descriptorOffset,
        texture,
        firstSubresource,
        numSubresources,
        srv);
}

void CommandList::SetGlobalTexture(
    const uint32_t rootParameterIndex,
    const Resource& texture,
    const UINT firstSubresource,
    const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    SetGlobalTexture(rootParameterIndex, 0, texture, firstSubresource, numSubresources, srv);
}

void CommandList::SetTexture(
    const uint32_t rootParameterIndex,
    const uint32_t descriptorOffset,
    const Resource& texture,
    const UINT firstSubresource,
    const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    SetGlobalTexture(rootParameterIndex, descriptorOffset, texture, firstSubresource, numSubresources, srv);
}

void CommandList::SetTexture(
    const uint32_t rootParameterIndex,
    const Resource& texture,
    const UINT firstSubresource,
    const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    SetGlobalTexture(rootParameterIndex, 0, texture, firstSubresource, numSubresources, srv);
}
//Modify End

void CommandList::SetStencilRef(UINT8 stencilRef)
{
    m_D3d12CommandList->OMSetStencilRef(stencilRef);
}

//Modify Begin:2026-08-24 by Hui
void CommandList::SetRenderTarget(const RenderTarget& renderTarget, UINT texArrayIndex /*= -1*/, UINT mipLevel /*= 0*/, bool useDepth /*= true*/, bool readonlyDepth)
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    renderTargetDescriptors.reserve(NumAttachmentPoints);

    const auto& textures = renderTarget.GetTextures();
    const bool isArrayItem = texArrayIndex != -1;

    // Bind color targets (max of 8 render targets can be bound to the rendering pipeline.
    for (int i = 0; i < 8; ++i)
    {
        auto& texture = textures[i];

        if (texture->IsValid())
        {
            renderTargetDescriptors.push_back(isArrayItem
                                                  ? texture->GetRenderTargetViewArray(texArrayIndex, mipLevel)
                                                  : texture->GetRenderTargetView());

            TrackResource(*texture);
        }
    }

    const auto& depthTexture = renderTarget.GetTexture(DepthStencil);

    CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
    if (useDepth && depthTexture->GetD3D12Resource())
    {
        depthStencilDescriptor = isArrayItem
                                     ? depthTexture->GetDepthStencilViewArray(texArrayIndex, mipLevel)
                                     : depthTexture->GetDepthStencilView();

        TrackResource(*depthTexture);
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

    m_D3d12CommandList->OMSetRenderTargets(static_cast<UINT>(renderTargetDescriptors.size()), renderTargetDescriptors.data(), FALSE, pDsv);

    m_LastRenderTargetState = RenderTargetState(renderTarget);
}
//Modify End

void CommandList::ClearRenderTarget(const RenderTarget& renderTarget, const float* clearColor, D3D12_CLEAR_FLAGS clearFlags)
{
    const auto& textures = renderTarget.GetTextures();

    for (int i = 0; i < 8; ++i)
    {
        auto& texture = textures[i];

        if (texture->IsValid())
        {
            ClearTexture(*texture, clearColor);
        }
    }

    const auto& depthTexture = renderTarget.GetTexture(DepthStencil);
    if (depthTexture->IsValid())
    {
        ClearDepthStencilTexture(*depthTexture, clearFlags);
    }
}

void CommandList::ClearRenderTarget(const RenderTarget& renderTarget, const ClearValue& clearColor, D3D12_CLEAR_FLAGS clearFlags)
{
    ClearRenderTarget(renderTarget, clearColor.GetColor(), clearFlags);
}

void CommandList::DiscardResource(const Resource& resource)
{
    m_D3d12CommandList->DiscardResource(resource.GetD3D12Resource().Get(), nullptr);
}

void CommandList::Draw(const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t startVertex,
    const uint32_t startInstance)
{
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandList::DrawIndexed(const uint32_t indexCount, const uint32_t instanceCount, const uint32_t startIndex,
    const int32_t baseVertex,
    const uint32_t startInstance)
{
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}
//Modify Begin:2026-08-24 by Hui
void CommandList::ExecuteIndirect(
    const ComPtr<ID3D12CommandSignature>& pCommandSignature,
    const D3D12_INDIRECT_ARGUMENT_TYPE executionArgumentType,
    const uint32_t maxCommandCount,
    const Resource& argumentBuffer,
    const uint64_t argumentBufferOffset,
    const Resource* countBuffer,
    const uint64_t countBufferOffset
)
{
    Assert(pCommandSignature != nullptr, "Indirect command signature is null.");
    Assert(argumentBuffer.IsValid(), "Indirect argument buffer is not initialized.");
    Assert(countBuffer == nullptr || countBuffer->IsValid(), "Indirect count buffer is not initialized.");
    FlushResourceBarriers();

    switch (executionArgumentType)
    {
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
    case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
        CommitStagedDescriptorsForDraw();
        break;
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
    case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
        CommitStagedDescriptorsForDispatch();
        break;
    default:
        Assert(false, "Unsupported indirect execution argument type.");
        break;
    }

    m_D3d12CommandList->ExecuteIndirect(
        pCommandSignature.Get(),
        maxCommandCount,
        argumentBuffer.GetD3D12Resource().Get(),
        argumentBufferOffset,
        countBuffer != nullptr ? countBuffer->GetD3D12Resource().Get() : nullptr,
        countBufferOffset);
    TrackResource(argumentBuffer);
    if (countBuffer != nullptr)
    {
        TrackResource(*countBuffer);
    }
}

void CommandList::ClearUnorderedAccessUint(const Resource& resource, const UINT values[4])
{
    Assert(resource.IsValid(), "Unordered-access clear resource is not initialized.");
    Assert(resource.SupportsUnorderedAccess(), "Unordered-access clear requires an unordered-access resource.");
    Assert(values != nullptr, "Unordered-access clear values are null.");

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor = resource.GetUnorderedAccessView(nullptr);
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor =
        m_DynamicDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->CopyDescriptor(*this, cpuDescriptor);
    FlushResourceBarriers();
    m_D3d12CommandList->ClearUnorderedAccessViewUint(
        gpuDescriptor,
        cpuDescriptor,
        resource.GetD3D12Resource().Get(),
        values,
        0u,
        nullptr);
    TrackResource(resource);
}
//Modify End

void CommandList::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ)
{
    FlushResourceBarriers();

    CommitStagedDescriptorsForDispatch();

    m_D3d12CommandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

//Modify Begin:2026-07-30 by Hui
void CommandList::DispatchMesh(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ)
{
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList6->DispatchMesh(numGroupsX, numGroupsY, numGroupsZ);
}
//Modify End

//Modify Begin:2026-07-30 by Hui
void CommandList::SetRaytracingPipelineState(const ComPtr<ID3D12StateObject>& stateObject)
{
    m_D3d12CommandList5->SetPipelineState1(stateObject.Get());
    TrackObject(stateObject);
}

void CommandList::DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc)
{
    FlushResourceBarriers();

    CommitStagedDescriptorsForDispatch();

    m_D3d12CommandList5->DispatchRays(&dispatchRaysDesc);
}

void CommandList::BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc)
{
    FlushResourceBarriers();
    m_D3d12CommandList5->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
}

void CommandList::StageDynamicDescriptors(
    const D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    const UINT rootParameterIndex,
    const UINT descriptorOffset,
    const UINT numDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor)
{
    m_DynamicDescriptorHeaps[heapType]->StageDescriptors(rootParameterIndex, descriptorOffset, numDescriptors, srcDescriptor);
}

void CommandList::BindExternalDescriptorHeap(
    const D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    ID3D12DescriptorHeap* heap)
{
    Assert(heap != nullptr, "External descriptor heap must not be null.");
    Assert(
        heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        "Only shader-visible descriptor heap types can be bound externally.");
    m_DynamicDescriptorHeaps[heapType]->Reset();
    SetDescriptorHeap(heapType, heap);
}
//Modify End

//Modify Begin:2026-07-30 by Hui
bool CommandList::Close(
    CommandList& pendingCommandList,
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    // Flush any remaining barriers.
    FlushResourceBarriers();

    m_D3d12CommandList->Close();

    // Flush pending resource barriers.
    const uint32_t numPendingBarriers = m_PResourceStateTracker->FlushPendingResourceBarriers(
        pendingCommandList,
        submissionScope);
    m_PResourceStateTracker->CommitFinalResourceStates(submissionScope);

    return numPendingBarriers > 0;
}
//Modify End

void CommandList::Close()
{
    FlushResourceBarriers();
    m_D3d12CommandList->Close();
}

void CommandList::Reset()
{
    ThrowIfFailed(m_D3d12CommandAllocator->Reset());
    ThrowIfFailed(m_D3d12CommandList->Reset(m_D3d12CommandAllocator.Get(), nullptr));

    m_PResourceStateTracker->Reset();
    m_PUploadBuffer->Reset();

    ReleaseTrackedObjects();

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DynamicDescriptorHeaps[i]->Reset();
        m_DescriptorHeaps[i] = nullptr;
    }

//Modify Begin:2026-08-20 by Hui
    m_GraphicsRootSignature = nullptr;
    m_ComputeRootSignature = nullptr;
    m_DescriptorTableRootSignature = nullptr;
//Modify End
}

//Modify Begin:2026-08-25 by Hui
bool CommandList::TransitionLifecycle(
    const CommandListLifecycle expected,
    const CommandListLifecycle desired) noexcept
{
    CommandListLifecycle current = expected;
    return m_Lifecycle.compare_exchange_strong(
        current,
        desired,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

CommandListLifecycle CommandList::GetLifecycle() const noexcept
{
    return m_Lifecycle.load(std::memory_order_acquire);
}
//Modify End

void CommandList::TrackObject(const ComPtr<ID3D12Object>& object)
{
    m_TrackedObjects.push_back(object);
}

void CommandList::TrackResource(const Resource& res)
{
    TrackObject(res.GetD3D12Resource());
    m_TrackedResourceStateRegistrations.push_back(res.GetStateRegistration());
}

void CommandList::ReleaseTrackedObjects()
{
    m_TrackedResourceStateRegistrations.clear();
    m_TrackedObjects.clear();
}

void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap)
{
    if (m_DescriptorHeaps[heapType] != heap)
    {
        m_DescriptorHeaps[heapType] = heap;
        BindDescriptorHeaps();
    }
}

//Modify Begin:2026-08-20 by Hui
void CommandList::InvalidateCachedNativeState()
{
    m_GraphicsRootSignature = nullptr;
    m_ComputeRootSignature = nullptr;
    m_DescriptorTableRootSignature = nullptr;
    for (ID3D12DescriptorHeap*& descriptorHeap : m_DescriptorHeaps)
    {
        descriptorHeap = nullptr;
    }
}
//Modify End

//Modify Begin:2026-08-24 by Hui
void CommandList::SetComputeRootUnorderedAccessView(UINT rootParameterIndex, const Resource& resource)
{
    auto d3d12Resource = resource.GetD3D12Resource();
    m_D3d12CommandList->SetComputeRootUnorderedAccessView(rootParameterIndex, d3d12Resource->GetGPUVirtualAddress());
    TrackObject(d3d12Resource);
}
//Modify End

void CommandList::SetComputeRootShaderResourceView(UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
{
    m_D3d12CommandList->SetComputeRootShaderResourceView(rootParameterIndex, gpuAddress);
}

void CommandList::SetComputeRootConstantBufferView(UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
{
    m_D3d12CommandList->SetComputeRootConstantBufferView(rootParameterIndex, gpuAddress);
}

//Modify Begin:2026-08-12 by Hui
void CommandList::SetGraphicsRootDescriptorTable(UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
{
    m_D3d12CommandList->SetGraphicsRootDescriptorTable(rootParameterIndex, descriptorHandle);
}
//Modify End

void CommandList::SetComputeRootDescriptorTable(UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
{
    m_D3d12CommandList->SetComputeRootDescriptorTable(rootParameterIndex, descriptorHandle);
}

void CommandList::SetAutomaticViewportAndScissorRect(const RenderTarget& renderTarget, const UINT mipLevel)
{
    const auto& color0Texture = renderTarget.GetTexture(Color0);
    const auto& depthTexture = renderTarget.GetTexture(DepthStencil);
    if (!color0Texture->IsValid() && !depthTexture->IsValid())
    {
        throw std::exception("Both Color0 and DepthStencil attachment are invalid. Cannot compute viewport.");
    }

    const auto destinationDesc = color0Texture->IsValid() ? color0Texture->GetD3D12ResourceDesc() : depthTexture->GetD3D12ResourceDesc();
    if (mipLevel >= destinationDesc.MipLevels)
    {
        throw std::exception("Mip level out of range.");
    }

    const auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(destinationDesc.Width >> mipLevel), static_cast<float>(destinationDesc.Height >> mipLevel));

    SetViewport(viewport);
    SetInfiniteScrissorRect();
}

void CommandList::SetInfiniteScrissorRect()
{
    auto scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
    SetScissorRect(scissorRect);
}

void CommandList::BindDescriptorHeaps()
{
    UINT numDescriptorHeaps = 0;
    ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

    for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        ID3D12DescriptorHeap* descriptorHeap = m_DescriptorHeaps[i];
        if (descriptorHeap)
        {
            descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
        }
    }

    m_D3d12CommandList->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}
