//Modify Begin:2026-08-25 by Hui
#pragma once

#include <RenderGraph/ResourceId.h>

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

class CommandList;
class CommandQueue;
class ComputeShader;
class FrameworkDeviceContext;
class GpuReadbackTexture;
class Texture;

namespace RenderGraph
{
    class RenderGraphBuilder;
}

class OIDNDenoiser final
{
public:
    struct GraphInputs
    {
        RenderGraph::ResourceId ReadbackSource = 0u;
        RenderGraph::ResourceId Output = 0u;
        RenderGraph::ResourceId InputToken = 0u;
        RenderGraph::ResourceId OutputToken = 0u;
        uint32_t Width = 1u;
        uint32_t Height = 1u;
        std::wstring DiagnosticNamePrefix = L"Framework.OIDN";
    };

    explicit OIDNDenoiser(FrameworkDeviceContext& deviceContext);
    ~OIDNDenoiser();

    OIDNDenoiser(const OIDNDenoiser&) = delete;
    OIDNDenoiser& operator=(const OIDNDenoiser&) = delete;

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_Enabled; }
    void OnResourcesRecreated(uint32_t width, uint32_t height);
    void ResetHistory();

    bool BeginReadback(bool accumulationEnabled, uint32_t accumulationFrameIndex, uint32_t staticSpp);
    bool RecordReadback(CommandList& commandList, const std::shared_ptr<Texture>& source);
    void EndReadback(uint64_t submittedFenceValue);
    void CancelReadback();
    void Poll(CommandQueue& directQueue);
    bool HasUploadedResult() const { return m_HasUploadedResult.load(std::memory_order_acquire); }
    uint64_t GetGeneration() const { return m_Generation; }

    void AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs);

private:
    struct Job
    {
        uint64_t Generation = 0u;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        uint32_t Spp = 0u;
        std::vector<float> Pixels;
    };

    struct CompositeConstants
    {
        uint32_t Width = 1u;
        uint32_t Height = 1u;
        uint32_t ResultValid = 0u;
        uint32_t Padding0 = 0u;
    };

    void WorkerLoop(std::stop_token stopToken);
    static void ExecuteDenoise(Job& job);
    void InvalidateGeneration(bool resetReadbackResources);
    void RecordUpload(CommandList& commandList);
    void RecordComposite(CommandList& commandList, const std::shared_ptr<Texture>& output);

    FrameworkDeviceContext& m_DeviceContext;
    std::unique_ptr<ComputeShader> m_CompositeShader;
    std::unique_ptr<GpuReadbackTexture> m_Readback;
    std::shared_ptr<Texture> m_Output;
    std::jthread m_Worker;
    std::mutex m_WorkMutex;
    std::condition_variable m_WorkCondition;
    std::optional<Job> m_PendingJob;
    std::optional<Job> m_CompletedJob;
    bool m_WorkerBusy = false;
    bool m_ShuttingDown = false;
    bool m_Enabled = false;
    bool m_ReadbackQueued = false;
    bool m_ReadbackRecorded = false;
    bool m_ReadbackPending = false;
    std::atomic_bool m_HasUploadedResult = false;
    uint64_t m_Generation = 1u;
    uint64_t m_ReadbackGeneration = 0u;
    uint32_t m_ReadbackSpp = 0u;
    uint32_t m_Width = 0u;
    uint32_t m_Height = 0u;
};
//Modify End
