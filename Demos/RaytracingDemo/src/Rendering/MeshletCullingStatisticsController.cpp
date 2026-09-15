//Modify Begin:2026-09-15 by Hui
#include <Rendering/MeshletCullingStatisticsController.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <cstddef>
#include <span>

MeshletCullingStatisticsController::MeshletCullingStatisticsController(
    FrameworkDeviceContext& deviceContext)
{
    m_VisibleDrawReadback.Initialize(deviceContext.GetDevice(), sizeof(uint32_t));
    m_VisibleMeshletReadback.Initialize(deviceContext.GetDevice(), sizeof(uint32_t));
}

bool MeshletCullingStatisticsController::BeginReadback()
{
    if (m_ReadbackPending || m_ReadbackQueued)
    {
        return false;
    }

    if (!m_VisibleDrawReadback.BeginCopy())
    {
        return false;
    }
    if (!m_VisibleMeshletReadback.BeginCopy())
    {
        m_VisibleDrawReadback.CancelCopy();
        return false;
    }

    m_ReadbackQueued = true;
    m_DrawCopyRecorded = false;
    m_MeshletCopyRecorded = false;
    m_CompletedDrawCount.reset();
    m_CompletedMeshletCount.reset();
    return true;
}

void MeshletCullingStatisticsController::RecordReadback(
    CommandList& commandList,
    const Resource& visibleDrawCountSource,
    const Resource& visibleMeshletCounter)
{
    if (!m_ReadbackQueued)
    {
        return;
    }

    m_DrawCopyRecorded = m_VisibleDrawReadback.RecordCopy(commandList, visibleDrawCountSource);
    m_MeshletCopyRecorded = m_VisibleMeshletReadback.RecordCopy(commandList, visibleMeshletCounter);
    Assert(
        m_DrawCopyRecorded && m_MeshletCopyRecorded,
        "Meshlet visibility counter readback must record both counters together.");
}

void MeshletCullingStatisticsController::EndReadback(const uint64_t submittedFenceValue)
{
    if (!m_ReadbackQueued)
    {
        return;
    }

    if (m_DrawCopyRecorded)
    {
        m_VisibleDrawReadback.EndCopy(submittedFenceValue);
    }
    else
    {
        m_VisibleDrawReadback.CancelCopy();
    }
    if (m_MeshletCopyRecorded)
    {
        m_VisibleMeshletReadback.EndCopy(submittedFenceValue);
    }
    else
    {
        m_VisibleMeshletReadback.CancelCopy();
    }

    m_ReadbackPending = m_DrawCopyRecorded || m_MeshletCopyRecorded;
    m_ReadbackQueued = false;
    m_DrawCopyRecorded = false;
    m_MeshletCopyRecorded = false;
}

void MeshletCullingStatisticsController::CancelReadback()
{
    if (m_ReadbackQueued)
    {
        m_VisibleDrawReadback.CancelCopy();
        m_VisibleMeshletReadback.CancelCopy();
    }
    m_ReadbackQueued = false;
    m_DrawCopyRecorded = false;
    m_MeshletCopyRecorded = false;
}

void MeshletCullingStatisticsController::CollectLatestCompleted(CommandQueue& commandQueue)
{
    if (!m_ReadbackPending)
    {
        return;
    }

    uint32_t visibleDrawCount = 0u;
    if (m_VisibleDrawReadback.CollectLatestCompleted(
        commandQueue,
        std::as_writable_bytes(std::span{ &visibleDrawCount, 1u })))
    {
        m_CompletedDrawCount = visibleDrawCount;
    }

    uint32_t visibleMeshletCount = 0u;
    if (m_VisibleMeshletReadback.CollectLatestCompleted(
        commandQueue,
        std::as_writable_bytes(std::span{ &visibleMeshletCount, 1u })))
    {
        m_CompletedMeshletCount = visibleMeshletCount;
    }

    if (m_CompletedDrawCount.has_value() && m_CompletedMeshletCount.has_value())
    {
        m_LatestStatistics = MeshletCullingStatistics{
            .VisibleDrawCount = m_CompletedDrawCount.value(),
            .VisibleMeshletCount = m_CompletedMeshletCount.value(),
        };
        m_ReadbackPending = false;
    }
}
//Modify End
