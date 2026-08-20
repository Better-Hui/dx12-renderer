//Modify Begin:2026-08-20 by Hui
#include <Framework/Rendering/Lighting/ActivePixelListController.h>

#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>

#include <cstddef>

static_assert(
    sizeof(ActivePixelDispatchDiagnostics) == sizeof(uint32_t) + sizeof(D3D12_DISPATCH_ARGUMENTS),
    "Active-pixel diagnostics must contain one count followed by one compute dispatch argument block.");

ActivePixelListController::ActivePixelListController(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
{
}

void ActivePixelListController::Reset()
{
    m_CompactionShader.reset();
    m_DispatchFinalizeShader.reset();
    m_ComputeIndirectSignature.reset();
    m_CountReadback.Reset();
    m_CountReadbackStatus = ActivePixelReadbackStatus::NotQueued;
    m_CountReadbackQueued = false;
    m_CountReadbackRecorded = false;
    m_CountReadbackPending = false;
    m_LatestDiagnostics.reset();
    m_ShaderVariants.Clear();
}

void ActivePixelListController::EnsurePipelines()
{
    if (m_CompactionShader != nullptr)
    {
        return;
    }

    const auto createShader = [this](
        const std::wstring& compiledFileName,
        const std::wstring& sourceFileName,
        const char* debugName)
    {
        ShaderVariantDesc shaderDesc;
        shaderDesc.CompiledFileName = compiledFileName;
        shaderDesc.SourceFileName = sourceFileName;
        shaderDesc.TargetProfile = ShaderTargetProfile::Compute();
        shaderDesc.DebugName = debugName;
        const std::shared_ptr<ShaderBlob> shaderBlob = m_ShaderVariants.GetOrCompile(shaderDesc);
        return std::make_unique<ComputeShader>(
            m_DeviceContext,
            *shaderBlob,
            ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob).Build());
    };

    m_CompactionShader = createShader(
        L"Framework.ActivePixelCompaction.cs.cso",
        L"Framework/shaders/Lighting/ActivePixelCompaction.cs.hlsl",
        "Framework Active Pixel Compaction");
    m_DispatchFinalizeShader = createShader(
        L"Framework.ActivePixelDispatchFinalize.cs.cso",
        L"Framework/shaders/Lighting/ActivePixelDispatchFinalize.cs.hlsl",
        "Framework Active Pixel Dispatch Finalize");

    constexpr std::array computeArguments = {
        IndirectArgumentDesc{ .Type = IndirectArgumentType::Dispatch },
    };
    m_ComputeIndirectSignature = std::make_unique<IndirectCommandSignature>(
        m_DeviceContext,
        IndirectCommandSignatureDesc{
            .Arguments = computeArguments,
            .ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS),
        });
    m_CountReadback.Initialize(m_DeviceContext.GetDevice(), sizeof(ActivePixelDispatchDiagnostics));
}

ComputeShader& ActivePixelListController::GetCompactionShader() const
{
    Assert(m_CompactionShader != nullptr, "Active-pixel compaction shader is unavailable.");
    return *m_CompactionShader;
}

ComputeShader& ActivePixelListController::GetDispatchFinalizeShader() const
{
    Assert(m_DispatchFinalizeShader != nullptr, "Active-pixel dispatch-finalize shader is unavailable.");
    return *m_DispatchFinalizeShader;
}

const IndirectCommandSignature& ActivePixelListController::GetComputeDispatchSignature() const
{
    Assert(m_ComputeIndirectSignature != nullptr, "Active-pixel compute indirect command signature is unavailable.");
    return *m_ComputeIndirectSignature;
}

ActivePixelDispatch ActivePixelListController::GetComputeDispatch(
    const StructuredBuffer& indices,
    const ByteAddressBuffer& count,
    ByteAddressBuffer& dispatchData) const
{
    Assert(
        m_ComputeIndirectSignature != nullptr,
        "Active-pixel compute indirect dispatch signature is unavailable.");
    Assert(
        dispatchData.GetD3D12ResourceDesc().Width >= sizeof(ActivePixelDispatchDiagnostics),
        "Active-pixel compute dispatch data buffer is too small.");
    return {
        .Pixels = {
            .Indices = &indices,
            .Count = &count,
        },
        .Signature = &GetComputeDispatchSignature(),
        .Arguments = &dispatchData,
        .ArgumentBufferOffset = sizeof(uint32_t),
    };
}

bool ActivePixelListController::BeginCountReadback()
{
    if (!m_CountReadback.IsInitialized())
    {
        m_CountReadbackStatus = ActivePixelReadbackStatus::NotQueued;
        return false;
    }

    if (m_CountReadbackPending)
    {
        return false;
    }

    m_CountReadbackQueued = m_CountReadback.BeginCopy();
    m_CountReadbackRecorded = false;
    if (!m_CountReadbackQueued)
    {
        m_CountReadbackStatus = ActivePixelReadbackStatus::NotQueued;
    }
    else if (!m_LatestDiagnostics.has_value())
    {
        m_CountReadbackStatus = ActivePixelReadbackStatus::NotCompleted;
    }
    return m_CountReadbackQueued;
}

bool ActivePixelListController::RecordCountReadback(CommandList& commandList, const Resource& diagnostics)
{
    if (!m_CountReadbackQueued)
    {
        return false;
    }

    m_CountReadbackRecorded = m_CountReadback.RecordCopy(commandList, diagnostics);
    return m_CountReadbackRecorded;
}

void ActivePixelListController::EndCountReadback(const uint64_t submittedFenceValue)
{
    if (!m_CountReadbackQueued)
    {
        return;
    }

    if (m_CountReadbackRecorded)
    {
        m_CountReadback.EndCopy(submittedFenceValue);
        m_CountReadbackPending = true;
    }
    else
    {
        m_CountReadback.CancelCopy();
        m_CountReadbackStatus = ActivePixelReadbackStatus::NotQueued;
    }
    m_CountReadbackQueued = false;
    m_CountReadbackRecorded = false;
}

void ActivePixelListController::CancelCountReadback()
{
    if (m_CountReadback.IsInitialized())
    {
        m_CountReadback.CancelCopy();
    }
    m_CountReadbackQueued = false;
    m_CountReadbackRecorded = false;
    m_CountReadbackPending = false;
    m_CountReadbackStatus = m_LatestDiagnostics.has_value()
        ? ActivePixelReadbackStatus::Completed
        : ActivePixelReadbackStatus::NotQueued;
}

void ActivePixelListController::CollectCountReadback(CommandQueue& commandQueue)
{
    if (!m_CountReadback.IsInitialized())
    {
        return;
    }

    ActivePixelDispatchDiagnostics diagnostics = {};
    const std::span<std::byte> destination = std::as_writable_bytes(std::span{ &diagnostics, 1u });
    if (m_CountReadback.CollectLatestCompleted(commandQueue, destination))
    {
        m_LatestDiagnostics = diagnostics;
        m_CountReadbackPending = false;
        m_CountReadbackStatus = ActivePixelReadbackStatus::Completed;
    }
}
//Modify End
