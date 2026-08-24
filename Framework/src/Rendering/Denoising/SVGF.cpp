//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/Denoising/SVGF.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <Framework/SVGFAtrous_CS.h>
#include <Framework/SVGFComposite_CS.h>
#include <Framework/SVGFTemporal_CS.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <algorithm>
#include <utility>

namespace
{
    constexpr FLOAT SvgfClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr bool SvgfScratchUsesDedicatedResources = true;

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

    struct SvgfTemporalPassData
    {
        SVGF* Feature = nullptr;
        std::shared_ptr<const SVGF::GraphInputs> Inputs;
        RenderGraph::ResourceId TemporalColor = 0;
        RenderGraph::ResourceId TemporalMoments = 0;
        RenderGraph::ResourceId Variance = 0;
    };

    struct SvgfAtrousPassData
    {
        SVGF* Feature = nullptr;
        std::shared_ptr<const SVGF::GraphInputs> Inputs;
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId Destination = 0;
        RenderGraph::ResourceId Variance = 0;
        uint32_t StepSize = 1;
        uint32_t Direction = 0;
    };

    struct SvgfCompositePassData
    {
        SVGF* Feature = nullptr;
        std::shared_ptr<const SVGF::GraphInputs> Inputs;
        RenderGraph::ResourceId Source = 0;
    };
}

