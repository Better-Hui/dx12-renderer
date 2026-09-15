//Modify Begin:2026-09-15 by Hui
#pragma once

#include <DX12Library/GpuReadbackBuffer.h>

#include <cstdint>
#include <optional>

class CommandList;
class CommandQueue;
class FrameworkDeviceContext;
class Resource;

struct MeshletCullingStatistics
{
    uint32_t VisibleDrawCount = 0u;
    uint32_t VisibleMeshletCount = 0u;
};

class MeshletCullingStatisticsController final
{
public:
    explicit MeshletCullingStatisticsController(FrameworkDeviceContext& deviceContext);

    MeshletCullingStatisticsController(const MeshletCullingStatisticsController&) = delete;
    MeshletCullingStatisticsController& operator=(const MeshletCullingStatisticsController&) = delete;

    [[nodiscard]] bool BeginReadback();
    void RecordReadback(
        CommandList& commandList,
        const Resource& visibleDrawCountSource,
        const Resource& visibleMeshletCounter);
    void EndReadback(uint64_t submittedFenceValue);
    void CancelReadback();
    void CollectLatestCompleted(CommandQueue& commandQueue);

    [[nodiscard]] std::optional<MeshletCullingStatistics> GetLatestStatistics() const
    {
        return m_LatestStatistics;
    }

private:
    GpuReadbackBuffer m_VisibleDrawReadback;
    GpuReadbackBuffer m_VisibleMeshletReadback;
    bool m_ReadbackQueued = false;
    bool m_DrawCopyRecorded = false;
    bool m_MeshletCopyRecorded = false;
    bool m_ReadbackPending = false;
    std::optional<uint32_t> m_CompletedDrawCount;
    std::optional<uint32_t> m_CompletedMeshletCount;
    std::optional<MeshletCullingStatistics> m_LatestStatistics;
};
//Modify End
