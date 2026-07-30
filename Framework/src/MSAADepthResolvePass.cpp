#include "MSAADepthResolvePass.h"
#include <DX12Library/Helpers.h>

//Modify Begin:2026-07-29 by BestHui
#include <Framework/CommandContext.h>
//Modify End
#include <Framework/MSAADepthResolve_CS.h>

namespace
{
    static constexpr uint32_t THREAD_GROUP_SIZE = 16u;
}

//Modify Begin:2026-07-27 by BestHui
MSAADepthResolvePass::MSAADepthResolvePass()
//Modify End
    : m_ComputeShader(
        ShaderBlob(ShaderBytecode_MSAADepthResolve_CS, sizeof(ShaderBytecode_MSAADepthResolve_CS)),
        ComputePipelineDescBuilder::ReflectedDefault(ShaderBlob(ShaderBytecode_MSAADepthResolve_CS, sizeof(ShaderBytecode_MSAADepthResolve_CS))).Build())
{
}

void MSAADepthResolvePass::Resolve(CommandList& commandList, const std::shared_ptr<Texture>& source, const std::shared_ptr<Texture>& destination) const
{
    const auto sourceDesc = source->GetD3D12ResourceDesc();
    const auto destinationDesc = destination->GetD3D12ResourceDesc();
    if (sourceDesc.Width != destinationDesc.Width || sourceDesc.Height != destinationDesc.Height)
    {
        throw std::exception("Source and destination sizes do not match.");
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC sourceSrvDesc{};
    sourceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    sourceSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    sourceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    m_ComputeShader.SetShaderResourceView(commandList, "input", ShaderResourceView(source, sourceSrvDesc));
    m_ComputeShader.SetUnorderedAccessView(commandList, "output", UnorderedAccessView(destination));

//Modify Begin:2026-07-29 by BestHui
    CommandContext commandContext(commandList);
    commandContext.BindPipeline(m_ComputeShader);
    commandContext.BindDescriptorSet(m_ComputeShader.GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(static_cast<uint32_t>(sourceDesc.Width), THREAD_GROUP_SIZE),
        Math::DivideByMultiple(sourceDesc.Height, THREAD_GROUP_SIZE));
//Modify End
}
