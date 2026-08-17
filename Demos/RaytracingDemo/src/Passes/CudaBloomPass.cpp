#include <Passes/CudaBloomPass.h>

#include <DX12Library/Helpers.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/PostProcess/Bloom.h>

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
//Modify Begin:2026-08-16 by BestHui
    m_FrameworkBloom.reset();
    m_FrameworkBloomWidth = 0;
    m_FrameworkBloomHeight = 0;
    m_FrameworkBloomPyramidLevels = 0;
//Modify End
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
//Modify Begin:2026-08-17 by BestHui
        const char* backendNames[] = { "CUDA", "Built-in Raster" };
        int backend = static_cast<int>(m_Backend);
        if (ImGui::Combo("Bloom Backend", &backend, backendNames, IM_ARRAYSIZE(backendNames)))
        {
            m_Backend = static_cast<Backend>(backend);
            changed = true;
        }

        if (!IsFrameworkRaster())
        {
            const char* methodNames[] = {
                "Classic Additive Pyramid",
                "Box Filter Approximation (optimized)",
                "Box Filter Approximation (original paper)",
                "Raster-Matched",
            };
            int method = static_cast<int>(m_CudaMethod);
            if (ImGui::Combo("CUDA Method", &method, methodNames, IM_ARRAYSIZE(methodNames)))
            {
                m_CudaMethod = static_cast<CudaMethod>(method);
                changed = true;
            }

            const char* tapCountNames[] = { "5 Tap", "10 Tap", "15 Tap" };
            constexpr std::array<DownsampleTapCount, IM_ARRAYSIZE(tapCountNames)> tapCounts = {
                DownsampleTapCount::Five,
                DownsampleTapCount::Ten,
                DownsampleTapCount::Fifteen,
            };
            const auto tapCountIterator = std::find(tapCounts.begin(), tapCounts.end(), m_DownsampleTapCount);
            int tapCount = tapCountIterator == tapCounts.end()
                ? 0
                : static_cast<int>(std::distance(tapCounts.begin(), tapCountIterator));
            if (ImGui::Combo("CUDA Downsample Taps", &tapCount, tapCountNames, IM_ARRAYSIZE(tapCountNames)))
            {
                m_DownsampleTapCount = tapCounts[static_cast<size_t>(tapCount)];
                changed = true;
            }

            if (m_CudaMethod == CudaMethod::RasterMatched)
            {
                ImGui::TextDisabled("5 Tap matches raster sampling; 10/15 Tap retain raster reconstruction with denser downsampling.");
            }
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
//Modify Begin:2026-08-17 by BestHui
        if (!IsFrameworkRaster() &&
            (m_CudaMethod == CudaMethod::BoxFilterApproximation ||
                m_CudaMethod == CudaMethod::BoxFilterOriginalPaper))
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

//Modify Begin:2026-08-16 by BestHui
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

//Modify Begin:2026-08-17 by BestHui
    if (!m_CudaContext.GetFunction(m_Module, "PrefilterDownsample5TapKernel", m_PrefilterDownsample5TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample10TapKernel", m_PrefilterDownsample10TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsample15TapKernel", m_PrefilterDownsample15TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample5TapKernel", m_Downsample5TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample10TapKernel", m_Downsample10TapKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "Downsample15TapKernel", m_Downsample15TapKernel, error) ||
//Modify Begin:2026-08-17 by BestHui
        !m_CudaContext.GetFunction(m_Module, "PrefilterDownsampleRasterMatchedKernel", m_PrefilterDownsampleRasterMatchedKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "DownsampleRasterMatchedKernel", m_DownsampleRasterMatchedKernel, error) ||
//Modify End
//Modify Begin:2026-07-30 by BestHui
        !m_CudaContext.GetFunction(m_Module, "UpsampleClassicKernel", m_UpsampleClassicKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "UpsampleBoxFilterKernel", m_UpsampleBoxFilterKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "UpsampleBoxFilterOriginalKernel", m_UpsampleBoxFilterOriginalKernel, error) ||
//Modify End
        !m_CudaContext.GetFunction(m_Module, "CompositeBloomKernel", m_CompositeBloomKernel, error) ||
//Modify Begin:2026-08-17 by BestHui
        !m_CudaContext.GetFunction(m_Module, "UpsampleRasterMatchedKernel", m_UpsampleRasterMatchedKernel, error) ||
        !m_CudaContext.GetFunction(m_Module, "CompositeRasterMatchedBloomKernel", m_CompositeRasterMatchedBloomKernel, error))
//Modify End
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

//Modify Begin:2026-07-30 by BestHui
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
//Modify Begin:2026-08-17 by BestHui
    const bool rasterMatched = IsCudaRasterMatched();
//Modify End
//Modify Begin:2026-08-16 by BestHui
    float boxFilterSigma = (std::max)(m_BoxFilterSigma, 0.001f);
//Modify End

//Modify Begin:2026-07-30 by BestHui
    if (m_ActiveCudaTimingFrameIndex >= 0)
    {
        RecordCudaTimingEvent(m_CudaTimingFrames[static_cast<size_t>(m_ActiveCudaTimingFrameIndex)].KernelsBegin);
    }
//Modify End

//Modify Begin:2026-08-17 by BestHui
    CUresult result = CUDA_SUCCESS;
    uint32_t firstWidth = getPyramidWidth(0u);
    uint32_t firstHeight = getPyramidHeight(0u);
    CUsurfObject firstOutput = getPyramidSurface(0u);
    const bool rasterMatchedSampling = rasterMatched && m_DownsampleTapCount == DownsampleTapCount::Five;
    CUfunction prefilterKernel = rasterMatchedSampling
        ? m_PrefilterDownsampleRasterMatchedKernel
        : m_PrefilterDownsample5TapKernel;
    CUfunction downsampleKernel = rasterMatchedSampling
        ? m_DownsampleRasterMatchedKernel
        : m_Downsample5TapKernel;
    if (!rasterMatchedSampling)
    {
        switch (m_DownsampleTapCount)
        {
        case DownsampleTapCount::Ten:
            prefilterKernel = m_PrefilterDownsample10TapKernel;
            downsampleKernel = m_Downsample10TapKernel;
            break;
        case DownsampleTapCount::Fifteen:
            prefilterKernel = m_PrefilterDownsample15TapKernel;
            downsampleKernel = m_Downsample15TapKernel;
            break;
        case DownsampleTapCount::Five:
        default:
            break;
        }
    }

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
        prefilterKernel,
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

    for (uint32_t level = 1u; level < levelCount; ++level)
    {
        CUsurfObject outputLevel = m_PyramidTextures.GetSurfaceObject(level);
        uint32_t sourceWidth = m_PyramidWidth[level - 1u];
        uint32_t sourceHeight = m_PyramidHeight[level - 1u];
        uint32_t outputWidth = m_PyramidWidth[level];
        uint32_t outputHeight = m_PyramidHeight[level];
        CUtexObject source = m_PyramidTextures.GetLinearTextureObject(level - 1u);
        void* downsampleArgs[] = {
            &source,
            &outputLevel,
            &sourceWidth,
            &sourceHeight,
            &outputWidth,
            &outputHeight,
        };
        result = cuLaunchKernel(
            downsampleKernel,
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
//Modify Begin:2026-08-17 by BestHui
        uint32_t lowWidth = m_PyramidWidth[level];
        uint32_t lowHeight = m_PyramidHeight[level];
//Modify End
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
//Modify Begin:2026-08-17 by BestHui
        void* rasterMatchedUpsampleArgs[] = {
            &low,
            &high,
            &highOutput,
            &highWidth,
            &highHeight,
            &lowWidth,
            &lowHeight,
        };
//Modify End
        CUfunction upsampleKernel = m_UpsampleClassicKernel;
        void** upsampleArgs = classicUpsampleArgs;
        if (rasterMatched)
        {
            upsampleKernel = m_UpsampleRasterMatchedKernel;
            upsampleArgs = rasterMatchedUpsampleArgs;
        }
        else
        {
            switch (m_CudaMethod)
            {
            case CudaMethod::BoxFilterApproximation:
                upsampleKernel = m_UpsampleBoxFilterKernel;
                upsampleArgs = boxFilterUpsampleArgs;
                break;
            case CudaMethod::BoxFilterOriginalPaper:
                upsampleKernel = m_UpsampleBoxFilterOriginalKernel;
                upsampleArgs = boxFilterUpsampleArgs;
                break;
            case CudaMethod::Classic:
            case CudaMethod::RasterMatched:
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
//Modify Begin:2026-08-17 by BestHui
    uint32_t bloomWidth = m_PyramidWidth[0u];
    uint32_t bloomHeight = m_PyramidHeight[0u];
    void* rasterMatchedCompositeArgs[] = {
        &input,
        &bloom,
        &output,
        &cudaWidth,
        &cudaHeight,
        &bloomWidth,
        &bloomHeight,
    };
    const CUfunction compositeKernel = rasterMatched
        ? m_CompositeRasterMatchedBloomKernel
        : m_CompositeBloomKernel;
    void** selectedCompositeArgs = rasterMatched
        ? rasterMatchedCompositeArgs
        : compositeArgs;
//Modify End
    result = cuLaunchKernel(
        compositeKernel,
        DivideRoundUp(width, blockX),
        DivideRoundUp(height, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        m_CudaContext.GetStream(),
        selectedCompositeArgs,
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

//Modify Begin:2026-08-17 by BestHui
    m_Status = IsCudaRasterMatched()
        ? "CUDA raster-matched bloom enqueued with shared Framework CUDA interop tools."
        : "CUDA bloom enqueued with shared Framework CUDA interop tools.";
//Modify End
    return true;
}
