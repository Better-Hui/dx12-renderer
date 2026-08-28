//Modify Begin:2026-08-28 by Hui
#pragma once

#include <DX12Library/GpuReadbackTexture.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

class FrameworkDeviceContext;
class Texture;

namespace FrameworkDiagnostics
{
    class DiagnosticsSession;

    struct DiagnosticsImageAssertionOptions
    {
        double MinimumNonBlackPixelRatio = 0.001;
        double MinimumMaxChannelMean = 0.0005;
        uint8_t NonBlackChannelThreshold = 2u;
    };

    struct DiagnosticsImageCaptureResult
    {
        std::string Name;
        std::filesystem::path AttachmentPath;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        double MeanRed = 0.0;
        double MeanGreen = 0.0;
        double MeanBlue = 0.0;
        double NonBlackPixelRatio = 0.0;
        bool Passed = false;
    };

    class DiagnosticsImageCapture final
    {
    public:
        DiagnosticsImageCapture(FrameworkDeviceContext& deviceContext, DiagnosticsSession& session);
        ~DiagnosticsImageCapture();

        DiagnosticsImageCapture(const DiagnosticsImageCapture&) = delete;
        DiagnosticsImageCapture& operator=(const DiagnosticsImageCapture&) = delete;

        bool Request(
            const Texture& source,
            std::string name,
            DiagnosticsImageAssertionOptions assertion = {});
        bool Poll();
        void Drain();

        [[nodiscard]] bool IsReadbackPending() const noexcept { return m_PendingReadback.has_value(); }
        [[nodiscard]] bool HasPendingWork() const noexcept;
        [[nodiscard]] std::string GetLastError() const { return m_LastError; }
        [[nodiscard]] std::optional<DiagnosticsImageCaptureResult> GetLatestResult() const { return m_LatestResult; }

    private:
        struct PendingReadback
        {
            std::string Name;
            std::filesystem::path AttachmentPath;
            DiagnosticsImageAssertionOptions Assertion;
            uint64_t FenceValue = 0u;
        };

        struct PendingWrite
        {
            std::filesystem::path AttachmentPath;
            std::future<std::string> Completion;
        };

        bool CompleteReadback();
        void ReapCompletedWrites(bool waitForAll);
        void RecordWriteFailure(const std::filesystem::path& attachmentPath, std::string message) noexcept;

        FrameworkDeviceContext& m_DeviceContext;
        DiagnosticsSession& m_Session;
        GpuReadbackTexture m_Readback;
        std::vector<std::byte> m_ReadbackBytes;
        std::optional<PendingReadback> m_PendingReadback;
        std::deque<PendingWrite> m_PendingWrites;
        std::optional<DiagnosticsImageCaptureResult> m_LatestResult;
        std::string m_LastError;
        uint64_t m_NextCaptureId = 1u;
    };
}
//Modify End
