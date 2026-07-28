#include <PostProcessing/CudaBloomPass.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

#include <d3dx12.h>
#include <imgui.h>
#include <Windows.h>
#include <cuda.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    constexpr uint32_t MAX_BLOOM_PYRAMID_LEVELS = 8;

    uint32_t DivideRoundUp(const uint32_t value, const uint32_t divisor)
    {
        return (value + divisor - 1u) / divisor;
    }

    uint32_t HalfSize(const uint32_t value)
    {
        return value > 1u ? value >> 1u : 1u;
    }

    std::string LoadTextFile(const std::wstring& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return {};
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    bool MatchesAdapterLuid(const char cudaLuid[8], const LUID& adapterLuid)
    {
        return std::memcmp(cudaLuid, &adapterLuid, sizeof(cudaLuid)) == 0;
    }

    uint64_t GetD3D12ResourceAllocationSize(ID3D12Device2* device, const D3D12_RESOURCE_DESC& desc)
    {
        return device->GetResourceAllocationInfo(0u, 1u, &desc).SizeInBytes;
    }
}

struct CudaBloomPass::CudaDriver
{
    struct ExternalTexture
    {
        CUexternalMemory Memory = nullptr;
        CUmipmappedArray MipmappedArray = nullptr;
        CUarray Array = nullptr;
        CUtexObject TextureObject = 0;
        CUsurfObject SurfaceObject = 0;
    };

    CUcontext Context = nullptr;
    CUmodule Module = nullptr;
    CUfunction PrefilterDownsampleKernel = nullptr;
    CUfunction DownsampleKernel = nullptr;
    CUfunction UpsampleAddKernel = nullptr;
    CUfunction CompositeBloomKernel = nullptr;
    ExternalTexture InputTexture;
    ExternalTexture OutputTexture;
    std::array<CUdeviceptr, MAX_BLOOM_PYRAMID_LEVELS> Pyramid = {};
    std::array<size_t, MAX_BLOOM_PYRAMID_LEVELS> PyramidCapacity = {};
    std::array<uint32_t, MAX_BLOOM_PYRAMID_LEVELS> PyramidWidth = {};
    std::array<uint32_t, MAX_BLOOM_PYRAMID_LEVELS> PyramidHeight = {};
    size_t Capacity = 0;

    static std::string GetError(CUresult result)
    {
        const char* message = nullptr;
        if (cuGetErrorString(result, &message) == CUDA_SUCCESS && message != nullptr)
        {
            return message;
        }
        return "CUDA error " + std::to_string(result);
    }

    static bool SelectDeviceForD3D12Adapter(CUdevice& outDevice, std::string& outError)
    {
        const auto d3dDevice = Application::Get().GetDevice();
        const LUID adapterLuid = d3dDevice->GetAdapterLuid();

        int deviceCount = 0;
        CUresult result = cuDeviceGetCount(&deviceCount);
        if (result != CUDA_SUCCESS)
        {
            outError = GetError(result);
            return false;
        }

        for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
        {
            CUdevice candidate = 0;
            result = cuDeviceGet(&candidate, deviceIndex);
            if (result != CUDA_SUCCESS)
            {
                continue;
            }

            char cudaLuid[8] = {};
            unsigned int deviceNodeMask = 0;
            result = cuDeviceGetLuid(cudaLuid, &deviceNodeMask, candidate);
            if (result == CUDA_SUCCESS && MatchesAdapterLuid(cudaLuid, adapterLuid))
            {
                outDevice = candidate;
                return true;
            }
        }

        outError = "CUDA could not find a device matching the D3D12 adapter LUID.";
        return false;
    }

    void ReleaseExternalTexture(ExternalTexture& texture)
    {
        if (texture.SurfaceObject != 0)
        {
            cuSurfObjectDestroy(texture.SurfaceObject);
            texture.SurfaceObject = 0;
        }
        if (texture.TextureObject != 0)
        {
            cuTexObjectDestroy(texture.TextureObject);
            texture.TextureObject = 0;
        }
        if (texture.MipmappedArray != nullptr)
        {
            cuMipmappedArrayDestroy(texture.MipmappedArray);
            texture.MipmappedArray = nullptr;
            texture.Array = nullptr;
        }
        if (texture.Memory != nullptr)
        {
            cuDestroyExternalMemory(texture.Memory);
            texture.Memory = nullptr;
        }
    }

