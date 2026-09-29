//Modify Begin:2026-08-28 by Hui
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
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>
#include <array>
#include <d3dx12/d3dx12.h>

namespace
{
    constexpr uint32_t HistogramBinCount = 256u;
    constexpr uint32_t HistogramBinStride = sizeof(uint32_t);
    constexpr float DefaultMinLogLuminance = -10.0f;
    constexpr float DefaultMaxLogLuminance = 2.0f;
    constexpr float DefaultAdaptationTau = 1.1f;
    constexpr float MinAdaptationTau = 0.01f;
    constexpr float MaxAdaptationTau = 8.0f;
    constexpr float MinLogLuminanceLimit = -16.0f;
    constexpr float MaxLogLuminanceLimit = 8.0f;
    constexpr float MinimumLogLuminanceRange = 0.01f;

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
        uint32_t ExposureEnabled = 1u;
        uint32_t OutputMode = static_cast<uint32_t>(AutoExposure::OutputMode::SDR);
        uint32_t Padding = 0u;
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

    AutoExposureConstants BuildConstants(
        const AutoExposure::FrameInputs& inputs,
        const AutoExposure::Settings& settings)
    {
        AutoExposureConstants constants;
        constants.InputWidth = inputs.InputWidth;
        constants.InputHeight = inputs.InputHeight;
        constants.OutputWidth = inputs.OutputWidth;
        constants.OutputHeight = inputs.OutputHeight;
        constants.DeltaTime = std::max(inputs.DeltaTime, 0.0f);
        constants.MinLogLuminance = settings.MinLogLuminance;
        constants.LogLuminanceRange = settings.MaxLogLuminance - settings.MinLogLuminance;
        constants.Tau = settings.Tau;
        constants.ExposureEnabled = settings.Enabled ? 1u : 0u;
        constants.OutputMode = static_cast<uint32_t>(settings.Output);
        return constants;
    }

}

class AutoExposureGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        Prepare,
        BuildHistogram,
        AverageHistogram,
        Apply,
    };

    struct Desc
    {
        Kind PassKind = Kind::Prepare;
        AutoExposure* Feature = nullptr;
        std::shared_ptr<const AutoExposure::GraphInputs> Inputs;
        std::wstring PassName;
        RenderGraph::ResourceId TokenBefore = 0;
        RenderGraph::ResourceId TokenAfter = 0;
        RenderGraph::ImportedResourceHandle ResourceA;
        RenderGraph::ImportedResourceHandle ResourceB;
    };

    explicit AutoExposureGraphPass(Desc desc)
        : m_Kind(desc.PassKind)
        , m_Feature(*desc.Feature)
        , m_Inputs(std::move(desc.Inputs))
        , m_TokenBefore(desc.TokenBefore)
        , m_TokenAfter(desc.TokenAfter)
        , m_ResourceA(std::move(desc.ResourceA))
        , m_ResourceB(std::move(desc.ResourceB))
    {
        Assert(desc.Feature != nullptr && m_Inputs != nullptr, "Auto exposure graph pass requires feature inputs.");
        SetPassName(desc.PassName);
        switch (m_Kind)
        {
        case Kind::Prepare:
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterOutput({ m_ResourceA.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_ResourceB.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_ResourceA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_ResourceB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            break;
        case Kind::BuildHistogram:
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_Inputs->Source, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_ResourceA.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_ResourceA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, true);
            break;
        case Kind::AverageHistogram:
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterOutput({ m_ResourceA.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_ResourceB.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_ResourceA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, true);
            AddImportedResourceAccess(m_ResourceB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, true);
            break;
        case Kind::Apply:
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_Inputs->Source, RenderGraph::InputType::ShaderResource });
            RegisterInput({ m_ResourceA.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_Inputs->Output, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_Inputs->OutputToken, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_ResourceA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            break;
        }
    }

protected:
    void InitImpl(CommandList&) override {}

    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override
    {
        CommandList& commandList = passContext.GetCommandList();
        const AutoExposure::FrameInputs frameInputs = m_Inputs->ResolveFrameInputs(context);
        switch (m_Kind)
        {
        case Kind::Prepare:
            m_Feature.RecordPrepare(commandList, frameInputs);
            break;
        case Kind::BuildHistogram:
            m_Feature.RecordBuildHistogram(commandList, frameInputs);
            break;
        case Kind::AverageHistogram:
            m_Feature.RecordAverageHistogram(commandList, frameInputs);
            break;
        case Kind::Apply:
            m_Feature.RecordApply(commandList, frameInputs);
            break;
        }
    }

private:
    Kind m_Kind;
    AutoExposure& m_Feature;
    std::shared_ptr<const AutoExposure::GraphInputs> m_Inputs;
    RenderGraph::ResourceId m_TokenBefore = 0;
    RenderGraph::ResourceId m_TokenAfter = 0;
    RenderGraph::ImportedResourceHandle m_ResourceA;
    RenderGraph::ImportedResourceHandle m_ResourceB;
};

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
        sanitized.MaxLogLuminance != m_Settings.MaxLogLuminance ||
        sanitized.Output != m_Settings.Output)
    {
        m_Settings = sanitized;
        m_HistoryValid = false;
    }
}

