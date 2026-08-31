#include <RaytracingDemo.h>

//Modify Begin:2026-08-23 by Hui
#include <imgui.h>
#include <thread>
//Modify End

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/Core/GraphicsSettings.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/ModelLoader.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Texture/TextureLoader.h>
//Modify End
//Modify Begin:2026-08-19 by Hui
#include <Framework/Scene/SceneImporter.h>
//Modify End

//Modify Begin:2026-08-19 by Hui
#include <Automation/RaytracingDemoAutomation.h>
//Modify End

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <imgui.h>

#include <algorithm>
//Modify Begin:2026-08-28 by Hui
#include <array>
//Modify End
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

//Modify Begin:2026-08-19 by Hui
using RuntimeAutomationAction = RaytracingDemoAutomation::Action;
using RuntimeAutomationMatrixCase = RaytracingDemoAutomation::MatrixCase;
//Modify End

namespace
{
//Modify Begin:2026-08-26 by Hui
    FrameworkDeviceContext CreateFrameworkDeviceContext(
        Application& application,
        FrameFeatureServices frameFeatureServices)
    {
        FrameworkDeviceContextDesc desc;
        desc.DeviceContext = application.GetD3D12DeviceContext();
        desc.DirectQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        desc.ComputeQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        desc.CopyQueue = application.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
        desc.FrameFeatures = std::move(frameFeatureServices.Runtime);
        desc.FrameGeneration = std::move(frameFeatureServices.FrameGeneration);
        return FrameworkDeviceContext(std::move(desc));
    }

    std::string GetEnvironmentValue(const char* variableName)
    {
        char* value = nullptr;
        size_t valueLength = 0;
        _dupenv_s(&value, &valueLength, variableName);
        const std::string result = value != nullptr ? value : "";
        std::free(value);
        return result;
    }

    std::string GetExecutablePath()
    {
        std::vector<wchar_t> buffer(32768u, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0u || length >= buffer.size())
        {
            return {};
        }
        return std::filesystem::path(std::wstring(buffer.data(), length)).string();
    }

    uint32_t ComputeDescriptorArrayCapacity(const size_t resourceCount, const size_t resourceCapacity)
    {
        return static_cast<uint32_t>(std::max<size_t>(
            RayTracingSceneResourceLayout::MinDescriptorArrayCapacity,
            std::max(resourceCount, resourceCapacity)));
    }

    DLSSMode ParseDLSSMode(const char* value)
    {
        if (value == nullptr || std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0)
        {
            return DLSSMode::Disabled;
        }
        if (std::strcmp(value, "dlaa") == 0)
        {
            return DLSSMode::DLAA;
        }
        if (std::strcmp(value, "balanced") == 0)
        {
            return DLSSMode::Balanced;
        }
        if (std::strcmp(value, "performance") == 0)
        {
            return DLSSMode::Performance;
        }
        if (std::strcmp(value, "ultra-performance") == 0)
        {
            return DLSSMode::UltraPerformance;
        }
        return DLSSMode::Quality;
    }
//Modify End

//Modify Begin:2026-08-19 by Hui
    MaterialShadingModel ParseMaterialShadingModel(const char* value)
    {
        if (value != nullptr &&
            (std::strcmp(value, "stylized-comic") == 0 ||
                std::strcmp(value, "stylized") == 0))
        {
            return MaterialShadingModel::StylizedComic;
        }

        return MaterialShadingModel::Pbr;
    }
//Modify End

//Modify Begin:2026-08-19 by Hui
    bool TryGetEnvironmentBoolean(const char* variableName, bool& value)
    {
        char* environmentValue = nullptr;
        size_t environmentValueLength = 0;
        _dupenv_s(&environmentValue, &environmentValueLength, variableName);
        if (environmentValue == nullptr)
        {
            return false;
        }

        value = std::strcmp(environmentValue, "0") != 0;
        std::free(environmentValue);
        return true;
    }

    std::string Trim(std::string value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character)
        {
            return std::isspace(character) != 0;
        });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character)
        {
            return std::isspace(character) != 0;
        }).base();
        return first >= last ? std::string() : std::string(first, last);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool TryParseBoolean(const std::string& text, bool& value)
    {
        const std::string normalized = ToLower(Trim(text));
        if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes")
        {
            value = true;
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool TryParseLightingTechnique(
        const std::string& text,
        RaytracingDemoLightingTechnique& technique)
    {
        const std::string value = ToLower(Trim(text));
        if (value == "none")
        {
            technique = RaytracingDemoLightingTechnique::None;
            return true;
        }
        if (value == "pathtracing")
        {
            technique = RaytracingDemoLightingTechnique::PathTracing;
            return true;
        }
        if (value == "restirdi")
        {
            technique = RaytracingDemoLightingTechnique::ReSTIRDI;
            return true;
        }
        if (value == "restirgi")
        {
            technique = RaytracingDemoLightingTechnique::ReSTIRGI;
            return true;
        }
        return false;
    }

    bool TryGetEnvironmentLightingTechnique(
        const char* variableName,
        RaytracingDemoLightingTechnique& technique)
    {
        char* environmentValue = nullptr;
        size_t environmentValueLength = 0;
        _dupenv_s(&environmentValue, &environmentValueLength, variableName);
        if (environmentValue == nullptr)
        {
            return false;
        }

        const std::string value(environmentValue);
        std::free(environmentValue);
        return TryParseLightingTechnique(value, technique);
    }

    class StartupIni final
    {
    public:
        bool TryGetString(const char* section, const char* key, std::string& value) const
        {
            const auto iterator = m_Values.find(MakeKey(section, key));
            if (iterator == m_Values.end())
            {
                return false;
            }
            value = iterator->second;
            return true;
        }

        bool TryGetBoolean(const char* section, const char* key, bool& value) const
        {
            std::string text;
            return TryGetString(section, key, text) && TryParseBoolean(text, value);
        }

        bool TryGetInt(const char* section, const char* key, int& value) const
        {
            std::string text;
            if (!TryGetString(section, key, text))
            {
                return false;
            }

            char* parseEnd = nullptr;
            const long parsed = std::strtol(text.c_str(), &parseEnd, 10);
            if (parseEnd == text.c_str() || *parseEnd != '\0')
            {
                return false;
            }
            value = static_cast<int>(parsed);
            return true;
        }

        bool TryGetFloat(const char* section, const char* key, float& value) const
        {
            std::string text;
            if (!TryGetString(section, key, text))
            {
                return false;
            }

            char* parseEnd = nullptr;
            const float parsed = std::strtof(text.c_str(), &parseEnd);
            if (parseEnd == text.c_str() || *parseEnd != '\0' || !std::isfinite(parsed))
            {
                return false;
            }
            value = parsed;
            return true;
        }

        const std::string& GetStatus() const { return m_Status; }

        static StartupIni Load(const std::filesystem::path& path)
        {
            StartupIni ini;
            std::ifstream stream(path);
            if (!stream)
            {
                ini.m_Status = "Startup config not found; using built-in defaults.";
                return ini;
            }

            std::string section;
            std::string line;
            while (std::getline(stream, line))
            {
                const size_t comment = line.find_first_of(";#");
                if (comment != std::string::npos)
                {
                    line.erase(comment);
                }

                line = Trim(std::move(line));
                if (line.empty())
                {
                    continue;
                }
                if (line.front() == '[' && line.back() == ']')
                {
                    section = ToLower(Trim(line.substr(1, line.size() - 2)));
                    continue;
                }

                const size_t separator = line.find('=');
                if (separator == std::string::npos || section.empty())
                {
                    continue;
                }

                const std::string key = Trim(line.substr(0, separator));
                const std::string value = Trim(line.substr(separator + 1));
                if (!key.empty())
                {
                    ini.m_Values[MakeKey(section.c_str(), key.c_str())] = value;
                }
            }

            ini.m_Status = "Startup config loaded: " + path.string();
            return ini;
        }

    private:
        static std::string MakeKey(const char* section, const char* key)
        {
            return ToLower(std::string(section) + "." + key);
        }

        std::unordered_map<std::string, std::string> m_Values;
        std::string m_Status;
    };
//Modify End

//Modify Begin:2026-08-21 by Hui
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

        constexpr const char* DefaultCountryKitchenRelativePath =
            "Assets/Scenes/CountryKitchen/scene.xml";
        std::filesystem::path searchDirectory = std::filesystem::current_path();
        while (!searchDirectory.empty())
        {
            const std::filesystem::path defaultScene = searchDirectory / DefaultCountryKitchenRelativePath;
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

        throw std::runtime_error("Default CountryKitchen scene file does not exist. Set RAYTRACING_DEMO_SCENE to a .json, .unity, .fbx, or Mitsuba .xml file.");
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

//Modify Begin:2026-08-19 by Hui
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

//Modify Begin:2026-08-28 by Hui
RaytracingDemo::RaytracingDemo(
    Application& application,
    const std::wstring& name,
    const int width,
    const int height,
    GraphicsSettings graphicsSettings,
    FrameFeatureServices frameFeatureServices)
    : Base(application, name, width, height, graphicsSettings.VSync)
    , m_Width(width)
    , m_Height(height)
    , m_FrameworkDeviceContext(CreateFrameworkDeviceContext(application, std::move(frameFeatureServices)))
    , m_ShaderPipelineBootstrap(m_FrameworkDeviceContext)
    , m_DiagnosticsImageCapture(m_FrameworkDeviceContext, m_Diagnostics)
    , m_DLSS(m_FrameworkDeviceContext)
    , m_PathTracingPipelines(m_FrameworkDeviceContext)
    , m_ActivePixels(m_FrameworkDeviceContext)
    , m_DirectLightingReSTIRDIPass(
        m_FrameworkDeviceContext,
        {
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.RIS.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Temporal.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Spatial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRDI/ReSTIRDI.Shade.cs.hlsl",
            { { "RAYTRACING_DEMO_SOFT_SHADOWS", "1" } },
            "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
            { PipelineStaticSamplers::LinearWrap(1u) },
        })
    , m_IndirectLightingReSTIRGIPass(
        m_FrameworkDeviceContext,
        {
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Initial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Temporal.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Spatial.cs.hlsl",
            L"Demos/RaytracingDemo/shaders/ReSTIRGI/ReSTIRGI.Shade.cs.hlsl",
            { { "RAYTRACING_DEMO_SOFT_SHADOWS", "1" } },
            "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
            { PipelineStaticSamplers::LinearWrap(1u) },
        })
    , m_Bloom(m_FrameworkDeviceContext)
    , m_AutoExposure(m_FrameworkDeviceContext)
    , m_ProfilerDisplay(graphicsSettings.ProfilerDisplayRefreshIntervalSeconds)
    , m_SceneResources(m_FrameworkDeviceContext.GetD3D12DeviceContext())
    , m_Lights(m_FrameworkDeviceContext)
{
    const XMVECTOR cameraPos = XMVectorSet(0, 8, -35, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 18, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    GetSceneCamera().SetLookAt(cameraPos, cameraTarget, cameraUp);
    const XMVECTOR initialForward = XMVector3Normalize(cameraTarget - cameraPos);
    m_CameraController.Yaw = XMConvertToDegrees(std::atan2(XMVectorGetX(initialForward), XMVectorGetZ(initialForward)));
    m_CameraController.Pitch = XMConvertToDegrees(std::asin(std::clamp(XMVectorGetY(initialForward), -1.0f, 1.0f)));
    GetSceneCamera().SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

    m_Hdr10OutputRequested = graphicsSettings.Hdr10Output;
    m_DLSS.SetMode(DLSSMode::Quality);
    LoadStartupConfiguration();

    char* mode = nullptr;
    size_t modeLength = 0;
    _dupenv_s(&mode, &modeLength, "RAYTRACING_DEMO_MODE");
    if (mode != nullptr)
    {
        const std::string environmentMode = ToLower(mode);
        if (environmentMode == "shader-table" || environmentMode == "shader-table-dxr" || environmentMode == "dxr")
        {
            m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
        }
        else if (environmentMode == "inline" || environmentMode == "inline-ray-query")
        {
            m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
        }
    }
    std::free(mode);

    char* dispatchMode = nullptr;
    size_t dispatchModeLength = 0;
    _dupenv_s(&dispatchMode, &dispatchModeLength, "RAYTRACING_DEMO_RAY_TRACING_DISPATCH");
    if (dispatchMode != nullptr)
    {
        const std::string environmentDispatchMode = ToLower(dispatchMode);
        if (environmentDispatchMode == "compacted" || environmentDispatchMode == "compacted-indirect" || environmentDispatchMode == "indirect")
        {
            m_PathTracingDispatchMode = PathTracingDispatchMode::CompactedIndirect;
        }
        else if (environmentDispatchMode == "full" || environmentDispatchMode == "full-resolution")
        {
            m_PathTracingDispatchMode = PathTracingDispatchMode::FullResolution;
        }
    }
    std::free(dispatchMode);
    char* bounceCount = nullptr;
    size_t bounceCountLength = 0;
    _dupenv_s(&bounceCount, &bounceCountLength, "RAYTRACING_DEMO_BOUNCES");
    if (bounceCount != nullptr)
    {
        char* parseEnd = nullptr;
        const long parsedBounces = std::strtol(bounceCount, &parseEnd, 10);
        if (parseEnd != bounceCount && *parseEnd == '\0')
        {
            m_MaxBounces = std::clamp(static_cast<int>(parsedBounces), 1, 5);
        }
    }
    std::free(bounceCount);

    char* materialShadingModel = nullptr;
    size_t materialShadingModelLength = 0;
    _dupenv_s(&materialShadingModel, &materialShadingModelLength, "RAYTRACING_DEMO_MATERIAL_SHADING");
    if (materialShadingModel != nullptr)
    {
        m_MaterialShadingModel = ParseMaterialShadingModel(materialShadingModel);
    }
    std::free(materialShadingModel);

    // Environment variables override startup configuration values.
    TryGetEnvironmentLightingTechnique("RAYTRACING_DEMO_DIRECT_LIGHTING", m_DirectLightingTechnique);
    TryGetEnvironmentLightingTechnique("RAYTRACING_DEMO_INDIRECT_LIGHTING", m_IndirectLightingTechnique);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_ACCUMULATION", m_AccumulationEnabled);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_HDR10", m_Hdr10OutputRequested);

    ReSTIRGISettings restirGISettings = m_IndirectLightingReSTIRGI.GetSettings();
    bool restirGISettingsOverridden = false;
    restirGISettingsOverridden |= TryGetEnvironmentBoolean(
        "RAYTRACING_DEMO_RESTIRGI_TEMPORAL",
        restirGISettings.EnableTemporalResampling);
    restirGISettingsOverridden |= TryGetEnvironmentBoolean(
        "RAYTRACING_DEMO_RESTIRGI_SPATIAL",
        restirGISettings.EnableSpatialResampling);
    if (restirGISettingsOverridden)
    {
        m_IndirectLightingReSTIRGI.SetSettings(restirGISettings);
    }

    char* softShadows = nullptr;
    size_t softShadowsLength = 0;
    _dupenv_s(&softShadows, &softShadowsLength, "RAYTRACING_DEMO_SOFT_SHADOWS");
    if (softShadows != nullptr)
    {
        m_SoftShadowsEnabled = std::strcmp(softShadows, "0") != 0;
    }
    std::free(softShadows);

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

    char* dlssMode = nullptr;
    size_t dlssModeLength = 0;
    _dupenv_s(&dlssMode, &dlssModeLength, "RAYTRACING_DEMO_DLSS");
    if (dlssMode != nullptr)
    {
        m_DLSS.SetMode(ParseDLSSMode(dlssMode));
    }
    std::free(dlssMode);

    char* rayReconstruction = nullptr;
    size_t rayReconstructionLength = 0;
    _dupenv_s(&rayReconstruction, &rayReconstructionLength, "RAYTRACING_DEMO_DLSS_RR");
    if (rayReconstruction != nullptr)
    {
        m_DLSS.SetRayReconstructionEnabled(std::strcmp(rayReconstruction, "0") != 0);
    }
    std::free(rayReconstruction);

    char* frameGeneration = nullptr;
    size_t frameGenerationLength = 0;
    _dupenv_s(&frameGeneration, &frameGenerationLength, "RAYTRACING_DEMO_DLSS_FRAME_GENERATION");
    if (frameGeneration != nullptr)
    {
        m_DLSS.SetFrameGenerationEnabled(std::strcmp(frameGeneration, "0") != 0);
    }
    std::free(frameGeneration);

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

    char* bloom = nullptr;
    size_t bloomLength = 0;
    _dupenv_s(&bloom, &bloomLength, "RAYTRACING_DEMO_BLOOM");
    if (bloom != nullptr)
    {
        m_Bloom.SetEnabled(std::strcmp(bloom, "0") != 0);
    }
    std::free(bloom);

//Modify End

//Modify Begin:2026-08-19 by Hui
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

    char* meshletBackend = nullptr;
    size_t meshletBackendLength = 0;
    _dupenv_s(&meshletBackend, &meshletBackendLength, "RAYTRACING_DEMO_MESHLET_BACKEND");
    if (meshletBackend != nullptr)
    {
        m_UseTaskShaderMeshlets = std::strcmp(meshletBackend, "indirect") != 0;
    }
    std::free(meshletBackend);
//Modify End

}

//Modify Begin:2026-08-26 by Hui
void RaytracingDemo::LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath)
{
    const auto recordStartupCheckpoint = [this](const char* checkpoint)
    {
        if (!m_Diagnostics.IsEnabled())
        {
            return;
        }

        const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_StartupLoadStartTime).count();
        m_Diagnostics.Record(
            "application.startup",
            "checkpoint",
            DiagnosticTelemetrySeverity::Info,
            {
                { "checkpoint", std::string(checkpoint) },
                { "elapsed_milliseconds", elapsedMilliseconds },
            });
        m_Diagnostics.Flush();
    };

    SceneImportOptions importOptions;
    importOptions.GenerateFallbackCamera = true;
    SceneImportResult sceneImport;
    if (m_StartupSceneImport.valid())
    {
        if (m_StartupSceneImport.wait_for(std::chrono::seconds::zero()) != std::future_status::ready)
        {
            throw std::logic_error("Startup scene import is not complete.");
        }
        sceneImport = m_StartupSceneImport.get();
    }
    else
    {
        sceneImport = SceneImporter::ImportFromFile(scenePath, importOptions);
    }
    m_Scene = sceneImport.SceneData;
    recordStartupCheckpoint("scene_imported");
    std::filesystem::path runtimeStatePath = scenePath;
    runtimeStatePath += ".runtime.json";
    if (std::filesystem::exists(runtimeStatePath))
    {
        SceneImporter::ApplyJsonRuntimeState(runtimeStatePath, m_Scene);
    }
    const SceneCamera& sceneCamera = m_Scene.GetCamera();

    if (!m_SceneResources.LoadScene(commandList, m_Scene, m_SceneRuntime.AreStressTestSpheresEnabled()))
    {
        throw std::runtime_error("Scene has no supported renderable objects.");
    }
    recordStartupCheckpoint("scene_resources_loaded");

    SceneSkybox sceneSkybox = m_Scene.GetSkybox();
    std::filesystem::path skyboxTexturePath = sceneSkybox.Texture.AssetPath;
    const bool hasSceneSkyboxTexture =
        !skyboxTexturePath.empty() && std::filesystem::exists(skyboxTexturePath);
    const bool hasExplicitSkyRadiance =
        sceneSkybox.AmbientColorAndIntensity.x > 0.0f ||
        sceneSkybox.AmbientColorAndIntensity.y > 0.0f ||
        sceneSkybox.AmbientColorAndIntensity.z > 0.0f;
    if (!hasExplicitSkyRadiance)
    {
        if (hasSceneSkyboxTexture)
        {
            sceneSkybox.AmbientColorAndIntensity = {
                1.0f,
                1.0f,
                1.0f,
                std::max(0.001f, sceneSkybox.AmbientColorAndIntensity.w)
            };
        }
        else
        {
            // Mitsuba packages often provide area emitters but no environment map. Keep their data intact
            // while giving the Demo a neutral daylight fallback instead of black environment radiance.
            sceneSkybox.AmbientColorAndIntensity = { 0.78f, 0.84f, 1.0f, 0.75f };
        }
    }
    m_Scene.SetSkybox(sceneSkybox);

    m_Lights.CreateFromScene(m_Scene);
    bool directionalLightsEnabled = m_Lights.AreDirectionalLightsEnabled();
    bool pointLightsEnabled = m_Lights.ArePointLightsEnabled();
    bool areaLightsEnabled = m_Lights.AreAreaLightsEnabled();
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_DIRECTIONAL_LIGHTS", directionalLightsEnabled);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_POINT_LIGHTS", pointLightsEnabled);
    TryGetEnvironmentBoolean("RAYTRACING_DEMO_AREA_LIGHTS", areaLightsEnabled);
    m_Lights.SetLightGroupSettings(directionalLightsEnabled, pointLightsEnabled, areaLightsEnabled);

    bool skyLightEnabled = true;
    if (TryGetEnvironmentBoolean("RAYTRACING_DEMO_SKY_LIGHT", skyLightEnabled) && !skyLightEnabled)
    {
        SkyLightData skyLight = m_Lights.GetSkyLight();
        skyLight.ColorAndIntensity.w = 0.0f;
        m_Lights.SetSkyLight(skyLight);
    }
    m_Lights.SetEmissiveMeshSurfaceEmitters(m_SceneResources.CollectEmissiveMeshSurfaceEmitters());
    recordStartupCheckpoint("scene_lighting_initialized");
    m_SkyboxEnabled = true;
    m_HasSceneCamera = m_Scene.HasCamera();

    ApplySceneCamera(GetSceneCamera(), sceneCamera, m_Width, m_Height);
    const XMFLOAT3 forward = RotateCameraVector(GetSceneCamera().GetRotation(), { 0.0f, 0.0f, 1.0f });
    CalculateCameraControllerFromLookDirection(
        XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
        m_CameraController.Yaw,
        m_CameraController.Pitch);
    m_CameraFov = sceneCamera.FieldOfView;
    m_CameraNearClipPlane = sceneCamera.NearClipPlane;
    m_CameraFarClipPlane = sceneCamera.FarClipPlane;
    XMStoreFloat3(&m_InitialSceneCameraTranslation, GetSceneCamera().GetTranslation());
    XMStoreFloat4(&m_InitialSceneCameraRotation, GetSceneCamera().GetRotation());
    m_InitialSceneCameraYaw = m_CameraController.Yaw;
    m_InitialSceneCameraPitch = m_CameraController.Pitch;
    m_HasInitialSceneCameraState = true;

    m_SkyboxTexture.reset();
    m_EnvironmentFallbackCubemap.reset();
    if (m_SkyboxEnabled && hasSceneSkyboxTexture)
    {
        m_SkyboxTexture = std::make_shared<Texture>(
            TextureUsageType::Other,
            L"",
            commandList.GetDeviceContext());
        TextureLoader(commandList.GetDeviceContext()).Load(
            commandList, *m_SkyboxTexture, skyboxTexturePath, TextureUsageType::Albedo);
    }
    else
    {
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clearValue.Color[0] = 1.0f;
        clearValue.Color[1] = 1.0f;
        clearValue.Color[2] = 1.0f;
        clearValue.Color[3] = 1.0f;
        m_EnvironmentFallbackCubemap = std::make_unique<Cubemap>(
            1u,
            XMVectorZero(),
            commandList,
            clearValue.Format,
            DXGI_FORMAT_D32_FLOAT,
            clearValue);
    }
    recordStartupCheckpoint("scene_skybox_loaded");
}
//Modify End

