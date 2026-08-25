//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/Bloom.h>

#include <DX12Library/Helpers.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    constexpr FLOAT BloomClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr bool BloomScratchUsesDedicatedResources = false;

    struct BloomPrefilterPassData
    {
        Bloom* Feature = nullptr;
        std::shared_ptr<const Bloom::GraphInputs> Inputs;
        RenderGraph::ResourceId Destination = 0;
    };

    struct BloomDownsamplePassData
    {
        Bloom* Feature = nullptr;
        std::shared_ptr<const Bloom::GraphInputs> Inputs;
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId Destination = 0;
    };

    struct BloomUpsamplePassData
    {
        Bloom* Feature = nullptr;
        std::shared_ptr<const Bloom::GraphInputs> Inputs;
        RenderGraph::ResourceId LowResolutionSource = 0;
        RenderGraph::ResourceId HighResolutionSource = 0;
        RenderGraph::ResourceId Destination = 0;
    };

    struct BloomCompositePassData
    {
        Bloom* Feature = nullptr;
        std::shared_ptr<const Bloom::GraphInputs> Inputs;
        RenderGraph::ResourceId BloomTexture = 0;
    };

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

    builder.AddPass<BloomPrefilterPassData>(
        L"Bloom Prefilter",
        [this, sharedInputs, destination = downsampleLevels.front()](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            BloomPrefilterPassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passData.Destination = destination;
            passBuilder.ReadToken(sharedInputs->InputToken);
            passBuilder.ReadTexture(sharedInputs->Source);
            passBuilder.WriteTexture(destination);
        },
        [](const BloomPrefilterPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordPrefilter(
                commandList,
                passData.Inputs->ResolveParameters(),
                context.GetTexture(passData.Inputs->Source),
                GetPassRenderTarget(context));
        });

    for (size_t level = 1; level < sharedInputs->PyramidLevels; ++level)
    {
        const std::wstring passName = L"Bloom Downsample " + std::to_wstring(level);
        const RenderGraph::ResourceId source = downsampleLevels[level - 1u];
        const RenderGraph::ResourceId destination = downsampleLevels[level];
        builder.AddPass<BloomDownsamplePassData>(
            passName.c_str(),
            [this, sharedInputs, source, destination](
                RenderGraph::RenderGraphPassBuilder& passBuilder,
                BloomDownsamplePassData& passData)
            {
                passData.Feature = this;
                passData.Inputs = sharedInputs;
                passData.Source = source;
                passData.Destination = destination;
                passBuilder.ReadTexture(source);
                passBuilder.WriteTexture(destination);
            },
            [](const BloomDownsamplePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
            {
                passData.Feature->RecordDownsample(
                    commandList,
                    passData.Inputs->ResolveParameters(),
                    context.GetTexture(passData.Source),
                    GetPassRenderTarget(context));
            });
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
        builder.AddPass<BloomUpsamplePassData>(
            passName.c_str(),
            [this, sharedInputs, lowResolutionSource, highResolutionSource, destination](
                RenderGraph::RenderGraphPassBuilder& passBuilder,
                BloomUpsamplePassData& passData)
            {
                passData.Feature = this;
                passData.Inputs = sharedInputs;
                passData.LowResolutionSource = lowResolutionSource;
                passData.HighResolutionSource = highResolutionSource;
                passData.Destination = destination;
                passBuilder.ReadTexture(lowResolutionSource);
                passBuilder.ReadTexture(highResolutionSource);
                passBuilder.WriteTexture(destination);
            },
            [](const BloomUpsamplePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
            {
                passData.Feature->RecordUpsample(
                    commandList,
                    passData.Inputs->ResolveParameters(),
                    context.GetTexture(passData.LowResolutionSource),
                    context.GetTexture(passData.HighResolutionSource),
                    GetPassRenderTarget(context));
            });
    }

    const RenderGraph::ResourceId bloomTexture = sharedInputs->PyramidLevels == 1u
        ? downsampleLevels.front()
        : upsampleLevels.front();
    builder.AddPass<BloomCompositePassData>(
        L"Bloom Composite",
        [this, sharedInputs, bloomTexture](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            BloomCompositePassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passData.BloomTexture = bloomTexture;
            passBuilder.ReadTexture(sharedInputs->Source);
            passBuilder.ReadTexture(bloomTexture);
            passBuilder.WriteTexture(sharedInputs->Output);
            passBuilder.WriteToken(sharedInputs->OutputToken);
        },
        [](const BloomCompositePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordComposite(
                commandList,
                passData.Inputs->ResolveParameters(),
                context.GetTexture(passData.Inputs->Source),
                context.GetTexture(passData.BloomTexture),
                GetPassRenderTarget(context));
        });
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