SVGF::SVGF(FrameworkDeviceContext& deviceContext)
    : m_TemporalShader(CreateReflectedComputeShader(
        deviceContext,
        ShaderBytecode_SVGFTemporal_CS,
        sizeof ShaderBytecode_SVGFTemporal_CS))
    , m_AtrousShader(CreateReflectedComputeShader(
        deviceContext,
        ShaderBytecode_SVGFAtrous_CS,
        sizeof ShaderBytecode_SVGFAtrous_CS))
    , m_CompositeShader(CreateReflectedComputeShader(
        deviceContext,
        ShaderBytecode_SVGFComposite_CS,
        sizeof ShaderBytecode_SVGFComposite_CS))
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
    if (m_Width == width && m_Height == height && m_HistoryColor[0] != nullptr)
    {
        return true;
    }

    Assert(width > 0u && height > 0u, "SVGF history dimensions are invalid.");
    m_Width = width;
    m_Height = height;
    m_HistoryColor[0] = RenderTexture::CreateUav2D(
        m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 0");
    m_HistoryColor[1] = RenderTexture::CreateUav2D(
        m_DeviceContext, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, L"SVGF History Color 1");
    m_HistoryMoments[0] = RenderTexture::CreateUav2D(
        m_DeviceContext, DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 0");
    m_HistoryMoments[1] = RenderTexture::CreateUav2D(
        m_DeviceContext, DXGI_FORMAT_R16G16_FLOAT, width, height, L"SVGF History Moments 1");
    ResetHistory();
    return true;
}

void SVGF::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(m_Enabled, "SVGF graph passes require the feature to be enabled.");
    Assert(
        inputs.NoisyRadiance != 0u &&
        inputs.GBufferNormal != 0u &&
        inputs.GBufferPosition != 0u &&
        inputs.MotionVector != 0u &&
        inputs.Depth != 0u &&
        inputs.Output != 0u,
        "SVGF graph resources are invalid.");
    Assert(inputs.InputToken != 0u && inputs.OutputToken != 0u, "SVGF graph tokens are invalid.");
    Assert(inputs.Width > 0u && inputs.Height > 0u, "SVGF graph dimensions are invalid.");
    Assert(
        static_cast<bool>(inputs.WidthExpression) && static_cast<bool>(inputs.HeightExpression),
        "SVGF graph dimension expressions are invalid.");
    Assert(static_cast<bool>(inputs.ResolveFrameIndex), "SVGF requires a frame-index resolver.");
    Assert(!inputs.DiagnosticNamePrefix.empty(), "SVGF requires a diagnostic-name prefix.");

    EnsureCreated(inputs.Width, inputs.Height);
    ResetHistory();
    const auto sharedInputs = std::make_shared<const GraphInputs>(std::move(inputs));
    const auto frameParity = sharedInputs->ResolveFrameIndex;
    const auto importHistory = [&builder, this, frameParity, &sharedInputs](
        const wchar_t* suffix,
        std::shared_ptr<Texture> (SVGF::*history)[2],
        const bool writeHistory)
    {
        const std::wstring name = sharedInputs->DiagnosticNamePrefix + L"." + suffix;
        return builder.ImportResource(
            name.c_str(),
            [this, history, frameParity, writeHistory]() -> const Resource&
            {
                const uint32_t readIndex = static_cast<uint32_t>(frameParity() & 1ull);
                const uint32_t index = writeHistory ? 1u - readIndex : readIndex;
                return *(*this.*history)[index];
            });
    };

    const RenderGraph::ImportedResourceHandle historyColorRead =
        importHistory(L"HistoryColor.Read", &SVGF::m_HistoryColor, false);
    const RenderGraph::ImportedResourceHandle historyColorWrite =
        importHistory(L"HistoryColor.Write", &SVGF::m_HistoryColor, true);
    const RenderGraph::ImportedResourceHandle historyMomentsRead =
        importHistory(L"HistoryMoments.Read", &SVGF::m_HistoryMoments, false);
    const RenderGraph::ImportedResourceHandle historyMomentsWrite =
        importHistory(L"HistoryMoments.Write", &SVGF::m_HistoryMoments, true);

    const auto createScratchTexture = [&builder, &sharedInputs](
        const wchar_t* suffix,
        const DXGI_FORMAT format)
    {
        const std::wstring name = sharedInputs->DiagnosticNamePrefix + L"." + suffix;
        return builder.CreateTexture(
            name.c_str(),
            sharedInputs->WidthExpression,
            sharedInputs->HeightExpression,
            format,
            SvgfClearColor,
            RenderGraph::ResourceInitAction::Discard,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_HEAP_FLAG_NONE,
            SvgfScratchUsesDedicatedResources);
    };

    const RenderGraph::ResourceId temporalColor =
        createScratchTexture(L"TemporalColor", DXGI_FORMAT_R16G16B16A16_FLOAT);
    const RenderGraph::ResourceId temporalMoments =
        createScratchTexture(L"TemporalMoments", DXGI_FORMAT_R16G16_FLOAT);
    const RenderGraph::ResourceId variance =
        createScratchTexture(L"Variance", DXGI_FORMAT_R16_FLOAT);
    const RenderGraph::ResourceId atrousPing =
        createScratchTexture(L"AtrousPing", DXGI_FORMAT_R16G16B16A16_FLOAT);
    const RenderGraph::ResourceId atrousPong =
        createScratchTexture(L"AtrousPong", DXGI_FORMAT_R16G16B16A16_FLOAT);
    const std::wstring temporalTokenName = sharedInputs->DiagnosticNamePrefix + L".TemporalFinished";
    const RenderGraph::ResourceId temporalToken = builder.CreateToken(temporalTokenName.c_str());

    builder.AddPass<SvgfTemporalPassData>(
        L"SVGF Temporal",
        [this, sharedInputs, historyColorRead, historyColorWrite, historyMomentsRead, historyMomentsWrite,
            temporalColor, temporalMoments, variance, temporalToken](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            SvgfTemporalPassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passData.TemporalColor = temporalColor;
            passData.TemporalMoments = temporalMoments;
            passData.Variance = variance;
            passBuilder.ReadToken(sharedInputs->InputToken);
            passBuilder.ReadBuffer(sharedInputs->NoisyRadiance);
            passBuilder.ReadBuffer(sharedInputs->GBufferNormal);
            passBuilder.ReadBuffer(sharedInputs->GBufferPosition);
            passBuilder.ReadBuffer(sharedInputs->MotionVector);
            passBuilder.ReadBuffer(sharedInputs->Depth);
            passBuilder.ReadImported(historyColorRead, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.ReadImported(historyMomentsRead, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.WriteUav(temporalColor);
            passBuilder.WriteUav(temporalMoments);
            passBuilder.WriteUav(variance);
            passBuilder.WriteImported(historyColorWrite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            passBuilder.WriteImported(historyMomentsWrite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            passBuilder.WriteToken(temporalToken);
        },
        [](const SvgfTemporalPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            const uint32_t readIndex = static_cast<uint32_t>(passData.Inputs->ResolveFrameIndex() & 1ull);
            const uint32_t writeIndex = 1u - readIndex;
            passData.Feature->RecordTemporal(
                commandList,
                context.GetTexture(passData.Inputs->NoisyRadiance),
                context.GetTexture(passData.Inputs->GBufferNormal),
                context.GetTexture(passData.Inputs->GBufferPosition),
                context.GetTexture(passData.Inputs->MotionVector),
                context.GetTexture(passData.Inputs->Depth),
                passData.Feature->m_HistoryColor[readIndex],
                passData.Feature->m_HistoryMoments[readIndex],
                context.GetTexture(passData.TemporalColor),
                context.GetTexture(passData.TemporalMoments),
                context.GetTexture(passData.Variance),
                passData.Feature->m_HistoryColor[writeIndex],
                passData.Feature->m_HistoryMoments[writeIndex],
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight);
        });

    RenderGraph::ResourceId filtered = temporalColor;
    const uint32_t iterationCount = std::clamp(m_Settings.AtrousIterations, 1u, 8u);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        const uint32_t stepSize = 1u << iteration;
        const auto addAtrousPass = [this, &builder, sharedInputs, variance, stepSize](
            const wchar_t* passName,
            const RenderGraph::ResourceId source,
            const RenderGraph::ResourceId destination,
            const uint32_t direction)
        {
            builder.AddPass<SvgfAtrousPassData>(
                passName,
                [this, sharedInputs, source, destination, variance, stepSize, direction](
                    RenderGraph::RenderGraphPassBuilder& passBuilder,
                    SvgfAtrousPassData& passData)
                {
                    passData.Feature = this;
                    passData.Inputs = sharedInputs;
                    passData.Source = source;
                    passData.Destination = destination;
                    passData.Variance = variance;
                    passData.StepSize = stepSize;
                    passData.Direction = direction;
                    passBuilder.ReadBuffer(source);
                    passBuilder.ReadBuffer(variance);
                    passBuilder.ReadBuffer(sharedInputs->GBufferNormal);
                    passBuilder.ReadBuffer(sharedInputs->GBufferPosition);
                    passBuilder.ReadBuffer(sharedInputs->Depth);
                    passBuilder.WriteUav(destination);
                },
                [](const SvgfAtrousPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
                {
                    passData.Feature->RecordAtrous(
                        commandList,
                        context.GetTexture(passData.Source),
                        context.GetTexture(passData.Destination),
                        context.GetTexture(passData.Variance),
                        context.GetTexture(passData.Inputs->GBufferNormal),
                        context.GetTexture(passData.Inputs->GBufferPosition),
                        context.GetTexture(passData.Inputs->Depth),
                        context.GetMetadata().m_ScreenWidth,
                        context.GetMetadata().m_ScreenHeight,
                        passData.StepSize,
                        passData.Direction);
                });
        };

        const std::wstring horizontalName = L"SVGF A-Trous Horizontal " + std::to_wstring(iteration);
        const std::wstring verticalName = L"SVGF A-Trous Vertical " + std::to_wstring(iteration);
        addAtrousPass(horizontalName.c_str(), filtered, atrousPing, 0u);
        addAtrousPass(verticalName.c_str(), atrousPing, atrousPong, 1u);
        filtered = atrousPong;
    }

    builder.AddPass<SvgfCompositePassData>(
        L"SVGF Composite",
        [this, sharedInputs, filtered, temporalToken](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            SvgfCompositePassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passData.Source = filtered;
            passBuilder.ReadToken(temporalToken);
            passBuilder.ReadBuffer(filtered);
            passBuilder.ReadBuffer(sharedInputs->Depth);
            passBuilder.WriteUav(sharedInputs->Output);
            passBuilder.WriteToken(sharedInputs->OutputToken);
        },
        [](const SvgfCompositePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordComposite(
                commandList,
                context.GetTexture(passData.Source),
                context.GetTexture(passData.Inputs->Depth),
                context.GetTexture(passData.Inputs->Output),
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight);
        });
}

void SVGF::RecordTemporal(
    CommandList& commandList,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& historyColor,
    const std::shared_ptr<Texture>& historyMoments,
    const std::shared_ptr<Texture>& temporalColor,
    const std::shared_ptr<Texture>& temporalMoments,
    const std::shared_ptr<Texture>& variance,
    const std::shared_ptr<Texture>& outputHistoryColor,
    const std::shared_ptr<Texture>& outputHistoryMoments,
    const uint32_t width,
    const uint32_t height)
{
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
    commandContext.SetTexture(*m_TemporalShader, "HistoryColor", ShaderResourceView(historyColor));
    commandContext.SetTexture(*m_TemporalShader, "HistoryMoments", ShaderResourceView(historyMoments));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "TemporalColor", UnorderedAccessView(temporalColor));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "TemporalMoments", UnorderedAccessView(temporalMoments));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "Variance", UnorderedAccessView(variance));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "OutHistoryColor", UnorderedAccessView(outputHistoryColor));
    commandContext.SetUnorderedAccessView(*m_TemporalShader, "OutHistoryMoments", UnorderedAccessView(outputHistoryMoments));
    commandContext.BindPipeline(*m_TemporalShader);
    commandContext.BindDescriptorSet(m_TemporalShader->GetDescriptorSet());
    commandContext.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    m_HistoryValid = true;
}

void SVGF::RecordAtrous(
    CommandList& commandList,
    const std::shared_ptr<Texture>& input,
    const std::shared_ptr<Texture>& output,
    const std::shared_ptr<Texture>& variance,
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
    commandContext.SetTexture(*m_AtrousShader, "Variance", ShaderResourceView(variance));
    commandContext.SetTexture(*m_AtrousShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandContext.SetTexture(*m_AtrousShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandContext.SetTexture(*m_AtrousShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandContext.SetUnorderedAccessView(*m_AtrousShader, "OutputColor", UnorderedAccessView(output));
    commandContext.BindPipeline(*m_AtrousShader);
    commandContext.BindDescriptorSet(m_AtrousShader->GetDescriptorSet());
    commandContext.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

void SVGF::RecordComposite(
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
//Modify End
