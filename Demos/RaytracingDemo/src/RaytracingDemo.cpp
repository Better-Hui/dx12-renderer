#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/GraphicsSettings.h>
#include <Framework/Mesh.h>
#include <Framework/ModelLoader.h>
#include <Framework/RasterPipelineStateBuilder.h>
#include <Framework/ShaderBlob.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    uint32_t ComputeDescriptorArrayCapacity(const size_t resourceCount, const size_t resourceCapacity)
    {
        return static_cast<uint32_t>(std::max<size_t>(
            RayTracingSceneResourceLayout::MinDescriptorArrayCapacity,
            std::max(resourceCount, resourceCapacity)));
    }

}

RaytracingDemo::RaytracingDemo(const std::wstring& name, const int width, const int height, GraphicsSettings graphicsSettings)
    : Base(name, width, height, false)
    , m_Width(width)
    , m_Height(height)
{
    (void)graphicsSettings;

    const XMVECTOR cameraPos = XMVectorSet(0, 8, -35, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 18, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    m_Camera.SetLookAt(cameraPos, cameraTarget, cameraUp);
//Modify Begin:2026-07-28 by BestHui
    const XMVECTOR initialForward = XMVector3Normalize(cameraTarget - cameraPos);
    m_CameraController.Yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(initialForward), XMVectorGetZ(initialForward)));
    m_CameraController.Pitch = XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(initialForward), -1.0f, 1.0f)));
//Modify End
    m_Camera.SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

    char* mode = nullptr;
    size_t modeLength = 0;
    _dupenv_s(&mode, &modeLength, "RAYTRACING_DEMO_MODE");
    if (mode != nullptr && std::strcmp(mode, "shader-table") == 0)
    {
        m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
    }
    std::free(mode);

    char* nrdMode = nullptr;
    size_t nrdModeLength = 0;
    _dupenv_s(&nrdMode, &nrdModeLength, "RAYTRACING_DEMO_NRD");
    if (nrdMode != nullptr)
    {
        m_Denoisers.SetAlgorithm(std::strcmp(nrdMode, "0") != 0 ? DenoiserController::Algorithm::NRD : DenoiserController::Algorithm::Off);
    }
    std::free(nrdMode);

    char* denoiserMode = nullptr;
    size_t denoiserModeLength = 0;
    _dupenv_s(&denoiserMode, &denoiserModeLength, "RAYTRACING_DEMO_DENOISER");
    if (denoiserMode != nullptr)
    {
        m_Denoisers.SetAlgorithmFromName(denoiserMode);
    }
    std::free(denoiserMode);

//Modify Begin:2026-07-27 by BestHui
    char* directLighting = nullptr;
    size_t directLightingLength = 0;
    _dupenv_s(&directLighting, &directLightingLength, "RAYTRACING_DEMO_DIRECT");
    if (directLighting != nullptr)
    {
        m_DirectLightingEnabled = std::strcmp(directLighting, "0") != 0;
    }
    std::free(directLighting);

    char* indirectLighting = nullptr;
    size_t indirectLightingLength = 0;
    _dupenv_s(&indirectLighting, &indirectLightingLength, "RAYTRACING_DEMO_INDIRECT");
    if (indirectLighting != nullptr)
    {
        m_IndirectLightingEnabled = std::strcmp(indirectLighting, "0") != 0;
    }
    std::free(indirectLighting);

    char* cudaBloom = nullptr;
    size_t cudaBloomLength = 0;
    _dupenv_s(&cudaBloom, &cudaBloomLength, "RAYTRACING_DEMO_CUDA_BLOOM");
    if (cudaBloom != nullptr)
    {
        m_CudaBloom.SetEnabled(std::strcmp(cudaBloom, "0") != 0);
    }
    std::free(cudaBloom);
//Modify End

}

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

    m_SceneResources.LoadDeferredLightingScene(*commandList);
    m_Lights.CreateDemoLights();

    m_SkyboxTexture = std::make_shared<Texture>();
    commandList->LoadTextureFromFile(*m_SkyboxTexture, L"Assets/Textures/skybox/skybox.dds", TextureUsageType::Albedo);

    m_ImGui = std::make_unique<ImGuiImpl>(*commandList, *PWindow);

    m_SkyboxMesh = Mesh::CreateCube(*commandList);
    m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);

    m_GBufferShader = std::make_shared<Shader>(
        ShaderBlob(L"GBuffer.vs.cso"),
        ShaderBlob(L"GBuffer.ps.cso"),
        [](RasterPipelineStateBuilder&) {});

    m_SkyboxShader = std::make_shared<Shader>(
        ShaderBlob(L"Skybox.vs.cso"),
        ShaderBlob(L"Skybox.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithFrontFaceCull().WithDepthTestNoWrite();
        });

    m_LightBillboardShader = std::make_shared<Shader>(
        ShaderBlob(L"LightBillboard.vs.cso"),
        ShaderBlob(L"LightBillboard.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
        });

    m_Denoisers.Initialize();
    if (IsDenoiserEnabled())
    {
        m_AccumulationEnabled = false;
    }

    m_SceneResources.AddRayTracingInstances(m_RayTracingAccelerationStructure);

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    m_SceneResources.UploadRayTracingBuffers(*commandList, m_RayTracingAccelerationStructure);
    m_Lights.InitializeGpuBuffers(*commandList);
