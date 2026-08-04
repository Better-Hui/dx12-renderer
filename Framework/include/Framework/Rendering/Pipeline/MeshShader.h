#pragma once

//Modify Begin:2026-07-30 by BestHui

#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>

#include <functional>
#include <memory>
#include <span>
#include <string>

class CommandContext;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

class MeshShader
{
public:
    explicit MeshShader(
		FrameworkDeviceContext& deviceContext,
        const ShaderBlob& meshShader,
        const ShaderBlob& pixelShader,
        const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
//Modify Begin:2026-07-31 by BestHui
    explicit MeshShader(
		FrameworkDeviceContext& deviceContext,
        const ShaderBlob& amplificationShader,
        const ShaderBlob& meshShader,
        const ShaderBlob& pixelShader,
        const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
//Modify End

    MeshShader(const MeshShader& other) = delete;
    MeshShader& operator=(const MeshShader& other) = delete;

    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }

private:
    friend class CommandContext;
//Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext& GetDeviceContext() const { return m_DeviceContext; }
//Modify End

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);
    const RootSignature& GetRootSignature() const { return *m_RootSignature; }
    const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
    bool UsesReflectedRootSignature() const { return true; }

    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderReflectionMetadata* outMetadata);
    void BuildPipelineLayout();
    void BuildReflectedRootSignature();

    FrameworkDeviceContext& m_DeviceContext;
    std::shared_ptr<RootSignature> m_RootSignature;
//Modify Begin:2026-07-31 by BestHui
    ShaderReflectionMetadata m_AmplificationShaderMetadata;
//Modify End
    ShaderReflectionMetadata m_MeshShaderMetadata;
    ShaderReflectionMetadata m_PixelShaderMetadata;
    std::unique_ptr<PipelineLayout> m_PipelineLayout;
    std::unique_ptr<PipelineBindingSet> m_BindingSet;
    std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
    PipelineDescriptorPool m_DescriptorPool;
    RasterPipelineStateBuilder m_PipelineStateBuilder;
    PipelineStateCache<RasterPipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
};

//Modify End
