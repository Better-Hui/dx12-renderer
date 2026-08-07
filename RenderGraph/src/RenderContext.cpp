//Modify Begin:2026-07-30 by BestHui
#include "RenderContext.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

//Modify Begin:2026-07-30 by BestHui
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
