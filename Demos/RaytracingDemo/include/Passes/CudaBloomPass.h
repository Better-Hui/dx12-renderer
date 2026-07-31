#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <array>

#include <d3d12.h>

#include <Framework/Interop/CudaInterop.h>

class Texture;

class CudaBloomPass final
{
public:
    CudaBloomPass();
    ~CudaBloomPass();

    bool DrawImGui();
//Modify Begin:2026-07-28 by BestHui
    bool ExecuteInPlace(Texture& postProcessColor, uint32_t width, uint32_t height, ID3D12CommandQueue* d3d12CommandQueue);
    void ReleaseInteropResource();
//Modify End
    void Shutdown();

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    const std::string& GetStatus() const { return m_Status; }

private:
    static constexpr uint32_t MaxBloomPyramidLevels = 8;
//Modify Begin:2026-07-30 by BestHui
    static constexpr uint32_t CudaTimingFrameCount = 3;

    struct CudaTimingFrame
    {
        CUevent D3DWaitBegin = nullptr;
        CUevent D3DWaitEnd = nullptr;
        CUevent KernelsBegin = nullptr;
        CUevent KernelsEnd = nullptr;
        CUevent SignalBegin = nullptr;
        CUevent SignalEnd = nullptr;
        uint64_t FrameIndex = 0;
        bool Initialized = false;
        bool Pending = false;
    };

    struct CudaTimingStats
    {
        float D3DToCudaWaitMs = 0.0f;
        float KernelsMs = 0.0f;
        float CudaSignalMs = 0.0f;
        float TotalCudaStreamMs = 0.0f;
        uint64_t FrameIndex = 0;
        bool Valid = false;
    };
//Modify End

    bool InitializeCuda();
//Modify Begin:2026-07-28 by BestHui
    bool EnsureD3D12InteropResource(Texture& postProcessColor, uint32_t width, uint32_t height);
//Modify End
    bool EnsureD3D12CudaSemaphore();
    bool SignalD3D12AndWaitInCuda(ID3D12CommandQueue* d3d12CommandQueue);
    bool SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue);
//Modify Begin:2026-07-30 by BestHui
    bool EnsureCudaPyramidTextures(uint32_t width, uint32_t height, uint32_t levelCount);
    bool EnsureCudaTimingFrames();
    void BeginCudaTimingFrame();
    void CollectCompletedCudaTimingFrames();
    void ReleaseCudaTimingFrames();
    void RecordCudaTimingEvent(CUevent cudaEvent);
//Modify End
    bool RunCudaBloom(uint32_t width, uint32_t height);

    bool m_Enabled = false;
    bool m_AvailabilityChecked = false;
    bool m_CudaAvailable = false;
    float m_Threshold = 0.55f;
    float m_SoftThreshold = 0.30f;
    float m_Intensity = 0.75f;
    int m_PyramidLevels = 5;
    std::string m_Status = "CUDA bloom is not initialized.";

    CudaContext m_CudaContext;
    CudaDx12InteropTexture m_InputTexture;
    CudaDx12TimelineSemaphore m_TimelineSemaphore;
//Modify Begin:2026-07-30 by BestHui
    CudaDeviceTexture2DPool m_PyramidTextures;
//Modify End
    CUmodule m_Module = nullptr;
    CUfunction m_PrefilterDownsampleCascadeKernel = nullptr;
    CUfunction m_DownsampleCascadeKernel = nullptr;
    CUfunction m_UpsampleAddKernel = nullptr;
    CUfunction m_CompositeBloomKernel = nullptr;
    std::array<uint32_t, MaxBloomPyramidLevels> m_PyramidWidth = {};
    std::array<uint32_t, MaxBloomPyramidLevels> m_PyramidHeight = {};
//Modify Begin:2026-07-30 by BestHui
    std::array<CudaTimingFrame, CudaTimingFrameCount> m_CudaTimingFrames = {};
    CudaTimingStats m_LastCudaTiming = {};
    uint32_t m_CudaTimingFrameCursor = 0;
    uint64_t m_CudaTimingFrameCounter = 0;
    int32_t m_ActiveCudaTimingFrameIndex = -1;
//Modify End
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    ID3D12Resource* m_SourceInteropResource = nullptr;
};
