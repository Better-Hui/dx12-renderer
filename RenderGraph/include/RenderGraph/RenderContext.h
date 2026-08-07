#pragma once

#include <memory>
#include "RenderMetadata.h"
#include "RenderTargetInfo.h"
#include "ResourcePool.h"

namespace RenderGraph
{
    class RenderGraphCommandExecutor;

    class FrameContext final
    {
    public:
        const RenderMetadata& GetMetadata() const { return m_Metadata; }
        const RenderTargetInfo& GetRenderTargetInfo() const { return m_RenderTargetInfo; }

        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
        const std::shared_ptr<Buffer>& GetBuffer(ResourceId resourceId) const;
        const Resource& GetResource(ResourceId resourceId) const;

    private:
        friend class RenderGraphCommandExecutor;

        FrameContext(std::shared_ptr<ResourcePool> resourcePool, const RenderMetadata& metadata)
            : m_ResourcePool(std::move(resourcePool))
            , m_Metadata(metadata)
        {
        }

        void SetRenderTargetInfo(RenderTargetInfo renderTargetInfo)
        {
            m_RenderTargetInfo = std::move(renderTargetInfo);
        }

        std::shared_ptr<ResourcePool> m_ResourcePool = nullptr;
        RenderMetadata m_Metadata = {};
        RenderTargetInfo m_RenderTargetInfo = {};
    };

    using RenderContext = FrameContext;
}
