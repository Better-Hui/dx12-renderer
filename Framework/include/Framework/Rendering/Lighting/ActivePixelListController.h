//Modify Begin:2026-08-20 by Hui
#pragma once

#include <Framework/Rendering/Lighting/ActivePixelList.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <DX12Library/GpuReadbackBuffer.h>

#include <memory>
#include <optional>

class CommandList;
class CommandQueue;
class ComputeShader;
class FrameworkDeviceContext;
class IndirectCommandSignature;
class Resource;

class ActivePixelListController final
{
public:
    explicit ActivePixelListController(FrameworkDeviceContext& deviceContext);

    ActivePixelListController(const ActivePixelListController&) = delete;
    ActivePixelListController& operator=(const ActivePixelListController&) = delete;

    void Reset();
    void EnsurePipelines();
    ComputeShader& GetCompactionShader() const;
    ComputeShader& GetDispatchFinalizeShader() const;
    const IndirectCommandSignature& GetComputeDispatchSignature() const;
    ActivePixelDispatch GetComputeDispatch(
        const StructuredBuffer& indices,
        const ByteAddressBuffer& count,
        ByteAddressBuffer& dispatchData) const;

    [[nodiscard]] bool BeginCountReadback();
    [[nodiscard]] bool RecordCountReadback(CommandList& commandList, const Resource& diagnostics);
    void EndCountReadback(uint64_t submittedFenceValue);
    void CancelCountReadback();
    void CollectCountReadback(CommandQueue& commandQueue);
    [[nodiscard]] ActivePixelReadbackStatus GetCountReadbackStatus() const { return m_CountReadbackStatus; }
    [[nodiscard]] std::optional<ActivePixelDispatchDiagnostics> GetLatestDiagnostics() const
    {
        return m_CountReadbackStatus == ActivePixelReadbackStatus::Completed
            ? m_LatestDiagnostics
            : std::nullopt;
    }

private:
    FrameworkDeviceContext& m_DeviceContext;
    ShaderVariantManager m_ShaderVariants;
    std::unique_ptr<ComputeShader> m_CompactionShader;
    std::unique_ptr<ComputeShader> m_DispatchFinalizeShader;
    std::unique_ptr<IndirectCommandSignature> m_ComputeIndirectSignature;
    GpuReadbackBuffer m_CountReadback;
    ActivePixelReadbackStatus m_CountReadbackStatus = ActivePixelReadbackStatus::NotQueued;
    bool m_CountReadbackQueued = false;
    bool m_CountReadbackRecorded = false;
    bool m_CountReadbackPending = false;
    std::optional<ActivePixelDispatchDiagnostics> m_LatestDiagnostics;
};
//Modify End
