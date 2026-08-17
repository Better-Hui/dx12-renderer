#include <Framework/Rendering/PostProcess/BloomUpsample.h>
#include <Framework/Blit_VS.h>
#include <Framework/Bloom_Composite_PS.h>
#include <Framework/Bloom_Downsample_PS.h>
#include <Framework/Geometry/Mesh.h>
#include <DirectXMath.h>
#include <DX12Library/Helpers.h>

using namespace DirectX;

namespace
{
	namespace RootParameters
	{
		enum
		{
			SourceTexture = 0,
			Parameters,
			NumRootParameters
		};

		struct ParametersCb
		{
			XMFLOAT2 TexelSize;
			float Intensity;
			float Padding;
		};
	}
}

//Modify Begin:2026-08-17 by BestHui
BloomUpsample::BloomUpsample(FrameworkDeviceContext& deviceContext, CommandList& commandList)
	: m_BlitMesh(Mesh::CreateBlitTriangle(commandList))
{
	auto upsampleShader = std::make_shared<Shader>(
		deviceContext,
		ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
		ShaderBlob(ShaderBytecode_Bloom_Downsample_PS, sizeof ShaderBytecode_Bloom_Downsample_PS),
        PipelineLayoutReflectionOptions{
            .StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) },
            .MaxDescriptorCount = 4096u,
            .ShaderStages = PipelineShaderStageFlags::AllGraphics
        },
		[](RasterPipelineStateBuilder& builder)
		{
			builder.WithAdditiveBlend();
		}
	);
	m_UpsampleMaterial = Material::Create(upsampleShader);

	auto compositeShader = std::make_shared<Shader>(
		deviceContext,
		ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
		ShaderBlob(ShaderBytecode_Bloom_Composite_PS, sizeof ShaderBytecode_Bloom_Composite_PS),
		PipelineLayoutReflectionOptions{
			.StaticSamplerContracts = { PipelineStaticSamplers::LinearClamp(3u) },
			.MaxDescriptorCount = 4096u,
			.ShaderStages = PipelineShaderStageFlags::AllGraphics
		});
	m_CompositeMaterial = Material::Create(compositeShader);
//Modify End
}

void BloomUpsample::Execute(CommandList& commandList, const BloomParameters& parameters, const std::shared_ptr<Texture>& source, const RenderTarget& destination)
{
	PIXScope(commandList, "Bloom Upsample");

	//Modify Begin:2026-08-17 by BestHui
	commandList.TransitionBarrier(*source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList.SetRenderTarget(destination);
	commandList.FlushResourceBarriers();
	//Modify End
	commandList.SetAutomaticViewportAndScissorRect(destination);

	m_UpsampleMaterial->SetShaderResourceView("sourceColorTexture", ShaderResourceView(source));

	RootParameters::ParametersCb parametersCb{};
	parametersCb.Intensity = parameters.Intensity;
	auto sourceDesc = source->GetD3D12ResourceDesc();
	auto fSourceWidth = static_cast<float>(sourceDesc.Width);
	auto fSourceHeight = static_cast<float>(sourceDesc.Height);
	parametersCb.TexelSize = { 0.5f / fSourceWidth , 0.5f / fSourceHeight }; // 0.5 is for more focused blur
	m_UpsampleMaterial->SetAllVariables(parametersCb);

	//Modify Begin:2026-08-17 by BestHui
	m_UpsampleMaterial->Bind(commandList);
	//Modify End
	m_BlitMesh->Draw(commandList);
}

//Modify Begin:2026-08-17 by BestHui
void BloomUpsample::ExecuteComposite(
	CommandList& commandList,
	const BloomParameters& parameters,
	const std::shared_ptr<Texture>& sourceColor,
	const std::shared_ptr<Texture>& bloom,
	const RenderTarget& destination)
{
	PIXScope(commandList, "Bloom Composite");

	commandList.TransitionBarrier(*sourceColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList.TransitionBarrier(*bloom, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList.SetRenderTarget(destination);
	commandList.FlushResourceBarriers();
	commandList.SetAutomaticViewportAndScissorRect(destination);

	m_CompositeMaterial->SetShaderResourceView("sourceColorTexture", ShaderResourceView(sourceColor));
	m_CompositeMaterial->SetShaderResourceView("bloomTexture", ShaderResourceView(bloom));

	RootParameters::ParametersCb parametersCb{};
	parametersCb.Intensity = parameters.Intensity;
	const auto bloomDesc = bloom->GetD3D12ResourceDesc();
	parametersCb.TexelSize = {
		0.5f / static_cast<float>(bloomDesc.Width),
		0.5f / static_cast<float>(bloomDesc.Height)
	};
	m_CompositeMaterial->SetAllVariables(parametersCb);
	m_CompositeMaterial->Bind(commandList);
	m_BlitMesh->Draw(commandList);
}
//Modify End
