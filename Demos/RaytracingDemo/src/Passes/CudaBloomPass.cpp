//Modify Begin:2026-07-30 by Hui
#include <Passes/CudaBloomPass.h>
//Modify End

#include <DX12Library/Helpers.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/PostProcess/Bloom.h>

#include <algorithm>

namespace
{
    uint32_t DivideRoundUp(const uint32_t value, const uint32_t divisor)
    {
        return (value + divisor - 1u) / divisor;
    }

    uint32_t HalfSize(const uint32_t value)
    {
        return value > 1u ? value >> 1u : 1u;
    }

}

//Modify Begin:2026-07-30 by Hui
uint32_t CudaBloomPass::ComputeMaxPyramidLevels(const uint32_t width, const uint32_t height)
{
    uint32_t levelWidth = HalfSize(width);
    uint32_t levelHeight = HalfSize(height);
    uint32_t levelCount = 0u;
    do
    {
        ++levelCount;
        if (levelWidth == 1u && levelHeight == 1u)
        {
            break;
        }
        levelWidth = HalfSize(levelWidth);
        levelHeight = HalfSize(levelHeight);
    } while (true);
    return levelCount;
}
//Modify End

//Modify Begin:2026-07-30 by Hui
CudaBloomPass::CudaBloomPass(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
{
}
//Modify End

CudaBloomPass::~CudaBloomPass()
{
    Shutdown();
}

void CudaBloomPass::Shutdown()
{
//Modify Begin:2026-08-16 by Hui
    m_FrameworkBloom.reset();
    m_FrameworkBloomWidth = 0;
    m_FrameworkBloomHeight = 0;
    m_FrameworkBloomPyramidLevels = 0;
//Modify End
    ReleaseInteropResource();
//Modify Begin:2026-07-30 by Hui
    ReleaseCudaTimingFrames();
    m_PyramidTextures.Release(&m_CudaContext);
    m_PyramidWidth.clear();
    m_PyramidHeight.clear();
//Modify End
    m_CudaContext.UnloadModule(m_Module);
    m_CudaContext.Shutdown();
    m_AvailabilityChecked = false;
    m_CudaAvailable = false;
    m_Status = "CUDA bloom is shut down.";
}

void CudaBloomPass::ReleaseInteropResource()
{
    m_InputTexture.Release(&m_CudaContext);
    m_TimelineSemaphore.Shutdown(&m_CudaContext);
    m_SourceInteropResource = nullptr;
    m_Width = 0;
    m_Height = 0;
}

//Modify Begin:2026-08-18 by Hui
CudaBloomPass::Settings CudaBloomPass::GetSettings() const
{
    return {
        .Enabled = m_Enabled,
        .SelectedBackend = m_Backend,
        .Method = m_CudaMethod,
        .Threshold = m_Threshold,
        .SoftThreshold = m_SoftThreshold,
        .Intensity = m_Intensity,
        .PyramidLevels = m_PyramidLevels,
        .BoxFilterSigma = m_BoxFilterSigma,
        .UseSharedMemoryDownsampling = m_UseSharedMemoryDownsampling,
        .BlockSize = m_ThreadBlockSize,
    };
}

void CudaBloomPass::SetSettings(const Settings& settings)
{
    m_Enabled = settings.Enabled;
    m_Backend = settings.SelectedBackend;
    m_CudaMethod = settings.Method;
    m_Threshold = settings.Threshold;
    m_SoftThreshold = settings.SoftThreshold;
    m_Intensity = settings.Intensity;
    m_PyramidLevels = settings.PyramidLevels;
    m_BoxFilterSigma = settings.BoxFilterSigma;
    m_UseSharedMemoryDownsampling = settings.UseSharedMemoryDownsampling;
    m_ThreadBlockSize = settings.BlockSize;
}

std::vector<CudaBloomPass::TimingStats> CudaBloomPass::ConsumeCompletedTimingStats()
{
    std::vector<TimingStats> completedSamples;
    completedSamples.swap(m_CompletedCudaTimingSamples);
    std::ranges::sort(completedSamples, {}, &TimingStats::FrameIndex);
    return completedSamples;
}
//Modify End

//Modify Begin:2026-08-16 by Hui
bool CudaBloomPass::ExecuteFrameworkBloom(
    const std::shared_ptr<Texture>& source,
    const std::shared_ptr<Texture>& destination,
    CommandList& commandList,
    const uint32_t width,
    const uint32_t height)
{
    if (!m_Enabled || !IsFrameworkRaster() || source == nullptr || destination == nullptr)
    {
        return false;
    }

    Assert(source.get() != destination.get(), "Framework raster bloom requires distinct source and destination textures.");

    const uint32_t maxPyramidLevels = ComputeMaxPyramidLevels(width, height);
    const int pyramidLevels = std::clamp(m_PyramidLevels, 1, static_cast<int>(maxPyramidLevels));
    if (m_FrameworkBloom == nullptr ||
        m_FrameworkBloomWidth != width ||
        m_FrameworkBloomHeight != height ||
        m_FrameworkBloomPyramidLevels != pyramidLevels)
    {
        m_FrameworkBloom = std::make_unique<Bloom>(
            m_DeviceContext,
            commandList,
            width,
            height,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            static_cast<size_t>(pyramidLevels) + 1u);
        m_FrameworkBloomWidth = width;
        m_FrameworkBloomHeight = height;
        m_FrameworkBloomPyramidLevels = pyramidLevels;
    }

    RenderTarget destinationRenderTarget;
    destinationRenderTarget.AttachTexture(Color0, destination);

    BloomParameters parameters = {};
    parameters.Intensity = m_Intensity;
    parameters.Threshold = m_Threshold;
    parameters.SoftThreshold = m_SoftThreshold;
    m_FrameworkBloom->Draw(commandList, source, destinationRenderTarget, parameters);
    m_Status = "Built-in raster bloom is active.";
    return true;
}
//Modify End

bool CudaBloomPass::InitializeCuda()
{
    if (m_AvailabilityChecked)
    {
        return m_CudaAvailable;
    }

    m_AvailabilityChecked = true;
    std::string error;
    const auto& device = m_DeviceContext.GetDevice();
    if (!m_CudaContext.InitializeForD3D12Device(device.Get(), error))
    {
        m_Status = error;
        return false;
    }

    if (!m_CudaContext.LoadModuleFromFile(L"Shaders/CudaBloom.ptx", m_Module, error))
    {
        m_Status = error;
        m_CudaContext.Shutdown();
        return false;
    }

//Modify Begin:2026-08-18 by Hui
    if (!m_CudaContext.GetFunction(m_Module, "BloomPrefilterDownsampleKernel", m_BloomPrefilterDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomOneLevelSharedDownsampleKernel", m_BloomOneLevelSharedDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomTwoLevelSharedDownsampleKernel", m_BloomTwoLevelSharedDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomThreeLevelSharedDownsampleKernel", m_BloomThreeLevelSharedDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomFourLevelSharedDownsampleKernel", m_BloomFourLevelSharedDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomFourTapDownsampleKernel", m_BloomFourTapDownsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomBoxFilterAdditiveUpsampleKernel", m_BloomBoxFilterAdditiveUpsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomBoxFilterInterpolatedUpsampleKernel", m_BloomBoxFilterInterpolatedUpsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomAdditiveUpsampleKernel", m_BloomAdditiveUpsampleKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "BloomCompositeKernel", m_BloomCompositeKernel, error))
    {
        m_Status = error;
        Shutdown();
        return false;
    }

    m_CudaAvailable = true;
    m_Status = "CUDA bloom is available.";
    return true;
}

bool CudaBloomPass::EnsureD3D12InteropResource(Texture& postProcessColor, const uint32_t width, const uint32_t height)
{
    const D3D12_RESOURCE_DESC sourceDesc = postProcessColor.GetD3D12ResourceDesc();
    if (sourceDesc.Format != DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        m_Status = "CUDA bloom expects an R32G32B32A32_FLOAT shared scene color texture.";
        return false;
    }

    ID3D12Resource* sourceResource = postProcessColor.GetD3D12Resource().Get();
    if (m_InputTexture.IsImported() &&
        m_SourceInteropResource == sourceResource &&
        m_Width == width &&
        m_Height == height)
    {
        return true;
    }

    if (!InitializeCuda())
    {
        return false;
    }

    std::string error;
    const auto& device = m_DeviceContext.GetDevice();
    if (!m_InputTexture.Import(m_CudaContext, device.Get(), sourceResource, width, height, true, true, error))
    {
        m_Status = error;
        return false;
    }

    m_SourceInteropResource = sourceResource;
    m_Width = width;
    m_Height = height;
    return true;
}

bool CudaBloomPass::EnsureD3D12CudaSemaphore()
{
    if (!InitializeCuda())
    {
        return false;
    }

    std::string error;
    const auto& device = m_DeviceContext.GetDevice();
    if (!m_TimelineSemaphore.Initialize(m_CudaContext, device.Get(), error))
    {
        m_Status = error;
        return false;
    }
    return true;
}

bool CudaBloomPass::SignalD3D12AndWaitInCuda(ID3D12CommandQueue* d3d12CommandQueue)
{
    if (!EnsureD3D12CudaSemaphore())
    {
        return false;
    }

    std::string error;
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].D3DWaitBegin);
    }
    if (!m_TimelineSemaphore.SignalD3D12AndWaitCuda(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
    }
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].D3DWaitEnd);
    }
    return true;
}