//Modify Begin:2026-07-27 by BestHui
    EnsureRayTracingPipelines();
//Modify End
    BindRayTracingShaderResources();

//Modify Begin:2026-07-28 by BestHui
    RebuildRenderGraph();
//Modify End

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
    m_RenderGraph.reset();
    m_LightBillboardShader.reset();
    m_SkyboxShader.reset();
    m_GBufferShader.reset();
    m_LightBillboardMesh.reset();
    m_SkyboxMesh.reset();
    m_ImGui.reset();
    m_Denoisers.Shutdown();
//Modify Begin:2026-07-27 by BestHui
    m_CudaBloom.Shutdown();
//Modify End
    m_SkyboxTexture.reset();
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.Reset();
//Modify End
    m_SceneResources.Clear();
}

RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = m_RayTracingAccelerationStructure.GetMeshes();

    RayTracingSceneResourceLayout layout;
    layout.TextureDescriptorCapacity = ComputeDescriptorArrayCapacity(m_SceneResources.GetTextureCount(), m_SceneResources.GetTextureCapacity());
    layout.GeometryDescriptorCapacity = ComputeDescriptorArrayCapacity(rayTracingMeshes.size(), rayTracingMeshes.capacity());
    return layout;
}

void RaytracingDemo::EnsureRayTracingPipelines()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.EnsurePipelines(m_PathTracingBackend, layout);
    if (m_RayTracingAccelerationStructure.GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
//Modify End
}

void RaytracingDemo::BindRayTracingShaderResources()
{
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.BindRayTracingResources(
        m_RayTracingAccelerationStructure,
        m_SceneResources,
        m_Lights,
        m_SkyboxTexture);
//Modify End
}

RaytracingDemo::CameraConstants RaytracingDemo::BuildCameraConstants() const
{
    CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, m_Camera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, m_Camera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, m_Camera.GetTranslation());
    camera.Width = static_cast<uint32_t>(m_Width);
    camera.Height = static_cast<uint32_t>(m_Height);
    camera.MaxBounces = static_cast<uint32_t>(std::clamp(m_MaxBounces, 0, 5));
    camera.SamplesPerPixel = 1;
    m_Lights.FillCameraConstants(camera.DirectionalLightCount, camera.PointLightCount, camera.AreaLightCount, camera.SkyLight);
    camera.FrameIndex = m_FrameIndex;
    const bool pathAccumulationEnabled = m_AccumulationEnabled && !IsDenoiserEnabled();
    camera.AccumulationFrameIndex = pathAccumulationEnabled ? m_AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = pathAccumulationEnabled ? 1u : 0u;
    m_Denoisers.FillCameraConstants(camera.NRDDenoiserMode, camera.NRDReblurHitDistanceParameters);
    camera.DirectLightingEnabled = m_DirectLightingEnabled ? 1u : 0u;
    camera.IndirectLightingEnabled = m_IndirectLightingEnabled ? 1u : 0u;
    return camera;
}

RaytracingDemo::PipelineConstants RaytracingDemo::BuildPipelineConstants() const
{
    PipelineConstants pipeline{};
    pipeline.View = m_Camera.GetViewMatrix();
    pipeline.Projection = m_Camera.GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, m_Camera.GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
    return pipeline;
}

//Modify Begin:2026-07-28 by BestHui
void RaytracingDemo::RebuildRenderGraph()
{
    if (m_RenderGraph != nullptr)
    {
        Application::Get().Flush();
    }
    m_RenderGraph = RaytracingDemoRenderGraphBuilder::Create(*this);
    m_RenderGraphDenoiserEnabled = IsDenoiserEnabled();
}

void RaytracingDemo::EnsureRenderGraphTopology()
{
    if (m_RenderGraph == nullptr || m_RenderGraphDenoiserEnabled != IsDenoiserEnabled())
    {
        RebuildRenderGraph();
        ResetAccumulation();
    }
}
//Modify End

void RaytracingDemo::ResetAccumulation(bool resetDenoiserHistory)
{
    m_AccumulationFrameIndex = 0;
    if (resetDenoiserHistory)
    {
        m_Denoisers.ResetHistory();
    }
}

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = static_cast<uint32_t>(m_Width);
    metadata.m_ScreenHeight = static_cast<uint32_t>(m_Height);
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();
    m_Lights.Upload(*commandList);
    commandQueue->ExecuteCommandList(commandList);

//Modify Begin:2026-07-28 by BestHui
    EnsureRenderGraphTopology();
//Modify End
    m_RenderGraph->Execute(metadata);
    PresentWithExternalPostProcess(metadata);

    ++m_FrameIndex;
    if (m_AccumulationEnabled && !IsDenoiserEnabled())
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }

    m_PreviousViewProjection = m_Camera.GetViewMatrix() * m_Camera.GetProjectionMatrix();
    m_HasPreviousViewProjection = true;
}
