//Modify Begin:2026-09-29 by Hui
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>
#include <utility>

struct ReSTIRGIPass::PipelineSet
{
    bool UseSoftShadowVariant = false;
    uint32_t EnvironmentProjectionVariant = 0u;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> InitialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> TemporalVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> SpatialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> ShadeVariants;
};

struct ReSTIRGIPass::InternalResources
{
    struct ReservoirSet
    {
        std::shared_ptr<Texture> Creation;
        std::shared_ptr<Texture> Hit;
        std::shared_ptr<Texture> Light;
    };

    struct HistorySet
    {
        ReservoirSet Temporal;
        ReservoirSet Spatial;
    };

    ReservoirSet Initial;
    std::array<HistorySet, 2> History;
};

namespace
{
    constexpr DXGI_FORMAT ReservoirFormat = DXGI_FORMAT_R32G32B32A32_UINT;

    void WriteStageTimestamp(
        CommandList& commandList,
        const ReSTIRGIExecutionInputs& inputs,
        const char* markerName)
    {
        if (inputs.EnableStageTiming && inputs.WriteTimestamp)
        {
            inputs.WriteTimestamp(commandList, markerName);
        }
    }

    void BindActivePixelList(
        CommandContext& commandContext,
        ComputeShader& shader,
        const ActivePixelDispatch& dispatch)
    {
        if (!dispatch.IsValid())
        {
            return;
        }

        commandContext.SetShaderResource(
            shader,
            "FrameworkActivePixelIndices",
            0u,
            *dispatch.Pixels.Indices);
        commandContext.SetShaderResource(
            shader,
            "FrameworkActivePixelCount",
            0u,
            *dispatch.Pixels.Count);
    }

    void DispatchReSTIRStage(
        CommandContext& commandContext,
        const ReSTIRGIFrameState& frameState,
        const ActivePixelDispatch& dispatch)
    {
        if (dispatch.IsValid())
        {
            commandContext.DispatchIndirect(
                *dispatch.Signature,
                IndirectCommandExecutionDesc{
                    .ArgumentBuffer = dispatch.Arguments,
                    .ArgumentBufferOffset = dispatch.ArgumentBufferOffset,
                });
            return;
        }

        commandContext.Dispatch(
            Math::DivideByMultiple(frameState.Constants.Width, 8u),
            Math::DivideByMultiple(frameState.Constants.Height, 8u),
            1u);
    }

    void DeclareReSTIRGISharedResources(
        RenderGraph::RenderGraphPassBuilder& passBuilder,
        const ReSTIRGIGraphInputs& inputs)
    {
        Assert(static_cast<bool>(inputs.DeclareSharedResources),
            "ReSTIR GI requires shared graph-resource declarations.");
        inputs.DeclareSharedResources(passBuilder);
    }
}

class ReSTIRGIGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        OutputClear,
        Initial,
        Temporal,
        Spatial,
        Shade,
    };

    struct Desc
    {
        Kind PassKind = Kind::Initial;
        ReSTIRGIPass* Pass = nullptr;
        std::shared_ptr<const ReSTIRGIGraphInputs> Inputs;
        std::wstring PassName;
        RenderGraph::ResourceId TokenBefore = 0;
        RenderGraph::ResourceId TokenAfter = 0;
        RenderGraph::ImportedResourceHandle InitialCreation;
        RenderGraph::ImportedResourceHandle InitialHit;
        RenderGraph::ImportedResourceHandle InitialLight;
        RenderGraph::ImportedResourceHandle HistoryReadCreation;
        RenderGraph::ImportedResourceHandle HistoryReadHit;
        RenderGraph::ImportedResourceHandle HistoryReadLight;
        RenderGraph::ImportedResourceHandle TemporalWriteCreation;
        RenderGraph::ImportedResourceHandle TemporalWriteHit;
        RenderGraph::ImportedResourceHandle TemporalWriteLight;
        RenderGraph::ImportedResourceHandle SpatialWriteCreation;
        RenderGraph::ImportedResourceHandle SpatialWriteHit;
        RenderGraph::ImportedResourceHandle SpatialWriteLight;
    };

    explicit ReSTIRGIGraphPass(Desc desc)
        : m_Kind(desc.PassKind)
        , m_Pass(*desc.Pass)
        , m_Inputs(std::move(desc.Inputs))
        , m_TokenBefore(desc.TokenBefore)
        , m_TokenAfter(desc.TokenAfter)
        , m_InitialCreation(std::move(desc.InitialCreation))
        , m_InitialHit(std::move(desc.InitialHit))
        , m_InitialLight(std::move(desc.InitialLight))
        , m_HistoryReadCreation(std::move(desc.HistoryReadCreation))
        , m_HistoryReadHit(std::move(desc.HistoryReadHit))
        , m_HistoryReadLight(std::move(desc.HistoryReadLight))
        , m_TemporalWriteCreation(std::move(desc.TemporalWriteCreation))
        , m_TemporalWriteHit(std::move(desc.TemporalWriteHit))
        , m_TemporalWriteLight(std::move(desc.TemporalWriteLight))
        , m_SpatialWriteCreation(std::move(desc.SpatialWriteCreation))
        , m_SpatialWriteHit(std::move(desc.SpatialWriteHit))
        , m_SpatialWriteLight(std::move(desc.SpatialWriteLight))
    {
        Assert(desc.Pass != nullptr && m_Inputs != nullptr, "ReSTIR GI graph pass requires pass inputs.");
        SetPassName(desc.PassName);
        if (m_Kind == Kind::OutputClear)
        {
            RegisterOutput({ m_Inputs->IndirectLighting, RenderGraph::OutputType::UnorderedAccess });
            return;
        }

        RenderGraph::RenderGraphPassBuilder sharedResourceBuilder(RenderGraph::RenderPassQueue::Direct);
        DeclareReSTIRGISharedResources(sharedResourceBuilder, *m_Inputs);
        sharedResourceBuilder.ApplyTo(*this);
        if (m_Kind == Kind::Initial)
        {
            RegisterOutput({ m_InitialCreation.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_InitialHit.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_InitialLight.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_InitialCreation, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_InitialHit, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_InitialLight, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else if (m_Kind == Kind::Temporal)
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_InitialCreation.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_InitialHit.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_InitialLight.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadCreation.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadHit.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadLight.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_TemporalWriteCreation.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TemporalWriteHit.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TemporalWriteLight.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_InitialCreation, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_InitialHit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_InitialLight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadCreation, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadHit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadLight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_TemporalWriteCreation, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_TemporalWriteHit, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_TemporalWriteLight, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else if (m_Kind == Kind::Spatial)
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_TemporalWriteCreation.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_TemporalWriteHit.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_TemporalWriteLight.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_SpatialWriteCreation.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_SpatialWriteHit.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_SpatialWriteLight.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_TemporalWriteCreation, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_TemporalWriteHit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_TemporalWriteLight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_SpatialWriteCreation, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_SpatialWriteHit, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_SpatialWriteLight, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_SpatialWriteCreation.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_SpatialWriteHit.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_SpatialWriteLight.GetId(), RenderGraph::InputType::ExternalAccess });
            if (m_Inputs->UseCompactedDispatch)
            {
                RegisterInput({ m_Inputs->IndirectLighting, RenderGraph::InputType::UnorderedAccess });
            }
            RegisterOutput({ m_Inputs->IndirectLighting, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_Inputs->OutputToken, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_SpatialWriteCreation, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_SpatialWriteHit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_SpatialWriteLight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
        }
    }

protected:
    void InitImpl(CommandList&) override {}

    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override
    {
        CommandList& commandList = passContext.GetCommandList();
        if (m_Kind == Kind::OutputClear)
        {
            const UINT clearValues[4] = {};
            CommandContext commandContext(commandList, passContext.GetBarrierContext());
            commandContext.ClearUnorderedAccessUint(context.GetResource(m_Inputs->IndirectLighting), clearValues);
            return;
        }

        ReSTIRGIExecutionInputs inputs = m_Inputs->ResolveFrameInputs(context);
        inputs.FrameState.Constants.HistoryValid =
            inputs.FrameState.Constants.HistoryValid != 0u && m_Pass.m_HistoryValid ? 1u : 0u;
        ReSTIRGIPass::PipelineSet& pipelines = m_Pass.GetPipelines(
            inputs.FrameState.UseSoftShadowVariant,
            inputs.FrameState.EnvironmentProjectionVariant);
        CommandContext commandContext(commandList, passContext.GetBarrierContext());
        if (inputs.PrepareCommandContext)
        {
            inputs.PrepareCommandContext(commandContext);
        }
        if (m_Kind == Kind::Initial)
        {
            WriteStageTimestamp(commandList, inputs, "ReSTIR GI.Begin");
            m_Pass.ExecuteInitialSampling(commandContext, inputs, pipelines);
            WriteStageTimestamp(commandList, inputs, "ReSTIR GI.Initial");
        }
        else if (m_Kind == Kind::Temporal)
        {
            m_Pass.ExecuteTemporalResampling(commandContext, inputs, pipelines);
            WriteStageTimestamp(commandList, inputs, "ReSTIR GI.Temporal");
        }
        else if (m_Kind == Kind::Spatial)
        {
            const uint32_t writeIndex = 1u - (inputs.FrameState.Constants.FrameIndex & 1u);
            const ReSTIRGIPass::InternalResources::ReservoirSet& temporal = m_Pass.m_Resources->History[writeIndex].Temporal;
            m_Pass.ExecuteSpatialResampling(commandContext, inputs, pipelines, temporal.Creation, temporal.Hit, temporal.Light);
            WriteStageTimestamp(commandList, inputs, "ReSTIR GI.Spatial");
        }
        else
        {
            const uint32_t writeIndex = 1u - (inputs.FrameState.Constants.FrameIndex & 1u);
            const ReSTIRGIPass::InternalResources::ReservoirSet& spatial = m_Pass.m_Resources->History[writeIndex].Spatial;
            m_Pass.ExecuteFinalShading(commandContext, inputs, pipelines, spatial.Creation, spatial.Hit, spatial.Light);
            WriteStageTimestamp(commandList, inputs, "ReSTIR GI.Shade");
            m_Pass.m_HistoryValid = inputs.FrameState.VariantConfig.EnableTemporalResampling;
        }
    }

private:
    Kind m_Kind;
    ReSTIRGIPass& m_Pass;
    std::shared_ptr<const ReSTIRGIGraphInputs> m_Inputs;
    RenderGraph::ResourceId m_TokenBefore = 0;
    RenderGraph::ResourceId m_TokenAfter = 0;
    RenderGraph::ImportedResourceHandle m_InitialCreation;
    RenderGraph::ImportedResourceHandle m_InitialHit;
    RenderGraph::ImportedResourceHandle m_InitialLight;
    RenderGraph::ImportedResourceHandle m_HistoryReadCreation;
    RenderGraph::ImportedResourceHandle m_HistoryReadHit;
    RenderGraph::ImportedResourceHandle m_HistoryReadLight;
    RenderGraph::ImportedResourceHandle m_TemporalWriteCreation;
    RenderGraph::ImportedResourceHandle m_TemporalWriteHit;
    RenderGraph::ImportedResourceHandle m_TemporalWriteLight;
    RenderGraph::ImportedResourceHandle m_SpatialWriteCreation;
    RenderGraph::ImportedResourceHandle m_SpatialWriteHit;
    RenderGraph::ImportedResourceHandle m_SpatialWriteLight;
};

ReSTIRGIPass::ReSTIRGIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRGIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

ReSTIRGIPass::~ReSTIRGIPass() = default;

void ReSTIRGIPass::EnsurePipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    const ReSTIRGIVariantConfig& variantConfig,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    Assert(variantConfig.MaxPathBounces >= 1u && variantConfig.MaxPathBounces <= 5u,
        "ReSTIR GI path bounce variant is out of range.");
    PipelineSet& pipelines = GetPipelines(useSoftShadowVariant, environmentProjectionVariant);
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Initial, variantConfig, shadingModel, useCompactedDispatch));
    if (variantConfig.EnableTemporalResampling)
    {
        static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Temporal, variantConfig, shadingModel, useCompactedDispatch));
    }
    if (variantConfig.EnableSpatialResampling)
    {
        static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Spatial, variantConfig, shadingModel, useCompactedDispatch));
    }
    static_cast<void>(GetStageShader(pipelines, ReSTIRGIStage::Shade, variantConfig, shadingModel, useCompactedDispatch));
}

