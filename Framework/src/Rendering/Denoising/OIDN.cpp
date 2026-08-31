//Modify Begin:2026-08-25 by Hui
#include <Framework/Rendering/Denoising/OIDN.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/DiagnosticRenderScope.h>
#include <DX12Library/GpuReadbackTexture.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/ResourceUploader.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Diagnostics/RenderGraphAccessValidation.h>
#include <Framework/OIDNComposite_CS.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Texture/RenderTexture.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <OpenImageDenoise/oidn.hpp>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <Windows.h>
#include <d3dx12/d3dx12.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{
    class OidnSharedBuffer final : public Resource
    {
    public:
        OidnSharedBuffer(
            const uint64_t sizeInBytes,
            const std::wstring& name,
            std::shared_ptr<D3D12DeviceContext> deviceContext)
            : Resource(
                CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes),
                D3D12_HEAP_FLAG_SHARED,
                nullptr,
                name,
                std::move(deviceContext))
        {
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(
            const D3D12_SHADER_RESOURCE_VIEW_DESC*) const override
        {
            return {};
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(
            const D3D12_UNORDERED_ACCESS_VIEW_DESC*) const override
        {
            return {};
        }
    };

    void ThrowIfOidnError(oidn::DeviceRef& device, const char* operation)
    {
        const char* errorMessage = nullptr;
        if (device.getError(errorMessage) != oidn::Error::None)
        {
            throw std::runtime_error(
                std::string(operation) + ": " +
                (errorMessage != nullptr ? errorMessage : "unknown OIDN error."));
        }
    }

    oidn::BufferRef ImportOidnSharedBuffer(
        oidn::DeviceRef& device,
        ID3D12Device2& d3d12Device,
        const Resource& resource,
        const uint64_t allocationSize)
    {
        HANDLE sharedHandle = nullptr;
        ThrowIfFailed(d3d12Device.CreateSharedHandle(
            resource.GetD3D12Resource().Get(),
            nullptr,
            GENERIC_ALL,
            nullptr,
            &sharedHandle));
        const oidn::BufferRef buffer = device.newBuffer(
            oidn::ExternalMemoryTypeFlag::D3D12Resource | oidn::ExternalMemoryTypeFlag::Dedicated,
            sharedHandle,
            nullptr,
            allocationSize);
        CloseHandle(sharedHandle);
        ThrowIfOidnError(device, "OIDN CUDA shared D3D12 buffer import failed");
        if (!buffer)
        {
            throw std::runtime_error("OIDN CUDA shared D3D12 buffer import returned null.");
        }
        return buffer;
    }

    oidn::SemaphoreRef ImportOidnSharedFence(
        oidn::DeviceRef& device,
        ID3D12Device2& d3d12Device,
        ID3D12Fence& fence)
    {
        HANDLE sharedHandle = nullptr;
        ThrowIfFailed(d3d12Device.CreateSharedHandle(
            &fence,
            nullptr,
            GENERIC_ALL,
            nullptr,
            &sharedHandle));
        const oidn::SemaphoreRef semaphore = device.newSemaphore(
            oidn::ExternalSemaphoreTypeFlag::D3D12Fence,
            sharedHandle,
            nullptr);
        CloseHandle(sharedHandle);
        ThrowIfOidnError(device, "OIDN CUDA shared D3D12 fence import failed");
        if (!semaphore)
        {
            throw std::runtime_error("OIDN CUDA shared D3D12 fence import returned null.");
        }
        return semaphore;
    }

    struct OidnReadbackPassData
    {
        OIDNDenoiser* Feature = nullptr;
        std::shared_ptr<const OIDNDenoiser::GraphInputs> Inputs;
    };

    struct OidnUploadPassData
    {
        OIDNDenoiser* Feature = nullptr;
    };

    struct OidnCompositePassData
    {
        OIDNDenoiser* Feature = nullptr;
        std::shared_ptr<const OIDNDenoiser::GraphInputs> Inputs;
    };

    std::unique_ptr<ComputeShader> CreateOidnCompositeShader(FrameworkDeviceContext& deviceContext)
    {
        const ShaderBlob shader(ShaderBytecode_OIDNComposite_CS, sizeof ShaderBytecode_OIDNComposite_CS);
        return std::make_unique<ComputeShader>(
            deviceContext,
            shader,
            ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
    }
}

struct OIDNDenoiser::CudaResources
{
    std::shared_ptr<OidnSharedBuffer> Input;
    std::shared_ptr<OidnSharedBuffer> Output;
    Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
    oidn::DeviceRef Device;
    oidn::BufferRef InputBuffer;
    oidn::BufferRef OutputBuffer;
    oidn::SemaphoreRef FenceSemaphore;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint = {};
    uint32_t NumRows = 0u;
    uint64_t AllocationSize = 0u;
    uint64_t NextFenceValue = 0u;
};

OIDNDenoiser::OIDNDenoiser(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
    , m_CompositeShader(CreateOidnCompositeShader(deviceContext))
    , m_Readback(std::make_unique<GpuReadbackTexture>())
    , m_Worker([this](const std::stop_token stopToken) { WorkerLoop(stopToken); })
{
}

OIDNDenoiser::~OIDNDenoiser()
{
    {
        const std::lock_guard lock(m_WorkMutex);
        m_ShuttingDown = true;
        m_PendingJob.reset();
    }
    m_Worker.request_stop();
    m_WorkCondition.notify_all();
}

void OIDNDenoiser::SetEnabled(const bool enabled)
{
    if (m_Enabled == enabled)
    {
        return;
    }

    m_Enabled = enabled;
    ResetHistory();
}

void OIDNDenoiser::OnResourcesRecreated(const uint32_t width, const uint32_t height)
{
    Assert(width > 0u && height > 0u, "OIDN output dimensions must be positive.");
    InvalidateGeneration(true);
    m_Output.reset();
    m_Width = 0u;
    m_Height = 0u;
    if (!m_Enabled)
    {
        return;
    }

    m_Output = RenderTexture::CreateUav2D(
        m_DeviceContext,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        width,
        height,
        L"OIDN Denoised HDR Result");
    m_Width = width;
    m_Height = height;
    m_UsingCuda = TryCreateCudaResources(width, height);
    if (!m_UsingCuda)
    {
        m_Readback->Initialize(m_DeviceContext.GetDevice(), *m_Output, 1u);
    }
}

void OIDNDenoiser::ResetHistory()
{
    InvalidateGeneration(false);
}

bool OIDNDenoiser::BeginReadback(
    const bool accumulationEnabled,
    const uint32_t accumulationFrameIndex,
    const uint32_t staticSpp)
{
    if (!m_Enabled ||
        m_Output == nullptr ||
        (!m_UsingCuda && !m_Readback->IsInitialized()) ||
        !accumulationEnabled ||
        accumulationFrameIndex < (std::max)(staticSpp, 1u) ||
        m_ReadbackQueued ||
        m_ReadbackPending ||
        m_HasUploadedResult)
    {
        return false;
    }

    {
        const std::lock_guard lock(m_WorkMutex);
        if (m_PendingJob.has_value() || m_CompletedJob.has_value() || m_WorkerBusy)
        {
            return false;
        }
    }

    m_ReadbackQueued = m_UsingCuda || m_Readback->BeginCopy();
    m_ReadbackRecorded = false;
    m_ReadbackDiagnosticFrameIndex = DiagnosticTelemetryEvent::NoFrame;
    m_ReadbackDiagnosticCorrelationId = 0u;
    if (m_ReadbackQueued)
    {
        m_ReadbackGeneration = m_Generation;
        m_ReadbackSpp = accumulationFrameIndex;
    }
    return m_ReadbackQueued;
}

bool OIDNDenoiser::RecordReadback(CommandList& commandList, const std::shared_ptr<Texture>& source)
{
    if (!m_ReadbackQueued || source == nullptr)
    {
        return false;
    }

    if (const DX12Diagnostics::DiagnosticRenderPassScope* scope =
        DX12Diagnostics::DiagnosticRenderPassScope::GetCurrent())
    {
        m_ReadbackDiagnosticFrameIndex = scope->GetDesc().FrameIndex;
        m_ReadbackDiagnosticCorrelationId = scope->GetDesc().CorrelationId;
    }
    FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
        m_DeviceContext,
        source->GetD3D12Resource().Get(),
        DX12Diagnostics::DiagnosticResourceAccess::Read,
        "native_oidn_readback_source");

    m_ReadbackRecorded = m_UsingCuda
        ? RecordCudaInput(commandList, source)
        : m_Readback->RecordCopy(commandList, *source);
    return m_ReadbackRecorded;
}

bool OIDNDenoiser::TryCreateCudaResources(const uint32_t width, const uint32_t height)
{
    static_assert(sizeof(::LUID) == OIDN_LUID_SIZE, "D3D12 and OIDN LUID sizes must match.");
    Assert(m_Output != nullptr, "OIDN CUDA resources require a persistent output texture.");

    try
    {
        auto resources = std::make_shared<CudaResources>();
        const Microsoft::WRL::ComPtr<ID3D12Device2>& device = m_DeviceContext.GetDevice();
        const D3D12_RESOURCE_DESC outputDesc = m_Output->GetD3D12ResourceDesc();
        Assert(
            outputDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            outputDesc.Width == width &&
            outputDesc.Height == height &&
            outputDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
            outputDesc.DepthOrArraySize == 1u &&
            outputDesc.MipLevels == 1u &&
            outputDesc.SampleDesc.Count == 1u,
            "OIDN CUDA output must be a single-sample R32G32B32A32_FLOAT texture.");

        UINT64 copySize = 0u;
        device->GetCopyableFootprints(
            &outputDesc,
            0u,
            1u,
            0u,
            &resources->Footprint,
            &resources->NumRows,
            nullptr,
            &copySize);
        Assert(
            copySize != 0u &&
            resources->NumRows == height &&
            resources->Footprint.Footprint.Width == width &&
            resources->Footprint.Footprint.RowPitch >= width * sizeof(float) * 4u,
            "OIDN CUDA staging footprint is invalid.");

        resources->Input = std::make_shared<OidnSharedBuffer>(
            copySize,
            L"OIDN CUDA Input Staging",
            m_DeviceContext.GetD3D12DeviceContext());
        resources->Output = std::make_shared<OidnSharedBuffer>(
            copySize,
            L"OIDN CUDA Output Staging",
            m_DeviceContext.GetD3D12DeviceContext());
        const D3D12_RESOURCE_DESC inputDesc = resources->Input->GetD3D12ResourceDesc();
        resources->AllocationSize = device->GetResourceAllocationInfo(0u, 1u, &inputDesc).SizeInBytes;
        Assert(resources->AllocationSize >= copySize, "OIDN CUDA shared staging allocation is too small.");

        const ::LUID d3d12Luid = device->GetAdapterLuid();
        oidn::LUID oidnLuid = {};
        std::memcpy(oidnLuid.bytes, &d3d12Luid, sizeof(d3d12Luid));
        resources->Device = oidn::newDevice(oidnLuid);
        if (!resources->Device)
        {
            return false;
        }
        resources->Device.commit();
        ThrowIfOidnError(resources->Device, "OIDN CUDA device initialization failed");
        if (resources->Device.get<oidn::DeviceType>("type") != oidn::DeviceType::CUDA)
        {
            return false;
        }

        resources->InputBuffer = ImportOidnSharedBuffer(
            resources->Device,
            *device.Get(),
            *resources->Input,
            resources->AllocationSize);
        resources->OutputBuffer = ImportOidnSharedBuffer(
            resources->Device,
            *device.Get(),
            *resources->Output,
            resources->AllocationSize);

        ThrowIfFailed(device->CreateFence(
            0u,
            D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(&resources->Fence)));
        resources->FenceSemaphore = ImportOidnSharedFence(
            resources->Device,
            *device.Get(),
            *resources->Fence.Get());

        m_CudaResources = std::move(resources);
        return true;
    }
    catch (...)
    {
        m_CudaResources.reset();
        return false;
    }
}

bool OIDNDenoiser::RecordCudaInput(
    CommandList& commandList,
    const std::shared_ptr<Texture>& source)
{
    if (m_CudaResources == nullptr || source == nullptr)
    {
        return false;
    }

    const D3D12_RESOURCE_DESC sourceDesc = source->GetD3D12ResourceDesc();
    const CudaResources& resources = *m_CudaResources;
    if (sourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        sourceDesc.Width != resources.Footprint.Footprint.Width ||
        sourceDesc.Height != resources.NumRows ||
        sourceDesc.Format != resources.Footprint.Footprint.Format ||
        sourceDesc.SampleDesc.Count != 1u)
    {
        return false;
    }

    const Microsoft::WRL::ComPtr<ID3D12Resource> sourceResource = source->GetD3D12Resource();
    const Microsoft::WRL::ComPtr<ID3D12Resource> destinationResource = resources.Input->GetD3D12Resource();
    FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
        m_DeviceContext,
        destinationResource.Get(),
        DX12Diagnostics::DiagnosticResourceAccess::Write,
        "native_oidn_cuda_input");
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = resources.Footprint;
    commandList.ExecuteExternalCommandRecording(
        [sourceResource, destinationResource, footprint](ID3D12GraphicsCommandList2& nativeCommandList)
        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = destinationResource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = sourceResource.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0u;
            nativeCommandList.CopyTextureRegion(&destination, 0u, 0u, 0u, &source, nullptr);
        });
    return true;
}

