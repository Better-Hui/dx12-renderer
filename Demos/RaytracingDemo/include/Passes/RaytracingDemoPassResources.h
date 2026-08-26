//Modify Begin:2026-08-25 by Hui
#pragma once

#include <Denoising/DenoiserController.h>
#include <PathTracing/PathTracingPipelineController.h>
#include <Scene/SceneResources.h>
#include <Scene/SceneLighting.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/Lighting/ReSTIRGI.h>
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <Framework/Rendering/PostProcess/AutoExposure.h>

#include <DX12Library/Camera.h>

#include <Passes/BloomController.h>

#include <DirectXMath.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <optional>

class CommandQueue;
class GpuTimestampProfiler;
class IndirectCommandSignature;
class ActivePixelListController;

namespace RenderGraph
{
    class FrameContext;
}

namespace FrameworkDiagnostics
{
    class DiagnosticsSession;
}

struct RaytracingDemoCameraConstants
{
    DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t SamplesPerPixel = 1;
    uint32_t DirectionalLightCount = 0;
    uint32_t PointLightCount = 0;
    uint32_t SurfaceEmitterCount = 0;
    uint32_t FrameIndex = 0;
    uint32_t AccumulationFrameIndex = 0;
    uint32_t AccumulationEnabled = 1;
    uint32_t NRDDenoiserMode = 0;
    uint32_t PaddingBeforeNrdParameters0 = 0;
    uint32_t PaddingBeforeNrdParameters1 = 0;
    DirectX::XMFLOAT4 NRDReblurHitDistanceParameters = { 3.0f, 0.1f, 20.0f, 0.0f };
    uint32_t ReSTIRDIHistoryValid = 0;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
    uint32_t Padding2 = 0;
    SkyLightData SkyLight = {};
};

struct RaytracingDemoPipelineConstants
{
    DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT2 ScreenResolution = { 1.0f, 1.0f };
    DirectX::XMFLOAT2 ScreenTexelSize = { 1.0f, 1.0f };
    DirectX::XMMATRIX PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t DebugMeshletClusters = 0;
    uint32_t PipelinePadding0 = 0;
    uint32_t PipelinePadding1 = 0;
    uint32_t PipelinePadding2 = 0;
};

struct RaytracingDemoModelConstants
{
    DirectX::XMMATRIX Model = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ModelViewProjection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX InverseTransposeModel = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX PreviousModelViewProjection = DirectX::XMMatrixIdentity();
};

struct RaytracingDemoGBufferMaterialConstants
{
    DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    DirectX::XMFLOAT4 Emission = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    uint32_t DiffuseTextureIndex = 0;
    uint32_t NormalTextureIndex = 0;
    uint32_t MetallicTextureIndex = 0;
    uint32_t RoughnessTextureIndex = 0;
    uint32_t AmbientOcclusionTextureIndex = 0;
    uint32_t EmissionTextureIndex = 0;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    uint32_t HasDiffuseMap = 0;
    uint32_t HasNormalMap = 0;
    uint32_t HasMetallicMap = 0;
    uint32_t HasRoughnessMap = 0;
    uint32_t HasAmbientOcclusionMap = 0;
    uint32_t HasEmissionMap = 0;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
};

struct RaytracingDemoGBufferDebugConstants
{
    uint32_t DebugMeshletClusters = 0;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
    uint32_t Padding2 = 0;
};

struct RaytracingDemoPassResources
{
    RaytracingDemoSceneResources& Scene;
    SceneLightManager& Lights;
    PathTracingPipelineController& Pipelines;
    ActivePixelListController& ActivePixels;
    ReSTIRDI& DirectLightingReSTIRDI;
    ReSTIRDIPass& DirectLightingReSTIRDIPass;
    ReSTIRGI& IndirectLightingReSTIRGI;
    ReSTIRGIPass& IndirectLightingReSTIRGIPass;
    DLSS& Dlss;
    std::shared_ptr<ComputeShader> DLSSRayReconstructionPrepareShader;
    std::shared_ptr<ComputeShader> CopyQueueValidationShader;
    DenoiserController& Denoisers;
    BloomController& Bloom;
    AutoExposure& Exposure;
    std::shared_ptr<Shader> GBufferShader;
    std::shared_ptr<Shader> GBufferMeshletIndirectShader;
    std::shared_ptr<MeshShader> GBufferTaskMeshShader;
    std::shared_ptr<ComputeShader> MeshletCullShader;
    IndirectCommandSignature* MeshletDrawCommandSignature = nullptr;
    std::shared_ptr<Shader> DisplayCompositeShader;
    std::shared_ptr<ComputeShader> SkyboxComputeShader;
    std::shared_ptr<ComputeShader> SkyboxEquirectangularComputeShader;
    std::shared_ptr<ComputeShader> SkyboxCubemapStripComputeShader;
    std::shared_ptr<Texture> SkyboxTexture;
    std::shared_ptr<Mesh> DisplayBlitMesh;
    Camera& SceneCamera;
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<D3D12DeviceContext> DeviceContext;
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> AsyncComputeQueue;
    std::shared_ptr<CommandQueue> CopyQueue;
    GpuTimestampProfiler* DirectGpuTimestampProfiler = nullptr;
    FrameworkDiagnostics::DiagnosticsSession* Diagnostics = nullptr;
};

