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
#include <Framework/Scene/SceneImporter.h>
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
//Modify Begin:2026-08-03 by BestHui
#include <ctime>
//Modify End
#include <filesystem>
//Modify Begin:2026-08-03 by BestHui
#include <fstream>
#include <iomanip>
//Modify End
#include <random>
#include <sstream>
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
    //Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext CreateFrameworkDeviceContext(Application& application)
    {
        FrameworkDeviceContextDesc desc;
        desc.Device = application.GetDevice();
        desc.DirectQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        desc.ComputeQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        desc.CopyQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
        desc.AllocateDescriptors = [&application](const D3D12_DESCRIPTOR_HEAP_TYPE type, const uint32_t count)
        {
            return application.AllocateDescriptors(type, count);
        };
        return FrameworkDeviceContext(std::move(desc));
    }
    //Modify End

    uint32_t ComputeDescriptorArrayCapacity(const size_t resourceCount, const size_t resourceCapacity)
    {
        return static_cast<uint32_t>(std::max<size_t>(
            RayTracingSceneResourceLayout::MinDescriptorArrayCapacity,
            std::max(resourceCount, resourceCapacity)));
    }

//Modify Begin:2026-08-03 by BestHui
    std::filesystem::path GetScenePath()
    {
        char* scenePath = nullptr;
        size_t scenePathLength = 0;
        _dupenv_s(&scenePath, &scenePathLength, "RAYTRACING_DEMO_SCENE");
        if (scenePath != nullptr)
        {
            std::filesystem::path result(scenePath);
            std::free(scenePath);
            scenePath = nullptr;
            if (!result.empty() && std::filesystem::exists(result))
            {
                return result;
            }
            throw std::runtime_error("RAYTRACING_DEMO_SCENE is set but the scene file does not exist.");
        }
        std::free(scenePath);

        _dupenv_s(&scenePath, &scenePathLength, "RAYTRACING_DEMO_UNITY_SCENE");
        if (scenePath != nullptr)
        {
            std::filesystem::path result(scenePath);
            std::free(scenePath);
            if (!result.empty() && std::filesystem::exists(result))
            {
                return result;
            }
            throw std::runtime_error("RAYTRACING_DEMO_UNITY_SCENE is set but the scene file does not exist.");
        }
        std::free(scenePath);

        constexpr const char* DefaultSponzaRelativePath =
            "Assets/Scenes/Sponza.unity";
        std::filesystem::path searchDirectory = std::filesystem::current_path();
        while (!searchDirectory.empty())
        {
            const std::filesystem::path defaultScene = searchDirectory / DefaultSponzaRelativePath;
            if (std::filesystem::exists(defaultScene))
            {
                return defaultScene;
            }

            const std::filesystem::path parentDirectory = searchDirectory.parent_path();
            if (parentDirectory == searchDirectory)
            {
                break;
            }
            searchDirectory = parentDirectory;
        }

        throw std::runtime_error("Default Sponza scene file does not exist. Set RAYTRACING_DEMO_SCENE to a .json or .unity file.");
    }
//Modify End

//Modify Begin:2026-08-03 by BestHui
    std::string EscapeCsvField(const std::string& value)
    {
        std::string escaped = "\"";
        for (const char character : value)
        {
            if (character == '\"')
            {
                escaped += "\"\"";
            }
            else
            {
                escaped += character;
            }
        }
        escaped += "\"";
        return escaped;
    }
//Modify End

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
    , m_FrameworkDeviceContext(CreateFrameworkDeviceContext(Application::Get()))
    , m_PathTracingPipelines(m_FrameworkDeviceContext)
//Modify Begin:2026-07-30 by BestHui
    , m_DirectLightingReSTIRDIPass(
        m_FrameworkDeviceContext,
        {
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl",
            { { "RAYTRACING_DEMO_SOFT_SHADOWS", "1" } },
//Modify Begin:2026-08-06 by BestHui
            "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
//Modify End
        })
//Modify End
    , m_SceneResources(Application::Get().GetDevice())
    , m_Lights(m_FrameworkDeviceContext)
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

//Modify Begin:2026-07-30 by BestHui
    char* softShadows = nullptr;
    size_t softShadowsLength = 0;
    _dupenv_s(&softShadows, &softShadowsLength, "RAYTRACING_DEMO_SOFT_SHADOWS");
    if (softShadows != nullptr)
    {
        m_SoftShadowsEnabled = std::strcmp(softShadows, "0") != 0;
    }
    std::free(softShadows);
//Modify End

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