void OIDNDenoiser::RecordCudaOutput(CommandList& commandList, const Job& job)
{
    Assert(job.Cuda != nullptr && m_Output != nullptr, "OIDN CUDA output copy resources are invalid.");
    const Microsoft::WRL::ComPtr<ID3D12Resource> sourceResource = job.Cuda->Output->GetD3D12Resource();
    const Microsoft::WRL::ComPtr<ID3D12Resource> destinationResource = m_Output->GetD3D12Resource();
    FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
        m_DeviceContext,
        sourceResource.Get(),
        DX12Diagnostics::DiagnosticResourceAccess::Read,
        "native_oidn_cuda_output");
    FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
        m_DeviceContext,
        destinationResource.Get(),
        DX12Diagnostics::DiagnosticResourceAccess::Write,
        "native_oidn_output");
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = job.Cuda->Footprint;
    commandList.ExecuteExternalCommandRecording(
        [sourceResource, destinationResource, footprint](ID3D12GraphicsCommandList2& nativeCommandList)
        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = destinationResource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0u;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = sourceResource.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            nativeCommandList.CopyTextureRegion(&destination, 0u, 0u, 0u, &source, nullptr);
        });
}

void OIDNDenoiser::EndReadback(const uint64_t submittedFenceValue)
{
    if (!m_ReadbackQueued)
    {
        return;
    }

    if (m_ReadbackRecorded && m_UsingCuda)
    {
        Assert(m_CudaResources != nullptr, "OIDN CUDA resources are not initialized.");
        Assert(submittedFenceValue != 0u, "OIDN CUDA input copy requires a direct queue submission fence.");

        Job job = {};
        job.Generation = m_ReadbackGeneration;
        job.Width = m_Width;
        job.Height = m_Height;
        job.Spp = m_ReadbackSpp;
        job.Cuda = m_CudaResources;
        job.DirectSubmissionFenceValue = submittedFenceValue;
        job.CudaInputFenceValue = ++m_CudaResources->NextFenceValue;
        job.CudaCompletionFenceValue = ++m_CudaResources->NextFenceValue;
        job.DiagnosticFrameIndex = m_ReadbackDiagnosticFrameIndex;
        job.DiagnosticCorrelationId = m_ReadbackDiagnosticCorrelationId;
        const auto directQueue = m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        Assert(directQueue != nullptr, "OIDN CUDA requires the direct command queue.");
        try
        {
            ThrowIfFailed(directQueue->GetD3D12CommandQueue()->Signal(
                m_CudaResources->Fence.Get(),
                job.CudaInputFenceValue));
            job.CudaProducerSignalIssued = true;
            RecordCudaFenceHandoff(
                job,
                "direct_to_cuda_signal",
                job.CudaInputFenceValue != 0u &&
                    job.CudaCompletionFenceValue > job.CudaInputFenceValue,
                "The Direct submission signaled the shared OIDN CUDA fence.");
        }
        catch (const std::exception& exception)
        {
            RecordCudaFenceHandoff(
                job,
                "direct_to_cuda_signal",
                false,
                exception.what());
            throw;
        }

        {
            const std::lock_guard lock(m_WorkMutex);
            Assert(
                !m_WorkerBusy && !m_PendingJob.has_value() && !m_CompletedJob.has_value(),
                "OIDN CUDA queued an overlapping denoise job.");
            if (job.Generation == m_Generation)
            {
                m_PendingJob = std::move(job);
            }
        }
        m_WorkCondition.notify_one();
    }
    else if (m_ReadbackRecorded)
    {
        m_Readback->EndCopy(submittedFenceValue);
        m_ReadbackPending = true;
    }
    else
    {
        m_Readback->CancelCopy();
    }
    m_ReadbackQueued = false;
    m_ReadbackRecorded = false;
}