void ReSTIRGIPass::AddPasses(
    RenderGraph::RenderGraphBuilder& builder,
    ReSTIRGIGraphInputs inputs)
{
    Assert(inputs.IndirectLighting != 0u && inputs.InputToken != 0u && inputs.OutputToken != 0u,
        "ReSTIR GI graph outputs or tokens are invalid.");
    Assert(inputs.Width > 0u && inputs.Height > 0u, "ReSTIR GI graph dimensions must be positive.");
    Assert(static_cast<bool>(inputs.GetFrameIndex), "ReSTIR GI requires a frame-index resolver.");
    Assert(static_cast<bool>(inputs.ResolveVariantConfig), "ReSTIR GI requires a variant resolver.");
    Assert(static_cast<bool>(inputs.ResolveFrameInputs), "ReSTIR GI requires a frame-input resolver.");
    EnsureResources(inputs.Width, inputs.Height);

    const auto graphInputs = std::make_shared<const ReSTIRGIGraphInputs>(std::move(inputs));
    const auto importTexture = [&builder](const wchar_t* name, const std::shared_ptr<Texture>& texture)
    {
        return builder.ImportResource(name, [texture]() -> const Resource& { return *texture; });
    };
    const auto importDynamicTexture = [&builder](const wchar_t* name, std::function<const Resource&()> resolver)
    {
        return builder.ImportResource(name, std::move(resolver));
    };

    const auto initialCreation = importTexture(L"Framework.ReSTIRGI.InitialCreation", m_Resources->Initial.Creation);
    const auto initialHit = importTexture(L"Framework.ReSTIRGI.InitialHit", m_Resources->Initial.Hit);
    const auto initialLight = importTexture(L"Framework.ReSTIRGI.InitialLight", m_Resources->Initial.Light);

    const auto historyReadCreation = importDynamicTexture(
        L"Framework.ReSTIRGI.HistoryReadCreation",
        [this, graphInputs]() -> const Resource&
        {
            const uint32_t readIndex = graphInputs->GetFrameIndex() & 1u;
            const InternalResources::ReservoirSet& history = graphInputs->ResolveVariantConfig().EnableSpatialResampling
                ? m_Resources->History[readIndex].Spatial
                : m_Resources->History[readIndex].Temporal;
            return *history.Creation;
        });
    const auto historyReadHit = importDynamicTexture(
        L"Framework.ReSTIRGI.HistoryReadHit",
        [this, graphInputs]() -> const Resource&
        {
            const uint32_t readIndex = graphInputs->GetFrameIndex() & 1u;
            const InternalResources::ReservoirSet& history = graphInputs->ResolveVariantConfig().EnableSpatialResampling
                ? m_Resources->History[readIndex].Spatial
                : m_Resources->History[readIndex].Temporal;
            return *history.Hit;
        });
    const auto historyReadLight = importDynamicTexture(
        L"Framework.ReSTIRGI.HistoryReadLight",
        [this, graphInputs]() -> const Resource&
        {
            const uint32_t readIndex = graphInputs->GetFrameIndex() & 1u;
            const InternalResources::ReservoirSet& history = graphInputs->ResolveVariantConfig().EnableSpatialResampling
                ? m_Resources->History[readIndex].Spatial
                : m_Resources->History[readIndex].Temporal;
            return *history.Light;
        });

    const auto temporalWriteCreation = importDynamicTexture(
        L"Framework.ReSTIRGI.TemporalWriteCreation",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Temporal.Creation;
        });
    const auto temporalWriteHit = importDynamicTexture(
        L"Framework.ReSTIRGI.TemporalWriteHit",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Temporal.Hit;
        });
    const auto temporalWriteLight = importDynamicTexture(
        L"Framework.ReSTIRGI.TemporalWriteLight",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Temporal.Light;
        });
    const auto spatialWriteCreation = importDynamicTexture(
        L"Framework.ReSTIRGI.SpatialWriteCreation",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Spatial.Creation;
        });
    const auto spatialWriteHit = importDynamicTexture(
        L"Framework.ReSTIRGI.SpatialWriteHit",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Spatial.Hit;
        });
    const auto spatialWriteLight = importDynamicTexture(
        L"Framework.ReSTIRGI.SpatialWriteLight",
        [this, graphInputs]() -> const Resource&
        {
            return *m_Resources->History[1u - (graphInputs->GetFrameIndex() & 1u)].Spatial.Light;
        });

    const RenderGraph::ResourceId initialFinished = builder.CreateToken(L"Framework.ReSTIRGI.InitialFinished");
    const RenderGraph::ResourceId temporalFinished = builder.CreateToken(L"Framework.ReSTIRGI.TemporalFinished");
    const RenderGraph::ResourceId spatialFinished = builder.CreateToken(L"Framework.ReSTIRGI.SpatialFinished");

    if (graphInputs->UseCompactedDispatch)
    {
        ReSTIRGIGraphPass::Desc desc;
        desc.PassKind = ReSTIRGIGraphPass::Kind::OutputClear;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR GI Output Clear";
        builder.AddPass(std::make_unique<ReSTIRGIGraphPass>(std::move(desc)));
    }

    {
        ReSTIRGIGraphPass::Desc desc;
        desc.PassKind = ReSTIRGIGraphPass::Kind::Initial;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR GI Initial Sampling";
        desc.TokenAfter = initialFinished;
        desc.InitialCreation = initialCreation;
        desc.InitialHit = initialHit;
        desc.InitialLight = initialLight;
        builder.AddPass(std::make_unique<ReSTIRGIGraphPass>(std::move(desc)));
    }

    {
        ReSTIRGIGraphPass::Desc desc;
        desc.PassKind = ReSTIRGIGraphPass::Kind::Temporal;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR GI Temporal Resampling";
        desc.TokenBefore = initialFinished;
        desc.TokenAfter = temporalFinished;
        desc.InitialCreation = initialCreation;
        desc.InitialHit = initialHit;
        desc.InitialLight = initialLight;
        desc.HistoryReadCreation = historyReadCreation;
        desc.HistoryReadHit = historyReadHit;
        desc.HistoryReadLight = historyReadLight;
        desc.TemporalWriteCreation = temporalWriteCreation;
        desc.TemporalWriteHit = temporalWriteHit;
        desc.TemporalWriteLight = temporalWriteLight;
        builder.AddPass(std::make_unique<ReSTIRGIGraphPass>(std::move(desc)));
    }

    {
        ReSTIRGIGraphPass::Desc desc;
        desc.PassKind = ReSTIRGIGraphPass::Kind::Spatial;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR GI Spatial Resampling";
        desc.TokenBefore = temporalFinished;
        desc.TokenAfter = spatialFinished;
        desc.TemporalWriteCreation = temporalWriteCreation;
        desc.TemporalWriteHit = temporalWriteHit;
        desc.TemporalWriteLight = temporalWriteLight;
        desc.SpatialWriteCreation = spatialWriteCreation;
        desc.SpatialWriteHit = spatialWriteHit;
        desc.SpatialWriteLight = spatialWriteLight;
        builder.AddPass(std::make_unique<ReSTIRGIGraphPass>(std::move(desc)));
    }

    {
        ReSTIRGIGraphPass::Desc desc;
        desc.PassKind = ReSTIRGIGraphPass::Kind::Shade;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR GI Shade";
        desc.TokenBefore = spatialFinished;
        desc.SpatialWriteCreation = spatialWriteCreation;
        desc.SpatialWriteHit = spatialWriteHit;
        desc.SpatialWriteLight = spatialWriteLight;
        builder.AddPass(std::make_unique<ReSTIRGIGraphPass>(std::move(desc)));
    }
}