//Modify Begin:2026-07-30 by BestHui
    char* asyncCompute = nullptr;
    size_t asyncComputeLength = 0;
    _dupenv_s(&asyncCompute, &asyncComputeLength, "RAYTRACING_DEMO_ASYNC_COMPUTE");
    if (asyncCompute != nullptr)
    {
        m_AsyncComputeEnabled = std::strcmp(asyncCompute, "0") != 0;
    }
    std::free(asyncCompute);

    char* debugSerializeAsyncCompute = nullptr;
    size_t debugSerializeAsyncComputeLength = 0;
    _dupenv_s(
        &debugSerializeAsyncCompute,
        &debugSerializeAsyncComputeLength,
        "RAYTRACING_DEMO_ASYNC_CPU_SERIALIZE");
    if (debugSerializeAsyncCompute != nullptr)
    {
        m_DebugSerializeAsyncCompute = std::strcmp(debugSerializeAsyncCompute, "0") != 0;
    }
    std::free(debugSerializeAsyncCompute);
//Modify End

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
//Modify Begin:2026-08-03 by BestHui
void RaytracingDemo::LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath)
{
    const SceneImportResult sceneImport = SceneImporter::ImportFromFile(scenePath);
    m_Scene = sceneImport.SceneData;
    std::filesystem::path runtimeStatePath = scenePath;
    runtimeStatePath += ".runtime.json";
    if (std::filesystem::exists(runtimeStatePath))
    {
        SceneImporter::ApplyJsonRuntimeState(runtimeStatePath, m_Scene);
    }
    const SceneCamera& sceneCamera = m_Scene.GetCamera();

    if (!m_SceneResources.LoadScene(commandList, m_Scene, m_StressTestSpheresEnabled))
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
//Modify Begin:2026-08-06 by BestHui
    m_Lights.SetEmissiveMeshLights(m_SceneResources.CollectEmissiveMeshLights());
//Modify End
    m_SkyboxEnabled = !skyboxTexturePath.empty() && std::filesystem::exists(skyboxTexturePath);
    m_HasSceneCamera = sceneCamera.RuntimeCamera != nullptr;

    ApplySceneCamera(GetSceneCamera(), sceneCamera, m_Width, m_Height);
    const XMFLOAT3 forward = RotateCameraVector(GetSceneCamera().GetRotation(), { 0.0f, 0.0f, 1.0f });
    CalculateCameraControllerFromLookDirection(
        XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
        m_CameraController.Yaw,
        m_CameraController.Pitch);
    m_CameraFov = sceneCamera.FieldOfView;
    m_CameraNearClipPlane = sceneCamera.NearClipPlane;
    m_CameraFarClipPlane = sceneCamera.FarClipPlane;
//Modify Begin:2026-07-30 by BestHui
    XMStoreFloat3(&m_InitialSceneCameraTranslation, GetSceneCamera().GetTranslation());
    XMStoreFloat4(&m_InitialSceneCameraRotation, GetSceneCamera().GetRotation());
    m_InitialSceneCameraYaw = m_CameraController.Yaw;
    m_InitialSceneCameraPitch = m_CameraController.Pitch;
    m_HasInitialSceneCameraState = true;
//Modify End

    m_SkyboxTexture.reset();
    if (m_SkyboxEnabled)
    {
        m_SkyboxTexture = std::make_shared<Texture>();
        commandList.LoadTextureFromFile(*m_SkyboxTexture, skyboxTexturePath.wstring(), TextureUsageType::Albedo);
    }
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::ResetCameraToInitialSceneState()
{
    if (!m_HasInitialSceneCameraState)
    {
        return;
    }

    GetSceneCamera().SetTranslation(XMLoadFloat3(&m_InitialSceneCameraTranslation));
    GetSceneCamera().SetRotation(XMLoadFloat4(&m_InitialSceneCameraRotation));
    m_CameraController.Yaw = m_InitialSceneCameraYaw;
    m_CameraController.Pitch = m_InitialSceneCameraPitch;
    m_HasPreviousViewProjection = false;
    ResetAccumulation();
}

void RaytracingDemo::AppendRuntimeAutomationLog(const std::string& message) const
{
    if (m_RuntimeAutomationLogPath.empty())
    {
        return;
    }

    std::ofstream log(m_RuntimeAutomationLogPath, std::ios::app);
    if (log.is_open())
    {
        log << message << '\n';
    }
}

void RaytracingDemo::InitializeRuntimeAutomation()
{
    char* automationMode = nullptr;
    size_t automationModeLength = 0;
    _dupenv_s(&automationMode, &automationModeLength, "RAYTRACING_DEMO_AUTOTEST");
    const std::string mode = automationMode != nullptr ? automationMode : "";
    std::free(automationMode);
    if (mode.empty() || mode == "0" || mode == "off")
    {
        return;
    }

    m_RuntimeAutomationLogPath = std::filesystem::current_path() / "Saved" / "RuntimeAutomation.log";
    std::filesystem::create_directories(m_RuntimeAutomationLogPath.parent_path());
    std::ofstream(m_RuntimeAutomationLogPath, std::ios::trunc) << "Runtime automation started." << '\n';

    char* stepMilliseconds = nullptr;
    size_t stepMillisecondsLength = 0;
    _dupenv_s(&stepMilliseconds, &stepMillisecondsLength, "RAYTRACING_DEMO_AUTOTEST_STEP_MS");
    if (stepMilliseconds != nullptr)
    {
        const double milliseconds = std::strtod(stepMilliseconds, nullptr);
        if (milliseconds > 0.0)
        {
            m_RuntimeAutomationStepIntervalSeconds = milliseconds / 1000.0;
        }
    }
    std::free(stepMilliseconds);

    char* quitOnComplete = nullptr;
    size_t quitOnCompleteLength = 0;
    _dupenv_s(&quitOnComplete, &quitOnCompleteLength, "RAYTRACING_DEMO_AUTOTEST_QUIT");
    m_RuntimeAutomationQuitOnComplete = quitOnComplete != nullptr && std::strcmp(quitOnComplete, "0") != 0;
    std::free(quitOnComplete);

    const auto addStep = [this](const RuntimeAutomationAction action, const uint32_t value, std::string name)
    {
        m_RuntimeAutomationSteps.push_back({ action, value, std::move(name) });
    };

    if (mode == "core")
    {
        addStep(RuntimeAutomationAction::SoftShadows, 0u, "soft=0");
        addStep(RuntimeAutomationAction::SoftShadows, 1u, "soft=1");
        addStep(RuntimeAutomationAction::StressSpheres, 1u, "stress=1");
        addStep(RuntimeAutomationAction::StressSpheres, 0u, "stress=0");
        addStep(RuntimeAutomationAction::StressSpheres, 1u, "stress=1");
        addStep(RuntimeAutomationAction::StressSpheres, 0u, "stress=0");
        addStep(RuntimeAutomationAction::MeshletGBuffer, 0u, "meshlet=0");
        addStep(RuntimeAutomationAction::MeshletGBuffer, 1u, "meshlet=1");
        addStep(RuntimeAutomationAction::MeshletTaskShader, 0u, "meshletbackend=indirect");
        addStep(RuntimeAutomationAction::MeshletTaskShader, 1u, "meshletbackend=task");
        addStep(RuntimeAutomationAction::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "direct=pathtracing");
        addStep(RuntimeAutomationAction::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::PathTracing), "indirect=pathtracing");
        addStep(RuntimeAutomationAction::AsyncCompute, 1u, "async=1");
        addStep(RuntimeAutomationAction::AsyncCompute, 0u, "async=0");
        addStep(RuntimeAutomationAction::IndirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::None), "indirect=none");
        addStep(RuntimeAutomationAction::DirectLighting, static_cast<uint32_t>(RaytracingDemoLightingTechnique::ReSTIRDI), "direct=restirdi");
        addStep(RuntimeAutomationAction::Skybox, 0u, "skybox=0");
        addStep(RuntimeAutomationAction::Skybox, 1u, "skybox=1");
        addStep(RuntimeAutomationAction::Accumulation, 0u, "accumulation=0");
        addStep(RuntimeAutomationAction::Accumulation, 1u, "accumulation=1");
    }
    else if (mode == "stress")
    {
        addStep(RuntimeAutomationAction::StressSpheres, 1u, "stress=1");
        addStep(RuntimeAutomationAction::StressSpheres, 0u, "stress=0");
        addStep(RuntimeAutomationAction::StressSpheres, 1u, "stress=1");
        addStep(RuntimeAutomationAction::StressSpheres, 0u, "stress=0");
    }
    else
    {
        throw std::runtime_error("RAYTRACING_DEMO_AUTOTEST must be 'core' or 'stress'.");
    }

    m_RuntimeAutomationEnabled = !m_RuntimeAutomationSteps.empty();
    m_RuntimeAutomationLastStepTime = -m_RuntimeAutomationStepIntervalSeconds;
    AppendRuntimeAutomationLog("Mode=" + mode + ", step interval=" + std::to_string(m_RuntimeAutomationStepIntervalSeconds) + " seconds.");
}