//Modify Begin:2026-08-26 by Hui
const std::shared_ptr<Texture>& RaytracingDemo::GetRayTracingEnvironmentTexture() const
{
    if (m_SkyboxTexture != nullptr)
    {
        return m_SkyboxTexture;
    }

    if (m_EnvironmentFallbackCubemap == nullptr)
    {
        throw std::logic_error("Ray-tracing environment texture has not been initialized.");
    }

    return m_EnvironmentFallbackCubemap->GetFallbackTexture();
}
//Modify End

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

void RaytracingDemo::LoadStartupConfiguration()
{
    const std::filesystem::path configurationPath = std::filesystem::current_path() / "Config" / "RaytracingDemo.ini";
    const StartupIni configuration = StartupIni::Load(configurationPath);
    m_StartupConfigurationStatus = configuration.GetStatus();

    auto applyBackend = [&](const char* section, const char* key)
    {
        std::string value;
        if (!configuration.TryGetString(section, key, value))
        {
            return;
        }

        value = ToLower(value);
        if (value == "shader-table" || value == "shader-table-dxr" || value == "dxr")
        {
            m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
        }
        else if (value == "inline" || value == "inline-ray-query")
        {
            m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
        }
    };

    auto applyLightingTechnique = [&](const char* section, const char* key, RaytracingDemoLightingTechnique& technique)
    {
        std::string value;
        RaytracingDemoLightingTechnique parsedTechnique = technique;
        if (configuration.TryGetString(section, key, value) && TryParseLightingTechnique(value, parsedTechnique))
        {
            technique = parsedTechnique;
        }
    };

    auto applyDispatchMode = [&](const char* section, const char* key)
    {
        std::string value;
        if (!configuration.TryGetString(section, key, value))
        {
            return;
        }

        value = ToLower(value);
        if (value == "compacted" || value == "compacted-indirect" || value == "indirect")
        {
            m_PathTracingDispatchMode = PathTracingDispatchMode::CompactedIndirect;
        }
        else if (value == "full" || value == "full-resolution")
        {
            m_PathTracingDispatchMode = PathTracingDispatchMode::FullResolution;
        }
    };
    applyBackend("Renderer", "PathTracingBackend");
    applyDispatchMode("Renderer", "PathTracingDispatch");
    applyLightingTechnique("Renderer", "DirectLighting", m_DirectLightingTechnique);
    applyLightingTechnique("Renderer", "IndirectLighting", m_IndirectLightingTechnique);

    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    if (configuration.TryGetInt("Renderer", "MaxBounces", intValue))
    {
        m_MaxBounces = std::clamp(intValue, 1, 5);
    }
    if (configuration.TryGetBoolean("Renderer", "Accumulation", boolValue))
    {
        m_AccumulationEnabled = boolValue;
    }
    if (configuration.TryGetBoolean("Renderer", "SoftShadows", boolValue))
    {
        m_SoftShadowsEnabled = boolValue;
    }
    if (configuration.TryGetBoolean("Renderer", "AsyncCompute", boolValue))
    {
        m_AsyncComputeEnabled = boolValue;
    }
    if (configuration.TryGetBoolean("Renderer", "ParallelDirectCommandRecording", boolValue))
    {
        m_ParallelDirectCommandRecordingEnabled = boolValue;
    }
    if (configuration.TryGetBoolean("Renderer", "MeshletGBuffer", boolValue))
    {
        m_UseMeshletGBuffer = boolValue;
    }
    if (configuration.TryGetBoolean("Renderer", "MeshletDebug", boolValue))
    {
        m_DebugMeshletClusters = boolValue;
        if (boolValue)
        {
            m_UseMeshletGBuffer = true;
        }
    }
    std::string stringValue;
    if (configuration.TryGetString("Renderer", "MeshletBackend", stringValue))
    {
        stringValue = ToLower(stringValue);
        if (stringValue == "task" || stringValue == "task-shader")
        {
            m_UseTaskShaderMeshlets = true;
        }
        else if (stringValue == "indirect" || stringValue == "compute-indirect")
        {
            m_UseTaskShaderMeshlets = false;
        }
    }
    if (configuration.TryGetString("Renderer", "MaterialShading", stringValue))
    {
        stringValue = ToLower(stringValue);
        m_MaterialShadingModel = ParseMaterialShadingModel(stringValue.c_str());
    }
//Modify Begin:2026-08-28 by Hui
    if (configuration.TryGetBoolean("Display", "HDR10", boolValue))
    {
        m_Hdr10OutputRequested = boolValue;
    }
//Modify End

    if (configuration.TryGetString("Denoiser", "Algorithm", stringValue))
    {
        stringValue = ToLower(stringValue);
        m_Denoisers.SetAlgorithmFromName(stringValue.c_str());
    }
    if (configuration.TryGetInt("Denoiser", "OIDNStaticSpp", intValue))
    {
        m_Denoisers.SetOIDNStaticSpp(static_cast<uint32_t>(std::max(intValue, 1)));
    }

    if (configuration.TryGetString("DLSS", "Mode", stringValue))
    {
        stringValue = ToLower(stringValue);
        m_DLSS.SetMode(ParseDLSSMode(stringValue.c_str()));
    }
    if (configuration.TryGetBoolean("DLSS", "RayReconstruction", boolValue))
    {
        m_DLSS.SetRayReconstructionEnabled(boolValue);
    }
    if (configuration.TryGetBoolean("DLSS", "FrameGeneration", boolValue))
    {
        m_DLSS.SetFrameGenerationEnabled(boolValue);
    }

    ReSTIRDISettings restirDISettings = m_DirectLightingReSTIRDI.GetSettings();
    if (configuration.TryGetInt("ReSTIRDI", "CandidateCount", intValue))
    {
        restirDISettings.CandidateCount = static_cast<uint32_t>(std::clamp(intValue, 1, 32));
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "InitialVisibility", boolValue))
    {
        restirDISettings.EnableInitialVisibility = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "TemporalResampling", boolValue))
    {
        restirDISettings.EnableTemporalResampling = boolValue;
    }
    if (configuration.TryGetString("ReSTIRDI", "TemporalBiasCorrection", stringValue))
    {
        stringValue = ToLower(stringValue);
        if (stringValue == "off")
        {
            restirDISettings.TemporalBiasCorrection = ReSTIRDITemporalBiasCorrectionMode::Off;
        }
        else if (stringValue == "basic")
        {
            restirDISettings.TemporalBiasCorrection = ReSTIRDITemporalBiasCorrectionMode::Basic;
        }
        else if (stringValue == "raytraced" || stringValue == "ray-traced")
        {
            restirDISettings.TemporalBiasCorrection = ReSTIRDITemporalBiasCorrectionMode::RayTraced;
        }
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "TemporalVisibilityShortcut", boolValue))
    {
        restirDISettings.EnableTemporalVisibilityShortcut = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "TemporalPermutationSampling", boolValue))
    {
        restirDISettings.EnableTemporalPermutationSampling = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "BoilingFilter", boolValue))
    {
        restirDISettings.EnableBoilingFilter = boolValue;
    }
    if (configuration.TryGetFloat("ReSTIRDI", "BoilingFilterStrength", floatValue))
    {
        restirDISettings.BoilingFilterStrength = std::max(0.0f, floatValue);
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "SpatialResampling", boolValue))
    {
        restirDISettings.EnableSpatialResampling = boolValue;
    }
    if (configuration.TryGetString("ReSTIRDI", "SpatialBiasCorrection", stringValue))
    {
        stringValue = ToLower(stringValue);
        if (stringValue == "off")
        {
            restirDISettings.SpatialBiasCorrection = ReSTIRDISpatialBiasCorrectionMode::Off;
        }
        else if (stringValue == "basic")
        {
            restirDISettings.SpatialBiasCorrection = ReSTIRDISpatialBiasCorrectionMode::Basic;
        }
        else if (stringValue == "pairwise")
        {
            restirDISettings.SpatialBiasCorrection = ReSTIRDISpatialBiasCorrectionMode::Pairwise;
        }
        else if (stringValue == "raytraced" || stringValue == "ray-traced")
        {
            restirDISettings.SpatialBiasCorrection = ReSTIRDISpatialBiasCorrectionMode::RayTraced;
        }
    }
    if (configuration.TryGetInt("ReSTIRDI", "SpatialNeighborCount", intValue))
    {
        restirDISettings.SpatialNeighborCount = static_cast<uint32_t>(std::clamp(intValue, 1, 32));
    }
    if (configuration.TryGetInt("ReSTIRDI", "SpatialDisocclusionBoostSampleCount", intValue))
    {
        restirDISettings.SpatialDisocclusionBoostSampleCount = static_cast<uint32_t>(std::clamp(intValue, 1, 32));
    }
    if (configuration.TryGetInt("ReSTIRDI", "SpatialTargetHistoryLength", intValue))
    {
        restirDISettings.SpatialTargetHistoryLength = static_cast<uint32_t>(std::clamp(intValue, 0, 64));
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "SpatialMaterialSimilarityTest", boolValue))
    {
        restirDISettings.EnableSpatialMaterialSimilarityTest = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "FinalVisibility", boolValue))
    {
        restirDISettings.EnableFinalVisibility = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "ReuseFinalVisibility", boolValue))
    {
        restirDISettings.ReuseFinalVisibility = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRDI", "DiscardInvisibleFinalSamples", boolValue))
    {
        restirDISettings.DiscardInvisibleFinalSamples = boolValue;
    }
    if (configuration.TryGetInt("ReSTIRDI", "FinalVisibilityMaxAge", intValue))
    {
        restirDISettings.FinalVisibilityMaxAge = static_cast<uint32_t>(std::clamp(intValue, 0, 16));
    }
    if (configuration.TryGetFloat("ReSTIRDI", "FinalVisibilityMaxDistance", floatValue))
    {
        restirDISettings.FinalVisibilityMaxDistance = std::max(0.0f, floatValue);
    }
    m_DirectLightingReSTIRDI.SetSettings(restirDISettings);

    ReSTIRGISettings restirGISettings = m_IndirectLightingReSTIRGI.GetSettings();
    if (configuration.TryGetInt("ReSTIRGI", "InitialCandidateCount", intValue))
    {
        restirGISettings.InitialCandidateCount = static_cast<uint32_t>(std::clamp(intValue, 1, 32));
    }
    if (configuration.TryGetBoolean("ReSTIRGI", "TemporalResampling", boolValue))
    {
        restirGISettings.EnableTemporalResampling = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRGI", "SpatialResampling", boolValue))
    {
        restirGISettings.EnableSpatialResampling = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRGI", "TemporalJacobian", boolValue))
    {
        restirGISettings.EnableTemporalJacobian = boolValue;
    }
    if (configuration.TryGetBoolean("ReSTIRGI", "RayTracedSpatialBiasCorrection", boolValue))
    {
        restirGISettings.EnableRayTracedSpatialBiasCorrection = boolValue;
    }
    if (configuration.TryGetInt("ReSTIRGI", "SpatialNeighborCount", intValue))
    {
        restirGISettings.SpatialNeighborCount = static_cast<uint32_t>(std::clamp(intValue, 1, 16));
    }
    m_IndirectLightingReSTIRGI.SetSettings(restirGISettings);

    if (configuration.TryGetBoolean("Bloom", "Enabled", boolValue))
    {
        m_Bloom.SetEnabled(boolValue);
    }
    if (configuration.TryGetString("Bloom", "Backend", stringValue))
    {
        stringValue = ToLower(stringValue);
        if (stringValue == "builtin" || stringValue == "framework" || stringValue == "framework-raster" || stringValue == "raster")
        {
            m_Bloom.SetBackend(BloomController::Backend::FrameworkRaster);
        }
        else if (stringValue == "cuda")
        {
            m_Bloom.SetBackend(BloomController::Backend::Cuda);
        }
    }
    if (configuration.TryGetString("Bloom", "CudaMethod", stringValue))
    {
        stringValue = ToLower(stringValue);
        if (stringValue == "boxfilter" || stringValue == "box-filter" || stringValue == "box-filter-approximation")
        {
            m_Bloom.SetCudaMethod(BloomController::CudaMethod::BoxFilterApproximation);
        }
        else if (stringValue == "boxfilter-original" || stringValue == "box-filter-original" || stringValue == "box-filter-original-paper")
        {
            m_Bloom.SetCudaMethod(BloomController::CudaMethod::BoxFilterOriginalPaper);
        }
        else if (stringValue == "classic" || stringValue == "classic-pyramid")
        {
            m_Bloom.SetCudaMethod(BloomController::CudaMethod::ClassicPyramid);
        }
    }
    if (configuration.TryGetFloat("Bloom", "Threshold", floatValue))
    {
        m_Bloom.SetThreshold(std::max(0.0f, floatValue));
    }
    if (configuration.TryGetFloat("Bloom", "SoftThreshold", floatValue))
    {
        m_Bloom.SetSoftThreshold(std::max(0.0f, floatValue));
    }
    if (configuration.TryGetFloat("Bloom", "Intensity", floatValue))
    {
        m_Bloom.SetIntensity(std::max(0.0f, floatValue));
    }
    if (configuration.TryGetInt("Bloom", "PyramidLevels", intValue))
    {
        m_Bloom.SetPyramidLevels(std::clamp(
            intValue,
            1,
            static_cast<int>(BloomController::ComputeMaxPyramidLevels(
                static_cast<uint32_t>((std::max)(m_Width, 1)),
                static_cast<uint32_t>((std::max)(m_Height, 1))))));
    }
    if (configuration.TryGetFloat("Bloom", "BoxFilterSigma", floatValue))
    {
        m_Bloom.SetBoxFilterSigma(std::max(0.001f, floatValue));
    }
    if (configuration.TryGetBoolean("Bloom", "SharedMemoryDownsampling", boolValue))
    {
        m_Bloom.SetUseSharedMemoryDownsampling(boolValue);
    }
    if (configuration.TryGetInt("Bloom", "ThreadBlockSize", intValue) && (intValue == 8 || intValue == 16))
    {
        m_Bloom.SetThreadBlockSize(intValue == 8
            ? BloomController::ThreadBlockSize::Size8x8
            : BloomController::ThreadBlockSize::Size16x16);
    }

    if (configuration.TryGetBoolean("Debug", "GpuTiming", boolValue))
    {
        m_GpuTimingEnabled = boolValue;
    }
    if (configuration.TryGetFloat("Debug", "ProfilerDisplayRefreshIntervalSeconds", floatValue))
    {
        SetProfilerDisplayRefreshIntervalSeconds(floatValue);
    }
}

void RaytracingDemo::InitializeDiagnostics()
{
    const std::string automationModeValue = GetEnvironmentValue("RAYTRACING_DEMO_AUTOTEST");
    const bool automationEnabled = !automationModeValue.empty() &&
        automationModeValue != "0" && automationModeValue != "off";

    if (!m_Diagnostics.BeginFromEnvironment("RaytracingDemo", automationEnabled))
    {
        if (automationEnabled)
        {
            throw std::runtime_error(
                "Runtime automation requires Diagnostics: " + m_Diagnostics.GetLastError());
        }
        return;
    }
    m_Diagnostics.AddMetadata("diagnostics_schema", "1");
    m_Diagnostics.AddMetadata("automation_mode", automationEnabled ? automationModeValue : "off");
    m_Diagnostics.AddMetadata("scene_path", GetScenePath().string());
    m_Diagnostics.AddMetadata("initial_width", std::to_string(m_Width));
    m_Diagnostics.AddMetadata("initial_height", std::to_string(m_Height));
    m_Diagnostics.AddMetadata("executable_path", GetExecutablePath());
    m_Diagnostics.AddMetadata("command_line", GetCommandLineA() != nullptr ? GetCommandLineA() : "");
//Modify Begin:2026-08-28 by Hui
    const Hdr10OutputCapabilities& hdr10Capabilities = PWindow->GetHdr10OutputCapabilities();
    m_Diagnostics.AddMetadata("presentation.hdr10_active", m_Hdr10OutputEnabled ? "true" : "false");
    m_Diagnostics.AddMetadata("presentation.hdr10_supported", hdr10Capabilities.IsSupported ? "true" : "false");
    m_Diagnostics.RecordAssertion(
        "presentation_hdr10_output",
        FrameworkDiagnostics::AssertionResult::Passed,
        m_Hdr10PresentationStatus,
        {
            { "requested", static_cast<uint64_t>(m_Hdr10OutputRequested) },
            { "active", static_cast<uint64_t>(m_Hdr10OutputEnabled) },
            { "supported", static_cast<uint64_t>(hdr10Capabilities.IsSupported) },
            { "peak_nits", static_cast<double>(m_Hdr10PeakNits) },
        });
//Modify End
//Modify Begin:2026-08-28 by Hui
    constexpr const char* reproductionEnvironment[] = {
        "RAYTRACING_DEMO_AUTOTEST",
        "RAYTRACING_DEMO_AUTOTEST_START_CASE",
        "RAYTRACING_DEMO_AUTOTEST_MAX_CASES",
        "RAYTRACING_DEMO_AUTOTEST_STEP_MS",
        "RAYTRACING_DEMO_AUTOTEST_TIMEOUT_SECONDS",
        "RAYTRACING_DEMO_SCENE",
        "RAYTRACING_DEMO_UNITY_SCENE",
        "RAYTRACING_DEMO_MODE",
        "RAYTRACING_DEMO_RAY_TRACING_DISPATCH",
        "RAYTRACING_DEMO_DIRECT_LIGHTING",
        "RAYTRACING_DEMO_INDIRECT_LIGHTING",
        "RAYTRACING_DEMO_BOUNCES",
        "RAYTRACING_DEMO_MATERIAL_SHADING",
        "RAYTRACING_DEMO_SOFT_SHADOWS",
        "RAYTRACING_DEMO_NRD",
        "RAYTRACING_DEMO_DENOISER",
        "RAYTRACING_DEMO_DLSS",
        "RAYTRACING_DEMO_DLSS_RR",
        "RAYTRACING_DEMO_DLSS_FRAME_GENERATION",
        "RAYTRACING_DEMO_HDR10",
        "RAYTRACING_DEMO_ASYNC_COMPUTE",
        "RAYTRACING_DEMO_PARALLEL_DIRECT_RECORDING",
        "RAYTRACING_DEMO_BLOOM",
        "RAYTRACING_DEMO_MESHLET_GBUFFER",
        "RAYTRACING_DEMO_MESHLET_DEBUG",
        "RAYTRACING_DEMO_MESHLET_BACKEND",
    };
//Modify End
    for (const char* variableName : reproductionEnvironment)
    {
        const std::string value = GetEnvironmentValue(variableName);
        if (!value.empty())
        {
            m_Diagnostics.AddMetadata(std::string("env.") + variableName, value);
        }
    }
    GetApplication().SetDiagnosticTelemetrySink(&m_Diagnostics);
    m_Diagnostics.Record("application.lifecycle", "load_content_begin");
}

//Modify Begin:2026-08-28 by Hui
void RaytracingDemo::RecordDiagnosticsFailure(std::string stage, const std::exception& exception)
{
    if (!m_Diagnostics.IsEnabled())
    {
        return;
    }
    m_Diagnostics.AttachDeviceRemovalDred(*m_FrameworkDeviceContext.GetDevice().Get(), stage);
    m_Diagnostics.RecordAssertion(
        "runtime." + stage,
        FrameworkDiagnostics::AssertionResult::Failed,
        exception.what(),
        { { "stage", stage } });
    m_Diagnostics.Finalize(FrameworkDiagnostics::SessionStatus::Failed, exception.what());
}
//Modify End

void RaytracingDemo::InitializeRuntimeAutomation()
{
    m_RuntimeAutomation.Initialize(
        RaytracingDemoAutomation::CreateTestSuites(),
        m_Diagnostics.IsEnabled() ? &m_Diagnostics : nullptr,
        [this](const uint32_t action, const uint32_t value)
        {
            ApplyRuntimeAutomationAction(action, value);
        },
        [this](const int exitCode)
        {
            GetApplication().Quit(exitCode);
        });
}

void RaytracingDemo::UpdateRuntimeAutomation(const double totalTime)
{
    m_RuntimeAutomation.Update(m_FrameIndex, totalTime);
}

void RaytracingDemo::ApplyRuntimeAutomationMatrixCase(const uint32_t caseIndex)
{
    const auto& matrixCases = RaytracingDemoAutomation::GetMatrixCases();
    if (caseIndex >= matrixCases.size())
    {
        throw std::out_of_range("Runtime automation matrix case index is out of range.");
    }

    const RuntimeAutomationMatrixCase& testCase = matrixCases[caseIndex];
    m_PathTracingBackend = testCase.Backend;
    m_DirectLightingTechnique = testCase.DirectLighting;
    m_IndirectLightingTechnique = testCase.IndirectLighting;
    m_AsyncComputeEnabled = testCase.Backend == PathTracingBackend::InlineRayQuery && testCase.AsyncCompute;
    m_ParallelDirectCommandRecordingEnabled = testCase.ParallelDirectCommandRecording;
    m_UseMeshletGBuffer = testCase.UseMeshletGBuffer;
    m_UseTaskShaderMeshlets = testCase.UseTaskShaderMeshlets;
    m_SoftShadowsEnabled = testCase.SoftShadows;
    m_SceneRuntime.SetStressTestSpheresEnabled(testCase.StressSpheres);
    m_SkyboxEnabled = testCase.Skybox;
    m_AccumulationEnabled = testCase.Accumulation;
    m_DLSS.SetMode(testCase.DlssMode);
    m_MaterialShadingModel = testCase.ShadingModel;
    SetMaxBounces(testCase.MaxBounces);
    m_DebugMeshletClusters = false;
    m_DebugLightingTextureTarget = 0;
    m_DebugTextureTarget = 0;
    m_DebugSerializeAsyncCompute = false;
    EnsureRayTracingPipelines();
    ResetAccumulation();
}

bool RaytracingDemo::ApplyTopologyRuntimeAutomationAction(
    const uint32_t actionValue,
    const uint32_t value)
{
    const bool enabled = value != 0u;
    const auto action = static_cast<RuntimeAutomationAction>(actionValue);
    switch (action)
    {
    case RuntimeAutomationAction::StressSpheres:
        m_SceneRuntime.SetStressTestSpheresEnabled(enabled);
        return true;
    case RuntimeAutomationAction::MeshletGBuffer:
        m_UseMeshletGBuffer = enabled;
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::MeshletTaskShader:
        m_UseTaskShaderMeshlets = enabled;
        m_UseMeshletGBuffer = true;
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::PathTracingBackend:
        m_PathTracingBackend = static_cast<PathTracingBackend>(value);
        if (m_PathTracingBackend != PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = false;
        }
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::DirectLighting:
        m_DirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::IndirectLighting:
        m_IndirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::CopyQueueValidation:
        m_CopyQueueValidationEnabled = enabled;
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::DynamicRayTracingUpdate:
        m_SceneResources.SetDynamicRayTracingUpdatesEnabled(enabled);
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::Denoiser:
        if (value > static_cast<uint32_t>(DenoiserController::Algorithm::OIDN))
        {
            throw std::out_of_range("Runtime automation denoiser selection is out of range.");
        }
        m_Denoisers.SetAlgorithm(static_cast<DenoiserController::Algorithm>(value));
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::DLSS:
        m_DLSS.SetMode(static_cast<DLSSMode>(value));
        ResetAccumulation();
        return true;
    case RuntimeAutomationAction::Skybox:
        m_SkyboxEnabled = enabled;
        ResetAccumulation();
        return true;
    default:
        return false;
    }
}

void RaytracingDemo::ApplyRuntimeAutomationAction(const uint32_t actionValue, const uint32_t value)
{
    const bool enabled = value != 0u;
    const auto action = static_cast<RuntimeAutomationAction>(actionValue);
    if (ApplyTopologyRuntimeAutomationAction(actionValue, value))
    {
        return;
    }
    switch (action)
    {
    case RuntimeAutomationAction::SoftShadows:
        m_SoftShadowsEnabled = enabled;
        EnsureRayTracingPipelines();
        BindRayTracingShaderResources();
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::MaxBounces:
        SetMaxBounces(static_cast<int>(value));
        break;
    case RuntimeAutomationAction::Wait:
        break;
    case RuntimeAutomationAction::VerifyActiveRayTracedPixelCount:
    {
        const auto failActivePixelAssertion = [this](std::string message)
        {
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordAssertion(
                    "active_pixel_dispatch",
                    FrameworkDiagnostics::AssertionResult::Failed,
                    message);
            }
            throw std::runtime_error(std::move(message));
        };
        if (m_PathTracingDispatchMode != PathTracingDispatchMode::CompactedIndirect)
        {
            m_RuntimeAutomation.AppendDiagnosticLog("Active ray-traced pixel verification skipped: full-resolution dispatch.");
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordAssertion(
                    "active_pixel_dispatch",
                    FrameworkDiagnostics::AssertionResult::Unknown,
                    "Verification is not applicable to full-resolution dispatch.");
            }
            break;
        }

        const ActivePixelReadbackStatus readbackStatus = m_ActivePixels.GetCountReadbackStatus();
        if (readbackStatus == ActivePixelReadbackStatus::NotQueued)
        {
            failActivePixelAssertion("Compacted ray-traced pixel dispatch readback was not queued.");
        }
        if (readbackStatus == ActivePixelReadbackStatus::NotCompleted)
        {
            failActivePixelAssertion("Compacted ray-traced pixel dispatch readback did not complete.");
        }

        const std::optional<ActivePixelDispatchDiagnostics> diagnostics = m_ActivePixels.GetLatestDiagnostics();
        if (!diagnostics.has_value())
        {
            failActivePixelAssertion("Completed compacted ray-traced pixel dispatch readback has no diagnostics.");
        }
        if (!diagnostics->HasConsistentDispatchArguments())
        {
            failActivePixelAssertion(
                "Compacted ray-traced pixel dispatch arguments do not match the active-pixel count: count=" +
                std::to_string(diagnostics->ActivePixelCount) +
                ", dispatch=(" + std::to_string(diagnostics->DispatchX) + ", " +
                std::to_string(diagnostics->DispatchY) + ", " +
                std::to_string(diagnostics->DispatchZ) + ").");
        }
        if (diagnostics->ActivePixelCount == 0u &&
            (m_DirectLightingTechnique != RaytracingDemoLightingTechnique::None ||
                (m_IndirectLightingTechnique != RaytracingDemoLightingTechnique::None && m_MaxBounces > 1)))
        {
            failActivePixelAssertion("Compacted ray-traced pixel count unexpectedly returned zero.");
        }

        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "active_pixel_dispatch",
                FrameworkDiagnostics::AssertionResult::Passed,
                "Active-pixel count and finalized indirect dispatch arguments agree.",
                {
                    { "active_pixel_count", static_cast<uint64_t>(diagnostics->ActivePixelCount) },
                    { "dispatch_x", static_cast<uint64_t>(diagnostics->DispatchX) },
                    { "dispatch_y", static_cast<uint64_t>(diagnostics->DispatchY) },
                    { "dispatch_z", static_cast<uint64_t>(diagnostics->DispatchZ) },
                });
        }

        m_RuntimeAutomation.AppendDiagnosticLog(
            "Latest completed active ray-traced pixels: " + std::to_string(diagnostics->ActivePixelCount) +
            "; dispatch: (" + std::to_string(diagnostics->DispatchX) + ", " +
            std::to_string(diagnostics->DispatchY) + ", " +
            std::to_string(diagnostics->DispatchZ) + ").");
        break;
    }
    case RuntimeAutomationAction::VerifyCopyQueueValidation:
    {
        const auto failCopyQueueAssertion = [this](std::string message)
        {
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordAssertion(
                    "copy_queue_validation",
                    FrameworkDiagnostics::AssertionResult::Failed,
                    message);
            }
            throw std::runtime_error(std::move(message));
        };

        if (!m_CopyQueueValidationEnabled)
        {
            failCopyQueueAssertion("Copy queue validation was disabled before verification.");
        }

        const RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
        const RenderGraph::RenderGraphCrossQueuePlanValidation& plan =
            renderGraph.GetCrossQueuePlanValidation();
        const RenderGraph::RenderGraphQueueSynchronizationStats& synchronization =
            renderGraph.GetFrameSynchronizationStats();
        const RenderGraph::RenderGraphQueueFenceValues frameFences =
            renderGraph.GetFrameSubmissionFences();
        const RenderGraph::RenderGraphQueueFenceValues copiedColorRetirement =
            renderGraph.GetResourceRetirement(
                RaytracingDemoRenderGraph::ResourceIds::CopyQueueValidationColor);
        const RenderGraph::RenderGraphQueueFenceValues computeColorRetirement =
            renderGraph.GetResourceRetirement(
                RaytracingDemoRenderGraph::ResourceIds::CopyQueueValidationComputeColor);

        const bool passed =
            plan.IsValid() &&
            plan.CopyPassCount >= 1u &&
            plan.DirectToCopyTransferCount >= 1u &&
            plan.CopyToConsumerTransferCount >= 1u &&
            frameFences.Copy != 0u &&
            frameFences.AsyncCompute != 0u &&
            synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::Copy) >= 1u &&
            synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::AsyncCompute) >= 1u &&
            synchronization.GetWaitCount(
                RenderGraph::RenderPassQueue::Direct,
                RenderGraph::RenderPassQueue::Copy) >= 1u &&
            synchronization.GetWaitCount(
                RenderGraph::RenderPassQueue::Copy,
                RenderGraph::RenderPassQueue::Direct) >= 1u &&
            synchronization.GetWaitCount(
                RenderGraph::RenderPassQueue::Direct,
                RenderGraph::RenderPassQueue::AsyncCompute) >= 1u &&
            synchronization.GetWaitCount(
                RenderGraph::RenderPassQueue::AsyncCompute,
                RenderGraph::RenderPassQueue::Direct) >= 1u &&
            copiedColorRetirement.Copy != 0u &&
            copiedColorRetirement.AsyncCompute != 0u &&
            computeColorRetirement.AsyncCompute != 0u &&
            computeColorRetirement.Direct != 0u;
        const std::string message =
            "Copy queue handoff: cross_queue_transfers=" +
            std::to_string(plan.CrossQueueResourceTransferCount) +
            ", missing_state_transitions=" + std::to_string(plan.MissingStatePlanTransitionCount) +
            ", incorrect_state_transitions=" + std::to_string(plan.IncorrectStatePlanTransitionCount) +
            ", copy_fence=" + std::to_string(frameFences.Copy) +
            ", async_fence=" + std::to_string(frameFences.AsyncCompute) + ".";
        if (!passed)
        {
            failCopyQueueAssertion(message);
        }

        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "copy_queue_validation",
                FrameworkDiagnostics::AssertionResult::Passed,
                message,
                {
                    { "cross_queue_transfer_count", plan.CrossQueueResourceTransferCount },
                    { "copy_pass_count", plan.CopyPassCount },
                    { "direct_to_copy_transfer_count", plan.DirectToCopyTransferCount },
                    { "copy_to_consumer_transfer_count", plan.CopyToConsumerTransferCount },
                    { "copy_submission_count", synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::Copy) },
                    { "async_submission_count", synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::AsyncCompute) },
                    { "copy_retirement_fence", copiedColorRetirement.Copy },
                    { "async_retirement_fence", copiedColorRetirement.AsyncCompute },
                    { "direct_consumer_retirement_fence", computeColorRetirement.Direct },
                });
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
    case RuntimeAutomationAction::VerifyDynamicRayTracingUpdate:
    {
        const auto failDynamicRtasAssertion = [this](std::string message)
        {
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordAssertion(
                    "dynamic_rtas_update",
                    FrameworkDiagnostics::AssertionResult::Failed,
                    message);
            }
            throw std::runtime_error(std::move(message));
        };

        const RaytracingDemoDynamicRtasUpdateStatistics& sceneStats =
            m_SceneResources.GetDynamicRayTracingUpdateStatistics();
        const RayTracingAccelerationStructureUpdateStatistics& accelerationStats =
            m_SceneResources.GetRayTracingAccelerationStructure().GetUpdateStatistics();
        const bool verifyRestore = value != 0u;
        const bool dynamicEmitterActive = m_SceneResources.HasActiveDynamicRayTracingEmitter();
        const uint64_t minimumUpdateCount = verifyRestore ? 4u : 3u;
        const bool passed =
            sceneStats.GeometryUploadCount >= minimumUpdateCount &&
            sceneStats.MeshletTransformUpdateCount >= minimumUpdateCount &&
            sceneStats.MeshletGeometryUpdateCount >= minimumUpdateCount &&
            (!dynamicEmitterActive || sceneStats.EmissiveMeshRefreshCount >= minimumUpdateCount) &&
            sceneStats.RefitCount >= minimumUpdateCount &&
            accelerationStats.BottomLevelUpdateCount >= minimumUpdateCount &&
            accelerationStats.TopLevelUpdateCount >= minimumUpdateCount &&
            accelerationStats.RetiredResourceCount >= minimumUpdateCount &&
            (!verifyRestore || (sceneStats.RestoreCount >= 1u && sceneStats.LastUpdateRestored));
        const std::string message =
            "Dynamic RTAS update: geometry_uploads=" + std::to_string(sceneStats.GeometryUploadCount) +
            ", meshlet_transform_updates=" + std::to_string(sceneStats.MeshletTransformUpdateCount) +
            ", meshlet_geometry_updates=" + std::to_string(sceneStats.MeshletGeometryUpdateCount) +
            ", emissive_mesh_refreshes=" + std::to_string(sceneStats.EmissiveMeshRefreshCount) +
            ", dynamic_emitter_active=" + std::to_string(dynamicEmitterActive) +
            ", refits=" + std::to_string(sceneStats.RefitCount) +
            ", blas_updates=" + std::to_string(accelerationStats.BottomLevelUpdateCount) +
            ", tlas_updates=" + std::to_string(accelerationStats.TopLevelUpdateCount) +
            ", retired_resources=" + std::to_string(accelerationStats.RetiredResourceCount) +
            ", restores=" + std::to_string(sceneStats.RestoreCount) + ".";
        if (!passed)
        {
            failDynamicRtasAssertion(message);
        }

        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "dynamic_rtas_update",
                FrameworkDiagnostics::AssertionResult::Passed,
                message,
                {
                    { "geometry_upload_count", sceneStats.GeometryUploadCount },
                    { "meshlet_transform_update_count", sceneStats.MeshletTransformUpdateCount },
                    { "meshlet_geometry_update_count", sceneStats.MeshletGeometryUpdateCount },
                    { "emissive_mesh_refresh_count", sceneStats.EmissiveMeshRefreshCount },
                    { "refit_count", sceneStats.RefitCount },
                    { "restore_count", sceneStats.RestoreCount },
                    { "blas_update_count", accelerationStats.BottomLevelUpdateCount },
                    { "tlas_update_count", accelerationStats.TopLevelUpdateCount },
                    { "tlas_build_count", accelerationStats.TopLevelBuildCount },
                    { "retired_resource_count", accelerationStats.RetiredResourceCount },
                    { "verified_restore", verifyRestore },
                });
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
    case RuntimeAutomationAction::VerifyDynamicSkinnedMeshCapability:
    {
        const RaytracingDemoDynamicSceneCapabilities& capabilities =
            m_SceneResources.GetDynamicSceneCapabilities();
        const bool passed =
            capabilities.SupportsMeshletTransformUpdates &&
            capabilities.SupportsMeshletGeometryUpdates &&
            capabilities.SupportsDynamicEmissiveMeshUpdates &&
            !capabilities.SupportsSkinnedMeshUpdates;
        const std::string message =
            "Dynamic scene capability: meshlet_transform=enabled, meshlet_geometry=enabled, "
            "dynamic_emissive_mesh=enabled, skinned_mesh=explicitly_unsupported.";
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "dynamic_skinned_mesh_capability",
                passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
                message,
                {
                    { "meshlet_transform_updates", capabilities.SupportsMeshletTransformUpdates },
                    { "meshlet_geometry_updates", capabilities.SupportsMeshletGeometryUpdates },
                    { "dynamic_emissive_mesh_updates", capabilities.SupportsDynamicEmissiveMeshUpdates },
                    { "skinned_mesh_updates", capabilities.SupportsSkinnedMeshUpdates },
                });
        }
        if (!passed)
        {
            throw std::runtime_error(message);
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
    case RuntimeAutomationAction::OIDNStaticSpp:
        m_Denoisers.SetOIDNStaticSpp(value);
        ResetAccumulation();
        break;
//Modify Begin:2026-08-25 by Hui
    case RuntimeAutomationAction::VerifyOIDNResult:
    {
        const bool passed =
            m_Denoisers.IsOIDNEnabled() &&
            IsAccumulationActive() &&
            m_Denoisers.HasOIDNResult();
        const std::string message = passed
            ? "OIDN holds the denoised result while static after D3D12 shared-memory CUDA denoise and RenderGraph composite."
            : "OIDN did not produce an uploaded result before the automation verification deadline.";
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "oidn_async_pipeline",
                passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
                message,
                {
                    { "denoiser_enabled", m_Denoisers.IsOIDNEnabled() },
                    { "manual_accumulation_enabled", m_AccumulationEnabled },
                    { "effective_accumulation_enabled", IsAccumulationActive() },
                    { "backend", m_Denoisers.IsOIDNUsingCuda() ? "cuda" : "cpu_fallback" },
                    { "result_uploaded", m_Denoisers.HasOIDNResult() },
                });
        }
        if (!passed)
        {
            throw std::runtime_error(message);
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
    case RuntimeAutomationAction::OIDNCameraMotion:
    {
        m_OIDNGenerationBeforeCameraMotion = m_Denoisers.GetOIDNGeneration();
        GetSceneCamera().Translate(DirectX::XMVectorSet(0.0f, 0.0f, 0.05f, 0.0f), Space::Local);
        ResetAccumulation(false, false);
        m_OIDNGenerationAfterCameraMotion = m_Denoisers.GetOIDNGeneration();
        const bool passed =
            m_OIDNGenerationAfterCameraMotion > m_OIDNGenerationBeforeCameraMotion &&
            !m_Denoisers.HasOIDNResult();
        const std::string message = passed
            ? "OIDN immediately invalidated its held result after camera motion."
            : "OIDN did not immediately invalidate its held result after camera motion.";
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "oidn_motion_reset_immediate",
                passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
                message,
                {
                    { "generation_before", m_OIDNGenerationBeforeCameraMotion },
                    { "generation_after", m_OIDNGenerationAfterCameraMotion },
                    { "result_uploaded", m_Denoisers.HasOIDNResult() },
                });
        }
        if (!passed)
        {
            throw std::runtime_error(message);
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
    case RuntimeAutomationAction::VerifyOIDNInvalidated:
    {
        const bool passed =
            m_Denoisers.IsOIDNEnabled() &&
            IsAccumulationActive() &&
            m_OIDNGenerationAfterCameraMotion > m_OIDNGenerationBeforeCameraMotion;
        const std::string message = passed
            ? "OIDN invalidated the prior result after camera motion and restarted static accumulation."
            : "OIDN did not advance its static-image generation after camera motion.";
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "oidn_motion_invalidation",
                passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
                message,
                {
                    { "denoiser_enabled", m_Denoisers.IsOIDNEnabled() },
                    { "effective_accumulation_enabled", IsAccumulationActive() },
                    { "generation_before", m_OIDNGenerationBeforeCameraMotion },
                    { "generation_after", m_OIDNGenerationAfterCameraMotion },
                    { "result_uploaded", m_Denoisers.HasOIDNResult() },
                });
        }
        if (!passed)
        {
            throw std::runtime_error(message);
        }
        m_RuntimeAutomation.AppendDiagnosticLog(message);
        break;
    }