bool CudaBloomPass::SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue)
{
    if (!EnsureD3D12CudaSemaphore())
    {
        return false;
    }

    std::string error;
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].SignalBegin);
    }
    if (!m_TimelineSemaphore.SignalCudaAndWaitInD3D12(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
    }
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        CudaTimingFrame& timingFrame = m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)];
        RecordCudaTimingEvent(timingFrame.SignalEnd);
        timingFrame.Pending = true;
        m_ActiveCudaTimingFrameIndex = -1;
    }
    return true;
}

bool CudaBloomPass::EnsureCudaPyramidTextures(const uint32_t width, const uint32_t height, const uint32_t levelCount)
{
    if (!InitializeCuda())
    {
        return false;
    }

    uint32_t levelWidth = HalfSize(width);
    uint32_t levelHeight = HalfSize(height);
    m_PyramidWidth.resize(levelCount);
    m_PyramidHeight.resize(levelCount);
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        std::string error;
        if (!m_PyramidTextures.EnsureTexture(level, levelWidth, levelHeight, error))
        {
            m_Status = error;
            return false;
        }

        m_PyramidWidth[level] = levelWidth;
        m_PyramidHeight[level] = levelHeight;
        levelWidth = HalfSize(levelWidth);
        levelHeight = HalfSize(levelHeight);
    }
    return true;
}

