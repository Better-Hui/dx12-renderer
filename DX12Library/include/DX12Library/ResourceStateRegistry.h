//Modify Begin:2026-07-30 by BestHui
#pragma once

//Modify Begin:2026-08-12 by BestHui
#include "Helpers.h"
//Modify End

#include <d3d12.h>

#include <mutex>
#include <memory>
#include <unordered_map>

class ResourceStateRegistry;

class ResourceStateRegistration final
{
public:
    ResourceStateRegistration(const ResourceStateRegistration&) = delete;
    ResourceStateRegistration& operator=(const ResourceStateRegistration&) = delete;
    ~ResourceStateRegistration();

private:
    friend class ResourceStateRegistry;

    ResourceStateRegistration(std::weak_ptr<ResourceStateRegistry> registry, ID3D12Resource* resource)
        : m_Registry(std::move(registry))
        , m_Resource(resource)
    {
    }

    std::weak_ptr<ResourceStateRegistry> m_Registry;
    ID3D12Resource* m_Resource = nullptr;
};

class ResourceStateRegistry final : public std::enable_shared_from_this<ResourceStateRegistry>
{
public:
    struct ResourceState
    {
        explicit ResourceState(const D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON)
            : State(state)
        {
        }

        void SetSubresourceState(const UINT subresource, const D3D12_RESOURCE_STATES state)
        {
            if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                State = state;
                SubresourceStates.clear();
            }
            else
            {
                SubresourceStates[subresource] = state;
            }
        }

        D3D12_RESOURCE_STATES GetSubresourceState(const UINT subresource) const
        {
            const auto iterator = SubresourceStates.find(subresource);
            return iterator != SubresourceStates.end() ? iterator->second : State;
        }

        D3D12_RESOURCE_STATES State;
        std::unordered_map<UINT, D3D12_RESOURCE_STATES> SubresourceStates;
    };

    using ResourceStateMap = std::unordered_map<ID3D12Resource*, ResourceState>;

    class SubmissionScope final
    {
    public:
        SubmissionScope(const SubmissionScope&) = delete;
        SubmissionScope& operator=(const SubmissionScope&) = delete;
        SubmissionScope(SubmissionScope&&) = default;
        SubmissionScope& operator=(SubmissionScope&&) = default;

        ResourceStateMap& GetStates() { return m_Registry.m_States; }

    private:
        friend class ResourceStateRegistry;

        explicit SubmissionScope(ResourceStateRegistry& registry)
            : m_Registry(registry)
            , m_Lock(registry.m_Mutex)
        {
        }

        ResourceStateRegistry& m_Registry;
        std::unique_lock<std::mutex> m_Lock;
    };

    SubmissionScope AcquireSubmissionScope()
    {
        return SubmissionScope(*this);
    }

    std::shared_ptr<ResourceStateRegistration> AcquireResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState)
    {
//Modify Begin:2026-08-12 by BestHui
        Assert(resource != nullptr, "Cannot register a null D3D12 resource state.");
//Modify End

        std::lock_guard lock(m_Mutex);
        if (const auto registration = m_Registrations[resource].lock())
        {
            return registration;
        }

        m_States[resource].SetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, initialState);
        auto registration = std::shared_ptr<ResourceStateRegistration>(
            new ResourceStateRegistration(weak_from_this(), resource));
        m_Registrations[resource] = registration;
        return registration;
    }

private:
    friend class ResourceStateRegistration;

    void ReleaseResource(ID3D12Resource* resource)
    {
        std::lock_guard lock(m_Mutex);
        m_States.erase(resource);
        m_Registrations.erase(resource);
    }

    std::mutex m_Mutex;
    ResourceStateMap m_States;
    std::unordered_map<ID3D12Resource*, std::weak_ptr<ResourceStateRegistration>> m_Registrations;
};

inline ResourceStateRegistration::~ResourceStateRegistration()
{
    if (const std::shared_ptr<ResourceStateRegistry> registry = m_Registry.lock())
    {
        registry->ReleaseResource(m_Resource);
    }
}
//Modify End