bool ReSTIRGIPass::EnsureResources(const uint32_t width, const uint32_t height)
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return false;
    }

    const auto createReservoirSet = [this, width, height](const wchar_t* prefix)
    {
        InternalResources::ReservoirSet reservoir;
        const std::wstring prefixString(prefix);
        reservoir.Creation = RenderTexture::CreateUav2D(
            m_DeviceContext,
            ReservoirFormat,
            width,
            height,
            prefixString + L" Creation");
        reservoir.Hit = RenderTexture::CreateUav2D(
            m_DeviceContext,
            ReservoirFormat,
            width,
            height,
            prefixString + L" Hit");
        reservoir.Light = RenderTexture::CreateUav2D(
            m_DeviceContext,
            ReservoirFormat,
            width,
            height,
            prefixString + L" Light");
        return reservoir;
    };

    m_Resources = std::make_unique<InternalResources>();
    m_Resources->Initial = createReservoirSet(L"ReSTIR GI Initial");
    m_Resources->History[0].Temporal = createReservoirSet(L"ReSTIR GI History 0 Temporal");
    m_Resources->History[0].Spatial = createReservoirSet(L"ReSTIR GI History 0 Spatial");
    m_Resources->History[1].Temporal = createReservoirSet(L"ReSTIR GI History 1 Temporal");
    m_Resources->History[1].Spatial = createReservoirSet(L"ReSTIR GI History 1 Spatial");
    m_ResourceWidth = width;
    m_ResourceHeight = height;
    m_HistoryValid = false;
    return true;
}

