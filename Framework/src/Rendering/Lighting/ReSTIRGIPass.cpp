#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <utility>

//Modify Begin:2026-08-10 by BestHui
struct ReSTIRGIPass::PipelineSet
{
    std::unique_ptr<ComputeShader> Initial;
    std::unique_ptr<ComputeShader> Temporal;
    std::unique_ptr<ComputeShader> Spatial;
    std::unique_ptr<ComputeShader> Shade;
};

struct ReSTIRGIPass::InternalResources
{
    struct ReservoirSet
    {
        std::shared_ptr<Texture> Creation;
        std::shared_ptr<Texture> Hit;
        std::shared_ptr<Texture> Light;
    };

//Modify Begin:2026-07-30 by BestHui
    struct HistorySet
    {
        ReservoirSet Temporal;
        ReservoirSet Spatial;
    };
//Modify End

    ReservoirSet Initial;
//Modify Begin:2026-07-30 by BestHui
    std::array<HistorySet, 2> History;
//Modify End
};

namespace
{
    constexpr DXGI_FORMAT ReservoirFormat = DXGI_FORMAT_R32G32B32A32_UINT;

    void InsertUavBarrier(
        CommandList& commandList,
        const std::shared_ptr<Texture>& creation,
        const std::shared_ptr<Texture>& hit,
        const std::shared_ptr<Texture>& light)
    {
        commandList.UavBarrier(*creation);
        commandList.UavBarrier(*hit);
        commandList.UavBarrier(*light);
    }
}

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
    const uint32_t environmentProjectionVariant)
{
    std::unique_ptr<PipelineSet>& pipelines = m_Pipelines[
        GetPipelineVariantIndex(useSoftShadowVariant, environmentProjectionVariant)];
    if (pipelines != nullptr)
    {
        return;
    }

    pipelines = std::make_unique<PipelineSet>();
    pipelines->Initial = CreateComputeShader(
        L"ReSTIRGI.Initial.cs.cso",
        m_ShaderSources.Initial,
        useSoftShadowVariant,
        environmentProjectionVariant);
    pipelines->Temporal = CreateComputeShader(
        L"ReSTIRGI.Temporal.cs.cso",
        m_ShaderSources.Temporal,
        useSoftShadowVariant,
        environmentProjectionVariant);
    pipelines->Spatial = CreateComputeShader(
        L"ReSTIRGI.Spatial.cs.cso",
        m_ShaderSources.Spatial,
        useSoftShadowVariant,
        environmentProjectionVariant);
    pipelines->Shade = CreateComputeShader(
        L"ReSTIRGI.Shade.cs.cso",
        m_ShaderSources.Shade,
        useSoftShadowVariant,
        environmentProjectionVariant);
}

