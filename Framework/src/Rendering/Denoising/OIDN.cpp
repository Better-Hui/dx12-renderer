//Modify Begin:2026-08-25 by Hui
#include <Framework/Rendering/Denoising/OIDN.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/GpuReadbackTexture.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/ResourceUploader.h>
#include <DX12Library/Texture.h>
#include <Framework/Core/FrameworkDeviceContext.h>
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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{
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
    m_Readback->Initialize(m_DeviceContext.GetDevice(), *m_Output, 1u);
    m_Width = width;
    m_Height = height;
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
        !m_Readback->IsInitialized() ||
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

    m_ReadbackQueued = m_Readback->BeginCopy();
    m_ReadbackRecorded = false;
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

    m_ReadbackRecorded = m_Readback->RecordCopy(commandList, *source);
    return m_ReadbackRecorded;
}

void OIDNDenoiser::EndReadback(const uint64_t submittedFenceValue)
{
    if (!m_ReadbackQueued)
    {
        return;
    }

    if (m_ReadbackRecorded)
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
    if (m_ReadbackQueued)
    {
        m_Readback->CancelCopy();
    }
    m_ReadbackQueued = false;
    m_ReadbackRecorded = false;
}

void OIDNDenoiser::Poll(CommandQueue& directQueue)
{
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
    Assert(m_Output != nullptr && m_Readback->IsInitialized(),
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
    const RenderGraph::ResourceId readbackFinished = builder.CreateToken(readbackTokenName.c_str());
    const RenderGraph::ResourceId uploadFinished = builder.CreateToken(uploadTokenName.c_str());

    builder.AddPass<OidnReadbackPassData>(
        L"OIDN HDR Readback",
        [this, sharedInputs, readbackFinished](RenderGraph::RenderGraphPassBuilder& passBuilder, OidnReadbackPassData& passData)
        {
            passData.Feature = this;
            passData.Inputs = sharedInputs;
            passBuilder.ReadToken(sharedInputs->InputToken);
            passBuilder.ReadCopySource(sharedInputs->ReadbackSource);
            passBuilder.WriteToken(readbackFinished);
        },
        [](const OidnReadbackPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            passData.Feature->RecordReadback(commandList, context.GetTexture(passData.Inputs->ReadbackSource));
        });

    builder.AddPass<OidnUploadPassData>(
        L"OIDN Result Upload",
        [this, output, uploadFinished](RenderGraph::RenderGraphPassBuilder& passBuilder, OidnUploadPassData& passData)
        {
            passData.Feature = this;
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
            ExecuteDenoise(job);
        }
        catch (...)
        {
            job.Pixels.clear();
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

void OIDNDenoiser::ExecuteDenoise(Job& job)
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
    filter.commit();
    filter.execute();

    const char* errorMessage = nullptr;
    if (device.getError(errorMessage) != oidn::Error::None)
    {
        throw std::runtime_error(errorMessage != nullptr ? errorMessage : "OIDN CPU denoise failed.");
    }
    job.Pixels = std::move(output);
}

void OIDNDenoiser::InvalidateGeneration(const bool resetReadbackResources)
{
    {
        const std::lock_guard lock(m_WorkMutex);
        ++m_Generation;
        m_PendingJob.reset();
        m_CompletedJob.reset();
    }
    m_HasUploadedResult.store(false, std::memory_order_release);
    if (resetReadbackResources)
    {
        m_Readback->Reset();
        m_ReadbackQueued = false;
        m_ReadbackRecorded = false;
        m_ReadbackPending = false;
        m_ReadbackGeneration = 0u;
        m_ReadbackSpp = 0u;
    }
}

void OIDNDenoiser::RecordUpload(CommandList& commandList)
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
    if (!completed.has_value() ||
        completed->Generation != m_Generation ||
        completed->Width != m_Width ||
        completed->Height != m_Height ||
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
    m_HasUploadedResult.store(true, std::memory_order_release);
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
