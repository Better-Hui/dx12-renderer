//Modify Begin:2026-08-07 by Hui
#pragma once

#include "RenderGraphExecutionPlan.h"
#include "RenderGraphQueueFence.h"

#include <d3d12.h>
#include <wrl.h>

#include <memory>
#include <span>
#include <vector>

class CommandQueue;

namespace RenderGraph
{
    class RenderPass;
    class ResourcePool;
    struct BufferDescription;
    struct RenderMetadata;
    struct TextureDescription;
    struct TokenDescription;

    class RenderGraphCompiler final
    {
    public:
        RenderGraphCompiler(
            Microsoft::WRL::ComPtr<ID3D12Device2> device,
            std::shared_ptr<ResourcePool> resourcePool);

        void ValidateDefinition(
            std::span<const std::unique_ptr<RenderPass>> renderPasses,
            std::span<const TextureDescription> textures,
            std::span<const BufferDescription> buffers,
            std::span<const TokenDescription> tokens) const;

        CompiledRenderGraph Compile(
            std::span<const std::unique_ptr<RenderPass>> renderPasses,
            std::span<const TextureDescription> textures,
            std::span<const BufferDescription> buffers,
            std::span<const TokenDescription> tokens,
            std::span<const ResourceId> externalOutputIds,
            const RenderMetadata& renderMetadata,
            const std::map<ResourceId, RenderGraphQueueFenceValues>& resourceRetirements) const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
        std::shared_ptr<ResourcePool> m_ResourcePool;
    };
}
//Modify End
