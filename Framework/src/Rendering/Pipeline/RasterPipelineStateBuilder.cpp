#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <DX12Library/Helpers.h>
#include <Framework/Geometry/Mesh.h>

#include <utility>

static constexpr UINT MAX_RENDER_TARGETS = _countof(D3D12_RT_FORMAT_ARRAY::RTFormats);

RasterPipelineStateBuilder::RasterPipelineStateBuilder()
    : m_InputLayout(VertexAttributes::INPUT_ELEMENT_COUNT)
{
    memcpy(m_InputLayout.data(), VertexAttributes::INPUT_ELEMENTS, sizeof(VertexAttributes::INPUT_ELEMENTS));
}

RasterPipelineStateBuilder::RasterPipelineStateBuilder(const std::shared_ptr<RootSignature> rootSignature)
    : m_RootSignature(rootSignature)
    , m_InputLayout(VertexAttributes::INPUT_ELEMENT_COUNT)
{
    memcpy(m_InputLayout.data(), VertexAttributes::INPUT_ELEMENTS, sizeof(VertexAttributes::INPUT_ELEMENTS));
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> RasterPipelineStateBuilder::Build(Microsoft::WRL::ComPtr<ID3D12Device2> device) const
{
    Assert(m_VertexShader != nullptr, "Vertex Shader cannot be null.");
    Assert(m_PixelShader != nullptr, "Pixel Shader cannot be null.");
    Assert(m_RootSignature != nullptr, "Root signature cannot be null.");

    // Setup the pipeline state.
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS Vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS Ps;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC Blend;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
    } pipelineStateStream;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = static_cast<UINT>(m_RenderTargetFormats.size());
    memcpy(rtvFormats.RTFormats, m_RenderTargetFormats.data(), m_RenderTargetFormats.size() * sizeof(DXGI_FORMAT));

    pipelineStateStream.RootSignature = m_RootSignature->GetRootSignature().Get();

    pipelineStateStream.InputLayout = { m_InputLayout.data(), static_cast<UINT>(m_InputLayout.size()) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.Vs = { m_VertexShader->GetBufferPointer(), m_VertexShader->GetBufferSize() };
    pipelineStateStream.Ps = { m_PixelShader->GetBufferPointer(), m_PixelShader->GetBufferSize() };
    pipelineStateStream.DsvFormat = m_DepthStencilFormat;
    pipelineStateStream.RtvFormats = rtvFormats;
    pipelineStateStream.Blend = m_BlendDesc;
    pipelineStateStream.Rasterizer = m_RasterizerDesc;

    // if depth-stencil format is unknown, disable the depth-stencil unit
    pipelineStateStream.DepthStencil = m_DepthStencilFormat != DXGI_FORMAT_UNKNOWN ?
                                           m_DepthStencilDesc :
                                           CD3DX12_DEPTH_STENCIL_DESC();

    pipelineStateStream.SampleDesc = m_SampleDesc;

    const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
        sizeof(PipelineStateStream), &pipelineStateStream
    };

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)));
    return pipelineState;
}

//Modify Begin:2026-07-27 by BestHui
RasterPipelineStateKey RasterPipelineStateBuilder::CreateKey(const RenderTargetState& renderTargetState) const
{
    RasterPipelineStateKey key;
    key.VertexShader = MakePipelineShaderBytecodeKey(m_VertexShader);
    key.PixelShader = MakePipelineShaderBytecodeKey(m_PixelShader);
    key.LayoutHash = MakePipelineRootSignatureHash(m_RootSignature.get());
    key.RenderTarget = renderTargetState;
    key.FixedFunctionStateHash = BuildFixedFunctionStateHash();
    return key;
}