using RaytracingDemoPassResourcesSnapshot = std::optional<RaytracingDemoPassResources>;

enum class RaytracingDemoLightingTechnique : uint32_t
{
    None = 0,
    PathTracing = 1,
    ReSTIRDI = 2,
    ReSTIRGI = 3,
};

struct RaytracingDemoFrameState
{
    PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
    PathTracingDispatchMode DispatchMode = PathTracingDispatchMode::FullResolution;
    MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
    RaytracingDemoLightingTechnique DirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    RaytracingDemoLightingTechnique IndirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    bool AsyncComputeEnabled = false;
    bool CopyQueueValidationEnabled = false;
    bool DynamicRayTracingUpdateEnabled = false;
    bool UseMeshletGBuffer = false;
    bool UseTaskShaderMeshlets = false;
    bool DebugMeshletClusters = false;
    bool SkyboxEnabled = false;
    bool DLSSEnabled = false;
    bool RayReconstructionEnabled = false;
    bool FrameGenerationEnabled = false;
    DLSSMode DlssMode = DLSSMode::Disabled;
    int DebugLightingTextureTarget = 0;
    int DebugTextureTarget = 0;
    int MaxBounces = 1;
    bool DenoiserEnabled = false;
    DenoiserController::Algorithm DenoiserAlgorithm = DenoiserController::Algorithm::Off;
    NRD::DenoiserMode NRDDenoiserMode = NRD::DenoiserMode::ReblurDiffuse;
    uint32_t SVGFAtrousIterations = 1;
    bool BloomEnabled = false;
    BloomController::Backend BloomBackend = BloomController::Backend::Cuda;
    int BloomPyramidLevels = 1;
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;
    float DLSSSharpness = 0.0f;
    DirectX::XMFLOAT2 DLSSJitterOffset = { 0.0f, 0.0f };
    DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
    bool AccumulationEnabled = false;
    uint32_t FrameIndex = 0;
    uint32_t AccumulationFrameIndex = 0;
    bool ReSTIRDIHistoryValid = false;
    bool ReSTIRGIHistoryValid = false;
    bool ReSTIRGIStageTimingEnabled = false;
    bool HasPreviousViewProjection = false;
    DirectX::XMMATRIX PreviousViewProjection = DirectX::XMMatrixIdentity();

    static constexpr bool SupportsDirectLighting(
        const PathTracingBackend backend,
        const RaytracingDemoLightingTechnique technique) noexcept
    {
        switch (technique)
        {
        case RaytracingDemoLightingTechnique::None:
        case RaytracingDemoLightingTechnique::PathTracing:
            return true;
        case RaytracingDemoLightingTechnique::ReSTIRDI:
            return backend == PathTracingBackend::InlineRayQuery;
        case RaytracingDemoLightingTechnique::ReSTIRGI:
        default:
            return false;
        }
    }

    static constexpr bool SupportsIndirectLighting(
        const PathTracingBackend backend,
        const RaytracingDemoLightingTechnique technique) noexcept
    {
        switch (technique)
        {
        case RaytracingDemoLightingTechnique::None:
        case RaytracingDemoLightingTechnique::PathTracing:
            return true;
        case RaytracingDemoLightingTechnique::ReSTIRGI:
            return backend == PathTracingBackend::InlineRayQuery;
        case RaytracingDemoLightingTechnique::ReSTIRDI:
        default:
            return false;
        }
    }

    static constexpr bool SupportsAsyncCompute(const PathTracingBackend backend) noexcept
    {
        return backend == PathTracingBackend::InlineRayQuery;
    }

    bool UsesDirectLighting() const noexcept
    {
        return DirectLightingTechnique != RaytracingDemoLightingTechnique::None &&
            SupportsDirectLighting(Backend, DirectLightingTechnique);
    }

    bool UsesIndirectLighting() const noexcept
    {
        return MaxBounces > 1 &&
            IndirectLightingTechnique != RaytracingDemoLightingTechnique::None &&
            SupportsIndirectLighting(Backend, IndirectLightingTechnique);
    }

    bool UsesCompactedRayTracedPixelDispatch() const
    {
        return DispatchMode == PathTracingDispatchMode::CompactedIndirect &&
            (UsesDirectLighting() || UsesIndirectLighting());
    }

    PathTracingCompositeFeatures GetCompositeFeatures() const
    {
        // OIDN reads the converged accumulation after this shader, so this
        // shader only needs NRD/SVGF permutations that emit noisy radiance.
        const bool writesInlineDenoiserInput = DenoiserEnabled &&
            (DenoiserAlgorithm == DenoiserController::Algorithm::NRD ||
             DenoiserAlgorithm == DenoiserController::Algorithm::SVGF);
        return {
            .DirectLightingEnabled = UsesDirectLighting(),
            .IndirectLightingEnabled = UsesIndirectLighting(),
            .AccumulationEnabled = AccumulationEnabled,
            .DenoiserMode = writesInlineDenoiserInput ? static_cast<uint32_t>(DenoiserAlgorithm) : 0u,
            .UseNrdReblur = false,
        };
    }
};

struct RaytracingDemoPassConfig
{
    std::shared_ptr<RaytracingDemoFrameState> FrameState;
};

RaytracingDemoCameraConstants BuildPassCameraConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::FrameContext& context);

RaytracingDemoPipelineConstants BuildPassPipelineConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config);
//Modify End