void OIDNDenoiser::CancelReadback()
{
    if (m_ReadbackQueued && !m_UsingCuda)
    {
        m_Readback->CancelCopy();
    }
    m_ReadbackQueued = false;
    m_ReadbackRecorded = false;
}

void OIDNDenoiser::Poll(CommandQueue& directQueue)
{
    if (m_UsingCuda)
    {
        std::optional<Job> completed;
        {
            const std::lock_guard lock(m_WorkMutex);
            if (m_CompletedJob.has_value())
            {
                completed = std::move(m_CompletedJob);
                m_CompletedJob.reset();
            }
        }

        if (!completed.has_value() || !completed->UsesCuda() || !completed->Succeeded)
        {
            return;
        }

        const std::shared_ptr<CudaResources>& cuda = completed->Cuda;
        const bool validProducerChain =
            completed->CudaProducerSignalIssued &&
            completed->CudaInputWaitIssued &&
            completed->CudaCompletionSignalIssued &&
            completed->CudaInputFenceValue != 0u &&
            completed->CudaCompletionFenceValue > completed->CudaInputFenceValue;
        if (!validProducerChain)
        {
            RecordCudaFenceHandoff(
                *completed,
                "cuda_to_direct_wait",
                false,
                "OIDN CUDA completed without a valid producer signal and CUDA wait/signal chain.");
            return;
        }

        try
        {
            ThrowIfFailed(directQueue.GetD3D12CommandQueue()->Wait(
                cuda->Fence.Get(),
                completed->CudaCompletionFenceValue));
            completed->CudaConsumerWaitIssued = true;
            RecordCudaFenceHandoff(
                *completed,
                "cuda_to_direct_wait",
                true,
                "The Direct queue waits for the OIDN CUDA completion fence before result upload.");
        }
        catch (const std::exception& exception)
        {
            RecordCudaFenceHandoff(
                *completed,
                "cuda_to_direct_wait",
                false,
                exception.what());
            throw;
        }

        if (completed->Generation == m_Generation &&
            completed->Width == m_Width &&
            completed->Height == m_Height &&
            cuda == m_CudaResources)
        {
            m_CudaResultPendingUpload = std::move(completed);
        }
        return;
    }

    if (!m_ReadbackPending || !m_Readback->IsInitialized())
    {
        return;
    }

    std::vector<std::byte> packedPixels(static_cast<size_t>(m_Readback->GetSizeInBytes()));
    if (!m_Readback->CollectLatestCompleted(directQueue, packedPixels))
    {
        return;
    }
    m_ReadbackPending = false;

    if (m_ReadbackGeneration != m_Generation ||
        m_Readback->GetWidth() != m_Width ||
        m_Readback->GetHeight() != m_Height ||
        m_Readback->GetFormat() != DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        return;
    }

    Job job = {};
    job.Generation = m_ReadbackGeneration;
    job.Width = m_Width;
    job.Height = m_Height;
    job.Spp = m_ReadbackSpp;
    job.Pixels.resize(static_cast<size_t>(job.Width) * job.Height * 4u);
    const uint32_t rowByteCount = job.Width * sizeof(float) * 4u;
    const uint32_t rowPitch = m_Readback->GetRowPitch();
    Assert(rowPitch >= rowByteCount, "OIDN readback row pitch is smaller than its HDR row.");
    for (uint32_t row = 0u; row < job.Height; ++row)
    {
        std::memcpy(
            job.Pixels.data() + static_cast<size_t>(row) * job.Width * 4u,
            packedPixels.data() + static_cast<size_t>(row) * rowPitch,
            rowByteCount);
    }

    {
        const std::lock_guard lock(m_WorkMutex);
        if (job.Generation != m_Generation || m_WorkerBusy || m_PendingJob.has_value())
        {
            return;
        }
        m_PendingJob = std::move(job);
    }
    m_WorkCondition.notify_one();
}

