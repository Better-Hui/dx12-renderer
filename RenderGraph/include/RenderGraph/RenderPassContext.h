#pragma once

//Modify Begin:2026-09-29 by Hui

#include <DX12Library/BarrierContext.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/RenderTarget.h>

class CommandList;
class Resource;

namespace RenderGraph
{
    /**
     * Recording context shared by one RenderGraph pass boundary and its body.
     *
     * The graph supplies boundary transitions through this context. Pass code
     * may use the same BarrierContext for local phase transitions and UAV
     * ordering, while the graph remains independent of Framework's higher
     * level binding API.
     */
    class RenderPassContext final
    {
    public:
        explicit RenderPassContext(CommandList& commandList)
            : m_CommandList(commandList)
            , m_BarrierContext(commandList.GetBarrierContext())
        {
        }

        ~RenderPassContext() = default;

        RenderPassContext(const RenderPassContext&) = delete;
        RenderPassContext& operator=(const RenderPassContext&) = delete;

        CommandList& GetCommandList() const noexcept { return m_CommandList; }
        BarrierContext& GetBarrierContext() noexcept { return m_BarrierContext; }
        const BarrierContext& GetBarrierContext() const noexcept { return m_BarrierContext; }

        void Use(
            const Resource& resource,
            D3D12_RESOURCE_STATES state,
            ResourceUse use = ResourceUse::Read,
            bool forceUavBarrier = false,
            UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            m_BarrierContext.Use(resource, state, use, forceUavBarrier, subresource);
        }

        void Use(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES state,
            ResourceUse use = ResourceUse::Read,
            bool forceUavBarrier = false,
            UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            m_BarrierContext.Use(resource, state, use, forceUavBarrier, subresource);
        }

        void Uav(const Resource& resource)
        {
            m_BarrierContext.Uav(resource);
        }

        void Uav(ID3D12Resource* resource)
        {
            m_BarrierContext.Uav(resource);
        }

        void AliasingBeforeFirstUse(const Resource& resource)
        {
            m_BarrierContext.AliasingBeforeFirstUse(resource);
        }

        void SetRenderTarget(
            const RenderTarget& renderTarget,
            bool readonlyDepth = false)
        {
            const auto& textures = renderTarget.GetTextures();
            for (size_t textureIndex = 0; textureIndex < NumAttachmentPoints - 1u; ++textureIndex)
            {
                const auto& texture = textures[textureIndex];
                if (texture != nullptr && texture->IsValid())
                {
                    m_BarrierContext.Use(
                        *texture, D3D12_RESOURCE_STATE_RENDER_TARGET, ResourceUse::Write);
                }
            }

            const auto& depthTexture = renderTarget.GetTexture(DepthStencil);
            if (depthTexture != nullptr && depthTexture->IsValid())
            {
                m_BarrierContext.Use(
                    *depthTexture,
                    readonlyDepth ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE,
                    readonlyDepth ? ResourceUse::Read : ResourceUse::Write);
            }

            m_BarrierContext.Flush();
            m_CommandList.SetRenderTarget(renderTarget, -1, 0, true, readonlyDepth);
        }

        void FlushBarriers()
        {
            // Flushing a local barrier batch must not close the pass scope.
            // Pass implementations may use this between internal phases; the
            // executor calls Finish() exactly once after Execute() returns.
            m_BarrierContext.Flush();
        }

        /**
         * Finish one pass recording scope and submit all pending local barriers.
         * The context remains attached until destruction so pass code cannot
         * accidentally install a second active barrier context before Finish.
         */
        void Finish()
        {
            Assert(!m_Finished, "RenderPassContext::Finish may only be called once.");
            m_BarrierContext.Flush();
            m_Finished = true;
        }

    private:
        CommandList& m_CommandList;
        BarrierContext& m_BarrierContext;
        bool m_Finished = false;
    };
}

//Modify End
