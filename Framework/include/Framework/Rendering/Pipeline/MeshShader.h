#pragma once

//Modify Begin:2026-07-31 by Hui

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
class FrameworkDeviceContext;

class MeshShader
{
public:
    explicit MeshShader(
		FrameworkDeviceContext& deviceContext,
        const ShaderBlob& meshShader,
        const ShaderBlob& pixelShader,
        PipelineLayoutReflectionOptions layoutOptions = {},
        const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
    explicit MeshShader(
		FrameworkDeviceContext& deviceContext,
        const ShaderBlob& amplificationShader,
        const ShaderBlob& meshShader,
        const ShaderBlob& pixelShader,
        PipelineLayoutReflectionOptions layoutOptions = {},
        const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});

    MeshShader(const MeshShader& other) = delete;
    MeshShader& operator=(const MeshShader& other) = delete;

    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }

private:
    friend class CommandContext;
    FrameworkDeviceContext& GetDeviceContext() const { return m_DeviceContext; }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);
    const RootSignature& GetRootSignature() const { return *m_RootSignature; }
    const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
    bool UsesReflectedRootSignature() const { return true; }

    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderReflectionMetadata* outMetadata);
    void BuildPipelineLayout();
    void BuildReflectedRootSignature();

    FrameworkDeviceContext& m_DeviceContext;
    std::shared_ptr<RootSignature> m_RootSignature;
    ShaderReflectionMetadata m_AmplificationShaderMetadata;
    ShaderReflectionMetadata m_MeshShaderMetadata;
    ShaderReflectionMetadata m_PixelShaderMetadata;
    PipelineLayoutReflectionOptions m_PipelineLayoutOptions;
    std::unique_ptr<PipelineLayout> m_PipelineLayout;
    std::unique_ptr<PipelineBindingSet> m_BindingSet;
    std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
    PipelineDescriptorPool m_DescriptorPool;
    RasterPipelineStateBuilder m_PipelineStateBuilder;
    PipelineStateCache<RasterPipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
};

//Modify End
