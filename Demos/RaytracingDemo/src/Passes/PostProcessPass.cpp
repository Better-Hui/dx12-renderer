//Modify Begin:2026-07-28 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/CommandContext.h>
#include <Framework/ShaderResourceView.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

void RaytracingDemo::PresentWithExternalPostProcess(const RenderGraph::RenderMetadata& metadata)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    const uint32_t renderWidth = static_cast<uint32_t>(m_Width);
    const uint32_t renderHeight = static_cast<uint32_t>(m_Height);

    RenderGraph::ResourceId displayColor = DemoResourceIds::SceneColor;
    if (m_CudaBloom.IsEnabled())
    {
//Modify Begin:2026-07-28 by BestHui
        const auto& sceneColor = m_RenderGraph->GetTexture(DemoResourceIds::SceneColor);
        m_RenderGraph->TransitionTexture(metadata, DemoResourceIds::SceneColor, D3D12_RESOURCE_STATE_COMMON, true);
        m_CudaBloom.ExecuteInPlace(*sceneColor, renderWidth, renderHeight);
//Modify End
    }

//Modify Begin:2026-07-28 by BestHui
    m_RenderGraph->PresentWithOverlayBlit(
        PWindow,
        displayColor,
        [this](CommandList& cmd, const std::shared_ptr<Texture>& sourceTexture)
        {
            m_DisplayCompositeShader->Bind(cmd);
            cmd.SetTexture(m_DisplayCompositeShader, "SceneColor", ShaderResourceView(sourceTexture));
            CommandContext(cmd).BindDescriptorSet(m_DisplayCompositeShader->GetDescriptorSet(), PipelineBindPoint::Graphics);
            m_DisplayBlitMesh->Draw(cmd);
        },
        [this](CommandList& cmd)
        {
            DrawPostBloomOverlays(cmd);
        });
//Modify End
}
//Modify End
