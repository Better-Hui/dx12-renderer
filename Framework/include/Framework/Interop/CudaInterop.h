#pragma once

#include <cuda.h>
#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <vector>

class CudaContext final
{
public:
    bool InitializeForD3D12Device(ID3D12Device* d3d12Device, std::string& outError);
    void Shutdown();
    void Synchronize() const;

    bool IsInitialized() const { return m_Context != nullptr; }
    CUstream GetStream() const { return m_Stream; }

    bool LoadModuleFromFile(const std::wstring& path, CUmodule& outModule, std::string& outError) const;
    bool GetFunction(CUmodule module, const char* name, CUfunction& outFunction, std::string& outError) const;
    void UnloadModule(CUmodule& module) const;

    static std::string GetError(CUresult result);

private:
    static bool SelectDeviceForD3D12Adapter(ID3D12Device* d3d12Device, CUdevice& outDevice, std::string& outError);

    CUcontext m_Context = nullptr;
    CUstream m_Stream = nullptr;
};

class CudaDx12InteropTexture final
{
public:
    ~CudaDx12InteropTexture();

    bool Import(
        CudaContext& context,
        ID3D12Device* device,
        ID3D12Resource* resource,
        uint32_t width,
        uint32_t height,
        bool createTextureObject,
        bool createSurfaceObject,
        std::string& outError);

    void Release(const CudaContext* context = nullptr);

    ID3D12Resource* GetD3D12Resource() const { return m_D3D12Resource; }
    CUtexObject GetTextureObject() const { return m_TextureObject; }
    CUsurfObject GetSurfaceObject() const { return m_SurfaceObject; }
    bool IsImported() const { return m_Memory != nullptr; }

private:
    CUexternalMemory m_Memory = nullptr;
    CUmipmappedArray m_MipmappedArray = nullptr;
    CUarray m_Array = nullptr;
    CUtexObject m_TextureObject = 0;
    CUsurfObject m_SurfaceObject = 0;
    ID3D12Resource* m_D3D12Resource = nullptr;
};

class CudaDx12TimelineSemaphore final
{
public:
    ~CudaDx12TimelineSemaphore();

    bool Initialize(CudaContext& context, ID3D12Device* device, std::string& outError);
    void Shutdown(const CudaContext* context = nullptr);

    bool SignalD3D12AndWaitCuda(ID3D12CommandQueue* d3d12CommandQueue, CudaContext& context, std::string& outError);
    bool SignalCudaAndWaitInD3D12(ID3D12CommandQueue* d3d12CommandQueue, CudaContext& context, std::string& outError);

private:
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    CUexternalSemaphore m_Semaphore = nullptr;
    uint64_t m_TimelineValue = 0;
};

class CudaDeviceBufferPool final
{
public:
    ~CudaDeviceBufferPool();

    bool EnsureBuffer(size_t index, size_t bytes, std::string& outError);
    CUdeviceptr GetBuffer(size_t index) const;
    size_t GetCapacity(size_t index) const;
    void Release(const CudaContext* context = nullptr);

private:
    std::vector<CUdeviceptr> m_Buffers;
    std::vector<size_t> m_Capacities;
};