void ReSTIRGIPass::ExecuteInitialSampling(
    CommandContext& commandContext,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Initial,
        inputs.FrameState.VariantConfig,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialCreation", UnorderedAccessView(m_Resources->Initial.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialHit", UnorderedAccessView(m_Resources->Initial.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialLight", UnorderedAccessView(m_Resources->Initial.Light));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRGIPass::ExecuteTemporalResampling(
    CommandContext& commandContext,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Temporal,
        inputs.FrameState.VariantConfig,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialCreation", ShaderResourceView(m_Resources->Initial.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialHit", ShaderResourceView(m_Resources->Initial.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialLight", ShaderResourceView(m_Resources->Initial.Light));
    if (inputs.FrameState.VariantConfig.EnableTemporalResampling)
    {
        const uint32_t historyReadIndex = inputs.FrameState.Constants.FrameIndex & 1u;
        const InternalResources::ReservoirSet& historyRead =
            inputs.FrameState.VariantConfig.EnableSpatialResampling
            ? m_Resources->History[historyReadIndex].Spatial
            : m_Resources->History[historyReadIndex].Temporal;
        commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(historyRead.Creation));
        commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(historyRead.Hit));
        commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(historyRead.Light));
    }
    const uint32_t historyWriteIndex = 1u - (inputs.FrameState.Constants.FrameIndex & 1u);
    const InternalResources::ReservoirSet& historyWrite = m_Resources->History[historyWriteIndex].Temporal;
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalCreation", UnorderedAccessView(historyWrite.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalHit", UnorderedAccessView(historyWrite.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalLight", UnorderedAccessView(historyWrite.Light));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRGIPass::ExecuteSpatialResampling(
    CommandContext& commandContext,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& sourceCreation,
    const std::shared_ptr<Texture>& sourceHit,
    const std::shared_ptr<Texture>& sourceLight)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Spatial,
        inputs.FrameState.VariantConfig,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    const uint32_t historyWriteIndex = 1u - (inputs.FrameState.Constants.FrameIndex & 1u);
    const InternalResources::ReservoirSet& spatialOutput = m_Resources->History[historyWriteIndex].Spatial;
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalCreation", ShaderResourceView(sourceCreation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalHit", ShaderResourceView(sourceHit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalLight", ShaderResourceView(sourceLight));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryCreation", UnorderedAccessView(spatialOutput.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryHit", UnorderedAccessView(spatialOutput.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryLight", UnorderedAccessView(spatialOutput.Light));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRGIPass::ExecuteFinalShading(
    CommandContext& commandContext,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& reservoirCreation,
    const std::shared_ptr<Texture>& reservoirHit,
    const std::shared_ptr<Texture>& reservoirLight)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRGIStage::Shade,
        inputs.FrameState.VariantConfig,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(reservoirCreation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(reservoirHit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(reservoirLight));
    commandContext.SetUnorderedAccessView(shader, "IndirectLighting", UnorderedAccessView(inputs.IndirectLighting));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

uint32_t ReSTIRGIPass::GetPipelineVariantIndex(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    Assert(
        environmentProjectionVariant < 3u,
        "Unsupported ReSTIR GI environment projection variant.");
    return environmentProjectionVariant |
        (useSoftShadowVariant ? 1u : 0u) << 2u;
}

ReSTIRGIPass::PipelineSet& ReSTIRGIPass::GetPipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    std::unique_ptr<PipelineSet>& pipelines = m_Pipelines[
        GetPipelineVariantIndex(useSoftShadowVariant, environmentProjectionVariant)];
    if (pipelines == nullptr)
    {
        pipelines = std::make_unique<PipelineSet>();
        pipelines->UseSoftShadowVariant = useSoftShadowVariant;
        pipelines->EnvironmentProjectionVariant = environmentProjectionVariant;
    }

    return *pipelines;
}

uint32_t ReSTIRGIPass::GetStageVariantKey(
    const ReSTIRGIStage stage,
    const ReSTIRGIVariantConfig& variantConfig,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch) const
{
    uint32_t featureKey = 0u;
    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        featureKey = variantConfig.MaxPathBounces;
        break;

    case ReSTIRGIStage::Shade:
        break;

    case ReSTIRGIStage::Temporal:
        featureKey = variantConfig.EnableTemporalResampling
            ? 1u | ((variantConfig.EnableTemporalJacobian ? 1u : 0u) << 1u)
            : 0u;
        break;

    case ReSTIRGIStage::Spatial:
        featureKey = (variantConfig.EnableRayTracedSpatialBiasCorrection ? 1u : 0u) |
            (variantConfig.EnableSpatialResampling ? 2u : 0u);
        break;
    }

    return featureKey |
        ((useCompactedDispatch ? 1u : 0u) << 7u) |
        (static_cast<uint32_t>(shadingModel) << 8u);
}

