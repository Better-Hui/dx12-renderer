#pragma once

#include <DX12Library/RootSignature.h>
#include <DX12Library/RenderTargetState.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>

#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>

#include <memory>
#include <vector>

class RasterPipelineStateBuilder final
{
public:
    RasterPipelineStateBuilder();
    explicit RasterPipelineStateBuilder(std::shared_ptr<RootSignature> rootSignature);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> Build(Microsoft::WRL::ComPtr<ID3D12Device2> device) const;
//Modify Begin:2026-07-27 by BestHui
    RasterPipelineStateKey CreateKey(const RenderTargetState& renderTargetState) const;
//Modify End

    RasterPipelineStateBuilder& WithRenderTargetFormats(const std::vector<DXGI_FORMAT>& renderTargetFormats, DXGI_FORMAT depthStencilFormat);
    RasterPipelineStateBuilder& WithRootSignature(std::shared_ptr<RootSignature> rootSignature);
    RasterPipelineStateBuilder& WithSampleDesc(const DXGI_SAMPLE_DESC& sampleDesc);
    RasterPipelineStateBuilder& WithShaders(const Microsoft::WRL::ComPtr<ID3DBlob>& vertexShader, const Microsoft::WRL::ComPtr<ID3DBlob>& pixelShader);

    RasterPipelineStateBuilder& WithBlend(const CD3DX12_BLEND_DESC& blendDesc);
    RasterPipelineStateBuilder& WithAlphaBlend();
    RasterPipelineStateBuilder& WithAdditiveBlend();

    RasterPipelineStateBuilder& WithDepthStencil(const CD3DX12_DEPTH_STENCIL_DESC& depthStencil);
    RasterPipelineStateBuilder& WithDisabledDepthStencil();
    RasterPipelineStateBuilder& WithDisabledDepthWrite();
    //Modify Begin:2026-07-23 by BestHui
    RasterPipelineStateBuilder& WithDepthTestNoWrite();
    //Modify End

    RasterPipelineStateBuilder& WithInputLayout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout);
    RasterPipelineStateBuilder& WithRasterizer(const CD3DX12_RASTERIZER_DESC& rasterizer);
    //Modify Begin:2026-07-23 by BestHui
    RasterPipelineStateBuilder& WithFrontFaceCull();
    RasterPipelineStateBuilder& WithNoCull();
    RasterPipelineStateBuilder& WithWireframeNoCull();
    //Modify End

private:
//Modify Begin:2026-07-27 by BestHui
    size_t BuildFixedFunctionStateHash() const;
//Modify End

    std::shared_ptr<RootSignature> m_RootSignature;

    std::vector<DXGI_FORMAT> m_RenderTargetFormats;
    DXGI_SAMPLE_DESC m_SampleDesc;
    DXGI_FORMAT m_DepthStencilFormat;

    Microsoft::WRL::ComPtr<ID3DBlob> m_VertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> m_PixelShader;
    CD3DX12_BLEND_DESC m_BlendDesc = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    CD3DX12_DEPTH_STENCIL_DESC m_DepthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    CD3DX12_RASTERIZER_DESC m_RasterizerDesc = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayout;
};
