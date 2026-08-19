#pragma once
//Modify Begin:2026-07-30 by Hui

#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>

#include <DX12Library/RootSignature.h>

#include <d3d12.h>
#include <wrl.h>

#include <memory>
#include <string>

class ShaderBlob;
class FrameworkDeviceContext;

class RayTracingPipelineState
{
public:
    RootSignature& GetGlobalRootSignature() const;
    const std::shared_ptr<RootSignature>& GetGlobalRootSignaturePtr() const;
    const Microsoft::WRL::ComPtr<ID3D12StateObject>& GetStateObject() const;
    const void* GetShaderIdentifier(const std::wstring& shaderName) const;

private:
    friend class RayTracingPipelineStateBuilder;

    std::shared_ptr<RootSignature> m_GlobalRootSignature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> m_StateObject;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
};

class RayTracingPipelineStateBuilder
{
public:
    RayTracingPipelineStateBuilder(
        FrameworkDeviceContext& deviceContext,
        const ShaderBlob& shaderLibrary,
        RayTracingPipelineDesc desc);

    std::shared_ptr<RayTracingPipelineState> Build() const;
    RayTracingPipelineStateKey CreateKey() const;

private:
    std::shared_ptr<RootSignature> BuildGlobalRootSignature() const;
    Microsoft::WRL::ComPtr<ID3D12StateObject> BuildStateObject(const std::shared_ptr<RootSignature>& globalRootSignature) const;

    const ShaderBlob& m_ShaderLibrary;
    FrameworkDeviceContext& m_DeviceContext;
    RayTracingPipelineDesc m_Desc;
};
//Modify End
