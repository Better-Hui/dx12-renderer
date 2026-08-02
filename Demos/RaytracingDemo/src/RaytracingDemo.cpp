#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/Core/GraphicsSettings.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Unity/UnitySceneImporter.h>
//Modify End

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>
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

    std::filesystem::path GetUnityScenePath()
    {
        char* scenePath = nullptr;
        size_t scenePathLength = 0;
        _dupenv_s(&scenePath, &scenePathLength, "RAYTRACING_DEMO_UNITY_SCENE");
        if (scenePath != nullptr)
        {
            std::filesystem::path result(scenePath);
            std::free(scenePath);
            scenePath = nullptr;
            if (!result.empty() && std::filesystem::exists(result))
            {
                return result;
            }
            throw std::runtime_error("RAYTRACING_DEMO_UNITY_SCENE is set but the scene file does not exist.");
        }
        std::free(scenePath);

        const std::filesystem::path defaultScene =
            "C:/Program Files/Unity/MDR/ModernDeferredRenderer/project/ModernDeferredRenderer/Assets/Scenes/CornellBox.unity";
        if (std::filesystem::exists(defaultScene))
        {
            return defaultScene;
        }

        throw std::runtime_error("Default Unity scene file does not exist.");
    }

    XMFLOAT3 RotateCameraVector(const XMVECTOR rotation, const XMFLOAT3& value)
    {
        const XMVECTOR vector = XMVectorSet(value.x, value.y, value.z, 0.0f);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Rotate(vector, rotation));
        return result;
    }

    void ApplySceneCamera(Camera& camera, const SceneCamera& sceneCamera, const int width, const int height)
    {
        if (sceneCamera.RuntimeCamera != nullptr)
        {
            camera.SetTranslation(sceneCamera.RuntimeCamera->GetTranslation());
            camera.SetRotation(sceneCamera.RuntimeCamera->GetRotation());
        }
        camera.SetProjection(sceneCamera.FieldOfView, static_cast<float>(width) / static_cast<float>(height), sceneCamera.NearClipPlane, sceneCamera.FarClipPlane);
    }

//Modify Begin:2026-07-29 by BestHui
    void CalculateCameraControllerFromLookDirection(
        const XMVECTOR lookDirection,
        float& yaw,
        float& pitch)
    {
        const XMVECTOR forward = XMVector3Normalize(lookDirection);
        yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(forward), XMVectorGetZ(forward)));
        pitch = -XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(forward), -1.0f, 1.0f)));
    }
//Modify End

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
    GetSceneCamera().SetLookAt(cameraPos, cameraTarget, cameraUp);
//Modify Begin:2026-07-28 by BestHui
    const XMVECTOR initialForward = XMVector3Normalize(cameraTarget - cameraPos);
    m_CameraController.Yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(initialForward), XMVectorGetZ(initialForward)));
    m_CameraController.Pitch = XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(initialForward), -1.0f, 1.0f)));
//Modify End
    GetSceneCamera().SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

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

//Modify Begin:2026-07-30 by BestHui
    char* meshletGBuffer = nullptr;
    size_t meshletGBufferLength = 0;
    _dupenv_s(&meshletGBuffer, &meshletGBufferLength, "RAYTRACING_DEMO_MESHLET_GBUFFER");
    if (meshletGBuffer != nullptr)
    {
        m_UseMeshletGBuffer = std::strcmp(meshletGBuffer, "0") != 0;
    }
    std::free(meshletGBuffer);

    char* meshletDebug = nullptr;
    size_t meshletDebugLength = 0;
    _dupenv_s(&meshletDebug, &meshletDebugLength, "RAYTRACING_DEMO_MESHLET_DEBUG");
    if (meshletDebug != nullptr)
    {
        m_DebugMeshletClusters = std::strcmp(meshletDebug, "0") != 0;
        if (m_DebugMeshletClusters)
        {
            m_UseMeshletGBuffer = true;
        }
    }
    std::free(meshletDebug);

//Modify Begin:2026-07-31 by BestHui
    char* meshletBackend = nullptr;
    size_t meshletBackendLength = 0;
    _dupenv_s(&meshletBackend, &meshletBackendLength, "RAYTRACING_DEMO_MESHLET_BACKEND");
    if (meshletBackend != nullptr)
    {
        m_UseTaskShaderMeshlets = std::strcmp(meshletBackend, "indirect") != 0;
    }
    std::free(meshletBackend);
