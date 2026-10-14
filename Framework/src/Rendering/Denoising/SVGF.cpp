//Modify Begin:2026-10-14 by Hui
#include <Framework/Rendering/Denoising/SVGF.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <Framework/SVGFPrepare_CS.h>
#include <Framework/SVGFTemporal_CS.h>
#include <Framework/SVGFFilterMoments_CS.h>
#include <Framework/SVGFAtrous_CS.h>
#include <Framework/SVGFComposite_CS.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <algorithm>
#include <array>
#include <utility>
#include <unordered_set>

namespace
{
    std::unique_ptr<ComputeShader> CreateShader(FrameworkDeviceContext& context, const void* data, size_t size)
    {
        const ShaderBlob blob(data, size);
        return std::make_unique<ComputeShader>(context, blob, ComputePipelineDescBuilder::ReflectedDefault(blob).Build());
    }

    enum class SVGFStage { Prepare, Temporal, Moments, Atrous, Feedback, Composite };

    struct SVGFPassData
    {
        std::shared_ptr<const SVGF::GraphInputs> Inputs;
        std::array<RenderGraph::ResourceId, 4> Scratch{};
        std::array<RenderGraph::ImportedResourceHandle, 4> ReadHistory, WriteHistory;
        SVGFStage Stage = SVGFStage::Prepare;
        uint32_t StepSize = 1;
        int32_t FeedbackIndex = -1;
        RenderGraph::ResourceId FilterInput = 0, FilterOutput = 0;
    };

    void Dispatch(CommandContext& context, ComputeShader& shader, uint32_t width, uint32_t height)
    {
        context.BindPipeline(shader);
        context.BindDescriptorSet(shader.GetDescriptorSet());
        context.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    }
}

SVGF::SVGF(FrameworkDeviceContext& deviceContext)
    : m_PrepareShader(CreateShader(deviceContext, ShaderBytecode_SVGFPrepare_CS, sizeof ShaderBytecode_SVGFPrepare_CS))
    , m_TemporalShader(CreateShader(deviceContext, ShaderBytecode_SVGFTemporal_CS, sizeof ShaderBytecode_SVGFTemporal_CS))
    , m_FilterMomentsShader(CreateShader(deviceContext, ShaderBytecode_SVGFFilterMoments_CS, sizeof ShaderBytecode_SVGFFilterMoments_CS))
    , m_AtrousShader(CreateShader(deviceContext, ShaderBytecode_SVGFAtrous_CS, sizeof ShaderBytecode_SVGFAtrous_CS))
    , m_CompositeShader(CreateShader(deviceContext, ShaderBytecode_SVGFComposite_CS, sizeof ShaderBytecode_SVGFComposite_CS))
    , m_DeviceContext(deviceContext)
{
}

SVGF::~SVGF() = default;

void SVGF::ResetHistory()
{
    m_HistoryValid = false;
}

bool SVGF::EnsureCreated(uint32_t width, uint32_t height)
{
    if (m_Width == width && m_Height == height && m_HistoryColor[0]) return true;
    Assert(width > 0u && height > 0u, "SVGF history dimensions are invalid.");
    m_Width = width;
    m_Height = height;
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto create = [&](DXGI_FORMAT format, const wchar_t* suffix)
        {
            const std::wstring name = std::wstring(L"SVGF History ") + suffix + L" " + std::to_wstring(i);
            return RenderTexture::CreateUav2D(m_DeviceContext, format, width, height, name.c_str());
        };
        // Squared HDR luminance requires full precision, especially after demodulation.
        m_HistoryColor[i] = create(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Color");
        m_HistoryMoments[i] = create(DXGI_FORMAT_R32G32_FLOAT, L"Moments");
        m_HistorySurface[i] = create(DXGI_FORMAT_R32G32B32A32_FLOAT, L"Surface");
        m_HistoryLength[i] = create(DXGI_FORMAT_R16_FLOAT, L"Length");
    }
    ResetHistory();
    return true;
}