void RaytracingDemo::UpdateRuntimeAutomation(const double totalTime)
{
    if (!m_RuntimeAutomationEnabled || m_RuntimeAutomationCompleted ||
        totalTime - m_RuntimeAutomationLastStepTime < m_RuntimeAutomationStepIntervalSeconds)
    {
        return;
    }

    if (m_RuntimeAutomationStepIndex >= m_RuntimeAutomationSteps.size())
    {
        m_RuntimeAutomationCompleted = true;
        AppendRuntimeAutomationLog("Runtime automation completed.");
        if (m_RuntimeAutomationQuitOnComplete)
        {
            Application::Get().Quit(0);
        }
        return;
    }

    const RuntimeAutomationStep& step = m_RuntimeAutomationSteps[m_RuntimeAutomationStepIndex++];
    const bool enabled = step.Value != 0u;
    switch (step.Action)
    {
    case RuntimeAutomationAction::SoftShadows:
        m_SoftShadowsEnabled = enabled;
        EnsureRayTracingPipelines();
        BindRayTracingShaderResources();
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::StressSpheres:
        m_StressTestSpheresEnabled = enabled;
        m_StressTestSpheresStateDirty = true;
        break;
    case RuntimeAutomationAction::MeshletGBuffer:
        m_UseMeshletGBuffer = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::MeshletTaskShader:
        m_UseTaskShaderMeshlets = enabled;
        m_UseMeshletGBuffer = true;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::DirectLighting:
        m_DirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(step.Value);
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::IndirectLighting:
        m_IndirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(step.Value);
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::AsyncCompute:
        if (m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = enabled;
            ResetAccumulation();
        }
        break;
    case RuntimeAutomationAction::Skybox:
        m_SkyboxEnabled = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::Accumulation:
        m_AccumulationEnabled = enabled;
        ResetAccumulation();
        break;
    }

    m_RuntimeAutomationLastStepTime = totalTime;
    AppendRuntimeAutomationLog("Applied " + step.Name + ".");
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::ApplyStressTestSpheresState()
{
    if (!m_StressTestSpheresStateDirty)
    {
        return;
    }

    if (m_SceneResources.AreStressTestSpheresEnabled() == m_StressTestSpheresEnabled)
    {
        m_StressTestSpheresStateDirty = false;
        return;
    }

    AppendRuntimeAutomationLog("Stress resource update begin: enabled=" + std::to_string(m_StressTestSpheresEnabled));
    Application::Get().Flush();
    AppendRuntimeAutomationLog("Stress resource update: queues flushed.");
    const auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();
    if (m_SceneResources.SetStressTestSpheresEnabled(*commandList, m_StressTestSpheresEnabled))
    {
        AppendRuntimeAutomationLog("Stress resource update: scene resources updated.");
        const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
        commandQueue->WaitForFenceValue(fenceValue);
        AppendRuntimeAutomationLog("Stress resource update: GPU update completed.");
        EnsureRayTracingPipelines();
        BindRayTracingShaderResources();
        AppendRuntimeAutomationLog("Stress resource update: ray tracing resources rebound.");

        ClearRenderGraphTimingHistory();
        ResetAccumulation();
    }

    m_StressTestSpheresStateDirty = false;
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
std::shared_ptr<ShaderBlob> RaytracingDemo::LoadShaderVariant(
    std::wstring compiledFileName,
    std::wstring sourceFileName,
    std::string targetProfile,
    std::vector<ShaderVariantDefine> defines)
{
    ShaderVariantDesc desc;
    desc.CompiledFileName = std::move(compiledFileName);
    desc.SourceFileName = std::move(sourceFileName);
    desc.TargetProfile = std::move(targetProfile);
    desc.Defines = std::move(defines);
    desc.DebugName = "RaytracingDemo";
    return m_ShaderVariants.GetOrCompile(desc);
}
//Modify End

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(m_FrameworkDeviceContext), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

//Modify Begin:2026-08-03 by BestHui
    LoadSceneContent(*commandList, GetScenePath());
//Modify End

    m_ImGui = std::make_unique<ImGuiImpl>(m_FrameworkDeviceContext, *commandList, *PWindow);

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
    const auto vertexShader = LoadShaderVariant(
        L"GBuffer.vs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.vs.hlsl",
        "vs_6_6");
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.ps.hlsl",
        "ps_6_6");
    m_GBufferShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
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
    const auto amplificationShader = LoadShaderVariant(
        L"GBuffer.task.as.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.as.hlsl",
        "as_6_6");
    const auto meshShader = LoadShaderVariant(
        L"GBuffer.task.ms.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.task.ms.hlsl",
        "ms_6_6");
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
        "ps_6_6");
    m_GBufferTaskMeshShader = std::make_shared<MeshShader>(
        m_FrameworkDeviceContext,
        *amplificationShader,
        *meshShader,
        *pixelShader,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });
