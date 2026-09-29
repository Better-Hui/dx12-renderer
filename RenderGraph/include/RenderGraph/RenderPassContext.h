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
            , m_BarrierContext(commandList)
            , m_PreviousBarrierContext(commandList.SetActiveBarrierContext(&m_BarrierContext))
        {
        }

        ~RenderPassContext()
        {
            // Destruction only detaches the context. Barrier submission is an
            // explicit operation so a failure cannot be silently swallowed.
            m_CommandList.SetActiveBarrierContext(m_PreviousBarrierContext);
        }

        RenderPassContext(const RenderPassContext&) = delete;
        RenderPassContext& operator=(const RenderPassContext&) = delete;

        CommandList& GetCommandList() const noexcept { return m_CommandList; }
        BarrierContext& GetBarrierContext() noexcept { return m_BarrierContext; }
        const BarrierContext& GetBarrierContext() const noexcept { return m_BarrierContext; }

        void Transition(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            bool uavBefore = false,
            UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            m_BarrierContext.Transition(resource, stateAfter, uavBefore, subresource);
        }

        void Transition(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES stateAfter,
            bool uavBefore = false,
            UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            m_BarrierContext.Transition(resource, stateAfter, uavBefore, subresource);
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
                    m_BarrierContext.PrepareResource(
                        *texture, D3D12_RESOURCE_STATE_RENDER_TARGET, false);
                }
            }

            const auto& depthTexture = renderTarget.GetTexture(DepthStencil);
            if (depthTexture != nullptr && depthTexture->IsValid())
            {
                m_BarrierContext.PrepareResource(
                    *depthTexture,
                    readonlyDepth ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE,
                    false);
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
        BarrierContext m_BarrierContext;
        BarrierContext* m_PreviousBarrierContext = nullptr;
        bool m_Finished = false;
    };
}

//Modify End
