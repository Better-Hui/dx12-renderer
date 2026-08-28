#pragma once

#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <RenderGraph/ResourceDescription.h>
#include <RenderGraph/RenderPass.h>

#include <d3d12.h>

#include <memory>
#include <vector>

namespace RaytracingDemoRenderGraph
{
    constexpr FLOAT GBUFFER_CLEAR_COLOR[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr FLOAT OUTPUT_CLEAR_COLOR[] = { 0.4f, 0.6f, 0.9f, 1.0f };
    constexpr DXGI_FORMAT GBUFFER_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT GBUFFER_NORMAL_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
    constexpr DXGI_FORMAT GBUFFER_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT MOTION_VECTOR_FORMAT = DXGI_FORMAT_R16G16_FLOAT;
    constexpr DXGI_FORMAT LIGHTING_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT ACCUMULATION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT NRD_RADIANCE_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT NRD_NORMAL_ROUGHNESS_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT NRD_VIEWZ_FORMAT = DXGI_FORMAT_R32_FLOAT;
    constexpr DXGI_FORMAT NRD_MOTION_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
//Modify Begin:2026-07-28 by Hui
    constexpr DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
//Modify End
    constexpr DXGI_FORMAT OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
//Modify Begin:2026-08-28 by Hui
    constexpr DXGI_FORMAT HDR10_LINEAR_OUTPUT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//Modify End
//Modify Begin:2026-08-07 by Hui
    constexpr DXGI_FORMAT DLSS_NORMAL_ROUGHNESS_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//Modify End

    class ResourceIds
    {
    public:
        static inline const RenderGraph::ResourceId GBufferAlbedoOcclusion = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferAlbedoOcclusion");
        static inline const RenderGraph::ResourceId GBufferSpecularSmoothness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferSpecularSmoothness");
        static inline const RenderGraph::ResourceId GBufferNormal = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferNormal");
        static inline const RenderGraph::ResourceId GBufferEmissionMetallic = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferEmissionMetallic");
        static inline const RenderGraph::ResourceId GBufferPosition = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferPosition");
        static inline const RenderGraph::ResourceId MotionVector = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.MotionVector");
        static inline const RenderGraph::ResourceId DirectLighting = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DirectLighting");
        static inline const RenderGraph::ResourceId IndirectLighting = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.IndirectLighting");
//Modify Begin:2026-08-20 by Hui
        static inline const RenderGraph::ResourceId ActiveRayPixelIndices = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActiveRayPixelIndices");
        static inline const RenderGraph::ResourceId ActiveRayPixelCount = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActiveRayPixelCount");
        static inline const RenderGraph::ResourceId ActivePixelDispatchData = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActivePixelDispatchData");
//Modify End
        static inline const RenderGraph::ResourceId SceneColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SceneColor");
//Modify Begin:2026-08-17 by Hui
        static inline const RenderGraph::ResourceId BloomOutput = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.BloomOutput");
//Modify End
//Modify Begin:2026-08-25 by Hui
        static inline const RenderGraph::ResourceId CopyQueueValidationColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.CopyQueueValidationColor");
        static inline const RenderGraph::ResourceId CopyQueueValidationComputeColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.CopyQueueValidationComputeColor");
//Modify End
//Modify Begin:2026-08-23 by Hui
        static inline const RenderGraph::ResourceId AutoExposureOutput = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.AutoExposureOutput");
//Modify End
//Modify Begin:2026-08-07 by Hui
        static inline const RenderGraph::ResourceId DLSSOutput = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DLSSOutput");
        static inline const RenderGraph::ResourceId DLSSNormalRoughness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DLSSNormalRoughness");
        static inline const RenderGraph::ResourceId FrameGenerationHudLess = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.FrameGenerationHudLess");
//Modify End
        static inline const RenderGraph::ResourceId HistoryColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.HistoryColor");
        static inline const RenderGraph::ResourceId NoisyRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NoisyRadiance");
        static inline const RenderGraph::ResourceId NRDNoisyRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDNoisyRadiance");
        static inline const RenderGraph::ResourceId NRDDenoisedRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDDenoisedRadiance");
        static inline const RenderGraph::ResourceId NRDNormalRoughness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDNormalRoughness");
        static inline const RenderGraph::ResourceId NRDViewZ = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDViewZ");
        static inline const RenderGraph::ResourceId NRDMotion = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDMotion");
        static inline const RenderGraph::ResourceId DepthBuffer = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DepthBuffer");

        static inline const RenderGraph::ResourceId BaseResourcesFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.BaseResourcesFinished");
        static inline const RenderGraph::ResourceId SceneResourcesReadyToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SceneResourcesReady");
        static inline const RenderGraph::ResourceId MeshletCounterResetToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.MeshletCounterReset");
        static inline const RenderGraph::ResourceId MeshletCullFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.MeshletCullFinished");
        static inline const RenderGraph::ResourceId DirectLightingIndirectArgumentsReadyToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DirectLightingIndirectArgumentsReady");
        static inline const RenderGraph::ResourceId IndirectLightingIndirectArgumentsReadyToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.IndirectLightingIndirectArgumentsReady");
//Modify Begin:2026-08-20 by Hui
        static inline const RenderGraph::ResourceId ActiveRayPixelCompactionFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActiveRayPixelCompactionFinished");
        static inline const RenderGraph::ResourceId ActivePixelDispatchFinalizedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActivePixelDispatchFinalized");
        static inline const RenderGraph::ResourceId ActivePixelComputeDispatchReadyToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActivePixelComputeDispatchReady");
        static inline const RenderGraph::ResourceId ActiveRayPixelCountReadbackFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ActiveRayPixelCountReadbackFinished");
        static inline const RenderGraph::ResourceId DxrCompactedDispatchTemplateFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DxrCompactedDispatchTemplateFinished");
//Modify End
        static inline const RenderGraph::ResourceId SkyboxFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SkyboxFinished");
        static inline const RenderGraph::ResourceId DirectLightingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DirectLightingFinished");
        static inline const RenderGraph::ResourceId IndirectLightingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.IndirectLightingFinished");
        static inline const RenderGraph::ResourceId RayTracingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.RayTracingFinished");
        static inline const RenderGraph::ResourceId DenoiseFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DenoiseFinished");
//Modify Begin:2026-08-24 by Hui
        static inline const RenderGraph::ResourceId BloomFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.BloomFinished");
//Modify End
//Modify Begin:2026-08-25 by Hui
        static inline const RenderGraph::ResourceId CopyQueueValidationFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.CopyQueueValidationFinished");
        static inline const RenderGraph::ResourceId CopyQueueValidationComputeFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.CopyQueueValidationComputeFinished");
        static inline const RenderGraph::ResourceId DynamicRayTracingGeometryUploadedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DynamicRayTracingGeometryUploaded");
        static inline const RenderGraph::ResourceId DynamicRayTracingUpdatedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DynamicRayTracingUpdated");
//Modify End
//Modify Begin:2026-08-23 by Hui
        static inline const RenderGraph::ResourceId AutoExposureFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.AutoExposureFinished");
//Modify End
//Modify Begin:2026-08-07 by Hui
        static inline const RenderGraph::ResourceId DLSSFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DLSSFinished");
        static inline const RenderGraph::ResourceId FrameGenerationHudLessFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.FrameGenerationHudLessFinished");
//Modify End
//Modify Begin:2026-07-31 by Hui
        static inline const RenderGraph::ResourceId DebugOutputFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DebugOutputFinished");
//Modify End
    };

    struct FrameGBufferResources
    {
        std::shared_ptr<Texture> AlbedoOcclusion;
        std::shared_ptr<Texture> SpecularSmoothness;
        std::shared_ptr<Texture> Normal;
        std::shared_ptr<Texture> EmissionMetallic;
        std::shared_ptr<Texture> Position;
        std::shared_ptr<Texture> MotionVector;
        std::shared_ptr<Texture> Depth;
    };

    struct LightingResources
    {
        std::shared_ptr<Texture> NoisyRadiance;
        std::shared_ptr<Texture> NRDNoisyRadiance;
        std::shared_ptr<Texture> SceneColor;
    };


    struct NRDResources
    {
        std::shared_ptr<Texture> NormalRoughness;
        std::shared_ptr<Texture> ViewZ;
        std::shared_ptr<Texture> Motion;
        std::shared_ptr<Texture> DenoisedRadiance;
    };

    FrameGBufferResources GetFrameGBufferResources(const RenderGraph::RenderContext& context);
    NRDResources GetNRDResources(const RenderGraph::RenderContext& context);

//Modify Begin:2026-08-07 by Hui
    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions(
        bool includeDLSS,
        bool includeFrameGeneration,
        bool includeRayReconstruction,
        bool includeFrameworkBloom,
        bool includeCopyQueueValidation,
        bool hdr10Output);
//Modify End
    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions(bool includeCompactedPathTracing);
//Modify Begin:2026-08-07 by Hui
    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions(
        bool includeDLSS,
        bool includeFrameGeneration,
        bool includeCompactedPathTracing,
        bool includeCopyQueueValidation,
        bool includeDynamicRayTracingUpdate);
//Modify End
}
