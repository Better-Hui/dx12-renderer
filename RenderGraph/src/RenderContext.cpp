//Modify Begin:2026-08-19 by Hui
#include "RenderContext.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

const std::shared_ptr<Texture>& RenderGraph::FrameContext::GetTexture(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetTexture(resourceId);
}

const std::shared_ptr<Buffer>& RenderGraph::FrameContext::GetBuffer(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetBuffer(resourceId);
}

const Resource& RenderGraph::FrameContext::GetResource(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetResource(resourceId);
}
//Modify End
