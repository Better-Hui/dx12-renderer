//Modify Begin:2026-08-18 by Hui
#include "RenderGraphCompiler.h"

#include "RenderMetadata.h"
#include "RenderPass.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Buffer.h>
#include <DX12Library/RenderTarget.h>

#include <algorithm>
#include <map>
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
        const RenderPassQueue queue,
        D3D12_RESOURCE_STATES& stateAfter,
        bool& insertUavBarrier)
    {
        insertUavBarrier = false;
        switch (inputType)
        {
        case InputType::ShaderResource:
            stateAfter = queue == RenderPassQueue::AsyncCompute
                ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
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

    bool IsLiveGpuResource(
        const ResourceId resourceId,
        std::span<RenderPass* const> renderPasses,
        std::span<const ResourceId> externalOutputIds)
    {
        if (std::ranges::find(externalOutputIds, resourceId) != externalOutputIds.end())
        {
            return true;
        }

        return std::ranges::any_of(
            renderPasses,
            [resourceId](const RenderPass* renderPass)
            {
                const auto matchesResource = [resourceId](const auto& access)
                {
                    return access.m_Id == resourceId;
                };
                return std::ranges::any_of(renderPass->GetInputs(), matchesResource) ||
                    std::ranges::any_of(renderPass->GetOutputs(), matchesResource);
            });
    }

    bool CanRecordInParallel(const RenderPass& renderPass)
    {
        return renderPass.GetQueue() == RenderPassQueue::Direct &&
            !renderPass.IsExternal() &&
            renderPass.IsParallelRecordingEligible();
    }

    bool IsAsyncComputeInput(const InputType inputType)
    {
        return inputType == InputType::Token ||
            inputType == InputType::ShaderResource ||
            inputType == InputType::NonPixelShaderResource ||
            inputType == InputType::UnorderedAccess ||
            inputType == InputType::CopySource ||
            inputType == InputType::IndirectArgument;
    }

    bool IsAsyncComputeOutput(const OutputType outputType)
    {
        return outputType == OutputType::Token ||
            outputType == OutputType::UnorderedAccess ||
            outputType == OutputType::CopyDestination;
    }

    bool IsCopyInput(const InputType inputType)
    {
        return inputType == InputType::Token || inputType == InputType::CopySource;
    }

    bool IsCopyOutput(const OutputType outputType)
    {
        return outputType == OutputType::Token || outputType == OutputType::CopyDestination;
    }

    bool HasValidCopyDestinationInitialization(
        const ResourceId resourceId,
        const std::span<const TextureDescription> textures,
        const std::span<const BufferDescription> buffers)
    {
        const auto matchesResource = [resourceId](const auto& description)
        {
            return description.m_Id == resourceId;
        };
        const auto isValidInitialization = [](const ResourceInitAction initAction)
        {
            return initAction == ResourceInitAction::CopyDestination ||
                initAction == ResourceInitAction::Preserve;
        };

        const auto texture = std::ranges::find_if(textures, matchesResource);
        if (texture != textures.end())
        {
            return isValidInitialization(texture->m_InitAction);
        }

        const auto buffer = std::ranges::find_if(buffers, matchesResource);
        return buffer != buffers.end() && isValidInitialization(buffer->m_InitAction);
    }

    bool ContainsResource(
        const std::span<RenderPass* const> passes,
        const ResourceId resourceId)
    {
        return std::ranges::any_of(
            passes,
            [resourceId](const RenderPass* pass)
            {
                const auto matches = [resourceId](const auto& access)
                {
                    return access.m_Id == resourceId;
                };
                return std::ranges::any_of(pass->GetInputs(), matches) ||
                    std::ranges::any_of(pass->GetOutputs(), matches);
            });
    }

    bool CanAppendNonDirectPass(
        const RenderGraphRecordingBatch& batch,
        const RenderPass& pass,
        const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
    {
        if (batch.Passes.empty() ||
            batch.Queue == RenderPassQueue::Direct ||
            batch.Queue != pass.GetQueue())
        {
            return false;
        }

        const auto planIt = resourceStatePlans.find(&pass);
        Assert(planIt != resourceStatePlans.end(), "Non-direct render pass has no resource-state plan.");
        Assert(planIt->second.DirectPreamble.has_value(), "Non-direct render pass has no direct-queue preamble.");
        const PassResourceStatePlan::NonDirectQueuePreamble& preamble = *planIt->second.DirectPreamble;
        if (!preamble.AliasingOutputs.empty())
        {
            return false;
        }

        const auto touchesEarlierGraphResource = [&batch](const PassResourceTransition& transition)
        {
            return ContainsResource(batch.Passes, transition.Id);
        };
        if (std::ranges::any_of(preamble.CrossQueueInputTransitions, touchesEarlierGraphResource) ||
            std::ranges::any_of(preamble.OutputTransitions, touchesEarlierGraphResource))
        {
            return false;
        }

        for (const ExternalResourceAccess& access : pass.GetExternalResourceAccesses())
        {
            if (access.Mode == ExternalResourceAccessMode::Write)
            {
                return false;
            }

            for (const RenderPass* earlierPass : batch.Passes)
            {
                const auto conflict = std::ranges::find_if(
                    earlierPass->GetExternalResourceAccesses(),
                    [&access](const ExternalResourceAccess& earlierAccess)
                    {
                        return earlierAccess.Resource == access.Resource &&
                            (earlierAccess.Mode == ExternalResourceAccessMode::Write ||
                                earlierAccess.StateAfter != access.StateAfter);
                    });
                if (conflict != earlierPass->GetExternalResourceAccesses().end())
                {
                    return false;
                }
            }
        }
        return true;
    }
}

RenderGraph::RenderGraphCompiler::RenderGraphCompiler(
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    std::shared_ptr<ResourcePool> resourcePool)
    : m_Device(std::move(device))
    , m_ResourcePool(std::move(resourcePool))
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
        Assert(renderPass != nullptr, "Render graph contains a null render pass.");
        Assert(!renderPass->IsExternal() || renderPass->GetQueue() == RenderPassQueue::Direct,
            "External render passes must use the direct queue.");

        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            for (const Input& input : renderPass->GetInputs())
            {
                Assert(IsAsyncComputeInput(input.m_Type),
                    "Async-compute passes only support compute-compatible inputs.");
            }
            for (const Output& output : renderPass->GetOutputs())
            {
                Assert(IsAsyncComputeOutput(output.m_Type),
                    "Async-compute passes cannot write render targets or depth resources.");
            }
            for (const ExternalResourceAccess& access : renderPass->GetExternalResourceAccesses())
            {
                Assert(access.Mode == ExternalResourceAccessMode::Read,
                    "Async-compute external resources are read-only; writes must use RenderGraph resources.");
            }
        }
        else if (renderPass->GetQueue() == RenderPassQueue::Copy)
        {
            for (const Input& input : renderPass->GetInputs())
            {
                Assert(IsCopyInput(input.m_Type),
                    "Copy-queue passes only support copy-source and token inputs.");
            }
            for (const Output& output : renderPass->GetOutputs())
            {
                Assert(IsCopyOutput(output.m_Type),
                    "Copy-queue passes only support copy-destination and token outputs.");
                if (output.m_Type == OutputType::CopyDestination)
                {
                    Assert(HasValidCopyDestinationInitialization(output.m_Id, textures, buffers),
                        "Copy-queue destinations must use CopyDestination or Preserve initialization; "
                        "Clear and Discard are illegal on copy command lists.");
                }
            }
            for (const ExternalResourceAccess& access : renderPass->GetExternalResourceAccesses())
            {
                const D3D12_RESOURCE_STATES expectedState =
                    access.Mode == ExternalResourceAccessMode::Read
                    ? D3D12_RESOURCE_STATE_COPY_SOURCE
                    : D3D12_RESOURCE_STATE_COPY_DEST;
                Assert(access.StateAfter == expectedState,
                    "Copy-queue external resources must declare COPY_SOURCE or COPY_DEST state.");
                Assert(!access.InsertUavBarrier,
                    "Copy-queue external resources cannot request UAV barriers.");
            }
        }
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
        if (IsLiveGpuResource(texture.m_Id, compiledGraph.m_RenderPasses, externalOutputIds))
        {
            m_ResourcePool->RegisterTexture(texture, compiledGraph.m_RenderPasses, renderMetadata, m_Device);
        }
    }
    for (const BufferDescription& buffer : buffers)
    {
        if (IsLiveGpuResource(buffer.m_Id, compiledGraph.m_RenderPasses, externalOutputIds))
        {
            m_ResourcePool->RegisterBuffer(buffer, compiledGraph.m_RenderPasses, renderMetadata, m_Device);
        }
    }
    m_ResourcePool->InitHeaps(compiledGraph.m_RenderPasses, m_Device, std::vector<ResourceId>(externalOutputIds.begin(), externalOutputIds.end()));
    m_ResourcePool->CreateResources();

    std::map<ResourceId, RenderPassQueue> lastWriterQueues;
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
            if (TryGetInputTransition(
                input.m_Type,
                renderPass->GetQueue(),
                stateAfter,
                insertUavBarrier))
            {
                resourceStatePlan.InputTransitions.push_back({ input.m_Id, stateAfter, insertUavBarrier });
            }
        }
        for (const ExternalResourceAccess& access : renderPass->GetExternalResourceAccesses())
        {
            Assert(access.Resource != nullptr && access.Resource->IsValid(),
                "Render pass external resource access must reference an initialized resource.");
            resourceStatePlan.ExternalResourceTransitions.push_back({
                access.Resource,
                access.StateAfter,
                access.InsertUavBarrier
            });
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

        if (renderPass->GetQueue() != RenderPassQueue::Direct)
        {
            PassResourceStatePlan::NonDirectQueuePreamble directPreamble = {};
            std::vector<PassResourceTransition> localInputTransitions;
            localInputTransitions.reserve(resourceStatePlan.InputTransitions.size());
            for (const PassResourceTransition& transition : resourceStatePlan.InputTransitions)
            {
                const auto lastWriter = lastWriterQueues.find(transition.Id);
                const bool requiresDirectTransition =
                    renderPass->GetQueue() == RenderPassQueue::Copy ||
                    (lastWriter != lastWriterQueues.end() &&
                        lastWriter->second != renderPass->GetQueue());
                if (requiresDirectTransition)
                {
                    directPreamble.CrossQueueInputTransitions.push_back(transition);
                }
                else
                {
                    localInputTransitions.push_back(transition);
                }
            }
            resourceStatePlan.InputTransitions = std::move(localInputTransitions);
            directPreamble.ExternalResourceTransitions = std::move(resourceStatePlan.ExternalResourceTransitions);
            directPreamble.AliasingOutputs = std::move(resourceStatePlan.AliasingOutputs);
            for (const PassResourceTransition& outputTransition : resourceStatePlan.OutputTransitions)
            {
                const auto matchingLocalInput = std::ranges::find_if(
                    resourceStatePlan.InputTransitions,
                    [&outputTransition](const PassResourceTransition& inputTransition)
                    {
                        return inputTransition.Id == outputTransition.Id &&
                            inputTransition.StateAfter == outputTransition.StateAfter;
                    });
                if (matchingLocalInput == resourceStatePlan.InputTransitions.end())
                {
                    directPreamble.OutputTransitions.push_back(outputTransition);
                }
            }
            resourceStatePlan.OutputTransitions.clear();
            resourceStatePlan.DirectPreamble = std::move(directPreamble);
        }
        compiledGraph.m_ResourceStatePlans.emplace(renderPass, std::move(resourceStatePlan));

        for (const Output& output : renderPass->GetOutputs())
        {
            if (IsProducerOutput(output.m_Type))
            {
                lastWriterQueues.insert_or_assign(output.m_Id, renderPass->GetQueue());
            }
        }
    }

    RenderGraphRecordingBatch recordingBatch = {};
    const auto flushRecordingBatch = [&compiledGraph, &recordingBatch]()
    {
        if (recordingBatch.Passes.empty())
        {
            return;
        }
        recordingBatch.RecordInParallel =
            recordingBatch.Queue == RenderPassQueue::Direct &&
            recordingBatch.Passes.size() > 1u;
        compiledGraph.m_RecordingBatches.push_back(std::move(recordingBatch));
        recordingBatch = {};
    };

    for (RenderPass* renderPass : compiledGraph.m_RenderPasses)
    {
        if (CanRecordInParallel(*renderPass))
        {
            if (!recordingBatch.Passes.empty() && recordingBatch.Queue != RenderPassQueue::Direct)
            {
                flushRecordingBatch();
            }
            recordingBatch.Queue = RenderPassQueue::Direct;
            recordingBatch.Passes.push_back(renderPass);
            continue;
        }

        if (renderPass->GetQueue() != RenderPassQueue::Direct)
        {
            if (!CanAppendNonDirectPass(
                recordingBatch,
                *renderPass,
                compiledGraph.m_ResourceStatePlans))
            {
                flushRecordingBatch();
                recordingBatch.Queue = renderPass->GetQueue();
            }
            recordingBatch.Passes.push_back(renderPass);
            continue;
        }

        flushRecordingBatch();
        compiledGraph.m_RecordingBatches.push_back({ { renderPass }, RenderPassQueue::Direct, false });
    }
    flushRecordingBatch();

    return compiledGraph;
}
//Modify End
