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
#include <Framework/UnitySceneParser.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
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

    XMFLOAT3 RotateUnityVector(const UnityQuaternion& rotation, const XMFLOAT3& value)
    {
        const XMVECTOR quaternion = XMVectorSet(rotation.X, rotation.Y, rotation.Z, rotation.W);
        const XMVECTOR vector = XMVectorSet(value.x, value.y, value.z, 0.0f);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Rotate(vector, quaternion));
        return result;
    }

    void ApplyUnityCamera(Camera& camera, const UnityCameraInfo& unityCamera, const int width, const int height)
    {
        const XMVECTOR position = XMVectorSet(
            unityCamera.Transform.WorldPosition.X,
            unityCamera.Transform.WorldPosition.Y,
            unityCamera.Transform.WorldPosition.Z,
            1.0f);
        const XMFLOAT3 forward = RotateUnityVector(unityCamera.Transform.WorldRotation, { 0.0f, 0.0f, 1.0f });
        const XMFLOAT3 up = RotateUnityVector(unityCamera.Transform.WorldRotation, { 0.0f, 1.0f, 0.0f });
        const XMVECTOR target = position + XMVectorSet(forward.x, forward.y, forward.z, 0.0f);
        camera.SetLookAt(position, target, XMVectorSet(up.x, up.y, up.z, 0.0f));
        camera.SetProjection(unityCamera.FieldOfView, static_cast<float>(width) / static_cast<float>(height), unityCamera.NearClipPlane, unityCamera.FarClipPlane);
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

//Modify Begin:2026-07-30 by BestHui
    UnityVector3 ToUnityVector3(const XMVECTOR value)
    {
        XMFLOAT3 stored{};
        XMStoreFloat3(&stored, value);
        return { stored.x, stored.y, stored.z };
    }

    UnityQuaternion ToUnityQuaternion(const XMVECTOR value)
    {
        XMFLOAT4 stored{};
        XMStoreFloat4(&stored, XMQuaternionNormalize(value));
        return { stored.x, stored.y, stored.z, stored.w };
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

    const std::filesystem::path unityScenePath = GetUnityScenePath();
    const UnitySceneData unityScene = UnitySceneParser::ParseFromFile(unityScenePath);
    if (unityScene.Cameras.empty())
    {
        throw std::runtime_error("Unity scene has no camera.");
    }

    if (!m_SceneResources.LoadUnityScene(*commandList, unityScene))
    {
        throw std::runtime_error("Unity scene has no supported renderable objects.");
    }
    m_Lights.CreateFromUnityScene(unityScene);
//Modify Begin:2026-07-29 by BestHui
//Modify Begin:2026-07-30 by BestHui
    m_SkyboxEnabled = true;
//Modify End
    const UnityCameraInfo& unityCamera = unityScene.Cameras.front();
//Modify Begin:2026-07-30 by BestHui
    m_UnityScenePath = unityScenePath;
    m_UnitySceneCamera = unityCamera;
    m_HasUnitySceneCamera = true;
//Modify End
    ApplyUnityCamera(m_Camera, unityCamera, m_Width, m_Height);
    const XMFLOAT3 forward = RotateUnityVector(unityCamera.Transform.WorldRotation, { 0.0f, 0.0f, 1.0f });
    CalculateCameraControllerFromLookDirection(
        XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
        m_CameraController.Yaw,
        m_CameraController.Pitch);
    m_CameraFov = unityCamera.FieldOfView;
//Modify End

    m_SkyboxTexture = std::make_shared<Texture>();
//Modify Begin:2026-07-30 by BestHui
    std::filesystem::path skyboxTexturePath;
    if (unityScene.HasSkyboxMaterial)
    {
        const UnityTextureBinding& skyboxTextureBinding = unityScene.SkyboxMaterial.MainTex.Texture.IsValid()
            ? unityScene.SkyboxMaterial.MainTex
            : unityScene.SkyboxMaterial.BaseMap;
        if (!skyboxTextureBinding.Texture.AssetPath.empty() && std::filesystem::exists(skyboxTextureBinding.Texture.AssetPath))
        {
            skyboxTexturePath = skyboxTextureBinding.Texture.AssetPath;
        }
    }
    if (skyboxTexturePath.empty())
    {
        skyboxTexturePath = L"Assets/Textures/skybox/skybox.dds";
    }
    commandList->LoadTextureFromFile(*m_SkyboxTexture, skyboxTexturePath.wstring(), TextureUsageType::Albedo);
//Modify End

    m_ImGui = std::make_unique<ImGuiImpl>(*commandList, *PWindow);

    m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);
//Modify Begin:2026-07-28 by BestHui
    m_DisplayBlitMesh = Mesh::CreateBlitTriangle(*commandList);
//Modify End

    m_GBufferShader = std::make_shared<Shader>(
        ShaderBlob(L"GBuffer.vs.cso"),
        ShaderBlob(L"GBuffer.ps.cso"),
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });

