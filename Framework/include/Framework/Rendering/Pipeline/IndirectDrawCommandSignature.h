//Modify Begin:2026-07-30 by Hui
#pragma once

#include <d3d12.h>
#include <wrl.h>

class RootSignature;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End

class IndirectDrawCommandSignature final
{
public:
    explicit IndirectDrawCommandSignature(FrameworkDeviceContext& deviceContext);
//Modify Begin:2026-07-31 by Hui
    IndirectDrawCommandSignature(
        FrameworkDeviceContext& deviceContext,
        const RootSignature& rootSignature,
        UINT rootParameterIndex,
        UINT rootConstantCount,
        UINT byteStride);
//Modify End

    const Microsoft::WRL::ComPtr<ID3D12CommandSignature>& GetD3D12CommandSignature() const { return m_CommandSignature; }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_CommandSignature;
};
//Modify End