void SVGF::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(m_Enabled, "SVGF graph registration requires the feature to be enabled.");
    Assert(inputs.NoisyRadiance && inputs.GBufferNormal && inputs.GBufferPosition &&
        inputs.GBufferAlbedoOcclusion && inputs.GBufferEmissionMetallic && inputs.MotionVector &&
        inputs.Depth && inputs.Output && inputs.InputToken && inputs.OutputToken,
        "SVGF graph resources are invalid.");
    Assert(inputs.WidthExpression && inputs.HeightExpression && inputs.ResolveFrameIndex &&
        inputs.ResolveWorldToView && !inputs.DiagnosticNamePrefix.empty(), "SVGF graph callbacks are invalid.");
    EnsureCreated(inputs.Width, inputs.Height);
    SVGFPassData data;
    data.Inputs = std::make_shared<const GraphInputs>(std::move(inputs));
    const auto frameParity = data.Inputs->ResolveFrameIndex;
    using HistoryArray = std::shared_ptr<Texture> (SVGF::*)[2];
    const std::array<HistoryArray, 4> histories{
        &SVGF::m_HistoryColor, &SVGF::m_HistoryMoments, &SVGF::m_HistorySurface, &SVGF::m_HistoryLength };
    const wchar_t* historyNames[]{ L"HistoryColor", L"HistoryMoments", L"HistorySurface", L"HistoryLength" };
    for (size_t i = 0; i < histories.size(); ++i)
    {
        const auto history = histories[i];
        for (uint32_t write = 0; write < 2; ++write)
        {
            const std::wstring name = data.Inputs->DiagnosticNamePrefix + L"." + historyNames[i] +
                (write ? L".Write" : L".Read");
            auto handle = builder.ImportResource(name.c_str(), [this, frameParity, history, write]() -> const Resource&
            {
                const uint32_t readIndex = static_cast<uint32_t>(frameParity() & 1ull);
                return *(*this.*history)[write ? 1u - readIndex : readIndex];
            });
            (write ? data.WriteHistory : data.ReadHistory)[i] = std::move(handle);
        }
    }
    const wchar_t* scratchNames[]{ L"Illumination", L"TemporalColor", L"Ping", L"Pong" };
    constexpr FLOAT clear[]{ 0.0f, 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < data.Scratch.size(); ++i)
    {
        const std::wstring name = data.Inputs->DiagnosticNamePrefix + L"." + scratchNames[i];
        data.Scratch[i] = builder.CreateTexture(name.c_str(), data.Inputs->WidthExpression,
            data.Inputs->HeightExpression, DXGI_FORMAT_R32G32B32A32_FLOAT, clear,
            RenderGraph::ResourceInitAction::Discard, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_HEAP_FLAG_NONE, true);
    }
    RenderGraph::ResourceId previousToken = data.Inputs->InputToken;
    const auto addStage = [&](const wchar_t* name, SVGFStage stage,
        RenderGraph::ResourceId filterInput = 0, RenderGraph::ResourceId filterOutput = 0,
        uint32_t stepSize = 1, int32_t feedbackIndex = -1)
    {
        const std::wstring tokenName = data.Inputs->DiagnosticNamePrefix + L"." + name +
            L".Finished" + std::to_wstring(stepSize);
        const auto outputToken = stage == SVGFStage::Composite ? data.Inputs->OutputToken : builder.CreateToken(tokenName.c_str());
        SVGFPassData stageData = data;
        stageData.Stage = stage;
        stageData.FilterInput = filterInput;
        stageData.FilterOutput = filterOutput;
        stageData.StepSize = stepSize;
        stageData.FeedbackIndex = feedbackIndex;
        builder.AddPass<SVGFPassData>(name,
            [stageData, inputToken = previousToken, outputToken](RenderGraph::RenderGraphPassBuilder& pass,
                SVGFPassData& passData)
            {
                passData = stageData;
                const auto& d = stageData;
                const auto& in = *d.Inputs;
                pass.ReadToken(inputToken);
                pass.WriteToken(outputToken);
                const auto readSurface = [&]()
                {
                    pass.ReadImported(d.WriteHistory[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                };
                switch (d.Stage)
                {
                case SVGFStage::Prepare:
                    for (const auto id : { in.NoisyRadiance, in.GBufferNormal, in.GBufferPosition,
                        in.GBufferAlbedoOcclusion, in.GBufferEmissionMetallic, in.Depth }) pass.ReadBuffer(id);
                    pass.WriteUav(d.Scratch[0]);
                    pass.WriteImported(d.WriteHistory[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    break;
                case SVGFStage::Temporal:
                    pass.ReadBuffer(d.Scratch[0]);
                    pass.ReadBuffer(in.GBufferPosition);
                    pass.ReadBuffer(in.MotionVector);
                    readSurface();
                    for (const auto& handle : d.ReadHistory)
                        pass.ReadImported(handle, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    pass.WriteUav(d.Scratch[1]);
                    pass.WriteImported(d.WriteHistory[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    pass.WriteImported(d.WriteHistory[3], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    break;
                case SVGFStage::Moments:
                    pass.ReadImported(d.WriteHistory[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    pass.ReadImported(d.WriteHistory[3], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    [[fallthrough]];
                case SVGFStage::Atrous:
                    readSurface();
                    pass.ReadBuffer(d.FilterInput);
                    pass.WriteUav(d.FilterOutput);
                    break;
                case SVGFStage::Feedback:
                    pass.ReadCopySource(d.FilterInput);
                    pass.WriteImported(d.WriteHistory[0], D3D12_RESOURCE_STATE_COPY_DEST);
                    break;
                case SVGFStage::Composite:
                    pass.ReadBuffer(d.FilterInput);
                    pass.ReadBuffer(in.Depth);
                    pass.ReadBuffer(in.GBufferAlbedoOcclusion);
                    pass.ReadBuffer(in.GBufferEmissionMetallic);
                    pass.WriteUav(in.Output);
                    break;
                }
            },
            [this](const SVGFPassData& data, const RenderGraph::RenderContext& context,
                RenderGraph::RenderPassContext& passContext)
            {
                const auto& input = *data.Inputs;
                const uint32_t readIndex = static_cast<uint32_t>(input.ResolveFrameIndex() & 1ull);
                const uint32_t writeIndex = 1u - readIndex;
                RecordingResources resources;
                resources.NoisyRadiance = context.GetTexture(input.NoisyRadiance);
                resources.GBufferNormal = context.GetTexture(input.GBufferNormal);
                resources.GBufferPosition = context.GetTexture(input.GBufferPosition);
                resources.GBufferAlbedoOcclusion = context.GetTexture(input.GBufferAlbedoOcclusion);
                resources.GBufferEmissionMetallic = context.GetTexture(input.GBufferEmissionMetallic);
                resources.MotionVector = context.GetTexture(input.MotionVector);
                resources.Depth = context.GetTexture(input.Depth);
                resources.Output = context.GetTexture(input.Output);
                resources.HistoryColorRead = m_HistoryColor[readIndex];
                resources.HistoryMomentsRead = m_HistoryMoments[readIndex];
                resources.HistorySurfaceRead = m_HistorySurface[readIndex];
                resources.HistoryLengthRead = m_HistoryLength[readIndex];
                resources.HistoryColorWrite = m_HistoryColor[writeIndex];
                resources.HistoryMomentsWrite = m_HistoryMoments[writeIndex];
                resources.HistorySurfaceWrite = m_HistorySurface[writeIndex];
                resources.HistoryLengthWrite = m_HistoryLength[writeIndex];
                resources.Illumination = context.GetTexture(data.Scratch[0]);
                resources.TemporalColor = context.GetTexture(data.Scratch[1]);
                resources.Ping = context.GetTexture(data.Scratch[2]);
                resources.Pong = context.GetTexture(data.Scratch[3]);
                resources.WorldToView = input.ResolveWorldToView();
                resources.PreviousWorldToView = m_HistoryValid ? m_PreviousView : resources.WorldToView;
                resources.HistoryValid = m_HistoryValid;
                CommandContext commandContext(passContext.GetCommandList(), passContext.GetBarrierContext());
                const uint32_t width = input.Width, height = input.Height;
                switch (data.Stage)
                {
                case SVGFStage::Prepare: RecordPrepare(commandContext, resources, width, height); break;
                case SVGFStage::Temporal: RecordTemporal(commandContext, resources, width, height); break;
                case SVGFStage::Moments:
                case SVGFStage::Atrous:
                    RecordFilter(commandContext, resources, context.GetTexture(data.FilterInput),
                        context.GetTexture(data.FilterOutput), width, height, data.StepSize, data.Stage == SVGFStage::Moments);
                    break;
                case SVGFStage::Feedback:
                {
                    const int32_t feedback = std::clamp(m_Settings.FeedbackTap, -1,
                        static_cast<int32_t>(std::clamp(m_Settings.AtrousIterations, 1u, 8u)) - 1);
                    if (feedback == data.FeedbackIndex)
                        commandContext.CopyResource(*resources.HistoryColorWrite, *context.GetTexture(data.FilterInput));
                    break;
                }
                case SVGFStage::Composite:
                    RecordComposite(commandContext, resources, context.GetTexture(data.FilterInput), width, height);
                    m_PreviousView = resources.WorldToView;
                    m_HistoryValid = true;
                    break;
                }
            });
        previousToken = outputToken;
    };
    addStage(L"SVGF Prepare", SVGFStage::Prepare);
    addStage(L"SVGF Temporal", SVGFStage::Temporal);
    // Feedback passes stay in the graph so the selected tap can change at runtime.
    addStage(L"SVGF Feedback Temporal", SVGFStage::Feedback, data.Scratch[1]);
    addStage(L"SVGF Filter Moments", SVGFStage::Moments, data.Scratch[1], data.Scratch[2]);
    auto filtered = data.Scratch[2];
    const uint32_t iterations = std::clamp(m_Settings.AtrousIterations, 1u, 8u);
    for (uint32_t i = 0; i < iterations; ++i)
    {
        const auto output = data.Scratch[(i & 1u) == 0u ? 3u : 2u];
        const std::wstring name = L"SVGF A-Trous " + std::to_wstring(i);
        addStage(name.c_str(), SVGFStage::Atrous, filtered, output, 1u << i);
        filtered = output;
        const std::wstring feedbackName = L"SVGF Feedback " + std::to_wstring(i);
        addStage(feedbackName.c_str(), SVGFStage::Feedback, filtered, 0, 1u, static_cast<int32_t>(i));
    }
    addStage(L"SVGF Composite", SVGFStage::Composite, filtered);
}

void SVGF::Record(CommandContext& context, const RecordingResources& r)
{
    if (!m_Enabled) return;
    const std::shared_ptr<Texture> textures[]{ r.NoisyRadiance, r.GBufferNormal, r.GBufferPosition,
        r.GBufferAlbedoOcclusion, r.GBufferEmissionMetallic, r.MotionVector, r.Depth,
        r.Output, r.HistoryColorRead, r.HistoryMomentsRead, r.HistorySurfaceRead, r.HistoryLengthRead,
        r.HistoryColorWrite, r.HistoryMomentsWrite, r.HistorySurfaceWrite, r.HistoryLengthWrite,
        r.Illumination, r.TemporalColor, r.Ping, r.Pong };
    Assert(r.NoisyRadiance && r.NoisyRadiance->IsValid(), "SVGF requires noisy radiance.");
    const auto extent = r.NoisyRadiance->GetD3D12Resource()->GetDesc();
    std::unordered_set<ID3D12Resource*> identities;
    for (size_t i = 0; i < std::size(textures); ++i)
    {
        const auto& texture = textures[i];
        Assert(texture && texture->IsValid(), "SVGF recording resources must be allocated before recording.");
        const auto desc = texture->GetD3D12Resource()->GetDesc();
        Assert(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.MipLevels == 1 &&
            desc.DepthOrArraySize == 1 && desc.SampleDesc.Count == 1 &&
            desc.Width == extent.Width && desc.Height == extent.Height,
            "SVGF requires equally sized single-mip, single-sample 2D textures.");
        Assert(identities.insert(texture->GetD3D12Resource().Get()).second, "SVGF recording textures must not alias.");
        Assert(i < 7 || texture->SupportsUnorderedAccess(), "SVGF recording histories and outputs require UAV usage.");
    }
    const uint32_t width = static_cast<uint32_t>(extent.Width), height = extent.Height;
    RecordPrepare(context, r, width, height);
    RecordTemporal(context, r, width, height);
    RecordFilter(context, r, r.TemporalColor, r.Ping, width, height, 1u, true);
    auto filtered = r.Ping;
    const uint32_t iterations = std::clamp(m_Settings.AtrousIterations, 1u, 8u);
    const int32_t feedback = std::clamp(m_Settings.FeedbackTap, -1, static_cast<int32_t>(iterations) - 1);
    const auto saveHistory = [&](const std::shared_ptr<Texture>& source)
    {
        context.UseResource(*source, D3D12_RESOURCE_STATE_COPY_SOURCE, ResourceUse::Read);
        context.UseResource(*r.HistoryColorWrite, D3D12_RESOURCE_STATE_COPY_DEST, ResourceUse::Write);
        context.FlushBarriers();
        context.CopyResource(*r.HistoryColorWrite, *source);
        context.UseResource(*source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, ResourceUse::Read);
        context.UseResource(*r.HistoryColorWrite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ResourceUse::Write);
        context.FlushBarriers();
    };
    if (feedback < 0) saveHistory(r.TemporalColor);
    for (uint32_t i = 0; i < iterations; ++i)
    {
        const auto output = (i & 1u) == 0u ? r.Pong : r.Ping;
        RecordFilter(context, r, filtered, output, width, height, 1u << i, false);
        filtered = output;
        // Feed back an early wavelet level rather than the most blurred level.
        if (static_cast<int32_t>(i) == feedback) saveHistory(filtered);
    }
    RecordComposite(context, r, filtered, width, height);
}

void SVGF::RecordPrepare(CommandContext& context, const RecordingResources& r, uint32_t width, uint32_t height)
{
    PrepareConstants constants;
    constants.WorldToView = r.WorldToView;
    constants.Width = width;
    constants.Height = height;
    auto& shader = *m_PrepareShader;
    context.SetConstantBuffer(shader, "SVGFPrepareConstants", constants);
    context.SetTexture(shader, "NoisyRadiance", ShaderResourceView(r.NoisyRadiance));
    context.SetTexture(shader, "GBufferNormal", ShaderResourceView(r.GBufferNormal));
    context.SetTexture(shader, "GBufferPosition", ShaderResourceView(r.GBufferPosition));
    context.SetTexture(shader, "DepthTexture", ShaderResourceView::DepthAsFloat(r.Depth));
    context.SetTexture(shader, "GBufferAlbedoOcclusion", ShaderResourceView(r.GBufferAlbedoOcclusion));
    context.SetTexture(shader, "GBufferEmissionMetallic", ShaderResourceView(r.GBufferEmissionMetallic));
    context.SetUnorderedAccessView(shader, "Illumination", UnorderedAccessView(r.Illumination));
    context.SetUnorderedAccessView(shader, "CurrentSurface", UnorderedAccessView(r.HistorySurfaceWrite));
    Dispatch(context, shader, width, height);
}

void SVGF::RecordTemporal(CommandContext& context, const RecordingResources& r, uint32_t width, uint32_t height)
{
    TemporalConstants constants;
    constants.PreviousWorldToView = r.PreviousWorldToView;
    constants.Width = width;
    constants.Height = height;
    constants.ResetHistory = r.HistoryValid ? 0u : 1u;
    constants.TemporalAlpha = std::clamp(m_Settings.TemporalAlpha, 0.001f, 1.0f);
    constants.MomentsAlpha = std::clamp(m_Settings.MomentsAlpha, 0.001f, 1.0f);
    auto& shader = *m_TemporalShader;
    context.SetConstantBuffer(shader, "SVGFTemporalConstants", constants);
    context.SetTexture(shader, "Illumination", ShaderResourceView(r.Illumination));
    context.SetTexture(shader, "CurrentSurface", ShaderResourceView(r.HistorySurfaceWrite));
    context.SetTexture(shader, "GBufferPosition", ShaderResourceView(r.GBufferPosition));
    context.SetTexture(shader, "MotionVector", ShaderResourceView(r.MotionVector));
    context.SetTexture(shader, "HistoryColor", ShaderResourceView(r.HistoryColorRead));
    context.SetTexture(shader, "HistoryMoments", ShaderResourceView(r.HistoryMomentsRead));
    context.SetTexture(shader, "HistorySurface", ShaderResourceView(r.HistorySurfaceRead));
    context.SetTexture(shader, "HistoryLength", ShaderResourceView(r.HistoryLengthRead));
    context.SetUnorderedAccessView(shader, "TemporalColor", UnorderedAccessView(r.TemporalColor));
    context.SetUnorderedAccessView(shader, "OutHistoryMoments", UnorderedAccessView(r.HistoryMomentsWrite));
    context.SetUnorderedAccessView(shader, "OutHistoryLength", UnorderedAccessView(r.HistoryLengthWrite));
    Dispatch(context, shader, width, height);
}

void SVGF::RecordFilter(CommandContext& context, const RecordingResources& r,
    const std::shared_ptr<Texture>& input, const std::shared_ptr<Texture>& output,
    uint32_t width, uint32_t height, uint32_t stepSize, bool filterMoments)
{
    FilterConstants constants;
    constants.Width = width;
    constants.Height = height;
    constants.StepSize = stepSize;
    constants.PhiColor = std::max(m_Settings.PhiColor, 0.001f);
    constants.PhiNormal = std::max(m_Settings.PhiNormal, 0.001f);
    constants.PhiDepth = std::max(m_Settings.PhiDepth, 0.001f);
    auto& shader = filterMoments ? *m_FilterMomentsShader : *m_AtrousShader;
    context.SetConstantBuffer(shader, "SVGFFilterConstants", constants);
    context.SetTexture(shader, "InputColor", ShaderResourceView(input));
    context.SetTexture(shader, "CurrentSurface", ShaderResourceView(r.HistorySurfaceWrite));
    if (filterMoments)
    {
        context.SetTexture(shader, "Moments", ShaderResourceView(r.HistoryMomentsWrite));
        context.SetTexture(shader, "HistoryLength", ShaderResourceView(r.HistoryLengthWrite));
    }
    context.SetUnorderedAccessView(shader, "OutputColor", UnorderedAccessView(output));
    Dispatch(context, shader, width, height);
}

void SVGF::RecordComposite(CommandContext& context, const RecordingResources& r,
    const std::shared_ptr<Texture>& input, uint32_t width, uint32_t height)
{
    CompositeConstants constants;
    constants.Width = width;
    constants.Height = height;
    auto& shader = *m_CompositeShader;
    context.SetConstantBuffer(shader, "SVGFCompositeConstants", constants);
    context.SetTexture(shader, "FilteredColor", ShaderResourceView(input));
    context.SetTexture(shader, "DepthTexture", ShaderResourceView::DepthAsFloat(r.Depth));
    context.SetTexture(shader, "GBufferAlbedoOcclusion", ShaderResourceView(r.GBufferAlbedoOcclusion));
    context.SetTexture(shader, "GBufferEmissionMetallic", ShaderResourceView(r.GBufferEmissionMetallic));
    context.SetUnorderedAccessView(shader, "Output", UnorderedAccessView(r.Output));
    Dispatch(context, shader, width, height);
}
//Modify End
