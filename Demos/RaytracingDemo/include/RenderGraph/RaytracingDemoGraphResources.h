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
//Modify Begin:2026-08-05 by BestHui
    constexpr DXGI_FORMAT RESTIR_DI_RESERVOIR_FORMAT = DXGI_FORMAT_R32G32B32A32_UINT;
    constexpr DXGI_FORMAT RESTIR_DI_HISTORY_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT RESTIR_DI_HISTORY_SHADING_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//Modify End
    constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
//Modify Begin:2026-07-28 by BestHui
    constexpr DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
//Modify End
    constexpr DXGI_FORMAT OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

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
//Modify Begin:2026-08-05 by BestHui
        static inline const RenderGraph::ResourceId ReSTIRDIReservoirA = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIReservoirA");
        static inline const RenderGraph::ResourceId ReSTIRDIReservoirB = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIReservoirB");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryPositionA = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryPositionA");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryPositionB = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryPositionB");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryNormalRoughnessA = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryNormalRoughnessA");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryNormalRoughnessB = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryNormalRoughnessB");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryDiffuseMetallicA = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryDiffuseMetallicA");
        static inline const RenderGraph::ResourceId ReSTIRDIHistoryDiffuseMetallicB = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistoryDiffuseMetallicB");
        static inline const RenderGraph::ResourceId ReSTIRDIHistorySpecularOcclusionA = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistorySpecularOcclusionA");
        static inline const RenderGraph::ResourceId ReSTIRDIHistorySpecularOcclusionB = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIHistorySpecularOcclusionB");
        static inline const RenderGraph::ResourceId ReSTIRDIRISReservoir = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIRISReservoir");
        static inline const RenderGraph::ResourceId ReSTIRDIBoilingReservoir = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIBoilingReservoir");
        static inline const RenderGraph::ResourceId ReSTIRDISpatialReservoir = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDISpatialReservoir");
//Modify End
        static inline const RenderGraph::ResourceId IndirectLighting = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.IndirectLighting");
        static inline const RenderGraph::ResourceId SceneColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SceneColor");
        static inline const RenderGraph::ResourceId HistoryColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.HistoryColor");
        static inline const RenderGraph::ResourceId NoisyRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NoisyRadiance");
        static inline const RenderGraph::ResourceId NRDNoisyRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDNoisyRadiance");
        static inline const RenderGraph::ResourceId NRDDenoisedRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDDenoisedRadiance");
        static inline const RenderGraph::ResourceId NRDNormalRoughness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDNormalRoughness");
        static inline const RenderGraph::ResourceId NRDViewZ = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDViewZ");
        static inline const RenderGraph::ResourceId NRDMotion = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NRDMotion");
        static inline const RenderGraph::ResourceId DepthBuffer = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DepthBuffer");

        static inline const RenderGraph::ResourceId BaseResourcesFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.BaseResourcesFinished");
        static inline const RenderGraph::ResourceId SkyboxFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SkyboxFinished");
        static inline const RenderGraph::ResourceId DirectLightingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DirectLightingFinished");
        static inline const RenderGraph::ResourceId ReSTIRDIRISFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIRISFinished");
        static inline const RenderGraph::ResourceId ReSTIRDITemporalFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDITemporalFinished");
        static inline const RenderGraph::ResourceId ReSTIRDIBoilingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIBoilingFinished");
        static inline const RenderGraph::ResourceId ReSTIRDISpatialFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDISpatialFinished");
        static inline const RenderGraph::ResourceId ReSTIRDIShadeFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.ReSTIRDIShadeFinished");
        static inline const RenderGraph::ResourceId IndirectLightingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.IndirectLightingFinished");
        static inline const RenderGraph::ResourceId RayTracingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.RayTracingFinished");
        static inline const RenderGraph::ResourceId DenoiseFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DenoiseFinished");
        static inline const RenderGraph::ResourceId CudaBloomFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.CudaBloomFinished");
//Modify Begin:2026-07-31 by BestHui
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
        std::shared_ptr<Texture> Direct;
        std::shared_ptr<Texture> Indirect;
        std::shared_ptr<Texture> HistoryColor;
        std::shared_ptr<Texture> NoisyRadiance;
        std::shared_ptr<Texture> NRDNoisyRadiance;
        std::shared_ptr<Texture> SceneColor;
    };

//Modify Begin:2026-08-05 by BestHui
    struct ReSTIRDIResources
    {
        std::shared_ptr<Texture> ReservoirA;
        std::shared_ptr<Texture> ReservoirB;
        std::shared_ptr<Texture> HistoryPositionA;
        std::shared_ptr<Texture> HistoryPositionB;
        std::shared_ptr<Texture> HistoryNormalRoughnessA;
        std::shared_ptr<Texture> HistoryNormalRoughnessB;
        std::shared_ptr<Texture> HistoryDiffuseMetallicA;
        std::shared_ptr<Texture> HistoryDiffuseMetallicB;
        std::shared_ptr<Texture> HistorySpecularOcclusionA;
        std::shared_ptr<Texture> HistorySpecularOcclusionB;
        std::shared_ptr<Texture> RISReservoir;
        std::shared_ptr<Texture> BoilingReservoir;
        std::shared_ptr<Texture> SpatialReservoir;
    };
//Modify End

    struct NRDResources
    {
        std::shared_ptr<Texture> NormalRoughness;
        std::shared_ptr<Texture> ViewZ;
        std::shared_ptr<Texture> Motion;
        std::shared_ptr<Texture> DenoisedRadiance;
    };

    FrameGBufferResources GetFrameGBufferResources(const RenderGraph::RenderContext& context);
    LightingResources GetLightingResources(const RenderGraph::RenderContext& context);
//Modify Begin:2026-08-05 by BestHui
    ReSTIRDIResources GetReSTIRDIResources(const RenderGraph::RenderContext& context);
//Modify End
    NRDResources GetNRDResources(const RenderGraph::RenderContext& context);

    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions();
    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions();
    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions();
}
