//Modify Begin:2026-08-20 by Hui
#pragma once

#include <cstdint>

class ByteAddressBuffer;
class IndirectCommandSignature;
class Resource;
class StructuredBuffer;

struct ActivePixelList
{
    const StructuredBuffer* Indices = nullptr;
    const ByteAddressBuffer* Count = nullptr;

    [[nodiscard]] bool IsValid() const
    {
        return Indices != nullptr && Count != nullptr;
    }
};

struct ActivePixelCompactionConstants
{
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    uint32_t Padding0 = 0u;
    uint32_t Padding1 = 0u;
};

enum class ActivePixelReadbackStatus : uint32_t
{
    NotQueued,
    NotCompleted,
    Completed,
};

struct ActivePixelDispatchDiagnostics
{
    uint32_t ActivePixelCount = 0u;
    uint32_t DispatchX = 0u;
    uint32_t DispatchY = 0u;
    uint32_t DispatchZ = 0u;

    [[nodiscard]] bool HasConsistentDispatchArguments() const
    {
        return DispatchX == (ActivePixelCount + 63u) / 64u &&
            DispatchY == 1u &&
            DispatchZ == 1u;
    }
};

struct ActivePixelDispatch
{
    ActivePixelList Pixels = {};
    const IndirectCommandSignature* Signature = nullptr;
    Resource* Arguments = nullptr;
    uint64_t ArgumentBufferOffset = 0u;

    [[nodiscard]] bool IsValid() const
    {
        return Pixels.IsValid() && Signature != nullptr && Arguments != nullptr;
    }
};
//Modify End
