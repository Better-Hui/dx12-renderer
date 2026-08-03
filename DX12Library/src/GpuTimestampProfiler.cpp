#include "DX12LibPCH.h"

#include "GpuTimestampProfiler.h"

#include "Application.h"
#include "CommandList.h"
#include "CommandQueue.h"
#include "Helpers.h"

#include "d3dx12.h"

#include <algorithm>
#include <cstring>

//Modify Begin:2026-07-29 by BestHui
bool GpuTimestampProfiler::Initialize(const uint32_t maxTimestampCount)
{
    Shutdown();

    if (maxTimestampCount == 0)
    {
        return false;
    }

    const auto device = Application::Get().GetDevice();
    const auto directQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (directQueue == nullptr || FAILED(directQueue->GetD3D12CommandQueue()->GetTimestampFrequency(&m_TimestampFrequency)) ||
        m_TimestampFrequency == 0)
    {
        return false;
    }

    m_MaxTimestampCount = maxTimestampCount;
    m_FrameSlots.resize(FrameSlotCount);

    for (FrameSlot& slot : m_FrameSlots)
    {
        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = maxTimestampCount;
        queryHeapDesc.NodeMask = 0;
        ThrowIfFailed(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&slot.QueryHeap)));

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint64_t) * maxTimestampCount);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&slot.ReadbackBuffer)));
        slot.Names.reserve(maxTimestampCount);
    }

    m_Initialized = true;
    return true;
}

void GpuTimestampProfiler::Shutdown()
{
    m_FrameSlots.clear();
    m_MaxTimestampCount = 0;
    m_CurrentSlotIndex = 0;
    m_CurrentFrameNumber = 0;
    m_TimestampFrequency = 0;
//Modify Begin:2026-08-02 by BestHui
    m_CpuFrameStart = {};
//Modify End
    m_Initialized = false;
    m_LastFrameGpuMilliseconds = 0.0;
}

void GpuTimestampProfiler::BeginFrame(const uint64_t frameNumber)
{
    if (!m_Initialized)
    {
        return;
    }

    m_CurrentFrameNumber = frameNumber;
    m_CurrentSlotIndex = static_cast<uint32_t>(frameNumber % FrameSlotCount);

    FrameSlot& slot = GetCurrentSlot();
    slot.Names.clear();
    slot.TimestampCount = 0;
    slot.FrameNumber = frameNumber;
    slot.SubmittedFenceValue = 0;
    slot.PendingReadback = false;
//Modify Begin:2026-08-02 by BestHui
    m_CpuFrameStart = std::chrono::steady_clock::now();
    slot.CpuMilliseconds.clear();
//Modify End
}

void GpuTimestampProfiler::WriteTimestamp(CommandList& commandList, const char* name)
{
    if (!m_Initialized)
    {
        return;
    }

    FrameSlot& slot = GetCurrentSlot();
    if (slot.TimestampCount >= m_MaxTimestampCount)
    {
        return;
    }

    const uint32_t queryIndex = slot.TimestampCount++;
    slot.Names.emplace_back(name != nullptr ? name : "");
//Modify Begin:2026-08-02 by BestHui
    slot.CpuMilliseconds.push_back(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_CpuFrameStart).count());
//Modify End
    commandList.GetGraphicsCommandList()->EndQuery(slot.QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}

void GpuTimestampProfiler::ResolveFrame(CommandList& commandList)
{
    if (!m_Initialized)
    {
        return;
    }

    FrameSlot& slot = GetCurrentSlot();
    if (slot.TimestampCount == 0)
    {
        return;
    }

    commandList.GetGraphicsCommandList()->ResolveQueryData(
        slot.QueryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        0,
        slot.TimestampCount,
        slot.ReadbackBuffer.Get(),
        0);
    slot.PendingReadback = true;
}

void GpuTimestampProfiler::EndFrame(const uint64_t submittedFenceValue)
{
    if (!m_Initialized)
    {
        return;
    }

    FrameSlot& slot = GetCurrentSlot();
    slot.SubmittedFenceValue = submittedFenceValue;
}

bool GpuTimestampProfiler::CollectCompletedFrame(CommandQueue& commandQueue, std::vector<GpuTimestampSample>& samples)
{
    samples.clear();
    if (!m_Initialized)
    {
        return false;
    }

    for (FrameSlot& slot : m_FrameSlots)
    {
        if (!slot.PendingReadback || slot.SubmittedFenceValue == 0 || !commandQueue.IsFenceComplete(slot.SubmittedFenceValue))
        {
            continue;
        }

        const uint64_t* mappedTimestamps = nullptr;
        D3D12_RANGE readRange = { 0, sizeof(uint64_t) * slot.TimestampCount };
        ThrowIfFailed(slot.ReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(const_cast<uint64_t**>(&mappedTimestamps))));

        if (slot.TimestampCount >= 2)
        {
            const uint64_t frameStart = mappedTimestamps[0];
            uint64_t previous = frameStart;
            samples.reserve(slot.TimestampCount);
            for (uint32_t i = 0; i < slot.TimestampCount; ++i)
            {
                const uint64_t timestamp = mappedTimestamps[i];
                GpuTimestampSample sample;
                sample.Name = i < slot.Names.size() ? slot.Names[i] : "";
                sample.MillisecondsFromFrameStart =
                    static_cast<double>(timestamp - frameStart) * 1000.0 / static_cast<double>(m_TimestampFrequency);
                sample.MillisecondsFromPrevious =
                    i == 0 ? 0.0 : static_cast<double>(timestamp - previous) * 1000.0 / static_cast<double>(m_TimestampFrequency);
//Modify Begin:2026-08-02 by BestHui
                sample.CpuMillisecondsFromFrameStart =
                    i < slot.CpuMilliseconds.size() ? slot.CpuMilliseconds[i] : 0.0;
                sample.CpuMillisecondsFromPrevious =
                    i > 0 && i < slot.CpuMilliseconds.size()
                        ? slot.CpuMilliseconds[i] - slot.CpuMilliseconds[i - 1]
                        : 0.0;
//Modify End
                previous = timestamp;
                samples.push_back(sample);
            }
            m_LastFrameGpuMilliseconds = samples.back().MillisecondsFromFrameStart;
        }

//Modify Begin:2026-08-03 by BestHui
        m_LastCollectedFrameNumber = slot.FrameNumber;
//Modify End
        D3D12_RANGE writeRange = { 0, 0 };
        slot.ReadbackBuffer->Unmap(0, &writeRange);
        slot.PendingReadback = false;
        return !samples.empty();
    }

    return false;
}

uint32_t GpuTimestampProfiler::GetCurrentTimestampCount() const
{
    return m_Initialized ? GetCurrentSlot().TimestampCount : 0;
}

GpuTimestampProfiler::FrameSlot& GpuTimestampProfiler::GetCurrentSlot()
{
    Assert(m_CurrentSlotIndex < m_FrameSlots.size(), "GPU timestamp frame slot is invalid.");
    return m_FrameSlots[m_CurrentSlotIndex];
}

const GpuTimestampProfiler::FrameSlot& GpuTimestampProfiler::GetCurrentSlot() const
{
    Assert(m_CurrentSlotIndex < m_FrameSlots.size(), "GPU timestamp frame slot is invalid.");
    return m_FrameSlots[m_CurrentSlotIndex];
}
//Modify End
