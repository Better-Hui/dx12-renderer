//Modify Begin:2026-08-07 by BestHui
#include "RenderGraphCompiler.h"

#include "RenderGraphResourceStateTracker.h"
#include "RenderMetadata.h"
#include "RenderPass.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Buffer.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Resource.h>

#include <algorithm>
#include <set>

namespace
{
    using namespace RenderGraph;

    bool IsProducerOutput(const OutputType outputType)
    {
        return outputType == OutputType::Token ||
            outputType == OutputType::RenderTarget ||
            outputType == OutputType::DepthWrite ||
            outputType == OutputType::UnorderedAccess ||
            outputType == OutputType::ExternalAccess ||
            outputType == OutputType::CopyDestination;
    }

    bool DirectlyDependsOn(const RenderPass& pass, const RenderPass& potentialProducer)
    {
        for (const Input& input : pass.GetInputs())
        {
            for (const Output& output : potentialProducer.GetOutputs())
            {
                if (IsProducerOutput(output.m_Type) && input.m_Id == output.m_Id)
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::vector<std::vector<RenderPass*>> TopologicallySort(
        std::span<const std::unique_ptr<RenderPass>> renderPasses)
    {
        std::vector<RenderPass*> unresolvedPasses;
        unresolvedPasses.reserve(renderPasses.size());
        for (const std::unique_ptr<RenderPass>& renderPass : renderPasses)
        {
            unresolvedPasses.push_back(renderPass.get());
        }

        std::vector<std::vector<RenderPass*>> result;
        while (!unresolvedPasses.empty())
        {
            std::vector<RenderPass*> dependencyFreePasses;
            for (RenderPass* renderPass : unresolvedPasses)
            {
                const bool hasDependency = std::ranges::any_of(
                    unresolvedPasses,
                    [renderPass](const RenderPass* otherPass)
                    {
                        return renderPass != otherPass && DirectlyDependsOn(*renderPass, *otherPass);
                    });
                if (!hasDependency)
                {
                    dependencyFreePasses.push_back(renderPass);
                }
            }

            Assert(!dependencyFreePasses.empty(), "Render graph has a loop.");
            for (RenderPass* renderPass : dependencyFreePasses)
            {
                std::erase(unresolvedPasses, renderPass);
            }
            result.push_back(std::move(dependencyFreePasses));
        }
        return result;
    }

    std::set<RenderPass*> FindUnusedPasses(
        const std::vector<std::vector<RenderPass*>>& sortedRenderPasses,
        std::span<const ResourceId> externalOutputIds)
    {
        std::set<ResourceId> usedResources(externalOutputIds.begin(), externalOutputIds.end());
        std::set<RenderPass*> unusedPasses;
        for (const std::vector<RenderPass*>& passLevel : sortedRenderPasses)
        {
            unusedPasses.insert(passLevel.begin(), passLevel.end());
        }

        for (auto levelIt = sortedRenderPasses.rbegin(); levelIt != sortedRenderPasses.rend(); ++levelIt)
        {
            for (RenderPass* renderPass : *levelIt)
            {
                const auto outputIt = std::ranges::find_if(
                    renderPass->GetOutputs(),
                    [&usedResources](const Output& output)
                    {
                        return IsProducerOutput(output.m_Type) && usedResources.contains(output.m_Id);
                    });
                if (outputIt == renderPass->GetOutputs().end())
                {
                    continue;
                }

                for (const Input& input : renderPass->GetInputs())
                {
                    usedResources.insert(input.m_Id);
                }
                unusedPasses.erase(renderPass);
            }
        }
        return unusedPasses;
    }

    bool TryGetInputTransition(
        const InputType inputType,
        D3D12_RESOURCE_STATES& stateAfter,
        bool& insertUavBarrier)
    {
        insertUavBarrier = false;
        switch (inputType)
        {
        case InputType::ShaderResource:
            stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            return true;
        case InputType::NonPixelShaderResource:
            stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            return true;
        case InputType::UnorderedAccess:
            stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            insertUavBarrier = true;
            return true;
        case InputType::ExternalAccess:
            stateAfter = D3D12_RESOURCE_STATE_COMMON;
            return true;
        case InputType::CopySource:
            stateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            return true;
        case InputType::IndirectArgument:
            stateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            return true;
        default:
            return false;
        }
    }

    bool TryGetOutputTransition(
        const OutputType outputType,
        D3D12_RESOURCE_STATES& stateAfter,
        bool& insertUavBarrier)
    {
        insertUavBarrier = false;
        switch (outputType)
        {
        case OutputType::CopyDestination:
            stateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            return true;
        case OutputType::UnorderedAccess:
            stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            insertUavBarrier = true;
            return true;
        case OutputType::ExternalAccess:
            stateAfter = D3D12_RESOURCE_STATE_COMMON;
            return true;
        default:
            return false;
        }
    }

    RenderTargetInfo CreateRenderTargetInfo(const RenderPass& renderPass, const ResourcePool& resourcePool)
    {
        RenderTargetInfo renderTargetInfo = {};
        uint32_t colorTextureCount = 0;
        uint32_t depthTextureCount = 0;
        for (const Output& output : renderPass.GetOutputs())
        {
            switch (output.m_Type)
            {
            case OutputType::RenderTarget:
                if (renderTargetInfo.m_RenderTarget == nullptr)
                {
                    renderTargetInfo.m_RenderTarget = std::make_shared<RenderTarget>();
                }
                renderTargetInfo.m_RenderTarget->AttachTexture(
                    static_cast<AttachmentPoint>(static_cast<uint32_t>(Color0) + colorTextureCount),
                    resourcePool.GetTexture(output.m_Id));
                ++colorTextureCount;
                Assert(colorTextureCount <= 8, "Too many color textures for the same render target.");
                break;
            case OutputType::DepthWrite:
            case OutputType::DepthRead:
                if (renderTargetInfo.m_RenderTarget == nullptr)
                {
                    renderTargetInfo.m_RenderTarget = std::make_shared<RenderTarget>();
                }
                renderTargetInfo.m_RenderTarget->AttachTexture(DepthStencil, resourcePool.GetTexture(output.m_Id));
                ++depthTextureCount;
                Assert(depthTextureCount == 1, "Too many depth textures for the same render target.");
                renderTargetInfo.m_ReadonlyDepth = output.m_Type == OutputType::DepthRead;
                break;
            default:
                break;
            }
        }
        return renderTargetInfo;
    }

    bool IsResourceDefined(
        const ResourceId resourceId,
        std::span<const TextureDescription> textures,
        std::span<const BufferDescription> buffers,
        std::span<const TokenDescription> tokens)
    {
        const auto matches = [resourceId](const auto& description) { return description.m_Id == resourceId; };
        return std::ranges::any_of(textures, matches) ||
            std::ranges::any_of(buffers, matches) ||
            std::ranges::any_of(tokens, matches);
    }

    bool CanRecordTogether(
        const RenderPass& firstPass,
        const RenderPass& secondPass)
    {
        if (firstPass.GetQueue() != RenderPassQueue::Direct ||
            secondPass.GetQueue() != RenderPassQueue::Direct ||
            firstPass.IsExternal() ||
            secondPass.IsExternal() ||
            !firstPass.IsParallelRecordingEligible() ||
            !secondPass.IsParallelRecordingEligible())
        {
            return false;
        }

        std::set<ResourceId> firstWrites;
        std::set<ResourceId> firstReads;
        for (const Output& output : firstPass.GetOutputs())
        {
            if (output.m_Type != OutputType::Token)
            {
                firstWrites.insert(output.m_Id);
            }
        }
        for (const Input& input : firstPass.GetInputs())
        {
            if (input.m_Type != InputType::Token)
            {
                firstReads.insert(input.m_Id);
            }
        }

        for (const Output& output : secondPass.GetOutputs())
        {
            if (output.m_Type != OutputType::Token &&
                (firstWrites.contains(output.m_Id) || firstReads.contains(output.m_Id)))
            {
                return false;
            }
        }
        for (const Input& input : secondPass.GetInputs())
        {
            if (input.m_Type != InputType::Token && firstWrites.contains(input.m_Id))
            {
                return false;
            }
        }
        return true;
    }
}

RenderGraph::RenderGraphCompiler::RenderGraphCompiler(
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    std::shared_ptr<ResourcePool> resourcePool,
    RenderGraphResourceStateTracker& resourceStateTracker)
    : m_Device(std::move(device))
    , m_ResourcePool(std::move(resourcePool))
    , m_ResourceStateTracker(resourceStateTracker)
{
    Assert(m_Device != nullptr, "Render graph compiler requires a D3D12 device.");
    Assert(m_ResourcePool != nullptr, "Render graph compiler requires a resource pool.");
}

void RenderGraph::RenderGraphCompiler::ValidateDefinition(
    const std::span<const std::unique_ptr<RenderPass>> renderPasses,
    const std::span<const TextureDescription> textures,
    const std::span<const BufferDescription> buffers,
    const std::span<const TokenDescription> tokens) const
{
    for (const std::unique_ptr<RenderPass>& renderPass : renderPasses)
    {
        for (const Input& input : renderPass->GetInputs())
        {
            Assert(IsResourceDefined(input.m_Id, textures, buffers, tokens), "Input undefined.");
        }
        for (const Output& output : renderPass->GetOutputs())
        {
            Assert(IsResourceDefined(output.m_Id, textures, buffers, tokens), "Output undefined.");
        }
    }
}

RenderGraph::CompiledRenderGraph RenderGraph::RenderGraphCompiler::Compile(
    const std::span<const std::unique_ptr<RenderPass>> renderPasses,
    const std::span<const TextureDescription> textures,
    const std::span<const BufferDescription> buffers,
    const std::span<const TokenDescription> tokens,
    const std::span<const ResourceId> externalOutputIds,
    const RenderMetadata& renderMetadata,
    const std::map<ResourceId, RenderGraphQueueFenceValues>& resourceRetirements) const
{
    ValidateDefinition(renderPasses, textures, buffers, tokens);
    const std::vector<std::vector<RenderPass*>> sortedRenderPasses = TopologicallySort(renderPasses);
    const std::set<RenderPass*> unusedPasses = FindUnusedPasses(sortedRenderPasses, externalOutputIds);

    CompiledRenderGraph compiledGraph;
    for (const std::vector<RenderPass*>& passLevel : sortedRenderPasses)
    {
        for (RenderPass* renderPass : passLevel)
        {
            if (!unusedPasses.contains(renderPass))
            {
                compiledGraph.m_RenderPasses.push_back(renderPass);
            }
        }
    }

    m_ResourcePool->Clear(resourceRetirements);
    for (const TextureDescription& texture : textures)
    {
        m_ResourcePool->RegisterTexture(texture, compiledGraph.m_RenderPasses, renderMetadata, m_Device);
    }
    for (const BufferDescription& buffer : buffers)
    {
        m_ResourcePool->RegisterBuffer(buffer, compiledGraph.m_RenderPasses, renderMetadata, m_Device);
    }
    m_ResourcePool->InitHeaps(compiledGraph.m_RenderPasses, m_Device, std::vector<ResourceId>(externalOutputIds.begin(), externalOutputIds.end()));

    m_ResourceStateTracker.Reset();
    for (const TextureDescription& texture : textures)
    {
        if (!m_ResourcePool->IsRegistered(texture.m_Id))
        {
            continue;
        }
        const ResourceDescription& description = m_ResourcePool->GetDescription(texture.m_Id);
        if (description.m_DedicatedResource || m_ResourcePool->HasResourceLifecycle(texture.m_Id))
        {
            m_ResourceStateTracker.SetCurrentResourceState(*m_ResourcePool->CreateTexture(texture.m_Id), D3D12_RESOURCE_STATE_COMMON);
        }
    }
    for (const BufferDescription& buffer : buffers)
    {
        if (!m_ResourcePool->IsRegistered(buffer.m_Id) || !m_ResourcePool->HasResourceLifecycle(buffer.m_Id))
        {
            continue;
        }
        m_ResourcePool->CreateBuffer(buffer.m_Id)->ForEachResourceRecursive(
            [this](const Resource& resource)
            {
                m_ResourceStateTracker.SetCurrentResourceState(resource, D3D12_RESOURCE_STATE_COMMON);
            });
    }

    for (uint32_t passIndex = 0; passIndex < compiledGraph.m_RenderPasses.size(); ++passIndex)
    {
        RenderPass* renderPass = compiledGraph.m_RenderPasses[passIndex];
        RenderTargetInfo renderTargetInfo = CreateRenderTargetInfo(*renderPass, *m_ResourcePool);
        if (renderTargetInfo.m_RenderTarget != nullptr)
        {
            compiledGraph.m_RenderTargets.emplace(renderPass, std::move(renderTargetInfo));
        }

        PassResourceStatePlan resourceStatePlan;
        for (const Input& input : renderPass->GetInputs())
        {
            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_COMMON;
            bool insertUavBarrier = false;
            if (TryGetInputTransition(input.m_Type, stateAfter, insertUavBarrier))
            {
                resourceStatePlan.InputTransitions.push_back({ input.m_Id, stateAfter, insertUavBarrier });
            }
        }
        for (const Output& output : renderPass->GetOutputs())
        {
            if (output.m_Type == OutputType::Token)
            {
                continue;
            }

            const auto& lifecycle = m_ResourcePool->GetResourceLifecycle(output.m_Id);
            if (lifecycle.m_BeginPassIndex == passIndex)
            {
                if (!m_ResourcePool->GetDescription(output.m_Id).m_DedicatedResource)
                {
                    resourceStatePlan.AliasingOutputs.push_back(output.m_Id);
                }
                resourceStatePlan.InitOutputs.push_back(output.m_Id);
            }

            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_COMMON;
            bool insertUavBarrier = false;
            if (TryGetOutputTransition(output.m_Type, stateAfter, insertUavBarrier))
            {
                resourceStatePlan.OutputTransitions.push_back({ output.m_Id, stateAfter, insertUavBarrier });
            }
        }

        //Modify Begin:2026-08-07 by BestHui
        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            PassResourceStatePlan::AsyncComputeDirectPreamble directPreamble = {};
            directPreamble.DirectProducerInputTransitions = resourceStatePlan.InputTransitions;
            directPreamble.AliasingOutputs = std::move(resourceStatePlan.AliasingOutputs);
            directPreamble.OutputTransitions = std::move(resourceStatePlan.OutputTransitions);
            resourceStatePlan.DirectPreamble = std::move(directPreamble);
        }
        //Modify End
        compiledGraph.m_ResourceStatePlans.emplace(renderPass, std::move(resourceStatePlan));
    }

    for (const std::vector<RenderPass*>& passLevel : sortedRenderPasses)
    {
        RenderGraphExecutionBatch parallelBatch = {};
        const auto flushParallelBatch = [&compiledGraph, &parallelBatch]()
        {
            if (parallelBatch.Passes.empty())
            {
                return;
            }
            parallelBatch.ParallelRecordingEligible = parallelBatch.Passes.size() > 1u;
            compiledGraph.m_ExecutionBatches.push_back(std::move(parallelBatch));
            parallelBatch = {};
        };

        for (RenderPass* renderPass : passLevel)
        {
            if (unusedPasses.contains(renderPass))
            {
                continue;
            }

            const bool canJoinParallelBatch =
                !parallelBatch.Passes.empty() &&
                CanRecordTogether(*parallelBatch.Passes.front(), *renderPass) &&
                std::ranges::all_of(
                    parallelBatch.Passes,
                    [renderPass](const RenderPass* batchPass)
                    {
                        return CanRecordTogether(*batchPass, *renderPass);
                    });
            if (parallelBatch.Passes.empty() && renderPass->IsParallelRecordingEligible())
            {
                parallelBatch.Passes.push_back(renderPass);
            }
            else if (canJoinParallelBatch)
            {
                parallelBatch.Passes.push_back(renderPass);
            }
            else
            {
                flushParallelBatch();
                if (renderPass->IsParallelRecordingEligible())
                {
                    parallelBatch.Passes.push_back(renderPass);
                }
                else
                {
                    compiledGraph.m_ExecutionBatches.push_back({ { renderPass }, false });
                }
            }
        }
        flushParallelBatch();
    }

    return compiledGraph;
}
//Modify End
