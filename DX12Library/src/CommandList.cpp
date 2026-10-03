#include "DX12LibPCH.h"

#include "CommandList.h"

//Modify Begin:2026-09-29 by Hui
#include "BarrierContext.h"
//Modify End
//Modify Begin:2026-09-30 by Hui
#include "PerformanceScope.h"
//Modify End

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

//Modify Begin:2026-09-29 by Hui
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
    m_DefaultBarrierContext = std::make_unique<BarrierContext>(*this);
    m_ActiveBarrierContext = m_DefaultBarrierContext.get();

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

//Modify Begin:2026-10-02 by Hui
CommandList::CommandList(
    const D3D12_COMMAND_LIST_TYPE type,
    std::shared_ptr<D3D12DeviceContext> deviceContext,
    ID3D12GraphicsCommandList2* externalCommandList)
    : m_D3d12CommandListType(type)
    , m_StableId(g_NextCommandListStableId.fetch_add(1u, std::memory_order_relaxed))
    , m_DeviceContext(std::move(deviceContext))
    , m_Device(m_DeviceContext != nullptr ? m_DeviceContext->GetDevice() : nullptr)
    , m_ResourceStateRegistry(m_DeviceContext != nullptr ? m_DeviceContext->GetResourceStateRegistry() : nullptr)
    , m_ExternalCommandList(true)
{
    Assert(m_DeviceContext != nullptr, "D3D12 device context is null.");
    Assert(m_Device != nullptr, "D3D12 device is null.");
    Assert(m_ResourceStateRegistry != nullptr, "Resource state registry is null.");
    Assert(externalCommandList != nullptr, "External command list is null.");
    Assert(externalCommandList->GetType() == type,
        "The declared external queue type does not match the native command list.");
    m_D3d12CommandList = externalCommandList;
    ThrowIfFailed(m_D3d12CommandList.As(&m_D3d12CommandList5));
    ThrowIfFailed(m_D3d12CommandList.As(&m_D3d12CommandList6));
    m_PUploadBuffer = std::make_unique<UploadBuffer>(m_Device);
    m_PResourceStateTracker = std::make_unique<ResourceStateTracker>(m_ResourceStateRegistry);
    m_DefaultBarrierContext = std::make_unique<BarrierContext>(*this);
    m_ActiveBarrierContext = m_DefaultBarrierContext.get();
    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        const uint32_t numDescriptorsPerHeap =
            i == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? 8192u : 1024u;
        m_DynamicDescriptorHeaps[i] = std::make_unique<DynamicDescriptorHeap>(
            m_Device, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i), numDescriptorsPerHeap);
        m_DescriptorHeaps[i] = nullptr;
    }
}
//Modify End

CommandList::~CommandList() = default;

//Modify Begin:2026-09-29 by Hui
BarrierContext* CommandList::SetActiveBarrierContext(BarrierContext* const context) noexcept
{
    BarrierContext* previous = m_ActiveBarrierContext;
    m_ActiveBarrierContext = context;
    return previous;
}

BarrierContext& CommandList::GetBarrierContext() const
{
    Assert(m_ActiveBarrierContext != nullptr, "Command recording requires an active barrier context.");
    Assert(!m_ExternalCommandList || m_ActiveBarrierContext != m_DefaultBarrierContext.get(),
        "Host-owned lists require an active ExternalCommandContext with explicit entry states.");
    return *m_ActiveBarrierContext;
}

void CommandList::TrackAccess(const ResourceAccess& access)
{
    Assert(access.NativeResource != nullptr, "A resource binding requires a native resource.");
//Modify Begin:2026-10-02 by Hui
    // Descriptor bindings can reference the same resource many times in one
    // pass. Deduplicate lifetime ownership at the native identity boundary so
    // every binding does not append another shared state registration.
    if (m_TrackedResourceIdentities.insert(access.NativeResource.Get()).second)
    {
        TrackObject(access.NativeResource);
        if (access.Registration != nullptr)
        {
            m_TrackedResourceStateRegistrations.push_back(access.Registration);
        }
    }
//Modify End
    for (const auto& dependency : access.Dependencies) TrackAccess(dependency);
}

