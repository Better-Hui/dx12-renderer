//Modify Begin:2026-08-07 by BestHui
#include <Framework/Rendering/Upscaling/DLSS.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <DX12Library/StreamlineRuntime.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <nvsdk_ngx_helpers.h>
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

struct DLSS::InternalState
{
    NVSDK_NGX_Parameter* CapabilityParameters = nullptr;
    NVSDK_NGX_Parameter* FeatureParameters = nullptr;
    NVSDK_NGX_Parameter* EvaluationParameters = nullptr;
    NVSDK_NGX_Handle* Feature = nullptr;
    DLSSMode FeatureMode = DLSSMode::Disabled;
    uint32_t RenderWidth = 0;
    uint32_t RenderHeight = 0;
    uint32_t DisplayWidth = 0;
    uint32_t DisplayHeight = 0;
};

namespace
{
    constexpr std::array<float, 16> Halton2 = {
        0.5f, 0.25f, 0.75f, 0.125f,
        0.625f, 0.375f, 0.875f, 0.0625f,
        0.5625f, 0.3125f, 0.8125f, 0.1875f,
        0.6875f, 0.4375f, 0.9375f, 0.03125f,
    };

    constexpr std::array<float, 16> Halton3 = {
        1.0f / 3.0f, 2.0f / 3.0f, 1.0f / 9.0f, 4.0f / 9.0f,
        7.0f / 9.0f, 2.0f / 9.0f, 5.0f / 9.0f, 8.0f / 9.0f,
        1.0f / 27.0f, 10.0f / 27.0f, 19.0f / 27.0f, 4.0f / 27.0f,
        13.0f / 27.0f, 22.0f / 27.0f, 7.0f / 27.0f, 16.0f / 27.0f,
    };

    NVSDK_NGX_PerfQuality_Value GetPerfQualityValue(const DLSSMode mode)
    {
        switch (mode)
        {
        case DLSSMode::DLAA:
            return NVSDK_NGX_PerfQuality_Value_DLAA;
        case DLSSMode::Quality:
            return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case DLSSMode::Balanced:
            return NVSDK_NGX_PerfQuality_Value_Balanced;
        case DLSSMode::Performance:
            return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case DLSSMode::UltraPerformance:
            return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        case DLSSMode::Disabled:
        default:
            return NVSDK_NGX_PerfQuality_Value_DLAA;
        }
    }

    sl::DLSSMode GetStreamlineMode(const DLSSMode mode)
    {
        switch (mode)
        {
        case DLSSMode::DLAA:
            return sl::DLSSMode::eDLAA;
        case DLSSMode::Quality:
            return sl::DLSSMode::eMaxQuality;
        case DLSSMode::Balanced:
            return sl::DLSSMode::eBalanced;
        case DLSSMode::Performance:
            return sl::DLSSMode::eMaxPerformance;
        case DLSSMode::UltraPerformance:
            return sl::DLSSMode::eUltraPerformance;
        case DLSSMode::Disabled:
        default:
            return sl::DLSSMode::eOff;
        }
    }

    sl::float4x4 GetStreamlineMatrix(const DirectX::XMMATRIX& matrix)
    {
        DirectX::XMFLOAT4X4 values{};
        DirectX::XMStoreFloat4x4(&values, matrix);
        sl::float4x4 result{};
        result.setRow(0u, { values._11, values._12, values._13, values._14 });
        result.setRow(1u, { values._21, values._22, values._23, values._24 });
        result.setRow(2u, { values._31, values._32, values._33, values._34 });
        result.setRow(3u, { values._41, values._42, values._43, values._44 });
        return result;
    }

    sl::Resource GetStreamlineTextureResource(const std::shared_ptr<Texture>& texture, const D3D12_RESOURCE_STATES state)
    {
        return {
            sl::ResourceType::eTex2d,
            texture->GetD3D12Resource().Get(),
            static_cast<uint32_t>(state),
        };
    }

    std::string GetResultMessage(const char* operation, const NVSDK_NGX_Result result)
    {
        std::ostringstream message;
        message << operation << " failed (NGX result " << static_cast<int>(result) << ").";
        return message.str();
    }

}

DLSS::DLSS(FrameworkDeviceContext& deviceContext, DLSSInitializationDesc initializationDesc)
    : m_DeviceContext(deviceContext)
    , m_InternalState(std::make_unique<InternalState>())
{
    (void)Initialize(initializationDesc);
}

DLSS::~DLSS()
{
    Shutdown();
}

