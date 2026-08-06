#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>

//Modify Begin:2026-07-30 by BestHui
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

struct ReSTIRDIPass::PipelineSet
{
    std::unique_ptr<ComputeShader> RIS;
    std::unique_ptr<ComputeShader> Temporal;
    std::unique_ptr<ComputeShader> Spatial;
    std::unique_ptr<ComputeShader> Shade;
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

    void InsertUavBarrier(CommandList& commandList, const std::shared_ptr<Texture>& texture)
    {
        commandList.UavBarrier(*texture);
    }
}

ReSTIRDIPass::ReSTIRDIPass(
    FrameworkDeviceContext& deviceContext,
    ReSTIRDIShaderSources shaderSources)
    : m_DeviceContext(deviceContext)
    , m_ShaderSources(std::move(shaderSources))
{
}

ReSTIRDIPass::~ReSTIRDIPass() = default;

void ReSTIRDIPass::EnsurePipelines(const bool useSoftShadowVariant)
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

void ReSTIRDIPass::Execute(
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs)
{
    if (!inputs.FrameState.Enabled)
    {
        return;
    }

    Assert(static_cast<bool>(inputs.BindSceneInputs), "ReSTIR DI requires scene input bindings.");
    Assert(inputs.DirectLighting != nullptr, "ReSTIR DI requires a direct lighting output.");
    Assert(inputs.MotionVector != nullptr, "ReSTIR DI requires motion vectors.");

    PipelineSet& pipelines = GetPipelines(inputs.FrameState.UseSoftShadowVariant);
    EnsureResources(inputs.FrameState.Width, inputs.FrameState.Height);

    ExecuteInitialSampling(commandList, inputs, pipelines);
    InsertUavBarrier(commandList, m_Resources->InitialReservoir);
    InsertUavBarrier(commandList, m_Resources->InitialReservoirState);

    ExecuteTemporalResampling(commandList, inputs, pipelines);
    InsertUavBarrier(commandList, m_Resources->TemporalReservoir);
    InsertUavBarrier(commandList, m_Resources->TemporalReservoirState);

    ExecuteSpatialResampling(commandList, inputs, pipelines);
    InsertUavBarrier(commandList, m_Resources->SpatialReservoir);
    InsertUavBarrier(commandList, m_Resources->SpatialReservoirState);

    ExecuteFinalShading(commandList, inputs, pipelines);
    InsertUavBarrier(commandList, inputs.DirectLighting);
}

void ReSTIRDIPass::EnsureResources(const uint32_t width, const uint32_t height)
{
    if (m_Resources != nullptr && m_ResourceWidth == width && m_ResourceHeight == height)
    {
        return;
    }

    m_Resources = std::make_unique<InternalResources>();
    const auto createReservoir = [width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(RESERVOIR_FORMAT, width, height, name);
    };
    const auto createHistoryPosition = [width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(HISTORY_POSITION_FORMAT, width, height, name);
    };
    const auto createHistoryShading = [width, height](const wchar_t* name)
    {
        return RenderTexture::CreateUav2D(HISTORY_SHADING_FORMAT, width, height, name);
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
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.RIS;
    inputs.BindSceneInputs(commandList, shader);

    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoir", UnorderedAccessView(m_Resources->InitialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoirState", UnorderedAccessView(m_Resources->InitialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteTemporalResampling(
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Temporal;
    inputs.BindSceneInputs(commandList, shader);

    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoir", ShaderResourceView(m_Resources->InitialReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoirState", ShaderResourceView(m_Resources->InitialReservoirState));
    commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(inputs.MotionVector));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoir", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirB : m_Resources->ReservoirA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoirState", ShaderResourceView(writeReservoirA ? m_Resources->ReservoirBState : m_Resources->ReservoirAState));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryPosition", ShaderResourceView(writeReservoirA ? m_Resources->HistoryPositionB : m_Resources->HistoryPositionA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryNormalRoughness", ShaderResourceView(writeReservoirA ? m_Resources->HistoryNormalRoughnessB : m_Resources->HistoryNormalRoughnessA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryDiffuseMetallic", ShaderResourceView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicB : m_Resources->HistoryDiffuseMetallicA));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIHistorySpecularOcclusion", ShaderResourceView(writeReservoirA ? m_Resources->HistorySpecularOcclusionB : m_Resources->HistorySpecularOcclusionA));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoir", UnorderedAccessView(m_Resources->TemporalReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoirState", UnorderedAccessView(m_Resources->TemporalReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteSpatialResampling(
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Spatial;
    inputs.BindSceneInputs(commandList, shader);

    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoir", ShaderResourceView(m_Resources->TemporalReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoirState", ShaderResourceView(m_Resources->TemporalReservoirState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(m_Resources->SpatialReservoir));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoirState", UnorderedAccessView(m_Resources->SpatialReservoirState));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

void ReSTIRDIPass::ExecuteFinalShading(
    CommandList& commandList,
    const ReSTIRDIExecutionInputs& inputs,
    PipelineSet& pipelines)
{
    ComputeShader& shader = *pipelines.Shade;
    inputs.BindSceneInputs(commandList, shader);

    const bool writeReservoirA = (inputs.FrameState.FrameIndex & 1u) == 0u;
    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(inputs.FrameState.Constants), &inputs.FrameState.Constants);
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoir", ShaderResourceView(m_Resources->SpatialReservoir));
    commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoirState", ShaderResourceView(m_Resources->SpatialReservoirState));
    commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(inputs.DirectLighting));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirA : m_Resources->ReservoirB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoirState", UnorderedAccessView(writeReservoirA ? m_Resources->ReservoirAState : m_Resources->ReservoirBState));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryPositionA : m_Resources->HistoryPositionB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryNormalRoughnessA : m_Resources->HistoryNormalRoughnessB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(writeReservoirA ? m_Resources->HistoryDiffuseMetallicA : m_Resources->HistoryDiffuseMetallicB));
    commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(writeReservoirA ? m_Resources->HistorySpecularOcclusionA : m_Resources->HistorySpecularOcclusionB));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(Math::DivideByMultiple(inputs.FrameState.Width, 8u), Math::DivideByMultiple(inputs.FrameState.Height, 8u), 1u);
}

ReSTIRDIPass::PipelineSet& ReSTIRDIPass::GetPipelines(const bool useSoftShadowVariant)
{
    EnsurePipelines(useSoftShadowVariant);
    PipelineSet* pipelines = useSoftShadowVariant
        ? m_SoftShadowPipelines.get()
        : m_HardShadowPipelines.get();
    Assert(pipelines != nullptr, "ReSTIR DI pipeline creation failed.");
    return *pipelines;
}

std::unique_ptr<ComputeShader> ReSTIRDIPass::CreateComputeShader(
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
//Modify End