//Modify End
    case RuntimeAutomationAction::AsyncCompute:
        if (m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = enabled;
            ResetAccumulation();
        }
        break;
    case RuntimeAutomationAction::ParallelDirectCommandRecording:
        m_ParallelDirectCommandRecordingEnabled = enabled;
        break;
    case RuntimeAutomationAction::Accumulation:
        m_AccumulationEnabled = enabled;
        ResetAccumulation();
        break;
    case RuntimeAutomationAction::GpuTiming:
        m_GpuTimingEnabled = enabled;
        ResetProfilerDisplay();
        if (!enabled)
        {
            m_RenderGraphTimingCaptureEnabled = false;
        }
        m_RenderGraphTimingHistory.Clear();
        break;
    case RuntimeAutomationAction::TimingCapture:
        m_RenderGraphTimingCaptureEnabled = enabled;
        m_RenderGraphTimingHistory.Clear();
        break;
    case RuntimeAutomationAction::ReSTIRGIStageTiming:
        m_ReSTIRGIStageTimingEnabled = enabled && m_GpuTimingEnabled;
        break;
    case RuntimeAutomationAction::ReSTIRGITemporalResampling:
    case RuntimeAutomationAction::ReSTIRGISpatialResampling:
    case RuntimeAutomationAction::ReSTIRGITemporalJacobian:
    case RuntimeAutomationAction::ReSTIRGISpatialRayTracedBiasCorrection:
    {
        ReSTIRGISettings restirGISettings = m_IndirectLightingReSTIRGI.GetSettings();
        switch (action)
        {
        case RuntimeAutomationAction::ReSTIRGITemporalResampling:
            restirGISettings.EnableTemporalResampling = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGISpatialResampling:
            restirGISettings.EnableSpatialResampling = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGITemporalJacobian:
            restirGISettings.EnableTemporalJacobian = enabled;
            break;
        case RuntimeAutomationAction::ReSTIRGISpatialRayTracedBiasCorrection:
            restirGISettings.EnableRayTracedSpatialBiasCorrection = enabled;
            break;
        default:
            break;
        }
        m_IndirectLightingReSTIRGI.SetSettings(restirGISettings);
        EnsureRayTracingPipelines();
        ResetAccumulation();
        break;
    }
    case RuntimeAutomationAction::DumpTiming:
        m_RenderGraphTimingHistory.DumpCsv();
        break;
    case RuntimeAutomationAction::MaterialShading:
        SetMaterialShadingModel(static_cast<MaterialShadingModel>(value));
        break;
    case RuntimeAutomationAction::ReSTIRDIConfig:
    {
        m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
        m_DirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRDI;
        m_IndirectLightingTechnique = RaytracingDemoLightingTechnique::None;
        m_MaxBounces = 1;
        ReSTIRDISettings settings = m_DirectLightingReSTIRDI.GetSettings();
        settings.EnableInitialVisibility = (value & (1u << 0u)) != 0u;
        settings.EnableTemporalResampling = (value & (1u << 1u)) != 0u;
        settings.EnableTemporalVisibilityShortcut = (value & (1u << 2u)) != 0u;
        settings.EnableTemporalPermutationSampling = (value & (1u << 3u)) != 0u;
        settings.EnableBoilingFilter = (value & (1u << 4u)) != 0u;
        settings.EnableSpatialResampling = (value & (1u << 5u)) != 0u;
        settings.EnableSpatialMaterialSimilarityTest = (value & (1u << 6u)) != 0u;
        settings.EnableFinalVisibility = (value & (1u << 7u)) != 0u;
        settings.ReuseFinalVisibility = (value & (1u << 8u)) != 0u;
        // Final discard is only meaningful when final visibility is enabled;
        // keep it off in the disabled branch to isolate that branch itself.
        settings.DiscardInvisibleFinalSamples = settings.EnableFinalVisibility &&
            ((value & (1u << 9u)) != 0u);
        m_DirectLightingReSTIRDI.SetSettings(settings);
        EnsureRayTracingPipelines();
        ResetAccumulation(false, true);
        break;
    }
