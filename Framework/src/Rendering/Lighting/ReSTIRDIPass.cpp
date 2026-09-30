#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>

//Modify Begin:2026-09-29 by Hui
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

#include <iterator>
#include <unordered_map>

struct ReSTIRDIPass::PipelineSet
{
    bool UseSoftShadowVariant = false;
    uint32_t EnvironmentProjectionVariant = 0u;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> RISVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> TemporalVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> BoilingFilterVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> SpatialVariants;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> ShadeVariants;
};

struct ReSTIRDIPass::InternalResources
{
    std::shared_ptr<Texture> ReservoirA;
    std::shared_ptr<Texture> ReservoirB;
    std::shared_ptr<Texture> ReservoirAState;
    std::shared_ptr<Texture> ReservoirBState;
    std::shared_ptr<Texture> HistoryPositionA;
    std::shared_ptr<Texture> HistoryPositionB;
    std::shared_ptr<Texture> HistoryNormalRoughnessA;
    std::shared_ptr<Texture> HistoryNormalRoughnessB;
    std::shared_ptr<Texture> HistoryDiffuseMetallicA;
    std::shared_ptr<Texture> HistoryDiffuseMetallicB;
    std::shared_ptr<Texture> HistorySpecularOcclusionA;
    std::shared_ptr<Texture> HistorySpecularOcclusionB;
    std::shared_ptr<Texture> InitialReservoir;
    std::shared_ptr<Texture> InitialReservoirState;
    std::shared_ptr<Texture> TemporalReservoir;
    std::shared_ptr<Texture> TemporalReservoirState;
    std::shared_ptr<Texture> SpatialReservoir;
    std::shared_ptr<Texture> SpatialReservoirState;
};

namespace
{
    constexpr DXGI_FORMAT RESERVOIR_FORMAT = DXGI_FORMAT_R32G32B32A32_UINT;
    constexpr DXGI_FORMAT HISTORY_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT HISTORY_SHADING_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const std::wstring ReSTIRDIBoilingFilterShaderSource =
        L"Framework/shaders/ReSTIRDI/ReSTIRDI.Boiling.cs.hlsl";

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
        const ReSTIRDIFrameState& frameState,
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
            Math::DivideByMultiple(frameState.Width, 8u),
            Math::DivideByMultiple(frameState.Height, 8u),
            1u);
    }

    void DeclareReSTIRDISharedResources(
        RenderGraph::RenderGraphPassBuilder& passBuilder,
        const ReSTIRDIGraphInputs& inputs)
    {
        Assert(static_cast<bool>(inputs.DeclareSharedResources),
            "ReSTIR DI requires shared graph-resource declarations.");
        inputs.DeclareSharedResources(passBuilder);
    }
}

class ReSTIRDIGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        OutputClear,
        Initial,
        Temporal,
        BoilingFilter,
        Spatial,
        Shade,
    };

    struct Desc
    {
        Kind PassKind = Kind::Initial;
        ReSTIRDIPass* Pass = nullptr;
        std::shared_ptr<const ReSTIRDIGraphInputs> Inputs;
        std::wstring PassName;
        RenderGraph::ResourceId TokenBefore = 0;
        RenderGraph::ResourceId TokenAfter = 0;
        RenderGraph::ImportedResourceHandle InitialReservoir;
        RenderGraph::ImportedResourceHandle InitialReservoirState;
        RenderGraph::ImportedResourceHandle TemporalReservoir;
        RenderGraph::ImportedResourceHandle TemporalReservoirState;
        RenderGraph::ImportedResourceHandle SpatialReservoir;
        RenderGraph::ImportedResourceHandle SpatialReservoirState;
        RenderGraph::ImportedResourceHandle FinalReservoir;
        RenderGraph::ImportedResourceHandle FinalReservoirState;
        RenderGraph::ImportedResourceHandle HistoryReadReservoir;
        RenderGraph::ImportedResourceHandle HistoryReadReservoirState;
        RenderGraph::ImportedResourceHandle HistoryReadPosition;
        RenderGraph::ImportedResourceHandle HistoryReadNormalRoughness;
        RenderGraph::ImportedResourceHandle HistoryReadDiffuseMetallic;
        RenderGraph::ImportedResourceHandle HistoryReadSpecularOcclusion;
        RenderGraph::ImportedResourceHandle HistoryWriteReservoir;
        RenderGraph::ImportedResourceHandle HistoryWriteReservoirState;
        RenderGraph::ImportedResourceHandle HistoryWritePosition;
        RenderGraph::ImportedResourceHandle HistoryWriteNormalRoughness;
        RenderGraph::ImportedResourceHandle HistoryWriteDiffuseMetallic;
        RenderGraph::ImportedResourceHandle HistoryWriteSpecularOcclusion;
    };

    explicit ReSTIRDIGraphPass(Desc desc)
        : m_Kind(desc.PassKind)
        , m_Pass(*desc.Pass)
        , m_Inputs(std::move(desc.Inputs))
        , m_TokenBefore(desc.TokenBefore)
        , m_TokenAfter(desc.TokenAfter)
        , m_InitialReservoir(std::move(desc.InitialReservoir))
        , m_InitialReservoirState(std::move(desc.InitialReservoirState))
        , m_TemporalReservoir(std::move(desc.TemporalReservoir))
        , m_TemporalReservoirState(std::move(desc.TemporalReservoirState))
        , m_SpatialReservoir(std::move(desc.SpatialReservoir))
        , m_SpatialReservoirState(std::move(desc.SpatialReservoirState))
        , m_FinalReservoir(std::move(desc.FinalReservoir))
        , m_FinalReservoirState(std::move(desc.FinalReservoirState))
        , m_HistoryReadReservoir(std::move(desc.HistoryReadReservoir))
        , m_HistoryReadReservoirState(std::move(desc.HistoryReadReservoirState))
        , m_HistoryReadPosition(std::move(desc.HistoryReadPosition))
        , m_HistoryReadNormalRoughness(std::move(desc.HistoryReadNormalRoughness))
        , m_HistoryReadDiffuseMetallic(std::move(desc.HistoryReadDiffuseMetallic))
        , m_HistoryReadSpecularOcclusion(std::move(desc.HistoryReadSpecularOcclusion))
        , m_HistoryWriteReservoir(std::move(desc.HistoryWriteReservoir))
        , m_HistoryWriteReservoirState(std::move(desc.HistoryWriteReservoirState))
        , m_HistoryWritePosition(std::move(desc.HistoryWritePosition))
        , m_HistoryWriteNormalRoughness(std::move(desc.HistoryWriteNormalRoughness))
        , m_HistoryWriteDiffuseMetallic(std::move(desc.HistoryWriteDiffuseMetallic))
        , m_HistoryWriteSpecularOcclusion(std::move(desc.HistoryWriteSpecularOcclusion))
    {
        Assert(desc.Pass != nullptr && m_Inputs != nullptr, "ReSTIR DI graph pass requires pass inputs.");
        SetPassName(desc.PassName);
        if (m_Kind == Kind::OutputClear)
        {
            RegisterOutput({ m_Inputs->DirectLighting, RenderGraph::OutputType::UnorderedAccess });
            return;
        }

        if (m_Kind != Kind::BoilingFilter)
        {
            RenderGraph::RenderGraphPassBuilder sharedResourceBuilder(RenderGraph::RenderPassQueue::Direct);
            DeclareReSTIRDISharedResources(sharedResourceBuilder, *m_Inputs);
            sharedResourceBuilder.ApplyTo(*this);
        }

        if (m_Kind == Kind::Initial)
        {
            RegisterOutput({ m_InitialReservoir.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_InitialReservoirState.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_InitialReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_InitialReservoirState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else if (m_Kind == Kind::Temporal)
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_InitialReservoir.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_InitialReservoirState.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadReservoir.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadReservoirState.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadPosition.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadNormalRoughness.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadDiffuseMetallic.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_HistoryReadSpecularOcclusion.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_TemporalReservoir.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TemporalReservoirState.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_InitialReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_InitialReservoirState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadReservoirState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadPosition, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadDiffuseMetallic, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryReadSpecularOcclusion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_TemporalReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_TemporalReservoirState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else if (m_Kind == Kind::BoilingFilter)
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterOutput({ m_TemporalReservoir.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TemporalReservoirState.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_TemporalReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, true);
            AddImportedResourceAccess(m_TemporalReservoirState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, true);
        }
        else if (m_Kind == Kind::Spatial)
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_FinalReservoir.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_FinalReservoirState.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_SpatialReservoir.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_SpatialReservoirState.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_TokenAfter, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_FinalReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_FinalReservoirState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_SpatialReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_SpatialReservoirState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
        }
        else
        {
            RegisterInput({ m_TokenBefore, RenderGraph::InputType::Token });
            RegisterInput({ m_FinalReservoir.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_FinalReservoirState.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterOutput({ m_HistoryWriteReservoir.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_HistoryWriteReservoirState.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_HistoryWritePosition.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_HistoryWriteNormalRoughness.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_HistoryWriteDiffuseMetallic.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_HistoryWriteSpecularOcclusion.GetId(), RenderGraph::OutputType::ExternalAccess });
            if (m_Inputs->UseCompactedDispatch)
            {
                RegisterInput({ m_Inputs->DirectLighting, RenderGraph::InputType::UnorderedAccess });
            }
            RegisterOutput({ m_Inputs->DirectLighting, RenderGraph::OutputType::UnorderedAccess });
            RegisterOutput({ m_Inputs->OutputToken, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(m_FinalReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_FinalReservoirState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::ExternalResourceAccessMode::Read, false);
            AddImportedResourceAccess(m_HistoryWriteReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_HistoryWriteReservoirState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_HistoryWritePosition, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_HistoryWriteNormalRoughness, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_HistoryWriteDiffuseMetallic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
            AddImportedResourceAccess(m_HistoryWriteSpecularOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::ExternalResourceAccessMode::Write, false);
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
            commandContext.ClearUnorderedAccessUint(context.GetResource(m_Inputs->DirectLighting), clearValues);
            return;
        }

        ReSTIRDIExecutionInputs inputs = m_Inputs->ResolveFrameInputs(context);
        ReSTIRDIPass::PipelineSet& pipelines = m_Pass.GetPipelines(
            inputs.FrameState.UseSoftShadowVariant,
            inputs.FrameState.EnvironmentProjectionVariant);
        CommandContext commandContext(commandList, passContext.GetBarrierContext());
        if (inputs.PrepareCommandContext)
        {
            inputs.PrepareCommandContext(commandContext);
        }
        if (m_Kind == Kind::Initial)
        {
            m_Pass.ExecuteInitialSampling(commandContext, inputs, pipelines);
        }
        else if (m_Kind == Kind::Temporal)
        {
            m_Pass.ExecuteTemporalResampling(commandContext, inputs, pipelines);
        }
        else if (m_Kind == Kind::BoilingFilter)
        {
            if (inputs.FrameState.Constants.TemporalResamplingEnabled != 0u &&
                inputs.FrameState.Constants.BoilingFilterEnabled != 0u)
            {
                m_Pass.ExecuteBoilingFilter(commandContext, inputs, pipelines);
            }
        }
        else if (m_Kind == Kind::Spatial)
        {
            const bool useTemporal = m_Inputs->EnableTemporalResampling;
            const std::shared_ptr<Texture>& inputReservoir = useTemporal
                ? m_Pass.m_Resources->TemporalReservoir
                : m_Pass.m_Resources->InitialReservoir;
            const std::shared_ptr<Texture>& inputReservoirState = useTemporal
                ? m_Pass.m_Resources->TemporalReservoirState
                : m_Pass.m_Resources->InitialReservoirState;
            m_Pass.ExecuteSpatialResampling(commandContext, inputs, pipelines, inputReservoir, inputReservoirState);
        }
        else
        {
            std::shared_ptr<Texture> reservoir = m_Pass.m_Resources->InitialReservoir;
            std::shared_ptr<Texture> reservoirState = m_Pass.m_Resources->InitialReservoirState;
            if (m_Inputs->EnableTemporalResampling)
            {
                reservoir = m_Pass.m_Resources->TemporalReservoir;
                reservoirState = m_Pass.m_Resources->TemporalReservoirState;
            }
            if (m_Inputs->EnableSpatialResampling)
            {
                reservoir = m_Pass.m_Resources->SpatialReservoir;
                reservoirState = m_Pass.m_Resources->SpatialReservoirState;
            }
            m_Pass.ExecuteFinalShading(commandContext, inputs, pipelines, reservoir, reservoirState);
        }
    }

private:
    Kind m_Kind;
    ReSTIRDIPass& m_Pass;
    std::shared_ptr<const ReSTIRDIGraphInputs> m_Inputs;
    RenderGraph::ResourceId m_TokenBefore = 0;
    RenderGraph::ResourceId m_TokenAfter = 0;
    RenderGraph::ImportedResourceHandle m_InitialReservoir;
    RenderGraph::ImportedResourceHandle m_InitialReservoirState;
    RenderGraph::ImportedResourceHandle m_TemporalReservoir;
    RenderGraph::ImportedResourceHandle m_TemporalReservoirState;
    RenderGraph::ImportedResourceHandle m_SpatialReservoir;
    RenderGraph::ImportedResourceHandle m_SpatialReservoirState;
    RenderGraph::ImportedResourceHandle m_FinalReservoir;
    RenderGraph::ImportedResourceHandle m_FinalReservoirState;
    RenderGraph::ImportedResourceHandle m_HistoryReadReservoir;
    RenderGraph::ImportedResourceHandle m_HistoryReadReservoirState;
    RenderGraph::ImportedResourceHandle m_HistoryReadPosition;
    RenderGraph::ImportedResourceHandle m_HistoryReadNormalRoughness;
    RenderGraph::ImportedResourceHandle m_HistoryReadDiffuseMetallic;
    RenderGraph::ImportedResourceHandle m_HistoryReadSpecularOcclusion;
    RenderGraph::ImportedResourceHandle m_HistoryWriteReservoir;
    RenderGraph::ImportedResourceHandle m_HistoryWriteReservoirState;
    RenderGraph::ImportedResourceHandle m_HistoryWritePosition;
    RenderGraph::ImportedResourceHandle m_HistoryWriteNormalRoughness;
    RenderGraph::ImportedResourceHandle m_HistoryWriteDiffuseMetallic;
    RenderGraph::ImportedResourceHandle m_HistoryWriteSpecularOcclusion;
};

ReSTIRDIPass::ReSTIRDIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRDIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

ReSTIRDIPass::~ReSTIRDIPass() = default;

void ReSTIRDIPass::EnsurePipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    PipelineSet& pipelines = GetPipelines(useSoftShadowVariant, environmentProjectionVariant);
    GetStageShader(pipelines, ReSTIRDIStage::RIS, constants, shadingModel, useCompactedDispatch);
    GetStageShader(pipelines, ReSTIRDIStage::Temporal, constants, shadingModel, useCompactedDispatch);
    if (constants.TemporalResamplingEnabled != 0u && constants.BoilingFilterEnabled != 0u)
    {
        GetStageShader(pipelines, ReSTIRDIStage::BoilingFilter, constants, shadingModel, false);
    }
    GetStageShader(pipelines, ReSTIRDIStage::Spatial, constants, shadingModel, useCompactedDispatch);
    GetStageShader(pipelines, ReSTIRDIStage::Shade, constants, shadingModel, useCompactedDispatch);
}

void ReSTIRDIPass::AddPasses(
    RenderGraph::RenderGraphBuilder& builder,
    ReSTIRDIGraphInputs inputs)
{
    Assert(inputs.DirectLighting != 0u && inputs.InputToken != 0u && inputs.OutputToken != 0u,
        "ReSTIR DI graph outputs or tokens are invalid.");
    Assert(inputs.Width > 0u && inputs.Height > 0u, "ReSTIR DI graph dimensions must be positive.");
    Assert(static_cast<bool>(inputs.GetFrameIndex), "ReSTIR DI requires a frame-index resolver.");
    Assert(static_cast<bool>(inputs.ResolveFrameInputs), "ReSTIR DI requires a frame-input resolver.");
    EnsureResources(inputs.Width, inputs.Height);

    const auto graphInputs = std::make_shared<const ReSTIRDIGraphInputs>(std::move(inputs));
    const auto importTexture = [this, &builder](const wchar_t* name, const std::shared_ptr<Texture>& texture)
    {
        return builder.ImportResource(name, [texture]() -> const Resource& { return *texture; });
    };
    const auto importDynamicTexture = [&builder](const wchar_t* name, std::function<const Resource&()> resolver)
    {
        return builder.ImportResource(name, std::move(resolver));
    };

    const auto initialReservoir = importTexture(
        L"Framework.ReSTIRDI.InitialReservoir", m_Resources->InitialReservoir);
    const auto initialReservoirState = importTexture(
        L"Framework.ReSTIRDI.InitialReservoirState", m_Resources->InitialReservoirState);
    const auto temporalReservoir = importTexture(
        L"Framework.ReSTIRDI.TemporalReservoir", m_Resources->TemporalReservoir);
    const auto temporalReservoirState = importTexture(
        L"Framework.ReSTIRDI.TemporalReservoirState", m_Resources->TemporalReservoirState);
    const auto spatialReservoir = importTexture(
        L"Framework.ReSTIRDI.SpatialReservoir", m_Resources->SpatialReservoir);
    const auto spatialReservoirState = importTexture(
        L"Framework.ReSTIRDI.SpatialReservoirState", m_Resources->SpatialReservoirState);

    const auto historyReadReservoir = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadReservoir",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->ReservoirB
                : *m_Resources->ReservoirA;
        });
    const auto historyReadReservoirState = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadReservoirState",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->ReservoirBState
                : *m_Resources->ReservoirAState;
        });
    const auto historyReadPosition = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadPosition",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryPositionB
                : *m_Resources->HistoryPositionA;
        });
    const auto historyReadNormalRoughness = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadNormalRoughness",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryNormalRoughnessB
                : *m_Resources->HistoryNormalRoughnessA;
        });
    const auto historyReadDiffuseMetallic = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadDiffuseMetallic",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryDiffuseMetallicB
                : *m_Resources->HistoryDiffuseMetallicA;
        });
    const auto historyReadSpecularOcclusion = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryReadSpecularOcclusion",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistorySpecularOcclusionB
                : *m_Resources->HistorySpecularOcclusionA;
        });

    const auto historyWriteReservoir = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWriteReservoir",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->ReservoirA
                : *m_Resources->ReservoirB;
        });
    const auto historyWriteReservoirState = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWriteReservoirState",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->ReservoirAState
                : *m_Resources->ReservoirBState;
        });
    const auto historyWritePosition = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWritePosition",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryPositionA
                : *m_Resources->HistoryPositionB;
        });
    const auto historyWriteNormalRoughness = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWriteNormalRoughness",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryNormalRoughnessA
                : *m_Resources->HistoryNormalRoughnessB;
        });
    const auto historyWriteDiffuseMetallic = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWriteDiffuseMetallic",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistoryDiffuseMetallicA
                : *m_Resources->HistoryDiffuseMetallicB;
        });
    const auto historyWriteSpecularOcclusion = importDynamicTexture(
        L"Framework.ReSTIRDI.HistoryWriteSpecularOcclusion",
        [this, graphInputs]() -> const Resource&
        {
            return (graphInputs->GetFrameIndex() & 1u) == 0u
                ? *m_Resources->HistorySpecularOcclusionA
                : *m_Resources->HistorySpecularOcclusionB;
        });

    const RenderGraph::ResourceId initialFinished = builder.CreateToken(L"Framework.ReSTIRDI.InitialFinished");
    const RenderGraph::ResourceId temporalFinished = builder.CreateToken(L"Framework.ReSTIRDI.TemporalFinished");
    const RenderGraph::ResourceId boilingFinished = builder.CreateToken(L"Framework.ReSTIRDI.BoilingFinished");
    const RenderGraph::ResourceId spatialFinished = builder.CreateToken(L"Framework.ReSTIRDI.SpatialFinished");

    if (graphInputs->UseCompactedDispatch)
    {
        ReSTIRDIGraphPass::Desc desc;
        desc.PassKind = ReSTIRDIGraphPass::Kind::OutputClear;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR DI Output Clear";
        builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
    }

    {
        ReSTIRDIGraphPass::Desc desc;
        desc.PassKind = ReSTIRDIGraphPass::Kind::Initial;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR DI Initial Sampling";
        desc.TokenAfter = initialFinished;
        desc.InitialReservoir = initialReservoir;
        desc.InitialReservoirState = initialReservoirState;
        builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
    }

    RenderGraph::ResourceId previousToken = initialFinished;
    RenderGraph::ImportedResourceHandle finalReservoir = initialReservoir;
    RenderGraph::ImportedResourceHandle finalReservoirState = initialReservoirState;
    if (graphInputs->EnableTemporalResampling)
    {
        ReSTIRDIGraphPass::Desc desc;
        desc.PassKind = ReSTIRDIGraphPass::Kind::Temporal;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR DI Temporal Resampling";
        desc.TokenBefore = previousToken;
        desc.TokenAfter = temporalFinished;
        desc.InitialReservoir = initialReservoir;
        desc.InitialReservoirState = initialReservoirState;
        desc.TemporalReservoir = temporalReservoir;
        desc.TemporalReservoirState = temporalReservoirState;
        desc.HistoryReadReservoir = historyReadReservoir;
        desc.HistoryReadReservoirState = historyReadReservoirState;
        desc.HistoryReadPosition = historyReadPosition;
        desc.HistoryReadNormalRoughness = historyReadNormalRoughness;
        desc.HistoryReadDiffuseMetallic = historyReadDiffuseMetallic;
        desc.HistoryReadSpecularOcclusion = historyReadSpecularOcclusion;
        builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
        previousToken = temporalFinished;
        finalReservoir = temporalReservoir;
        finalReservoirState = temporalReservoirState;

        if (graphInputs->EnableBoilingFilter)
        {
            ReSTIRDIGraphPass::Desc desc;
            desc.PassKind = ReSTIRDIGraphPass::Kind::BoilingFilter;
            desc.Pass = this;
            desc.Inputs = graphInputs;
            desc.PassName = L"ReSTIR DI Boiling Filter";
            desc.TokenBefore = previousToken;
            desc.TokenAfter = boilingFinished;
            desc.TemporalReservoir = temporalReservoir;
            desc.TemporalReservoirState = temporalReservoirState;
            builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
            previousToken = boilingFinished;
        }
    }

    if (graphInputs->EnableSpatialResampling)
    {
        const auto spatialInputReservoir = finalReservoir;
        const auto spatialInputReservoirState = finalReservoirState;
        ReSTIRDIGraphPass::Desc desc;
        desc.PassKind = ReSTIRDIGraphPass::Kind::Spatial;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR DI Spatial Resampling";
        desc.TokenBefore = previousToken;
        desc.TokenAfter = spatialFinished;
        desc.FinalReservoir = spatialInputReservoir;
        desc.FinalReservoirState = spatialInputReservoirState;
        desc.SpatialReservoir = spatialReservoir;
        desc.SpatialReservoirState = spatialReservoirState;
        builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
        previousToken = spatialFinished;
        finalReservoir = spatialReservoir;
        finalReservoirState = spatialReservoirState;
    }

    {
        ReSTIRDIGraphPass::Desc desc;
        desc.PassKind = ReSTIRDIGraphPass::Kind::Shade;
        desc.Pass = this;
        desc.Inputs = graphInputs;
        desc.PassName = L"ReSTIR DI Shade";
        desc.TokenBefore = previousToken;
        desc.FinalReservoir = finalReservoir;
        desc.FinalReservoirState = finalReservoirState;
        desc.HistoryWriteReservoir = historyWriteReservoir;
        desc.HistoryWriteReservoirState = historyWriteReservoirState;
        desc.HistoryWritePosition = historyWritePosition;
        desc.HistoryWriteNormalRoughness = historyWriteNormalRoughness;
        desc.HistoryWriteDiffuseMetallic = historyWriteDiffuseMetallic;
        desc.HistoryWriteSpecularOcclusion = historyWriteSpecularOcclusion;
        builder.AddPass(std::make_unique<ReSTIRDIGraphPass>(std::move(desc)));
    }
}