std::vector<ShaderVariantDefine> ReSTIRGIPass::GetStageVariantDefines(
    const ReSTIRGIStage stage,
    const ReSTIRGIVariantConfig& variantConfig,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch) const
{
    const auto booleanDefine = [](const char* name, const bool value)
    {
        return ShaderVariantDefine { name, value ? "1" : "0" };
    };

    std::vector<ShaderVariantDefine> defines;
    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        defines = {
            { "RESTIR_GI_MAX_PATH_BOUNCES", std::to_string(variantConfig.MaxPathBounces) },
        };
        break;

    case ReSTIRGIStage::Shade:
        break;

    case ReSTIRGIStage::Temporal:
        defines = {
            booleanDefine("RESTIR_GI_USE_TEMPORAL_REUSE", variantConfig.EnableTemporalResampling),
            booleanDefine(
                "RESTIR_GI_USE_TEMPORAL_JACOBIAN",
                variantConfig.EnableTemporalResampling && variantConfig.EnableTemporalJacobian),
        };
        break;

    case ReSTIRGIStage::Spatial:
        defines = {
            booleanDefine("RESTIR_GI_USE_SPATIAL_REUSE", variantConfig.EnableSpatialResampling),
            booleanDefine(
                "RESTIR_GI_USE_RAY_TRACED_SPATIAL_BIAS_CORRECTION",
                variantConfig.EnableRayTracedSpatialBiasCorrection),
        };
        break;
    }

    defines.push_back({
        "FRAMEWORK_MATERIAL_SHADING_MODEL",
        std::to_string(static_cast<uint32_t>(shadingModel))
    });
    defines.push_back({
        "FRAMEWORK_ACTIVE_PIXEL_LIST",
        useCompactedDispatch ? "1" : "0"
    });
    return defines;
}

