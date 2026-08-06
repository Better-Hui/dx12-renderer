#include <FrameworkRenderFeatures/Lighting/ReSTIRDIPass.h>

//Modify Begin:2026-07-30 by BestHui
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

namespace
{
    using namespace FrameworkRenderFeatures;
    using namespace RenderGraph;

    std::vector<Input> CreateGBufferInputs(const ReSTIRDIGBufferResourceIds& gbuffer)
    {
        return {
            { gbuffer.AlbedoOcclusion, InputType::NonPixelShaderResource },
            { gbuffer.SpecularSmoothness, InputType::NonPixelShaderResource },
            { gbuffer.Normal, InputType::NonPixelShaderResource },
            { gbuffer.EmissionMetallic, InputType::NonPixelShaderResource },
            { gbuffer.Position, InputType::NonPixelShaderResource },
            { gbuffer.Depth, InputType::NonPixelShaderResource },
        };
    }

    void AppendHistoryInputs(
        std::vector<Input>& renderGraphInputs,
        const ReSTIRDIHistoryResourceIds& history)
    {
        renderGraphInputs.insert(renderGraphInputs.end(), {
            { history.ReservoirA, InputType::NonPixelShaderResource },
            { history.ReservoirB, InputType::NonPixelShaderResource },
            { history.ReservoirAState, InputType::NonPixelShaderResource },
            { history.ReservoirBState, InputType::NonPixelShaderResource },
            { history.PositionA, InputType::NonPixelShaderResource },
            { history.PositionB, InputType::NonPixelShaderResource },
            { history.NormalRoughnessA, InputType::NonPixelShaderResource },
            { history.NormalRoughnessB, InputType::NonPixelShaderResource },
            { history.DiffuseMetallicA, InputType::NonPixelShaderResource },
            { history.DiffuseMetallicB, InputType::NonPixelShaderResource },
            { history.SpecularOcclusionA, InputType::NonPixelShaderResource },
            { history.SpecularOcclusionB, InputType::NonPixelShaderResource },
        });
    }
}

struct FrameworkRenderFeatures::ReSTIRDIPass::PipelineSet
{
    std::unique_ptr<ComputeShader> RIS;
    std::unique_ptr<ComputeShader> Temporal;
    std::unique_ptr<ComputeShader> Spatial;
    std::unique_ptr<ComputeShader> Shade;
};

FrameworkRenderFeatures::ReSTIRDIPass::ReSTIRDIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRDIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

FrameworkRenderFeatures::ReSTIRDIPass::~ReSTIRDIPass() = default;

void FrameworkRenderFeatures::ReSTIRDIPass::EnsurePipelines(const bool useSoftShadowVariant)
{
    std::unique_ptr<PipelineSet>& pipelines = useSoftShadowVariant
        ? m_SoftShadowPipelines
        : m_HardShadowPipelines;
    if (pipelines != nullptr)
    {
        return;
    }

    pipelines = std::make_unique<PipelineSet>();
    pipelines->RIS = CreateComputeShader(L"ReSTIRDI.RIS.cs.cso", m_ShaderSources.RIS, useSoftShadowVariant);
    pipelines->Temporal = CreateComputeShader(L"ReSTIRDI.Temporal.cs.cso", m_ShaderSources.Temporal, useSoftShadowVariant);
    pipelines->Spatial = CreateComputeShader(L"ReSTIRDI.Spatial.cs.cso", m_ShaderSources.Spatial, useSoftShadowVariant);
    pipelines->Shade = CreateComputeShader(L"ReSTIRDI.Shade.cs.cso", m_ShaderSources.Shade, useSoftShadowVariant);
}

