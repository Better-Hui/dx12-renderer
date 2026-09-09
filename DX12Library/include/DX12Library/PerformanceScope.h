//Modify Begin:2026-09-09 by Hui
#pragma once

#include "DiagnosticTelemetry.h"

#if !defined(DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES)
#if defined(_DEBUG)
#define DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES 1
#else
#define DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES 0
#endif
#endif

#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace DX12Diagnostics
{
#if defined(DX12_RENDERER_FULL_PERFORMANCE_SCOPES) && DX12_RENDERER_FULL_PERFORMANCE_SCOPES
    class ScopedCpuPerformanceScope final
    {
    public:
        ScopedCpuPerformanceScope(
            DiagnosticTelemetrySink* sink,
            uint64_t frameIndex,
            std::string_view name,
            std::string_view queueName,
            uint64_t correlationId = 0,
            std::string_view scopeKind = "cpu") noexcept
            : m_Sink(sink)
            , m_FrameIndex(frameIndex)
            , m_CorrelationId(correlationId)
        {
            if (m_Sink == nullptr)
            {
                return;
            }

            try
            {
                m_Name.assign(name);
                m_QueueName.assign(queueName);
                m_ScopeKind.assign(scopeKind);
                std::vector<uint64_t>& scopeStack = GetScopeStack();
                m_ParentScopeId = scopeStack.empty() ? 0u : scopeStack.back();
                m_ScopeDepth = static_cast<uint64_t>(scopeStack.size());
                m_ScopeId = s_NextScopeId.fetch_add(1u, std::memory_order_relaxed);
                scopeStack.push_back(m_ScopeId);
                m_StartTime = std::chrono::steady_clock::now();
                m_Active = true;
            }
            catch (...)
            {
                m_Sink = nullptr;
            }
        }

        ~ScopedCpuPerformanceScope() noexcept
        {
            if (!m_Active)
            {
                return;
            }

            const double durationMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_StartTime).count();
            std::vector<uint64_t>& scopeStack = GetScopeStack();
            if (!scopeStack.empty() && scopeStack.back() == m_ScopeId)
            {
                scopeStack.pop_back();
            }

            try
            {
                m_Sink->RecordTelemetry({
                    .Category = "profiler.cpu.scope",
                    .Name = m_Name,
                    .FrameIndex = m_FrameIndex,
                    .CorrelationId = m_CorrelationId,
                    .Fields = {
                        { "queue", m_QueueName },
                        { "scope_kind", m_ScopeKind },
                        { "scope_id", m_ScopeId },
                        { "parent_scope_id", m_ParentScopeId },
                        { "scope_depth", m_ScopeDepth },
                        { "cpu_duration_ms", durationMilliseconds },
                    },
                });
            }
            catch (...)
            {
            }
        }

        ScopedCpuPerformanceScope(const ScopedCpuPerformanceScope&) = delete;
        ScopedCpuPerformanceScope& operator=(const ScopedCpuPerformanceScope&) = delete;

    private:
        static std::vector<uint64_t>& GetScopeStack() noexcept
        {
            static thread_local std::vector<uint64_t> scopeStack;
            return scopeStack;
        }

        inline static std::atomic<uint64_t> s_NextScopeId = 1u;

        DiagnosticTelemetrySink* m_Sink = nullptr;
        std::chrono::steady_clock::time_point m_StartTime = {};
        std::string m_Name;
        std::string m_QueueName;
        std::string m_ScopeKind;
        uint64_t m_FrameIndex = DiagnosticTelemetryEvent::NoFrame;
        uint64_t m_CorrelationId = 0;
        uint64_t m_ScopeId = 0;
        uint64_t m_ParentScopeId = 0;
        uint64_t m_ScopeDepth = 0;
        bool m_Active = false;
    };