CommandList::ResourceAccess CommandList::ResourceAccess::Uav(
    const Resource& resource, const UINT first, const UINT count)
{
    ResourceAccess access{ resource.GetD3D12Resource(), resource.GetStateRegistration(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, first, count, true };
    resource.ForEachResourceRecursive([&](const Resource& dependency)
    {
        if (dependency.GetD3D12Resource() != resource.GetD3D12Resource())
            access.Dependencies.push_back({ dependency.GetD3D12Resource(), dependency.GetStateRegistration(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true });
    });
    return access;
}

void CommandList::SetResourceBindings(
    const BindingPoint point, const UINT rootParameter, const std::span<const ResourceAccess> accesses)
{
    auto& bindings = point == BindingPoint::Graphics ? m_GraphicsResourceBindings : m_ComputeResourceBindings;
    std::erase_if(bindings, [rootParameter](const auto& entry) { return entry.first.first == rootParameter; });
    if (point == BindingPoint::Graphics)
        m_GraphicsResourceBindingsDirty = true;
    else
        m_ComputeResourceBindingsDirty = true;
    for (UINT i = 0; i < accesses.size(); ++i)
        SetResourceBinding(point, rootParameter, i, accesses[i]);
}

void CommandList::SetResourceBinding(
    const BindingPoint point, const UINT rootParameter, const UINT offset, ResourceAccess access)
{
    TrackAccess(access);
    auto& bindings = point == BindingPoint::Graphics ? m_GraphicsResourceBindings : m_ComputeResourceBindings;
    bindings[{ rootParameter, offset }] = std::move(access);
    if (point == BindingPoint::Graphics)
        m_GraphicsResourceBindingsDirty = true;
    else
        m_ComputeResourceBindingsDirty = true;
}

void CommandList::PrepareBoundResources(const BindingPoint point,
    const std::span<const ResourceAccess> extraAccesses, const bool inputAssembler, const bool indexed)
{
    DX12_CPU_RECORDING_SCOPE("access.prepare");
    struct AccessState { D3D12_RESOURCE_STATES State; bool UavWrite; };
    std::unordered_map<ID3D12Resource*, std::unordered_map<UINT, AccessState>> resources;
    const auto merge = [](AccessState& target, const AccessState incoming)
    {
        if (target.State != incoming.State)
        {
            constexpr UINT writeStates = D3D12_RESOURCE_STATE_RENDER_TARGET |
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
                D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST;
            Assert(((target.State | incoming.State) & writeStates) == 0,
                "One GPU operation binds overlapping subresources for incompatible read/write access.");
            target.State |= incoming.State;
        }
        target.UavWrite |= incoming.UavWrite;
    };
    const auto& bindings = point == BindingPoint::Graphics ? m_GraphicsResourceBindings : m_ComputeResourceBindings;
    auto& preparedBindings = point == BindingPoint::Graphics
        ? m_PreparedGraphicsResourceBindings
        : m_PreparedComputeResourceBindings;
    bool& bindingsDirty = point == BindingPoint::Graphics
        ? m_GraphicsResourceBindingsDirty
        : m_ComputeResourceBindingsDirty;
    if (bindingsDirty)
    {
        //Modify Begin:2026-10-03 by Hui
        // Binding metadata is immutable until the next SetResourceBinding call.
        // Flatten it once so every execution only performs the state merge that
        // must remain execution-local for UAV ordering.
        preparedBindings.clear();
        preparedBindings.reserve(bindings.size());
        const auto append = [&](const auto& self, const ResourceAccess& access) -> void
        {
            for (const ResourceAccess& dependency : access.Dependencies)
                self(self, dependency);
            preparedBindings.push_back(&access);
        };
        for (const auto& [key, access] : bindings)
            append(append, access);
        bindingsDirty = false;
        //Modify End
    }

    const std::function<void(const ResourceAccess&)> add = [&](const ResourceAccess& access)
    {
        for (const auto& dependency : access.Dependencies) add(dependency);
        auto& states = resources[access.NativeResource.Get()];
        const auto addSubresource = [&](const UINT subresource)
        {
            const AccessState incoming{ access.State, access.UavWrite };
            auto [it, inserted] = states.try_emplace(subresource, incoming);
            if (!inserted) merge(it->second, incoming);
        };
        if (access.NumSubresources == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            addSubresource(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        else
            for (UINT i = 0; i < access.NumSubresources; ++i)
                addSubresource(access.FirstSubresource + i);
    };
    resources.reserve(preparedBindings.size() + extraAccesses.size());
    for (const ResourceAccess* access : preparedBindings)
        add(*access);
    if (point == BindingPoint::Graphics)
    {
        for (const ResourceAccess& access : m_RenderTargetResourceBindings)
            add(access);
        if (inputAssembler)
        {
            for (const auto& [key, access] : m_GraphicsFixedBindings)
            {
                if (key != 200u || indexed)
                    add(access);
            }
        }
    }
    for (const auto& access : extraAccesses) add(access);

    for (auto& [resource, states] : resources)
    {
        // ALL plus an exact view must be merged before any transition is queued.
        const auto all = states.find(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        if (all != states.end() && states.size() > 1)
        {
            const AccessState base = all->second;
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            D3D12_FEATURE_DATA_FORMAT_INFO info{ desc.Format, 1 };
            if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
                ThrowIfFailed(m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info)));
            const UINT count = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? 1u :
                desc.MipLevels * (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize) * info.PlaneCount;
            states.erase(all);
            for (UINT i = 0; i < count; ++i)
            {
                auto [it, inserted] = states.try_emplace(i, base);
                if (!inserted) merge(it->second, base);
            }
        }
        for (const auto& [subresource, access] : states)
            GetBarrierContext().Use(
                resource,
                access.State,
                access.UavWrite ? ResourceUse::Write : ResourceUse::Read,
                false,
                subresource);
    }
}
//Modify End

//Modify Begin:2026-09-29 by Hui
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
        GetBarrierContext().NotifyCommandRecorded();
        InvalidateCachedNativeState();
        throw;
    }

    GetBarrierContext().NotifyCommandRecorded();
    InvalidateCachedNativeState();
}

void CommandList::FlushResourceBarriers()
{
    DX12_CPU_RECORDING_SCOPE("barrier.flush");
    Assert(!m_ExternalCommandList || !m_PResourceStateTracker->HasPendingResourceBarriers(),
        "External resources need a declared before-state before recording GPU work.");
    m_PResourceStateTracker->FlushResourceBarriers(*this);
}

void CommandList::FlushExternalResourceBarriers(
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    Assert(m_ExternalCommandList, "Only external command lists can flush external resource barriers.");
    // External callers provide the host-owned entry state through BarrierContext.
    // Resolve any first-use transitions here, after all declarations have been
    // registered, instead of rejecting the pending list before resolution.
    m_PResourceStateTracker->FlushPendingResourceBarriers(*this, submissionScope);
    m_PResourceStateTracker->FlushResourceBarriers(*this);
    m_PResourceStateTracker->CommitFinalResourceStates(submissionScope);
}
//Modify End

//Modify Begin:2026-10-01 by Hui
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

void CommandList::ReserveResourceTracking(const size_t resourceCount)
{
    m_TrackedObjects.reserve(m_TrackedObjects.size() + resourceCount);
    m_TrackedResourceIdentities.reserve(m_TrackedResourceIdentities.size() + resourceCount);
    m_TrackedResourceStateRegistrations.reserve(
        m_TrackedResourceStateRegistrations.size() + resourceCount);
    m_PResourceStateTracker->ReserveResourceTracking(resourceCount);
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

//Modify Begin:2026-09-29 by Hui
void CommandList::CopyResource(const Resource& dstRes, const Resource& srcRes)
{
    TrackResource(dstRes);
    TrackResource(srcRes);
    CopyResource(dstRes.GetD3D12Resource(), srcRes.GetD3D12Resource());
}

void CommandList::CopyResource(
    const ComPtr<ID3D12Resource> dstRes,
    const ComPtr<ID3D12Resource> srcRes)
{
    Assert(dstRes != nullptr, "Copy destination resource must not be null.");
    Assert(srcRes != nullptr, "Copy source resource must not be null.");
    GetBarrierContext().Use(srcRes.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, ResourceUse::Read);
    GetBarrierContext().Use(dstRes.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ResourceUse::Write);
    FlushResourceBarriers();

    m_D3d12CommandList->CopyResource(dstRes.Get(), srcRes.Get());

    TrackObject(dstRes);
    TrackObject(srcRes);
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::CopyBufferRegion(
    const Resource& destination,
    const uint64_t destinationOffset,
    const ComPtr<ID3D12Resource> source,
    const uint64_t sourceOffset,
    const uint64_t sizeInBytes)
{
    Assert(source != nullptr, "Copy source buffer must not be null.");
    Assert(destination.GetD3D12Resource() != nullptr, "Copy destination buffer must not be null.");
    GetBarrierContext().Use(source.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, ResourceUse::Read);
    GetBarrierContext().Use(destination, D3D12_RESOURCE_STATE_COPY_DEST, ResourceUse::Write);

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
    GetBarrierContext().Use(source, D3D12_RESOURCE_STATE_COPY_SOURCE, ResourceUse::Read);
    GetBarrierContext().Use(destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ResourceUse::Write);

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
    if (m_ActiveBarrierContext != nullptr)
    {
        m_ActiveBarrierContext->Use(
            srcRes, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, ResourceUse::Read, false, srcSubresource);
        m_ActiveBarrierContext->Use(
            dstRes, D3D12_RESOURCE_STATE_RESOLVE_DEST, ResourceUse::Write, false, dstSubresource);
    }
    FlushResourceBarriers();

    m_D3d12CommandList->ResolveSubresource(dstRes.GetD3D12Resource().Get(), dstSubresource,
        srcRes.GetD3D12Resource().Get(), srcSubresource,
        dstRes.GetD3D12ResourceDesc().Format);

    TrackResource(srcRes);
    TrackResource(dstRes);
}
//Modify End

//Modify Begin:2026-10-01 by Hui
void CommandList::SetShadingRateImage(const Resource& resource)
{
    const auto d3d12Resource = resource.GetD3D12Resource();
    GetBarrierContext().Use(resource, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, ResourceUse::Read);
    FlushResourceBarriers();
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
    if (m_ActiveBarrierContext != nullptr)
    {
        m_ActiveBarrierContext->Use(texture, D3D12_RESOURCE_STATE_RENDER_TARGET, ResourceUse::Write);
        m_ActiveBarrierContext->Flush();
    }
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
    if (m_ActiveBarrierContext != nullptr)
    {
        m_ActiveBarrierContext->Use(texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, ResourceUse::Write);
        m_ActiveBarrierContext->Flush();
    }
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

//Modify Begin:2026-09-29 by Hui
void CommandList::SetVertexBuffer(const uint32_t slot, const VertexBuffer& vertexBuffer)
{
    m_GraphicsFixedBindings[100u + slot] = { vertexBuffer.GetD3D12Resource(),
        vertexBuffer.GetStateRegistration(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER };
    m_GraphicsResourceBindingsDirty = true;
    const auto vertexBufferView = vertexBuffer.GetVertexBufferView();

    m_D3d12CommandList->IASetVertexBuffers(slot, 1, &vertexBufferView);

    TrackResource(vertexBuffer);
}

void CommandList::SetVertexBufferView(
    const uint32_t slot,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    const Resource& resource)
{
    m_GraphicsFixedBindings[100u + slot] = { resource.GetD3D12Resource(),
        resource.GetStateRegistration(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER };
    m_GraphicsResourceBindingsDirty = true;
    m_D3d12CommandList->IASetVertexBuffers(slot, 1, &vertexBufferView);
    TrackResource(resource);
}

void CommandList::SetIndexBuffer(const IndexBuffer& indexBuffer)
{
    m_GraphicsFixedBindings[200u] = { indexBuffer.GetD3D12Resource(),
        indexBuffer.GetStateRegistration(), D3D12_RESOURCE_STATE_INDEX_BUFFER };
    m_GraphicsResourceBindingsDirty = true;
    const auto indexBufferView = indexBuffer.GetIndexBufferView();

    m_D3d12CommandList->IASetIndexBuffer(&indexBufferView);

    TrackResource(indexBuffer);
}

void CommandList::SetIndexBufferView(
    const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
    const Resource& resource)
{
    m_GraphicsFixedBindings[200u] = { resource.GetD3D12Resource(),
        resource.GetStateRegistration(), D3D12_RESOURCE_STATE_INDEX_BUFFER };
    m_GraphicsResourceBindingsDirty = true;
    m_D3d12CommandList->IASetIndexBuffer(&indexBufferView);
    TrackResource(resource);
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

//Modify Begin:2026-09-29 by Hui
void CommandList::SetGraphicsRootSignature(const RootSignature& rootSignature)
{
    m_DescriptorBindingPoint = BindingPoint::Graphics;
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
        m_GraphicsResourceBindings.clear();
        m_GraphicsResourceBindingsDirty = true;
        m_GraphicsRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetGraphicsRootSignature(m_GraphicsRootSignature);

        TrackObject(m_GraphicsRootSignature);
    }
}

void CommandList::SetComputeRootSignature(const RootSignature& rootSignature)
{
    m_DescriptorBindingPoint = BindingPoint::Compute;
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
        m_ComputeResourceBindings.clear();
        m_ComputeResourceBindingsDirty = true;
        m_ComputeRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetComputeRootSignature(m_ComputeRootSignature);

        TrackObject(m_ComputeRootSignature);
    }
}

void CommandList::SetGraphicsAndComputeRootSignature(const RootSignature& rootSignature)
{
    m_DescriptorBindingPoint = BindingPoint::Graphics;
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
        m_GraphicsResourceBindings.clear();
        m_GraphicsResourceBindingsDirty = true;
        m_GraphicsRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetGraphicsRootSignature(m_GraphicsRootSignature);
    }
    if (m_ComputeRootSignature != d3d12RootSignature)
    {
        m_ComputeResourceBindings.clear();
        m_ComputeResourceBindingsDirty = true;
        m_ComputeRootSignature = d3d12RootSignature;
        m_D3d12CommandList->SetComputeRootSignature(m_ComputeRootSignature);
    }

    TrackObject(d3d12RootSignature);
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::SetShaderResourceView(const uint32_t rootParameterIndex, const uint32_t descriptorOffset,
    const Resource& resource,
    const UINT firstSubresource, const UINT numSubresources,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    const auto state = m_DescriptorBindingPoint == BindingPoint::Graphics ?
        D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    SetResourceBinding(m_DescriptorBindingPoint, rootParameterIndex, descriptorOffset,
        { resource.GetD3D12Resource(), resource.GetStateRegistration(), state, firstSubresource, numSubresources });
    m_DynamicDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(
        rootParameterIndex, descriptorOffset, 1, resource.GetShaderResourceView(srv));
    TrackResource(resource);
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::SetUnorderedAccessView(const uint32_t rootParameterIndex, const uint32_t descriptorOffset,
    const Resource& resource,
    const UINT firstSubresource, const UINT numSubresources,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc)
{
    Assert(resource.SupportsUnorderedAccess(), "Cannot bind a resource without unordered-access usage as a UAV.");
    SetResourceBinding(m_DescriptorBindingPoint, rootParameterIndex, descriptorOffset,
        ResourceAccess::Uav(resource, firstSubresource, numSubresources));
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

//Modify Begin:2026-09-29 by Hui
void CommandList::SetRenderTarget(const RenderTarget& renderTarget, UINT texArrayIndex /*= -1*/, UINT mipLevel /*= 0*/, bool useDepth /*= true*/, bool readonlyDepth)
{
    m_RenderTargetResourceBindings.clear();
    m_GraphicsResourceBindingsDirty = true;
    const auto addAttachment = [&](const Texture& texture, const D3D12_RESOURCE_STATES state, const bool depth)
    {
        const auto desc = texture.GetD3D12Resource()->GetDesc();
        D3D12_FEATURE_DATA_FORMAT_INFO info{ desc.Format, 1 };
        if (depth) ThrowIfFailed(m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info)));
        const UINT arrays = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize;
        const UINT firstArray = texArrayIndex == UINT(-1) ? 0u : texArrayIndex;
        const UINT endArray = texArrayIndex == UINT(-1) ? arrays : texArrayIndex + 1;
        const UINT mip = texArrayIndex == UINT(-1) ? 0u : mipLevel;
        for (UINT plane = 0; plane < info.PlaneCount; ++plane)
            for (UINT array = firstArray; array < endArray; ++array)
                m_RenderTargetResourceBindings.push_back({ texture.GetD3D12Resource(), texture.GetStateRegistration(),
                    state, D3D12CalcSubresource(mip, array, plane, desc.MipLevels, arrays), 1u });
    };
    {
        const auto& textures = renderTarget.GetTextures();
        for (size_t textureIndex = 0; textureIndex < NumAttachmentPoints - 1u; ++textureIndex)
        {
            if (textures[textureIndex] != nullptr && textures[textureIndex]->IsValid())
            {
                addAttachment(*textures[textureIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, false);
            }
        }
        const auto& depthTexture = renderTarget.GetTexture(DepthStencil);
        if (useDepth && depthTexture != nullptr && depthTexture->IsValid())
        {
            addAttachment(*depthTexture,
                readonlyDepth ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        }
    }
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

//Modify Begin:2026-09-29 by Hui
void CommandList::Draw(const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t startVertex,
    const uint32_t startInstance)
{
    PrepareBoundResources(BindingPoint::Graphics, {}, true, false);
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
    GetBarrierContext().NotifyCommandRecorded();
}

void CommandList::DrawIndexed(const uint32_t indexCount, const uint32_t instanceCount, const uint32_t startIndex,
    const int32_t baseVertex,
    const uint32_t startInstance)
{
    PrepareBoundResources(BindingPoint::Graphics);
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
    GetBarrierContext().NotifyCommandRecorded();
}
//Modify End
//Modify Begin:2026-09-29 by Hui
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
    const ResourceAccess indirectAccesses[]{
        { argumentBuffer.GetD3D12Resource(), argumentBuffer.GetStateRegistration(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT },
        { countBuffer ? countBuffer->GetD3D12Resource() : nullptr,
          countBuffer ? countBuffer->GetStateRegistration() : nullptr, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT }
    };
    PrepareBoundResources(executionArgumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ||
        executionArgumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ||
        executionArgumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
        ? BindingPoint::Graphics : BindingPoint::Compute,
        std::span<const ResourceAccess>(indirectAccesses, countBuffer ? 2u : 1u),
        executionArgumentType != D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH,
        executionArgumentType == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED);
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
    GetBarrierContext().NotifyCommandRecorded();
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

    if (m_ActiveBarrierContext != nullptr)
    {
        m_ActiveBarrierContext->Use(
            resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ResourceUse::Write, true);
        m_ActiveBarrierContext->Flush();
    }

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
    GetBarrierContext().NotifyCommandRecorded();
    TrackResource(resource);
}
//Modify End

//Modify Begin:2026-10-01 by Hui
void CommandList::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ)
{
    DX12_CPU_RECORDING_SCOPE("dispatch.record");
    PrepareBoundResources(BindingPoint::Compute);
    FlushResourceBarriers();

    CommitStagedDescriptorsForDispatch();

    m_D3d12CommandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
    GetBarrierContext().NotifyCommandRecorded();
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::DispatchMesh(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ)
{
    PrepareBoundResources(BindingPoint::Graphics, {}, false);
    FlushResourceBarriers();

    CommitStagedDescriptorsForDraw();

    m_D3d12CommandList6->DispatchMesh(numGroupsX, numGroupsY, numGroupsZ);
    GetBarrierContext().NotifyCommandRecorded();
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::SetRaytracingPipelineState(const ComPtr<ID3D12StateObject>& stateObject)
{
    m_D3d12CommandList5->SetPipelineState1(stateObject.Get());
    TrackObject(stateObject);
}

void CommandList::DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc)
{
    PrepareBoundResources(BindingPoint::Compute);
    FlushResourceBarriers();

    CommitStagedDescriptorsForDispatch();

    m_D3d12CommandList5->DispatchRays(&dispatchRaysDesc);
    GetBarrierContext().NotifyCommandRecorded();
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

    // Rebinding the same external heap must preserve the dynamic descriptor state.
    // Resetting it here only discards staged tables and forces needless CPU work.
    if (m_DescriptorHeaps[heapType] == heap)
    {
        return;
    }

    m_DynamicDescriptorHeaps[heapType]->Reset();
    SetDescriptorHeap(heapType, heap);
}
//Modify End

//Modify Begin:2026-07-30 by Hui
bool CommandList::Close(
    CommandList& pendingCommandList,
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    Assert(!m_ExternalCommandList, "External command lists cannot be closed by DX12Renderer.");
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

//Modify Begin:2026-10-02 by Hui
bool CommandList::HasPendingSubmissionWork() const noexcept
{
    return m_PResourceStateTracker->HasPendingSubmissionWork();
}

bool CommandList::CloseForSubmission(ResourceStateRegistry::SubmissionScope& submissionScope)
{
    Assert(!m_ExternalCommandList, "External command lists cannot be closed by DX12Renderer.");
    Assert(
        !m_PResourceStateTracker->HasPendingSubmissionWork(),
        "A command list with pending submission work requires a pending barrier command list.");
    FlushResourceBarriers();
    m_D3d12CommandList->Close();
    m_PResourceStateTracker->CommitFinalResourceStates(submissionScope);
    return false;
}
//Modify End

void CommandList::Close()
{
    Assert(!m_ExternalCommandList, "External command lists cannot be closed by DX12Renderer.");
    FlushResourceBarriers();
    m_D3d12CommandList->Close();
}

void CommandList::Reset()
{
    Assert(!m_ExternalCommandList, "External command lists cannot be reset by DX12Renderer.");
//Modify Begin:2026-09-29 by Hui
    Assert(m_ActiveBarrierContext == m_DefaultBarrierContext.get(),
        "Cannot reset a command list while another recording context is attached.");
//Modify End
    ThrowIfFailed(m_D3d12CommandAllocator->Reset());
    ThrowIfFailed(m_D3d12CommandList->Reset(m_D3d12CommandAllocator.Get(), nullptr));

    m_PResourceStateTracker->Reset();
//Modify Begin:2026-09-29 by Hui
    m_DefaultBarrierContext->Reset();
    m_GraphicsResourceBindings.clear();
    m_ComputeResourceBindings.clear();
    m_GraphicsFixedBindings.clear();
    m_RenderTargetResourceBindings.clear();
    m_PreparedGraphicsResourceBindings.clear();
    m_PreparedComputeResourceBindings.clear();
    m_GraphicsResourceBindingsDirty = true;
    m_ComputeResourceBindingsDirty = true;
//Modify End
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
//Modify Begin:2026-09-30 by Hui
    ID3D12Resource* const nativeResource = res.GetD3D12ResourcePtr();
    if (!m_TrackedResourceIdentities.insert(nativeResource).second)
    {
        return;
    }
    TrackObject(res.GetD3D12Resource());
    m_TrackedResourceStateRegistrations.push_back(res.GetStateRegistration());
//Modify End
}

void CommandList::ReleaseTrackedObjects()
{
    m_TrackedResourceStateRegistrations.clear();
//Modify Begin:2026-09-30 by Hui
    m_TrackedResourceIdentities.clear();
//Modify End
    m_TrackedObjects.clear();
}

//Modify Begin:2026-10-02 by Hui
bool CommandList::HasTransientUavBuffer(
    const uint64_t key,
    const uint64_t sizeInBytes) const
{
    const auto iterator = m_TransientUavBuffers.find(key);
    return iterator != m_TransientUavBuffers.end() &&
        iterator->second.Resource != nullptr &&
        iterator->second.Capacity >= sizeInBytes;
}

CommandList::TransientUavBuffer& CommandList::GetOrCreateTransientUavBuffer(
    const uint64_t key,
    const uint64_t sizeInBytes,
    const wchar_t* name)
{
    Assert(sizeInBytes > 0, "Transient UAV buffer size must be non-zero.");
    auto& buffer = m_TransientUavBuffers[key];
    if (buffer.Resource != nullptr && buffer.Capacity >= sizeInBytes)
    {
        return buffer;
    }

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(m_Device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource)));
    if (name != nullptr)
    {
        resource->SetName(name);
    }

    buffer.Resource = std::move(resource);
    buffer.StateRegistration = m_ResourceStateRegistry->AcquireResource(
        buffer.Resource.Get(),
        D3D12_RESOURCE_STATE_COMMON);
    buffer.Capacity = sizeInBytes;
    return buffer;
}
//Modify End

void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap)
{
    if (m_DescriptorHeaps[heapType] != heap)
    {
        m_DescriptorHeaps[heapType] = heap;
        BindDescriptorHeaps();
    }
}

//Modify Begin:2026-09-29 by Hui
void CommandList::InvalidateCachedNativeState()
{
    m_GraphicsResourceBindings.clear();
    m_ComputeResourceBindings.clear();
    m_GraphicsFixedBindings.clear();
    m_RenderTargetResourceBindings.clear();
    m_PreparedGraphicsResourceBindings.clear();
    m_PreparedComputeResourceBindings.clear();
    m_GraphicsResourceBindingsDirty = true;
    m_ComputeResourceBindingsDirty = true;
    m_GraphicsRootSignature = nullptr;
    m_ComputeRootSignature = nullptr;
    m_DescriptorTableRootSignature = nullptr;
    for (ID3D12DescriptorHeap*& descriptorHeap : m_DescriptorHeaps)
    {
        descriptorHeap = nullptr;
    }
}
//Modify End

//Modify Begin:2026-09-29 by Hui
void CommandList::SetComputeRootUnorderedAccessView(UINT rootParameterIndex, const Resource& resource)
{
    SetResourceBinding(BindingPoint::Compute, rootParameterIndex, 0,
        { resource.GetD3D12Resource(), resource.GetStateRegistration(),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true });
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

//Modify Begin:2026-09-29 by Hui
void CommandList::SetExternalComputePipeline(
    ID3D12RootSignature* rootSignature, ID3D12PipelineState* pipelineState)
{
    Assert(m_ExternalCommandList, "External compute pipeline binding requires an external command list.");
    Assert(rootSignature != nullptr && pipelineState != nullptr, "External compute pipeline is incomplete.");
    m_DescriptorBindingPoint = BindingPoint::Compute;
    if (m_ComputeRootSignature != rootSignature)
    {
        m_ComputeResourceBindings.clear();
        m_ComputeResourceBindingsDirty = true;
    }
    m_ComputeRootSignature = rootSignature;
    m_D3d12CommandList->SetComputeRootSignature(rootSignature);
    m_D3d12CommandList->SetPipelineState(pipelineState);
}

//Modify End

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
