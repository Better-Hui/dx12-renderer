#include <Framework/Rendering/PostProcess/BloomPrefilter.h>
#include <Framework/Blit_VS.h>
#include <Framework/Bloom_Prefilter_PS.h>
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
			XMFLOAT4 Filter;
			XMFLOAT2 TexelSize;
			float Intensity;
			float Padding;
		};
	}
}

//Modify Begin:2026-07-27 by BestHui
BloomPrefilter::BloomPrefilter(FrameworkDeviceContext& deviceContext, CommandList& commandList)
//Modify End
	: m_BlitMesh(Mesh::CreateBlitTriangle(commandList))
{
//Modify Begin:2026-07-27 by BestHui
	auto shader = std::make_shared<Shader>(
		deviceContext,
		ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
		ShaderBlob(ShaderBytecode_Bloom_Prefilter_PS, sizeof ShaderBytecode_Bloom_Prefilter_PS)
		);
//Modify End
	m_Material = Material::Create(shader);
}

void BloomPrefilter::Execute(CommandList& commandList, const BloomParameters& parameters, const std::shared_ptr<Texture>& source, const RenderTarget& destination)
{
	PIXScope(commandList, "Bloom Prefilter");

	commandList.SetRenderTarget(destination);
	commandList.SetAutomaticViewportAndScissorRect(destination);

	m_Material->SetShaderResourceView("sourceColorTexture", ShaderResourceView(source));

	RootParameters::ParametersCb parametersCb;
	float knee = parameters.Threshold * parameters.SoftThreshold;
	parametersCb.Filter = { parameters.Threshold, parameters.Threshold - knee, 2.0f * knee, 0.25f / (knee + 0.00001f) };
	parametersCb.Intensity = parameters.Intensity;
	auto sourceDesc = source->GetD3D12ResourceDesc();
	float fSourceWidth = static_cast<float>(sourceDesc.Width);
	float fSourceHeight = static_cast<float>(sourceDesc.Height);
	parametersCb.TexelSize = { 1 / fSourceWidth , 1 / fSourceHeight };
	m_Material->SetAllVariables(parametersCb);

	m_Material->Bind(commandList);
	m_BlitMesh->Draw(commandList);
	m_Material->Unbind(commandList);
}
