//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

#include <mutex>
#include <unordered_map>

class ResourceStateRegistry final
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

    void RegisterResource(ID3D12Resource* resource, const D3D12_RESOURCE_STATES state)
    {
        if (resource == nullptr)
        {
            return;
        }

        std::lock_guard lock(m_Mutex);
        m_States[resource].SetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state);
    }

    void RemoveResource(ID3D12Resource* resource)
    {
        if (resource == nullptr)
        {
            return;
        }

        std::lock_guard lock(m_Mutex);
        m_States.erase(resource);
    }

private:
    std::mutex m_Mutex;
    ResourceStateMap m_States;
};
//Modify End
