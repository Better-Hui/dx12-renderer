#include <Framework/CudaInterop.h>

#include <Windows.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    bool MatchesAdapterLuid(const char cudaLuid[8], const LUID& adapterLuid)
    {
        return std::memcmp(cudaLuid, &adapterLuid, sizeof(cudaLuid)) == 0;
    }

    uint64_t GetD3D12ResourceAllocationSize(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc)
    {
        return device->GetResourceAllocationInfo(0u, 1u, &desc).SizeInBytes;
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
}

std::string CudaContext::GetError(const CUresult result)
{
    const char* message = nullptr;
    if (cuGetErrorString(result, &message) == CUDA_SUCCESS && message != nullptr)
    {
        return message;
    }
    return "CUDA error " + std::to_string(result);
}

bool CudaContext::SelectDeviceForD3D12Adapter(ID3D12Device* d3d12Device, CUdevice& outDevice, std::string& outError)
{
    if (d3d12Device == nullptr)
    {
        outError = "D3D12 device is null.";
        return false;
    }

    const LUID adapterLuid = d3d12Device->GetAdapterLuid();

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

bool CudaContext::InitializeForD3D12Device(ID3D12Device* d3d12Device, std::string& outError)
{
    if (IsInitialized())
    {
        return true;
    }

    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS)
    {
        outError = GetError(result);
        return false;
    }

    CUdevice device = 0;
    if (!SelectDeviceForD3D12Adapter(d3d12Device, device, outError))
    {
        return false;
    }

    result = cuCtxCreate(&m_Context, nullptr, 0, device);
    if (result != CUDA_SUCCESS)
    {
        outError = GetError(result);
        Shutdown();
        return false;
    }

    result = cuStreamCreate(&m_Stream, CU_STREAM_NON_BLOCKING);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA stream creation failed: " + GetError(result);
        Shutdown();
        return false;
    }

    return true;
}

void CudaContext::Synchronize() const
{
    if (m_Stream != nullptr)
    {
        cuStreamSynchronize(m_Stream);
    }
}

void CudaContext::Shutdown()
{
    Synchronize();

    if (m_Stream != nullptr)
    {
        cuStreamDestroy(m_Stream);
        m_Stream = nullptr;
    }
    if (m_Context != nullptr)
    {
        cuCtxDestroy(m_Context);
        m_Context = nullptr;
    }
}

bool CudaContext::LoadModuleFromFile(const std::wstring& path, CUmodule& outModule, std::string& outError) const
{
    const std::string ptx = LoadTextFile(path);
    if (ptx.empty())
    {
        outError = "CUDA PTX file was not found.";
        return false;
    }

    CUresult result = cuModuleLoadData(&outModule, ptx.c_str());
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA PTX load failed: " + GetError(result);
        outModule = nullptr;
        return false;
    }

    return true;
}

bool CudaContext::GetFunction(CUmodule module, const char* name, CUfunction& outFunction, std::string& outError) const
{
    CUresult result = cuModuleGetFunction(&outFunction, module, name);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA kernel lookup failed: " + GetError(result);
        outFunction = nullptr;
        return false;
    }

    return true;
}

void CudaContext::UnloadModule(CUmodule& module) const
{
    if (module != nullptr)
    {
        cuModuleUnload(module);
        module = nullptr;
    }
}

CudaDx12InteropTexture::~CudaDx12InteropTexture()
{
    Release();
}