void ReSTIRDIPass::EnsureResources(const uint32_t width, const uint32_t height)
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return;
    }

    m_Resources = std::make_unique<InternalResources>();
    const auto createReservoir = [this, width, height](const wchar_t* name)
    {
        std::shared_ptr<Texture> texture = RenderTexture::CreateUav2D(
            m_DeviceContext, RESERVOIR_FORMAT, width, height, name);
        return texture;
    };
    const auto createHistoryPosition = [this, width, height](const wchar_t* name)
    {
        std::shared_ptr<Texture> texture = RenderTexture::CreateUav2D(
            m_DeviceContext, HISTORY_POSITION_FORMAT, width, height, name);
        return texture;
    };
    const auto createHistoryShading = [this, width, height](const wchar_t* name)
    {
        std::shared_ptr<Texture> texture = RenderTexture::CreateUav2D(
            m_DeviceContext, HISTORY_SHADING_FORMAT, width, height, name);
        return texture;
    };

    m_Resources->ReservoirA = createReservoir(L"ReSTIR DI Reservoir A");
    m_Resources->ReservoirB = createReservoir(L"ReSTIR DI Reservoir B");
    m_Resources->ReservoirAState = createReservoir(L"ReSTIR DI Reservoir A State");
    m_Resources->ReservoirBState = createReservoir(L"ReSTIR DI Reservoir B State");
    m_Resources->HistoryPositionA = createHistoryPosition(L"ReSTIR DI History Position A");
    m_Resources->HistoryPositionB = createHistoryPosition(L"ReSTIR DI History Position B");
    m_Resources->HistoryNormalRoughnessA = createHistoryShading(L"ReSTIR DI History Normal Roughness A");
    m_Resources->HistoryNormalRoughnessB = createHistoryShading(L"ReSTIR DI History Normal Roughness B");
    m_Resources->HistoryDiffuseMetallicA = createHistoryShading(L"ReSTIR DI History Diffuse Metallic A");
    m_Resources->HistoryDiffuseMetallicB = createHistoryShading(L"ReSTIR DI History Diffuse Metallic B");
    m_Resources->HistorySpecularOcclusionA = createHistoryShading(L"ReSTIR DI History Specular Occlusion A");
    m_Resources->HistorySpecularOcclusionB = createHistoryShading(L"ReSTIR DI History Specular Occlusion B");
    m_Resources->InitialReservoir = createReservoir(L"ReSTIR DI RIS Reservoir");
    m_Resources->InitialReservoirState = createReservoir(L"ReSTIR DI RIS Reservoir State");
    m_Resources->TemporalReservoir = createReservoir(L"ReSTIR DI Temporal Reservoir");
    m_Resources->TemporalReservoirState = createReservoir(L"ReSTIR DI Temporal Reservoir State");
    m_Resources->SpatialReservoir = createReservoir(L"ReSTIR DI Spatial Reservoir");
    m_Resources->SpatialReservoirState = createReservoir(L"ReSTIR DI Spatial Reservoir State");
    m_ResourceWidth = width;
    m_ResourceHeight = height;
}

