//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/Bloom.h>

#include <DX12Library/Helpers.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    constexpr FLOAT BloomClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr bool BloomScratchUsesDedicatedResources = false;

    const RenderTarget& GetPassRenderTarget(const RenderGraph::RenderContext& context)
    {
        const auto& renderTarget = context.GetRenderTargetInfo().m_RenderTarget;
        Assert(renderTarget != nullptr, "Bloom render pass requires a RenderGraph render target.");
        return *renderTarget;
    }

    RenderGraph::RenderMetadataExpression<uint32_t> CreatePyramidDimensionExpression(
        const RenderGraph::RenderMetadataExpression<uint32_t>& baseExpression,
        const size_t level)
    {
        return [baseExpression, level](const RenderGraph::RenderMetadata& metadata)
        {
            uint32_t dimension = (std::max)(1u, baseExpression(metadata));
            for (size_t currentLevel = 0; currentLevel <= level; ++currentLevel)
            {
                dimension = (std::max)(1u, dimension >> 1u);
            }
            return dimension;
        };
    }
}

class BloomGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        Prefilter,
        Downsample,
        Upsample,
        Composite,
    };

    struct Desc
    {
        Kind PassKind = Kind::Prefilter;
        Bloom* Feature = nullptr;
        std::shared_ptr<const Bloom::GraphInputs> Inputs;
        std::wstring PassName;
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId LowResolutionSource = 0;
        RenderGraph::ResourceId HighResolutionSource = 0;
        RenderGraph::ResourceId Destination = 0;
        RenderGraph::ResourceId BloomTexture = 0;
    };

    explicit BloomGraphPass(Desc desc)
        : m_Kind(desc.PassKind)
        , m_Feature(*desc.Feature)
        , m_Inputs(std::move(desc.Inputs))
        , m_Source(desc.Source)
        , m_Destination(desc.Destination)
        , m_LowResolutionSource(desc.LowResolutionSource)
        , m_HighResolutionSource(desc.HighResolutionSource)
        , m_BloomTexture(desc.BloomTexture)
    {
        Assert(desc.Feature != nullptr && m_Inputs != nullptr, "Bloom graph pass requires feature inputs.");
        SetPassName(desc.PassName);
        switch (m_Kind)
        {
        case Kind::Prefilter:
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterInput({ m_Inputs->Source, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_Destination, RenderGraph::OutputType::RenderTarget });
            break;
        case Kind::Downsample:
            RegisterInput({ m_Source, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_Destination, RenderGraph::OutputType::RenderTarget });
            break;
        case Kind::Upsample:
            RegisterInput({ m_LowResolutionSource, RenderGraph::InputType::ShaderResource });
            RegisterInput({ m_HighResolutionSource, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_Destination, RenderGraph::OutputType::RenderTarget });
            break;
        case Kind::Composite:
            RegisterInput({ m_Inputs->Source, RenderGraph::InputType::ShaderResource });
            RegisterInput({ m_BloomTexture, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_Inputs->Output, RenderGraph::OutputType::RenderTarget });
            RegisterOutput({ m_Inputs->OutputToken, RenderGraph::OutputType::Token });
            break;
        }
    }

protected:
    void InitImpl(CommandList&) override {}

    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override
    {
        CommandList& commandList = passContext.GetCommandList();
        const BloomParameters parameters = m_Inputs->ResolveParameters();
        switch (m_Kind)
        {
        case Kind::Prefilter:
            m_Feature.RecordPrefilter(
                commandList,
                parameters,
                context.GetTexture(m_Inputs->Source),
                GetPassRenderTarget(context));
            break;
        case Kind::Downsample:
            m_Feature.RecordDownsample(
                commandList,
                parameters,
                context.GetTexture(m_Source),
                GetPassRenderTarget(context));
            break;
        case Kind::Upsample:
            m_Feature.RecordUpsample(
                commandList,
                parameters,
                context.GetTexture(m_LowResolutionSource),
                context.GetTexture(m_HighResolutionSource),
                GetPassRenderTarget(context));
            break;
        case Kind::Composite:
            m_Feature.RecordComposite(
                commandList,
                parameters,
                context.GetTexture(m_Inputs->Source),
                context.GetTexture(m_BloomTexture),
                GetPassRenderTarget(context));
            break;
        }
    }

private:
    Kind m_Kind;
    Bloom& m_Feature;
    std::shared_ptr<const Bloom::GraphInputs> m_Inputs;
    RenderGraph::ResourceId m_Source = 0;
    RenderGraph::ResourceId m_Destination = 0;
    RenderGraph::ResourceId m_LowResolutionSource = 0;
    RenderGraph::ResourceId m_HighResolutionSource = 0;
    RenderGraph::ResourceId m_BloomTexture = 0;
};

Bloom::Bloom(FrameworkDeviceContext& deviceContext, CommandList& commandList)
    : m_Prefilter(deviceContext, commandList)
    , m_Downsample(deviceContext, commandList)
    , m_Upsample(deviceContext, commandList)
{
}