bool CudaDx12InteropTexture::Import(
    CudaContext& context,
    ID3D12Device* device,
    ID3D12Resource* resource,
    const uint32_t width,
    const uint32_t height,
    const bool createTextureObject,
    const bool createSurfaceObject,
    std::string& outError)
{
    if (resource == nullptr || device == nullptr)
    {
        outError = "Cannot import a null D3D12 texture into CUDA.";
        return false;
    }
    if (m_D3D12Resource == resource && IsImported())
    {
        return true;
    }

    Release(&context);

    HANDLE sharedHandle = nullptr;
    HRESULT sharedHandleResult = device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &sharedHandle);
    if (FAILED(sharedHandleResult))
    {
        outError = "D3D12 shared texture handle creation failed. The texture must be created with D3D12_HEAP_FLAG_SHARED.";
        return false;
    }

    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC memoryDesc = {};
    memoryDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
    memoryDesc.handle.win32.handle = sharedHandle;
    memoryDesc.size = GetD3D12ResourceAllocationSize(device, resourceDesc);
    memoryDesc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;

    CUresult result = cuImportExternalMemory(&m_Memory, &memoryDesc);
    CloseHandle(sharedHandle);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA failed to import shared D3D12 texture: " + CudaContext::GetError(result);
        return false;
    }

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipDesc = {};
    mipDesc.offset = 0;
    mipDesc.arrayDesc.Width = width;
    mipDesc.arrayDesc.Height = height;
    mipDesc.arrayDesc.Depth = 0;
    mipDesc.arrayDesc.Format = CU_AD_FORMAT_FLOAT;
    mipDesc.arrayDesc.NumChannels = 4;
    mipDesc.arrayDesc.Flags = (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0
        ? CUDA_ARRAY3D_COLOR_ATTACHMENT
        : 0u;
    if (createSurfaceObject)
    {
        mipDesc.arrayDesc.Flags |= CUDA_ARRAY3D_SURFACE_LDST;
    }
    mipDesc.numLevels = 1;

    result = cuExternalMemoryGetMappedMipmappedArray(&m_MipmappedArray, m_Memory, &mipDesc);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA failed to map shared D3D12 texture as mipmapped array: " + CudaContext::GetError(result);
        Release(&context);
        return false;
    }

    result = cuMipmappedArrayGetLevel(&m_Array, m_MipmappedArray, 0u);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA failed to get shared texture mip level: " + CudaContext::GetError(result);
        Release(&context);
        return false;
    }

    CUDA_RESOURCE_DESC cudaResourceDesc = {};
    cudaResourceDesc.resType = CU_RESOURCE_TYPE_ARRAY;
    cudaResourceDesc.res.array.hArray = m_Array;

    if (createTextureObject)
    {
        CUDA_TEXTURE_DESC textureDesc = {};
        textureDesc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
        textureDesc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
        textureDesc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
        textureDesc.filterMode = CU_TR_FILTER_MODE_POINT;
        textureDesc.flags = 0;
        result = cuTexObjectCreate(&m_TextureObject, &cudaResourceDesc, &textureDesc, nullptr);
        if (result != CUDA_SUCCESS)
        {
            outError = "CUDA failed to create texture object: " + CudaContext::GetError(result);
            Release(&context);
            return false;
        }
    }

    if (createSurfaceObject)
    {
        result = cuSurfObjectCreate(&m_SurfaceObject, &cudaResourceDesc);
        if (result != CUDA_SUCCESS)
        {
            outError = "CUDA failed to create surface object: " + CudaContext::GetError(result);
            Release(&context);
            return false;
        }
    }

    m_D3D12Resource = resource;
    return true;
}

void CudaDx12InteropTexture::Release(const CudaContext* context)
{
    if (context != nullptr)
    {
        context->Synchronize();
    }
    if (m_SurfaceObject != 0)
    {
        cuSurfObjectDestroy(m_SurfaceObject);
        m_SurfaceObject = 0;
    }
    if (m_TextureObject != 0)
    {
        cuTexObjectDestroy(m_TextureObject);
        m_TextureObject = 0;
    }
    if (m_MipmappedArray != nullptr)
    {
        cuMipmappedArrayDestroy(m_MipmappedArray);
        m_MipmappedArray = nullptr;
        m_Array = nullptr;
    }
    if (m_Memory != nullptr)
    {
        cuDestroyExternalMemory(m_Memory);
        m_Memory = nullptr;
    }
    m_D3D12Resource = nullptr;
}

CudaDx12TimelineSemaphore::~CudaDx12TimelineSemaphore()
{
    Shutdown();
}

bool CudaDx12TimelineSemaphore::Initialize(CudaContext& context, ID3D12Device* device, std::string& outError)
{
    if (m_Fence != nullptr && m_Semaphore != nullptr)
    {
        return true;
    }

    Shutdown(&context);

    HRESULT result = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_Fence));
    if (FAILED(result))
    {
        outError = "D3D12 shared fence creation failed.";
        return false;
    }

    HANDLE sharedFenceHandle = nullptr;
    result = device->CreateSharedHandle(m_Fence.Get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle);
    if (FAILED(result))
    {
        outError = "D3D12 shared fence handle creation failed.";
        m_Fence.Reset();
        return false;
    }

    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC semaphoreDesc = {};
    semaphoreDesc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
    semaphoreDesc.handle.win32.handle = sharedFenceHandle;

    CUresult cudaResult = cuImportExternalSemaphore(&m_Semaphore, &semaphoreDesc);
    CloseHandle(sharedFenceHandle);
    if (cudaResult != CUDA_SUCCESS)
    {
        outError = "CUDA failed to import D3D12 shared fence: " + CudaContext::GetError(cudaResult);
        m_Fence.Reset();
        return false;
    }

    m_TimelineValue = 0;
    return true;
}

