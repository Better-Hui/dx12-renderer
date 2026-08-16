#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <array>
#include <vector>

#include <d3d12.h>

#include <Framework/Interop/CudaInterop.h>

class Texture;
class FrameworkDeviceContext;

class CudaBloomPass final
{
public:
//Modify Begin:2026-08-16 by BestHui
    enum class Method : uint32_t
    {
        Classic = 0,
        BoxFilterApproximation = 1,
        BoxFilterOriginalPaper = 2,
    };
    enum class DownsampleMode : uint32_t
    {
        Cascaded2x2 = 0,
        NonCascaded5Tap = 1,
        NonCascaded5TapShared = 2,
        NonCascaded10Tap = 3,
        NonCascaded15Tap = 4,
    };
//Modify End
//Modify Begin:2026-07-30 by BestHui
    explicit CudaBloomPass(FrameworkDeviceContext& deviceContext);
//Modify End
    ~CudaBloomPass();

//Modify Begin:2026-07-30 by BestHui
    bool DrawImGui(uint32_t width, uint32_t height);
//Modify End
//Modify Begin:2026-07-28 by BestHui
    bool ExecuteInPlace(Texture& postProcessColor, uint32_t width, uint32_t height, ID3D12CommandQueue* d3d12CommandQueue);
    void ReleaseInteropResource();
//Modify End
    void Shutdown();

//Modify Begin:2026-07-30 by BestHui
    static uint32_t ComputeMaxPyramidLevels(uint32_t width, uint32_t height);
//Modify End

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    const std::string& GetStatus() const { return m_Status; }
//Modify Begin:2026-08-16 by BestHui
    void SetMethod(Method method) { m_Method = method; }
    void SetThreshold(float threshold) { m_Threshold = threshold; }
    void SetSoftThreshold(float softThreshold) { m_SoftThreshold = softThreshold; }
    void SetIntensity(float intensity) { m_Intensity = intensity; }
    void SetPyramidLevels(int pyramidLevels) { m_PyramidLevels = pyramidLevels; }
    void SetBoxFilterSigma(float sigma) { m_BoxFilterSigma = sigma; }
    void SetDownsampleMode(DownsampleMode mode) { m_DownsampleMode = mode; }
//Modify End

private:
//Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext& m_DeviceContext;
//Modify End
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
//Modify Begin:2026-08-16 by BestHui
    Method m_Method = Method::Classic;
    float m_BoxFilterSigma = 1.0f;
    DownsampleMode m_DownsampleMode = DownsampleMode::Cascaded2x2;
//Modify End
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
    CUfunction m_PrefilterDownsample5TapKernel = nullptr;
    CUfunction m_PrefilterDownsample5TapSharedKernel = nullptr;
    CUfunction m_PrefilterDownsample10TapKernel = nullptr;
    CUfunction m_PrefilterDownsample15TapKernel = nullptr;
    CUfunction m_Downsample5TapKernel = nullptr;
    CUfunction m_Downsample5TapSharedKernel = nullptr;
    CUfunction m_Downsample10TapKernel = nullptr;
    CUfunction m_Downsample15TapKernel = nullptr;
//Modify Begin:2026-07-30 by BestHui
    CUfunction m_UpsampleClassicKernel = nullptr;
    CUfunction m_UpsampleBoxFilterKernel = nullptr;
    CUfunction m_UpsampleBoxFilterOriginalKernel = nullptr;
//Modify End
    CUfunction m_CompositeBloomKernel = nullptr;
//Modify Begin:2026-07-30 by BestHui
    std::vector<uint32_t> m_PyramidWidth;
    std::vector<uint32_t> m_PyramidHeight;
//Modify End
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
