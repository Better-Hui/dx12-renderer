#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <Framework/Rendering/Lighting/ActivePixelList.h>

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
//Modify Begin:2026-08-20 by Hui
        const RenderGraph::RenderMetadataExpression<uint32_t> displayWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_DisplayWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> displayHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_DisplayHeight; };
//Modify End

//Modify Begin:2026-08-17 by Hui
        std::vector<RenderGraph::TextureDescription> textureDescriptions = {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, renderWidthExpression, renderHeightExpression, OUTPUT_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::SceneColor, renderWidthExpression, renderHeightExpression, SCENE_COLOR_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_SHARED, true },
            { ResourceIds::GBufferAlbedoOcclusion, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferSpecularSmoothness, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferNormal, renderWidthExpression, renderHeightExpression, GBUFFER_NORMAL_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferEmissionMetallic, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferPosition, renderWidthExpression, renderHeightExpression, GBUFFER_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::MotionVector, renderWidthExpression, renderHeightExpression, MOTION_VECTOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::DirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::IndirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear, D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::HistoryColor, renderWidthExpression, renderHeightExpression, ACCUMULATION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::NoisyRadiance, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNoisyRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDDenoisedRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDNormalRoughness, renderWidthExpression, renderHeightExpression, NRD_NORMAL_ROUGHNESS_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDViewZ, renderWidthExpression, renderHeightExpression, NRD_VIEWZ_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NRDMotion, renderWidthExpression, renderHeightExpression, NRD_MOTION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::DepthBuffer, renderWidthExpression, renderHeightExpression, DEPTH_FORMAT, { 1.0f, 0u }, RenderGraph::ResourceInitAction::Clear },
        };
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

    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions(const bool includeCompactedPathTracing)
    {
//Modify Begin:2026-08-19 by Hui
        if (!includeCompactedPathTracing)
        {
            return {};
        }
        const RenderGraph::RenderMetadataExpression<size_t> activePixelCapacity = [](const RenderGraph::RenderMetadata& metadata)
        {
            return static_cast<size_t>(metadata.m_ScreenWidth) * metadata.m_ScreenHeight;
        };
        return {
            {
                ResourceIds::ActiveRayPixelIndices,
                activePixelCapacity,
                sizeof(uint32_t),
                RenderGraph::ResourceInitAction::Discard,
                RenderGraph::BufferKind::Structured,
                RenderGraph::BufferUsage::ShaderResource | RenderGraph::BufferUsage::UnorderedAccess,
            },
            {
                ResourceIds::ActiveRayPixelCount,
                [](const RenderGraph::RenderMetadata&) { return size_t{ 4u }; },
                size_t{ 1u },
                RenderGraph::ResourceInitAction::Discard,
                RenderGraph::BufferKind::Raw,
                RenderGraph::BufferUsage::ShaderResource | RenderGraph::BufferUsage::UnorderedAccess,
            },
            {
                ResourceIds::ActivePixelDispatchData,
                [](const RenderGraph::RenderMetadata&) { return sizeof(ActivePixelDispatchDiagnostics); },
                size_t{ 1u },
                RenderGraph::ResourceInitAction::Discard,
                RenderGraph::BufferKind::Raw,
                RenderGraph::BufferUsage::UnorderedAccess,
            },
        };
//Modify End
    }

    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions(
        const bool includeDLSS,
        const bool includeFrameGeneration,
        const bool includeCompactedPathTracing)
    {
//Modify Begin:2026-08-20 by Hui
        std::vector<RenderGraph::TokenDescription> tokenDescriptions = {
            { ResourceIds::BaseResourcesFinishedToken },
            { ResourceIds::SceneResourcesReadyToken },
            { ResourceIds::MeshletCounterResetToken },
            { ResourceIds::MeshletCullFinishedToken },
            { ResourceIds::SkyboxFinishedToken },
            { ResourceIds::DirectLightingFinishedToken },
            { ResourceIds::IndirectLightingFinishedToken },
            { ResourceIds::RayTracingFinishedToken },
            { ResourceIds::DenoiseFinishedToken },
            { ResourceIds::CudaBloomFinishedToken },
            { ResourceIds::DebugOutputFinishedToken },
        };
        if (includeCompactedPathTracing)
        {
            tokenDescriptions.emplace_back(ResourceIds::ActiveRayPixelCompactionFinishedToken);
            tokenDescriptions.emplace_back(ResourceIds::ActivePixelDispatchFinalizedToken);
            tokenDescriptions.emplace_back(ResourceIds::ActivePixelComputeDispatchReadyToken);
            tokenDescriptions.emplace_back(ResourceIds::ActiveRayPixelCountReadbackFinishedToken);
            tokenDescriptions.emplace_back(ResourceIds::DxrCompactedDispatchTemplateFinishedToken);
            tokenDescriptions.emplace_back(ResourceIds::DirectLightingIndirectArgumentsReadyToken);
            tokenDescriptions.emplace_back(ResourceIds::IndirectLightingIndirectArgumentsReadyToken);
        }
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
