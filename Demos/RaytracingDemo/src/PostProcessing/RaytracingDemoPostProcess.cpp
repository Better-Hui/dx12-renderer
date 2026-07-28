//Modify Begin:2026-07-28 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

void RaytracingDemo::PresentWithExternalPostProcess(const RenderGraph::RenderMetadata& metadata)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    const uint32_t renderWidth = static_cast<uint32_t>(m_Width);
    const uint32_t renderHeight = static_cast<uint32_t>(m_Height);

//Modify Begin:2026-07-28 by BestHui
    RenderGraph::ResourceId displayColor = DemoResourceIds::SceneColor;
    if (m_CudaBloom.IsEnabled())
    {
        m_RenderGraph->CopyTexture(metadata, DemoResourceIds::SceneColor, DemoResourceIds::PostProcessColor, true);

        const auto& postProcessColor = m_RenderGraph->GetTexture(DemoResourceIds::PostProcessColor);
        m_RenderGraph->TransitionTexture(metadata, DemoResourceIds::PostProcessColor, D3D12_RESOURCE_STATE_COMMON, true);
        m_CudaBloom.ExecuteInPlace(*postProcessColor, renderWidth, renderHeight);
        displayColor = DemoResourceIds::PostProcessColor;
    }
//Modify End

    m_RenderGraph->PresentWithOverlay(
        PWindow,
//Modify Begin:2026-07-28 by BestHui
        displayColor,
//Modify End
        [this](CommandList& cmd)
        {
            DrawPostBloomOverlays(cmd);
        });
}
//Modify End