void DLSS::SetMode(const DLSSMode mode)
{
    if (m_Mode == mode)
    {
        return;
    }

    m_Mode = mode;
    InvalidateHistory();
}

void DLSS::SetRayReconstructionEnabled(const bool enabled)
{
    const std::shared_ptr<StreamlineRuntime> streamlineRuntime = m_DeviceContext.GetStreamlineRuntime();
    if (enabled && (streamlineRuntime == nullptr || !streamlineRuntime->IsInitialized()))
    {
        m_StatusMessage = "DLSS Ray Reconstruction requires the Streamline runtime.";
        m_RayReconstructionEnabled = false;
        return;
    }
    if (m_RayReconstructionEnabled != enabled)
    {
        m_RayReconstructionEnabled = enabled;
        InvalidateHistory();
    }
}

void DLSS::SetFrameGenerationEnabled(const bool enabled)
{
    const std::shared_ptr<StreamlineRuntime> streamlineRuntime = m_DeviceContext.GetStreamlineRuntime();
    if (enabled && (streamlineRuntime == nullptr || !streamlineRuntime->IsInitialized()))
    {
        m_StatusMessage = "DLSS Frame Generation requires the Streamline runtime.";
        m_FrameGenerationEnabled = false;
        return;
    }
    if (m_FrameGenerationEnabled == enabled)
    {
        return;
    }
    if (!m_DeviceContext.SetFrameGenerationEnabled(enabled))
    {
        m_StatusMessage = "DLSS Frame Generation swapchain recreation failed: " +
            (streamlineRuntime != nullptr ? streamlineRuntime->GetStatusMessage() : std::string("no runtime"));
        return;
    }

    sl::ReflexOptions reflexOptions{};
    reflexOptions.mode = enabled ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
    const sl::Result reflexResult = slReflexSetOptions(reflexOptions);
    if (reflexResult != sl::Result::eOk)
    {
        m_DeviceContext.SetFrameGenerationEnabled(false);
        m_StatusMessage = "slReflexSetOptions failed while toggling DLSS Frame Generation.";
        return;
    }
    m_FrameGenerationEnabled = enabled;
    InvalidateHistory();
}

DLSSOptimalSettings DLSS::GetOptimalSettings(const uint32_t displayWidth, const uint32_t displayHeight)
{
    DLSSOptimalSettings settings{};
    settings.RenderWidth = (std::max)(displayWidth, 1u);
    settings.RenderHeight = (std::max)(displayHeight, 1u);

    if (!IsEnabled() || m_Mode == DLSSMode::DLAA)
    {
        return settings;
    }

    if (m_RayReconstructionEnabled)
    {
        sl::DLSSDOptions options{};
        options.mode = GetStreamlineMode(m_Mode);
        options.outputWidth = displayWidth;
        options.outputHeight = displayHeight;
        options.colorBuffersHDR = sl::Boolean::eTrue;
        options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::eUnpacked;
        sl::DLSSDOptimalSettings reconstructionSettings{};
        const sl::Result result = slDLSSDGetOptimalSettings(options, reconstructionSettings);
        if (result != sl::Result::eOk)
        {
            m_StatusMessage = "slDLSSDGetOptimalSettings failed (Streamline result " + std::to_string(static_cast<uint32_t>(result)) + ").";
            return settings;
        }
        settings.RenderWidth = (std::max)(reconstructionSettings.optimalRenderWidth, 1u);
        settings.RenderHeight = (std::max)(reconstructionSettings.optimalRenderHeight, 1u);
        settings.Sharpness = reconstructionSettings.optimalSharpness;
        return settings;
    }

    unsigned int optimalWidth = settings.RenderWidth;
    unsigned int optimalHeight = settings.RenderHeight;
    unsigned int maximumWidth = optimalWidth;
    unsigned int maximumHeight = optimalHeight;
    unsigned int minimumWidth = optimalWidth;
    unsigned int minimumHeight = optimalHeight;
    float sharpness = 0.0f;
    const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
        m_InternalState->CapabilityParameters,
        displayWidth,
        displayHeight,
        GetPerfQualityValue(m_Mode),
        &optimalWidth,
        &optimalHeight,
        &maximumWidth,
        &maximumHeight,
        &minimumWidth,
        &minimumHeight,
        &sharpness);
    if (NVSDK_NGX_FAILED(result))
    {
        m_StatusMessage = GetResultMessage("NGX_DLSS_GET_OPTIMAL_SETTINGS", result);
        return settings;
    }

    settings.RenderWidth = (std::max)(optimalWidth, 1u);
    settings.RenderHeight = (std::max)(optimalHeight, 1u);
    settings.Sharpness = sharpness;
    return settings;
}