    void ReleaseExternalTextures()
    {
        ReleaseExternalTexture(InputTexture);
        ReleaseExternalTexture(OutputTexture);
        Capacity = 0;
    }

    void Destroy()
    {
        ReleaseExternalTextures();
        for (CUdeviceptr& pyramidBuffer : Pyramid)
        {
            if (pyramidBuffer != 0)
            {
                cuMemFree(pyramidBuffer);
                pyramidBuffer = 0;
            }
        }
        PyramidCapacity = {};
        PyramidWidth = {};
        PyramidHeight = {};
        if (Module != nullptr)
        {
            cuModuleUnload(Module);
            Module = nullptr;
        }
        if (Context != nullptr)
        {
            cuCtxDestroy(Context);
            Context = nullptr;
        }
    }

    bool ImportSharedTexture(
        ID3D12Resource* resource,
        const uint32_t width,
        const uint32_t height,
        const bool createTextureObject,
        const bool createSurfaceObject,
        ExternalTexture& texture,
        std::string& outError)
    {
        HANDLE sharedHandle = nullptr;
        const auto device = Application::Get().GetDevice();
        const HRESULT sharedHandleResult = device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &sharedHandle);
        if (FAILED(sharedHandleResult))
        {
            outError = "D3D12 shared texture handle creation failed. The source/destination texture must be created with D3D12_HEAP_FLAG_SHARED.";
            return false;
        }

        const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
        const uint64_t allocationSize = GetD3D12ResourceAllocationSize(device.Get(), resourceDesc);

        CUDA_EXTERNAL_MEMORY_HANDLE_DESC memoryDesc = {};
        memoryDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
        memoryDesc.handle.win32.handle = sharedHandle;
        memoryDesc.size = allocationSize;
        memoryDesc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;

        CUresult result = cuImportExternalMemory(&texture.Memory, &memoryDesc);
        CloseHandle(sharedHandle);
        if (result != CUDA_SUCCESS)
        {
            outError = "CUDA failed to import shared D3D12 texture: " + GetError(result);
            return false;
        }

        CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipDesc = {};
        mipDesc.offset = 0;
        mipDesc.arrayDesc.Width = width;
        mipDesc.arrayDesc.Height = height;
        mipDesc.arrayDesc.Depth = 0;
        mipDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
        mipDesc.arrayDesc.NumChannels = 4;
        mipDesc.arrayDesc.Flags = (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0
            ? CUDA_ARRAY3D_COLOR_ATTACHMENT
            : 0u;
        if (createSurfaceObject)
        {
            mipDesc.arrayDesc.Flags |= CUDA_ARRAY3D_SURFACE_LDST;
        }
        mipDesc.numLevels = 1;

        result = cuExternalMemoryGetMappedMipmappedArray(&texture.MipmappedArray, texture.Memory, &mipDesc);
        if (result != CUDA_SUCCESS)
        {
            outError = "CUDA failed to map shared D3D12 texture as mipmapped array: " + GetError(result);
            ReleaseExternalTexture(texture);
            return false;
        }

        result = cuMipmappedArrayGetLevel(&texture.Array, texture.MipmappedArray, 0u);
        if (result != CUDA_SUCCESS)
        {
            outError = "CUDA failed to get shared texture mip level: " + GetError(result);
            ReleaseExternalTexture(texture);
            return false;
        }

        CUDA_RESOURCE_DESC cudaResourceDesc = {};
        cudaResourceDesc.resType = CU_RESOURCE_TYPE_ARRAY;
        cudaResourceDesc.res.array.hArray = texture.Array;

        if (createTextureObject)
        {
            CUDA_TEXTURE_DESC textureDesc = {};
            textureDesc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
            textureDesc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
            textureDesc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
            textureDesc.filterMode = CU_TR_FILTER_MODE_POINT;
            textureDesc.flags = 0;
            result = cuTexObjectCreate(&texture.TextureObject, &cudaResourceDesc, &textureDesc, nullptr);
            if (result != CUDA_SUCCESS)
            {
                outError = "CUDA failed to create source texture object: " + GetError(result);
                ReleaseExternalTexture(texture);
                return false;
            }
        }

        if (createSurfaceObject)
        {
            result = cuSurfObjectCreate(&texture.SurfaceObject, &cudaResourceDesc);
            if (result != CUDA_SUCCESS)
            {
                outError = "CUDA failed to create destination surface object: " + GetError(result);
                ReleaseExternalTexture(texture);
                return false;
            }
        }

        return true;
    }
};