//Modify Begin:2026-08-26 by Hui
    case RuntimeAutomationAction::CaptureScreenshot:
        if (value > static_cast<uint32_t>(RaytracingDemoAutomation::ScreenshotCapture::ReSTIRDIAndGI))
        {
            throw std::out_of_range("Runtime automation screenshot capture index is out of range.");
        }
        m_PendingAutomationScreenshot = value;
        break;
//Modify End
    case RuntimeAutomationAction::MatrixCase:
        ApplyRuntimeAutomationMatrixCase(value);
        break;
    }
}

//Modify Begin:2026-08-28 by Hui
void RaytracingDemo::CapturePendingAutomationScreenshot()
{
    if (!m_PendingAutomationScreenshot.has_value())
    {
        return;
    }
    if (m_RenderGraphFrameState->FrameGenerationEnabled)
    {
        throw std::runtime_error("Automation screenshots do not support frame generation.");
    }

    const uint32_t captureIndex = m_PendingAutomationScreenshot.value();
    constexpr std::array<const char*, 5u> captureNames = {
        "pt-direct", "pt-indirect", "restir-di", "restir-gi", "restir-di-gi",
    };
    RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
    const std::shared_ptr<Texture>& presentationTexture =
        renderGraph.GetTexture(renderGraph.GetPresentationResourceId());
    if (!m_DiagnosticsImageCapture.Request(*presentationTexture, captureNames[captureIndex]))
    {
        return;
    }
    m_RuntimeAutomation.AppendDiagnosticLog(
        "Queued asynchronous diagnostics image capture: " + std::string(captureNames[captureIndex]) + ".");
    m_PendingAutomationScreenshot.reset();
}
//Modify End