void ReSTIRDIPass::ExecuteInitialSampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::RIS,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    if (shader.HasConstantBuffer("ReSTIRDIConstants"))
    {
        commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    }
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoir", UnorderedAccessView(m_Resources->InitialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoirState", UnorderedAccessView(m_Resources->InitialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRDIPass::ExecuteTemporalResampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Temporal,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    if (shader.HasConstantBuffer("ReSTIRDIConstants"))
    {
        commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    }
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoir", ShaderResourceView(m_Resources->InitialReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoirState", ShaderResourceView(m_Resources->InitialReservoirState));
    if (shader.HasShaderResourceView("MotionVectorTexture"))
    {
        commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoir", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirB : m_Resources->ReservoirA));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoirState", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirBState : m_Resources->ReservoirAState));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryPosition", ShaderResourceView(writeReservoirA ? m_Resources->HistoryPositionB : m_Resources->HistoryPositionA));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryNormalRoughness", ShaderResourceView(writeReservoirA ? m_Resources->HistoryNormalRoughnessB : m_Resources->HistoryNormalRoughnessA));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryDiffuseMetallic", ShaderResourceView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicB : m_Resources->HistoryDiffuseMetallicA));
        commandContext.SetShaderResourceView(shader, "ReSTIRDIHistorySpecularOcclusion", ShaderResourceView(writeReservoirA ? m_Resources->HistorySpecularOcclusionB : m_Resources->HistorySpecularOcclusionA));
    }
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoir", UnorderedAccessView(m_Resources->TemporalReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoirState", UnorderedAccessView(m_Resources->TemporalReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRDIPass::ExecuteBoilingFilter(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::BoilingFilter,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel,
        false);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIBoilingReservoir", UnorderedAccessView(m_Resources->TemporalReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIBoilingReservoirState", UnorderedAccessView(m_Resources->TemporalReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Height, 8u),
        1u);
}