ComputeShader& ReSTIRGIPass::GetStageShader(
    PipelineSet& pipelines,
    const ReSTIRGIStage stage,
    const ReSTIRGIVariantConfig& variantConfig,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>>* stageVariants = nullptr;
    const std::wstring* sourceFileName = nullptr;
    std::wstring compiledFileName;
    switch (stage)
    {
    case ReSTIRGIStage::Initial:
        stageVariants = &pipelines.InitialVariants;
        sourceFileName = &m_ShaderSources.Initial;
        compiledFileName = L"ReSTIRGI.Initial.cs.cso";
        break;
    case ReSTIRGIStage::Temporal:
        stageVariants = &pipelines.TemporalVariants;
        sourceFileName = &m_ShaderSources.Temporal;
        compiledFileName = L"ReSTIRGI.Temporal.cs.cso";
        break;
    case ReSTIRGIStage::Spatial:
        stageVariants = &pipelines.SpatialVariants;
        sourceFileName = &m_ShaderSources.Spatial;
        compiledFileName = L"ReSTIRGI.Spatial.cs.cso";
        break;
    case ReSTIRGIStage::Shade:
        stageVariants = &pipelines.ShadeVariants;
        sourceFileName = &m_ShaderSources.Shade;
        compiledFileName = L"ReSTIRGI.Shade.cs.cso";
        break;
    }

    Assert(stageVariants != nullptr && sourceFileName != nullptr, "Unsupported ReSTIR GI stage.");
    const uint32_t variantKey = GetStageVariantKey(stage, variantConfig, shadingModel, useCompactedDispatch);
    auto [shaderIt, inserted] = stageVariants->try_emplace(variantKey);
    if (inserted)
    {
        shaderIt->second = CreateComputeShader(
            compiledFileName + L".variant" + std::to_wstring(variantKey),
            *sourceFileName,
            pipelines.UseSoftShadowVariant,
            pipelines.EnvironmentProjectionVariant,
            GetStageVariantDefines(stage, variantConfig, shadingModel, useCompactedDispatch));
    }

    Assert(shaderIt->second != nullptr, "ReSTIR GI stage shader creation failed.");
    return *shaderIt->second;
}

