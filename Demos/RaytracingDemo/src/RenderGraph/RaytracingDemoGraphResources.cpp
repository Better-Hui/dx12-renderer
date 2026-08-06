#include <RenderGraph/RaytracingDemoGraphResources.h>

namespace RaytracingDemoRenderGraph
{
    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions()
    {
        const RenderGraph::RenderMetadataExpression<uint32_t> renderWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> renderHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenHeight; };

        return {
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
            { ResourceIds::DirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
//Modify Begin:2026-08-05 by BestHui
            { ResourceIds::ReSTIRDIReservoirA, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIReservoirB, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIReservoirAState, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIReservoirBState, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryPositionA, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryPositionB, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryNormalRoughnessA, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryNormalRoughnessB, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryDiffuseMetallicA, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistoryDiffuseMetallicB, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistorySpecularOcclusionA, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIHistorySpecularOcclusionB, renderWidthExpression, renderHeightExpression, RESTIR_DI_HISTORY_SHADING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Preserve, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_HEAP_FLAG_NONE, true },
            { ResourceIds::ReSTIRDIRISReservoir, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::ReSTIRDIRISReservoirState, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::ReSTIRDITemporalReservoir, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::ReSTIRDITemporalReservoirState, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::ReSTIRDISpatialReservoir, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::ReSTIRDISpatialReservoirState, renderWidthExpression, renderHeightExpression, RESTIR_DI_RESERVOIR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
//Modify End
//Modify Begin:2026-07-30 by BestHui
            { ResourceIds::IndirectLighting, renderWidthExpression, renderHeightExpression, LIGHTING_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard, D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAG_NONE, true },
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
            { ResourceIds::ReSTIRDIRISFinishedToken },
            { ResourceIds::ReSTIRDITemporalFinishedToken },
            { ResourceIds::ReSTIRDISpatialFinishedToken },
            { ResourceIds::ReSTIRDIShadeFinishedToken },
            { ResourceIds::IndirectLightingFinishedToken },
            { ResourceIds::RayTracingFinishedToken },
            { ResourceIds::DenoiseFinishedToken },
            { ResourceIds::CudaBloomFinishedToken },
//Modify Begin:2026-07-31 by BestHui
            { ResourceIds::DebugOutputFinishedToken },
//Modify End
        };
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

    LightingResources GetLightingResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.GetTexture(ResourceIds::DirectLighting),
            context.GetTexture(ResourceIds::IndirectLighting),
            context.GetTexture(ResourceIds::HistoryColor),
            context.GetTexture(ResourceIds::NoisyRadiance),
            context.GetTexture(ResourceIds::NRDNoisyRadiance),
            context.GetTexture(ResourceIds::SceneColor),
        };
    }

//Modify Begin:2026-08-05 by BestHui
    ReSTIRDIResources GetReSTIRDIResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.GetTexture(ResourceIds::ReSTIRDIReservoirA),
            context.GetTexture(ResourceIds::ReSTIRDIReservoirB),
            context.GetTexture(ResourceIds::ReSTIRDIReservoirAState),
            context.GetTexture(ResourceIds::ReSTIRDIReservoirBState),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryPositionA),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryPositionB),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryNormalRoughnessA),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryNormalRoughnessB),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryDiffuseMetallicA),
            context.GetTexture(ResourceIds::ReSTIRDIHistoryDiffuseMetallicB),
            context.GetTexture(ResourceIds::ReSTIRDIHistorySpecularOcclusionA),
            context.GetTexture(ResourceIds::ReSTIRDIHistorySpecularOcclusionB),
            context.GetTexture(ResourceIds::ReSTIRDIRISReservoir),
            context.GetTexture(ResourceIds::ReSTIRDIRISReservoirState),
            context.GetTexture(ResourceIds::ReSTIRDITemporalReservoir),
            context.GetTexture(ResourceIds::ReSTIRDITemporalReservoirState),
            context.GetTexture(ResourceIds::ReSTIRDISpatialReservoir),
            context.GetTexture(ResourceIds::ReSTIRDISpatialReservoirState),
        };
    }
//Modify End

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
