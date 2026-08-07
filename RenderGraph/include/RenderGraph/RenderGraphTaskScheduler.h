#pragma once

//Modify Begin:2026-08-07 by BestHui
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace RenderGraph
{
    class RenderGraphTaskScheduler final
    {
    public:
        explicit RenderGraphTaskScheduler(uint32_t workerCount = 0u);
        ~RenderGraphTaskScheduler();

        RenderGraphTaskScheduler(const RenderGraphTaskScheduler&) = delete;
        RenderGraphTaskScheduler& operator=(const RenderGraphTaskScheduler&) = delete;

        template <typename FunctionT>
        auto Enqueue(FunctionT&& function) -> std::future<std::invoke_result_t<std::decay_t<FunctionT>&>>
        {
            using ResultT = std::invoke_result_t<std::decay_t<FunctionT>&>;

            auto task = std::make_shared<std::packaged_task<ResultT()>>(std::forward<FunctionT>(function));
            std::future<ResultT> result = task->get_future();
            {
                std::lock_guard lock(m_TaskMutex);
                m_Tasks.emplace_back([task]() { (*task)(); });
            }
            m_TaskAvailable.notify_one();
            return result;
        }

        uint32_t GetWorkerCount() const { return static_cast<uint32_t>(m_Workers.size()); }

    private:
        static uint32_t ResolveWorkerCount(uint32_t requestedWorkerCount);
        void WorkerLoop(std::stop_token stopToken);

        std::mutex m_TaskMutex;
        std::condition_variable_any m_TaskAvailable;
        std::deque<std::function<void()>> m_Tasks;
        std::vector<std::jthread> m_Workers;
    };
}
//Modify End
