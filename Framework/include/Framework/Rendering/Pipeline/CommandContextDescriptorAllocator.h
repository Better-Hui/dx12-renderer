#pragma once

//Modify Begin:2026-07-30 by Hui

#include <array>
#include <cstdint>

#include <d3d12.h>

class CommandList;
class BindlessDescriptorHeap;
struct PipelineDescriptorTableAllocation;

enum class PipelineBindPoint;

class CommandContextDescriptorAllocator final
{
public:
    static constexpr uint32_t MaxRootDescriptorTables = 32;
    static constexpr uint32_t MaxPipelineBindPoints = 3;

    void ResetTransientBindings();
    //Modify Begin:2026-10-02 by Hui
    void ResetTransientBindings(PipelineBindPoint bindPoint);
    //Modify End
    void SetBindlessDescriptorHeap(BindlessDescriptorHeap* bindlessDescriptorHeap);
    bool HasBindlessDescriptorHeap() const { return m_BindlessDescriptorHeap != nullptr; }
    void StageDescriptorTable(
        CommandList& commandList,
        PipelineBindPoint bindPoint,
        uint32_t rootParameterIndex,
        const PipelineDescriptorTableAllocation& allocation);

private:
    struct BoundTable
    {
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle = {};
        uint32_t NumHandles = 0;
        uint64_t Revision = 0;
        bool Valid = false;
    };

    //Modify Begin:2026-10-02 by Hui
    std::array<std::array<BoundTable, MaxRootDescriptorTables>, MaxPipelineBindPoints> m_BoundTables = {};
    //Modify End
    BindlessDescriptorHeap* m_BindlessDescriptorHeap = nullptr;
};

//Modify End