void OIDNDenoiser::AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs)
{
    Assert(m_Enabled, "OIDN graph passes require the feature to be enabled.");
    Assert(m_Output != nullptr && (m_UsingCuda || m_Readback->IsInitialized()),
        "OIDN resources must be recreated before graph registration.");
    Assert(inputs.ReadbackSource != 0u && inputs.Output != 0u &&
            inputs.InputToken != 0u && inputs.OutputToken != 0u,
        "OIDN graph inputs are invalid.");
    Assert(inputs.Width == m_Width && inputs.Height == m_Height,
        "OIDN graph dimensions do not match the persistent output.");

    const auto sharedInputs = std::make_shared<const GraphInputs>(std::move(inputs));
    const std::wstring outputName = sharedInputs->DiagnosticNamePrefix + L".Output";
    const std::wstring readbackTokenName = sharedInputs->DiagnosticNamePrefix + L".ReadbackFinished";
    const std::wstring uploadTokenName = sharedInputs->DiagnosticNamePrefix + L".UploadFinished";
    const RenderGraph::ImportedResourceHandle output = builder.ImportResource(
        outputName.c_str(),
        [this]() -> const Resource& { return *m_Output; });
    RenderGraph::ImportedResourceHandle cudaInput;
    RenderGraph::ImportedResourceHandle cudaOutput;
    if (m_UsingCuda)
    {
        const std::shared_ptr<CudaResources> cuda = m_CudaResources;
        Assert(cuda != nullptr, "OIDN CUDA graph registration requires shared staging resources.");
        const std::wstring cudaInputName = sharedInputs->DiagnosticNamePrefix + L".CudaInput";
        const std::wstring cudaOutputName = sharedInputs->DiagnosticNamePrefix + L".CudaOutput";
        cudaInput = builder.ImportResource(
            cudaInputName.c_str(),
            [cuda]() -> const Resource& { return *cuda->Input; });
        cudaOutput = builder.ImportResource(
            cudaOutputName.c_str(),
            [cuda]() -> const Resource& { return *cuda->Output; });
    }
    const RenderGraph::ResourceId readbackFinished = builder.CreateToken(readbackTokenName.c_str());
    const RenderGraph::ResourceId uploadFinished = builder.CreateToken(uploadTokenName.c_str());

    builder.AddPass<OidnReadbackPassData>(
        L"OIDN HDR Readback",
        [this, sharedInputs, cudaInput, readbackFinished](RenderGraph::RenderGraphPassBuilder& passBuilder, OidnReadbackPassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passBuilder.ReadToken(sharedInputs->InputToken);
            passBuilder.ReadCopySource(sharedInputs->ReadbackSource);
            if (cudaInput.IsValid())
            {
                passBuilder.WriteImported(cudaInput, D3D12_RESOURCE_STATE_COPY_DEST);
            }
            passBuilder.WriteToken(readbackFinished);
        },
        [](const OidnReadbackPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordReadback(commandList, context.GetTexture(passData.Inputs->ReadbackSource));
        });

    builder.AddPass<OidnUploadPassData>(
        L"OIDN Result Upload",
        [this, cudaOutput, output, uploadFinished](RenderGraph::RenderGraphPassBuilder& passBuilder, OidnUploadPassData& passData)
        {
            passData.Feature = this;
            if (cudaOutput.IsValid())
            {
                passBuilder.ReadImported(cudaOutput, D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            passBuilder.WriteImported(output, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteToken(uploadFinished);
        },
        [](const OidnUploadPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
        {
            passData.Feature->RecordUpload(commandList);
        });

    builder.AddPass<OidnCompositePassData>(
        L"OIDN Composite",
        [this, sharedInputs, output, readbackFinished, uploadFinished](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            OidnCompositePassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passBuilder.ReadToken(sharedInputs->InputToken);
            passBuilder.ReadToken(readbackFinished);
            passBuilder.ReadToken(uploadFinished);
            passBuilder.ReadImported(output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.WriteUav(sharedInputs->Output);
            passBuilder.WriteToken(sharedInputs->OutputToken);
        },
        [](const OidnCompositePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordComposite(commandList, context.GetTexture(passData.Inputs->Output));
        });
}

void OIDNDenoiser::WorkerLoop(const std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        Job job = {};
        {
            std::unique_lock lock(m_WorkMutex);
            m_WorkCondition.wait(lock, [this, &stopToken]()
            {
                return stopToken.stop_requested() || m_ShuttingDown || m_PendingJob.has_value();
            });
            if (stopToken.stop_requested() || m_ShuttingDown)
            {
                return;
            }
            job = std::move(*m_PendingJob);
            m_PendingJob.reset();
            m_WorkerBusy = true;
        }

        try
        {
            if (job.UsesCuda())
            {
                ExecuteCudaDenoise(job);
            }
            else
            {
                ExecuteCpuDenoise(job);
            }
        }
        catch (const std::exception& exception)
        {
            if (job.UsesCuda())
            {
                RecordCudaFenceHandoff(
                    job,
                    "cuda_worker",
                    false,
                    exception.what());
            }
            job.Pixels.clear();
            job.Succeeded = false;
        }
        catch (...)
        {
            if (job.UsesCuda())
            {
                RecordCudaFenceHandoff(
                    job,
                    "cuda_worker",
                    false,
                    "OIDN CUDA worker failed with an unknown exception.");
            }
            job.Pixels.clear();
            job.Succeeded = false;
        }

        {
            const std::lock_guard lock(m_WorkMutex);
            m_WorkerBusy = false;
            if (!m_ShuttingDown)
            {
                m_CompletedJob = std::move(job);
            }
        }
    }
}

void OIDNDenoiser::ExecuteCpuDenoise(Job& job)
{
    Assert(job.Width > 0u && job.Height > 0u, "OIDN job dimensions are invalid.");
    Assert(job.Pixels.size() == static_cast<size_t>(job.Width) * job.Height * 4u,
        "OIDN job pixel data is invalid.");

    std::vector<float> output(job.Pixels.size());
    oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
    device.commit();
    oidn::FilterRef filter = device.newFilter("RT");
    constexpr size_t pixelStride = sizeof(float) * 4u;
    const size_t rowStride = static_cast<size_t>(job.Width) * pixelStride;
    filter.setImage("color", job.Pixels.data(), oidn::Format::Float3, job.Width, job.Height, 0u, pixelStride, rowStride);
    filter.setImage("output", output.data(), oidn::Format::Float3, job.Width, job.Height, 0u, pixelStride, rowStride);
    filter.set("hdr", true);
    filter.set("quality", oidn::Quality::Fast);
    filter.commit();
    filter.execute();
    ThrowIfOidnError(device, "OIDN CPU denoise failed");
    job.Pixels = std::move(output);
    job.Succeeded = true;
}

void OIDNDenoiser::ExecuteCudaDenoise(Job& job)
{
    Assert(job.Cuda != nullptr, "OIDN CUDA job has no shared resources.");
    Assert(job.Width > 0u && job.Height > 0u, "OIDN CUDA job dimensions are invalid.");

    const bool validInputFence =
        job.CudaProducerSignalIssued &&
        job.CudaInputFenceValue != 0u &&
        job.CudaCompletionFenceValue > job.CudaInputFenceValue;
    if (!validInputFence)
    {
        RecordCudaFenceHandoff(
            job,
            "cuda_input_wait",
            false,
            "OIDN CUDA was scheduled without a valid Direct producer signal.");
        throw std::logic_error("OIDN CUDA was scheduled without a valid Direct producer signal.");
    }

    CudaResources& cuda = *job.Cuda;
    constexpr size_t pixelStride = sizeof(float) * 4u;
    cuda.Device.waitSemaphoreAsync(cuda.FenceSemaphore, job.CudaInputFenceValue);
    ThrowIfOidnError(cuda.Device, "OIDN CUDA input fence wait failed");
    job.CudaInputWaitIssued = true;

    oidn::FilterRef filter = cuda.Device.newFilter("RT");
    if (!filter)
    {
        throw std::runtime_error("OIDN CUDA RT filter creation returned null.");
    }
    filter.setImage(
        "color",
        cuda.InputBuffer,
        oidn::Format::Float3,
        job.Width,
        job.Height,
        0u,
        pixelStride,
        cuda.Footprint.Footprint.RowPitch);
    filter.setImage(
        "output",
        cuda.OutputBuffer,
        oidn::Format::Float3,
        job.Width,
        job.Height,
        0u,
        pixelStride,
        cuda.Footprint.Footprint.RowPitch);
    filter.set("hdr", true);
    filter.set("quality", oidn::Quality::Fast);
    filter.commit();
    ThrowIfOidnError(cuda.Device, "OIDN CUDA filter commit failed");
    filter.execute();
    ThrowIfOidnError(cuda.Device, "OIDN CUDA denoise failed");

    cuda.Device.signalSemaphoreAsync(cuda.FenceSemaphore, job.CudaCompletionFenceValue);
    cuda.Device.sync();
    ThrowIfOidnError(cuda.Device, "OIDN CUDA output fence signal failed");
    job.CudaCompletionSignalIssued = true;
    RecordCudaFenceHandoff(
        job,
        "cuda_wait_and_signal",
        true,
        "OIDN CUDA waited for Direct input and signaled completion on the shared fence.");
    job.Succeeded = true;
}

void OIDNDenoiser::InvalidateGeneration(const bool resetReadbackResources)
{
    {
        const std::lock_guard lock(m_WorkMutex);
        ++m_Generation;
        m_PendingJob.reset();
        m_CompletedJob.reset();
    }
    m_CudaResultPendingUpload.reset();
    m_HasUploadedResult.store(false, std::memory_order_release);
    if (resetReadbackResources)
    {
        m_Readback->Reset();
        m_CudaResources.reset();
        m_UsingCuda = false;
        m_ReadbackQueued = false;
        m_ReadbackRecorded = false;
        m_ReadbackPending = false;
        m_ReadbackGeneration = 0u;
        m_ReadbackSpp = 0u;
    }
}

void OIDNDenoiser::RecordUpload(CommandList& commandList)
{
    if (m_UsingCuda)
    {
        std::optional<Job> completed = std::move(m_CudaResultPendingUpload);
        m_CudaResultPendingUpload.reset();
        if (!completed.has_value() ||
            !completed->UsesCuda() ||
            !completed->Succeeded ||
            completed->Generation != m_Generation ||
            completed->Width != m_Width ||
            completed->Height != m_Height ||
            completed->Cuda != m_CudaResources ||
            m_Output == nullptr)
        {
            return;
        }

        RecordCudaOutput(commandList, *completed);
        m_HasUploadedResult.store(true, std::memory_order_release);
        return;
    }

    std::optional<Job> completed;
    {
        const std::lock_guard lock(m_WorkMutex);
        if (m_CompletedJob.has_value())
        {
            completed = std::move(m_CompletedJob);
            m_CompletedJob.reset();
        }
    }
    if (!completed.has_value() ||
        completed->Generation != m_Generation ||
        completed->Width != m_Width ||
        completed->Height != m_Height ||
        !completed->Succeeded ||
        completed->Pixels.empty() ||
        m_Output == nullptr)
    {
        return;
    }

    const D3D12_SUBRESOURCE_DATA subresource = {
        .pData = completed->Pixels.data(),
        .RowPitch = static_cast<LONG_PTR>(completed->Width * sizeof(float) * 4u),
        .SlicePitch = static_cast<LONG_PTR>(completed->Pixels.size() * sizeof(float)),
    };
    ResourceUploader(commandList.GetDeviceContext()).CopyTextureSubresources(
        commandList,
        *m_Output,
        0u,
        1u,
        &subresource);
    FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
        m_DeviceContext,
        m_Output->GetD3D12Resource().Get(),
        DX12Diagnostics::DiagnosticResourceAccess::Write,
        "native_oidn_output");
    m_HasUploadedResult.store(true, std::memory_order_release);
}

void OIDNDenoiser::RecordCudaFenceHandoff(
    const Job& job,
    const char* phase,
    const bool valid,
    const char* message) const noexcept
{
    m_DeviceContext.RecordDiagnosticTelemetry({
        .Category = "assertion",
        .Name = "oidn_cuda_fence_handoff",
        .Severity = valid ? DiagnosticTelemetrySeverity::Info : DiagnosticTelemetrySeverity::Error,
        .FrameIndex = job.DiagnosticFrameIndex,
        .CorrelationId = job.DiagnosticCorrelationId,
        .Fields = {
            { "result", std::string(valid ? "pass" : "fail") },
            { "phase", std::string(phase != nullptr ? phase : "unknown") },
            { "message", std::string(message != nullptr ? message : "") },
            { "direct_submission_fence", job.DirectSubmissionFenceValue },
            { "cuda_input_fence", job.CudaInputFenceValue },
            { "cuda_completion_fence", job.CudaCompletionFenceValue },
            { "producer_signal_issued", job.CudaProducerSignalIssued },
            { "cuda_input_wait_issued", job.CudaInputWaitIssued },
            { "cuda_completion_signal_issued", job.CudaCompletionSignalIssued },
            { "direct_consumer_wait_issued", job.CudaConsumerWaitIssued },
        },
    });
}

void OIDNDenoiser::RecordComposite(
    CommandList& commandList,
    const std::shared_ptr<Texture>& output)
{
    Assert(output != nullptr && m_Output != nullptr, "OIDN composite resources are invalid.");
    CompositeConstants constants = {};
    constants.Width = m_Width;
    constants.Height = m_Height;
    constants.ResultValid = m_HasUploadedResult.load(std::memory_order_acquire) ? 1u : 0u;

    CommandContext commandContext(commandList);
    commandContext.SetConstantBuffer(*m_CompositeShader, "OIDNCompositeConstants", constants);
    commandContext.SetTexture(*m_CompositeShader, "DenoisedResult", ShaderResourceView(m_Output));
    commandContext.SetUnorderedAccessView(*m_CompositeShader, "SceneColor", UnorderedAccessView(output));
    commandContext.BindPipeline(*m_CompositeShader);
    commandContext.BindDescriptorSet(m_CompositeShader->GetDescriptorSet());
    commandContext.Dispatch(
        Math::DivideByMultiple(m_Width, 8u),
        Math::DivideByMultiple(m_Height, 8u),
        1u);
}
//Modify End
