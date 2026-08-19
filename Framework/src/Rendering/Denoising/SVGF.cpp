//Modify Begin:2026-08-12 by Hui
#include <Framework/Rendering/Denoising/SVGF.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/SVGFAtrous_CS.h>
#include <Framework/SVGFComposite_CS.h>
#include <Framework/SVGFTemporal_CS.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <algorithm>
#include <d3dx12/d3dx12.h>

namespace
{
    std::unique_ptr<ComputeShader> CreateReflectedComputeShader(
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

SVGF::SVGF(FrameworkDeviceContext& deviceContext)
    : m_TemporalShader(CreateReflectedComputeShader(deviceContext, ShaderBytecode_SVGFTemporal_CS, sizeof ShaderBytecode_SVGFTemporal_CS))
    , m_AtrousShader(CreateReflectedComputeShader(deviceContext, ShaderBytecode_SVGFAtrous_CS, sizeof ShaderBytecode_SVGFAtrous_CS))
    , m_CompositeShader(CreateReflectedComputeShader(deviceContext, ShaderBytecode_SVGFComposite_CS, sizeof ShaderBytecode_SVGFComposite_CS))
    , m_DeviceContext(deviceContext)
{
}

SVGF::~SVGF() = default;

void SVGF::ResetHistory()
{
    m_HistoryValid = false;
}

bool SVGF::EnsureCreated(const uint32_t width, const uint32_t height)
{
    if (m_Width == width && m_Height == height && m_TemporalColor != nullptr)
    {
        return true;
    }

    m_Width = width;
    m_Height = height;
    m_HistoryIndex = 0;
    m_HistoryValid = false;

    m_HistoryColor[0] = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 0");
    m_HistoryColor[1] = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 1");
    m_HistoryMoments[0] = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 0");
    m_HistoryMoments[1] = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 1");
    m_TemporalColor = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Temporal Color");
    m_TemporalMoments = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF Temporal Moments");
    m_Variance = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16_FLOAT, width, height, L"SVGF Variance");
    m_AtrousPing = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Atrous Ping");
    m_AtrousPong = RenderTexture::CreateUav2D(m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Atrous Pong");
    return true;
}

void SVGF::Temporal(
    CommandList& commandList,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& depthTexture,
    const uint32_t width,
    const uint32_t height)
{
    const uint32_t previousIndex = m_HistoryIndex;
    const uint32_t nextIndex = 1u - m_HistoryIndex;

    TemporalConstants constants = {};
    constants.Width = width;
    constants.Height = height;
    constants.ResetHistory = m_HistoryValid ? 0u : 1u;
    constants.TemporalAlpha = std::clamp(m_Settings.TemporalAlpha, 0.001f, 1.0f);
    constants.MomentsAlpha = std::clamp(m_Settings.MomentsAlpha, 0.001f, 1.0f);
    constants.PhiNormal = m_Settings.PhiNormal;
    constants.PhiDepth = m_Settings.PhiDepth;

    const CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_TemporalShader, "SVGFTemporalConstants", constants);
    commandContext.SetTexture(*m_TemporalShader, "NoisyRadiance", ShaderResourceView(noisyRadiance));
    commandContext.SetTexture(*m_TemporalShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandContext.SetTexture(*m_TemporalShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandContext.SetTexture(*m_TemporalShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandContext.SetTexture(*m_TemporalShader, "MotionVector", ShaderResourceView(motionVector));
    commandContext.SetTexture(*m_TemporalShader, "HistoryColor", ShaderResourceView(m_HistoryColor[previousIndex]));
    commandContext.SetTexture(*m_TemporalShader, "HistoryMoments", ShaderResourceView(m_HistoryMoments[previousIndex]));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "TemporalColor", UnorderedAccessView(m_TemporalColor));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "TemporalMoments", UnorderedAccessView(m_TemporalMoments));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "Variance", UnorderedAccessView(m_Variance));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "OutHistoryColor", UnorderedAccessView(m_HistoryColor[nextIndex]));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "OutHistoryMoments", UnorderedAccessView(m_HistoryMoments[nextIndex]));
    commandContext.BindPipeline(*m_TemporalShader);
    commandContext.BindDescriptorSet(m_TemporalShader->GetDescriptorSet());
    commandContext.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    m_HistoryIndex = nextIndex;
    m_HistoryValid = true;
}

