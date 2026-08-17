#include <Framework/Rendering/PostProcess/BloomDownsample.h>
#include <Framework/Blit_VS.h>
#include <Framework/Bloom_Downsample_PS.h>
#include <Framework/Geometry/Mesh.h>
#include <DirectXMath.h>
#include <DX12Library/Helpers.h>

using namespace DirectX;

namespace
{
	namespace RootParameters
	{
		struct ParametersCb
		{
			XMFLOAT2 TexelSize;
			float Intensity;
			float Padding;
		};
	}
}

//Modify Begin:2026-07-27 by Hui
BloomDownsample::BloomDownsample(FrameworkDeviceContext& deviceContext, CommandList& commandList)
//Modify End
	: m_BlitMesh(Mesh::CreateBlitTriangle(commandList))
{
//Modify Begin:2026-07-27 by Hui
	auto shader = std::make_shared<Shader>(
		deviceContext,
		ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
		ShaderBlob(ShaderBytecode_Bloom_Downsample_PS, sizeof ShaderBytecode_Bloom_Downsample_PS),
//Modify Begin:2026-07-30 by Hui
        PipelineLayoutReflectionOptions{
            .StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) },
            .MaxDescriptorCount = 4096u,
            .ShaderStages = PipelineShaderStageFlags::AllGraphics
        }
//Modify End
		);
//Modify End
	m_Material = Material::Create(shader);
}

void BloomDownsample::Execute(CommandList& commandList, const BloomParameters& parameters, const std::shared_ptr<Texture>& source, const RenderTarget& destination)
{
	PIXScope(commandList, "Bloom Downsample");

	commandList.SetRenderTarget(destination);
	commandList.SetAutomaticViewportAndScissorRect(destination);

	m_Material->SetShaderResourceView("sourceColorTexture", ShaderResourceView(source));

	RootParameters::ParametersCb parametersCb{};
	parametersCb.Intensity = parameters.Intensity;
	auto sourceDesc = source->GetD3D12ResourceDesc();
	auto fSourceWidth = static_cast<float>(sourceDesc.Width);
	auto fSourceHeight = static_cast<float>(sourceDesc.Height);
	parametersCb.TexelSize = { 1 / fSourceWidth , 1 / fSourceHeight };
	m_Material->SetAllVariables(parametersCb);

	//Modify Begin:2026-08-17 by Hui
	m_Material->Bind(commandList);
	//Modify End
	m_BlitMesh->Draw(commandList);
}