const AutoExposure::Settings& AutoExposure::GetSettings() const
{
    return m_Settings;
}

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
    }

    if (m_AdaptedLuminance == nullptr)
    {
        m_AdaptedLuminance = RenderTexture::CreateUav2D(
            m_DeviceContext,
            DXGI_FORMAT_R32_FLOAT,
            1u,
            1u,
            L"Auto Exposure Adapted Luminance");
    }

    if (m_OutputWidth != outputWidth || m_OutputHeight != outputHeight)
    {
        m_OutputWidth = outputWidth;
        m_OutputHeight = outputHeight;
        m_HistoryValid = false;
    }
}

void AutoExposure::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(inputs.Source != 0u && inputs.Output != 0u, "Auto exposure graph resources are invalid.");
    Assert(inputs.InputToken != 0u && inputs.OutputToken != 0u, "Auto exposure graph tokens are invalid.");
    Assert(inputs.OutputWidth > 0u && inputs.OutputHeight > 0u, "Auto exposure output dimensions must be positive.");
    Assert(static_cast<bool>(inputs.ResolveFrameInputs), "Auto exposure requires a frame-input resolver.");
    EnsureResources(inputs.OutputWidth, inputs.OutputHeight);

    const auto sharedInputs = std::make_shared<const GraphInputs>(std::move(inputs));
    const RenderGraph::ResourceId prepareFinished = builder.CreateToken(L"Framework.AutoExposure.PrepareFinished");
    const RenderGraph::ResourceId histogramFinished = builder.CreateToken(L"Framework.AutoExposure.HistogramFinished");
    const RenderGraph::ResourceId averageFinished = builder.CreateToken(L"Framework.AutoExposure.AverageFinished");

    const auto histogramPrepare = builder.ImportResource(
        L"Framework.AutoExposure.Histogram.Prepare",
        [this]() -> const Resource& { return *m_Histogram; });
    const auto adaptedPrepare = builder.ImportResource(
        L"Framework.AutoExposure.AdaptedLuminance.Prepare",
        [this]() -> const Resource& { return *m_AdaptedLuminance; });
    const auto histogramBuild = builder.ImportResource(
        L"Framework.AutoExposure.Histogram.Build",
        [this]() -> const Resource& { return *m_Histogram; });
    const auto histogramAverage = builder.ImportResource(
        L"Framework.AutoExposure.Histogram.Average",
        [this]() -> const Resource& { return *m_Histogram; });
    const auto adaptedAverage = builder.ImportResource(
        L"Framework.AutoExposure.AdaptedLuminance.Average",
        [this]() -> const Resource& { return *m_AdaptedLuminance; });
    const auto adaptedApply = builder.ImportResource(
        L"Framework.AutoExposure.AdaptedLuminance.Apply",
        [this]() -> const Resource& { return *m_AdaptedLuminance; });

    builder.AddPass(std::make_unique<AutoExposureGraphPass>(AutoExposureGraphPass::Desc{
        .PassKind = AutoExposureGraphPass::Kind::Prepare,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Auto Exposure Prepare",
        .TokenAfter = prepareFinished,
        .ResourceA = histogramPrepare,
        .ResourceB = adaptedPrepare,
    }));
    builder.AddPass(std::make_unique<AutoExposureGraphPass>(AutoExposureGraphPass::Desc{
        .PassKind = AutoExposureGraphPass::Kind::BuildHistogram,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Auto Exposure Build Histogram",
        .TokenBefore = prepareFinished,
        .TokenAfter = histogramFinished,
        .ResourceA = histogramBuild,
    }));
    builder.AddPass(std::make_unique<AutoExposureGraphPass>(AutoExposureGraphPass::Desc{
        .PassKind = AutoExposureGraphPass::Kind::AverageHistogram,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Auto Exposure Average Histogram",
        .TokenBefore = histogramFinished,
        .TokenAfter = averageFinished,
        .ResourceA = histogramAverage,
        .ResourceB = adaptedAverage,
    }));
    builder.AddPass(std::make_unique<AutoExposureGraphPass>(AutoExposureGraphPass::Desc{
        .PassKind = AutoExposureGraphPass::Kind::Apply,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Auto Exposure Apply",
        .TokenBefore = averageFinished,
        .ResourceA = adaptedApply,
    }));
}