void ReSTIRDIPass::ExecuteSpatialResampling(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& inputReservoir,
    const std::shared_ptr<Texture>& inputReservoirState)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Spatial,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    if (shader.HasConstantBuffer("ReSTIRDIConstants"))
    {
        commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    }
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoir", ShaderResourceView(inputReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoirState", ShaderResourceView(inputReservoirState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(m_Resources->SpatialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoirState", UnorderedAccessView(m_Resources->SpatialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

void ReSTIRDIPass::ExecuteFinalShading(
    CommandContext& commandContext,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& finalReservoir,
    const std::shared_ptr<Texture>& finalReservoirState)
{
    ComputeShader& shader = GetStageShader(
        pipelines,
        ReSTIRDIStage::Shade,
        inputs.FrameState.Constants,
        inputs.FrameState.ShadingModel,
        inputs.CompactedDispatch.IsValid());
    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    inputs.BindSceneInputs(commandContext, shader);
    BindActivePixelList(commandContext, shader, inputs.CompactedDispatch);
    if (shader.HasConstantBuffer("ReSTIRDIConstants"))
    {
        commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    }
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoir", ShaderResourceView(finalReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoirState", ShaderResourceView(finalReservoirState));
    commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(inputs.DirectLighting));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirA : m_Resources->ReservoirB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoirState", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirAState : m_Resources->ReservoirBState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryPositionA : m_Resources->HistoryPositionB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryNormalRoughnessA : m_Resources->HistoryNormalRoughnessB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicA : m_Resources->HistoryDiffuseMetallicB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(writeReservoirA ? m_Resources->HistorySpecularOcclusionA : m_Resources->HistorySpecularOcclusionB));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    DispatchReSTIRStage(commandContext, inputs.FrameState, inputs.CompactedDispatch);
}