//Modify End

    withShaderCreateContext("GBufferMeshletIndirect", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.vs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.vs.hlsl",
        "vs_6_6");
    const auto pixelShader = LoadShaderVariant(
        L"GBuffer.meshletindirect.ps.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/GBuffer.meshletindirect.ps.hlsl",
        "ps_6_6");
    PipelineLayoutReflectionOptions meshletIndirectLayoutOptions;
    meshletIndirectLayoutOptions.MaxDescriptorCount = 4096u;
    meshletIndirectLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    meshletIndirectLayoutOptions.RootConstantBufferNames.push_back("MeshletDrawCBuffer");
    m_GBufferMeshletIndirectShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        meshletIndirectLayoutOptions,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithNoCull();
        });
    });

    withShaderCreateContext("MeshletCull", [&]()
    {
    const auto meshletCullShaderBlob = LoadShaderVariant(
        L"MeshletCull.cs.cso",
        L"Demos/RaytracingDemo/shaders/GBuffer/MeshletCull.cs.hlsl",
        "cs_6_6");
    m_MeshletCullShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *meshletCullShaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(*meshletCullShaderBlob).Build());
    m_MeshletDrawCommandSignature = m_GBufferMeshletIndirectShader->CreateIndirectDrawCommandSignature(
        "MeshletDrawCBuffer",
        sizeof(MeshletIndirectCommand));
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("DisplayComposite", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"DisplayComposite.vs.cso",
        L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.vs.hlsl",
        "vs_6_6");
    const auto pixelShader = LoadShaderVariant(
        L"DisplayComposite.ps.cso",
        L"Demos/RaytracingDemo/shaders/PostProcessing/DisplayComposite.ps.hlsl",
        "ps_6_6");
    m_DisplayCompositeShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        [](RasterPipelineStateBuilder&) {});
    });