//Modify Begin:2026-07-28 by BestHui
    m_DisplayCompositeShader = std::make_shared<Shader>(
        ShaderBlob(L"DisplayComposite.vs.cso"),
        ShaderBlob(L"DisplayComposite.ps.cso"),
        [](RasterPipelineStateBuilder&) {});
//Modify End

//Modify Begin:2026-07-28 by BestHui
    const ShaderBlob skyboxComputeShader(L"Skybox.cs.cso");
    m_SkyboxComputeShader = std::make_shared<ComputeShader>(
        skyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(skyboxComputeShader).Build());
//Modify End

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
}

void RaytracingDemo::EnsureRenderGraphTopology()
{
    if (m_RenderGraph == nullptr ||
        m_RenderGraphDenoiserEnabled != IsDenoiserEnabled() ||
        m_RenderGraphCudaBloomEnabled != m_CudaBloom.IsEnabled())
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
    if (!m_HasUnitySceneCamera || m_UnityScenePath.empty())
    {
        throw std::runtime_error("No Unity scene camera is loaded.");
    }
    if (m_UnitySceneCamera.Transform.ParentTransformId != 0)
    {
        throw std::runtime_error("Saving camera with a parent transform is not supported yet.");
    }

    UnityCameraWriteInfo cameraWriteInfo;
    cameraWriteInfo.GameObjectId = m_UnitySceneCamera.GameObjectId;
    cameraWriteInfo.TransformFileId = m_UnitySceneCamera.Transform.FileId;
    cameraWriteInfo.LocalPosition = ToUnityVector3(m_Camera.GetTranslation());
    cameraWriteInfo.LocalRotation = ToUnityQuaternion(m_Camera.GetRotation());
    cameraWriteInfo.FieldOfView = m_CameraFov;
    UnitySceneParser::WriteCameraToFile(m_UnityScenePath, cameraWriteInfo);

    m_UnitySceneCamera.Transform.LocalPosition = cameraWriteInfo.LocalPosition;
    m_UnitySceneCamera.Transform.LocalRotation = cameraWriteInfo.LocalRotation;
    m_UnitySceneCamera.Transform.WorldPosition = cameraWriteInfo.LocalPosition;
    m_UnitySceneCamera.Transform.WorldRotation = cameraWriteInfo.LocalRotation;
    m_UnitySceneCamera.FieldOfView = cameraWriteInfo.FieldOfView;
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
//Modify Begin:2026-07-28 by BestHui
    try
    {
        m_RenderGraph->Execute(metadata);
    }
    catch (const std::exception& exception)
    {
        throw std::runtime_error(std::string("RaytracingDemo::OnRender RenderGraph.Execute failed: ") + exception.what());
    }

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

    m_PreviousViewProjection = m_Camera.GetViewMatrix() * m_Camera.GetProjectionMatrix();
    m_HasPreviousViewProjection = true;
}
