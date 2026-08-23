//Modify Begin:2026-08-23 by Hui
#include <Framework/Rendering/PostProcess/AutoExposure.h>

#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/AutoExposureApply_CS.h>
#include <Framework/AutoExposureAverageHistogram_CS.h>
#include <Framework/AutoExposureBuildHistogram_CS.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <algorithm>
#include <array>
#include <d3dx12/d3dx12.h>

namespace
{
    constexpr uint32_t HistogramBinCount = 256u;
    constexpr uint32_t HistogramBinStride = sizeof(uint32_t);
//Modify Begin:2026-08-23 by Hui
    constexpr float DefaultMinLogLuminance = -10.0f;
    constexpr float DefaultMaxLogLuminance = 2.0f;
    constexpr float DefaultAdaptationTau = 1.1f;
    constexpr float MinAdaptationTau = 0.01f;
    constexpr float MaxAdaptationTau = 8.0f;
    constexpr float MinLogLuminanceLimit = -16.0f;
    constexpr float MaxLogLuminanceLimit = 8.0f;
    constexpr float MinimumLogLuminanceRange = 0.01f;
//Modify End

    struct AutoExposureConstants
    {
        uint32_t InputWidth = 1u;
        uint32_t InputHeight = 1u;
        uint32_t OutputWidth = 1u;
        uint32_t OutputHeight = 1u;
        float MinLogLuminance = DefaultMinLogLuminance;
        float LogLuminanceRange = DefaultMaxLogLuminance - DefaultMinLogLuminance;
        float DeltaTime = 0.0f;
        float Tau = DefaultAdaptationTau;
        float ExposureScale = 1.0f;
//Modify Begin:2026-08-23 by Hui
        uint32_t ExposureEnabled = 1u;
//Modify End
    };

