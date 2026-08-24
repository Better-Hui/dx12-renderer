//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/BloomUpsample.h>

#include <Framework/Blit_VS.h>
#include <Framework/Bloom_Composite_PS.h>
#include <Framework/Geometry/Mesh.h>

#include <DX12Library/Helpers.h>
#include <DirectXMath.h>

using namespace DirectX;

namespace
{
    struct ParametersCb
    {
        XMFLOAT2 TexelSize;
        float Intensity;
        float Padding;
    };
}

BloomUpsample::BloomUpsample(FrameworkDeviceContext& deviceContext, CommandList& commandList)
    : m_BlitMesh(Mesh::CreateBlitTriangle(commandList))
{
    auto shader = std::make_shared<Shader>(
        deviceContext,
        ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
        ShaderBlob(ShaderBytecode_Bloom_Composite_PS, sizeof ShaderBytecode_Bloom_Composite_PS),
        PipelineLayoutReflectionOptions{
            .StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) },
            .MaxDescriptorCount = 4096u,
            .ShaderStages = PipelineShaderStageFlags::AllGraphics
        });
    m_Material = Material::Create(shader);
}

void BloomUpsample::Execute(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& lowResolutionSource,
    const std::shared_ptr<Texture>& highResolutionSource,
    const RenderTarget& destination)
{
    PIXScope(commandList, "Bloom Upsample");
    ExecuteInternal(
        commandList,
        parameters,
        highResolutionSource,
        lowResolutionSource,
        destination);
}

void BloomUpsample::ExecuteComposite(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& sourceColor,
    const std::shared_ptr<Texture>& bloom,
    const RenderTarget& destination)
{
    PIXScope(commandList, "Bloom Composite");
    ExecuteInternal(commandList, parameters, sourceColor, bloom, destination);
}

void BloomUpsample::ExecuteInternal(
    CommandList& commandList,
    const BloomParameters& parameters,
    const std::shared_ptr<Texture>& sourceColor,
    const std::shared_ptr<Texture>& bloom,
    const RenderTarget& destination)
{
    commandList.SetRenderTarget(destination);
    commandList.SetAutomaticViewportAndScissorRect(destination);

    m_Material->SetShaderResourceView("sourceColorTexture", ShaderResourceView(sourceColor));
    m_Material->SetShaderResourceView("bloomTexture", ShaderResourceView(bloom));

    ParametersCb parametersCb{};
    parametersCb.Intensity = parameters.Intensity;
    const auto bloomDesc = bloom->GetD3D12ResourceDesc();
    parametersCb.TexelSize = {
        0.5f / static_cast<float>(bloomDesc.Width),
        0.5f / static_cast<float>(bloomDesc.Height)
    };
    m_Material->SetAllVariables(parametersCb);
    m_Material->Bind(commandList);
    m_BlitMesh->Draw(commandList);
}
//Modify End
