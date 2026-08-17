#include <RenderGraph/RaytracingDemoGraphResources.h>

namespace RaytracingDemoRenderGraph
{
    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions(
        const bool includeDLSS,
        const bool includeFrameGeneration,
        const bool includeRayReconstruction,
        const bool includeFrameworkBloom)
    {
        const RenderGraph::RenderMetadataExpression<uint32_t> renderWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> renderHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenHeight; };
//Modify Begin:2026-08-07 by BestHui
        const RenderGraph::RenderMetadataExpression<uint32_t> displayWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_DisplayWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> displayHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_DisplayHeight; };
//Modify End

//Modify Begin:2026-08-07 by BestHui
        std::vector<RenderGraph::TextureDescription> textureDescriptions = {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, renderWidthExpression, renderHeightExpression, OUTPUT_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
//Modify Begin:2026-07-28 by BestHui
            { ResourceIds::SceneColor, renderWidthExpression, renderHeightExpression, SCENE_COLOR_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_SHARED, true },
//Modify End
            { ResourceIds::GBufferAlbedoOcclusion, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferSpecularSmoothness, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferNormal, renderWidthExpression, renderHeightExpression, GBUFFER_NORMAL_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferEmissionMetallic, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferPosition, renderWidthExpression, renderHeightExpression, GBUFFER_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::MotionVector, renderWidthExpression, renderHeightExpression, MOTION_VECTOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
//Modify Begin:2026-08-11 by BestHui
            { ResourceIds::DirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
//Modify End
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-11 by BestHui
            { ResourceIds::IndirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear, D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAG_NONE, true },
//Modify End
//Modify End
//Modify Begin:2026-08-06 by BestHui
            { ResourceIds::HistoryColor, renderWidthExpression, renderHeightExpression, ACCUMULATION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
//Modify End
            { ResourceIds::NoisyRadiance, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNoisyRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDDenoisedRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNormalRoughness, renderWidthExpression, renderHeightExpression, NRD_NORMAL_ROUGHNESS_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDViewZ, renderWidthExpression, renderHeightExpression, NRD_VIEWZ_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDMotion, renderWidthExpression, renderHeightExpression, NRD_MOTION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::DepthBuffer, renderWidthExpression, renderHeightExpression, DEPTH_FORMAT, { 1.0f, 0u }, RenderGraph::ResourceInitAction::Clear },
        };
//Modify Begin:2026-08-17 by BestHui
        if (includeFrameworkBloom)
        {
            textureDescriptions.emplace_back(
                ResourceIds::BloomOutput,
                renderWidthExpression,
                renderHeightExpression,
                SCENE_COLOR_FORMAT,
                OUTPUT_CLEAR_COLOR,
                RenderGraph::ResourceInitAction::Discard,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_HEAP_FLAG_NONE,
                true);
        }
//Modify End
        if (includeDLSS)
        {
            textureDescriptions.emplace_back(
                ResourceIds::DLSSOutput,
                displayWidthExpression,
                displayHeightExpression,
                SCENE_COLOR_FORMAT,
                OUTPUT_CLEAR_COLOR,
                RenderGraph::ResourceInitAction::Discard,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_HEAP_FLAG_NONE,
                true);
        }
        if (includeRayReconstruction)
        {
            textureDescriptions.emplace_back(
                ResourceIds::DLSSNormalRoughness,
                renderWidthExpression,
                renderHeightExpression,
                DLSS_NORMAL_ROUGHNESS_FORMAT,
                GBUFFER_CLEAR_COLOR,
                RenderGraph::ResourceInitAction::Discard,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_HEAP_FLAG_NONE,
                true);
        }
        if (includeFrameGeneration)
        {
            textureDescriptions.emplace_back(
                ResourceIds::FrameGenerationHudLess,
                displayWidthExpression,
                displayHeightExpression,
                OUTPUT_FORMAT,
                OUTPUT_CLEAR_COLOR,
                RenderGraph::ResourceInitAction::Discard,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                D3D12_HEAP_FLAG_NONE,
                true);
        }
        return textureDescriptions;
//Modify End
    }

    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions()
    {
        return {};
    }

    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions(
        const bool includeDLSS,
        const bool includeFrameGeneration)
    {
//Modify Begin:2026-08-07 by BestHui
        std::vector<RenderGraph::TokenDescription> tokenDescriptions = {
            { ResourceIds::BaseResourcesFinishedToken },
            { ResourceIds::SkyboxFinishedToken },
            { ResourceIds::DirectLightingFinishedToken },
            { ResourceIds::IndirectLightingFinishedToken },
            { ResourceIds::RayTracingFinishedToken },
            { ResourceIds::DenoiseFinishedToken },
            { ResourceIds::CudaBloomFinishedToken },
//Modify Begin:2026-07-31 by BestHui
            { ResourceIds::DebugOutputFinishedToken },
//Modify End
        };
        if (includeDLSS)
        {
            tokenDescriptions.emplace_back(ResourceIds::DLSSFinishedToken);
        }
        if (includeFrameGeneration)
        {
            tokenDescriptions.emplace_back(ResourceIds::FrameGenerationHudLessFinishedToken);
        }
        return tokenDescriptions;
//Modify End
    }

    FrameGBufferResources GetFrameGBufferResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.GetTexture(ResourceIds::GBufferAlbedoOcclusion),
            context.GetTexture(ResourceIds::GBufferSpecularSmoothness),
            context.GetTexture(ResourceIds::GBufferNormal),
            context.GetTexture(ResourceIds::GBufferEmissionMetallic),
            context.GetTexture(ResourceIds::GBufferPosition),
            context.GetTexture(ResourceIds::MotionVector),
            context.GetTexture(ResourceIds::DepthBuffer),
        };
    }

    NRDResources GetNRDResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.GetTexture(ResourceIds::NRDNormalRoughness),
            context.GetTexture(ResourceIds::NRDViewZ),
            context.GetTexture(ResourceIds::NRDMotion),
            context.GetTexture(ResourceIds::NRDDenoisedRadiance),
        };
    }
}