//Modify End

//Modify Begin:2026-07-28 by BestHui
    withShaderCreateContext("SkyboxCompute", [&]()
    {
    const auto skyboxComputeShader = LoadShaderVariant(
        L"Skybox.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/Skybox.cs.hlsl",
        "cs_6_6");
    m_SkyboxComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *skyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*skyboxComputeShader).Build());

    const auto equirectangularSkyboxComputeShader = LoadShaderVariant(
        L"SkyboxEquirectangular.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/SkyboxEquirectangular.cs.hlsl",
        "cs_6_6");
    m_SkyboxEquirectangularComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *equirectangularSkyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*equirectangularSkyboxComputeShader).Build());

//Modify Begin:2026-08-06 by BestHui
    const auto cubemapStripSkyboxComputeShader = LoadShaderVariant(
        L"SkyboxCubemapStrip.cs.cso",
        L"Demos/RaytracingDemo/shaders/Skybox/SkyboxCubemapStrip.cs.hlsl",
        "cs_6_6");
    m_SkyboxCubemapStripComputeShader = std::make_shared<ComputeShader>(
        m_FrameworkDeviceContext,
        *cubemapStripSkyboxComputeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*cubemapStripSkyboxComputeShader).Build());
//Modify End
    });
//Modify End

    withShaderCreateContext("LightBillboard", [&]()
    {
    const auto vertexShader = LoadShaderVariant(
        L"LightBillboard.vs.cso",
        L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.vs.hlsl",
        "vs_6_6");
    const auto pixelShader = LoadShaderVariant(
        L"LightBillboard.ps.cso",
        L"Demos/RaytracingDemo/shaders/LightBillboard/LightBillboard.ps.hlsl",
        "ps_6_6");
    m_LightBillboardShader = std::make_shared<Shader>(
        m_FrameworkDeviceContext,
        *vertexShader,
        *pixelShader,
        [](RasterPipelineStateBuilder& builder)
        {
            builder.WithAlphaBlend().WithDepthTestNoWrite().WithNoCull();
        });
    });

    m_Denoisers.Initialize(m_FrameworkDeviceContext);
    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_SceneResources.BuildRayTracingAccelerationStructure(*commandList, accelerationStructureSettings);
    m_Lights.InitializeGpuBuffers(*commandList);
//Modify Begin:2026-07-27 by BestHui
    EnsureRayTracingPipelines();
//Modify End
//Modify Begin:2026-08-06 by BestHui
    PrewarmRuntimeShadowVariants();
//Modify End
    BindRayTracingShaderResources();

//Modify Begin:2026-07-28 by BestHui
//Modify Begin:2026-07-29 by BestHui
    m_GpuTimestampProfiler.Initialize(128);
//Modify Begin:2026-08-03 by BestHui
    m_AsyncComputeGpuTimestampProfiler.Initialize(128, D3D12_COMMAND_LIST_TYPE_COMPUTE);
//Modify End
//Modify End
    RebuildRenderGraph();
//Modify End

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
//Modify Begin:2026-07-30 by BestHui
    InitializeRuntimeAutomation();
//Modify End
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
    m_SkyboxEquirectangularComputeShader.reset();
//Modify Begin:2026-08-06 by BestHui
    m_SkyboxCubemapStripComputeShader.reset();
//Modify End
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
//Modify Begin:2026-08-03 by BestHui
    m_AsyncComputeGpuTimestampProfiler.Shutdown();
//Modify End
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
//Modify Begin:2026-08-06 by BestHui
    layout.EnvironmentProjection = m_SkyboxTexture != nullptr
        ? ShaderResourceView::GetEnvironmentTextureProjection(*m_SkyboxTexture)
        : EnvironmentTextureProjection::Cubemap;
//Modify End
    return layout;
}

void RaytracingDemo::EnsureRayTracingPipelines()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
//Modify Begin:2026-07-27 by BestHui
    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        m_SoftShadowsEnabled ? PathTracingShadowMode::SoftShadows : PathTracingShadowMode::HardShadows,
        layout);
//Modify Begin:2026-07-30 by BestHui
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        m_SoftShadowsEnabled,
        static_cast<uint32_t>(layout.EnvironmentProjection));