void SVGF::AtrousPass(
    CommandList& commandList,
    const std::shared_ptr<Texture>& input,
    const std::shared_ptr<Texture>& output,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& depthTexture,
    const uint32_t width,
    const uint32_t height,
    const uint32_t stepSize,
    const uint32_t direction)
{
    AtrousConstants constants = {};
    constants.Width = width;
    constants.Height = height;
    constants.StepSize = stepSize;
    constants.Direction = direction;
    constants.PhiColor = m_Settings.PhiColor;
    constants.PhiNormal = m_Settings.PhiNormal;
    constants.PhiDepth = m_Settings.PhiDepth;

    const CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_AtrousShader, "SVGFAtrousConstants", constants);
    commandContext.SetTexture(*m_AtrousShader, "InputColor", ShaderResourceView(input));
    commandContext.SetTexture(*m_AtrousShader, "Variance", ShaderResourceView(m_Variance));
    commandContext.SetTexture(*m_AtrousShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandContext.SetTexture(*m_AtrousShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandContext.SetTexture(*m_AtrousShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandContext.SetUnorderedAccessView(*m_AtrousShader, "OutputColor", UnorderedAccessView(output));
    commandContext.BindPipeline(*m_AtrousShader);
    commandContext.BindDescriptorSet(m_AtrousShader->GetDescriptorSet());
    commandContext.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

std::shared_ptr<Texture> SVGF::Atrous(
    CommandList& commandList,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& depthTexture,
    const uint32_t width,
    const uint32_t height)
{
    std::shared_ptr<Texture> input = m_TemporalColor;
    const uint32_t iterationCount = std::clamp(m_Settings.AtrousIterations, 1u, 8u);

    for (uint32_t iteration = 0; iteration < iterationCount; ++iteration)
    {
        const uint32_t stepSize = 1u << iteration;
        const std::shared_ptr<Texture> horizontal = (iteration % 2u) == 0u ? m_AtrousPing : m_AtrousPong;
        const std::shared_ptr<Texture> vertical = (iteration % 2u) == 0u ? m_AtrousPong : m_AtrousPing;

        AtrousPass(commandList, input, horizontal, gBufferNormal, gBufferPosition, depthTexture, width, height, stepSize, 0u);
        AtrousPass(commandList, horizontal, vertical, gBufferNormal, gBufferPosition, depthTexture, width, height, stepSize, 1u);
        input = vertical;
    }

    return input;
}

void SVGF::Composite(
    CommandList& commandList,
    const std::shared_ptr<Texture>& input,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    CompositeConstants constants = {};
    constants.Width = width;
    constants.Height = height;

    const CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_CompositeShader, "SVGFCompositeConstants", constants);
    commandContext.SetTexture(*m_CompositeShader, "FilteredColor", ShaderResourceView(input));
    commandContext.SetTexture(*m_CompositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandContext.SetUnorderedAccessView(*m_CompositeShader, "Output", UnorderedAccessView(output));
    commandContext.BindPipeline(*m_CompositeShader);
    commandContext.BindDescriptorSet(m_CompositeShader->GetDescriptorSet());
    commandContext.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

void SVGF::Execute(
    CommandList& commandList,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    if (!m_Enabled || !EnsureCreated(width, height))
    {
        return;
    }

    Temporal(commandList, noisyRadiance, gBufferNormal, gBufferPosition, motionVector, depthTexture, width, height);
    const std::shared_ptr<Texture> filtered = Atrous(commandList, gBufferNormal, gBufferPosition, depthTexture, width, height);
    Composite(commandList, filtered, depthTexture, output, width, height);
}
//Modify End