size_t RasterPipelineStateBuilder::BuildFixedFunctionStateHash() const
{
    size_t seed = 0;
    PipelineHashBytes(seed, &m_BlendDesc, sizeof(m_BlendDesc));
    PipelineHashBytes(seed, &m_DepthStencilDesc, sizeof(m_DepthStencilDesc));
    PipelineHashBytes(seed, &m_RasterizerDesc, sizeof(m_RasterizerDesc));
    PipelineHashBytes(seed, &m_SampleDesc, sizeof(m_SampleDesc));
    PipelineHashValue(seed, m_DepthStencilFormat);
    PipelineHashValue(seed, m_RenderTargetFormats.size());
    for (const DXGI_FORMAT format : m_RenderTargetFormats)
    {
        PipelineHashValue(seed, format);
    }
    PipelineHashCombine(seed, MakePipelineInputLayoutHash(m_InputLayout));
    return seed;
}
//Modify End

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithRenderTargetFormats(const std::vector<DXGI_FORMAT>& renderTargetFormats, DXGI_FORMAT depthStencilFormat)
{
    Assert(renderTargetFormats.size() < MAX_RENDER_TARGETS, "Too many render target formats.");

    m_RenderTargetFormats = renderTargetFormats;
    m_DepthStencilFormat = depthStencilFormat;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithRootSignature(std::shared_ptr<RootSignature> rootSignature)
{
    Assert(rootSignature != nullptr, "Root signature cannot be null.");

    m_RootSignature = std::move(rootSignature);
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithSampleDesc(const DXGI_SAMPLE_DESC& sampleDesc)
{
    m_SampleDesc = sampleDesc;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithShaders(const Microsoft::WRL::ComPtr<ID3DBlob>& vertexShader, const Microsoft::WRL::ComPtr<ID3DBlob>& pixelShader)
{
    Assert(vertexShader != nullptr, "Vertex Shader cannot be null.");
    Assert(pixelShader != nullptr, "Pixel Shader cannot be null.");

    m_VertexShader = vertexShader;
    m_PixelShader = pixelShader;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithBlend(const CD3DX12_BLEND_DESC& blendDesc)
{
    m_BlendDesc = blendDesc;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithAlphaBlend()
{
    auto blendDesc = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    auto& rtBlendDesc = blendDesc.RenderTarget[0];
    rtBlendDesc.BlendEnable = true;
    rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    //Modify Begin:2026-07-21 by BestHui
    rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    //Modify End
    rtBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    return WithBlend(blendDesc);
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithAdditiveBlend()
{
    auto blendDesc = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    auto& rtBlendDesc = blendDesc.RenderTarget[0];
    rtBlendDesc.BlendEnable = true;
    rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlendDesc.SrcBlend = D3D12_BLEND_ONE;
    rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlend = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ONE;
    return WithBlend(blendDesc);
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithDepthStencil(const CD3DX12_DEPTH_STENCIL_DESC& depthStencil)
{
    m_DepthStencilDesc = depthStencil;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithDisabledDepthStencil()
{
    return WithDepthStencil({});
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithDisabledDepthWrite()
{
    auto desc = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT{});
    desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    return WithDepthStencil(desc);
}

//Modify Begin:2026-07-23 by BestHui
RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithDepthTestNoWrite()
{
    return WithDisabledDepthWrite();
}
//Modify End

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithInputLayout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout)
{
    m_InputLayout = inputLayout;
    return *this;
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithRasterizer(const CD3DX12_RASTERIZER_DESC& rasterizer)
{
    m_RasterizerDesc = rasterizer;
    return *this;
}

//Modify Begin:2026-07-23 by BestHui
RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithFrontFaceCull()
{
    auto rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT{});
    rasterizer.CullMode = D3D12_CULL_MODE_FRONT;
    return WithRasterizer(rasterizer);
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithNoCull()
{
    auto rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT{});
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    return WithRasterizer(rasterizer);
}

RasterPipelineStateBuilder& RasterPipelineStateBuilder::WithWireframeNoCull()
{
    auto rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT{});
    rasterizer.FillMode = D3D12_FILL_MODE_WIREFRAME;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    return WithRasterizer(rasterizer);
}
//Modify End