//Modify End
    if (m_SceneResources.GetRayTracingAccelerationStructure().GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::PrewarmRuntimeShadowVariants()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const PathTracingShadowMode currentShadowMode = m_SoftShadowsEnabled
        ? PathTracingShadowMode::SoftShadows
        : PathTracingShadowMode::HardShadows;
    const PathTracingShadowMode alternateShadowMode = currentShadowMode == PathTracingShadowMode::SoftShadows
        ? PathTracingShadowMode::HardShadows
        : PathTracingShadowMode::SoftShadows;

    m_PathTracingPipelines.EnsurePipelines(m_PathTracingBackend, alternateShadowMode, layout);
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        alternateShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection));
    m_PathTracingPipelines.EnsurePipelines(m_PathTracingBackend, currentShadowMode, layout);
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        currentShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection));
    BindRayTracingShaderResources();
}
//Modify End

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
//Modify Begin:2026-07-30 by BestHui
    UpdateRenderGraphFrameState();
//Modify End
    if (m_RenderGraph != nullptr)
    {
        Application::Get().Flush();
//Modify Begin:2026-07-28 by BestHui
        m_CudaBloom.ReleaseInteropResource();
//Modify End
    }
//Modify Begin:2026-07-30 by BestHui
    EnsureRayTracingPipelines();
//Modify End
    m_RenderGraph = RaytracingDemoRenderGraphBuilder::Create(*this);
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraph->SetAsyncComputeGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraph->SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
//Modify End
//Modify End
//Modify End
    m_RenderGraphDenoiserEnabled = IsDenoiserEnabled();
    m_RenderGraphCudaBloomEnabled = m_CudaBloom.IsEnabled();
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraphAsyncComputeEnabled = m_AsyncComputeEnabled;
    m_RenderGraphPathTracingBackend = m_PathTracingBackend;
//Modify Begin:2026-08-06 by BestHui
    m_RenderGraphDirectLightingTechnique = m_DirectLightingTechnique;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraphLightingDebugTextureTarget = m_DebugLightingTextureTarget;
//Modify End
//Modify End
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
//Modify Begin:2026-08-03 by BestHui
        || m_RenderGraphAsyncComputeEnabled != m_AsyncComputeEnabled
        || m_RenderGraphPathTracingBackend != m_PathTracingBackend
//Modify Begin:2026-08-06 by BestHui
        || m_RenderGraphDirectLightingTechnique != m_DirectLightingTechnique
//Modify End
//Modify Begin:2026-07-30 by BestHui
        || m_RenderGraphLightingDebugTextureTarget != m_DebugLightingTextureTarget
//Modify End
//Modify End
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

void RaytracingDemo::ResetAccumulation(bool resetDenoiserHistory, bool resetReSTIRDIHistory)
{
    m_AccumulationFrameIndex = 0;
//Modify Begin:2026-08-05 by BestHui
    if (resetReSTIRDIHistory)
    {
        m_ReSTIRDIHistoryValid = false;
    }
//Modify End
    if (resetDenoiserHistory)
    {
        m_Denoisers.ResetHistory();
    }
}

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::SaveCurrentCameraToUnityScene()
{
//Modify Begin:2026-08-03 by BestHui
    if (m_Scene.GetSourcePath().extension() != ".unity")
    {
        throw std::runtime_error("Saving a JSON scene camera is not implemented yet.");
    }
//Modify End
    if (!m_HasSceneCamera || m_Scene.GetSourcePath().empty())
    {
        throw std::runtime_error("No source scene camera is loaded.");
    }
    if (m_Scene.GetCamera().SourceBinding.ParentTransformId != 0)
    {
        throw std::runtime_error("Saving camera with a parent transform is not supported yet.");
    }

    m_Scene.UpdateCamera(GetSceneCamera(), m_CameraFov, m_CameraNearClipPlane, m_CameraFarClipPlane);
    SceneImporter::WriteCameraToSourceFile(m_Scene.GetSourcePath(), m_Scene.GetCamera());
    m_CameraSaveStatus = "Camera saved to Unity scene.";
}

void RaytracingDemo::SaveCurrentScene()
{
    if (!m_HasSceneCamera || m_Scene.GetSourcePath().empty())
    {
        throw std::runtime_error("No source scene camera is loaded.");
    }

    m_Scene.UpdateCamera(GetSceneCamera(), m_CameraFov, m_CameraNearClipPlane, m_CameraFarClipPlane);
    m_Lights.ApplyToScene(m_Scene);
    std::filesystem::path runtimeStatePath = m_Scene.GetSourcePath();
    runtimeStatePath += ".runtime.json";
    SceneImporter::WriteJsonRuntimeState(runtimeStatePath, m_Scene);
    m_CameraSaveStatus = "Runtime scene state saved to " + runtimeStatePath.filename().string() + ".";
}
//Modify End
//Modify End

//Modify Begin:2026-08-03 by BestHui
void RaytracingDemo::ClearRenderGraphTimingHistory()
{
    m_RenderGraphTimingHistory.clear();
    m_RenderGraphTimingExportStatus = "RG timing history cleared.";
}