CudaBloomPass::CudaBloomPass() = default;

CudaBloomPass::~CudaBloomPass()
{
    Shutdown();
}

void CudaBloomPass::Shutdown()
{
    if (m_Cuda != nullptr)
    {
        m_Cuda->Destroy();
        m_Cuda.reset();
    }
    m_SourceInteropResource = nullptr;
    m_DestinationInteropResource = nullptr;
    m_CudaAvailable = false;
    m_AvailabilityChecked = false;
}

bool CudaBloomPass::DrawImGui()
{
    bool changed = false;
    if (ImGui::CollapsingHeader("CUDA Bloom", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enable CUDA Bloom", &m_Enabled);
        changed |= ImGui::SliderFloat("Bloom Threshold", &m_Threshold, 0.05f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Soft Threshold", &m_SoftThreshold, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Intensity", &m_Intensity, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderInt("Bloom Pyramid Levels", &m_PyramidLevels, 1, static_cast<int>(MAX_BLOOM_PYRAMID_LEVELS));

        if (m_Enabled && !m_AvailabilityChecked)
        {
            InitializeCuda();
        }
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
    m_Cuda = std::make_unique<CudaDriver>();
    CudaDriver& cuda = *m_Cuda;

    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        m_Status = cuda.GetError(result);
        cuda.Destroy();
        return false;
    }

    CUdevice device = 0;
    std::string deviceError;
    if (!CudaDriver::SelectDeviceForD3D12Adapter(device, deviceError))
    {
        m_Status = deviceError;
        cuda.Destroy();
        return false;
    }

    result = cuCtxCreate(&cuda.Context, nullptr, 0, device);
    if (result != CUDA_SUCCESS)
    {
        m_Status = cuda.GetError(result);
        cuda.Destroy();
        return false;
    }

    const std::string ptx = LoadTextFile(L"Shaders/CudaBloom.ptx");
    if (ptx.empty())
    {
        m_Status = "Shaders/CudaBloom.ptx was not found.";
        cuda.Destroy();
        return false;
    }

    result = cuModuleLoadData(&cuda.Module, ptx.c_str());
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA PTX load failed: " + cuda.GetError(result);
        cuda.Destroy();
        return false;
    }

    result = cuModuleGetFunction(&cuda.PrefilterDownsampleKernel, cuda.Module, "PrefilterDownsampleKernel");
    if (result == CUDA_SUCCESS)
    {
        result = cuModuleGetFunction(&cuda.DownsampleKernel, cuda.Module, "DownsampleKernel");
    }
    if (result == CUDA_SUCCESS)
    {
        result = cuModuleGetFunction(&cuda.UpsampleAddKernel, cuda.Module, "UpsampleAddKernel");
    }
    if (result == CUDA_SUCCESS)
    {
        result = cuModuleGetFunction(&cuda.CompositeBloomKernel, cuda.Module, "CompositeBloomKernel");
    }
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom kernel lookup failed: " + cuda.GetError(result);
        cuda.Destroy();
        return false;
    }

    m_CudaAvailable = true;
    m_Status = "CUDA bloom is available.";
    return true;
}