bool CudaBloomPass::EnsureCudaTimingFrames()
{
    if (!InitializeCuda())
    {
        return false;
    }

    for (CudaTimingFrame& timingFrame : m_CudaTimingFrames)
    {
        if (timingFrame.Initialized)
        {
            continue;
        }

        const auto createEvent = [this](CUevent& cudaEvent) -> bool
        {
            const CUresult result = cuEventCreate(&cudaEvent, CU_EVENT_DEFAULT);
            if (result != CUDA_SUCCESS)
            {
                m_Status = "CUDA timing event creation failed: " + CudaContext::GetError(result);
                return false;
            }
            return true;
        };

        if (!createEvent(timingFrame.D3DWaitBegin) ||
            !createEvent(timingFrame.D3DWaitEnd) ||
            !createEvent(timingFrame.KernelsBegin) ||
            !createEvent(timingFrame.KernelsEnd) ||
            !createEvent(timingFrame.SignalBegin) ||
            !createEvent(timingFrame.SignalEnd))
        {
            ReleaseCudaTimingFrames();
            return false;
        }

        timingFrame.Initialized = true;
    }

    return true;
}

void CudaBloomPass::BeginCudaTimingFrame()
{
    m_ActiveCudaTimingFrameIndex = -1;
    if (!EnsureCudaTimingFrames())
    {
        return;
    }

    CudaTimingFrame& timingFrame = m_CudaTimingFrames[m_CudaTimingFrameCursor];
    if (timingFrame.Pending && cuEventQuery(timingFrame.SignalEnd) == CUDA_ERROR_NOT_READY)
    {
        return;
    }

    timingFrame.Pending = false;
    timingFrame.FrameIndex = ++m_CudaTimingFrameCounter;
    m_ActiveCudaTimingFrameIndex = static_cast<int32_t>(m_CudaTimingFrameCursor);
    m_CudaTimingFrameCursor = (m_CudaTimingFrameCursor + 1u) % CudaTimingFrameCount;
}