void AutoExposure::RecordPrepare(CommandList& commandList, const FrameInputs& inputs)
{
    Assert(inputs.Source != nullptr && inputs.Source->IsValid(), "Auto exposure source texture is invalid.");
    Assert(inputs.Output != nullptr && inputs.Output->IsValid(), "Auto exposure output texture is invalid.");
    Assert(inputs.InputWidth > 0u && inputs.InputHeight > 0u, "Auto exposure input dimensions must be positive.");
    Assert(inputs.OutputWidth > 0u && inputs.OutputHeight > 0u, "Auto exposure output dimensions must be positive.");
    EnsureResources(inputs.OutputWidth, inputs.OutputHeight);
    CommandContext commandContext(commandList);
    if (m_Settings.Enabled)
    {
        commandContext.ClearUnorderedAccessUint(*m_Histogram, std::array<UINT, 4>{ 0u, 0u, 0u, 0u }.data());
    }
    if (!m_HistoryValid)
    {
        commandContext.ClearUnorderedAccessUint(
            *m_AdaptedLuminance,
            std::array<UINT, 4>{ 0x3f800000u, 0u, 0u, 0u }.data());
        m_HistoryValid = true;
    }
}

void AutoExposure::RecordBuildHistogram(CommandList& commandList, const FrameInputs& inputs)
{
    if (!m_Settings.Enabled)
    {
        return;
    }
    const AutoExposureConstants constants = BuildConstants(inputs, m_Settings);
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_BuildHistogramShader, "AutoExposureConstants", constants);
    commandContext.SetTexture(*m_BuildHistogramShader, "SourceColor", ShaderResourceView(inputs.Source));
    commandContext.SetUnorderedAccessView(*m_BuildHistogramShader, "Histogram", UnorderedAccessView(m_Histogram));
    commandContext.BindPipeline(*m_BuildHistogramShader);
    commandContext.BindDescriptorSet(m_BuildHistogramShader->GetDescriptorSet());
    commandContext.Dispatch((inputs.InputWidth + 15u) / 16u, (inputs.InputHeight + 15u) / 16u, 1u);
}

void AutoExposure::RecordAverageHistogram(CommandList& commandList, const FrameInputs& inputs)
{
    if (!m_Settings.Enabled)
    {
        return;
    }
    const AutoExposureConstants constants = BuildConstants(inputs, m_Settings);
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_AverageHistogramShader, "AutoExposureConstants", constants);
    commandContext.SetUnorderedAccessView(*m_AverageHistogramShader, "Histogram", UnorderedAccessView(m_Histogram));
    commandContext.SetUnorderedAccessView(
        *m_AverageHistogramShader,
        "AdaptedLuminance",
        UnorderedAccessView(m_AdaptedLuminance));
    commandContext.BindPipeline(*m_AverageHistogramShader);
    commandContext.BindDescriptorSet(m_AverageHistogramShader->GetDescriptorSet());
    commandContext.Dispatch(1u, 1u, 1u);
}

void AutoExposure::RecordApply(CommandList& commandList, const FrameInputs& inputs)
{
    const AutoExposureConstants constants = BuildConstants(inputs, m_Settings);
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_ApplyShader, "AutoExposureConstants", constants);
    commandContext.SetTexture(*m_ApplyShader, "SourceColor", ShaderResourceView(inputs.Source));
    commandContext.SetTexture(*m_ApplyShader, "AdaptedLuminance", ShaderResourceView(m_AdaptedLuminance));
    commandContext.SetUnorderedAccessView(*m_ApplyShader, "OutputColor", UnorderedAccessView(inputs.Output));
    commandContext.BindPipeline(*m_ApplyShader);
    commandContext.BindDescriptorSet(m_ApplyShader->GetDescriptorSet());
    commandContext.Dispatch((inputs.OutputWidth + 7u) / 8u, (inputs.OutputHeight + 7u) / 8u, 1u);
}
//Modify End
