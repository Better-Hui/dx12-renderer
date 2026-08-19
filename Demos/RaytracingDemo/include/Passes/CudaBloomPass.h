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
class CommandList;
class Bloom;

class CudaBloomPass final
{
public:
//Modify Begin:2026-08-17 by Hui
    enum class Backend : uint32_t
    {
        Cuda = 0,
        FrameworkRaster = 1,
    };

    enum class CudaMethod : uint32_t
    {
        ClassicPyramid = 0,
        BoxFilterApproximation = 1,
        BoxFilterOriginalPaper = 2,
    };

    enum class ThreadBlockSize : uint32_t
    {
        Size8x8 = 8,
        Size16x16 = 16,
    };

    struct Settings
    {
        bool Enabled = false;
        Backend SelectedBackend = Backend::Cuda;
        CudaMethod Method = CudaMethod::ClassicPyramid;
        float Threshold = 0.55f;
        float SoftThreshold = 0.30f;
        float Intensity = 0.75f;
        int PyramidLevels = 16;
        float BoxFilterSigma = 1.0f;
        bool UseSharedMemoryDownsampling = false;
        ThreadBlockSize BlockSize = ThreadBlockSize::Size16x16;
    };

    struct TimingStats
    {
        float D3DToCudaWaitMs = 0.0f;
        float KernelsMs = 0.0f;
        float CudaSignalMs = 0.0f;
        float TotalCudaStreamMs = 0.0f;
        uint64_t FrameIndex = 0;
        bool Valid = false;
    };

//Modify End
//Modify Begin:2026-07-30 by Hui
    explicit CudaBloomPass(FrameworkDeviceContext& deviceContext);
//Modify End
    ~CudaBloomPass();

//Modify Begin:2026-07-28 by Hui
    bool ExecuteInPlace(Texture& postProcessColor, uint32_t width, uint32_t height, ID3D12CommandQueue* d3d12CommandQueue);
//Modify Begin:2026-08-16 by Hui
    bool ExecuteFrameworkBloom(
        const std::shared_ptr<Texture>& source,
        const std::shared_ptr<Texture>& destination,
        CommandList& commandList,
        uint32_t width,
        uint32_t height);
//Modify End
    void ReleaseInteropResource();
//Modify End
    void Shutdown();

//Modify Begin:2026-07-30 by Hui
    static uint32_t ComputeMaxPyramidLevels(uint32_t width, uint32_t height);
//Modify End

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    Backend GetBackend() const { return m_Backend; }
    bool IsFrameworkRaster() const { return m_Backend == Backend::FrameworkRaster; }
    const std::string& GetStatus() const { return m_Status; }
    Settings GetSettings() const;
    void SetSettings(const Settings& settings);
    std::vector<TimingStats> ConsumeCompletedTimingStats();
//Modify Begin:2026-08-17 by Hui
    void SetBackend(Backend backend) { m_Backend = backend; }
    void SetCudaMethod(CudaMethod method) { m_CudaMethod = method; }
    void SetThreshold(float threshold) { m_Threshold = threshold; }
    void SetSoftThreshold(float softThreshold) { m_SoftThreshold = softThreshold; }
    void SetIntensity(float intensity) { m_Intensity = intensity; }
    void SetPyramidLevels(int pyramidLevels) { m_PyramidLevels = pyramidLevels; }
    void SetBoxFilterSigma(float sigma) { m_BoxFilterSigma = sigma; }
    void SetUseSharedMemoryDownsampling(bool enabled) { m_UseSharedMemoryDownsampling = enabled; }
    void SetThreadBlockSize(ThreadBlockSize blockSize) { m_ThreadBlockSize = blockSize; }
//Modify End

private:
//Modify Begin:2026-07-30 by Hui
    FrameworkDeviceContext& m_DeviceContext;
//Modify End
//Modify Begin:2026-07-30 by Hui
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

//Modify End

    bool InitializeCuda();
//Modify Begin:2026-07-28 by Hui
    bool EnsureD3D12InteropResource(Texture& postProcessColor, uint32_t width, uint32_t height);
//Modify End
    bool EnsureD3D12CudaSemaphore();
    bool SignalD3D12AndWaitInCuda(ID3D12CommandQueue* d3d12CommandQueue);
    bool SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue);
//Modify Begin:2026-07-30 by Hui
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
//Modify Begin:2026-08-17 by Hui
    int m_PyramidLevels = 16;
//Modify End
//Modify Begin:2026-08-17 by Hui
    Backend m_Backend = Backend::Cuda;
    CudaMethod m_CudaMethod = CudaMethod::ClassicPyramid;
    float m_BoxFilterSigma = 1.0f;
    bool m_UseSharedMemoryDownsampling = false;
    ThreadBlockSize m_ThreadBlockSize = ThreadBlockSize::Size16x16;
    std::unique_ptr<Bloom> m_FrameworkBloom;
    uint32_t m_FrameworkBloomWidth = 0;
    uint32_t m_FrameworkBloomHeight = 0;
    int m_FrameworkBloomPyramidLevels = 0;
//Modify End
    std::string m_Status = "CUDA bloom is not initialized.";

    CudaContext m_CudaContext;
    CudaDx12InteropTexture m_InputTexture;
    CudaDx12TimelineSemaphore m_TimelineSemaphore;
//Modify Begin:2026-07-30 by Hui
    CudaDeviceTexture2DPool m_PyramidTextures;
//Modify End
    CUmodule m_Module = nullptr;
//Modify Begin:2026-08-17 by Hui
    CUfunction m_BloomPrefilterDownsampleKernel = nullptr;
    CUfunction m_BloomOneLevelSharedDownsampleKernel = nullptr;
    CUfunction m_BloomTwoLevelSharedDownsampleKernel = nullptr;
    CUfunction m_BloomThreeLevelSharedDownsampleKernel = nullptr;
    CUfunction m_BloomFourLevelSharedDownsampleKernel = nullptr;
    CUfunction m_BloomFourTapDownsampleKernel = nullptr;
//Modify End
//Modify Begin:2026-08-17 by Hui
    CUfunction m_BloomBoxFilterAdditiveUpsampleKernel = nullptr;
    CUfunction m_BloomBoxFilterInterpolatedUpsampleKernel = nullptr;
    CUfunction m_BloomAdditiveUpsampleKernel = nullptr;
    CUfunction m_BloomCompositeKernel = nullptr;
//Modify End
//Modify Begin:2026-07-30 by Hui
    std::vector<uint32_t> m_PyramidWidth;
    std::vector<uint32_t> m_PyramidHeight;
//Modify End
//Modify Begin:2026-07-30 by Hui
    std::array<CudaTimingFrame, CudaTimingFrameCount> m_CudaTimingFrames = {};
    std::vector<TimingStats> m_CompletedCudaTimingSamples;
    uint32_t m_CudaTimingFrameCursor = 0;
    uint64_t m_CudaTimingFrameCounter = 0;
    int32_t m_ActiveCudaTimingFrameIndex = -1;
//Modify End
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    ID3D12Resource* m_SourceInteropResource = nullptr;
};
