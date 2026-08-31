//Modify Begin:2026-08-31 by Hui
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace DX12Diagnostics
{
    enum class DiagnosticResourceAccess : uint8_t
    {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
    };

    constexpr DiagnosticResourceAccess operator|(
        const DiagnosticResourceAccess left,
        const DiagnosticResourceAccess right) noexcept
    {
        return static_cast<DiagnosticResourceAccess>(
            static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr bool HasDiagnosticResourceAccess(
        const DiagnosticResourceAccess value,
        const DiagnosticResourceAccess requested) noexcept
    {
        return (static_cast<uint8_t>(value) & static_cast<uint8_t>(requested)) ==
            static_cast<uint8_t>(requested);
    }

    struct DiagnosticDeclaredResource final
    {
        ID3D12Resource* ResourceIdentity = nullptr;
        uint64_t LogicalResourceId = 0;
        std::string LogicalResourceName;
        DiagnosticResourceAccess Access = DiagnosticResourceAccess::None;
    };

    struct DiagnosticRenderPassScopeDesc final
    {
        uint64_t CorrelationId = 0;
        uint64_t FrameIndex = 0;
        std::string PassName;
        std::string QueueName;
        std::vector<DiagnosticDeclaredResource> DeclaredResources;
    };

    struct DiagnosticResourceAccessValidation final
    {
        bool Declared = false;
        bool AccessAllowed = false;
        uint64_t LogicalResourceId = 0;
        std::string LogicalResourceName;
    };

    class DiagnosticRenderPassScope final
    {
    public:
        explicit DiagnosticRenderPassScope(DiagnosticRenderPassScopeDesc desc) noexcept
            : m_Desc(std::move(desc))
            , m_Previous(Current())
        {
            Current() = this;
        }

        ~DiagnosticRenderPassScope()
        {
            Current() = m_Previous;
        }

        DiagnosticRenderPassScope(const DiagnosticRenderPassScope&) = delete;
        DiagnosticRenderPassScope& operator=(const DiagnosticRenderPassScope&) = delete;

        [[nodiscard]] static DiagnosticRenderPassScope* GetCurrent() noexcept
        {
            return Current();
        }

        [[nodiscard]] DiagnosticResourceAccessValidation ValidateAccess(
            ID3D12Resource* resourceIdentity,
            DiagnosticResourceAccess access) noexcept
        {
            ++m_ObservedAccessCount;
            for (const DiagnosticDeclaredResource& declared : m_Desc.DeclaredResources)
            {
                if (declared.ResourceIdentity != resourceIdentity)
                {
                    continue;
                }

                DiagnosticResourceAccessValidation validation = {
                    .Declared = true,
                    .AccessAllowed = HasDiagnosticResourceAccess(declared.Access, access),
                    .LogicalResourceId = declared.LogicalResourceId,
                    .LogicalResourceName = declared.LogicalResourceName,
                };
                if (validation.AccessAllowed)
                {
                    ++m_MatchedAccessCount;
                }
                else
                {
                    ++m_InvalidAccessCount;
                }
                return validation;
            }

            ++m_InvalidAccessCount;
            return {};
        }

        [[nodiscard]] const DiagnosticRenderPassScopeDesc& GetDesc() const noexcept { return m_Desc; }
        [[nodiscard]] uint64_t GetObservedAccessCount() const noexcept { return m_ObservedAccessCount; }
        [[nodiscard]] uint64_t GetMatchedAccessCount() const noexcept { return m_MatchedAccessCount; }
        [[nodiscard]] uint64_t GetInvalidAccessCount() const noexcept { return m_InvalidAccessCount; }

    private:
        static DiagnosticRenderPassScope*& Current() noexcept
        {
            static thread_local DiagnosticRenderPassScope* current = nullptr;
            return current;
        }

        DiagnosticRenderPassScopeDesc m_Desc;
        DiagnosticRenderPassScope* m_Previous = nullptr;
        uint64_t m_ObservedAccessCount = 0;
        uint64_t m_MatchedAccessCount = 0;
        uint64_t m_InvalidAccessCount = 0;
    };
}
//Modify End