void FrameworkRenderFeatures::ReSTIRDIPass::AddPasses(
    std::vector<std::unique_ptr<RenderGraph::RenderPass>>& renderPasses,
    const ReSTIRDIPassInputs& inputs)
{
    Assert(inputs.SceneAdapter != nullptr, "ReSTIR DI requires a scene adapter.");
    Assert(static_cast<bool>(inputs.SceneAdapter->GetFrameState), "ReSTIR DI requires a frame-state provider.");
    Assert(static_cast<bool>(inputs.SceneAdapter->BindInputs), "ReSTIR DI requires a scene input binder.");

    const ReSTIRDIPassResources& resources = inputs.Resources;
    std::vector<Input> risInputs = CreateGBufferInputs(resources.GBuffer);
    risInputs.insert(risInputs.begin(), { resources.PrerequisiteToken, InputType::Token });

    renderPasses.emplace_back(RenderPass::Create(
        L"ReSTIR DI RIS",
        risInputs,
        {
            { resources.Intermediate.RISReservoir, OutputType::UnorderedAccess },
            { resources.Intermediate.RISReservoirState, OutputType::UnorderedAccess },
            { resources.Intermediate.RISFinishedToken, OutputType::Token },
        },
        [this, inputs](const RenderContext& context, CommandList& commandList)
        {
            ExecuteStage(Stage::RIS, inputs, context, commandList,
                [&context, &resources = inputs.Resources](CommandContext& commandContext, ComputeShader& shader, const ReSTIRDIFrameState&)
                {
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoir", UnorderedAccessView(context.GetTexture(resources.Intermediate.RISReservoir)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoirState", UnorderedAccessView(context.GetTexture(resources.Intermediate.RISReservoirState)));
                });
        }));

    std::vector<Input> temporalInputs = CreateGBufferInputs(resources.GBuffer);
    temporalInputs.insert(temporalInputs.begin(), {
        { resources.Intermediate.RISReservoir, InputType::NonPixelShaderResource },
        { resources.Intermediate.RISReservoirState, InputType::NonPixelShaderResource },
        { resources.GBuffer.MotionVector, InputType::NonPixelShaderResource },
    });
    AppendHistoryInputs(temporalInputs, resources.History);
    temporalInputs.emplace_back(resources.Intermediate.RISFinishedToken, InputType::Token);

    renderPasses.emplace_back(RenderPass::Create(
        L"ReSTIR DI Temporal",
        temporalInputs,
        {
            { resources.Intermediate.TemporalReservoir, OutputType::UnorderedAccess },
            { resources.Intermediate.TemporalReservoirState, OutputType::UnorderedAccess },
            { resources.Intermediate.TemporalFinishedToken, OutputType::Token },
        },
        [this, inputs](const RenderContext& context, CommandList& commandList)
        {
            ExecuteStage(Stage::Temporal, inputs, context, commandList,
                [&context, &resources = inputs.Resources](CommandContext& commandContext, ComputeShader& shader, const ReSTIRDIFrameState& frameState)
                {
                    const bool writeReservoirA = (frameState.FrameIndex & 1u) == 0u;
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoir", ShaderResourceView(context.GetTexture(resources.Intermediate.RISReservoir)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoirState", ShaderResourceView(context.GetTexture(resources.Intermediate.RISReservoirState)));
                    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(context.GetTexture(resources.GBuffer.MotionVector)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoir", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.ReservoirB : resources.History.ReservoirA)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoirState", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.ReservoirBState : resources.History.ReservoirAState)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryPosition", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.PositionB : resources.History.PositionA)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryNormalRoughness", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.NormalRoughnessB : resources.History.NormalRoughnessA)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryDiffuseMetallic", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.DiffuseMetallicB : resources.History.DiffuseMetallicA)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistorySpecularOcclusion", ShaderResourceView(context.GetTexture(writeReservoirA ? resources.History.SpecularOcclusionB : resources.History.SpecularOcclusionA)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoir", UnorderedAccessView(context.GetTexture(resources.Intermediate.TemporalReservoir)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoirState", UnorderedAccessView(context.GetTexture(resources.Intermediate.TemporalReservoirState)));
                });
        }));

    std::vector<Input> spatialInputs = CreateGBufferInputs(resources.GBuffer);
    spatialInputs.insert(spatialInputs.begin(), {
        { resources.Intermediate.TemporalReservoir, InputType::NonPixelShaderResource },
        { resources.Intermediate.TemporalReservoirState, InputType::NonPixelShaderResource },
    });
    spatialInputs.emplace_back(resources.Intermediate.TemporalFinishedToken, InputType::Token);

    renderPasses.emplace_back(RenderPass::Create(
        L"ReSTIR DI Spatial",
        spatialInputs,
        {
            { resources.Intermediate.SpatialReservoir, OutputType::UnorderedAccess },
            { resources.Intermediate.SpatialReservoirState, OutputType::UnorderedAccess },
            { resources.Intermediate.SpatialFinishedToken, OutputType::Token },
        },
        [this, inputs](const RenderContext& context, CommandList& commandList)
        {
            ExecuteStage(Stage::Spatial, inputs, context, commandList,
                [&context, &resources = inputs.Resources](CommandContext& commandContext, ComputeShader& shader, const ReSTIRDIFrameState&)
                {
                    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoir", ShaderResourceView(context.GetTexture(resources.Intermediate.TemporalReservoir)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoirState", ShaderResourceView(context.GetTexture(resources.Intermediate.TemporalReservoirState)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(context.GetTexture(resources.Intermediate.SpatialReservoir)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoirState", UnorderedAccessView(context.GetTexture(resources.Intermediate.SpatialReservoirState)));
                });
        }));

    std::vector<Input> shadeInputs = CreateGBufferInputs(resources.GBuffer);
    shadeInputs.insert(shadeInputs.begin(), {
        { resources.Intermediate.SpatialReservoir, InputType::NonPixelShaderResource },
        { resources.Intermediate.SpatialReservoirState, InputType::NonPixelShaderResource },
    });
    shadeInputs.emplace_back(resources.Intermediate.SpatialFinishedToken, InputType::Token);

    renderPasses.emplace_back(RenderPass::Create(
        L"ReSTIR DI Shade",
        shadeInputs,
        {
            { resources.DirectLighting, OutputType::UnorderedAccess },
            { resources.History.ReservoirA, OutputType::UnorderedAccess },
            { resources.History.ReservoirB, OutputType::UnorderedAccess },
            { resources.History.ReservoirAState, OutputType::UnorderedAccess },
            { resources.History.ReservoirBState, OutputType::UnorderedAccess },
            { resources.History.PositionA, OutputType::UnorderedAccess },
            { resources.History.PositionB, OutputType::UnorderedAccess },
            { resources.History.NormalRoughnessA, OutputType::UnorderedAccess },
            { resources.History.NormalRoughnessB, OutputType::UnorderedAccess },
            { resources.History.DiffuseMetallicA, OutputType::UnorderedAccess },
            { resources.History.DiffuseMetallicB, OutputType::UnorderedAccess },
            { resources.History.SpecularOcclusionA, OutputType::UnorderedAccess },
            { resources.History.SpecularOcclusionB, OutputType::UnorderedAccess },
            { resources.Intermediate.ShadeFinishedToken, OutputType::Token },
        },
        [this, inputs](const RenderContext& context, CommandList& commandList)
        {
            ExecuteStage(Stage::Shade, inputs, context, commandList,
                [&context, &resources = inputs.Resources](CommandContext& commandContext, ComputeShader& shader, const ReSTIRDIFrameState& frameState)
                {
                    const bool writeReservoirA = (frameState.FrameIndex & 1u) == 0u;
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoir", ShaderResourceView(context.GetTexture(resources.Intermediate.SpatialReservoir)));
                    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoirState", ShaderResourceView(context.GetTexture(resources.Intermediate.SpatialReservoirState)));
                    commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(context.GetTexture(resources.DirectLighting)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.ReservoirA : resources.History.ReservoirB)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoirState", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.ReservoirAState : resources.History.ReservoirBState)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.PositionA : resources.History.PositionB)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.NormalRoughnessA : resources.History.NormalRoughnessB)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.DiffuseMetallicA : resources.History.DiffuseMetallicB)));
                    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(context.GetTexture(writeReservoirA ? resources.History.SpecularOcclusionA : resources.History.SpecularOcclusionB)));
                });
        }));
}