//Modify Begin:2026-08-23 by Hui
bool RaytracingDemo::AdvanceStartupLoad()
//Modify End
//Modify Begin:2026-08-21 by Hui
try
//Modify End
{
//Modify Begin:2026-08-23 by Hui
    if (m_StartupLoadStage == StartupLoadStage::Bootstrap)
    {
        InitializeDiagnostics();
        Assert(RayTracingShader::IsSupported(m_FrameworkDeviceContext), "DirectX Raytracing is not supported by the selected adapter.");
        SetProfilerDisplayRefreshIntervalSeconds(m_ProfilerDisplay.GetRefreshIntervalSeconds());

        const auto commandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        const auto commandList = commandQueue->GetCommandList();
        m_ImGui = std::make_unique<ImGuiImpl>(m_FrameworkDeviceContext, *commandList, *PWindow);
        m_LightBillboardMesh = Mesh::CreateVerticalQuad(*commandList);
//Modify Begin:2026-08-26 by Hui
        m_SpotLightGizmoMesh = Mesh::CreateCone(*commandList, 1.0f, 1.0f, 32);
//Modify End
        m_DisplayBlitMesh = Mesh::CreateBlitTriangle(*commandList);
        commandQueue->ExecuteCommandList(commandList);

        m_StartupScenePath = GetScenePath();
        SceneImportOptions importOptions;
        importOptions.GenerateFallbackCamera = true;
        m_StartupSceneImport = std::async(
            std::launch::async,
            [scenePath = m_StartupScenePath, importOptions]()
            {
                return SceneImporter::ImportFromFile(scenePath, importOptions);
            });
        m_StartupLoadStartTime = std::chrono::steady_clock::now();
        SetStartupLoadStage(StartupLoadStage::ImportScene, "Parsing scene description", 0.08f);
        return true;
    }

    if (m_StartupLoadStage == StartupLoadStage::ImportScene)
    {
        if (!m_StartupSceneImport.valid())
        {
            throw std::logic_error("Startup scene import task was not created.");
        }
        if (m_StartupSceneImport.wait_for(std::chrono::seconds::zero()) != std::future_status::ready)
        {
            // Prevent the loading loop from monopolizing a CPU core while the importer runs.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            return true;
        }

        SetStartupLoadStage(StartupLoadStage::UploadScene, "Loading scene geometry and textures", 0.22f);
        return true;
    }

    const auto commandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (m_StartupLoadStage == StartupLoadStage::WaitForGpu)
    {
        if (m_StartupGpuFenceValue == 0u || !commandQueue->IsFenceComplete(m_StartupGpuFenceValue))
        {
            return true;
        }

        InitializeRuntimeAutomation();
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.Record("application.lifecycle", "load_content_complete");
        }
        SetStartupLoadStage(StartupLoadStage::Complete, "Ready", 1.0f);
        return true;
    }
    if (m_StartupLoadStage == StartupLoadStage::Complete)
    {
        return true;
    }

    const auto commandList = commandQueue->GetCommandList();
    if (m_StartupLoadStage == StartupLoadStage::UploadScene)
    {
        LoadSceneContent(*commandList, m_StartupScenePath);
        commandQueue->ExecuteCommandList(commandList);
        SetStartupLoadStage(StartupLoadStage::CreateGeometryPipelines, "Creating geometry pipelines", 0.48f);
        return true;
    }
