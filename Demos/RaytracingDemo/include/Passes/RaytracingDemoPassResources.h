//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <Denoising/DenoiserController.h>
#include <PathTracing/PathTracingPipelineController.h>
#include <Scene/SceneResources.h>
#include <Scene/SceneLighting.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Lighting/ReSTIRDI.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>

#include <DX12Library/Camera.h>

#include <DirectXMath.h>
//Modify Begin:2026-07-30 by BestHui
#include <wrl.h>
//Modify End

#include <cstdint>
#include <memory>

class CommandQueue;
class CudaBloomPass;
class IndirectDrawCommandSignature;

namespace RenderGraph
{
    struct RenderContext;
}

struct RaytracingDemoCameraConstants
{
    DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t MaxBounces = 5;
    uint32_t SamplesPerPixel = 1;
    uint32_t DirectionalLightCount = 0;
    uint32_t PointLightCount = 0;
    uint32_t AreaLightCount = 0;
    uint32_t FrameIndex = 0;
    uint32_t AccumulationFrameIndex = 0;
    uint32_t AccumulationEnabled = 1;
    uint32_t NRDDenoiserMode = 0;
    uint32_t DenoiserEnabled = 0;
    DirectX::XMFLOAT4 NRDReblurHitDistanceParameters = { 3.0f, 0.1f, 20.0f, 0.0f };
//Modify Begin:2026-08-06 by BestHui
    uint32_t DirectLightingActive = 1;
    uint32_t IndirectLightingActive = 1;
//Modify End
//Modify Begin:2026-08-05 by BestHui
    uint32_t ReSTIRDIHistoryValid = 0;
//Modify End
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
//Modify Begin:2026-08-05 by BestHui
    ReSTIRDI& DirectLightingReSTIRDI;
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
    std::shared_ptr<Texture> SkyboxTexture;
    std::shared_ptr<Mesh> DisplayBlitMesh;
    Camera& SceneCamera;
//Modify Begin:2026-07-30 by BestHui
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> AsyncComputeQueue;
//Modify End
};

//Modify Begin:2026-08-05 by BestHui
enum class RaytracingDemoLightingTechnique : uint32_t
{
    None = 0,
    PathTracing = 1,
    ReSTIRDI = 2,
};
//Modify End

struct RaytracingDemoPassConfig
{
    const PathTracingBackend* Backend = nullptr;
//Modify Begin:2026-08-05 by BestHui
    const RaytracingDemoLightingTechnique* DirectLightingTechnique = nullptr;
    const RaytracingDemoLightingTechnique* IndirectLightingTechnique = nullptr;
//Modify End
    const bool* AsyncComputeEnabled = nullptr;
    const bool* UseMeshletGBuffer = nullptr;
    const bool* UseTaskShaderMeshlets = nullptr;
    const bool* DebugMeshletClusters = nullptr;
    const bool* SkyboxEnabled = nullptr;
    const int* DebugLightingTextureTarget = nullptr;
    const int* DebugTextureTarget = nullptr;
    const int* MaxBounces = nullptr;
    const int* Width = nullptr;
    const int* Height = nullptr;
    const bool* AccumulationEnabled = nullptr;
    const uint32_t* FrameIndex = nullptr;
    const uint32_t* AccumulationFrameIndex = nullptr;
//Modify Begin:2026-08-05 by BestHui
    const bool* ReSTIRDIHistoryValid = nullptr;
//Modify End
    const bool* HasPreviousViewProjection = nullptr;
    const DirectX::XMMATRIX* PreviousViewProjection = nullptr;
};

RaytracingDemoCameraConstants BuildPassCameraConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::RenderContext& context);

RaytracingDemoPipelineConstants BuildPassPipelineConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config);
//Modify End