//Modify End
//Modify End

}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::LoadUnitySceneContent(CommandList& commandList, const std::filesystem::path& unityScenePath)
{
    const UnitySceneImportResult unityImport = UnitySceneImporter::ImportFromFile(unityScenePath);
    m_Scene = unityImport.SceneData;
    const SceneCamera& sceneCamera = m_Scene.GetCamera();

    if (!m_SceneResources.LoadScene(commandList, m_Scene))
    {
        throw std::runtime_error("Scene has no supported renderable objects.");
    }

    SceneSkybox sceneSkybox = m_Scene.GetSkybox();
    std::filesystem::path skyboxTexturePath = sceneSkybox.Texture.AssetPath;
    if (skyboxTexturePath.empty())
    {
        skyboxTexturePath = L"Assets/Textures/skybox/skybox.dds";
        sceneSkybox.Texture.AssetPath = skyboxTexturePath;
    }
    if (sceneSkybox.AmbientColorAndIntensity.x <= 0.0f &&
        sceneSkybox.AmbientColorAndIntensity.y <= 0.0f &&
        sceneSkybox.AmbientColorAndIntensity.z <= 0.0f)
    {
        sceneSkybox.AmbientColorAndIntensity = { 1.0f, 1.0f, 1.0f, std::max(0.001f, sceneSkybox.AmbientColorAndIntensity.w) };
    }
    m_Scene.SetSkybox(sceneSkybox);

    m_Lights.CreateFromScene(m_Scene);
    m_SkyboxEnabled = true;
    m_HasSceneCamera = sceneCamera.RuntimeCamera != nullptr;

    ApplySceneCamera(GetSceneCamera(), sceneCamera, m_Width, m_Height);
    const XMFLOAT3 forward = RotateCameraVector(GetSceneCamera().GetRotation(), { 0.0f, 0.0f, 1.0f });
    CalculateCameraControllerFromLookDirection(
        XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
        m_CameraController.Yaw,
        m_CameraController.Pitch);
    m_CameraFov = sceneCamera.FieldOfView;

    m_SkyboxTexture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*m_SkyboxTexture, skyboxTexturePath.wstring(), TextureUsageType::Albedo);
}
//Modify End

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

//Modify Begin:2026-07-30 by BestHui
    char* useUnityScene = nullptr;
    size_t useUnitySceneLength = 0;
    _dupenv_s(&useUnityScene, &useUnitySceneLength, "RAYTRACING_DEMO_USE_UNITY_SCENE");
    const bool shouldUseUnityScene = useUnityScene != nullptr && std::strcmp(useUnityScene, "0") != 0;
    std::free(useUnityScene);

    if (shouldUseUnityScene)
    {
        const std::filesystem::path unityScenePath = GetUnityScenePath();
        LoadUnitySceneContent(*commandList, unityScenePath);
    }
    else
    {
        m_SceneResources.LoadDeferredLightingScene(*commandList);
        m_Lights.CreateDemoLights();
        m_SkyboxEnabled = true;
        m_HasSceneCamera = false;
        m_SkyboxTexture = std::make_shared<Texture>();
        commandList->LoadTextureFromFile(*m_SkyboxTexture, L"Assets/Textures/skybox/skybox.dds", TextureUsageType::Albedo);
    }
//Modify End

    m_ImGui = std::make_unique<ImGuiImpl>(*commandList, *PWindow);

    m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);
//Modify Begin:2026-07-28 by BestHui
    m_DisplayBlitMesh = Mesh::CreateBlitTriangle(*commandList);
//Modify End