//Modify End

//Modify Begin:2026-08-25 by Hui
    if (m_StartupLoadStage == StartupLoadStage::CreateGeometryPipelines)
    {
        m_ShaderPipelineBootstrap.CreateGeometryPipelines();
        SetStartupLoadStage(StartupLoadStage::CreatePostProcessPipelines, "Creating post-processing pipelines", 0.66f);
        return true;
    }

    if (m_StartupLoadStage == StartupLoadStage::CreatePostProcessPipelines)
    {
        m_ShaderPipelineBootstrap.CreatePostProcessPipelines();
        SetStartupLoadStage(StartupLoadStage::CreateLightingPipeline, "Creating lighting pipeline", 0.80f);
        return true;
    }

    if (m_StartupLoadStage == StartupLoadStage::CreateLightingPipeline)
    {
        m_ShaderPipelineBootstrap.CreateLightingPipelines();
        SetStartupLoadStage(StartupLoadStage::FinalizeRendering, "Building ray-tracing resources", 0.90f);
        return true;
    }
//Modify End

//Modify Begin:2026-08-23 by Hui
    if (m_StartupLoadStage != StartupLoadStage::FinalizeRendering)
    {
        return true;
    }
//Modify End

    m_Denoisers.Initialize(m_FrameworkDeviceContext);
    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_SceneResources.BuildRayTracingAccelerationStructure(*commandList, accelerationStructureSettings);
    m_Lights.InitializeGpuBuffers(*commandList);
    m_Bloom.InitializeFrameworkBloom(*commandList);
//Modify Begin:2026-08-19 by Hui
    EnsureRayTracingPipelines();
//Modify End
//Modify Begin:2026-08-19 by Hui
    PrewarmRuntimeShadowVariants();
//Modify End
    BindRayTracingShaderResources();

//Modify Begin:2026-08-19 by Hui
    m_GpuTimestampProfiler.Initialize(
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        128);
    m_AsyncComputeGpuTimestampProfiler.Initialize(
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
        128,
        D3D12_COMMAND_LIST_TYPE_COMPUTE);
    m_CopyGpuTimestampProfiler.Initialize(
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY),
        128,
        D3D12_COMMAND_LIST_TYPE_COPY);
    RebuildRenderGraph();
//Modify End

//Modify Begin:2026-08-23 by Hui
    m_StartupGpuFenceValue = commandQueue->ExecuteCommandList(commandList);
    SetStartupLoadStage(StartupLoadStage::WaitForGpu, "Waiting for initial GPU uploads", 0.98f);
//Modify End
    return true;
}
//Modify Begin:2026-08-21 by Hui
catch (const std::exception& exception)
{
    RecordDiagnosticsFailure("load_content", exception);
    throw;
}
catch (...)
{
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "runtime.load_content",
            FrameworkDiagnostics::AssertionResult::Failed,
            "A non-standard exception escaped RaytracingDemo::LoadContent.",
            { { "stage", std::string("load_content") } });
        m_Diagnostics.Finalize(
            FrameworkDiagnostics::SessionStatus::Failed,
            "A non-standard exception escaped RaytracingDemo::LoadContent.");
    }
    throw;
}
//Modify End

//Modify Begin:2026-08-23 by Hui
bool RaytracingDemo::LoadContent()
{
    if (m_StartupLoadStage == StartupLoadStage::Bootstrap)
    {
        ApplyHdr10Output(m_Hdr10OutputRequested);
    }
    return AdvanceStartupLoad();
}

void RaytracingDemo::SetStartupLoadStage(
    const StartupLoadStage stage,
    std::string status,
    const float progress)
{
    m_StartupLoadStage = stage;
    m_StartupLoadStatus = std::move(status);
    m_StartupLoadProgress = std::clamp(progress, 0.0f, 1.0f);

    if (m_Diagnostics.IsEnabled() && m_StartupLoadStartTime.time_since_epoch().count() != 0)
    {
        const double elapsedMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_StartupLoadStartTime).count();
        m_Diagnostics.Record(
            "application.startup",
            "stage",
            DiagnosticTelemetrySeverity::Info,
            {
                { "status", m_StartupLoadStatus },
                { "progress", static_cast<double>(m_StartupLoadProgress) },
                { "elapsed_milliseconds", elapsedMilliseconds },
            });
        m_Diagnostics.Flush();
    }
}

void RaytracingDemo::RenderStartupLoadingScreen()
{
    if (m_ImGui == nullptr)
    {
        return;
    }

    m_ImGui->BeginFrame();

    ImGuiIO& io = ImGui::GetIO();
    constexpr float PanelWidth = 440.0f;
    const ImVec2 panelPosition(
        (std::max)(0.0f, (io.DisplaySize.x - PanelWidth) * 0.5f),
        (std::max)(0.0f, (io.DisplaySize.y - 150.0f) * 0.5f));
    ImGui::SetNextWindowPos(panelPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(PanelWidth, 0.0f), ImGuiCond_Always);
    constexpr ImGuiWindowFlags LoadingWindowFlags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Startup Loading", nullptr, LoadingWindowFlags);
    ImGui::TextUnformatted("Starting Raytracing Demo");
    ImGui::Spacing();
    ImGui::TextUnformatted(m_StartupLoadStatus.c_str());
    ImGui::ProgressBar(m_StartupLoadProgress, ImVec2(-1.0f, 0.0f));
    const double elapsedSeconds = m_StartupLoadStartTime.time_since_epoch().count() == 0
        ? 0.0
        : std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartupLoadStartTime).count();
    ImGui::Text("%.0f%%  |  %.1f s", m_StartupLoadProgress * 100.0f, elapsedSeconds);
    ImGui::End();
    m_ImGui->Render();

    const auto commandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();
    const RenderTarget& backBufferRenderTarget = PWindow->GetRenderTarget();
    constexpr float ClearColor[] = { 0.025f, 0.030f, 0.045f, 1.0f };
    PWindow->PrepareBackBufferForRenderTarget(*commandList);
    commandList->SetRenderTarget(backBufferRenderTarget);
    commandList->SetAutomaticViewportAndScissorRect(backBufferRenderTarget);
    commandList->ClearRenderTarget(backBufferRenderTarget, ClearColor, static_cast<D3D12_CLEAR_FLAGS>(0));
    m_ImGui->DrawToRenderTarget(*commandList);
    commandQueue->ExecuteCommandList(commandList);
    PWindow->Present();
}
//Modify End

void RaytracingDemo::UnloadContent()
{
//Modify Begin:2026-08-21 by Hui
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.Record("application.lifecycle", "unload_content_begin");
    }
    if (m_RenderPipeline.HasRenderGraph())
    {
        m_RenderPipeline.GetRenderGraph().SetDiagnosticTelemetrySink(nullptr);
    }
//Modify End
//Modify Begin:2026-08-28 by Hui
    m_DiagnosticsImageCapture.Drain();
//Modify End
//Modify Begin:2026-08-19 by Hui
    m_Bloom.ReleaseInteropResource();
//Modify End
    m_RenderPipeline.Reset();
//Modify Begin:2026-08-25 by Hui
    m_ShaderPipelineBootstrap.Reset();
//Modify End
    m_LightBillboardMesh.reset();
//Modify Begin:2026-08-26 by Hui
    m_SpotLightGizmoMesh.reset();
//Modify End
//Modify Begin:2026-08-19 by Hui
    m_DisplayBlitMesh.reset();
//Modify End
    m_ImGui.reset();
    m_Denoisers.Shutdown();
//Modify Begin:2026-08-19 by Hui
    m_Bloom.Shutdown();
//Modify End
    m_SkyboxTexture.reset();
//Modify Begin:2026-08-26 by Hui
    m_EnvironmentFallbackCubemap.reset();
//Modify End
//Modify Begin:2026-08-19 by Hui
    m_PathTracingPipelines.Reset();
    m_ActivePixels.Reset();
//Modify End
//Modify Begin:2026-08-19 by Hui
    m_GpuTimestampProfiler.Shutdown();
    m_AsyncComputeGpuTimestampProfiler.Shutdown();
    m_CopyGpuTimestampProfiler.Shutdown();
//Modify End
    m_SceneResources.Clear();
//Modify Begin:2026-08-28 by Hui
    GetApplication().SetDiagnosticTelemetrySink(nullptr);
    if (m_Diagnostics.IsEnabled())
    {
        const bool automationFailed = m_RuntimeAutomation.HasFailed();
        m_Diagnostics.Finalize(
            automationFailed ? FrameworkDiagnostics::SessionStatus::Failed : FrameworkDiagnostics::SessionStatus::Passed,
            automationFailed
                ? m_RuntimeAutomation.GetFailureMessage()
                : m_RuntimeAutomation.IsCompleted()
                    ? "Automation scenario completed successfully."
                    : "RaytracingDemo unloaded normally.");
    }
//Modify End
}

RayTracingSceneResourceLayout RaytracingDemo::BuildRayTracingSceneResourceLayout() const
{
    const RayTracingAccelerationStructure& accelerationStructure = m_SceneResources.GetRayTracingAccelerationStructure();
    const std::vector<std::shared_ptr<Mesh>>& rayTracingMeshes = accelerationStructure.GetMeshes();

    RayTracingSceneResourceLayout layout;
    layout.TextureDescriptorCapacity = ComputeDescriptorArrayCapacity(m_SceneResources.GetTextureCount(), m_SceneResources.GetTextureCapacity());
    layout.GeometryDescriptorCapacity = ComputeDescriptorArrayCapacity(rayTracingMeshes.size(), rayTracingMeshes.capacity());
//Modify Begin:2026-08-19 by Hui
    layout.EnvironmentProjection = m_SkyboxTexture != nullptr
        ? ShaderResourceView::GetEnvironmentTextureProjection(*m_SkyboxTexture)
        : EnvironmentTextureProjection::Cubemap;
//Modify End
    return layout;
}

void RaytracingDemo::EnsureRayTracingPipelines()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const ReSTIRDIFrameConstants restirDIConstants = m_DirectLightingReSTIRDI.GetFrameConstants(false);
//Modify Begin:2026-08-19 by Hui
    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        m_SoftShadowsEnabled ? PathTracingShadowMode::SoftShadows : PathTracingShadowMode::HardShadows,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces),
        m_PathTracingDispatchMode);
    const bool compactedDispatchEnabled =
        m_PathTracingDispatchMode == PathTracingDispatchMode::CompactedIndirect;
    const bool restirDICompactedDispatch = compactedDispatchEnabled &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    if (compactedDispatchEnabled)
    {
        m_ActivePixels.EnsurePipelines();
    }
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        m_SoftShadowsEnabled,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel,
        restirDICompactedDispatch);
    const bool restirGIActive =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;
    const ReSTIRGIVariantConfig restirGIVariantConfig = m_IndirectLightingReSTIRGI.GetVariantConfig(
        static_cast<uint32_t>(m_MaxBounces));
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            m_SoftShadowsEnabled,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel,
            compactedDispatchEnabled);
    }
    if (m_SceneResources.GetRayTracingAccelerationStructure().GetInstanceCount() > 0)
    {
        BindRayTracingShaderResources();
    }
