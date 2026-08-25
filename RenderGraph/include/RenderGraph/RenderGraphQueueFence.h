//Modify Begin:2026-07-30 by Hui
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace RenderGraph
{
    enum class RenderPassQueue;

    struct RenderGraphQueueFenceValues
    {
        uint64_t Direct = 0;
        uint64_t AsyncCompute = 0;
        uint64_t Copy = 0;

        void Merge(const RenderGraphQueueFenceValues& other)
        {
            Direct = (std::max)(Direct, other.Direct);
            AsyncCompute = (std::max)(AsyncCompute, other.AsyncCompute);
            Copy = (std::max)(Copy, other.Copy);
        }

        bool IsEmpty() const
        {
            return Direct == 0 && AsyncCompute == 0 && Copy == 0;
        }
    };

    struct RenderGraphQueueSynchronizationStats
    {
        std::array<uint64_t, 3> SubmissionCounts = {};
        std::array<std::array<uint64_t, 3>, 3> WaitCounts = {};

        static size_t GetQueueIndex(RenderPassQueue queue);
        void RecordSubmission(RenderPassQueue queue);
        void RecordWait(RenderPassQueue producerQueue, RenderPassQueue consumerQueue);
        [[nodiscard]] uint64_t GetSubmissionCount(RenderPassQueue queue) const;
        [[nodiscard]] uint64_t GetWaitCount(
            RenderPassQueue producerQueue,
            RenderPassQueue consumerQueue) const;
    };
}
//Modify End
