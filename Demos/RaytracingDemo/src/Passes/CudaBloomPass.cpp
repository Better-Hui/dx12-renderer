#include <Passes/CudaBloomPass.h>

#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <imgui.h>

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

//Modify Begin:2026-07-30 by BestHui
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

//Modify Begin:2026-07-30 by BestHui
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
    ReleaseInteropResource();
//Modify Begin:2026-07-30 by BestHui
    ReleaseCudaTimingFrames();
    m_PyramidTextures.Release(&m_CudaContext);
//Modify Begin:2026-07-30 by BestHui
    m_PyramidWidth.clear();
    m_PyramidHeight.clear();
//Modify End
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

//Modify Begin:2026-08-16 by BestHui
bool CudaBloomPass::DrawImGui(const uint32_t width, const uint32_t height)
{
    bool changed = false;
    if (ImGui::CollapsingHeader("CUDA Bloom"))
    {
        changed |= ImGui::Checkbox("Enable CUDA Bloom", &m_Enabled);
//Modify Begin:2026-08-16 by BestHui
        const char* bloomMethodNames[] = {
            "Classic Additive Pyramid",
            "Box Filter Approximation (optimized)",
            "Box Filter Approximation (original paper)"
        };
        int bloomMethod = static_cast<int>(m_Method);
        if (ImGui::Combo("Bloom Method", &bloomMethod, bloomMethodNames, IM_ARRAYSIZE(bloomMethodNames)))
        {
            m_Method = static_cast<Method>(bloomMethod);
            changed = true;
        }
//Modify End
//Modify Begin:2026-08-16 by BestHui
        const char* downsampleModeNames[] = {
            "2x2 Cascade",
            "5-Tap Diagonal Bilinear Non-Cascaded",
            "5-Tap Diagonal Bilinear Shared Memory",
            "10-Tap Non-Cascaded",
            "15-Tap Non-Cascaded"
        };
        int downsampleMode = static_cast<int>(m_DownsampleMode);
        if (ImGui::Combo("Bloom Downsample", &downsampleMode, downsampleModeNames, IM_ARRAYSIZE(downsampleModeNames)))
        {
            downsampleMode = std::clamp(downsampleMode, 0, IM_ARRAYSIZE(downsampleModeNames) - 1);
            m_DownsampleMode = static_cast<DownsampleMode>(downsampleMode);
            changed = true;
        }
//Modify End
        changed |= ImGui::SliderFloat("Bloom Threshold", &m_Threshold, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Soft Knee", &m_SoftThreshold, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Intensity", &m_Intensity, 0.0f, 5.0f, "%.2f");
//Modify Begin:2026-07-30 by BestHui
        const uint32_t pyramidWidth = m_Width > 0u ? m_Width : width;
        const uint32_t pyramidHeight = m_Height > 0u ? m_Height : height;
        const int maxPyramidLevels = static_cast<int>(ComputeMaxPyramidLevels(pyramidWidth, pyramidHeight));
        m_PyramidLevels = std::clamp(m_PyramidLevels, 1, maxPyramidLevels);
        changed |= ImGui::SliderInt("Bloom Pyramid Levels", &m_PyramidLevels, 1, maxPyramidLevels);
//Modify End
//Modify Begin:2026-08-16 by BestHui
        if (m_Method != Method::Classic)
        {
            changed |= ImGui::SliderFloat("Box Filter Sigma", &m_BoxFilterSigma, 0.1f, 16.0f, "%.2f");
        }
//Modify End
//Modify Begin:2026-07-30 by BestHui
        if (m_LastCudaTiming.Valid)
        {
            ImGui::Text(
                "CUDA timing: wait %.3f ms, kernels %.3f ms, signal %.3f ms, stream %.3f ms",
                m_LastCudaTiming.D3DToCudaWaitMs,
                m_LastCudaTiming.KernelsMs,
                m_LastCudaTiming.CudaSignalMs,
                m_LastCudaTiming.TotalCudaStreamMs);
        }
//Modify End
        ImGui::TextWrapped("%s", m_Status.c_str());
    }
    return changed;
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

    if (!m_CudaContext.GetFunction(m_Module, "PrefilterDownsampleCascadeKernel", m_PrefilterDownsampleCascadeKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "DownsampleCascadeKernel", m_DownsampleCascadeKernel, error) ||
//Modify Begin:2026-08-16 by BestHui
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample5TapKernel", m_PrefilterDownsample5TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample5TapSharedKernel", m_PrefilterDownsample5TapSharedKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample10TapKernel", m_PrefilterDownsample10TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample15TapKernel", m_PrefilterDownsample15TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample5TapKernel", m_Downsample5TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample5TapSharedKernel", m_Downsample5TapSharedKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample10TapKernel", m_Downsample10TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample15TapKernel", m_Downsample15TapKernel, error) ||
//Modify End
//Modify Begin:2026-07-30 by BestHui
        !m_CudaContext.GetFunction(m_Module, "UpsampleClassicKernel", m_UpsampleClassicKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "UpsampleBoxFilterKernel", m_UpsampleBoxFilterKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "UpsampleBoxFilterOriginalKernel", m_UpsampleBoxFilterOriginalKernel, error) ||
//Modify End
        !m_CudaContext.GetFunction(m_Module, "CompositeBloomKernel", m_CompositeBloomKernel, error))
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
//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].D3DWaitBegin);
    }