void RaytracingDemo::RecordRenderGraphTimingSamples(
    const uint64_t frameNumber,
    const char* queueName,
    const std::vector<GpuTimestampSample>& samples)
{
    if (samples.empty())
    {
        return;
    }

    auto frameIt = std::find_if(
        m_RenderGraphTimingHistory.begin(),
        m_RenderGraphTimingHistory.end(),
        [frameNumber](const RenderGraphTimingFrame& frame) { return frame.FrameNumber == frameNumber; });
    if (frameIt == m_RenderGraphTimingHistory.end())
    {
        const auto insertPosition = std::find_if(
            m_RenderGraphTimingHistory.begin(),
            m_RenderGraphTimingHistory.end(),
            [frameNumber](const RenderGraphTimingFrame& frame) { return frame.FrameNumber > frameNumber; });
        frameIt = m_RenderGraphTimingHistory.insert(insertPosition, { frameNumber, {} });
    }

    const std::string queueNameString = queueName != nullptr ? queueName : "Unknown";
    auto queueIt = std::find_if(
        frameIt->Queues.begin(),
        frameIt->Queues.end(),
        [&queueNameString](const RenderGraphTimingQueue& queue) { return queue.QueueName == queueNameString; });
    if (queueIt == frameIt->Queues.end())
    {
        frameIt->Queues.push_back({ queueNameString, samples });
    }
    else
    {
        queueIt->Samples = samples;
    }

    while (m_RenderGraphTimingHistory.size() > static_cast<size_t>(m_RenderGraphTimingHistoryCapacity))
    {
        m_RenderGraphTimingHistory.pop_front();
    }
}

//Modify Begin:2026-07-30 by BestHui
RaytracingDemoPassResources RaytracingDemo::CreatePassResources()
{
    return {
        m_SceneResources,
        m_Lights,
        m_PathTracingPipelines,
//Modify Begin:2026-08-05 by BestHui
        m_DirectLightingReSTIRDI,
//Modify Begin:2026-07-30 by BestHui
        m_DirectLightingReSTIRDIPass,
//Modify End
//Modify End
        m_Denoisers,
        m_CudaBloom,
        m_GBufferShader,
        m_GBufferMeshletIndirectShader,
        m_GBufferTaskMeshShader,
        m_MeshletCullShader,
        m_MeshletDrawCommandSignature.get(),
        m_DisplayCompositeShader,
        m_SkyboxComputeShader,
        m_SkyboxEquirectangularComputeShader,
//Modify Begin:2026-08-06 by BestHui
        m_SkyboxCubemapStripComputeShader,
//Modify End
        m_SkyboxTexture,
        m_DisplayBlitMesh,
        GetSceneCamera(),
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)
    };
}

RaytracingDemoPassConfig RaytracingDemo::CreatePassConfig() const
{
    return {
        m_RenderGraphFrameState,
    };
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void RaytracingDemo::UpdateRenderGraphFrameState()
{
    RaytracingDemoFrameState& state = *m_RenderGraphFrameState;
    state.Backend = m_PathTracingBackend;
    state.DirectLightingTechnique = m_DirectLightingTechnique;
    state.IndirectLightingTechnique = m_IndirectLightingTechnique;
    state.AsyncComputeEnabled = m_AsyncComputeEnabled;
    state.UseMeshletGBuffer = m_UseMeshletGBuffer;
    state.UseTaskShaderMeshlets = m_UseTaskShaderMeshlets;
    state.DebugMeshletClusters = m_DebugMeshletClusters;
    state.SkyboxEnabled = m_SkyboxEnabled;
    state.DebugLightingTextureTarget = m_DebugLightingTextureTarget;
    state.DebugTextureTarget = m_DebugTextureTarget;
    state.MaxBounces = m_MaxBounces;
    state.Width = static_cast<uint32_t>(m_Width);
    state.Height = static_cast<uint32_t>(m_Height);
    state.AccumulationEnabled = m_AccumulationEnabled;
    state.FrameIndex = m_FrameIndex;
    state.AccumulationFrameIndex = m_AccumulationFrameIndex;
    state.ReSTIRDIHistoryValid = m_ReSTIRDIHistoryValid;
    state.HasPreviousViewProjection = m_HasPreviousViewProjection;
    state.PreviousViewProjection = m_PreviousViewProjection;
}
//Modify End

bool RaytracingDemo::DumpRenderGraphTimingHistory()
{
    if (m_RenderGraphTimingHistory.empty())
    {
        m_RenderGraphTimingExportStatus = "RG timing history is empty.";
        return false;
    }

    try
    {
        const std::filesystem::path outputDirectory = std::filesystem::current_path() / "Profiling";
        std::filesystem::create_directories(outputDirectory);

        const auto currentTime = std::chrono::system_clock::now();
        const std::time_t currentTimeValue = std::chrono::system_clock::to_time_t(currentTime);
        std::tm localTime{};
        localtime_s(&localTime, &currentTimeValue);
        std::ostringstream filename;
        filename << "RenderGraphTiming_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".csv";
        const std::filesystem::path outputPath = outputDirectory / filename.str();

        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open output file.");
        }

        output << "frame,queue,marker,gpu_delta_ms,gpu_total_ms,cpu_delta_ms,cpu_total_ms\n";
        output << std::fixed << std::setprecision(6);
        for (const RenderGraphTimingFrame& frame : m_RenderGraphTimingHistory)
        {
            for (const RenderGraphTimingQueue& queue : frame.Queues)
            {
                for (const GpuTimestampSample& sample : queue.Samples)
                {
                    output << frame.FrameNumber << ','
                           << EscapeCsvField(queue.QueueName) << ','
                           << EscapeCsvField(sample.Name) << ','
                           << sample.MillisecondsFromPrevious << ','
                           << sample.MillisecondsFromFrameStart << ','
                           << sample.CpuMillisecondsFromPrevious << ','
                           << sample.CpuMillisecondsFromFrameStart << '\n';
                }
            }
        }

        m_RenderGraphTimingExportStatus = "Exported " + std::to_string(m_RenderGraphTimingHistory.size()) +
            " frames to " + outputPath.string();
        return true;
    }
    catch (const std::exception& exception)
    {
        m_RenderGraphTimingExportStatus = std::string("RG timing export failed: ") + exception.what();
        return false;
    }
}
//Modify End

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

