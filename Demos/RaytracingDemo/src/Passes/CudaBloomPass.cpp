#include <Passes/CudaBloomPass.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

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

CudaBloomPass::CudaBloomPass() = default;

CudaBloomPass::~CudaBloomPass()
{
    Shutdown();
}

void CudaBloomPass::Shutdown()
{
    ReleaseInteropResource();
    m_PyramidBuffers.Release(&m_CudaContext);
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

bool CudaBloomPass::DrawImGui()
{
    bool changed = false;
    if (ImGui::CollapsingHeader("CUDA Bloom", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enable CUDA Bloom", &m_Enabled);
        changed |= ImGui::SliderFloat("Bloom Threshold", &m_Threshold, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Soft Knee", &m_SoftThreshold, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Intensity", &m_Intensity, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderInt("Bloom Pyramid Levels", &m_PyramidLevels, 1, static_cast<int>(MaxBloomPyramidLevels));
        ImGui::TextWrapped("%s", m_Status.c_str());
    }
    return changed;
}

bool CudaBloomPass::InitializeCuda()
{
    if (m_AvailabilityChecked)
    {
        return m_CudaAvailable;
    }

    m_AvailabilityChecked = true;
    std::string error;
    const auto device = Application::Get().GetDevice();
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
        !m_CudaContext.GetFunction(m_Module, "UpsampleAddKernel", m_UpsampleAddKernel, error) ||
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
    const auto device = Application::Get().GetDevice();
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
    const auto device = Application::Get().GetDevice();
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
    if (!m_TimelineSemaphore.SignalD3D12AndWaitCuda(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
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
    if (!m_TimelineSemaphore.SignalCudaAndWaitInD3D12(d3d12CommandQueue, m_CudaContext, error))
    {
        m_Status = error;
        return false;
    }
    return true;
}

bool CudaBloomPass::EnsureCudaPyramidBuffers(const uint32_t width, const uint32_t height, const uint32_t levelCount)
{
    if (!InitializeCuda())
    {
        return false;
    }

    uint32_t levelWidth = HalfSize(width);
    uint32_t levelHeight = HalfSize(height);
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const size_t requiredBytes = static_cast<size_t>(levelWidth) * static_cast<size_t>(levelHeight) * sizeof(float) * 4u;
        std::string error;
        if (!m_PyramidBuffers.EnsureBuffer(level, requiredBytes, error))
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

bool CudaBloomPass::RunCudaBloom(const uint32_t width, const uint32_t height)
{
    const uint32_t levelCount = static_cast<uint32_t>(std::clamp(m_PyramidLevels, 1, static_cast<int>(MaxBloomPyramidLevels)));
    if (!EnsureCudaPyramidBuffers(width, height, levelCount))
    {
        return false;
    }

    constexpr uint32_t blockX = 16;
    constexpr uint32_t blockY = 16;
    constexpr uint32_t cascadeBlockX = 8;
    constexpr uint32_t cascadeBlockY = 8;
    constexpr uint32_t cascadeSharedBytes = cascadeBlockX * cascadeBlockY * sizeof(float) * 4u;

    auto getPyramidPtr = [this, levelCount](const uint32_t level) -> CUdeviceptr
    {
        return level < levelCount ? m_PyramidBuffers.GetBuffer(level) : 0;
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

    CUdeviceptr level0 = getPyramidPtr(0u);
    CUdeviceptr level1 = getPyramidPtr(1u);
    CUdeviceptr level2 = getPyramidPtr(2u);
    CUdeviceptr level3 = getPyramidPtr(3u);
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
    CUresult result = cuLaunchKernel(
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
        CUdeviceptr source = m_PyramidBuffers.GetBuffer(baseLevel);
        CUdeviceptr output0 = getPyramidPtr(baseLevel + 1u);
        CUdeviceptr output1 = getPyramidPtr(baseLevel + 2u);
        CUdeviceptr output2 = getPyramidPtr(baseLevel + 3u);
        CUdeviceptr output3 = getPyramidPtr(baseLevel + 4u);
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

    for (uint32_t level = levelCount - 1u; level > 0u; --level)
    {
        CUdeviceptr low = m_PyramidBuffers.GetBuffer(level);
        CUdeviceptr high = m_PyramidBuffers.GetBuffer(level - 1u);
        uint32_t lowWidth = m_PyramidWidth[level];
        uint32_t lowHeight = m_PyramidHeight[level];
        uint32_t lowPitch = lowWidth;
        uint32_t highWidth = m_PyramidWidth[level - 1u];
        uint32_t highHeight = m_PyramidHeight[level - 1u];
        uint32_t highPitch = highWidth;
        void* upsampleArgs[] = {
            &low,
            &high,
            &lowWidth,
            &lowHeight,
            &lowPitch,
            &highWidth,
            &highHeight,
            &highPitch,
        };
        result = cuLaunchKernel(
            m_UpsampleAddKernel,
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

    uint32_t bloomPitch = level0Width;
    void* compositeArgs[] = {
        &input,
        &level0,
        &output,
        &cudaWidth,
        &cudaHeight,
        &level0Width,
        &level0Height,
        &bloomPitch,
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
    if (!EnsureD3D12InteropResource(postProcessColor, width, height))
    {
        return false;
    }
    if (!SignalD3D12AndWaitInCuda(d3d12CommandQueue))
    {
        return false;
    }
    if (!RunCudaBloom(width, height))
    {
        m_CudaContext.Synchronize();
        return false;
    }
    if (!SignalCudaAndWaitInD3D12(d3d12CommandQueue))
    {
        return false;
    }

    m_Status = "CUDA bloom enqueued with shared Framework CUDA interop tools.";
    return true;
}