void CudaBloomPass::CollectCompletedCudaTimingFrames()
{
    for (CudaTimingFrame& timingFrame : m_CudaTimingFrames)
    {
        if (!timingFrame.Pending)
        {
            continue;
        }

        const CUresult queryResult = cuEventQuery(timingFrame.SignalEnd);
        if (queryResult == CUDA_ERROR_NOT_READY)
        {
            continue;
        }
        if (queryResult != CUDA_SUCCESS)
        {
            m_Status = "CUDA timing query failed: " + CudaContext::GetError(queryResult);
            timingFrame.Pending = false;
            continue;
        }

        TimingStats timingStats = {};
        timingStats.FrameIndex = timingFrame.FrameIndex;
        timingStats.Valid = true;
        cuEventElapsedTime(&timingStats.D3DToCudaWaitMs, timingFrame.D3DWaitBegin, timingFrame.D3DWaitEnd);
        cuEventElapsedTime(&timingStats.KernelsMs, timingFrame.KernelsBegin, timingFrame.KernelsEnd);
        cuEventElapsedTime(&timingStats.CudaSignalMs, timingFrame.SignalBegin, timingFrame.SignalEnd);
        cuEventElapsedTime(&timingStats.TotalCudaStreamMs, timingFrame.D3DWaitBegin, timingFrame.SignalEnd);
        m_CompletedCudaTimingSamples.push_back(timingStats);
        timingFrame.Pending = false;
    }
}

void CudaBloomPass::ReleaseCudaTimingFrames()
{
    if (m_CudaContext.IsInitialized())
    {
        m_CudaContext.Synchronize();
    }

    for (CudaTimingFrame& timingFrame : m_CudaTimingFrames)
    {
        if (timingFrame.SignalEnd != nullptr)
        {
            cuEventDestroy(timingFrame.SignalEnd);
            timingFrame.SignalEnd = nullptr;
        }
        if (timingFrame.SignalBegin != nullptr)
        {
            cuEventDestroy(timingFrame.SignalBegin);
            timingFrame.SignalBegin = nullptr;
        }
        if (timingFrame.KernelsEnd != nullptr)
        {
            cuEventDestroy(timingFrame.KernelsEnd);
            timingFrame.KernelsEnd = nullptr;
        }
        if (timingFrame.KernelsBegin != nullptr)
        {
            cuEventDestroy(timingFrame.KernelsBegin);
            timingFrame.KernelsBegin = nullptr;
        }
        if (timingFrame.D3DWaitEnd != nullptr)
        {
            cuEventDestroy(timingFrame.D3DWaitEnd);
            timingFrame.D3DWaitEnd = nullptr;
        }
        if (timingFrame.D3DWaitBegin != nullptr)
        {
            cuEventDestroy(timingFrame.D3DWaitBegin);
            timingFrame.D3DWaitBegin = nullptr;
        }

        timingFrame.Initialized = false;
        timingFrame.Pending = false;
        timingFrame.FrameIndex = 0;
    }

    m_CompletedCudaTimingSamples.clear();
    m_CudaTimingFrameCursor = 0;
    m_CudaTimingFrameCounter = 0;
    m_ActiveCudaTimingFrameIndex = -1;
}

void CudaBloomPass::RecordCudaTimingEvent(CUevent cudaEvent)
{
    if (cudaEvent == nullptr)
    {
        return;
    }

    const CUresult result = cuEventRecord(cudaEvent, m_CudaContext.GetStream());
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA timing event record failed: " + CudaContext::GetError(result);
    }
}

