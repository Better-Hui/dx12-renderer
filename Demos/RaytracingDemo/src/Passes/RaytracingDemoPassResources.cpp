#include <Passes/RaytracingDemoPassResources.h>

namespace RaytracingDemoRenderGraph
{
    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions()
    {
        const RenderGraph::RenderMetadataExpression<uint32_t> renderWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> renderHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenHeight; };

        return {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, renderWidthExpression, renderHeightExpression, OUTPUT_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferAlbedoOcclusion, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferSpecularSmoothness, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferNormal, renderWidthExpression, renderHeightExpression, GBUFFER_NORMAL_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferEmissionMetallic, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferPosition, renderWidthExpression, renderHeightExpression, GBUFFER_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::MotionVector, renderWidthExpression, renderHeightExpression, MOTION_VECTOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::DirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::IndirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::Accumulation, renderWidthExpression, renderHeightExpression, ACCUMULATION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NoisyRadiance, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNoisyRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDDenoisedRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNormalRoughness, renderWidthExpression, renderHeightExpression, NRD_NORMAL_ROUGHNESS_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDViewZ, renderWidthExpression, renderHeightExpression, NRD_VIEWZ_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDMotion, renderWidthExpression, renderHeightExpression, NRD_MOTION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::DepthBuffer, renderWidthExpression, renderHeightExpression, DEPTH_FORMAT, { 1.0f, 0u }, RenderGraph::ResourceInitAction::Clear },
        };
    }

    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions()
    {
        return {};
    }

    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions()
    {
        return {
            { ResourceIds::BaseResourcesFinishedToken },
            { ResourceIds::SkyboxFinishedToken },
            { ResourceIds::DirectLightingFinishedToken },
            { ResourceIds::IndirectLightingFinishedToken },
            { ResourceIds::RayTracingFinishedToken },
            { ResourceIds::DenoiseFinishedToken },
            { ResourceIds::LightBillboardFinishedToken },
        };
    }

    FrameGBufferResources GetFrameGBufferResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.m_ResourcePool->GetTexture(ResourceIds::GBufferAlbedoOcclusion),
            context.m_ResourcePool->GetTexture(ResourceIds::GBufferSpecularSmoothness),
            context.m_ResourcePool->GetTexture(ResourceIds::GBufferNormal),
            context.m_ResourcePool->GetTexture(ResourceIds::GBufferEmissionMetallic),
            context.m_ResourcePool->GetTexture(ResourceIds::GBufferPosition),
            context.m_ResourcePool->GetTexture(ResourceIds::MotionVector),
            context.m_ResourcePool->GetTexture(ResourceIds::DepthBuffer),
        };
    }

    LightingResources GetLightingResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.m_ResourcePool->GetTexture(ResourceIds::DirectLighting),
            context.m_ResourcePool->GetTexture(ResourceIds::IndirectLighting),
            context.m_ResourcePool->GetTexture(ResourceIds::Accumulation),
            context.m_ResourcePool->GetTexture(ResourceIds::NoisyRadiance),
            context.m_ResourcePool->GetTexture(ResourceIds::NRDNoisyRadiance),
            context.m_ResourcePool->GetTexture(RenderGraph::ResourceIds::GRAPH_OUTPUT),
        };
    }

    NRDResources GetNRDResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.m_ResourcePool->GetTexture(ResourceIds::NRDNormalRoughness),
            context.m_ResourcePool->GetTexture(ResourceIds::NRDViewZ),
            context.m_ResourcePool->GetTexture(ResourceIds::NRDMotion),
            context.m_ResourcePool->GetTexture(ResourceIds::NRDDenoisedRadiance),
        };
    }
}
