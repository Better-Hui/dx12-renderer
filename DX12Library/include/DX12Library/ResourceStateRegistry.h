//Modify Begin:2026-09-29 by Hui
#pragma once

#include "Helpers.h"

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
        ResourceState() = default;

        explicit ResourceState(const D3D12_RESOURCE_STATES state)
            : State(state)
            , HasAllSubresourcesState(true)
        {
        }

        void SetSubresourceState(const UINT subresource, const D3D12_RESOURCE_STATES state)
        {
            if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                State = state;
                SubresourceStates.clear();
                HasAllSubresourcesState = true;
            }
            else
            {
                SubresourceStates[subresource] = state;
            }
        }

        bool HasKnownState(const UINT subresource) const
        {
            return subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                ? HasAllSubresourcesState
                : HasAllSubresourcesState || SubresourceStates.contains(subresource);
        }

        D3D12_RESOURCE_STATES GetSubresourceState(const UINT subresource) const
        {
            const auto iterator = SubresourceStates.find(subresource);
            return iterator != SubresourceStates.end() ? iterator->second : State;
        }

        D3D12_RESOURCE_STATES State;
        bool HasAllSubresourcesState = false;
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
        Assert(resource != nullptr, "Cannot register a null D3D12 resource state.");

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

    // An external renderer owns the resource lifetime and supplies the actual
    // state at the point where the plugin begins recording. Keep the existing
    // registration alive, but replace the cached state with that explicit
    // entry state.
    void SetResourceState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES state,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        Assert(resource != nullptr, "Cannot set a null D3D12 resource state.");
        std::lock_guard lock(m_Mutex);
        m_States[resource].SetSubresourceState(subresource, state);
    }

    bool TryGetResourceState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& state,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const
    {
        Assert(resource != nullptr, "Cannot query a null D3D12 resource state.");
        std::lock_guard lock(m_Mutex);
        const auto iterator = m_States.find(resource);
        if (iterator == m_States.end() || !iterator->second.HasKnownState(subresource))
        {
            return false;
        }
        state = iterator->second.GetSubresourceState(subresource);
        return true;
    }

    /**
     * Forget state supplied by an external renderer after it destroys a resource.
     * An internally owned registration always wins and is left untouched.
     */
    void ForgetExternalResource(ID3D12Resource* resource)
    {
        Assert(resource != nullptr, "Cannot forget a null external D3D12 resource state.");
        std::lock_guard lock(m_Mutex);
        const auto registration = m_Registrations.find(resource);
        if (registration != m_Registrations.end() && !registration->second.expired())
        {
            return;
        }
        m_States.erase(resource);
        m_Registrations.erase(resource);
    }

private:
    friend class ResourceStateRegistration;

    void ReleaseResource(ID3D12Resource* resource)
    {
        std::lock_guard lock(m_Mutex);
        m_States.erase(resource);
        m_Registrations.erase(resource);
    }

    mutable std::mutex m_Mutex;
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
