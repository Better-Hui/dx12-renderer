//Modify Begin:2026-09-28 by Hui
#include <Framework/Rendering/PostProcess/AccumulationPass.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Accumulation_CS.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>

namespace
{
    struct AccumulationConstants
    {
        uint32_t Width = 1u;
        uint32_t Height = 1u;
        uint32_t PreviousSampleCount = 0u;
        uint32_t Padding = 0u;
    };
}

AccumulationPass::AccumulationPass(FrameworkDeviceContext& deviceContext, Inputs inputs)
    : m_DeviceContext(deviceContext)
    , m_Inputs(std::move(inputs))
{
    Assert(m_Inputs.SceneColor != 0u, "Accumulation pass requires a scene color resource.");
    Assert(m_Inputs.HistoryColor != 0u, "Accumulation pass requires a history resource.");
    Assert(m_Inputs.InputToken != 0u, "Accumulation pass requires an input token.");
    Assert(m_Inputs.OutputToken != 0u, "Accumulation pass requires an output token.");
    Assert(static_cast<bool>(m_Inputs.ResolvePreviousSampleCount),
        "Accumulation pass requires a sample-count resolver.");
    Assert(m_Inputs.Width > 0u && m_Inputs.Height > 0u,
        "Accumulation pass dimensions must be positive.");

    SetPassName(L"Framework Accumulation");
    RegisterInput({ m_Inputs.InputToken, RenderGraph::InputType::Token });
    RegisterInput({ m_Inputs.SceneColor, RenderGraph::InputType::UnorderedAccess });
    RegisterInput({ m_Inputs.HistoryColor, RenderGraph::InputType::UnorderedAccess });
    RegisterOutput({ m_Inputs.SceneColor, RenderGraph::OutputType::UnorderedAccess });
    RegisterOutput({ m_Inputs.HistoryColor, RenderGraph::OutputType::UnorderedAccess });
    RegisterOutput({ m_Inputs.OutputToken, RenderGraph::OutputType::Token });

    const ShaderBlob shader(ShaderBytecode_Accumulation_CS, sizeof ShaderBytecode_Accumulation_CS);
    m_Shader = std::make_unique<ComputeShader>(
        m_DeviceContext,
        shader,
        ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
}

AccumulationPass::~AccumulationPass() = default;

void AccumulationPass::InitImpl(CommandList&)
{
}

void AccumulationPass::ExecuteImpl(
    const RenderGraph::RenderContext& context,
    RenderGraph::RenderPassContext& passContext)
{
    CommandList& commandList = passContext.GetCommandList();
    const AccumulationConstants constants = {
        m_Inputs.Width,
        m_Inputs.Height,
        m_Inputs.ResolvePreviousSampleCount(),
        0u,
    };
    ComputeShader& shader = *m_Shader;
    CommandContext commandContext(commandList, passContext.GetBarrierContext());
    commandContext.SetConstantBuffer(shader, "PostDenoiseAccumulationConstants", constants);
    commandContext.SetUnorderedAccessView(
        shader,
        "SceneColor",
        UnorderedAccessView(context.GetTexture(m_Inputs.SceneColor)));
    commandContext.SetUnorderedAccessView(
        shader,
        "HistoryColor",
        UnorderedAccessView(context.GetTexture(m_Inputs.HistoryColor)));
    commandContext.BindPipeline(shader);
    commandContext.BindDescriptorSet(shader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(m_Inputs.Width, 8u),
        Math::DivideByMultiple(m_Inputs.Height, 8u),
        1u);
}
//Modify End