FrameworkRenderFeatures::ReSTIRDIPass::PipelineSet& FrameworkRenderFeatures::ReSTIRDIPass::GetPipelines(const bool useSoftShadowVariant)
{
    EnsurePipelines(useSoftShadowVariant);
    PipelineSet* pipelines = useSoftShadowVariant
        ? m_SoftShadowPipelines.get()
        : m_HardShadowPipelines.get();
    Assert(pipelines != nullptr, "ReSTIR DI pipeline creation failed.");
    return *pipelines;
}

std::unique_ptr<ComputeShader> FrameworkRenderFeatures::ReSTIRDIPass::CreateComputeShader(
    const std::wstring& compiledFileName,
    const std::wstring& sourceFileName,
    const bool useSoftShadowVariant)
{
    ShaderVariantDesc shaderDesc;
    shaderDesc.CompiledFileName = useSoftShadowVariant
        ? compiledFileName + L".softshadow"
        : compiledFileName;
    shaderDesc.SourceFileName = sourceFileName;
    shaderDesc.TargetProfile = "cs_6_6";
    shaderDesc.DebugName = "Framework ReSTIR DI";
    if (useSoftShadowVariant)
    {
        shaderDesc.Defines = m_ShaderSources.SoftShadowDefines;
    }

    const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
    return std::make_unique<ComputeShader>(
        m_DeviceContext,
        *shaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob)
            .WithDirectlyIndexedResourceHeap()
            .Build());
}

void FrameworkRenderFeatures::ReSTIRDIPass::ExecuteStage(
    const Stage stage,
    const ReSTIRDIPassInputs& inputs,
    const RenderGraph::RenderContext& context,
    CommandList& commandList,
    const std::function<void(CommandContext&, ComputeShader&, const ReSTIRDIFrameState&)>& bindStageResources)
{
    const ReSTIRDIFrameState frameState = inputs.SceneAdapter->GetFrameState(context);
    if (!frameState.Enabled)
    {
        return;
    }

    PipelineSet& pipelines = GetPipelines(frameState.UseSoftShadowVariant);
    ComputeShader* shader = nullptr;
    switch (stage)
    {
    case Stage::RIS:
        shader = pipelines.RIS.get();
        break;
    case Stage::Temporal:
        shader = pipelines.Temporal.get();
        break;
    case Stage::Spatial:
        shader = pipelines.Spatial.get();
        break;
    case Stage::Shade:
        shader = pipelines.Shade.get();
        break;
    }
    Assert(shader != nullptr, "ReSTIR DI stage has no compute shader.");

    inputs.SceneAdapter->BindInputs(context, commandList, *shader);
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*shader, "ReSTIRDIConstants", sizeof(frameState.Constants), &frameState.Constants);
    bindStageResources(commandContext, *shader, frameState);
    commandContext.BindPipeline(*shader);
    commandContext.BindDescriptorSet(shader->GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(frameState.Width, 8u),
        Math::DivideByMultiple(frameState.Height, 8u),
        1u);
}
//Modify End