//Modify Begin:2026-07-30 by BestHui
    const auto withShaderCreateContext = [](const char* name, const auto& createShader)
    {
        try
        {
            createShader();
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(std::string("Failed to create shader pipeline '") + name + "': " + exception.what());
        }
    };

    withShaderCreateContext("GBuffer", [&]()
    {
//Modify End
    m_GBufferShader = std::make_shared<Shader>(
        ShaderBlob(L"GBuffer.vs.cso"),
        ShaderBlob(L"GBuffer.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
//Modify Begin:2026-07-30 by BestHui
    });
//Modify End

//Modify Begin:2026-07-31 by BestHui
    withShaderCreateContext("GBufferTaskMeshShader", [&]()
    {
    m_GBufferTaskMeshShader = std::make_shared<MeshShader>(
        ShaderBlob(L"GBuffer.task.as.cso"),
        ShaderBlob(L"GBuffer.task.ms.cso"),
        ShaderBlob(L"GBuffer.meshletindirect.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });
//Modify End

    withShaderCreateContext("GBufferMeshletIndirect", [&]()
    {
    PipelineLayoutReflectionOptions meshletIndirectLayoutOptions;
    meshletIndirectLayoutOptions.MaxDescriptorCount = 4096u;
    meshletIndirectLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    meshletIndirectLayoutOptions.RootConstantBufferNames.push_back("MeshletDrawCBuffer");
    m_GBufferMeshletIndirectShader = std::make_shared<Shader>(
        ShaderBlob(L"GBuffer.meshletindirect.vs.cso"),
        ShaderBlob(L"GBuffer.meshletindirect.ps.cso"),
        meshletIndirectLayoutOptions,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });

    withShaderCreateContext("MeshletCull", [&]()
    {
    const ShaderBlob meshletCullShaderBlob(L"MeshletCull.cs.cso");
    m_MeshletCullShader = std::make_shared<ComputeShader>(
        meshletCullShaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(meshletCullShaderBlob).Build());
    m_MeshletDrawCommandSignature = m_GBufferMeshletIndirectShader->CreateIndirectDrawCommandSignature(
        "MeshletDrawCBuffer",
        sizeof(MeshletIndirectCommand));
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("DisplayComposite", [&]()
    {
    m_DisplayCompositeShader = std::make_shared<Shader>(
        ShaderBlob(L"DisplayComposite.vs.cso"),
        ShaderBlob(L"DisplayComposite.ps.cso"),
        [](RasterPipelineStateBuilder&) {});
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("SkyboxCompute", [&]()
    {
    const ShaderBlob skyboxComputeShader(L"Skybox.cs.cso");
    m_SkyboxComputeShader = std::make_shared<ComputeShader>(
        skyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(skyboxComputeShader).Build());
    });
//Modify End

    withShaderCreateContext("LightBillboard", [&]()
    {
    m_LightBillboardShader = std::make_shared<Shader>(
        ShaderBlob(L"LightBillboard.vs.cso"),
        ShaderBlob(L"LightBillboard.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
        });
    });

    m_Denoisers.Initialize();
    if (IsDenoiserEnabled())
    {
        m_AccumulationEnabled = false;
    }

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_SceneResources.BuildRayTracingAccelerationStructure(*commandList, accelerationStructureSettings);
    m_Lights.InitializeGpuBuffers(*commandList);
//Modify Begin:2026-07-27 by BestHui
    EnsureRayTracingPipelines();
//Modify End
    BindRayTracingShaderResources();

//Modify Begin:2026-07-28 by BestHui
//Modify Begin:2026-07-29 by BestHui
    m_GpuTimestampProfiler.Initialize(128);
//Modify End
    RebuildRenderGraph();
//Modify End

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
//Modify Begin:2026-07-28 by BestHui
    m_CudaBloom.ReleaseInteropResource();
//Modify End
    m_RenderGraph.reset();
    m_LightBillboardShader.reset();
//Modify Begin:2026-07-28 by BestHui
    m_SkyboxComputeShader.reset();
    m_DisplayCompositeShader.reset();
//Modify End
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-07-31 by BestHui
    m_GBufferTaskMeshShader.reset();
//Modify End
    m_GBufferMeshletIndirectShader.reset();
    m_MeshletCullShader.reset();
    m_MeshletDrawCommandSignature.reset();
//Modify End
    m_GBufferShader.reset();
    m_LightBillboardMesh.reset();
//Modify Begin:2026-07-28 by BestHui
    m_DisplayBlitMesh.reset();
//Modify End
    m_ImGui.reset();
    m_Denoisers.Shutdown();
//Modify Begin:2026-07-27 by BestHui
    m_CudaBloom.Shutdown();
//Modify End
    m_SkyboxTexture.reset();
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.Reset();
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_GpuTimestampProfiler.Shutdown();
//Modify End
    m_SceneResources.Clear();
}

RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const RayTracingAccelerationStructure& accelerationStructure = m_SceneResources.GetRayTracingAccelerationStructure();
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = accelerationStructure.GetMeshes();

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
    if (m_SceneResources.GetRayTracingAccelerationStructure().GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
//Modify End
}

void RaytracingDemo::BindRayTracingShaderResources()
{
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.BindRayTracingResources(
        m_SceneResources.GetRayTracingAccelerationStructure(),
        m_SceneResources,
        m_Lights,
        m_SkyboxTexture);
//Modify End
}

RaytracingDemo::CameraConstants RaytracingDemo::BuildCameraConstants() const
{
    CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, GetSceneCamera().GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, GetSceneCamera().GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, GetSceneCamera().GetTranslation());
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
//Modify Begin:2026-08-02 by BestHui
    camera.DenoiserEnabled = static_cast<uint32_t>(m_Denoisers.GetAlgorithm());
//Modify End
    camera.DirectLightingEnabled = m_DirectLightingEnabled ? 1u : 0u;
    camera.IndirectLightingEnabled = m_IndirectLightingEnabled ? 1u : 0u;
    return camera;
}

RaytracingDemo::PipelineConstants RaytracingDemo::BuildPipelineConstants() const
{
    PipelineConstants pipeline{};
    pipeline.View = GetSceneCamera().GetViewMatrix();
    pipeline.Projection = GetSceneCamera().GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, GetSceneCamera().GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
//Modify Begin:2026-07-30 by BestHui
    pipeline.PreviousViewProjection = m_HasPreviousViewProjection ? m_PreviousViewProjection : pipeline.ViewProjection;
    pipeline.DebugMeshletClusters = m_DebugMeshletClusters ? 1u : 0u;
//Modify End
    return pipeline;
}

//Modify Begin:2026-07-28 by BestHui
void RaytracingDemo::RebuildRenderGraph()
{
    if (m_RenderGraph != nullptr)
    {
        Application::Get().Flush();
//Modify Begin:2026-07-28 by BestHui
        m_CudaBloom.ReleaseInteropResource();
//Modify End
    }
    m_RenderGraph = RaytracingDemoRenderGraphBuilder::Create(*this);
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify End
    m_RenderGraphDenoiserEnabled = IsDenoiserEnabled();
    m_RenderGraphCudaBloomEnabled = m_CudaBloom.IsEnabled();
//Modify Begin:2026-07-31 by BestHui
    m_RenderGraphMeshletGBufferEnabled = m_UseMeshletGBuffer;
    m_RenderGraphTaskMeshletEnabled = m_UseTaskShaderMeshlets;
    m_RenderGraphMeshletDebugEnabled = m_UseMeshletGBuffer && m_DebugMeshletClusters;
    m_RenderGraphDebugTextureTarget = m_DebugTextureTarget;
//Modify End
}

void RaytracingDemo::EnsureRenderGraphTopology()
{
    if (m_RenderGraph == nullptr ||
        m_RenderGraphDenoiserEnabled != IsDenoiserEnabled() ||
        m_RenderGraphCudaBloomEnabled != m_CudaBloom.IsEnabled()
//Modify Begin:2026-07-31 by BestHui
        || m_RenderGraphMeshletGBufferEnabled != m_UseMeshletGBuffer
        || m_RenderGraphTaskMeshletEnabled != m_UseTaskShaderMeshlets
        || m_RenderGraphMeshletDebugEnabled != (m_UseMeshletGBuffer && m_DebugMeshletClusters)
        || m_RenderGraphDebugTextureTarget != m_DebugTextureTarget
//Modify End
        )
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

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::SaveCurrentCameraToUnityScene()
{
    if (!m_HasSceneCamera || m_Scene.GetSourcePath().empty())
    {
        throw std::runtime_error("No source scene camera is loaded.");
    }
    if (m_Scene.GetCamera().SourceBinding.ParentTransformId != 0)
    {
        throw std::runtime_error("Saving camera with a parent transform is not supported yet.");
    }

    m_Scene.UpdateCamera(GetSceneCamera(), m_CameraFov);
    UnitySceneImporter::WriteCameraToSourceFile(m_Scene.GetSourcePath(), m_Scene.GetCamera());
    m_CameraSaveStatus = "Camera saved to Unity scene.";
}
//Modify End

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

//Modify Begin:2026-07-29 by BestHui
    const auto commandQueue = Application::Get().GetCommandQueue();
    if (m_GpuTimingEnabled &&
        m_GpuTimestampProfiler.CollectCompletedFrame(*commandQueue, m_GpuTimestampSamples) &&
        e.TotalTime - m_LastGpuTimingUiUpdateTime >= 0.25)
    {
        m_GpuTimestampDisplaySamples = m_GpuTimestampSamples;
        m_LastGpuTimingUiUpdateTime = e.TotalTime;
    }
//Modify End

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

//Modify Begin:2026-07-28 by BestHui
    EnsureRenderGraphTopology();
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify End
//Modify Begin:2026-08-02 by BestHui
    const auto renderGraphCpuStart = std::chrono::steady_clock::now();
//Modify End
//Modify Begin:2026-07-28 by BestHui
    try
    {
        m_RenderGraph->Execute(metadata);
    }
    catch (const std::exception& exception)
    {
        throw std::runtime_error(std::string("RaytracingDemo::OnRender RenderGraph.Execute failed: ") + exception.what());
    }
//Modify Begin:2026-08-02 by BestHui
    m_LastRenderGraphCpuMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - renderGraphCpuStart).count();
//Modify End

    try
    {
        PresentDisplayOutput();
    }
    catch (const std::exception& exception)
    {
        throw std::runtime_error(std::string("RaytracingDemo::OnRender PresentDisplayOutput failed: ") + exception.what());
    }
//Modify End

    ++m_FrameIndex;
    if (m_AccumulationEnabled && !IsDenoiserEnabled())
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }

    m_PreviousViewProjection = GetSceneCamera().GetViewMatrix() * GetSceneCamera().GetProjectionMatrix();
    m_HasPreviousViewProjection = true;
}
