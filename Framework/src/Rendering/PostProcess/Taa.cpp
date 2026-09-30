//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/Taa.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Resource.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Texture.h>
#include <Framework/Blit_VS.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Scene/Material.h>
#include <Framework/TAA_Resolve_PS.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>
#include <utility>

namespace
{
    struct RootConstants
    {
        DirectX::XMFLOAT2 TexelSize;
        float ModulationFactor = 0.0f;
    };

    const RenderTarget& GetPassRenderTarget(const RenderGraph::RenderContext& context)
    {
        const std::shared_ptr<RenderTarget>& renderTarget = context.GetRenderTargetInfo().m_RenderTarget;
        Assert(renderTarget != nullptr, "TAA resolve pass requires a RenderGraph render target.");
        return *renderTarget;
    }
}

class TaaGraphPass final : public RenderGraph::RenderPass
{
public:
    enum class Kind
    {
        Resolve,
        CaptureHistory,
    };

    struct Desc
    {
        Kind PassKind = Kind::Resolve;
        TAA* Feature = nullptr;
        std::shared_ptr<const TAA::GraphInputs> Inputs;
        std::wstring PassName;
        RenderGraph::ImportedResourceHandle HistoryRead;
        RenderGraph::ImportedResourceHandle HistoryWrite;
    };

    explicit TaaGraphPass(Desc desc)
        : m_Kind(desc.PassKind)
        , m_Feature(*desc.Feature)
        , m_Inputs(std::move(desc.Inputs))
        , m_HistoryRead(std::move(desc.HistoryRead))
        , m_HistoryWrite(std::move(desc.HistoryWrite))
    {
        Assert(desc.Feature != nullptr && m_Inputs != nullptr, "TAA graph pass requires feature inputs.");
        SetPassName(desc.PassName);
        if (m_Kind == Kind::Resolve)
        {
            RegisterInput({ m_Inputs->InputToken, RenderGraph::InputType::Token });
            RegisterInput({ m_Inputs->CurrentColor, RenderGraph::InputType::ShaderResource });
            RegisterInput({ m_HistoryRead.GetId(), RenderGraph::InputType::ExternalAccess });
            RegisterInput({ m_Inputs->Velocity, RenderGraph::InputType::ShaderResource });
            RegisterOutput({ m_Inputs->Output, RenderGraph::OutputType::RenderTarget });
            AddImportedResourceAccess(
                m_HistoryRead,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                RenderGraph::ExternalResourceAccessMode::Read,
                false);
        }
        else
        {
            RegisterInput({ m_Inputs->Output, RenderGraph::InputType::CopySource });
            RegisterOutput({ m_HistoryWrite.GetId(), RenderGraph::OutputType::ExternalAccess });
            RegisterOutput({ m_Inputs->OutputToken, RenderGraph::OutputType::Token });
            AddImportedResourceAccess(
                m_HistoryWrite,
                D3D12_RESOURCE_STATE_COPY_DEST,
                RenderGraph::ExternalResourceAccessMode::Write,
                false);
        }
    }

protected:
    void InitImpl(CommandList&) override {}

    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override
    {
        CommandList& commandList = passContext.GetCommandList();
        if (m_Kind == Kind::Resolve)
        {
            CommandContext commandContext(commandList, passContext.GetBarrierContext());
            m_Feature.RecordResolve(
                commandContext,
                context.GetTexture(m_Inputs->CurrentColor),
                m_Feature.m_HistoryBuffers[m_Feature.m_HistoryIndex],
                context.GetTexture(m_Inputs->Velocity),
                GetPassRenderTarget(context),
                m_Feature.m_HistoryValid ? m_Inputs->ResolveModulationFactor() : 0.0f,
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight);
            return;
        }

        const uint32_t writeIndex = 1u - m_Feature.m_HistoryIndex;
        CommandContext commandContext(commandList, passContext.GetBarrierContext());
        commandContext.CopyResource(
            *m_Feature.m_HistoryBuffers[writeIndex],
            *context.GetTexture(m_Inputs->Output));
        m_Feature.m_HistoryValid = true;
        m_Feature.m_HistoryCapturePending = true;
    }

private:
    Kind m_Kind;
    TAA& m_Feature;
    std::shared_ptr<const TAA::GraphInputs> m_Inputs;
    RenderGraph::ImportedResourceHandle m_HistoryRead;
    RenderGraph::ImportedResourceHandle m_HistoryWrite;
};

TAA::TAA(
    FrameworkDeviceContext& deviceContext,
    CommandList& commandList,
    const DXGI_FORMAT backBufferFormat,
    const uint32_t width,
    const uint32_t height)
    : m_BlitMesh(Mesh::CreateBlitTriangle(commandList))
    , m_DeviceContext(deviceContext)
    , m_Format(backBufferFormat)
    , m_Width((std::max)(width, 1u))
    , m_Height((std::max)(height, 1u))
{
    Assert(m_Format != DXGI_FORMAT_UNKNOWN, "TAA output format is invalid.");
    auto shader = std::make_shared<Shader>(
        deviceContext,
        ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
        ShaderBlob(ShaderBytecode_TAA_Resolve_PS, sizeof ShaderBytecode_TAA_Resolve_PS),
        PipelineLayoutReflectionOptions{
            .StaticSamplerContracts = {
                PipelineStaticSamplers::PointClamp(2u),
                PipelineStaticSamplers::LinearClamp(3u)
            },
            .MaxDescriptorCount = 4096u,
            .ShaderStages = PipelineShaderStageFlags::AllGraphics
        });
    m_Material = Material::Create(shader);
}

