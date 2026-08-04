#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Window.h>

//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

class SharedUploadBuffer
{
public:
    //Modify Begin:2026-07-30 by BestHui
    explicit SharedUploadBuffer(FrameworkDeviceContext& deviceContext)
        : m_DeviceContext(deviceContext)
    {
    }
    //Modify End
    // Resets the pointer of the current buffer.
    void BeginFrame(uint64_t frameIndex);

    template <typename T>
    void Upload(CommandList& commandList, Resource& destination, const std::vector<T>& data, const uint64_t destinationOffset = 0U)
    {
        Upload(commandList, destination, data.data(), data.size() * sizeof(T), sizeof(T), destinationOffset);
    }

    void Upload(CommandList& commandList, const Resource& destination, const void* pData, uint64_t sizeInBytes, uint64_t alignment, uint64_t destinationOffset = 0U);

private:
    static constexpr auto BUFFER_COUNT = Window::BUFFER_COUNT;
    static constexpr uint64_t CAPACITY_ALIGNMENT = 102400;



    struct BufferInfo
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> m_Buffer = nullptr;
        uint8_t* m_DataBegin = nullptr;
        uint8_t* m_DataCur = nullptr;
        uint8_t* m_DataEnd = nullptr;

        uint64_t GetCurrentUsedSize() const
        {
            if (m_Buffer == nullptr)
            {
                return 0;
            }
            return m_DataCur - m_DataBegin;
        }
    };

    BufferInfo& GetBufferInfoForFrame(uint64_t frameCount);

    uint8_t* SuballocateFromBuffer(BufferInfo& bufferInfo, uint64_t size, uint64_t alignment) const;

    void EnsureBufferCapacity(BufferInfo& bufferInfo, uint64_t capacity) const;

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(uint64_t capacity) const;

    //Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext& m_DeviceContext;
    uint64_t m_FrameIndex = 0;
    //Modify End
    BufferInfo m_BufferInfos[BUFFER_COUNT];
};