void ReSTIRGIPass::Execute(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs)
{
    if (!inputs.FrameState.Enabled)
    {
        return;
    }

    Assert(static_cast<bool>(inputs.BindSceneInputs), "ReSTIR GI requires scene input bindings.");
    Assert(inputs.IndirectLighting != nullptr, "ReSTIR GI requires an indirect lighting output.");
    Assert(inputs.MotionVector != nullptr, "ReSTIR GI requires motion vectors.");

    PipelineSet& pipelines = GetPipelines(
        inputs.FrameState.UseSoftShadowVariant,
        inputs.FrameState.EnvironmentProjectionVariant);
//Modify Begin:2026-07-30 by BestHui
    const bool resourcesRecreated = EnsureResources(
        inputs.FrameState.Constants.Width,
        inputs.FrameState.Constants.Height);
    ReSTIRGIExecutionInputs executionInputs = inputs;
    executionInputs.FrameState.Constants.HistoryValid =
        inputs.FrameState.Constants.HistoryValid != 0u && m_HistoryValid && !resourcesRecreated ? 1u : 0u;
//Modify End

//Modify Begin:2026-07-30 by BestHui
    const uint32_t historyWriteIndex = 1u - m_HistoryReadIndex;
    const bool temporalResamplingEnabled = executionInputs.FrameState.Constants.TemporalResamplingEnabled != 0u;
    const bool spatialResamplingEnabled = executionInputs.FrameState.Constants.SpatialResamplingEnabled != 0u;
    const bool requiresTemporalOutput = temporalResamplingEnabled || spatialResamplingEnabled;
//Modify End

    ExecuteInitialSampling(commandList, executionInputs, pipelines);
    InsertUavBarrier(
        commandList,
        m_Resources->Initial.Creation,
        m_Resources->Initial.Hit,
        m_Resources->Initial.Light);

//Modify Begin:2026-08-11 by BestHui
    const InternalResources::ReservoirSet* finalReservoir = &m_Resources->Initial;
    if (requiresTemporalOutput)
    {
        ExecuteTemporalResampling(commandList, executionInputs, pipelines);
        InsertUavBarrier(
            commandList,
            m_Resources->History[historyWriteIndex].Temporal.Creation,
            m_Resources->History[historyWriteIndex].Temporal.Hit,
            m_Resources->History[historyWriteIndex].Temporal.Light);
        finalReservoir = &m_Resources->History[historyWriteIndex].Temporal;
    }

    if (spatialResamplingEnabled)
    {
        ExecuteSpatialResampling(commandList, executionInputs, pipelines);
        InsertUavBarrier(
            commandList,
            m_Resources->History[historyWriteIndex].Spatial.Creation,
            m_Resources->History[historyWriteIndex].Spatial.Hit,
            m_Resources->History[historyWriteIndex].Spatial.Light);
        finalReservoir = &m_Resources->History[historyWriteIndex].Spatial;
    }

    ExecuteFinalShading(
        commandList,
        executionInputs,
        pipelines,
        finalReservoir->Creation,
        finalReservoir->Hit,
        finalReservoir->Light);
//Modify End
    commandList.UavBarrier(*executionInputs.IndirectLighting);
//Modify Begin:2026-08-11 by BestHui
    if (requiresTemporalOutput)
    {
        m_HistoryReadIndex = historyWriteIndex;
    }
    m_HistoryValid = temporalResamplingEnabled;
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
bool ReSTIRGIPass::EnsureResources(const uint32_t width, const uint32_t height)
//Modify End
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return false;
    }

    const auto createReservoirSet = [width, height](const wchar_t* prefix)
    {
        InternalResources::ReservoirSet reservoir;
        const std::wstring prefixString(prefix);
        reservoir.Creation = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Creation");
        reservoir.Hit = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Hit");
        reservoir.Light = RenderTexture::CreateUav2D(
            ReservoirFormat,
            width,
            height,
            prefixString + L" Light");
        return reservoir;
    };

    m_Resources = std::make_unique<InternalResources>();
    m_Resources->Initial = createReservoirSet(L"ReSTIR GI Initial");
//Modify Begin:2026-07-30 by BestHui
    m_Resources->History[0].Temporal = createReservoirSet(L"ReSTIR GI History 0 Temporal");
    m_Resources->History[0].Spatial = createReservoirSet(L"ReSTIR GI History 0 Spatial");
    m_Resources->History[1].Temporal = createReservoirSet(L"ReSTIR GI History 1 Temporal");
    m_Resources->History[1].Spatial = createReservoirSet(L"ReSTIR GI History 1 Spatial");
//Modify End
    m_ResourceWidth = width;
    m_ResourceHeight = height;
    m_HistoryReadIndex = 0;
//Modify Begin:2026-07-30 by BestHui
    m_HistoryValid = false;
    return true;
//Modify End
}

