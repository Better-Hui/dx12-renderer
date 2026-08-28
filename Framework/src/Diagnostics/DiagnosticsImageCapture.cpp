//Modify Begin:2026-08-28 by Hui
#include <Framework/Diagnostics/DiagnosticsImageCapture.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Diagnostics/DiagnosticsSession.h>

#include <DirectXTex.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    constexpr size_t MaxBackgroundWriteCount = 2u;

    std::string SanitizeAttachmentName(std::string value)
    {
        for (char& character : value)
        {
            const bool isAlphaNumeric =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
            if (!isAlphaNumeric && character != '-' && character != '_')
            {
                character = '_';
            }
        }
        return value.empty() ? "image" : value;
    }

    std::string WritePng(
        const std::filesystem::path& outputPath,
        std::vector<uint8_t> pixels,
        const uint32_t width,
        const uint32_t height)
    {
        try
        {
            if (width == 0u || height == 0u || pixels.size() != static_cast<size_t>(width) * height * 4u)
            {
                return "Image attachment dimensions do not match the encoded pixel buffer.";
            }
            std::filesystem::create_directories(outputPath.parent_path());
            const DirectX::Image image = {
                width,
                height,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                static_cast<size_t>(width) * 4u,
                pixels.size(),
                pixels.data(),
            };
            ThrowIfFailed(DirectX::SaveToWICFile(
                image,
                DirectX::WIC_FLAGS_FORCE_SRGB,
                GUID_ContainerFormatPng,
                outputPath.c_str()));
            return {};
        }
        catch (const std::exception& exception)
        {
            return exception.what();
        }
    }
}

FrameworkDiagnostics::DiagnosticsImageCapture::DiagnosticsImageCapture(
    FrameworkDeviceContext& deviceContext,
    DiagnosticsSession& session)
    : m_DeviceContext(deviceContext)
    , m_Session(session)
{
}

FrameworkDiagnostics::DiagnosticsImageCapture::~DiagnosticsImageCapture()
{
    Drain();
}

bool FrameworkDiagnostics::DiagnosticsImageCapture::Request(
    const Texture& source,
    std::string name,
    const DiagnosticsImageAssertionOptions assertion)
{
    m_LastError.clear();
    if (m_PendingReadback.has_value())
    {
        m_LastError = "A diagnostics image readback is already pending.";
        return false;
    }
    if (!source.IsValid())
    {
        m_LastError = "Diagnostics image readback source is invalid.";
        return false;
    }

    try
    {
        const D3D12_RESOURCE_DESC sourceDescription = source.GetD3D12ResourceDesc();
        const bool requiresReinitialize =
            !m_Readback.IsInitialized() ||
            m_Readback.GetWidth() != sourceDescription.Width ||
            m_Readback.GetHeight() != sourceDescription.Height ||
            m_Readback.GetFormat() != sourceDescription.Format;
        if (requiresReinitialize)
        {
            m_Readback.Initialize(m_DeviceContext.GetDevice(), source);
        }
        if (!m_Readback.BeginCopy())
        {
            m_LastError = "All diagnostics image readback slots are still in flight.";
            m_Session.Record(
                "diagnostics.readback",
                "slot_exhausted",
                DiagnosticTelemetrySeverity::Warning,
                { { "name", name }, { "slot_count", static_cast<uint64_t>(GpuReadbackTexture::DefaultSlotCount) } });
            return false;
        }

        const std::shared_ptr<CommandQueue> queue =
            m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        const std::shared_ptr<CommandList> commandList = queue->GetCommandList();
        if (!m_Readback.RecordCopy(*commandList, source))
        {
            m_Readback.CancelCopy();
            m_LastError = "Diagnostics image readback did not record a copy command.";
            return false;
        }
        const uint64_t fenceValue = queue->ExecuteCommandList(commandList);
        m_Readback.EndCopy(fenceValue);

        const std::string attachmentStem = SanitizeAttachmentName(name) + "-" + std::to_string(m_NextCaptureId++);
        const std::filesystem::path attachmentPath = std::filesystem::path("images") / (attachmentStem + ".png");
        if (!m_Session.RegisterAttachment(attachmentPath, "image/png"))
        {
            throw std::runtime_error("Diagnostics image attachment path was rejected.");
        }
        m_ReadbackBytes.resize(static_cast<size_t>(m_Readback.GetSizeInBytes()));
        m_PendingReadback = {
            .Name = std::move(name),
            .AttachmentPath = attachmentPath,
            .Assertion = assertion,
            .FenceValue = fenceValue,
        };
        m_Session.Record(
            "diagnostics.readback",
            "image_queued",
            DiagnosticTelemetrySeverity::Info,
            {
                { "name", m_PendingReadback->Name },
                { "attachment", attachmentPath.generic_string() },
                { "width", static_cast<uint64_t>(m_Readback.GetWidth()) },
                { "height", static_cast<uint64_t>(m_Readback.GetHeight()) },
                { "format", static_cast<uint64_t>(m_Readback.GetFormat()) },
                { "fence_value", fenceValue },
            });
        return true;
    }
    catch (const std::exception& exception)
    {
        m_Readback.CancelCopy();
        m_LastError = exception.what();
        m_Session.RecordAssertion(
            "diagnostics.image_readback_request",
            AssertionResult::Failed,
            m_LastError,
            { { "name", name } });
        return false;
    }
}