//Modify End
}

//Modify Begin:2026-08-19 by Hui
void RaytracingDemo::SetMaterialShadingModel(const MaterialShadingModel shadingModel)
{
    if (m_MaterialShadingModel == shadingModel)
    {
        return;
    }

    m_MaterialShadingModel = shadingModel;
    if (m_RenderPipeline.HasRenderGraph())
    {
        EnsureRayTracingPipelines();
    }
    ResetAccumulation();
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemo::SetMaxBounces(const int maxBounces)
{
    const int clampedMaxBounces = std::clamp(maxBounces, 1, 5);
    if (m_MaxBounces == clampedMaxBounces)
    {
        return;
    }

    m_MaxBounces = clampedMaxBounces;
    if (m_RenderPipeline.HasRenderGraph())
    {
        EnsureRayTracingPipelines();
        UpdateRenderGraphFrameState();
        EnsureRenderGraphTopology();
    }
    ResetAccumulation();
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemo::PrewarmRuntimeShadowVariants()
{
    const RayTracingSceneResourceLayout layout = BuildRayTracingSceneResourceLayout();
    const ReSTIRDIFrameConstants restirDIConstants = m_DirectLightingReSTIRDI.GetFrameConstants(false);
    const PathTracingShadowMode currentShadowMode = m_SoftShadowsEnabled
        ? PathTracingShadowMode::SoftShadows
        : PathTracingShadowMode::HardShadows;
    const PathTracingShadowMode alternateShadowMode = currentShadowMode == PathTracingShadowMode::SoftShadows
        ? PathTracingShadowMode::HardShadows
        : PathTracingShadowMode::SoftShadows;
    const bool compactedDispatchEnabled =
        m_PathTracingDispatchMode == PathTracingDispatchMode::CompactedIndirect;
    const bool restirDICompactedDispatch = compactedDispatchEnabled &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    if (compactedDispatchEnabled)
    {
        m_ActivePixels.EnsurePipelines();
    }

    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        alternateShadowMode,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces),
        m_PathTracingDispatchMode);
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        alternateShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel,
        restirDICompactedDispatch);
    const bool restirGIActive =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;
    const ReSTIRGIVariantConfig restirGIVariantConfig = m_IndirectLightingReSTIRGI.GetVariantConfig(
        static_cast<uint32_t>(m_MaxBounces));
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            alternateShadowMode == PathTracingShadowMode::SoftShadows,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel,
            compactedDispatchEnabled);
    }
    m_PathTracingPipelines.EnsurePipelines(
        m_PathTracingBackend,
        currentShadowMode,
        m_MaterialShadingModel,
        layout,
        static_cast<uint32_t>(m_MaxBounces),
        m_PathTracingDispatchMode);
    m_DirectLightingReSTIRDIPass.EnsurePipelines(
        currentShadowMode == PathTracingShadowMode::SoftShadows,
        static_cast<uint32_t>(layout.EnvironmentProjection),
        restirDIConstants,
        m_MaterialShadingModel,
        restirDICompactedDispatch);
    if (restirGIActive)
    {
        m_IndirectLightingReSTIRGIPass.EnsurePipelines(
            currentShadowMode == PathTracingShadowMode::SoftShadows,
            static_cast<uint32_t>(layout.EnvironmentProjection),
            restirGIVariantConfig,
            m_MaterialShadingModel,
            compactedDispatchEnabled);
    }
    BindRayTracingShaderResources();
}
//Modify End

void RaytracingDemo::BindRayTracingShaderResources()
{
//Modify Begin:2026-08-19 by Hui
    m_PathTracingPipelines.BindRayTracingResources(
        m_SceneResources.GetRayTracingAccelerationStructure(),
        m_SceneResources,
        m_Lights,
        GetRayTracingEnvironmentTexture());
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
//Modify Begin:2026-08-19 by Hui
    pipeline.PreviousViewProjection = m_HasPreviousViewProjection ? m_PreviousViewProjection : pipeline.ViewProjection;
    pipeline.DebugMeshletClusters = m_DebugMeshletClusters ? 1u : 0u;
//Modify End
    return pipeline;
}