void Bloom::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(inputs.Source != 0u && inputs.Output != 0u, "Bloom graph resources are invalid.");
    Assert(inputs.InputToken != 0u && inputs.OutputToken != 0u, "Bloom graph tokens are invalid.");
    Assert(static_cast<bool>(inputs.WidthExpression) && static_cast<bool>(inputs.HeightExpression),
        "Bloom graph dimensions are invalid.");
    Assert(static_cast<bool>(inputs.ResolveParameters), "Bloom requires a parameter resolver.");
    Assert(!inputs.DiagnosticNamePrefix.empty(), "Bloom requires a diagnostic-name prefix.");
    Assert(inputs.Format != DXGI_FORMAT_UNKNOWN, "Bloom texture format is invalid.");
    Assert(inputs.PyramidLevels > 0u, "Bloom requires at least one pyramid level.");

    const auto sharedInputs = std::make_shared<const GraphInputs>(std::move(inputs));
    std::vector<RenderGraph::ResourceId> downsampleLevels(sharedInputs->PyramidLevels);
    std::vector<RenderGraph::ResourceId> upsampleLevels(sharedInputs->PyramidLevels, 0u);

    for (size_t level = 0; level < sharedInputs->PyramidLevels; ++level)
    {
        const std::wstring resourceName = sharedInputs->DiagnosticNamePrefix +
            L".Downsample." + std::to_wstring(level);
        downsampleLevels[level] = builder.CreateTexture(
            resourceName.c_str(),
            CreatePyramidDimensionExpression(sharedInputs->WidthExpression, level),
            CreatePyramidDimensionExpression(sharedInputs->HeightExpression, level),
            sharedInputs->Format,
            BloomClearColor,
            RenderGraph::ResourceInitAction::Discard,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_HEAP_FLAG_NONE,
            BloomScratchUsesDedicatedResources);
    }

    builder.AddPass(std::make_unique<BloomGraphPass>(BloomGraphPass::Desc{
        .PassKind = BloomGraphPass::Kind::Prefilter,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Bloom Prefilter",
        .Destination = downsampleLevels.front(),
    }));

    for (size_t level = 1; level < sharedInputs->PyramidLevels; ++level)
    {
        const std::wstring passName = L"Bloom Downsample " + std::to_wstring(level);
        const RenderGraph::ResourceId source = downsampleLevels[level - 1u];
        const RenderGraph::ResourceId destination = downsampleLevels[level];
        builder.AddPass(std::make_unique<BloomGraphPass>(BloomGraphPass::Desc{
            .PassKind = BloomGraphPass::Kind::Downsample,
            .Feature = this,
            .Inputs = sharedInputs,
            .PassName = passName,
            .Source = source,
            .Destination = destination,
        }));
    }

    for (size_t level = sharedInputs->PyramidLevels - 1u; level > 0u; --level)
    {
        const size_t highResolutionLevel = level - 1u;
        const std::wstring resourceName = sharedInputs->DiagnosticNamePrefix +
            L".Upsample." + std::to_wstring(highResolutionLevel);
        const RenderGraph::ResourceId destination = builder.CreateTexture(
            resourceName.c_str(),
            CreatePyramidDimensionExpression(sharedInputs->WidthExpression, highResolutionLevel),
            CreatePyramidDimensionExpression(sharedInputs->HeightExpression, highResolutionLevel),
            sharedInputs->Format,
            BloomClearColor,
            RenderGraph::ResourceInitAction::Discard,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_HEAP_FLAG_NONE,
            BloomScratchUsesDedicatedResources);
        upsampleLevels[highResolutionLevel] = destination;

        const RenderGraph::ResourceId lowResolutionSource =
            level == sharedInputs->PyramidLevels - 1u
            ? downsampleLevels[level]
            : upsampleLevels[level];
        const RenderGraph::ResourceId highResolutionSource = downsampleLevels[highResolutionLevel];
        const std::wstring passName = L"Bloom Upsample " + std::to_wstring(highResolutionLevel);
        builder.AddPass(std::make_unique<BloomGraphPass>(BloomGraphPass::Desc{
            .PassKind = BloomGraphPass::Kind::Upsample,
            .Feature = this,
            .Inputs = sharedInputs,
            .PassName = passName,
            .LowResolutionSource = lowResolutionSource,
            .HighResolutionSource = highResolutionSource,
            .Destination = destination,
        }));
    }

    const RenderGraph::ResourceId bloomTexture = sharedInputs->PyramidLevels == 1u
        ? downsampleLevels.front()
        : upsampleLevels.front();
    builder.AddPass(std::make_unique<BloomGraphPass>(BloomGraphPass::Desc{
        .PassKind = BloomGraphPass::Kind::Composite,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"Bloom Composite",
        .BloomTexture = bloomTexture,
    }));
}

void Bloom::RecordPrefilter(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& source,
    const RenderTarget& destination)
{
    m_Prefilter.Execute(commandList, parameters, source, destination);
}

void Bloom::RecordDownsample(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& source,
    const RenderTarget& destination)
{
    m_Downsample.Execute(commandList, parameters, source, destination);
}

void Bloom::RecordUpsample(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& lowResolutionSource,
    const std::shared_ptr<Texture>& highResolutionSource,
    const RenderTarget& destination)
{
    m_Upsample.Execute(commandList, parameters, lowResolutionSource, highResolutionSource, destination);
}

void Bloom::RecordComposite(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& sourceColor,
    const std::shared_ptr<Texture>& bloom,
    const RenderTarget& destination)
{
    m_Upsample.ExecuteComposite(commandList, parameters, sourceColor, bloom, destination);
}
//Modify End
