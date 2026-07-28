//Modify Begin:2026-07-27 by BestHui
#include <Framework/SVGF.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <Framework/ComputeShader.h>
#include <Framework/RenderTexture.h>
#include <Framework/ShaderBlob.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/SVGFAtrous_CS.h>
#include <Framework/SVGFComposite_CS.h>
#include <Framework/SVGFTemporal_CS.h>
#include <Framework/UnorderedAccessView.h>

#include <algorithm>
#include <d3dx12.h>

namespace
{
    std::unique_ptr<ComputeShader> CreateReflectedComputeShader(const void* shaderBytecode, const size_t shaderBytecodeSize)
    {
        const ShaderBlob shader(shaderBytecode, shaderBytecodeSize);
        return std::make_unique<ComputeShader>(
            shader,
            ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
    }
}

SVGF::SVGF()
    : m_TemporalShader(CreateReflectedComputeShader(ShaderBytecode_SVGFTemporal_CS, sizeof ShaderBytecode_SVGFTemporal_CS))
    , m_AtrousShader(CreateReflectedComputeShader(ShaderBytecode_SVGFAtrous_CS, sizeof ShaderBytecode_SVGFAtrous_CS))
    , m_CompositeShader(CreateReflectedComputeShader(ShaderBytecode_SVGFComposite_CS, sizeof ShaderBytecode_SVGFComposite_CS))
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

//Modify Begin:2026-07-27 by BestHui
    m_HistoryColor[0] = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 0");
    m_HistoryColor[1] = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 1");
    m_HistoryMoments[0] = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 0");
    m_HistoryMoments[1] = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 1");
    m_TemporalColor = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Temporal Color");
    m_TemporalMoments = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF Temporal Moments");
    m_Variance = RenderTexture::CreateUav2D(DXGI_FORMAT_R16_FLOAT, width, height, L"SVGF Variance");
    m_AtrousPing = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Atrous Ping");
    m_AtrousPong = RenderTexture::CreateUav2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF Atrous Pong");
//Modify End
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

    m_TemporalShader->Bind(commandList);
    m_TemporalShader->SetConstantBuffer(commandList, "SVGFTemporalConstants", constants);
    commandList.SetTexture(*m_TemporalShader, "NoisyRadiance", ShaderResourceView(noisyRadiance));
    commandList.SetTexture(*m_TemporalShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandList.SetTexture(*m_TemporalShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandList.SetTexture(*m_TemporalShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandList.SetTexture(*m_TemporalShader, "MotionVector", ShaderResourceView(motionVector));
    commandList.SetTexture(*m_TemporalShader, "HistoryColor", ShaderResourceView(m_HistoryColor[previousIndex]));
    commandList.SetTexture(*m_TemporalShader, "HistoryMoments", ShaderResourceView(m_HistoryMoments[previousIndex]));
    m_TemporalShader->SetUnorderedAccessView(commandList, "TemporalColor", UnorderedAccessView(m_TemporalColor));
    m_TemporalShader->SetUnorderedAccessView(commandList, "TemporalMoments", UnorderedAccessView(m_TemporalMoments));
    m_TemporalShader->SetUnorderedAccessView(commandList, "Variance", UnorderedAccessView(m_Variance));
    m_TemporalShader->SetUnorderedAccessView(commandList, "OutHistoryColor", UnorderedAccessView(m_HistoryColor[nextIndex]));
    m_TemporalShader->SetUnorderedAccessView(commandList, "OutHistoryMoments", UnorderedAccessView(m_HistoryMoments[nextIndex]));
    m_TemporalShader->ApplyBindings(commandList);
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    m_HistoryIndex = nextIndex;
    m_HistoryValid = true;
}

//Modify Begin:2026-07-27 by BestHui
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

    m_AtrousShader->Bind(commandList);
    m_AtrousShader->SetConstantBuffer(commandList, "SVGFAtrousConstants", constants);
    commandList.SetTexture(*m_AtrousShader, "InputColor", ShaderResourceView(input));
    commandList.SetTexture(*m_AtrousShader, "Variance", ShaderResourceView(m_Variance));
    commandList.SetTexture(*m_AtrousShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandList.SetTexture(*m_AtrousShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandList.SetTexture(*m_AtrousShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    m_AtrousShader->SetUnorderedAccessView(commandList, "OutputColor", UnorderedAccessView(output));
    m_AtrousShader->ApplyBindings(commandList);
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
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
//Modify End

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

    m_CompositeShader->Bind(commandList);
    m_CompositeShader->SetConstantBuffer(commandList, "SVGFCompositeConstants", constants);
    commandList.SetTexture(*m_CompositeShader, "FilteredColor", ShaderResourceView(input));
    commandList.SetTexture(*m_CompositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    m_CompositeShader->SetUnorderedAccessView(commandList, "Output", UnorderedAccessView(output));
    m_CompositeShader->ApplyBindings(commandList);
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
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