bool CudaBloomPass::EnsureD3D12InteropResources(Texture& source, Texture& destination, const uint32_t width, const uint32_t height)
{
    const D3D12_RESOURCE_DESC sourceDesc = source.GetD3D12ResourceDesc();
    const D3D12_RESOURCE_DESC destinationDesc = destination.GetD3D12ResourceDesc();
    if (sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || destinationDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        m_Status = "CUDA bloom expects R8G8B8A8_UNORM source and destination textures.";
        return false;
    }

    ID3D12Resource* sourceResource = source.GetD3D12Resource().Get();
    ID3D12Resource* destinationResource = destination.GetD3D12Resource().Get();
    if (m_Cuda != nullptr &&
        m_Cuda->InputTexture.TextureObject != 0 &&
        m_Cuda->OutputTexture.SurfaceObject != 0 &&
        m_SourceInteropResource == sourceResource &&
        m_DestinationInteropResource == destinationResource &&
        m_Width == width &&
        m_Height == height)
    {
        return true;
    }

    if (!InitializeCuda())
    {
        return false;
    }

    m_Width = width;
    m_Height = height;
    m_SourceInteropResource = nullptr;
    m_DestinationInteropResource = nullptr;
    m_Cuda->ReleaseExternalTextures();

    std::string interopError;
    if (!m_Cuda->ImportSharedTexture(sourceResource, width, height, true, false, m_Cuda->InputTexture, interopError))
    {
        m_Status = interopError;
        return false;
    }

    if (!m_Cuda->ImportSharedTexture(destinationResource, width, height, false, true, m_Cuda->OutputTexture, interopError))
    {
        m_Status = interopError;
        m_Cuda->ReleaseExternalTextures();
        return false;
    }

    const auto device = Application::Get().GetDevice();
    m_Cuda->Capacity = static_cast<size_t>(GetD3D12ResourceAllocationSize(device.Get(), sourceDesc));
    m_SourceInteropResource = sourceResource;
    m_DestinationInteropResource = destinationResource;
    return true;
}

bool CudaBloomPass::EnsureCudaPyramidBuffers(const uint32_t width, const uint32_t height, const uint32_t levelCount)
{
    if (!InitializeCuda())
    {
        return false;
    }

    CudaDriver& cuda = *m_Cuda;
    uint32_t levelWidth = HalfSize(width);
    uint32_t levelHeight = HalfSize(height);
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const size_t requiredBytes = static_cast<size_t>(levelWidth) * static_cast<size_t>(levelHeight) * sizeof(float) * 4u;
        if (cuda.PyramidCapacity[level] < requiredBytes)
        {
            if (cuda.Pyramid[level] != 0)
            {
                cuMemFree(cuda.Pyramid[level]);
                cuda.Pyramid[level] = 0;
            }

            CUresult result = cuMemAlloc(&cuda.Pyramid[level], requiredBytes);
            if (result != CUDA_SUCCESS)
            {
                m_Status = "CUDA bloom pyramid allocation failed: " + cuda.GetError(result);
                return false;
            }
            cuda.PyramidCapacity[level] = requiredBytes;
        }

        cuda.PyramidWidth[level] = levelWidth;
        cuda.PyramidHeight[level] = levelHeight;

        levelWidth = HalfSize(levelWidth);
        levelHeight = HalfSize(levelHeight);
    }
    return true;
}

