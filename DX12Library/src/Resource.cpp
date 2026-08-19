#include "DX12LibPCH.h"

#include "Resource.h"

#include "D3D12DeviceContext.h"
#include "ResourceStateRegistry.h"
#include "ResourceStateTracker.h"

Resource::Resource(const std::wstring& name, std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_FormatSupport({})
    , m_ResourceName(name)
//Modify Begin:2026-08-12 by Hui
    , m_DeviceContext(std::move(deviceContext))
//Modify End
{}

Resource::Resource(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue,
    const std::wstring& name, std::shared_ptr<D3D12DeviceContext> deviceContext)
//Modify Begin:2026-08-12 by Hui
    : m_DeviceContext(std::move(deviceContext))
//Modify End
{
//Modify Begin:2026-08-12 by Hui
    Assert(m_DeviceContext != nullptr, "GPU resource creation requires an explicit D3D12 device context.");
//Modify End
    if (clearValue)
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }

    const auto device = m_DeviceContext->GetDevice();

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        m_d3d12ClearValue.get(),
        IID_PPV_ARGS(&m_d3d12Resource)
    ));

    AcquireResourceState(D3D12_RESOURCE_STATE_COMMON);

    CheckFeatureSupport();
    SetName(name);
}

//Modify Begin:2026-08-12 by Hui
Resource::Resource(
    const D3D12_RESOURCE_DESC& resourceDesc,
    const D3D12_HEAP_FLAGS heapFlags,
    const D3D12_CLEAR_VALUE* clearValue,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_DeviceContext(std::move(deviceContext))
{
    Assert(m_DeviceContext != nullptr, "GPU resource creation requires an explicit D3D12 device context.");
    if (clearValue)
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }

    const auto device = m_DeviceContext->GetDevice();

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        heapFlags,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        m_d3d12ClearValue.get(),
        IID_PPV_ARGS(&m_d3d12Resource)
    ));

    AcquireResourceState(D3D12_RESOURCE_STATE_COMMON);

    CheckFeatureSupport();
    SetName(name);
}
//Modify End

Resource::Resource(
    const D3D12_RESOURCE_DESC& resourceDesc,
    const ComPtr<ID3D12Heap>& pHeap,
    UINT64 heapOffset,
    const D3D12_CLEAR_VALUE* clearValue,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
//Modify Begin:2026-08-12 by Hui
    : m_DeviceContext(std::move(deviceContext))
//Modify End
{
//Modify Begin:2026-08-12 by Hui
    Assert(m_DeviceContext != nullptr, "Placed GPU resource creation requires an explicit D3D12 device context.");
//Modify End
    if (clearValue)
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }

    const auto device = m_DeviceContext->GetDevice();

    Assert(pHeap != nullptr, "Heap cannot be null.");
    ThrowIfFailed(device->CreatePlacedResource(
        pHeap.Get(),
        heapOffset,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        m_d3d12ClearValue.get(),
        IID_PPV_ARGS(&m_d3d12Resource)
    ));

    AcquireResourceState(D3D12_RESOURCE_STATE_COMMON);

    CheckFeatureSupport();
    SetName(name);

}

Resource::Resource(
    ComPtr<ID3D12Resource> resource,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_d3d12Resource(resource)
    , m_FormatSupport({})
//Modify Begin:2026-08-12 by Hui
    , m_DeviceContext(std::move(deviceContext))
//Modify End
{
//Modify Begin:2026-08-12 by Hui
    Assert(m_d3d12Resource != nullptr, "Wrapped D3D12 resource is null.");
    Assert(m_DeviceContext != nullptr, "Wrapped GPU resource requires an explicit D3D12 device context.");
//Modify End
    AcquireResourceState(D3D12_RESOURCE_STATE_COMMON);
    CheckFeatureSupport();
    SetName(name);
}

Resource::Resource(const Resource& copy)
    : m_d3d12Resource(copy.m_d3d12Resource)
    , m_FormatSupport(copy.m_FormatSupport)
    , m_ResourceName(copy.m_ResourceName)
    , m_DeviceContext(copy.m_DeviceContext)
    , m_StateRegistration(copy.m_StateRegistration)
{
    if (copy.m_d3d12ClearValue)
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*copy.m_d3d12ClearValue);
}

Resource::Resource(Resource&& copy)
    : m_d3d12Resource(std::move(copy.m_d3d12Resource))
    , m_FormatSupport(copy.m_FormatSupport)
    , m_d3d12ClearValue(std::move(copy.m_d3d12ClearValue))
    , m_ResourceName(std::move(copy.m_ResourceName))
    , m_DeviceContext(std::move(copy.m_DeviceContext))
    , m_StateRegistration(std::move(copy.m_StateRegistration))
{}