bool FrameworkDiagnostics::DiagnosticsImageCapture::Poll()
{
    ReapCompletedWrites(false);
    if (!m_PendingReadback.has_value())
    {
        return false;
    }
    const std::shared_ptr<CommandQueue> queue =
        m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (!m_Readback.CollectLatestCompleted(*queue, m_ReadbackBytes))
    {
        return false;
    }
    return CompleteReadback();
}

void FrameworkDiagnostics::DiagnosticsImageCapture::Drain()
{
    if (m_PendingReadback.has_value())
    {
        const std::shared_ptr<CommandQueue> queue =
            m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
        queue->WaitForFenceValue(m_PendingReadback->FenceValue);
        Poll();
    }
    ReapCompletedWrites(true);
}

bool FrameworkDiagnostics::DiagnosticsImageCapture::HasPendingWork() const noexcept
{
    return m_PendingReadback.has_value() || !m_PendingWrites.empty();
}

bool FrameworkDiagnostics::DiagnosticsImageCapture::CompleteReadback()
{
    const PendingReadback request = std::move(m_PendingReadback.value());
    m_PendingReadback.reset();
    try
    {
        DirectX::ScratchImage sourceImage;
        ThrowIfFailed(sourceImage.Initialize2D(
            m_Readback.GetFormat(),
            m_Readback.GetWidth(),
            m_Readback.GetHeight(),
            1u,
            1u));
        const DirectX::Image* sourceSubresource = sourceImage.GetImage(0u, 0u, 0u);
        if (sourceSubresource == nullptr || sourceSubresource->height != m_Readback.GetHeight())
        {
            throw std::runtime_error("Diagnostics image readback returned an invalid source image.");
        }
        const size_t sourceRowSize = static_cast<size_t>(m_Readback.GetRowSizeInBytes());
        if (sourceRowSize > sourceSubresource->rowPitch)
        {
            throw std::runtime_error("Diagnostics image readback row size exceeds the decoded image row pitch.");
        }
        for (uint32_t row = 0u; row < m_Readback.GetHeight(); ++row)
        {
            std::memcpy(
                sourceSubresource->pixels + static_cast<size_t>(row) * sourceSubresource->rowPitch,
                m_ReadbackBytes.data() + static_cast<size_t>(row) * m_Readback.GetRowPitch(),
                sourceRowSize);
        }

        DirectX::ScratchImage convertedImage;
        const DirectX::Image* image = sourceSubresource;
        if (image->format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            ThrowIfFailed(DirectX::Convert(
                *image,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DirectX::TEX_FILTER_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                convertedImage));
            image = convertedImage.GetImage(0u, 0u, 0u);
        }
        if (image == nullptr || image->width == 0u || image->height == 0u)
        {
            throw std::runtime_error("Diagnostics image conversion returned an invalid RGBA8 image.");
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(image->width) * image->height * 4u);
        uint64_t nonBlackPixelCount = 0u;
        std::array<uint64_t, 3u> channelSums = {};
        for (size_t y = 0u; y < image->height; ++y)
        {
            const uint8_t* sourceRow = image->pixels + y * image->rowPitch;
            uint8_t* destinationRow = pixels.data() + y * image->width * 4u;
            std::memcpy(destinationRow, sourceRow, image->width * 4u);
            for (size_t x = 0u; x < image->width; ++x)
            {
                const uint8_t* pixel = sourceRow + x * 4u;
                channelSums[0] += pixel[0];
                channelSums[1] += pixel[1];
                channelSums[2] += pixel[2];
                if ((std::max)({ pixel[0], pixel[1], pixel[2] }) > request.Assertion.NonBlackChannelThreshold)
                {
                    ++nonBlackPixelCount;
                }
            }
        }

        const double pixelCount = static_cast<double>(image->width) * image->height;
        const double normalization = 1.0 / (pixelCount * 255.0);
        DiagnosticsImageCaptureResult result;
        result.Name = request.Name;
        result.AttachmentPath = request.AttachmentPath;
        result.Width = static_cast<uint32_t>(image->width);
        result.Height = static_cast<uint32_t>(image->height);
        result.MeanRed = static_cast<double>(channelSums[0]) * normalization;
        result.MeanGreen = static_cast<double>(channelSums[1]) * normalization;
        result.MeanBlue = static_cast<double>(channelSums[2]) * normalization;
        result.NonBlackPixelRatio = static_cast<double>(nonBlackPixelCount) / pixelCount;
        result.Passed = result.NonBlackPixelRatio >= request.Assertion.MinimumNonBlackPixelRatio &&
            (std::max)({ result.MeanRed, result.MeanGreen, result.MeanBlue }) >= request.Assertion.MinimumMaxChannelMean;
        m_LatestResult = result;

        const std::filesystem::path outputPath = m_Session.GetOutputDirectory() / request.AttachmentPath;
        if (m_PendingWrites.size() >= MaxBackgroundWriteCount)
        {
            throw std::runtime_error("Diagnostics image attachment writer is saturated.");
        }
        m_PendingWrites.push_back({
            .AttachmentPath = request.AttachmentPath,
            .Completion = std::async(
                std::launch::async,
                [outputPath, pixels = std::move(pixels), width = result.Width, height = result.Height]() mutable
                {
                    return WritePng(outputPath, std::move(pixels), width, height);
                }),
        });

        const std::string message =
            "Image=" + request.Name + "; attachment=" + request.AttachmentPath.generic_string() +
            "; size=" + std::to_string(result.Width) + "x" + std::to_string(result.Height) +
            "; mean_rgb=(" + std::to_string(result.MeanRed) + ", " +
                std::to_string(result.MeanGreen) + ", " + std::to_string(result.MeanBlue) +
            "); non_black_ratio=" + std::to_string(result.NonBlackPixelRatio) + ".";
        m_Session.Record(
            "diagnostics.image",
            "readback_completed",
            DiagnosticTelemetrySeverity::Info,
            {
                { "name", result.Name },
                { "attachment", request.AttachmentPath.generic_string() },
                { "width", static_cast<uint64_t>(result.Width) },
                { "height", static_cast<uint64_t>(result.Height) },
                { "mean_red", result.MeanRed },
                { "mean_green", result.MeanGreen },
                { "mean_blue", result.MeanBlue },
                { "non_black_pixel_ratio", result.NonBlackPixelRatio },
                { "fence_value", request.FenceValue },
            });
        m_Session.RecordAssertion(
            "image." + SanitizeAttachmentName(request.Name),
            result.Passed ? AssertionResult::Passed : AssertionResult::Failed,
            message,
            {
                { "minimum_non_black_pixel_ratio", request.Assertion.MinimumNonBlackPixelRatio },
                { "minimum_max_channel_mean", request.Assertion.MinimumMaxChannelMean },
            });
        return true;
    }
    catch (const std::exception& exception)
    {
        m_LastError = exception.what();
        m_Session.RecordAssertion(
            "diagnostics.image_readback_completion",
            AssertionResult::Failed,
            m_LastError,
            { { "name", request.Name }, { "attachment", request.AttachmentPath.generic_string() } });
        return false;
    }
}

void FrameworkDiagnostics::DiagnosticsImageCapture::ReapCompletedWrites(const bool waitForAll)
{
    while (!m_PendingWrites.empty())
    {
        PendingWrite& write = m_PendingWrites.front();
        if (!waitForAll && write.Completion.wait_for(std::chrono::seconds::zero()) != std::future_status::ready)
        {
            break;
        }
        const std::string error = write.Completion.get();
        if (!error.empty())
        {
            RecordWriteFailure(write.AttachmentPath, error);
        }
        else
        {
            m_Session.Record(
                "diagnostics.image",
                "attachment_written",
                DiagnosticTelemetrySeverity::Info,
                { { "attachment", write.AttachmentPath.generic_string() } });
        }
        m_PendingWrites.pop_front();
    }
}

void FrameworkDiagnostics::DiagnosticsImageCapture::RecordWriteFailure(
    const std::filesystem::path& attachmentPath,
    std::string message) noexcept
{
    m_LastError = message;
    m_Session.RecordAssertion(
        "diagnostics.image_attachment_write",
        AssertionResult::Failed,
        std::move(message),
        { { "attachment", attachmentPath.generic_string() } });
}
//Modify End