DirectX::XMFLOAT2 DLSS::GetJitterOffset(const uint64_t frameIndex) const
{
    if (!IsEnabled())
    {
        return { 0.0f, 0.0f };
    }

    const size_t sampleIndex = static_cast<size_t>(frameIndex % Halton2.size());
    return { Halton2[sampleIndex] - 0.5f, Halton3[sampleIndex] - 0.5f };
}

void DLSS::InvalidateHistory()
{
    m_HistoryReset = true;
}

void DLSS::OnResourcesRecreated()
{
    if (m_InternalState->EvaluationParameters != nullptr)
    {
        NVSDK_NGX_Parameter_SetD3d12Resource(m_InternalState->EvaluationParameters, NVSDK_NGX_Parameter_Color, nullptr);
        NVSDK_NGX_Parameter_SetD3d12Resource(m_InternalState->EvaluationParameters, NVSDK_NGX_Parameter_Output, nullptr);
        NVSDK_NGX_Parameter_SetD3d12Resource(m_InternalState->EvaluationParameters, NVSDK_NGX_Parameter_Depth, nullptr);
        NVSDK_NGX_Parameter_SetD3d12Resource(m_InternalState->EvaluationParameters, NVSDK_NGX_Parameter_MotionVectors, nullptr);
    }
    InvalidateHistory();
}

void DLSS::Execute(CommandList& commandList, const DLSSExecutionInputs& inputs)
{
    if (!IsEnabled())
    {
        throw std::runtime_error("DLSS execution was requested while the feature is disabled or unsupported.");
    }

    if (inputs.Color == nullptr || inputs.Depth == nullptr || inputs.MotionVectors == nullptr || inputs.Output == nullptr)
    {
        throw std::runtime_error("DLSS requires color, depth, motion-vector, and output textures.");
    }

    if (m_RayReconstructionEnabled)
    {
        ExecuteRayReconstruction(commandList, inputs);
        return;
    }

    if (!EnsureFeature(commandList, inputs))
    {
        throw std::runtime_error(m_StatusMessage);
    }

    NVSDK_NGX_D3D12_DLSS_Eval_Params evaluationParams{};
    evaluationParams.Feature.pInColor = inputs.Color->GetD3D12Resource().Get();
    evaluationParams.Feature.pInOutput = inputs.Output->GetD3D12Resource().Get();
    evaluationParams.Feature.InSharpness = inputs.Sharpness;
    evaluationParams.pInDepth = inputs.Depth->GetD3D12Resource().Get();
    evaluationParams.pInMotionVectors = inputs.MotionVectors->GetD3D12Resource().Get();
    evaluationParams.InJitterOffsetX = inputs.JitterOffset.x;
    evaluationParams.InJitterOffsetY = inputs.JitterOffset.y;
    evaluationParams.InRenderSubrectDimensions = { inputs.RenderWidth, inputs.RenderHeight };
    evaluationParams.InReset = inputs.Reset || m_HistoryReset ? 1 : 0;
    evaluationParams.InMVScaleX = static_cast<float>(inputs.RenderWidth);
    evaluationParams.InMVScaleY = static_cast<float>(inputs.RenderHeight);
    evaluationParams.InPreExposure = 1.0f;
    evaluationParams.InExposureScale = 1.0f;

    const NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(
        commandList.GetGraphicsCommandList().Get(),
        m_InternalState->Feature,
        m_InternalState->EvaluationParameters,
        &evaluationParams);
    if (NVSDK_NGX_FAILED(result))
    {
        m_StatusMessage = GetResultMessage("NGX_D3D12_EVALUATE_DLSS_EXT", result);
        throw std::runtime_error(m_StatusMessage);
    }

    m_HistoryReset = false;
}