void ReSTIRGIPass::ExecuteInitialSampling(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Initial;
//Modify Begin:2026-07-30 by BestHui
    CommandContext commandContext(commandList);
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialCreation", UnorderedAccessView(m_Resources->Initial.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialHit", UnorderedAccessView(m_Resources->Initial.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIInitialLight", UnorderedAccessView(m_Resources->Initial.Light));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteTemporalResampling(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Temporal;
//Modify Begin:2026-07-30 by BestHui
    CommandContext commandContext(commandList);
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialCreation", ShaderResourceView(m_Resources->Initial.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialHit", ShaderResourceView(m_Resources->Initial.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIInitialLight", ShaderResourceView(m_Resources->Initial.Light));
    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
//Modify Begin:2026-07-30 by BestHui
    const InternalResources::ReservoirSet& historyRead = m_Resources->History[m_HistoryReadIndex].Temporal;
    const InternalResources::ReservoirSet& historyWrite = m_Resources->History[1u - m_HistoryReadIndex].Temporal;
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(historyRead.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(historyRead.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(historyRead.Light));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalCreation", UnorderedAccessView(historyWrite.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalHit", UnorderedAccessView(historyWrite.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGITemporalLight", UnorderedAccessView(historyWrite.Light));
//Modify End
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteSpatialResampling(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Spatial;
//Modify Begin:2026-07-30 by BestHui
    CommandContext commandContext(commandList);
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
//Modify Begin:2026-07-30 by BestHui
    const InternalResources::ReservoirSet& currentTemporal = m_Resources->History[1u - m_HistoryReadIndex].Temporal;
    const InternalResources::ReservoirSet& previousSpatial = m_Resources->History[m_HistoryReadIndex].Spatial;
    const InternalResources::ReservoirSet& spatialOutput = m_Resources->History[1u - m_HistoryReadIndex].Spatial;
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalCreation", ShaderResourceView(currentTemporal.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalHit", ShaderResourceView(currentTemporal.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGITemporalLight", ShaderResourceView(currentTemporal.Light));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIPreviousSpatialCreation", ShaderResourceView(previousSpatial.Creation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIPreviousSpatialHit", ShaderResourceView(previousSpatial.Hit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIPreviousSpatialLight", ShaderResourceView(previousSpatial.Light));
    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryCreation", UnorderedAccessView(spatialOutput.Creation));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryHit", UnorderedAccessView(spatialOutput.Hit));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRGIHistoryLight", UnorderedAccessView(spatialOutput.Light));
//Modify End
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

void ReSTIRGIPass::ExecuteFinalShading(
    CommandList& commandList,
    const ReSTIRGIExecutionInputs& inputs,
    PipelineSet& pipelines,
    const std::shared_ptr<Texture>& reservoirCreation,
    const std::shared_ptr<Texture>& reservoirHit,
    const std::shared_ptr<Texture>& reservoirLight)
{
    ComputeShader& shader = *pipelines.Shade;
//Modify Begin:2026-07-30 by BestHui
    CommandContext commandContext(commandList);
    inputs.BindSceneInputs(commandContext, shader);
//Modify End
    commandContext.SetConstantBuffer(shader, "ReSTIRGIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
//Modify Begin:2026-08-11 by BestHui
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryCreation", ShaderResourceView(reservoirCreation));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryHit", ShaderResourceView(reservoirHit));
    commandContext.SetShaderResourceView(shader, "ReSTIRGIHistoryLight", ShaderResourceView(reservoirLight));
//Modify End
    commandContext.SetUnorderedAccessView(shader, "IndirectLighting", UnorderedAccessView(inputs.IndirectLighting));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(inputs.FrameState.Constants.Width, 8u),
        Math::DivideByMultiple(inputs.FrameState.Constants.Height, 8u),
        1u);
}

size_t ReSTIRGIPass::GetPipelineVariantIndex(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    Assert(
        environmentProjectionVariant < EnvironmentProjectionVariantCount,
        "Unsupported ReSTIR GI environment projection variant.");
    return static_cast<size_t>(environmentProjectionVariant * 2u + (useSoftShadowVariant ? 1u : 0u));
}

ReSTIRGIPass::PipelineSet& ReSTIRGIPass::GetPipelines(
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
{
    EnsurePipelines(useSoftShadowVariant, environmentProjectionVariant);
    PipelineSet* pipelines = m_Pipelines[
        GetPipelineVariantIndex(useSoftShadowVariant, environmentProjectionVariant)].get();
    Assert(pipelines != nullptr, "ReSTIR GI pipeline creation failed.");
    return *pipelines;
}

std::unique_ptr<ComputeShader> ReSTIRGIPass::CreateComputeShader(
    const std::wstring& compiledFileName,
    const std::wstring& sourceFileName,
    const bool useSoftShadowVariant,
    const uint32_t environmentProjectionVariant)
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

    const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
    return std::make_unique<ComputeShader>(
        m_DeviceContext,
        *shaderBlob,
        ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob)
            .WithDirectlyIndexedResourceHeap()
            .Build());
}
//Modify End