size_t ReSTIRDIPass::GetPipelineVariantIndex(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    Assert(
        environmentProjectionVariant < EnvironmentProjectionVariantCount,
        "Unsupported ReSTIR DI environment projection variant.");
    return static_cast<size_t>(environmentProjectionVariant * 2u + (useSoftShadowVariant ? 1u : 0u));
}

uint32_t ReSTIRDIPass::GetStageVariantKey(
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    if (stage == ReSTIRDIStage::BoilingFilter)
    {
        return 0u;
    }

    uint32_t featureKey = 0u;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        featureKey = constants.InitialVisibilityEnabled != 0u ? 1u : 0u;
        break;

    case ReSTIRDIStage::Temporal:
        if (constants.TemporalResamplingEnabled == 0u)
        {
            break;
        }
        Assert(constants.TemporalBiasCorrectionMode <= 2u, "Unsupported ReSTIR DI temporal bias correction mode.");
        featureKey = 1u |
            ((constants.TemporalBiasCorrectionMode & 0x3u) << 1u) |
            ((constants.TemporalVisibilityShortcutEnabled != 0u ? 1u : 0u) << 3u) |
            ((constants.TemporalPermutationSamplingEnabled != 0u ? 1u : 0u) << 4u) |
            ((constants.TemporalIgnoreGeometryEnabled != 0u ? 1u : 0u) << 5u);
        break;

    case ReSTIRDIStage::BoilingFilter:
        break;

    case ReSTIRDIStage::Spatial:
        if (constants.SpatialResamplingEnabled == 0u)
        {
            break;
        }
        Assert(constants.SpatialBiasCorrectionMode <= 3u, "Unsupported ReSTIR DI spatial bias correction mode.");
        featureKey = 1u |
            ((constants.SpatialBiasCorrectionMode & 0x3u) << 1u) |
            ((constants.SpatialMaterialSimilarityTestEnabled != 0u ? 1u : 0u) << 3u);
        break;

    case ReSTIRDIStage::Shade:
        if (constants.FinalVisibilityEnabled == 0u)
        {
            break;
        }
        featureKey = 1u |
            ((constants.FinalVisibilityReuseEnabled != 0u ? 1u : 0u) << 1u) |
            ((constants.FinalVisibilityDiscardInvisibleSamples != 0u ? 1u : 0u) << 2u);
        break;
    }

    return featureKey |
        ((useCompactedDispatch ? 1u : 0u) << 7u) |
        (static_cast<uint32_t>(shadingModel) << 8u);
}

