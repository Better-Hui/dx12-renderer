//Modify Begin:2026-07-30 by Hui
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
//Modify Begin:2026-08-10 by Hui
#include <Framework/Rendering/Lighting/ReSTIRGI.h>
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>
//Modify End
#include <Framework/Rendering/Pipeline/MeshShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
//Modify Begin:2026-08-07 by Hui
#include <Framework/Rendering/Upscaling/DLSS.h>
//Modify End

#include <DX12Library/Camera.h>

#include <Passes/CudaBloomPass.h>

#include <DirectXMath.h>
//Modify Begin:2026-07-30 by Hui
#include <wrl.h>
//Modify End

#include <cstdint>
#include <memory>
#include <optional>

class CommandQueue;
//Modify Begin:2026-08-11 by Hui
class GpuTimestampProfiler;
//Modify End
class IndirectDrawCommandSignature;

namespace RenderGraph
{
    class FrameContext;
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
//Modify Begin:2026-08-13 by Hui
    uint32_t PaddingBeforeNrdParameters0 = 0;
    uint32_t PaddingBeforeNrdParameters1 = 0;
//Modify End
    DirectX::XMFLOAT4 NRDReblurHitDistanceParameters = { 3.0f, 0.1f, 20.0f, 0.0f };
//Modify Begin:2026-08-05 by Hui
    uint32_t ReSTIRDIHistoryValid = 0;
//Modify End
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
//Modify Begin:2026-08-05 by Hui
    ReSTIRDI& DirectLightingReSTIRDI;
//Modify Begin:2026-07-30 by Hui
    ReSTIRDIPass& DirectLightingReSTIRDIPass;
//Modify End
//Modify Begin:2026-08-10 by Hui
    ReSTIRGI& IndirectLightingReSTIRGI;
    ReSTIRGIPass& IndirectLightingReSTIRGIPass;
//Modify End
//Modify End
//Modify Begin:2026-08-07 by Hui
    DLSS& Dlss;
    std::shared_ptr<ComputeShader> DLSSRayReconstructionPrepareShader;
//Modify End
    DenoiserController& Denoisers;
    CudaBloomPass& CudaBloom;
    std::shared_ptr<Shader> GBufferShader;
    std::shared_ptr<Shader> GBufferMeshletIndirectShader;
    std::shared_ptr<MeshShader> GBufferTaskMeshShader;
    std::shared_ptr<ComputeShader> MeshletCullShader;
    IndirectDrawCommandSignature* MeshletDrawCommandSignature = nullptr;
    std::shared_ptr<Shader> DisplayCompositeShader;
    std::shared_ptr<ComputeShader> SkyboxComputeShader;
    std::shared_ptr<ComputeShader> SkyboxEquirectangularComputeShader;
//Modify Begin:2026-08-06 by Hui
    std::shared_ptr<ComputeShader> SkyboxCubemapStripComputeShader;
//Modify End
    std::shared_ptr<Texture> SkyboxTexture;
    std::shared_ptr<Mesh> DisplayBlitMesh;
    Camera& SceneCamera;
//Modify Begin:2026-07-30 by Hui
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
//Modify Begin:2026-08-07 by Hui
    std::shared_ptr<D3D12DeviceContext> DeviceContext;
//Modify End
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> AsyncComputeQueue;
    std::shared_ptr<CommandQueue> CopyQueue;
//Modify End
//Modify Begin:2026-08-11 by Hui
    GpuTimestampProfiler* DirectGpuTimestampProfiler = nullptr;
//Modify End
};

using RaytracingDemoPassResourcesSnapshot = std::optional<RaytracingDemoPassResources>;

//Modify Begin:2026-08-05 by Hui
enum class RaytracingDemoLightingTechnique : uint32_t
{
    None = 0,
    PathTracing = 1,
    ReSTIRDI = 2,
//Modify Begin:2026-08-10 by Hui
    ReSTIRGI = 3,
//Modify End
};
//Modify End

struct RaytracingDemoFrameState
{
    PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
//Modify Begin:2026-07-30 by Hui
    MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
//Modify End
    RaytracingDemoLightingTechnique DirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    RaytracingDemoLightingTechnique IndirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    bool AsyncComputeEnabled = false;
    bool UseMeshletGBuffer = false;
    bool UseTaskShaderMeshlets = false;
    bool DebugMeshletClusters = false;
    bool SkyboxEnabled = false;
//Modify Begin:2026-08-07 by Hui
    bool DLSSEnabled = false;
    bool RayReconstructionEnabled = false;
    bool FrameGenerationEnabled = false;
    DLSSMode DlssMode = DLSSMode::Disabled;
//Modify End
    int DebugLightingTextureTarget = 0;
    int DebugTextureTarget = 0;
    int MaxBounces = 1;
//Modify Begin:2026-08-13 by Hui
    bool DenoiserEnabled = false;
    DenoiserController::Algorithm DenoiserAlgorithm = DenoiserController::Algorithm::Off;
//Modify Begin:2026-08-18 by Hui
    bool BloomEnabled = false;
    CudaBloomPass::Backend BloomBackend = CudaBloomPass::Backend::Cuda;
//Modify End
//Modify End
    uint32_t Width = 1;
    uint32_t Height = 1;
//Modify Begin:2026-08-07 by Hui
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;
    float DLSSSharpness = 0.0f;
    DirectX::XMFLOAT2 DLSSJitterOffset = { 0.0f, 0.0f };
    DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
//Modify End
    bool AccumulationEnabled = false;
    uint32_t FrameIndex = 0;
    uint32_t AccumulationFrameIndex = 0;
    bool ReSTIRDIHistoryValid = false;
//Modify Begin:2026-08-10 by Hui
    bool ReSTIRGIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-11 by Hui
    bool ReSTIRGIStageTimingEnabled = false;
//Modify End
    bool HasPreviousViewProjection = false;
    DirectX::XMMATRIX PreviousViewProjection = DirectX::XMMatrixIdentity();

//Modify Begin:2026-08-13 by Hui
    bool UsesDirectLighting() const
    {
        return DirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing ||
            (DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI &&
                Backend == PathTracingBackend::InlineRayQuery);
    }

    bool UsesIndirectLighting() const
    {
        return MaxBounces > 1 &&
            (IndirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing ||
                (IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
                    Backend == PathTracingBackend::InlineRayQuery));
    }

    PathTracingCompositeFeatures GetCompositeFeatures() const
    {
        return {
            .DirectLightingEnabled = UsesDirectLighting(),
            .IndirectLightingEnabled = UsesIndirectLighting(),
            .AccumulationEnabled = AccumulationEnabled,
            .DenoiserMode = DenoiserEnabled ? static_cast<uint32_t>(DenoiserAlgorithm) : 0u,
            .UseNrdReblur = false,
        };
    }
//Modify End
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