bool TAA::EnsureCreated(const uint32_t width, const uint32_t height)
{
    if (m_Width == width && m_Height == height && m_HistoryBuffers[0] != nullptr)
    {
        return true;
    }

    Assert(width > 0u && height > 0u, "TAA history dimensions are invalid.");
    m_Width = width;
    m_Height = height;
    const auto historyDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_Format, width, height, 1, 1);
    m_HistoryBuffers[0] = std::make_shared<Texture>(
        historyDesc,
        nullptr,
        TextureUsageType::Other,
        L"TAA History 0",
        m_DeviceContext.GetD3D12DeviceContext());
    m_HistoryBuffers[1] = std::make_shared<Texture>(
        historyDesc,
        nullptr,
        TextureUsageType::Other,
        L"TAA History 1",
        m_DeviceContext.GetD3D12DeviceContext());
    ResetHistory();
    return true;
}

void TAA::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(
        inputs.CurrentColor != 0u && inputs.Velocity != 0u && inputs.Output != 0u,
        "TAA graph resources are invalid.");
    Assert(inputs.InputToken != 0u && inputs.OutputToken != 0u, "TAA graph tokens are invalid.");
    Assert(inputs.Width > 0u && inputs.Height > 0u, "TAA graph dimensions are invalid.");
    Assert(static_cast<bool>(inputs.ResolveModulationFactor), "TAA requires a modulation-factor resolver.");
    Assert(!inputs.DiagnosticNamePrefix.empty(), "TAA requires a diagnostic-name prefix.");

    EnsureCreated(inputs.Width, inputs.Height);
    ResetHistory();
    const auto sharedInputs = std::make_shared<const GraphInputs>(std::move(inputs));
    const std::wstring historyReadName = sharedInputs->DiagnosticNamePrefix + L".History.Read";
    const std::wstring historyWriteName = sharedInputs->DiagnosticNamePrefix + L".History.Write";
    const RenderGraph::ImportedResourceHandle historyRead = builder.ImportResource(
        historyReadName.c_str(),
        [this]() -> const Resource& { return *m_HistoryBuffers[m_HistoryIndex]; });
    const RenderGraph::ImportedResourceHandle historyWrite = builder.ImportResource(
        historyWriteName.c_str(),
        [this]() -> const Resource& { return *m_HistoryBuffers[1u - m_HistoryIndex]; });

    builder.AddPass(std::make_unique<TaaGraphPass>(TaaGraphPass::Desc{
        .PassKind = TaaGraphPass::Kind::Resolve,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"TAA Resolve",
        .HistoryRead = historyRead,
    }));

    builder.AddPass(std::make_unique<TaaGraphPass>(TaaGraphPass::Desc{
        .PassKind = TaaGraphPass::Kind::CaptureHistory,
        .Feature = this,
        .Inputs = sharedInputs,
        .PassName = L"TAA Capture History",
        .HistoryWrite = historyWrite,
    }));
}

void TAA::RecordResolve(
    CommandContext& commandContext,
    const std::shared_ptr<Texture>& currentBuffer,
    const std::shared_ptr<Texture>& historyBuffer,
    const std::shared_ptr<Texture>& velocityBuffer,
    const RenderTarget& destination,
    const float modulationFactor,
    const uint32_t width,
    const uint32_t height)
{
    CommandList& commandList = commandContext.GetCommandList();
    PIXScope(commandList, "TAA Resolve");
    RootConstants rootConstants = {};
    rootConstants.TexelSize = {
        1.0f / static_cast<float>((std::max)(width, 1u)),
        1.0f / static_cast<float>((std::max)(height, 1u)),
    };
    rootConstants.ModulationFactor = std::clamp(modulationFactor, 0.0f, 1.0f);
    m_Material->SetAllVariables(rootConstants);
    m_Material->SetShaderResourceView("currentColorBuffer", ShaderResourceView(currentBuffer));
    m_Material->SetShaderResourceView("historyColorBuffer", ShaderResourceView(historyBuffer));
    m_Material->SetShaderResourceView("velocityColorBuffer", ShaderResourceView(velocityBuffer));
    m_Material->Bind(commandList);
    m_BlitMesh->Draw(commandList);
}

void TAA::ResetHistory()
{
    m_HistoryIndex = 0u;
    m_HistoryValid = false;
    m_HistoryCapturePending = false;
}

DirectX::XMFLOAT2 TAA::ComputeJitterOffset() const
{
    DirectX::XMFLOAT2 jitterOffset = JITTER_OFFSETS[m_FrameIndex];
    jitterOffset.x = (jitterOffset.x - 0.5f) / static_cast<float>(m_Width);
    jitterOffset.y = (jitterOffset.y - 0.5f) / static_cast<float>(m_Height);
    return jitterOffset;
}

const DirectX::XMMATRIX& TAA::GetPreviousViewProjectionMatrix() const
{
    return m_PreviousViewProjectionMatrix;
}

DirectX::XMFLOAT2 TAA::GetCurrentJitterOffset() const
{
    return JITTER_OFFSETS[m_FrameIndex];
}

void TAA::OnRenderedFrame(const DirectX::XMMATRIX& viewProjectionMatrix)
{
    if (m_HistoryCapturePending)
    {
        m_HistoryIndex = 1u - m_HistoryIndex;
        m_HistoryCapturePending = false;
    }
    m_PreviousViewProjectionMatrix = viewProjectionMatrix;
    m_FrameIndex = (m_FrameIndex + 1u) % JITTER_OFFSETS_COUNT;
}
//Modify End