bool CudaBloomPass::RunCudaBloom(const uint32_t width, const uint32_t height)
{
    const uint32_t maxPyramidLevels = ComputeMaxPyramidLevels(width, height);
    m_PyramidLevels = std::clamp(m_PyramidLevels, 1, static_cast<int>(maxPyramidLevels));
    const uint32_t levelCount = static_cast<uint32_t>(m_PyramidLevels);
    if (!EnsureCudaPyramidTextures(width, height, levelCount))
    {
        return false;
    }

    const uint32_t blockDimension = static_cast<uint32_t>(m_ThreadBlockSize);
    const uint32_t blockX = blockDimension;
    const uint32_t blockY = blockDimension;

    auto getPyramidSurface = [this, levelCount](const uint32_t level) -> CUsurfObject
    {
        return level < levelCount ? m_PyramidTextures.GetSurfaceObject(level) : 0;
    };
    auto getPyramidWidth = [this, levelCount](const uint32_t level) -> uint32_t
    {
        return level < levelCount ? m_PyramidWidth[level] : 0u;
    };
    auto getPyramidHeight = [this, levelCount](const uint32_t level) -> uint32_t
    {
        return level < levelCount ? m_PyramidHeight[level] : 0u;
    };

    CUtexObject input = m_InputTexture.GetTextureObject();
    CUsurfObject output = m_InputTexture.GetSurfaceObject();
    uint32_t cudaWidth = width;
    uint32_t cudaHeight = height;
    float threshold = m_Threshold;
    float softThreshold = m_SoftThreshold;
    float intensity = m_Intensity;
    const bool classicPyramid = m_CudaMethod == CudaMethod::ClassicPyramid;
    float boxFilterSigma = (std::max)(m_BoxFilterSigma, 0.001f);

    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].KernelsBegin);
    }

    CUresult result = CUDA_SUCCESS;
    uint32_t firstWidth = getPyramidWidth(0u);
    uint32_t firstHeight = getPyramidHeight(0u);
    CUsurfObject firstOutput = getPyramidSurface(0u);

    CUtexObject inputDownsample = m_InputTexture.GetLinearTextureObject();
    void* prefilterArgs[] = {
        &inputDownsample,
        &firstOutput,
        &cudaWidth,
        &cudaHeight,
        &firstWidth,
        &firstHeight,
        &threshold,
        &softThreshold,
        &intensity,
    };
    result = cuLaunchKernel(
        m_BloomPrefilterDownsampleKernel,
        DivideRoundUp(firstWidth, blockX),
        DivideRoundUp(firstHeight, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        m_CudaContext.GetStream(),
        prefilterArgs,
        nullptr);
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom prefilter launch failed: " + CudaContext::GetError(result);
        return false;
    }

    const auto launchFourTapDownsample = [&](const uint32_t sourceLevel, const uint32_t outputLevel) -> bool
    {
        CUsurfObject output = getPyramidSurface(outputLevel);
        uint32_t sourceWidth = getPyramidWidth(sourceLevel);
        uint32_t sourceHeight = getPyramidHeight(sourceLevel);
        uint32_t outputWidth = getPyramidWidth(outputLevel);
        uint32_t outputHeight = getPyramidHeight(outputLevel);
        CUtexObject source = m_PyramidTextures.GetLinearTextureObject(sourceLevel);
        void* downsampleArgs[] = {
            &source,
            &output,
            &sourceWidth,
            &sourceHeight,
            &outputWidth,
            &outputHeight,
        };
        result = cuLaunchKernel(
            m_BloomFourTapDownsampleKernel,
            DivideRoundUp(outputWidth, blockX),
            DivideRoundUp(outputHeight, blockY),
            1u,
            blockX,
            blockY,
            1u,
            0u,
            m_CudaContext.GetStream(),
            downsampleArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom downsample launch failed: " + CudaContext::GetError(result);
            return false;
        }
        return true;
    };

    const auto launchSharedCascade = [&](const uint32_t sourceLevel, const uint32_t cascadeLevelCount) -> bool
    {
        const uint32_t firstOutputLevel = sourceLevel + 1u;
        CUtexObject source = m_PyramidTextures.GetPointTextureObject(sourceLevel);
        CUsurfObject output1 = getPyramidSurface(firstOutputLevel);
        uint32_t sourceWidth = getPyramidWidth(sourceLevel);
        uint32_t sourceHeight = getPyramidHeight(sourceLevel);
        uint32_t output1Width = getPyramidWidth(firstOutputLevel);
        uint32_t output1Height = getPyramidHeight(firstOutputLevel);

        switch (cascadeLevelCount)
        {
        case 1u:
        {
            void* args[] = {
                &source,
                &output1,
                &sourceWidth,
                &sourceHeight,
                &output1Width,
                &output1Height,
            };
            result = cuLaunchKernel(
                m_BloomOneLevelSharedDownsampleKernel,
                DivideRoundUp(output1Width, 8u),
                DivideRoundUp(output1Height, 8u),
                1u,
                blockX,
                blockY,
                1u,
                0u,
                m_CudaContext.GetStream(),
                args,
                nullptr);
            break;
        }
        case 2u:
        {
            CUsurfObject output2 = getPyramidSurface(firstOutputLevel + 1u);
            uint32_t output2Width = getPyramidWidth(firstOutputLevel + 1u);
            uint32_t output2Height = getPyramidHeight(firstOutputLevel + 1u);
            void* args[] = {
                &source,
                &output1,
                &output2,
                &sourceWidth,
                &sourceHeight,
                &output1Width,
                &output1Height,
                &output2Width,
                &output2Height,
            };
            result = cuLaunchKernel(
                m_BloomTwoLevelSharedDownsampleKernel,
                DivideRoundUp(output1Width, 8u),
                DivideRoundUp(output1Height, 8u),
                1u,
                blockX,
                blockY,
                1u,
                0u,
                m_CudaContext.GetStream(),
                args,
                nullptr);
            break;
        }
        case 3u:
        {
            CUsurfObject output2 = getPyramidSurface(firstOutputLevel + 1u);
            CUsurfObject output3 = getPyramidSurface(firstOutputLevel + 2u);
            uint32_t output2Width = getPyramidWidth(firstOutputLevel + 1u);
            uint32_t output2Height = getPyramidHeight(firstOutputLevel + 1u);
            uint32_t output3Width = getPyramidWidth(firstOutputLevel + 2u);
            uint32_t output3Height = getPyramidHeight(firstOutputLevel + 2u);
            void* args[] = {
                &source,
                &output1,
                &output2,
                &output3,
                &sourceWidth,
                &sourceHeight,
                &output1Width,
                &output1Height,
                &output2Width,
                &output2Height,
                &output3Width,
                &output3Height,
            };
            result = cuLaunchKernel(
                m_BloomThreeLevelSharedDownsampleKernel,
                DivideRoundUp(output1Width, 8u),
                DivideRoundUp(output1Height, 8u),
                1u,
                blockX,
                blockY,
                1u,
                0u,
                m_CudaContext.GetStream(),
                args,
                nullptr);
            break;
        }
        case 4u:
        {
            CUsurfObject output2 = getPyramidSurface(firstOutputLevel + 1u);
            CUsurfObject output3 = getPyramidSurface(firstOutputLevel + 2u);
            CUsurfObject output4 = getPyramidSurface(firstOutputLevel + 3u);
            uint32_t output2Width = getPyramidWidth(firstOutputLevel + 1u);
            uint32_t output2Height = getPyramidHeight(firstOutputLevel + 1u);
            uint32_t output3Width = getPyramidWidth(firstOutputLevel + 2u);
            uint32_t output3Height = getPyramidHeight(firstOutputLevel + 2u);
            uint32_t output4Width = getPyramidWidth(firstOutputLevel + 3u);
            uint32_t output4Height = getPyramidHeight(firstOutputLevel + 3u);
            void* args[] = {
                &source,
                &output1,
                &output2,
                &output3,
                &output4,
                &sourceWidth,
                &sourceHeight,
                &output1Width,
                &output1Height,
                &output2Width,
                &output2Height,
                &output3Width,
                &output3Height,
                &output4Width,
                &output4Height,
            };
            result = cuLaunchKernel(
                m_BloomFourLevelSharedDownsampleKernel,
                DivideRoundUp(output1Width, 8u),
                DivideRoundUp(output1Height, 8u),
                1u,
                blockX,
                blockY,
                1u,
                0u,
                m_CudaContext.GetStream(),
                args,
                nullptr);
            break;
        }
        default:
            m_Status = "Invalid CUDA bloom shared cascade level count.";
            return false;
        }

        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom shared-memory downsample launch failed: " + CudaContext::GetError(result);
            return false;
        }
        return true;
    };

    const bool canUseSharedCascade = m_UseSharedMemoryDownsampling && levelCount >= 5u;
    if (canUseSharedCascade)
    {
        uint32_t sourceLevel = 0u;
        while (sourceLevel + 4u < levelCount)
        {
            if (!launchSharedCascade(sourceLevel, 4u))
            {
                return false;
            }
            sourceLevel += 4u;
        }
        const uint32_t remainingLevelCount = levelCount - sourceLevel - 1u;
        if (remainingLevelCount > 0u && !launchSharedCascade(sourceLevel, remainingLevelCount))
        {
            return false;
        }
    }
    else
    {
        for (uint32_t level = 1u; level < levelCount; ++level)
        {
            if (!launchFourTapDownsample(level - 1u, level))
            {
                return false;
            }
        }
    }

    for (uint32_t level = levelCount - 1u; level > 0u; --level)
    {
        CUtexObject low = m_PyramidTextures.GetLinearTextureObject(level);
        CUtexObject high = m_PyramidTextures.GetPointTextureObject(level - 1u);
        CUsurfObject highOutput = m_PyramidTextures.GetSurfaceObject(level - 1u);
        uint32_t highWidth = m_PyramidWidth[level - 1u];
        uint32_t highHeight = m_PyramidHeight[level - 1u];
        uint32_t lowWidth = m_PyramidWidth[level];
        uint32_t lowHeight = m_PyramidHeight[level];
        uint32_t highLevel = level - 1u;
        void* boxFilterUpsampleArgs[] = {
            &low,
            &high,
            &highOutput,
            &highWidth,
            &highHeight,
            &lowWidth,
            &lowHeight,
            &highLevel,
            &boxFilterSigma,
        };
        void* rasterBloomUpsampleArgs[] = {
            &low,
            &high,
            &highOutput,
            &highWidth,
            &highHeight,
            &lowWidth,
            &lowHeight,
        };
        CUfunction upsampleKernel = m_BloomAdditiveUpsampleKernel;
        void** upsampleArgs = rasterBloomUpsampleArgs;
        if (!classicPyramid)
        {
            switch (m_CudaMethod)
            {
            case CudaMethod::BoxFilterApproximation:
                upsampleKernel = m_BloomBoxFilterAdditiveUpsampleKernel;
                upsampleArgs = boxFilterUpsampleArgs;
                break;
            case CudaMethod::BoxFilterOriginalPaper:
                upsampleKernel = m_BloomBoxFilterInterpolatedUpsampleKernel;
                upsampleArgs = boxFilterUpsampleArgs;
                break;
            case CudaMethod::ClassicPyramid:
            default:
                break;
            }
        }
        result = cuLaunchKernel(
            upsampleKernel,
            DivideRoundUp(highWidth, blockX),
            DivideRoundUp(highHeight, blockY),
            1u,
            blockX,
            blockY,
            1u,
            0u,
            m_CudaContext.GetStream(),
            upsampleArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom upsample launch failed: " + CudaContext::GetError(result);
            return false;
        }
    }

    CUtexObject bloom = m_PyramidTextures.GetLinearTextureObject(0u);
    uint32_t bloomWidth = m_PyramidWidth[0u];
    uint32_t bloomHeight = m_PyramidHeight[0u];
    void* rasterBloomCompositeArgs[] = {
        &input,
        &bloom,
        &output,
        &cudaWidth,
        &cudaHeight,
        &bloomWidth,
        &bloomHeight,
    };
    result = cuLaunchKernel(
        m_BloomCompositeKernel,
        DivideRoundUp(width, blockX),
        DivideRoundUp(height, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        m_CudaContext.GetStream(),
        rasterBloomCompositeArgs,
        nullptr);
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom composite launch failed: " + CudaContext::GetError(result);
        return false;
    }

    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].KernelsEnd);
    }

    return true;
}

bool CudaBloomPass::ExecuteInPlace(Texture& postProcessColor, const uint32_t width, const uint32_t height, ID3D12CommandQueue* d3d12CommandQueue)
{
    if (!m_Enabled)
    {
        return false;
    }
    if (!InitializeCuda())
    {
        return false;
    }
    CollectCompletedCudaTimingFrames();
    BeginCudaTimingFrame();
    if (!EnsureD3D12InteropResource(postProcessColor, width, height))
    {
        m_ActiveCudaTimingFrameIndex = -1;
        return false;
    }
    if (!SignalD3D12AndWaitInCuda(d3d12CommandQueue))
    {
        m_ActiveCudaTimingFrameIndex = -1;
        return false;
    }
    if (!RunCudaBloom(width, height))
    {
        m_ActiveCudaTimingFrameIndex = -1;
        m_CudaContext.Synchronize();
        return false;
    }
    if (!SignalCudaAndWaitInD3D12(d3d12CommandQueue))
    {
        m_ActiveCudaTimingFrameIndex = -1;
        return false;
    }

    m_Status = "CUDA bloom enqueued with shared Framework CUDA interop tools.";
    return true;
}
//Modify End
