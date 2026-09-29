//Modify Begin:2026-09-15 by Hui
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

}

class SVGFGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        Temporal,
        Atrous,
        Composite,
    };

    struct Desc
    {
        Kind PassKind = Kind::Temporal;
        std::shared_ptr<const SVGF::GraphInputs> Inputs;
        const wchar_t* PassName = L"SVGF";
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId Destination = 0;
        RenderGraph::ResourceId Variance = 0;
        RenderGraph::ResourceId TemporalColor = 0;
        RenderGraph::ResourceId TemporalMoments = 0;
        RenderGraph::ResourceId OutputToken = 0;
        uint32_t StepSize = 1u;
        uint32_t Direction = 0u;
        RenderGraph::ImportedResourceHandle HistoryColorRead;
        RenderGraph::ImportedResourceHandle HistoryColorWrite;
        RenderGraph::ImportedResourceHandle HistoryMomentsRead;
        RenderGraph::ImportedResourceHandle HistoryMomentsWrite;
    };

    SVGFGraphPass(SVGF& feature, Desc desc)
        : m_Kind(desc.PassKind)
        , m_Feature(feature)
        , m_Inputs(std::move(desc.Inputs))
        , m_Source(desc.Source)
        , m_Destination(desc.Destination)
        , m_Variance(desc.Variance)
        , m_TemporalColor(desc.TemporalColor)
        , m_TemporalMoments(desc.TemporalMoments)
        , m_OutputToken(desc.OutputToken)
        , m_StepSize(desc.StepSize)
        , m_Direction(desc.Direction)
    {
        SetPassName(desc.PassName);
        if (m_Kind == Kind::Temporal)
        {
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterInput({ m_Inputs->NoisyRadiance, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->GBufferNormal, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->GBufferPosition, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->MotionVector, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->Depth, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ desc.HistoryColorRead.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ desc.HistoryMomentsRead.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_TemporalColor, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_TemporalMoments, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_Variance, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ desc.HistoryColorWrite.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ desc.HistoryMomentsWrite.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_OutputToken, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(
                desc.HistoryColorRead,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                RenderGraph::ExternalResourceAccessMode::Read,
                false);
            AddImportedResourceAccess(
                desc.HistoryMomentsRead,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                RenderGraph::ExternalResourceAccessMode::Read,
                false);
            AddImportedResourceAccess(
                desc.HistoryColorWrite,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                RenderGraph::ExternalResourceAccessMode::Write,
                false);
            AddImportedResourceAccess(
                desc.HistoryMomentsWrite,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                RenderGraph::ExternalResourceAccessMode::Write,
                false);
        }
        else if (m_Kind == Kind::Atrous)
        {
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterInput({ m_Source, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Variance, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->GBufferNormal, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->GBufferPosition, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->Depth, RenderGraph::InputType::NonPixelShaderResource });
            RegisterOutput({ m_Destination, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_OutputToken, RenderGraph::OutputType::Token });
        }
        else
        {
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterInput({ m_Source, RenderGraph::InputType::NonPixelShaderResource });
            RegisterInput({ m_Inputs->Depth, RenderGraph::InputType::NonPixelShaderResource });
            RegisterOutput({ m_Inputs->Output, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_OutputToken, RenderGraph::OutputType::Token });
        }
    }

protected:
    void InitImpl(CommandList&) override {}

    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override
    {
        CommandList& commandList = passContext.GetCommandList();
        const uint32_t width = context.GetMetadata().m_ScreenWidth;
        const uint32_t height = context.GetMetadata().m_ScreenHeight;
        if (m_Kind == Kind::Temporal)
        {
            const uint32_t readIndex = static_cast<uint32_t>(m_Inputs->ResolveFrameIndex() & 1ull);
            const uint32_t writeIndex = 1u - readIndex;
            m_Feature.RecordTemporal(
                commandList,
                context.GetTexture(m_Inputs->NoisyRadiance),
                context.GetTexture(m_Inputs->GBufferNormal),
                context.GetTexture(m_Inputs->GBufferPosition),
                context.GetTexture(m_Inputs->MotionVector),
                context.GetTexture(m_Inputs->Depth),
                m_Feature.m_HistoryColor[readIndex],
                m_Feature.m_HistoryMoments[readIndex],
                context.GetTexture(m_TemporalColor),
                context.GetTexture(m_TemporalMoments),
                context.GetTexture(m_Variance),
                m_Feature.m_HistoryColor[writeIndex],
                m_Feature.m_HistoryMoments[writeIndex],
                width,
                height);
        }
        else if (m_Kind == Kind::Atrous)
        {
            m_Feature.RecordAtrous(
                commandList,
                context.GetTexture(m_Source),
                context.GetTexture(m_Destination),
                context.GetTexture(m_Variance),
                context.GetTexture(m_Inputs->GBufferNormal),
                context.GetTexture(m_Inputs->GBufferPosition),
                context.GetTexture(m_Inputs->Depth),
                width,
                height,
                m_StepSize,
                m_Direction);
        }
        else
        {
            m_Feature.RecordComposite(
                commandList,
                context.GetTexture(m_Source),
                context.GetTexture(m_Inputs->Depth),
                context.GetTexture(m_Inputs->Output),
                width,
                height);
        }
    }

private:
    Kind m_Kind;
    SVGF& m_Feature;
    std::shared_ptr<const SVGF::GraphInputs> m_Inputs;
    RenderGraph::ResourceId m_Source = 0;
    RenderGraph::ResourceId m_Destination = 0;
    RenderGraph::ResourceId m_Variance = 0;
    RenderGraph::ResourceId m_TemporalColor = 0;
    RenderGraph::ResourceId m_TemporalMoments = 0;
    RenderGraph::ResourceId m_OutputToken = 0;
    uint32_t m_StepSize = 1u;
    uint32_t m_Direction = 0u;
};

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
        const std::wstring& suffix,
        const DXGI_FORMAT format,
        const bool dedicatedResource)
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
            dedicatedResource);
    };

    const RenderGraph::ResourceId temporalColor =
        createScratchTexture(L"TemporalColor", DXGI_FORMAT_R16G16B16A16_FLOAT, SvgfScratchUsesDedicatedResources);
    const RenderGraph::ResourceId temporalMoments =
        createScratchTexture(L"TemporalMoments", DXGI_FORMAT_R16G16_FLOAT, SvgfScratchUsesDedicatedResources);
    const RenderGraph::ResourceId variance =
        createScratchTexture(L"Variance", DXGI_FORMAT_R16_FLOAT, SvgfScratchUsesDedicatedResources);
    const std::wstring temporalTokenName = sharedInputs->DiagnosticNamePrefix + L".TemporalFinished";
    const RenderGraph::ResourceId temporalToken = builder.CreateToken(temporalTokenName.c_str());

    builder.AddPass(std::make_unique<SVGFGraphPass>(
        *this,
        SVGFGraphPass::Desc{
            .PassKind = SVGFGraphPass::Kind::Temporal,
            .Inputs = sharedInputs,
            .PassName = L"SVGF Temporal",
            .Variance = variance,
            .TemporalColor = temporalColor,
            .TemporalMoments = temporalMoments,
            .OutputToken = temporalToken,
            .HistoryColorRead = historyColorRead,
            .HistoryColorWrite = historyColorWrite,
            .HistoryMomentsRead = historyMomentsRead,
            .HistoryMomentsWrite = historyMomentsWrite,
        }));

    RenderGraph::ResourceId filtered = temporalColor;
    RenderGraph::ResourceId previousAtrousToken = temporalToken;
    const uint32_t iterationCount = std::clamp(m_Settings.AtrousIterations, 1u, 8u);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        const uint32_t stepSize = 1u << iteration;
        // The graph is not SSA: every half-pass needs a distinct logical version.
        // The transient allocator can still alias non-overlapping physical memory.
        const RenderGraph::ResourceId horizontalOutput = createScratchTexture(
            L"AtrousHorizontal." + std::to_wstring(iteration),
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            false);
        const RenderGraph::ResourceId verticalOutput = createScratchTexture(
            L"AtrousVertical." + std::to_wstring(iteration),
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            false);
        const RenderGraph::ResourceId horizontalToken = builder.CreateToken(
            (sharedInputs->DiagnosticNamePrefix + L".AtrousHorizontalFinished." + std::to_wstring(iteration)).c_str());
        const RenderGraph::ResourceId verticalToken = builder.CreateToken(
            (sharedInputs->DiagnosticNamePrefix + L".AtrousVerticalFinished." + std::to_wstring(iteration)).c_str());
        const std::wstring horizontalName = L"SVGF A-Trous Horizontal " + std::to_wstring(iteration);
        const std::wstring verticalName = L"SVGF A-Trous Vertical " + std::to_wstring(iteration);
        builder.AddPass(std::make_unique<SVGFGraphPass>(
            *this,
            SVGFGraphPass::Desc{
                .PassKind = SVGFGraphPass::Kind::Atrous,
                .Inputs = sharedInputs,
                .PassName = horizontalName.c_str(),
                .Source = filtered,
                .Destination = horizontalOutput,
                .Variance = variance,
                .OutputToken = horizontalToken,
                .StepSize = stepSize,
                .Direction = 0u,
            }));
        builder.AddPass(std::make_unique<SVGFGraphPass>(
            *this,
            SVGFGraphPass::Desc{
                .PassKind = SVGFGraphPass::Kind::Atrous,
                .Inputs = sharedInputs,
                .PassName = verticalName.c_str(),
                .Source = horizontalOutput,
                .Destination = verticalOutput,
                .Variance = variance,
                .OutputToken = verticalToken,
                .StepSize = stepSize,
                .Direction = 1u,
            }));
        filtered = verticalOutput;
        previousAtrousToken = verticalToken;
    }

    builder.AddPass(std::make_unique<SVGFGraphPass>(
        *this,
        SVGFGraphPass::Desc{
            .PassKind = SVGFGraphPass::Kind::Composite,
            .Inputs = sharedInputs,
            .PassName = L"SVGF Composite",
            .Source = filtered,
            .OutputToken = sharedInputs->OutputToken,
        }));
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