    std::unique_ptr<ComputeShader> CreateComputeShader(
        FrameworkDeviceContext& deviceContext,
        const void* shaderBytecode,
        const size_t shaderBytecodeSize)
    {
        const ShaderBlob shader(shaderBytecode, shaderBytecodeSize);
        return std::make_unique<ComputeShader>(
            deviceContext,
            shader,
            ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
    }
}

//Modify Begin:2026-08-23 by Hui
void AutoExposure::SetSettings(const Settings& settings)
{
    Settings sanitized = settings;
    sanitized.Tau = std::clamp(sanitized.Tau, MinAdaptationTau, MaxAdaptationTau);
    sanitized.MinLogLuminance = std::clamp(
        sanitized.MinLogLuminance,
        MinLogLuminanceLimit,
        MaxLogLuminanceLimit - MinimumLogLuminanceRange);
    sanitized.MaxLogLuminance = std::clamp(
        sanitized.MaxLogLuminance,
        MinLogLuminanceLimit + MinimumLogLuminanceRange,
        MaxLogLuminanceLimit);
    if (sanitized.MaxLogLuminance <= sanitized.MinLogLuminance)
    {
        sanitized.MaxLogLuminance = std::min(
            MaxLogLuminanceLimit,
            sanitized.MinLogLuminance + MinimumLogLuminanceRange);
        if (sanitized.MaxLogLuminance <= sanitized.MinLogLuminance)
        {
            sanitized.MinLogLuminance = sanitized.MaxLogLuminance - MinimumLogLuminanceRange;
        }
    }

    if (sanitized.Enabled != m_Settings.Enabled ||
        sanitized.Tau != m_Settings.Tau ||
        sanitized.MinLogLuminance != m_Settings.MinLogLuminance ||
        sanitized.MaxLogLuminance != m_Settings.MaxLogLuminance)
    {
        m_Settings = sanitized;
        m_HistoryValid = false;
    }
}

const AutoExposure::Settings& AutoExposure::GetSettings() const
{
    return m_Settings;
}
//Modify End

AutoExposure::AutoExposure(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
    , m_BuildHistogramShader(CreateComputeShader(
        deviceContext,
        ShaderBytecode_AutoExposureBuildHistogram_CS,
        sizeof ShaderBytecode_AutoExposureBuildHistogram_CS))
    , m_AverageHistogramShader(CreateComputeShader(
        deviceContext,
        ShaderBytecode_AutoExposureAverageHistogram_CS,
        sizeof ShaderBytecode_AutoExposureAverageHistogram_CS))
    , m_ApplyShader(CreateComputeShader(
        deviceContext,
        ShaderBytecode_AutoExposureApply_CS,
        sizeof ShaderBytecode_AutoExposureApply_CS))
{
}

void AutoExposure::ResetHistory()
{
    m_HistoryValid = false;
}

void AutoExposure::EnsureResources(const uint32_t outputWidth, const uint32_t outputHeight)
{
    if (m_Histogram == nullptr)
    {
        const auto histogramDesc = CD3DX12_RESOURCE_DESC::Buffer(
            HistogramBinCount * HistogramBinStride,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_Histogram = std::make_shared<ByteAddressBuffer>(
            histogramDesc,
            HistogramBinCount,
            HistogramBinStride,
            L"Auto Exposure Histogram",
            m_DeviceContext.GetD3D12DeviceContext());
        m_Histogram->SetAutoBarriersEnabled(false);
    }

    if (m_AdaptedLuminance == nullptr)
    {
        m_AdaptedLuminance = RenderTexture::CreateUav2D(
            m_DeviceContext,
            DXGI_FORMAT_R32_FLOAT,
            1u,
            1u,
            L"Auto Exposure Adapted Luminance");
        m_AdaptedLuminance->SetAutoBarriersEnabled(false);
    }

    if (m_OutputWidth != outputWidth || m_OutputHeight != outputHeight)
    {
        m_OutputWidth = outputWidth;
        m_OutputHeight = outputHeight;
        m_HistoryValid = false;
    }
}

void AutoExposure::Execute(
    CommandList& commandList,
    const std::shared_ptr<Texture>& source,
    const std::shared_ptr<Texture>& output,
    const uint32_t inputWidth,
    const uint32_t inputHeight,
    const uint32_t outputWidth,
    const uint32_t outputHeight,
    const float deltaTime)
{
    Assert(source != nullptr && source->IsValid(), "Auto exposure source texture is invalid.");
    Assert(output != nullptr && output->IsValid(), "Auto exposure output texture is invalid.");
    Assert(inputWidth > 0u && inputHeight > 0u, "Auto exposure input dimensions must be positive.");
    Assert(outputWidth > 0u && outputHeight > 0u, "Auto exposure output dimensions must be positive.");

    EnsureResources(outputWidth, outputHeight);

    AutoExposureConstants constants;
    constants.InputWidth = inputWidth;
    constants.InputHeight = inputHeight;
    constants.OutputWidth = outputWidth;
    constants.OutputHeight = outputHeight;
    constants.DeltaTime = std::max(deltaTime, 0.0f);
//Modify Begin:2026-08-23 by Hui
    constants.MinLogLuminance = m_Settings.MinLogLuminance;
    constants.LogLuminanceRange = m_Settings.MaxLogLuminance - m_Settings.MinLogLuminance;
    constants.Tau = m_Settings.Tau;
    constants.ExposureEnabled = m_Settings.Enabled ? 1u : 0u;
//Modify End

    CommandContext commandContext(commandList);

//Modify Begin:2026-08-23 by Hui
    const bool historyWasInvalid = !m_HistoryValid;
//Modify End
    if (historyWasInvalid)
    {
        commandList.TransitionBarrier(*m_AdaptedLuminance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandContext.ClearUnorderedAccessUint(
            *m_AdaptedLuminance,
            std::array<UINT, 4>{ 0x3f800000u, 0u, 0u, 0u }.data());
        m_HistoryValid = true;
    }

    if (m_Settings.Enabled)
    {
        commandList.TransitionBarrier(*m_Histogram, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandContext.ClearUnorderedAccessUint(*m_Histogram, std::array<UINT, 4>{ 0u, 0u, 0u, 0u }.data());
        if (!historyWasInvalid)
        {
            commandList.TransitionBarrier(*m_AdaptedLuminance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        commandContext.SetConstantBuffer(*m_BuildHistogramShader, "AutoExposureConstants", constants);
        commandContext.SetTexture(*m_BuildHistogramShader, "SourceColor", ShaderResourceView(source));
        commandContext.SetUnorderedAccessView(*m_BuildHistogramShader, "Histogram", UnorderedAccessView(m_Histogram));
        commandContext.BindPipeline(*m_BuildHistogramShader);
        commandContext.BindDescriptorSet(m_BuildHistogramShader->GetDescriptorSet());
        commandContext.Dispatch((inputWidth + 15u) / 16u, (inputHeight + 15u) / 16u, 1u);
        commandContext.UavBarrier(*m_Histogram);

        commandContext.SetConstantBuffer(*m_AverageHistogramShader, "AutoExposureConstants", constants);
        commandContext.SetUnorderedAccessView(*m_AverageHistogramShader, "Histogram", UnorderedAccessView(m_Histogram));
        commandContext.SetUnorderedAccessView(
            *m_AverageHistogramShader,
            "AdaptedLuminance",
            UnorderedAccessView(m_AdaptedLuminance));
        commandContext.BindPipeline(*m_AverageHistogramShader);
        commandContext.BindDescriptorSet(m_AverageHistogramShader->GetDescriptorSet());
        commandContext.Dispatch(1u, 1u, 1u);
        commandContext.UavBarrier(*m_AdaptedLuminance);
        commandList.TransitionBarrier(*m_AdaptedLuminance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    else if (historyWasInvalid)
    {
        commandList.TransitionBarrier(*m_AdaptedLuminance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    commandContext.SetConstantBuffer(*m_ApplyShader, "AutoExposureConstants", constants);
    commandContext.SetTexture(*m_ApplyShader, "SourceColor", ShaderResourceView(source));
    commandContext.SetTexture(*m_ApplyShader, "AdaptedLuminance", ShaderResourceView(m_AdaptedLuminance));
    commandContext.SetUnorderedAccessView(*m_ApplyShader, "OutputColor", UnorderedAccessView(output));
    commandContext.BindPipeline(*m_ApplyShader);
    commandContext.BindDescriptorSet(m_ApplyShader->GetDescriptorSet());
    commandContext.Dispatch((outputWidth + 7u) / 8u, (outputHeight + 7u) / 8u, 1u);
}
//Modify End