Resource& Resource::operator=(const Resource& other)
{
    if (this != &other)
    {
        m_d3d12Resource = other.m_d3d12Resource;
        m_FormatSupport = other.m_FormatSupport;
        m_ResourceName = other.m_ResourceName;
        m_DeviceContext = other.m_DeviceContext;
        m_StateRegistration = other.m_StateRegistration;
        if (other.m_d3d12ClearValue)
        {
            m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*other.m_d3d12ClearValue);
        }
    }

    return *this;
}

Resource& Resource::operator=(Resource&& other) noexcept
{
    if (this != &other)
    {
        m_d3d12Resource = std::move(other.m_d3d12Resource);
        m_FormatSupport = other.m_FormatSupport;
        m_ResourceName = std::move(other.m_ResourceName);
        m_d3d12ClearValue = std::move(other.m_d3d12ClearValue);
        m_DeviceContext = std::move(other.m_DeviceContext);
        m_StateRegistration = std::move(other.m_StateRegistration);

        other.Reset();
    }

    return *this;
}


Resource::~Resource()
{}

//Modify Begin:2026-08-12 by Hui
void Resource::AttachDeviceContext(std::shared_ptr<D3D12DeviceContext> deviceContext)
{
    Assert(deviceContext != nullptr, "Command list has no D3D12 device context.");
    Assert(
        m_DeviceContext == nullptr || m_DeviceContext.get() == deviceContext.get(),
        "GPU resource belongs to a different D3D12 device context.");
    m_DeviceContext = std::move(deviceContext);
}
//Modify End

void Resource::SetD3D12Resource(ComPtr<ID3D12Resource> d3d12Resource, const D3D12_CLEAR_VALUE* clearValue)
{
//Modify Begin:2026-08-12 by Hui
    if (d3d12Resource != nullptr)
    {
        Assert(m_DeviceContext != nullptr, "Assigning a GPU resource requires an explicit D3D12 device context.");
    }
//Modify End
    m_d3d12Resource = d3d12Resource;
    AcquireResourceState(D3D12_RESOURCE_STATE_COMMON);
//Modify Begin:2026-08-07 by Hui
    if (clearValue != nullptr)
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }
    else
    {
        m_d3d12ClearValue.reset();
    }
//Modify End
    CheckFeatureSupport();
    SetName(m_ResourceName);
}

const D3D12_CLEAR_VALUE& Resource::GetD3D12ClearValue() const
{
    return *m_d3d12ClearValue;
}

void Resource::SetName(const std::wstring& name)
{
    m_ResourceName = name;
    if (m_d3d12Resource && !m_ResourceName.empty())
    {
        m_d3d12Resource->SetName(m_ResourceName.c_str());
    }
}

const std::wstring& Resource::GetName() const
{
    return m_ResourceName;
}

void Resource::Reset()
{
    m_d3d12Resource.Reset();
    m_StateRegistration.reset();
    m_FormatSupport = {};
    m_d3d12ClearValue.reset();
    m_ResourceName.clear();
}

//Modify Begin:2026-08-12 by Hui
void Resource::AcquireResourceState(const D3D12_RESOURCE_STATES initialState)
{
    if (m_d3d12Resource == nullptr)
    {
        m_StateRegistration.reset();
        return;
    }

    Assert(m_DeviceContext != nullptr, "GPU resource has no D3D12 device context.");
    m_StateRegistration = m_DeviceContext->GetResourceStateRegistry()->AcquireResource(
        m_d3d12Resource.Get(),
        initialState);
}
//Modify End

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT1 formatSupport) const
{
    return (m_FormatSupport.Support1 & formatSupport) != 0;
}

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT2 formatSupport) const
{
    return (m_FormatSupport.Support2 & formatSupport) != 0;
}

void Resource::CheckFeatureSupport()
{
    if (m_d3d12Resource)
    {
//Modify Begin:2026-08-12 by Hui
        Assert(m_DeviceContext != nullptr, "GPU resource has no D3D12 device context.");
//Modify End
        auto desc = m_d3d12Resource->GetDesc();
        const auto device = m_DeviceContext->GetDevice();

        m_FormatSupport.Format = desc.Format;
        ThrowIfFailed(device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT,
            &m_FormatSupport,
            sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));
    }
    else
    {
        m_FormatSupport = {};
    }
}
