//Modify Begin:2026-08-07 by Hui
#include "RenderGraphTaskScheduler.h"

#include <cstdlib>
#include <limits>

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

        //Modify Begin:2026-10-02 by Hui
        // Allow deployments to size the recording pool explicitly.  The
        // environment override is useful for reproducing contention without
        // rebuilding the renderer; zero keeps the hardware default below.
        const char* configuredWorkerCount = std::getenv("RENDERGRAPH_PARALLEL_WORKERS");
        if (configuredWorkerCount == nullptr)
        {
            configuredWorkerCount = std::getenv("RAYTRACING_DEMO_PARALLEL_WORKERS");
        }
        if (configuredWorkerCount != nullptr && configuredWorkerCount[0] != '\0')
        {
            char* parseEnd = nullptr;
            const unsigned long parsedWorkerCount = std::strtoul(configuredWorkerCount, &parseEnd, 10);
            if (parseEnd != configuredWorkerCount && *parseEnd == '\0' && parsedWorkerCount > 0ul)
            {
                return parsedWorkerCount > static_cast<unsigned long>((std::numeric_limits<uint32_t>::max)())
                    ? (std::numeric_limits<uint32_t>::max)()
                    : static_cast<uint32_t>(parsedWorkerCount);
            }
        }
        //Modify End

        const uint32_t hardwareThreadCount = std::thread::hardware_concurrency();
        const uint32_t backgroundWorkerCount = hardwareThreadCount > 1u ? hardwareThreadCount - 1u : 1u;
        return backgroundWorkerCount;
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