std::unique_ptr<ComputeShader> ReSTIRGIPass::CreateComputeShader(
    const std::wstring& compiledFileName,
    const std::wstring& sourceFileName,
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    std::vector<ShaderVariantDefine> featureDefines)
{
    ShaderVariantDesc shaderDesc;
    shaderDesc.CompiledFileName = useSoftShadowVariant
        ? compiledFileName + L".softshadow"
        : compiledFileName;
    shaderDesc.SourceFileName = sourceFileName;
    shaderDesc.TargetProfile = ShaderTargetProfile::Compute();
    shaderDesc.DebugName = "Framework ReSTIR GI";
    if (useSoftShadowVariant)
    {
        shaderDesc.Defines = m_ShaderSources.SoftShadowDefines;
    }
    if (environmentProjectionVariant != 0u)
    {
        Assert(
            !m_ShaderSources.EnvironmentProjectionDefineName.empty(),
            "ReSTIR GI environment projection variants require a define name.");
        shaderDesc.CompiledFileName += L".environment" + std::to_wstring(environmentProjectionVariant);
        shaderDesc.Defines.push_back({
            m_ShaderSources.EnvironmentProjectionDefineName,
            std::to_string(environmentProjectionVariant)
        });
    }
    shaderDesc.Defines.insert(
        shaderDesc.Defines.end(),
        std::make_move_iterator(featureDefines.begin()),
        std::make_move_iterator(featureDefines.end()));

    const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
    ComputePipelineDescBuilder pipelineDescBuilder =
        ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob)
            .WithDirectlyIndexedResourceHeap();
    for (const PipelineStaticSamplerContract& contract : m_ShaderSources.StaticSamplerContracts)
    {
        pipelineDescBuilder.WithStaticSamplerContract(contract);
    }
    return std::make_unique<ComputeShader>(
        m_DeviceContext,
        *shaderBlob,
        pipelineDescBuilder.Build());
}
//Modify End