#else
    class ScopedCpuPerformanceScope final
    {
    public:
        ScopedCpuPerformanceScope(
            DiagnosticTelemetrySink* sink,
            const uint64_t frameIndex,
            const std::string_view name,
            const std::string_view queueName,
            const uint64_t correlationId = 0,
            const std::string_view scopeKind = "cpu") noexcept
            : m_Sink(sink)
            , m_FrameIndex(frameIndex)
            , m_CorrelationId(correlationId)
        {
            if (m_Sink == nullptr)
            {
                return;
            }

            std::array<uint64_t, kMaxScopeDepth>& scopeStack = GetScopeStack();
            size_t& scopeStackSize = GetScopeStackSize();
            m_ParentScopeId = scopeStackSize == 0u ? 0u : scopeStack[scopeStackSize - 1u];
            m_ScopeDepth = static_cast<uint64_t>(scopeStackSize);
            m_ScopeId = s_NextScopeId.fetch_add(1u, std::memory_order_relaxed);
            if (scopeStackSize < kMaxScopeDepth)
            {
                scopeStack[scopeStackSize++] = m_ScopeId;
            }
            CopyString(m_Name, m_NameLength, name);
            CopyString(m_QueueName, m_QueueNameLength, queueName);
            CopyString(m_ScopeKind, m_ScopeKindLength, scopeKind);
            m_StartTime = std::chrono::steady_clock::now();
            m_Active = true;
        }

        ~ScopedCpuPerformanceScope() noexcept
        {
            if (!m_Active)
            {
                return;
            }

            const double durationMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_StartTime).count();
            std::array<uint64_t, kMaxScopeDepth>& scopeStack = GetScopeStack();
            size_t& scopeStackSize = GetScopeStackSize();
            if (scopeStackSize != 0u && scopeStack[scopeStackSize - 1u] == m_ScopeId)
            {
                --scopeStackSize;
            }

            m_Sink->RecordPerformanceScope({
                .FrameIndex = m_FrameIndex,
                .CorrelationId = m_CorrelationId,
                .ScopeId = m_ScopeId,
                .ParentScopeId = m_ParentScopeId,
                .ScopeDepth = m_ScopeDepth,
                .Name = std::string_view(m_Name.data(), m_NameLength),
                .QueueName = std::string_view(m_QueueName.data(), m_QueueNameLength),
                .ScopeKind = std::string_view(m_ScopeKind.data(), m_ScopeKindLength),
                .DurationMilliseconds = durationMilliseconds,
            });
        }

        ScopedCpuPerformanceScope(const ScopedCpuPerformanceScope&) = delete;
        ScopedCpuPerformanceScope& operator=(const ScopedCpuPerformanceScope&) = delete;

    private:
        static constexpr size_t kMaxScopeDepth = 64u;
        static constexpr size_t kNameCapacity = 160u;
        static constexpr size_t kQueueNameCapacity = 32u;
        static constexpr size_t kScopeKindCapacity = 64u;

        template <size_t Capacity>
        static void CopyString(
            std::array<char, Capacity>& destination,
            size_t& destinationLength,
            const std::string_view source) noexcept
        {
            destinationLength = (std::min)(source.size(), Capacity - 1u);
            if (destinationLength != 0u)
            {
                std::memcpy(destination.data(), source.data(), destinationLength);
            }
            destination[destinationLength] = '\0';
        }

        static std::array<uint64_t, kMaxScopeDepth>& GetScopeStack() noexcept
        {
            static thread_local std::array<uint64_t, kMaxScopeDepth> scopeStack = {};
            return scopeStack;
        }

        static size_t& GetScopeStackSize() noexcept
        {
            static thread_local size_t scopeStackSize = 0u;
            return scopeStackSize;
        }

        inline static std::atomic<uint64_t> s_NextScopeId = 1u;

        DiagnosticTelemetrySink* m_Sink = nullptr;
        std::chrono::steady_clock::time_point m_StartTime = {};
        std::array<char, kNameCapacity> m_Name = {};
        std::array<char, kQueueNameCapacity> m_QueueName = {};
        std::array<char, kScopeKindCapacity> m_ScopeKind = {};
        uint64_t m_FrameIndex = DiagnosticTelemetryEvent::NoFrame;
        uint64_t m_CorrelationId = 0;
        uint64_t m_ScopeId = 0;
        uint64_t m_ParentScopeId = 0;
        uint64_t m_ScopeDepth = 0;
        size_t m_NameLength = 0u;
        size_t m_QueueNameLength = 0u;
        size_t m_ScopeKindLength = 0u;
        bool m_Active = false;
    };
#endif
}

#define DX12_RENDERER_PERFORMANCE_SCOPE_CONCATENATE_IMPL(left, right) left##right
#define DX12_RENDERER_PERFORMANCE_SCOPE_CONCATENATE(left, right) \
    DX12_RENDERER_PERFORMANCE_SCOPE_CONCATENATE_IMPL(left, right)
#define DX12_CPU_PERFORMANCE_SCOPE(sink, frameIndex, name, queueName, correlationId, scopeKind) \
    ::DX12Diagnostics::ScopedCpuPerformanceScope \
        DX12_RENDERER_PERFORMANCE_SCOPE_CONCATENATE(dx12CpuPerformanceScope_, __COUNTER__)( \
            sink, frameIndex, name, queueName, correlationId, scopeKind)

#else

#define DX12_CPU_PERFORMANCE_SCOPE(...) static_cast<void>(0)

#endif
//Modify End
