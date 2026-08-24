#pragma once

//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/BloomDownsample.h>
#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <Framework/Rendering/PostProcess/BloomPrefilter.h>
#include <Framework/Rendering/PostProcess/BloomUpsample.h>

#include <RenderGraph/ResourceDescription.h>
#include <RenderGraph/ResourceId.h>

#include <cstddef>
#include <functional>
#include <string>

namespace RenderGraph
{
    class RenderGraphBuilder;
}

class Bloom final
{
public:
    struct GraphInputs
    {
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId Output = 0;
        RenderGraph::ResourceId InputToken = 0;
        RenderGraph::ResourceId OutputToken = 0;
        RenderGraph::RenderMetadataExpression<uint32_t> WidthExpression;
        RenderGraph::RenderMetadataExpression<uint32_t> HeightExpression;
        std::function<BloomParameters()> ResolveParameters;
        std::wstring DiagnosticNamePrefix = L"Framework.Bloom";
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
        size_t PyramidLevels = 1u;
    };

    Bloom(FrameworkDeviceContext& deviceContext, CommandList& commandList);

    void AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs);

private:
    void RecordPrefilter(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& source,
        const RenderTarget& destination);
    void RecordDownsample(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& source,
        const RenderTarget& destination);
    void RecordUpsample(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& lowResolutionSource,
        const std::shared_ptr<Texture>& highResolutionSource,
        const RenderTarget& destination);
    void RecordComposite(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& sourceColor,
        const std::shared_ptr<Texture>& bloom,
        const RenderTarget& destination);

    BloomPrefilter m_Prefilter;
    BloomDownsample m_Downsample;
    BloomUpsample m_Upsample;
};
//Modify End