void CudaDx12TimelineSemaphore::Shutdown(const CudaContext* context)
{
    if (context != nullptr)
    {
        context->Synchronize();
    }
    if (m_Semaphore != nullptr)
    {
        cuDestroyExternalSemaphore(m_Semaphore);
        m_Semaphore = nullptr;
    }
    m_Fence.Reset();
    m_TimelineValue = 0;
}

bool CudaDx12TimelineSemaphore::SignalD3D12AndWaitCuda(ID3D12CommandQueue* d3d12CommandQueue, CudaContext& context, std::string& outError)
{
    if (d3d12CommandQueue == nullptr)
    {
        outError = "D3D12 command queue is null.";
        return false;
    }

    const uint64_t d3d12ReadyValue = ++m_TimelineValue;
    HRESULT d3dResult = d3d12CommandQueue->Signal(m_Fence.Get(), d3d12ReadyValue);
    if (FAILED(d3dResult))
    {
        outError = "D3D12 queue failed to signal CUDA shared fence.";
        return false;
    }

    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS waitParams = {};
    waitParams.params.fence.value = d3d12ReadyValue;
    CUresult cudaResult = cuWaitExternalSemaphoresAsync(&m_Semaphore, &waitParams, 1, context.GetStream());
    if (cudaResult != CUDA_SUCCESS)
    {
        outError = "CUDA failed to wait for D3D12 shared fence: " + CudaContext::GetError(cudaResult);
        return false;
    }

    return true;
}

bool CudaDx12TimelineSemaphore::SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue, CudaContext& context, std::string& outError)
{
    if (d3d12CommandQueue == nullptr)
    {
        outError = "D3D12 command queue is null.";
        return false;
    }

    const uint64_t cudaFinishedValue = ++m_TimelineValue;
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signalParams = {};
    signalParams.params.fence.value = cudaFinishedValue;
    CUresult cudaResult = cuSignalExternalSemaphoresAsync(&m_Semaphore, &signalParams, 1, context.GetStream());
    if (cudaResult != CUDA_SUCCESS)
    {
        outError = "CUDA failed to signal D3D12 shared fence: " + CudaContext::GetError(cudaResult);
        context.Synchronize();
        return false;
    }

    HRESULT d3dResult = d3d12CommandQueue->Wait(m_Fence.Get(), cudaFinishedValue);
    if (FAILED(d3dResult))
    {
        outError = "D3D12 queue failed to wait for CUDA shared fence.";
        context.Synchronize();
        return false;
    }

    return true;
}

CudaDeviceBufferPool::~CudaDeviceBufferPool()
{
    Release();
}

bool CudaDeviceBufferPool::EnsureBuffer(const size_t index, const size_t bytes, std::string& outError)
{
    if (m_Buffers.size() <= index)
    {
        m_Buffers.resize(index + 1u, 0);
        m_Capacities.resize(index + 1u, 0);
    }

    if (m_Capacities[index] >= bytes)
    {
        return true;
    }

    if (m_Buffers[index] != 0)
    {
        cuMemFree(m_Buffers[index]);
        m_Buffers[index] = 0;
        m_Capacities[index] = 0;
    }

    CUresult result = cuMemAlloc(&m_Buffers[index], bytes);
    if (result != CUDA_SUCCESS)
    {
        outError = "CUDA buffer allocation failed: " + CudaContext::GetError(result);
        return false;
    }

    m_Capacities[index] = bytes;
    return true;
}

CUdeviceptr CudaDeviceBufferPool::GetBuffer(const size_t index) const
{
    return index < m_Buffers.size() ? m_Buffers[index] : 0;
}

size_t CudaDeviceBufferPool::GetCapacity(const size_t index) const
{
    return index < m_Capacities.size() ? m_Capacities[index] : 0;
}

void CudaDeviceBufferPool::Release(const CudaContext* context)
{
    if (context != nullptr)
    {
        context->Synchronize();
    }

    for (CUdeviceptr& buffer : m_Buffers)
    {
        if (buffer != 0)
        {
            cuMemFree(buffer);
            buffer = 0;
        }
    }

    m_Buffers.clear();
    m_Capacities.clear();
}