bool CudaBloomPass::RunCudaBloom(const uint32_t width, const uint32_t height)
{
    const uint32_t levelCount = static_cast<uint32_t>(std::clamp(m_PyramidLevels, 1, static_cast<int>(MAX_BLOOM_PYRAMID_LEVELS)));
    if (!EnsureCudaPyramidBuffers(width, height, levelCount))
    {
        return false;
    }

    CudaDriver& cuda = *m_Cuda;
    constexpr uint32_t blockX = 16;
    constexpr uint32_t blockY = 16;

    CUtexObject input = cuda.InputTexture.TextureObject;
    CUsurfObject output = cuda.OutputTexture.SurfaceObject;
    uint32_t cudaWidth = width;
    uint32_t cudaHeight = height;
    CUdeviceptr level0 = cuda.Pyramid[0];
    uint32_t level0Width = cuda.PyramidWidth[0];
    uint32_t level0Height = cuda.PyramidHeight[0];
    uint32_t level0Pitch = level0Width;
    float threshold = m_Threshold;
    float softThreshold = m_SoftThreshold;
    float intensity = m_Intensity;

    void* prefilterArgs[] = {
        &input,
        &level0,
        &cudaWidth,
        &cudaHeight,
        &level0Width,
        &level0Height,
        &level0Pitch,
        &threshold,
        &softThreshold,
        &intensity,
    };
    CUresult result = cuLaunchKernel(
        cuda.PrefilterDownsampleKernel,
        DivideRoundUp(level0Width, blockX),
        DivideRoundUp(level0Height, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        nullptr,
        prefilterArgs,
        nullptr);
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom prefilter launch failed: " + cuda.GetError(result);
        return false;
    }

    for (uint32_t level = 1; level < levelCount; ++level)
    {
        CUdeviceptr previous = cuda.Pyramid[level - 1u];
        CUdeviceptr current = cuda.Pyramid[level];
        uint32_t previousWidth = cuda.PyramidWidth[level - 1u];
        uint32_t previousHeight = cuda.PyramidHeight[level - 1u];
        uint32_t previousPitch = previousWidth;
        uint32_t currentWidth = cuda.PyramidWidth[level];
        uint32_t currentHeight = cuda.PyramidHeight[level];
        uint32_t currentPitch = currentWidth;
        void* downsampleArgs[] = {
            &previous,
            &current,
            &previousWidth,
            &previousHeight,
            &previousPitch,
            &currentWidth,
            &currentHeight,
            &currentPitch,
        };
        result = cuLaunchKernel(
            cuda.DownsampleKernel,
            DivideRoundUp(currentWidth, blockX),
            DivideRoundUp(currentHeight, blockY),
            1u,
            blockX,
            blockY,
            1u,
            0u,
            nullptr,
            downsampleArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom downsample launch failed: " + cuda.GetError(result);
            return false;
        }
    }

    for (uint32_t level = levelCount - 1u; level > 0u; --level)
    {
        CUdeviceptr low = cuda.Pyramid[level];
        CUdeviceptr high = cuda.Pyramid[level - 1u];
        uint32_t lowWidth = cuda.PyramidWidth[level];
        uint32_t lowHeight = cuda.PyramidHeight[level];
        uint32_t lowPitch = lowWidth;
        uint32_t highWidth = cuda.PyramidWidth[level - 1u];
        uint32_t highHeight = cuda.PyramidHeight[level - 1u];
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
            cuda.UpsampleAddKernel,
            DivideRoundUp(highWidth, blockX),
            DivideRoundUp(highHeight, blockY),
            1u,
            blockX,
            blockY,
            1u,
            0u,
            nullptr,
            upsampleArgs,
            nullptr);
        if (result != CUDA_SUCCESS)
        {
            m_Status = "CUDA bloom upsample launch failed: " + cuda.GetError(result);
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
        cuda.CompositeBloomKernel,
        DivideRoundUp(width, blockX),
        DivideRoundUp(height, blockY),
        1u,
        blockX,
        blockY,
        1u,
        0u,
        nullptr,
        compositeArgs,
        nullptr);
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom composite launch failed: " + cuda.GetError(result);
        return false;
    }

    result = cuCtxSynchronize();
    if (result != CUDA_SUCCESS)
    {
        m_Status = "CUDA bloom sync failed: " + cuda.GetError(result);
        return false;
    }

    return true;
}

bool CudaBloomPass::Execute(Texture& source, Texture& destination, const uint32_t width, const uint32_t height)
{
    if (!m_Enabled)
    {
        return false;
    }

    if (!InitializeCuda())
    {
        return false;
    }

    if (!EnsureD3D12InteropResources(source, destination, width, height))
    {
        return false;
    }

    if (!RunCudaBloom(width, height))
    {
        return false;
    }

    m_Status = "CUDA bloom executed with shared D3D12 textures.";
    return true;
}
