//Modify Begin:2026-08-19 by Hui
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

class ByteAddressBuffer;
class CommandList;
class FrameworkDeviceContext;
class Resource;

class IndirectCommandBuffer final
{
public:
    IndirectCommandBuffer(
        FrameworkDeviceContext& deviceContext,
        uint64_t capacityInBytes,
        std::wstring name);

    void Upload(CommandList& commandList, std::span<const std::byte> commandData);

    [[nodiscard]] Resource& GetResource();
    [[nodiscard]] const Resource& GetResource() const;
    [[nodiscard]] uint64_t GetCapacityInBytes() const { return m_CapacityInBytes; }

private:
    uint64_t m_CapacityInBytes = 0;
    std::unique_ptr<ByteAddressBuffer> m_Buffer;
};
//Modify End
