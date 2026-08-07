//Modify Begin:2026-08-07 by BestHui
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include <memory>
#include <string>

class StreamlineRuntime final
{
public:
    StreamlineRuntime() = default;
    ~StreamlineRuntime();

    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    bool Initialize(ID3D12Device2* nativeDevice, const std::wstring& logDirectory);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_Initialized; }
    [[nodiscard]] bool IsRayReconstructionSupported() const { return m_RayReconstructionSupported; }
    [[nodiscard]] bool IsFrameGenerationSupported() const { return m_FrameGenerationSupported; }
    [[nodiscard]] bool IsFrameGenerationEnabled() const { return m_FrameGenerationEnabled; }
    [[nodiscard]] const std::string& GetStatusMessage() const { return m_StatusMessage; }

    bool SetFrameGenerationEnabled(bool enabled);

    HRESULT CreateCommandQueue(
        ID3D12Device2* nativeDevice,
        const D3D12_COMMAND_QUEUE_DESC* desc,
        REFIID riid,
        void** commandQueue) const;
    HRESULT CreateSwapChainForHwnd(
        IDXGIFactory4* nativeFactory,
        IUnknown* commandQueue,
        HWND window,
        const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
        IDXGIOutput* restrictToOutput,
        IDXGISwapChain1** swapChain) const;

private:
    bool UpgradeDevice(ID3D12Device2* nativeDevice);

    ID3D12Device2* m_ProxyDevice = nullptr;
    bool m_Initialized = false;
    bool m_RayReconstructionSupported = false;
    bool m_FrameGenerationSupported = false;
    bool m_FrameGenerationEnabled = false;
    std::string m_StatusMessage;
};
//Modify End