void DLSS::ExecuteRayReconstruction(CommandList& commandList, const DLSSExecutionInputs& inputs)
{
    if (inputs.DiffuseAlbedo == nullptr || inputs.SpecularAlbedo == nullptr || inputs.Normals == nullptr || inputs.Roughness == nullptr)
    {
        throw std::runtime_error("DLSS Ray Reconstruction requires diffuse albedo, specular albedo, normals, and roughness textures.");
    }

    const std::shared_ptr<StreamlineRuntime> streamlineRuntime = m_DeviceContext.GetStreamlineRuntime();
    if (streamlineRuntime == nullptr || !streamlineRuntime->IsInitialized())
    {
        throw std::runtime_error("DLSS Ray Reconstruction requires an initialized Streamline runtime.");
    }

    sl::FrameToken* frameToken = nullptr;
    const sl::Result tokenResult = slGetNewFrameToken(frameToken, &inputs.FrameIndex);
    if (tokenResult != sl::Result::eOk || frameToken == nullptr)
    {
        throw std::runtime_error("slGetNewFrameToken failed for DLSS Ray Reconstruction.");
    }

    const DirectX::XMMATRIX clipToPreviousClip = DirectX::XMMatrixMultiply(
        DirectX::XMMatrixInverse(nullptr, inputs.ViewProjection),
        inputs.HasPreviousViewProjection ? inputs.PreviousViewProjection : inputs.ViewProjection);
    const DirectX::XMMATRIX inverseView = DirectX::XMMatrixInverse(nullptr, inputs.View);
    DirectX::XMFLOAT4X4 inverseViewValues{};
    DirectX::XMStoreFloat4x4(&inverseViewValues, inverseView);

    sl::Constants constants{};
    constants.cameraViewToClip = GetStreamlineMatrix(inputs.Projection);
    constants.clipToCameraView = GetStreamlineMatrix(DirectX::XMMatrixInverse(nullptr, inputs.Projection));
    constants.clipToPrevClip = GetStreamlineMatrix(clipToPreviousClip);
    constants.prevClipToClip = GetStreamlineMatrix(DirectX::XMMatrixInverse(nullptr, clipToPreviousClip));
    constants.jitterOffset = { inputs.JitterOffset.x, inputs.JitterOffset.y };
    constants.mvecScale = { 1.0f, 1.0f };
    constants.cameraPos = { inverseViewValues._41, inverseViewValues._42, inverseViewValues._43 };
    constants.cameraRight = { inverseViewValues._11, inverseViewValues._12, inverseViewValues._13 };
    constants.cameraUp = { inverseViewValues._21, inverseViewValues._22, inverseViewValues._23 };
    constants.cameraFwd = { inverseViewValues._31, inverseViewValues._32, inverseViewValues._33 };
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eTrue;
    constants.reset = inputs.Reset || m_HistoryReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    const sl::ViewportHandle viewport(0u);
    const sl::Result constantsResult = slSetConstants(constants, *frameToken, viewport);
    if (constantsResult != sl::Result::eOk)
    {
        throw std::runtime_error("slSetConstants failed for DLSS Ray Reconstruction.");
    }

    sl::DLSSOptions dlssOptions{};
    dlssOptions.mode = GetStreamlineMode(m_Mode);
    dlssOptions.outputWidth = inputs.DisplayWidth;
    dlssOptions.outputHeight = inputs.DisplayHeight;
    dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
    const sl::Result dlssOptionsResult = slDLSSSetOptions(viewport, dlssOptions);
    if (dlssOptionsResult != sl::Result::eOk)
    {
        throw std::runtime_error("slDLSSSetOptions failed for DLSS Ray Reconstruction.");
    }

    sl::DLSSDOptions reconstructionOptions{};
    reconstructionOptions.mode = GetStreamlineMode(m_Mode);
    reconstructionOptions.outputWidth = inputs.DisplayWidth;
    reconstructionOptions.outputHeight = inputs.DisplayHeight;
    reconstructionOptions.sharpness = inputs.Sharpness;
    reconstructionOptions.colorBuffersHDR = sl::Boolean::eTrue;
    reconstructionOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::eUnpacked;
    reconstructionOptions.worldToCameraView = GetStreamlineMatrix(inputs.View);
    reconstructionOptions.cameraViewToWorld = GetStreamlineMatrix(inverseView);
    const sl::Result reconstructionOptionsResult = slDLSSDSetOptions(viewport, reconstructionOptions);
    if (reconstructionOptionsResult != sl::Result::eOk)
    {
        throw std::runtime_error("slDLSSDSetOptions failed for DLSS Ray Reconstruction.");
    }

    const sl::Extent renderExtent{ 0u, 0u, inputs.RenderWidth, inputs.RenderHeight };
    const sl::Extent displayExtent{ 0u, 0u, inputs.DisplayWidth, inputs.DisplayHeight };
    sl::Resource color = GetStreamlineTextureResource(inputs.Color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource depth = GetStreamlineTextureResource(inputs.Depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource motionVectors = GetStreamlineTextureResource(inputs.MotionVectors, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource diffuseAlbedo = GetStreamlineTextureResource(inputs.DiffuseAlbedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource specularAlbedo = GetStreamlineTextureResource(inputs.SpecularAlbedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource normals = GetStreamlineTextureResource(inputs.Normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource roughness = GetStreamlineTextureResource(inputs.Roughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource output = GetStreamlineTextureResource(inputs.Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const std::array<sl::ResourceTag, 8> tags = {
        sl::ResourceTag{ &color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &displayExtent },
        sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &motionVectors, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &diffuseAlbedo, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &specularAlbedo, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &normals, sl::kBufferTypeNormals, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
        sl::ResourceTag{ &roughness, sl::kBufferTypeRoughness, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
    };
    const auto rawCommandList = reinterpret_cast<sl::CommandBuffer*>(commandList.GetGraphicsCommandList().Get());
    const sl::Result tagResult = slSetTagForFrame(*frameToken, viewport, tags.data(), static_cast<uint32_t>(tags.size()), rawCommandList);
    if (tagResult != sl::Result::eOk)
    {
        throw std::runtime_error("slSetTagForFrame failed for DLSS Ray Reconstruction.");
    }

    const sl::BaseStructure* evaluateInputs[] = { &viewport };
    const sl::Result evaluateResult = slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, evaluateInputs, _countof(evaluateInputs), rawCommandList);
    if (evaluateResult != sl::Result::eOk)
    {
        throw std::runtime_error("slEvaluateFeature(DLSS_RR) failed.");
    }

    m_HistoryReset = false;
}

bool DLSS::Initialize(const DLSSInitializationDesc& initializationDesc)
{
    const std::filesystem::path applicationDataPath = initializationDesc.ApplicationDataPath.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(initializationDesc.ApplicationDataPath);
    std::error_code createDirectoryError;
    std::filesystem::create_directories(applicationDataPath, createDirectoryError);

    const NVSDK_NGX_Result initResult = NVSDK_NGX_D3D12_Init(
        initializationDesc.ApplicationId,
        applicationDataPath.c_str(),
        m_DeviceContext.GetDevice().Get());
    if (NVSDK_NGX_FAILED(initResult))
    {
        m_StatusMessage = GetResultMessage("NVSDK_NGX_D3D12_Init", initResult);
        return false;
    }
    m_Initialized = true;

    const NVSDK_NGX_Result capabilityResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&m_InternalState->CapabilityParameters);
    if (NVSDK_NGX_FAILED(capabilityResult))
    {
        m_StatusMessage = GetResultMessage("NVSDK_NGX_D3D12_GetCapabilityParameters", capabilityResult);
        Shutdown();
        return false;
    }

    int available = 0;
    const NVSDK_NGX_Result availabilityResult = NVSDK_NGX_Parameter_GetI(
        m_InternalState->CapabilityParameters,
        NVSDK_NGX_Parameter_SuperSampling_Available,
        &available);
    if (NVSDK_NGX_FAILED(availabilityResult) || available == 0)
    {
        m_StatusMessage = NVSDK_NGX_FAILED(availabilityResult)
            ? GetResultMessage("DLSS support query", availabilityResult)
            : "DLSS Super Resolution is not supported by the active adapter or driver.";
        Shutdown();
        return false;
    }

    const NVSDK_NGX_Result featureAllocateResult = NVSDK_NGX_D3D12_AllocateParameters(&m_InternalState->FeatureParameters);
    if (NVSDK_NGX_FAILED(featureAllocateResult))
    {
        m_StatusMessage = GetResultMessage("NVSDK_NGX_D3D12_AllocateParameters for DLSS creation", featureAllocateResult);
        Shutdown();
        return false;
    }

    const NVSDK_NGX_Result evaluationAllocateResult = NVSDK_NGX_D3D12_AllocateParameters(&m_InternalState->EvaluationParameters);
    if (NVSDK_NGX_FAILED(evaluationAllocateResult))
    {
        m_StatusMessage = GetResultMessage("NVSDK_NGX_D3D12_AllocateParameters for DLSS evaluation", evaluationAllocateResult);
        Shutdown();
        return false;
    }

    m_Supported = true;
    m_StatusMessage = "DLSS Super Resolution is available.";
    return true;
}

bool DLSS::EnsureFeature(CommandList& commandList, const DLSSExecutionInputs& inputs)
{
    assert(m_InternalState != nullptr);
    if (m_InternalState->Feature != nullptr &&
        m_InternalState->FeatureMode == m_Mode &&
        m_InternalState->RenderWidth == inputs.RenderWidth &&
        m_InternalState->RenderHeight == inputs.RenderHeight &&
        m_InternalState->DisplayWidth == inputs.DisplayWidth &&
        m_InternalState->DisplayHeight == inputs.DisplayHeight)
    {
        return true;
    }

    if (m_InternalState->Feature != nullptr)
    {
        m_DeviceContext.Flush();
    }
    ReleaseFeature();

    NVSDK_NGX_DLSS_Create_Params createParams{};
    createParams.Feature.InWidth = inputs.RenderWidth;
    createParams.Feature.InHeight = inputs.RenderHeight;
    createParams.Feature.InTargetWidth = inputs.DisplayWidth;
    createParams.Feature.InTargetHeight = inputs.DisplayHeight;
    createParams.Feature.InPerfQualityValue = GetPerfQualityValue(m_Mode);
    createParams.InFeatureCreateFlags =
        NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    NVSDK_NGX_Handle* createdFeature = nullptr;
    const NVSDK_NGX_Result createResult = NGX_D3D12_CREATE_DLSS_EXT(
        commandList.GetGraphicsCommandList().Get(),
        1u,
        1u,
        &createdFeature,
        m_InternalState->FeatureParameters,
        &createParams);
    if (NVSDK_NGX_FAILED(createResult))
    {
        std::ostringstream message;
        message << GetResultMessage("NGX_D3D12_CREATE_DLSS_EXT", createResult)
            << " Cached feature=" << (m_InternalState->Feature != nullptr)
            << ", cached mode=" << static_cast<uint32_t>(m_InternalState->FeatureMode)
            << ", cached render=" << m_InternalState->RenderWidth << 'x' << m_InternalState->RenderHeight
            << ", cached display=" << m_InternalState->DisplayWidth << 'x' << m_InternalState->DisplayHeight
            << ", requested mode=" << static_cast<uint32_t>(m_Mode)
            << ", requested render=" << inputs.RenderWidth << 'x' << inputs.RenderHeight
            << ", requested display=" << inputs.DisplayWidth << 'x' << inputs.DisplayHeight;
        m_StatusMessage = message.str();
        return false;
    }

    m_InternalState->Feature = createdFeature;
    m_InternalState->FeatureMode = m_Mode;
    m_InternalState->RenderWidth = inputs.RenderWidth;
    m_InternalState->RenderHeight = inputs.RenderHeight;
    m_InternalState->DisplayWidth = inputs.DisplayWidth;
    m_InternalState->DisplayHeight = inputs.DisplayHeight;
    m_HistoryReset = true;
    return true;
}

void DLSS::ReleaseFeature()
{
    if (m_InternalState == nullptr || m_InternalState->Feature == nullptr)
    {
        return;
    }

    NVSDK_NGX_D3D12_ReleaseFeature(m_InternalState->Feature);
    m_InternalState->Feature = nullptr;
    m_InternalState->FeatureMode = DLSSMode::Disabled;
    m_InternalState->RenderWidth = 0;
    m_InternalState->RenderHeight = 0;
    m_InternalState->DisplayWidth = 0;
    m_InternalState->DisplayHeight = 0;
}

void DLSS::Shutdown()
{
    if (m_InternalState == nullptr)
    {
        return;
    }

    ReleaseFeature();
    if (m_InternalState->EvaluationParameters != nullptr)
    {
        NVSDK_NGX_D3D12_DestroyParameters(m_InternalState->EvaluationParameters);
        m_InternalState->EvaluationParameters = nullptr;
    }
    if (m_InternalState->FeatureParameters != nullptr)
    {
        NVSDK_NGX_D3D12_DestroyParameters(m_InternalState->FeatureParameters);
        m_InternalState->FeatureParameters = nullptr;
    }
    if (m_InternalState->CapabilityParameters != nullptr)
    {
        NVSDK_NGX_D3D12_DestroyParameters(m_InternalState->CapabilityParameters);
        m_InternalState->CapabilityParameters = nullptr;
    }
    if (m_Initialized)
    {
        NVSDK_NGX_D3D12_Shutdown1(m_DeviceContext.GetDevice().Get());
        m_Initialized = false;
    }
    m_Supported = false;
}
//Modify End