//Modify End
    if (!m_TimelineSemaphore.SignalD3D12AndWaitCuda(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
    }
//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].D3DWaitEnd);
    }
//Modify End
    return true;
}

bool CudaBloomPass::SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue)
{
    if (!EnsureD3D12CudaSemaphore())
    {
        return false;
    }

    std::string error;
//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].SignalBegin);
    }
//Modify End
    if (!m_TimelineSemaphore.SignalCudaAndWaitInD3D12(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
    }
//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        CudaTimingFrame& timingFrame = m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)];
        RecordCudaTimingEvent(timingFrame.SignalEnd);
        timingFrame.Pending = true;
        m_ActiveCudaTimingFrameIndex = -1;
    }
//Modify End
    return true;
}

//Modify Begin:2026-07-30 by BestHui
bool CudaBloomPass::EnsureCudaPyramidTextures(const uint32_t width, const uint32_t height, const uint32_t levelCount)
{
    if (!InitializeCuda())
    {
        return false;
    }

    uint32_t levelWidth = HalfSize(width);
    uint32_t levelHeight = HalfSize(height);
//Modify Begin:2026-07-30 by BestHui
    m_PyramidWidth.resize(levelCount);
    m_PyramidHeight.resize(levelCount);
//Modify End
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
//Modify End

//Modify Begin:2026-07-30 by BestHui
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

        CudaTimingStats timingStats = {};
        timingStats.FrameIndex = timingFrame.FrameIndex;
        timingStats.Valid = true;
        cuEventElapsedTime(&timingStats.D3DToCudaWaitMs, timingFrame.D3DWaitBegin, timingFrame.D3DWaitEnd);
        cuEventElapsedTime(&timingStats.KernelsMs, timingFrame.KernelsBegin, timingFrame.KernelsEnd);
        cuEventElapsedTime(&timingStats.CudaSignalMs, timingFrame.SignalBegin, timingFrame.SignalEnd);
        cuEventElapsedTime(&timingStats.TotalCudaStreamMs, timingFrame.D3DWaitBegin, timingFrame.SignalEnd);
        m_LastCudaTiming = timingStats;
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

    m_LastCudaTiming = {};
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
//Modify End

bool CudaBloomPass::RunCudaBloom(const uint32_t width, const uint32_t height)
{
    const uint32_t maxPyramidLevels = ComputeMaxPyramidLevels(width, height);
    m_PyramidLevels = std::clamp(m_PyramidLevels, 1, static_cast<int>(maxPyramidLevels));
    const uint32_t levelCount = static_cast<uint32_t>(m_PyramidLevels);
//Modify Begin:2026-07-30 by BestHui
    if (!EnsureCudaPyramidTextures(width, height, levelCount))
//Modify End
    {
        return false;
    }

    constexpr uint32_t blockX = 16;
    constexpr uint32_t blockY = 16;
    constexpr uint32_t cascadeBlockX = 8;
    constexpr uint32_t cascadeBlockY = 8;
    constexpr uint32_t cascadeSharedBytes = cascadeBlockX * cascadeBlockY * sizeof(float) * 4u;
    constexpr uint32_t shared5BlockX = 16;
    constexpr uint32_t shared5BlockY = 8;
    constexpr uint32_t shared5TileWidth = shared5BlockX * 2u + 2u;
    constexpr uint32_t shared5TileHeight = shared5BlockY * 2u + 2u;
    constexpr uint32_t shared5SharedBytes = shared5TileWidth * shared5TileHeight * sizeof(float) * 4u;

//Modify Begin:2026-07-30 by BestHui
    auto getPyramidPointTexture = [this, levelCount](const uint32_t level) -> CUtexObject
    {
        return level < levelCount ? m_PyramidTextures.GetPointTextureObject(level) : 0;
    };
    auto getPyramidLinearTexture = [this, levelCount](const uint32_t level) -> CUtexObject
    {
        return level < levelCount ? m_PyramidTextures.GetLinearTextureObject(level) : 0;
    };
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
//Modify End

    CUtexObject input = m_InputTexture.GetTextureObject();
    CUsurfObject output = m_InputTexture.GetSurfaceObject();
    uint32_t cudaWidth = width;
    uint32_t cudaHeight = height;
    float threshold = m_Threshold;
    float softThreshold = m_SoftThreshold;
    float intensity = m_Intensity;
//Modify Begin:2026-08-16 by BestHui
    float boxFilterSigma = (std::max)(m_BoxFilterSigma, 0.001f);
//Modify End

//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].KernelsBegin);
    }
//Modify End

//Modify Begin:2026-08-16 by BestHui
    CUresult result = CUDA_SUCCESS;
    if (m_DownsampleMode == DownsampleMode::Cascaded2x2)
    {
//Modify Begin:2026-07-30 by BestHui
        CUsurfObject level0 = getPyramidSurface(0u);
        CUsurfObject level1 = getPyramidSurface(1u);
        CUsurfObject level2 = getPyramidSurface(2u);
        CUsurfObject level3 = getPyramidSurface(3u);
//Modify End
        uint32_t level0Width = getPyramidWidth(0u);
        uint32_t level0Height = getPyramidHeight(0u);
        uint32_t level1Width = getPyramidWidth(1u);
        uint32_t level1Height = getPyramidHeight(1u);
        uint32_t level2Width = getPyramidWidth(2u);
        uint32_t level2Height = getPyramidHeight(2u);
        uint32_t level3Width = getPyramidWidth(3u);
        uint32_t level3Height = getPyramidHeight(3u);

        void* prefilterCascadeArgs[] = {
            &input,
            &level0,
            &level1,
            &level2,
            &level3,
            &cudaWidth,
            &cudaHeight,
            &level0Width,
            &level0Height,
            &level1Width,
            &level1Height,
            &level2Width,
            &level2Height,
            &level3Width,
            &level3Height,
            &threshold,
            &softThreshold,
            &intensity,
        };
        result = cuLaunchKernel(
            m_PrefilterDownsampleCascadeKernel,
            DivideRoundUp(level0Width, cascadeBlockX),
            DivideRoundUp(level0Height, cascadeBlockY),
            1u,
            cascadeBlockX,
            cascadeBlockY,
            1u,
            cascadeSharedBytes,
            m_CudaContext.GetStream(),
            prefilterCascadeArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom prefilter launch failed: " + CudaContext::GetError(result);
            return false;
        }

        for (uint32_t baseLevel = 3u; baseLevel + 1u < levelCount; baseLevel += 4u)
        {
//Modify Begin:2026-07-30 by BestHui
            CUtexObject source = m_PyramidTextures.GetPointTextureObject(baseLevel);
            CUsurfObject output0 = getPyramidSurface(baseLevel + 1u);
            CUsurfObject output1 = getPyramidSurface(baseLevel + 2u);
            CUsurfObject output2 = getPyramidSurface(baseLevel + 3u);
            CUsurfObject output3 = getPyramidSurface(baseLevel + 4u);
//Modify End
            uint32_t sourceWidth = m_PyramidWidth[baseLevel];
            uint32_t sourceHeight = m_PyramidHeight[baseLevel];
            uint32_t output0Width = getPyramidWidth(baseLevel + 1u);
            uint32_t output0Height = getPyramidHeight(baseLevel + 1u);
            uint32_t output1Width = getPyramidWidth(baseLevel + 2u);
            uint32_t output1Height = getPyramidHeight(baseLevel + 2u);
            uint32_t output2Width = getPyramidWidth(baseLevel + 3u);
            uint32_t output2Height = getPyramidHeight(baseLevel + 3u);
            uint32_t output3Width = getPyramidWidth(baseLevel + 4u);
            uint32_t output3Height = getPyramidHeight(baseLevel + 4u);
            void* downsampleCascadeArgs[] = {
                &source,
                &output0,
                &output1,
                &output2,
                &output3,
                &sourceWidth,
                &sourceHeight,
                &output0Width,
                &output0Height,
                &output1Width,
                &output1Height,
                &output2Width,
                &output2Height,
                &output3Width,
                &output3Height,
            };
            result = cuLaunchKernel(
                m_DownsampleCascadeKernel,
                DivideRoundUp(output0Width, cascadeBlockX),
                DivideRoundUp(output0Height, cascadeBlockY),
                1u,
                cascadeBlockX,
                cascadeBlockY,
                1u,
                cascadeSharedBytes,
                m_CudaContext.GetStream(),
                downsampleCascadeArgs,
                nullptr);
            if (result != CUDA_SUCCESS)
            {
                m_Status = "CUDA bloom downsample launch failed: " + CudaContext::GetError(result);
                return false;
            }
        }
    }
    else
    {
        uint32_t firstWidth = getPyramidWidth(0u);
        uint32_t firstHeight = getPyramidHeight(0u);
        CUsurfObject firstOutput = getPyramidSurface(0u);
        CUfunction prefilterKernel = m_PrefilterDownsample5TapKernel;
        CUfunction downsampleKernel = m_Downsample5TapKernel;
        switch (m_DownsampleMode)
        {
        case DownsampleMode::NonCascaded10Tap:
            prefilterKernel = m_PrefilterDownsample10TapKernel;
            downsampleKernel = m_Downsample10TapKernel;
            break;
        case DownsampleMode::NonCascaded15Tap:
            prefilterKernel = m_PrefilterDownsample15TapKernel;
            downsampleKernel = m_Downsample15TapKernel;
            break;
        case DownsampleMode::NonCascaded5TapShared:
        case DownsampleMode::NonCascaded5Tap:
        default:
            break;
        }
        const bool shared5Mode = m_DownsampleMode == DownsampleMode::NonCascaded5TapShared;
        const auto isStrictHalf = [](const uint32_t sourceSize, const uint32_t outputSize) -> bool
        {
            return outputSize > 0u && sourceSize == outputSize * 2u;
        };
        const bool shared5Prefilter = shared5Mode &&
            isStrictHalf(cudaWidth, firstWidth) &&
            isStrictHalf(cudaHeight, firstHeight);
        CUtexObject inputDownsample = shared5Prefilter
            ? m_InputTexture.GetTextureObject()
            : m_InputTexture.GetLinearTextureObject();
        const uint32_t prefilterBlockWidth = shared5Prefilter ? shared5BlockX : blockX;
        const uint32_t prefilterBlockHeight = shared5Prefilter ? shared5BlockY : blockY;
        const uint32_t prefilterSharedMemory = shared5Prefilter ? shared5SharedBytes : 0u;
        if (shared5Prefilter)
        {
            prefilterKernel = m_PrefilterDownsample5TapSharedKernel;
        }
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
            prefilterKernel,
            DivideRoundUp(firstWidth, prefilterBlockWidth),
            DivideRoundUp(firstHeight, prefilterBlockHeight),
            1u,
            prefilterBlockWidth,
            prefilterBlockHeight,
            1u,
            prefilterSharedMemory,
            m_CudaContext.GetStream(),
            prefilterArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom prefilter launch failed: " + CudaContext::GetError(result);
            return false;
        }

        for (uint32_t level = 1u; level < levelCount; ++level)
        {
            CUsurfObject outputLevel = m_PyramidTextures.GetSurfaceObject(level);
            uint32_t sourceWidth = m_PyramidWidth[level - 1u];
            uint32_t sourceHeight = m_PyramidHeight[level - 1u];
            uint32_t outputWidth = m_PyramidWidth[level];
            uint32_t outputHeight = m_PyramidHeight[level];
            const bool shared5Downsample = shared5Mode &&
                isStrictHalf(sourceWidth, outputWidth) &&
                isStrictHalf(sourceHeight, outputHeight);
            CUfunction levelDownsampleKernel = shared5Downsample
                ? m_Downsample5TapSharedKernel
                : downsampleKernel;
            CUtexObject source = shared5Downsample
                ? m_PyramidTextures.GetPointTextureObject(level - 1u)
                : m_PyramidTextures.GetLinearTextureObject(level - 1u);
            const uint32_t levelBlockWidth = shared5Downsample ? shared5BlockX : blockX;
            const uint32_t levelBlockHeight = shared5Downsample ? shared5BlockY : blockY;
            const uint32_t levelSharedMemory = shared5Downsample ? shared5SharedBytes : 0u;
            void* downsampleArgs[] = {
                &source,
                &outputLevel,
                &sourceWidth,
                &sourceHeight,
                &outputWidth,
                &outputHeight,
            };
            result = cuLaunchKernel(
                levelDownsampleKernel,
                DivideRoundUp(outputWidth, levelBlockWidth),
                DivideRoundUp(outputHeight, levelBlockHeight),
                1u,
                levelBlockWidth,
                levelBlockHeight,
                1u,
                levelSharedMemory,
                m_CudaContext.GetStream(),
                downsampleArgs,
                nullptr);
            if (result != CUDA_SUCCESS)
            {
                m_Status = "CUDA bloom non-cascaded downsample launch failed: " + CudaContext::GetError(result);
                return false;
            }
        }
    }

