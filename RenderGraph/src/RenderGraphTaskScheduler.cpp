//Modify Begin:2026-08-07 by BestHui
#include "RenderGraphTaskScheduler.h"

namespace RenderGraph
{
    RenderGraphTaskScheduler::RenderGraphTaskScheduler(const uint32_t workerCount)
    {
        const uint32_t resolvedWorkerCount = ResolveWorkerCount(workerCount);
        m_Workers.reserve(resolvedWorkerCount);
        for (uint32_t workerIndex = 0u; workerIndex < resolvedWorkerCount; ++workerIndex)
        {
            m_Workers.emplace_back([this](const std::stop_token stopToken) { WorkerLoop(stopToken); });
        }
    }

    RenderGraphTaskScheduler::~RenderGraphTaskScheduler()
    {
        {
            std::lock_guard lock(m_TaskMutex);
            m_Stopping = true;
        }
        for (std::jthread& worker : m_Workers)
        {
            worker.request_stop();
        }
        m_TaskAvailable.notify_all();
    }

    uint32_t RenderGraphTaskScheduler::ResolveWorkerCount(const uint32_t requestedWorkerCount)
    {
        if (requestedWorkerCount != 0u)
        {
            return requestedWorkerCount;
        }

        const uint32_t hardwareThreadCount = std::thread::hardware_concurrency();
        const uint32_t backgroundWorkerCount = hardwareThreadCount > 1u ? hardwareThreadCount - 1u : 1u;
        return (std::min)(backgroundWorkerCount, 8u);
    }

    void RenderGraphTaskScheduler::WorkerLoop(const std::stop_token stopToken)
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock lock(m_TaskMutex);
                m_TaskAvailable.wait(lock, stopToken, [this]() { return m_Stopping || !m_Tasks.empty(); });
                if (m_Tasks.empty())
                {
                    if (m_Stopping)
                    {
                        return;
                    }
                    continue;
                }

                task = std::move(m_Tasks.front());
                m_Tasks.pop_front();
            }
            task();
        }
    }
}
//Modify End