std::vector<ShaderVariantDefine> ReSTIRDIPass::GetStageVariantDefines(
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    if (stage == ReSTIRDIStage::BoilingFilter)
    {
        return {};
    }

    const auto booleanDefine = [](const char* name, const bool value)
    {
        return ShaderVariantDefine { name, value ? "1" : "0" };
    };

    std::vector<ShaderVariantDefine> defines;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        defines = {
            booleanDefine("RESTIR_DI_USE_INITIAL_VISIBILITY", constants.InitialVisibilityEnabled != 0u),
        };
        break;

    case ReSTIRDIStage::Temporal:
        defines = {
            booleanDefine("RESTIR_DI_USE_TEMPORAL_REUSE", constants.TemporalResamplingEnabled != 0u),
            { "RESTIR_DI_TEMPORAL_BIAS_MODE", std::to_string(constants.TemporalBiasCorrectionMode) },
            booleanDefine("RESTIR_DI_USE_TEMPORAL_VISIBILITY_SHORTCUT", constants.TemporalVisibilityShortcutEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING", constants.TemporalPermutationSamplingEnabled != 0u),
            booleanDefine("RESTIR_DI_TEMPORAL_IGNORE_GEOMETRY", constants.TemporalIgnoreGeometryEnabled != 0u),
        };
        break;

    case ReSTIRDIStage::BoilingFilter:
        break;

    case ReSTIRDIStage::Spatial:
        defines = {
            booleanDefine("RESTIR_DI_USE_SPATIAL_REUSE", constants.SpatialResamplingEnabled != 0u),
            { "RESTIR_DI_SPATIAL_BIAS_MODE", std::to_string(constants.SpatialBiasCorrectionMode) },
            booleanDefine("RESTIR_DI_USE_SPATIAL_MATERIAL_SIMILARITY", constants.SpatialMaterialSimilarityTestEnabled != 0u),
        };
        break;

    case ReSTIRDIStage::Shade:
        defines = {
            booleanDefine("RESTIR_DI_USE_FINAL_VISIBILITY", constants.FinalVisibilityEnabled != 0u),
            booleanDefine("RESTIR_DI_USE_FINAL_VISIBILITY_REUSE", constants.FinalVisibilityReuseEnabled != 0u),
            booleanDefine("RESTIR_DI_DISCARD_INVISIBLE_FINAL_SAMPLES", constants.FinalVisibilityDiscardInvisibleSamples != 0u),
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

ReSTIRDIPass::PipelineSet& ReSTIRDIPass::GetPipelines(
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

    Assert(pipelines != nullptr, "ReSTIR DI pipeline creation failed.");
    return *pipelines;
}

ComputeShader& ReSTIRDIPass::GetStageShader(
    PipelineSet& pipelines,
    const ReSTIRDIStage stage,
    const ReSTIRDIFrameConstants& constants,
    const MaterialShadingModel shadingModel,
    const bool useCompactedDispatch)
{
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>>* stagePipelines = nullptr;
    const std::wstring* sourceFileName = nullptr;
    std::wstring compiledFileName;
    switch (stage)
    {
    case ReSTIRDIStage::RIS:
        stagePipelines = &pipelines.RISVariants;
        compiledFileName = L"ReSTIRDI.RIS.cs.cso";
        sourceFileName = &m_ShaderSources.RIS;
        break;
    case ReSTIRDIStage::Temporal:
        stagePipelines = &pipelines.TemporalVariants;
        compiledFileName = L"ReSTIRDI.Temporal.cs.cso";
        sourceFileName = &m_ShaderSources.Temporal;
        break;
    case ReSTIRDIStage::BoilingFilter:
        stagePipelines = &pipelines.BoilingFilterVariants;
        compiledFileName = L"Framework.ReSTIRDI.Boiling.cs.cso";
        sourceFileName = &ReSTIRDIBoilingFilterShaderSource;
        break;
    case ReSTIRDIStage::Spatial:
        stagePipelines = &pipelines.SpatialVariants;
        compiledFileName = L"ReSTIRDI.Spatial.cs.cso";
        sourceFileName = &m_ShaderSources.Spatial;
        break;
    case ReSTIRDIStage::Shade:
        stagePipelines = &pipelines.ShadeVariants;
        compiledFileName = L"ReSTIRDI.Shade.cs.cso";
        sourceFileName = &m_ShaderSources.Shade;
        break;
    }

    Assert(stagePipelines != nullptr && sourceFileName != nullptr, "Unsupported ReSTIR DI stage.");
    const uint32_t variantKey = GetStageVariantKey(stage, constants, shadingModel, useCompactedDispatch);
    auto [shaderIt, inserted] = stagePipelines->try_emplace(variantKey);
    if (inserted)
    {
        shaderIt->second = CreateComputeShader(
            compiledFileName + L".variant" + std::to_wstring(variantKey),
            *sourceFileName,
            pipelines.UseSoftShadowVariant,
            pipelines.EnvironmentProjectionVariant,
            GetStageVariantDefines(stage, constants, shadingModel, useCompactedDispatch));
    }

    Assert(shaderIt->second != nullptr, "ReSTIR DI stage shader creation failed.");
    return *shaderIt->second;
}

std::unique_ptr<ComputeShader> ReSTIRDIPass::CreateComputeShader(
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
    shaderDesc.DebugName = "Framework ReSTIR DI";
    if (useSoftShadowVariant)
    {
        shaderDesc.Defines = m_ShaderSources.SoftShadowDefines;
    }
    if (environmentProjectionVariant != 0u)
    {
        Assert(
            !m_ShaderSources.EnvironmentProjectionDefineName.empty(),
            "ReSTIR DI environment projection variants require a define name.");
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