//Modify End
    for (uint32_t level = levelCount - 1u; level > 0u; --level)
    {
//Modify Begin:2026-07-30 by BestHui
        CUtexObject low = m_PyramidTextures.GetLinearTextureObject(level);
        CUtexObject high = m_PyramidTextures.GetPointTextureObject(level - 1u);
        CUsurfObject highOutput = m_PyramidTextures.GetSurfaceObject(level - 1u);
//Modify End
        uint32_t highWidth = m_PyramidWidth[level - 1u];
        uint32_t highHeight = m_PyramidHeight[level - 1u];
//Modify Begin:2026-08-16 by BestHui
        uint32_t highLevel = level - 1u;
//Modify End
        void* classicUpsampleArgs[] = {
            &low,
            &high,
//Modify Begin:2026-07-30 by BestHui
            &highOutput,
//Modify End
            &highWidth,
            &highHeight,
        };
        void* boxFilterUpsampleArgs[] = {
            &low,
            &high,
            &highOutput,
            &highWidth,
            &highHeight,
            &highLevel,
            &boxFilterSigma,
        };
        CUfunction upsampleKernel = m_UpsampleClassicKernel;
        void** upsampleArgs = classicUpsampleArgs;
        switch (m_Method)
        {
        case Method::BoxFilterApproximation:
            upsampleKernel = m_UpsampleBoxFilterKernel;
            upsampleArgs = boxFilterUpsampleArgs;
            break;
        case Method::BoxFilterOriginalPaper:
            upsampleKernel = m_UpsampleBoxFilterOriginalKernel;
            upsampleArgs = boxFilterUpsampleArgs;
            break;
        case Method::Classic:
        default:
            break;
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

//Modify Begin:2026-07-30 by BestHui
    CUtexObject bloom = m_PyramidTextures.GetLinearTextureObject(0u);
//Modify End
    void* compositeArgs[] = {
        &input,
//Modify Begin:2026-07-30 by BestHui
        &bloom,
//Modify End
        &output,
        &cudaWidth,
        &cudaHeight,
    };
    result = cuLaunchKernel(
        m_CompositeBloomKernel,
        DivideRoundUp(width, blockX),
        DivideRoundUp(height, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        m_CudaContext.GetStream(),
        compositeArgs,
        nullptr);
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom composite launch failed: " + CudaContext::GetError(result);
        return false;
    }

//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].KernelsEnd);
    }
//Modify End

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
//Modify Begin:2026-07-30 by BestHui
    CollectCompletedCudaTimingFrames();
    BeginCudaTimingFrame();
//Modify End
    if (!EnsureD3D12InteropResource(postProcessColor, width, height))
    {
//Modify Begin:2026-07-30 by BestHui
        m_ActiveCudaTimingFrameIndex = -1;
//Modify End
        return false;
    }
    if (!SignalD3D12AndWaitInCuda(d3d12CommandQueue))
    {
//Modify Begin:2026-07-30 by BestHui
        m_ActiveCudaTimingFrameIndex = -1;
//Modify End
        return false;
    }
    if (!RunCudaBloom(width, height))
    {
//Modify Begin:2026-07-30 by BestHui
        m_ActiveCudaTimingFrameIndex = -1;
//Modify End
        m_CudaContext.Synchronize();
        return false;
    }
    if (!SignalCudaAndWaitInD3D12(d3d12CommandQueue))
    {
//Modify Begin:2026-07-30 by BestHui
        m_ActiveCudaTimingFrameIndex = -1;
//Modify End
        return false;
    }

    m_Status = "CUDA bloom enqueued with shared Framework CUDA interop tools.";
    return true;
}