//Modify Begin:2026-08-28 by Hui
bool RaytracingDemo::ApplyHdr10Output(const bool enabled)
{
    if (PWindow == nullptr)
    {
        m_Hdr10OutputRequested = false;
        m_Hdr10OutputEnabled = false;
        m_Hdr10PresentationStatus = "HDR10 presentation requires an initialized window.";
        return false;
    }
    if (enabled && m_DLSS.IsFrameGenerationEnabled())
    {
        m_Hdr10OutputRequested = false;
        m_Hdr10PresentationStatus =
            "HDR10 is unavailable while DLSS Frame Generation owns the presentation path.";
        return false;
    }

    const bool configured = GetApplication().SetHdr10Output(*PWindow, enabled);
    m_Hdr10OutputEnabled = configured && PWindow->IsHdr10OutputEnabled();
    m_Hdr10OutputRequested = enabled;
    const Hdr10OutputCapabilities& capabilities = PWindow->GetHdr10OutputCapabilities();
    m_Hdr10PeakNits = m_Hdr10OutputEnabled
        ? std::clamp(capabilities.MaxLuminanceNits, 200.0f, 10000.0f)
        : 1000.0f;
    m_Hdr10PresentationStatus = m_Hdr10OutputEnabled
        ? "HDR10/PQ active."
        : enabled
            ? "HDR10 unavailable on the current output; SDR fallback remains active."
            : "SDR output active.";

    AutoExposure::Settings exposureSettings = m_AutoExposure.GetSettings();
    exposureSettings.Output = m_Hdr10OutputEnabled
        ? AutoExposure::OutputMode::HDR10
        : AutoExposure::OutputMode::SDR;
    m_AutoExposure.SetSettings(exposureSettings);

    m_RenderPipeline.Reset();
    m_RenderGraphTimingHistory.Clear();
    ResetAccumulation();

    if (m_ImGui != nullptr)
    {
        m_ImGui.reset();
        const auto directQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        const auto commandList = directQueue->GetCommandList();
        m_ImGui = std::make_unique<ImGuiImpl>(m_FrameworkDeviceContext, *commandList, *PWindow);
        directQueue->ExecuteCommandList(commandList);
    }

    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.Record(
            "presentation",
            "hdr10_output",
            DiagnosticTelemetrySeverity::Info,
            {
                { "requested", static_cast<uint64_t>(enabled) },
                { "active", static_cast<uint64_t>(m_Hdr10OutputEnabled) },
                { "supported", static_cast<uint64_t>(capabilities.IsSupported) },
                { "peak_nits", static_cast<double>(m_Hdr10PeakNits) },
            });
    }
    return m_Hdr10OutputEnabled == enabled;
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemo::RebuildRenderGraph()
{
    ResetProfilerDisplay();
    UpdateRenderGraphFrameState();
    EnsureRayTracingPipelines();

    if (!m_RenderPipeline.HasRenderGraph())
    {
        m_Denoisers.OnResourcesRecreated(
            m_RenderGraphFrameState->Width,
            m_RenderGraphFrameState->Height);
    }

    const RaytracingDemoRenderPipelineConfiguration configuration =
        RaytracingDemoRenderPipelineController::BuildConfiguration(*m_RenderGraphFrameState);
    m_RenderPipeline.Rebuild(
        configuration,
        [this]()
        {
            m_FrameworkDeviceContext.Flush();
            m_Bloom.ReleaseInteropResource();
            m_DLSS.OnResourcesRecreated();
            m_Denoisers.OnResourcesRecreated(
                m_RenderGraphFrameState->Width,
                m_RenderGraphFrameState->Height);
        },
        [this]()
        {
            return RaytracingDemoRenderGraphBuilder::Create(*this);
        },
        [this](RenderGraph::RenderGraphRoot& renderGraph)
        {
            renderGraph.SetDiagnosticTelemetrySink(m_Diagnostics.IsEnabled() ? &m_Diagnostics : nullptr);
            renderGraph.SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
            renderGraph.SetAsyncComputeGpuTimestampProfiler(
                m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
            renderGraph.SetCopyGpuTimestampProfiler(
                m_GpuTimingEnabled ? &m_CopyGpuTimestampProfiler : nullptr);
            renderGraph.SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
            renderGraph.SetParallelDirectCommandRecording(m_ParallelDirectCommandRecordingEnabled);
        });
}

void RaytracingDemo::ResetProfilerDisplay()
{
    m_ProfilerDisplay.Reset();
}

void RaytracingDemo::SetProfilerDisplayRefreshIntervalSeconds(const double refreshIntervalSeconds)
{
    m_ProfilerDisplay.SetRefreshIntervalSeconds(refreshIntervalSeconds);
    if (PWindow != nullptr)
    {
        PWindow->SetProfilerDisplayRefreshIntervalSeconds(m_ProfilerDisplay.GetRefreshIntervalSeconds());
    }
}

void RaytracingDemo::EnsureRenderGraphTopology()
{
    if (m_RenderPipeline.NeedsRebuild(
        RaytracingDemoRenderPipelineController::BuildConfiguration(*m_RenderGraphFrameState)))
    {
        RebuildRenderGraph();
        ResetAccumulation();
    }
}

//Modify End

//Modify Begin:2026-08-25 by Hui
void RaytracingDemo::ResetAccumulation(
    const bool resetDenoiserHistory,
    const bool resetReSTIRHistory,
    const bool resetOIDNHistory)
{
    m_AccumulationFrameIndex = 0;
    if (resetReSTIRHistory)
    {
        m_ReSTIRDIHistoryValid = false;
        m_ReSTIRGIHistoryValid = false;
    }
    if (resetDenoiserHistory)
    {
        m_Denoisers.ResetHistory();
        m_DLSS.InvalidateHistory();
        m_HasPreviousViewProjection = false;
    }
    else if (resetOIDNHistory)
    {
        m_Denoisers.ResetOIDNHistory();
    }
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemo::SaveCurrentCameraToUnityScene()
{
    if (m_Scene.GetSourcePath().extension() != ".unity")
    {
        throw std::runtime_error("Saving a JSON scene camera is not implemented yet.");
    }
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

//Modify Begin:2026-08-26 by Hui
RaytracingDemoPassResources RaytracingDemo::CreatePassResources()
{
    return {
        m_SceneResources,
        m_Lights,
        m_PathTracingPipelines,
        m_ActivePixels,
        m_DirectLightingReSTIRDI,
        m_DirectLightingReSTIRDIPass,
        m_IndirectLightingReSTIRGI,
        m_IndirectLightingReSTIRGIPass,
        m_DLSS,
        m_ShaderPipelineBootstrap.GetDLSSRayReconstructionPrepareShader(),
        m_ShaderPipelineBootstrap.GetCopyQueueValidationShader(),
        m_Denoisers,
        m_Bloom,
        m_AutoExposure,
        m_ShaderPipelineBootstrap.GetGBufferShader(),
        m_ShaderPipelineBootstrap.GetMeshletIndirectGBufferShader(),
        m_ShaderPipelineBootstrap.GetTaskMeshGBufferShader(),
        m_ShaderPipelineBootstrap.GetMeshletCullShader(),
        m_ShaderPipelineBootstrap.GetMeshletDrawCommandSignature(),
        m_ShaderPipelineBootstrap.GetDisplayCompositeShader(),
        m_ShaderPipelineBootstrap.GetSkyboxComputeShader(),
        m_ShaderPipelineBootstrap.GetSkyboxEquirectangularComputeShader(),
        m_ShaderPipelineBootstrap.GetSkyboxCubemapStripComputeShader(),
        GetRayTracingEnvironmentTexture(),
        m_SkyboxTexture == nullptr,
        m_DisplayBlitMesh,
        GetSceneCamera(),
        m_FrameworkDeviceContext.GetDevice(),
        m_FrameworkDeviceContext.GetD3D12DeviceContext(),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
        m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY),
        &m_GpuTimestampProfiler,
        m_Diagnostics.IsEnabled() ? &m_Diagnostics : nullptr
    };
}

RaytracingDemoPassConfig RaytracingDemo::CreatePassConfig() const
{
    return {
        m_RenderGraphFrameState,
    };
}
//Modify End

//Modify Begin:2026-08-28 by Hui
void RaytracingDemo::UpdateRenderGraphFrameState()
{
    RaytracingDemoFrameState& state = *m_RenderGraphFrameState;
    state.Backend = m_PathTracingBackend;
    state.DispatchMode = m_PathTracingDispatchMode;
    state.ShadingModel = m_MaterialShadingModel;
    state.DirectLightingTechnique = m_DirectLightingTechnique;
    state.IndirectLightingTechnique = m_IndirectLightingTechnique;
    state.AsyncComputeEnabled = m_AsyncComputeEnabled;
    state.CopyQueueValidationEnabled = m_CopyQueueValidationEnabled;
    state.DynamicRayTracingUpdateEnabled = m_SceneResources.RequiresDynamicRayTracingUpdatePass();
    state.UseMeshletGBuffer = m_UseMeshletGBuffer;
    state.UseTaskShaderMeshlets = m_UseTaskShaderMeshlets;
    state.DebugMeshletClusters = m_DebugMeshletClusters;
    state.SkyboxEnabled = m_SkyboxEnabled;
    state.DebugLightingTextureTarget = m_DebugLightingTextureTarget;
    state.DebugTextureTarget = m_DebugTextureTarget;
    state.MaxBounces = m_MaxBounces;
    state.DenoiserEnabled = IsDenoiserEnabled();
    state.DenoiserAlgorithm = state.DenoiserEnabled
        ? m_Denoisers.GetAlgorithm()
        : DenoiserController::Algorithm::Off;
    state.NRDDenoiserMode = m_Denoisers.GetNRDMode();
    state.SVGFAtrousIterations = m_Denoisers.GetSVGFAtrousIterations();
    state.BloomEnabled = m_Bloom.IsEnabled();
    state.BloomBackend = m_Bloom.GetBackend();
    state.BloomPyramidLevels = (std::max)(1, m_Bloom.GetPyramidLevels());
    const uint32_t displayWidth = static_cast<uint32_t>((std::max)(m_Width, 1));
    const uint32_t displayHeight = static_cast<uint32_t>((std::max)(m_Height, 1));
    const DLSSOptimalSettings dlssSettings = m_DLSS.GetOptimalSettings(displayWidth, displayHeight);
    state.DLSSEnabled = m_DLSS.IsEnabled();
    state.RayReconstructionEnabled = state.DLSSEnabled && m_DLSS.IsRayReconstructionEnabled();
    state.FrameGenerationEnabled = !m_Hdr10OutputEnabled && m_DLSS.IsFrameGenerationEnabled();
    state.Hdr10OutputEnabled = m_Hdr10OutputEnabled;
    state.DlssMode = m_DLSS.GetMode();
    state.Width = dlssSettings.RenderWidth;
    state.Height = dlssSettings.RenderHeight;
    state.DisplayWidth = displayWidth;
    state.DisplayHeight = displayHeight;
    state.DLSSSharpness = dlssSettings.Sharpness;
    state.DLSSJitterOffset = m_DLSS.GetJitterOffset(m_FrameIndex);
    state.View = GetSceneCamera().GetViewMatrix();
    state.Projection = GetSceneCamera().GetProjectionMatrix();
    if (state.DLSSEnabled)
    {
        state.Projection.r[2].m128_f32[0] += 2.0f * state.DLSSJitterOffset.x / static_cast<float>(state.Width);
        state.Projection.r[2].m128_f32[1] -= 2.0f * state.DLSSJitterOffset.y / static_cast<float>(state.Height);
    }
    state.ViewProjection = state.View * state.Projection;
    state.AccumulationEnabled = IsAccumulationActive();
    state.FrameIndex = m_FrameIndex;
    state.AccumulationFrameIndex = m_AccumulationFrameIndex;
    state.ReSTIRDIHistoryValid = m_ReSTIRDIHistoryValid;
    state.ReSTIRGIHistoryValid = m_ReSTIRGIHistoryValid;
    state.ReSTIRGIStageTimingEnabled = m_GpuTimingEnabled && m_ReSTIRGIStageTimingEnabled;
    state.HasPreviousViewProjection = m_HasPreviousViewProjection;
    state.PreviousViewProjection = m_PreviousViewProjection;
}
//Modify End

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

//Modify Begin:2026-08-23 by Hui
    if (!IsStartupLoadComplete())
    {
        try
        {
            RenderStartupLoadingScreen();
            if (!AdvanceStartupLoad())
            {
                throw std::runtime_error("RaytracingDemo startup loading did not complete successfully.");
            }
        }
        catch (const std::exception& exception)
        {
            RecordDiagnosticsFailure("startup_load", exception);
            throw;
        }
        return;
    }
//Modify End

//Modify Begin:2026-08-21 by Hui
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.SetFrameIndex(m_FrameIndex);
    }
//Modify End

//Modify Begin:2026-08-28 by Hui
    if (m_PendingHdr10OutputRequest.has_value())
    {
        const bool requestedHdr10Output = m_PendingHdr10OutputRequest.value();
        m_PendingHdr10OutputRequest.reset();
        ApplyHdr10Output(requestedHdr10Output);
    }

    const auto directCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto asyncComputeCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    const auto copyCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    const auto collectGpuTimingFrames = [this](
        GpuTimestampProfiler& profiler,
        CommandQueue& commandQueue,
        const auto& accumulateSamples,
        const char* queueName)
    {
        std::vector<GpuTimestampSample> completedSamples;
        while (profiler.CollectCompletedFrame(commandQueue, completedSamples))
        {
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordGpuTimings(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
            accumulateSamples(completedSamples);
            if (m_RenderGraphTimingCaptureEnabled)
            {
                m_RenderGraphTimingHistory.Record(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
        }
    };
    if (m_GpuTimingEnabled)
    {
        collectGpuTimingFrames(
            m_GpuTimestampProfiler,
            *directCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateDirectQueueSamples(samples);
            },
            "Direct");
        collectGpuTimingFrames(
            m_AsyncComputeGpuTimestampProfiler,
            *asyncComputeCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateAsyncComputeQueueSamples(samples);
            },
            "AsyncCompute");
        collectGpuTimingFrames(
            m_CopyGpuTimestampProfiler,
            *copyCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateCopyQueueSamples(samples);
            },
            "Copy");
    }

    for (const BloomController::TimingStats& cudaTiming : m_Bloom.ConsumeCompletedTimingStats())
    {
        m_ProfilerDisplay.AccumulateCudaTiming({
            .D3DToCudaWaitMilliseconds = cudaTiming.D3DToCudaWaitMs,
            .KernelsMilliseconds = cudaTiming.KernelsMs,
            .CudaSignalMilliseconds = cudaTiming.CudaSignalMs,
            .TotalCudaStreamMilliseconds = cudaTiming.TotalCudaStreamMs,
            .FrameIndex = cudaTiming.FrameIndex,
            .Valid = cudaTiming.Valid,
        });
    }
    m_ProfilerDisplay.Update(e.TotalTime);

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

    try
    {
        if (m_SceneRuntime.ApplyPendingChanges(
            m_FrameworkDeviceContext,
            m_SceneResources,
            m_Lights,
            m_RuntimeAutomation))
        {
            m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: rebuild render graph.");
            RebuildRenderGraph();
            m_RenderGraphTimingHistory.Clear();
            ResetAccumulation();
            m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: complete.");
        }
    }
    catch (const std::exception& exception)
    {
        if (m_RuntimeAutomation.FailNow(
            FrameworkDiagnostics::AutomationExitCode::ControlFailure,
            "Scene runtime update failed: " + std::string(exception.what())))
        {
            return;
        }
        RecordDiagnosticsFailure("scene_runtime", exception);
        throw;
    }

    if (!m_Hdr10OutputEnabled && m_DLSS.IsFrameGenerationEnabled())
    {
        m_DLSS.BeginFrameGeneration(m_FrameIndex);
    }

    UpdateRenderGraphFrameState();
    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = m_RenderGraphFrameState->Width;
    metadata.m_ScreenHeight = m_RenderGraphFrameState->Height;
    metadata.m_DisplayWidth = m_RenderGraphFrameState->DisplayWidth;
    metadata.m_DisplayHeight = m_RenderGraphFrameState->DisplayHeight;
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;
    metadata.m_DeltaTime = m_DeltaTime;

    EnsureRenderGraphTopology();
    RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
    renderGraph.SetDiagnosticTelemetrySink(m_Diagnostics.IsEnabled() ? &m_Diagnostics : nullptr);
    if (m_RenderGraphFrameState->FrameGenerationEnabled)
    {
        m_FrameGenerationInputs = {};
        m_FrameGenerationInputs.Depth = renderGraph.GetTexture(RaytracingDemoRenderGraph::ResourceIds::DepthBuffer);
        m_FrameGenerationInputs.MotionVectors = renderGraph.GetTexture(RaytracingDemoRenderGraph::ResourceIds::MotionVector);
        m_FrameGenerationInputs.HudLessColor = renderGraph.GetTexture(RaytracingDemoRenderGraph::ResourceIds::AutoExposureOutput);
        m_FrameGenerationInputs.RenderWidth = m_RenderGraphFrameState->Width;
        m_FrameGenerationInputs.RenderHeight = m_RenderGraphFrameState->Height;
        m_FrameGenerationInputs.DisplayWidth = m_RenderGraphFrameState->DisplayWidth;
        m_FrameGenerationInputs.DisplayHeight = m_RenderGraphFrameState->DisplayHeight;
        m_FrameGenerationInputs.FrameIndex = m_FrameIndex;
        m_FrameGenerationInputs.HasPreviousViewProjection = m_RenderGraphFrameState->HasPreviousViewProjection;
        m_FrameGenerationInputs.JitterOffset = m_RenderGraphFrameState->DLSSJitterOffset;
        m_FrameGenerationInputs.View = m_RenderGraphFrameState->View;
        m_FrameGenerationInputs.Projection = m_RenderGraphFrameState->Projection;
        m_FrameGenerationInputs.ViewProjection = m_RenderGraphFrameState->ViewProjection;
        m_FrameGenerationInputs.PreviousViewProjection = m_RenderGraphFrameState->PreviousViewProjection;
        m_DLSS.PrepareFrameGeneration(m_FrameGenerationInputs);
    }
    renderGraph.SetGpuTimestampProfiler(m_GpuTimingEnabled ? &m_GpuTimestampProfiler : nullptr);
    renderGraph.SetAsyncComputeGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_AsyncComputeGpuTimestampProfiler : nullptr);
    renderGraph.SetCopyGpuTimestampProfiler(
        m_GpuTimingEnabled ? &m_CopyGpuTimestampProfiler : nullptr);
    renderGraph.SetDebugSerializeAsyncCompute(m_DebugSerializeAsyncCompute);
    const auto renderGraphCpuStart = std::chrono::steady_clock::now();
    const bool readsCompactedRayTracedPixelCount =
        m_RenderGraphFrameState->UsesCompactedRayTracedPixelDispatch();
    const bool activeRayCountReadbackQueued =
        readsCompactedRayTracedPixelCount && m_ActivePixels.BeginCountReadback();
    const bool oidnReadbackQueued = m_Denoisers.BeginOIDNReadback(
        m_RenderGraphFrameState->AccumulationEnabled,
        m_RenderGraphFrameState->AccumulationFrameIndex);
    BindlessDescriptorHeap& bindlessDescriptorHeap = m_SceneResources.GetBindlessDescriptorHeap();
    bindlessDescriptorHeap.BeginFrame(*directCommandQueue, *asyncComputeCommandQueue);
    bool bindlessFrameEnded = false;
    const auto endBindlessFrame = [&renderGraph, &bindlessDescriptorHeap, &bindlessFrameEnded]()
    {
        if (bindlessFrameEnded)
        {
            return;
        }

        const RenderGraph::RenderGraphQueueFenceValues& frameFences = renderGraph.GetFrameSubmissionFences();
        bindlessDescriptorHeap.EndFrame(frameFences.Direct, frameFences.AsyncCompute);
        bindlessFrameEnded = true;
    };
    bool activeRayCountReadbackEnded = false;
    const auto endActiveRayCountReadback = [&renderGraph, &activeRayCountReadbackEnded, activeRayCountReadbackQueued, this]()
    {
        if (!activeRayCountReadbackQueued || activeRayCountReadbackEnded)
        {
            return;
        }

        m_ActivePixels.EndCountReadback(renderGraph.GetFrameSubmissionFences().Direct);
        activeRayCountReadbackEnded = true;
    };
    bool oidnReadbackEnded = false;
    const auto endOidnReadback = [&renderGraph, &oidnReadbackEnded, oidnReadbackQueued, this]()
    {
        if (!oidnReadbackQueued || oidnReadbackEnded)
        {
            return;
        }

        m_Denoisers.EndOIDNReadback(renderGraph.GetFrameSubmissionFences().Direct);
        oidnReadbackEnded = true;
    };
    try
    {
        renderGraph.Execute(metadata);
        endBindlessFrame();
        endActiveRayCountReadback();
        endOidnReadback();
    }
    catch (const std::exception& exception)
    {
        endBindlessFrame();
        if (activeRayCountReadbackQueued && renderGraph.GetFrameSubmissionFences().Direct != 0u)
        {
            endActiveRayCountReadback();
        }
        else
        {
            if (activeRayCountReadbackQueued)
            {
                m_ActivePixels.CancelCountReadback();
            }
        }
        if (oidnReadbackQueued && renderGraph.GetFrameSubmissionFences().Direct != 0u)
        {
            endOidnReadback();
        }
        else if (oidnReadbackQueued)
        {
            m_Denoisers.CancelOIDNReadback();
        }
        RecordDiagnosticsFailure("render_graph_execute", exception);
        throw std::runtime_error(std::string("RaytracingDemo::OnRender RenderGraph.Execute failed: ") + exception.what());
    }
    const double renderGraphCpuMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - renderGraphCpuStart).count();
    if (m_GpuTimingEnabled)
    {
        m_ProfilerDisplay.AccumulateRenderGraphCpuMilliseconds(renderGraphCpuMilliseconds);
    }
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.Record(
            "profiler.cpu",
            "render_graph_execute",
            DiagnosticTelemetrySeverity::Info,
            { { "cpu_duration_ms", renderGraphCpuMilliseconds } });
    }

    try
    {
        PresentDisplayOutput();
        CapturePendingAutomationScreenshot();
    }
    catch (const std::exception& exception)
    {
        RecordDiagnosticsFailure("present", exception);
        const char* backendName = m_PathTracingBackend == PathTracingBackend::InlineRayQuery
            ? "InlineRayQuery"
            : "ShaderTableDxr";
        throw std::runtime_error(
            std::string("RaytracingDemo::OnRender PresentDisplayOutput failed: ") + exception.what() +
            " [Backend=" + backendName +
            ", AsyncCompute=" + (m_AsyncComputeEnabled ? "true" : "false") + "]");
    }

    ++m_FrameIndex;
    if (m_RenderGraphFrameState->AccumulationEnabled)
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }
    m_ReSTIRDIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    m_ReSTIRGIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;

    m_PreviousViewProjection = m_RenderGraphFrameState->ViewProjection;
    m_HasPreviousViewProjection = true;
}
//Modify End