//Modify Begin:2026-07-29 by BestHui
    const auto directCommandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
//Modify Begin:2026-08-03 by BestHui
    const auto asyncComputeCommandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
//Modify End
//Modify Begin:2026-08-03 by BestHui
    const auto collectGpuTimingFrames = [this](
        GpuTimestampProfiler& profiler,
        CommandQueue& commandQueue,
        std::vector<GpuTimestampSample>& latestSamples,
        const char* queueName)
    {
        bool collectedAnyFrame = false;
        std::vector<GpuTimestampSample> completedSamples;
        while (profiler.CollectCompletedFrame(commandQueue, completedSamples))
        {
            latestSamples = completedSamples;
            if (m_RenderGraphTimingCaptureEnabled)
            {
                RecordRenderGraphTimingSamples(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
            collectedAnyFrame = true;
        }
        return collectedAnyFrame;
    };
    const bool collectedDirectGpuTimingFrame = m_GpuTimingEnabled && collectGpuTimingFrames(
        m_GpuTimestampProfiler,
        *directCommandQueue,
        m_GpuTimestampSamples,
        "Direct");
    const bool collectedAsyncComputeGpuTimingFrame = m_GpuTimingEnabled && collectGpuTimingFrames(
        m_AsyncComputeGpuTimestampProfiler,
        *asyncComputeCommandQueue,
        m_AsyncComputeGpuTimestampSamples,
        "AsyncCompute");
//Modify End
    if ((collectedDirectGpuTimingFrame || collectedAsyncComputeGpuTimingFrame) &&
        e.TotalTime - m_LastGpuTimingUiUpdateTime >= 0.25)
    {
        if (collectedDirectGpuTimingFrame)
        {
            m_GpuTimestampDisplaySamples = m_GpuTimestampSamples;
        }
        if (collectedAsyncComputeGpuTimingFrame)
        {
            m_AsyncComputeGpuTimestampDisplaySamples = m_AsyncComputeGpuTimestampSamples;
        }
        m_LastGpuTimingUiUpdateTime = e.TotalTime;
    }
//Modify End

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

//Modify Begin:2026-07-30 by BestHui
    ApplyStressTestSpheresState();
//Modify End

    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = static_cast<uint32_t>(m_Width);
    metadata.m_ScreenHeight = static_cast<uint32_t>(m_Height);
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;

//Modify Begin:2026-07-28 by BestHui
//Modify Begin:2026-07-30 by BestHui
    UpdateRenderGraphFrameState();
//Modify End
    EnsureRenderGraphTopology();
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_RenderGraph->SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
//Modify Begin:2026-08-03 by BestHui
    m_RenderGraph->SetAsyncComputeGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
//Modify Begin:2026-07-30 by BestHui
    m_RenderGraph->SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
//Modify End
//Modify End
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
//Modify Begin:2026-08-03 by BestHui
        const char* backendName = m_PathTracingBackend == PathTracingBackend::InlineRayQuery
            ? "InlineRayQuery"
            : "ShaderTableDxr";
        throw std::runtime_error(
            std::string("RaytracingDemo::OnRender PresentDisplayOutput failed: ") + exception.what() +
            " [Backend=" + backendName +
            ", AsyncCompute=" + (m_AsyncComputeEnabled ? "true" : "false") + "]");
//Modify End
    }
//Modify End

    ++m_FrameIndex;
//Modify Begin:2026-08-06 by BestHui
    if (m_AccumulationEnabled)
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }
//Modify Begin:2026-08-05 by BestHui
    m_ReSTIRDIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
//Modify End

    m_PreviousViewProjection = GetSceneCamera().GetViewMatrix() * GetSceneCamera().GetProjectionMatrix();
    m_HasPreviousViewProjection = true;
}
