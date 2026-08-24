#pragma once

//Modify Begin:2026-08-24 by Hui
#include <RenderGraph/ResourceId.h>

#include <DirectXMath.h>
#include <d3d12.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CommandList;
class FrameworkDeviceContext;
class Material;
class Mesh;
class RenderTarget;
class Texture;

namespace RenderGraph
{
    class RenderGraphBuilder;
}

class TAA final
{
public:
    struct GraphInputs
    {
        RenderGraph::ResourceId CurrentColor = 0;
        RenderGraph::ResourceId Velocity = 0;
        RenderGraph::ResourceId Output = 0;
        RenderGraph::ResourceId InputToken = 0;
        RenderGraph::ResourceId OutputToken = 0;
        uint32_t Width = 1;
        uint32_t Height = 1;
        std::function<float()> ResolveModulationFactor;
        std::wstring DiagnosticNamePrefix = L"Framework.TAA";
    };

    TAA(
        FrameworkDeviceContext& deviceContext,
        CommandList& commandList,
        DXGI_FORMAT backBufferFormat,
        uint32_t width,
        uint32_t height);

    [[nodiscard]] DirectX::XMFLOAT2 ComputeJitterOffset() const;
    [[nodiscard]] const DirectX::XMMATRIX& GetPreviousViewProjectionMatrix() const;

    void AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs);
    void ResetHistory();

    [[nodiscard]] DirectX::XMFLOAT2 GetCurrentJitterOffset() const;
    void OnRenderedFrame(const DirectX::XMMATRIX& viewProjectionMatrix);

private:
    bool EnsureCreated(uint32_t width, uint32_t height);
    void RecordResolve(
        CommandList& commandList,
        const std::shared_ptr<Texture>& currentBuffer,
        const std::shared_ptr<Texture>& historyBuffer,
        const std::shared_ptr<Texture>& velocityBuffer,
        const RenderTarget& destination,
        float modulationFactor,
        uint32_t width,
        uint32_t height);

    std::shared_ptr<Mesh> m_BlitMesh;
    std::shared_ptr<Material> m_Material;
    FrameworkDeviceContext& m_DeviceContext;
    DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;

    constexpr static DirectX::XMFLOAT2 JITTER_OFFSETS[]{
        {0, 0},
        {0.5f, 0.5f},
        {0.5f, -0.5f},
        {-0.5f, -0.5f},
        {-0.5f, 0.5f},
    };
    constexpr static uint32_t JITTER_OFFSETS_COUNT = _countof(JITTER_OFFSETS);
    DirectX::XMMATRIX m_PreviousViewProjectionMatrix = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    uint32_t m_HistoryIndex = 0;
    bool m_HistoryValid = false;
    bool m_HistoryCapturePending = false;
    std::shared_ptr<Texture> m_HistoryBuffers[2];
};
//Modify End
