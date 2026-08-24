//Modify Begin:2026-08-24 by Hui
#include "RenderGraphBuilder.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

#include <algorithm>

namespace RenderGraph
{
    RenderGraphPassBuilder::RenderGraphPassBuilder(const RenderGraphBuildOptions options)
        : m_Options(options)
    {
    }

    ResourceId RenderGraphPassBuilder::ReadToken(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::Token);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadTexture(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::ShaderResource);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadUav(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::UnorderedAccess);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadCopySource(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::CopySource);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadIndirectArgument(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::IndirectArgument);
        return resourceId;
    }

    void RenderGraphPassBuilder::ReadIndirectArgument(const Resource& resource)
    {
        AddExternalAccess(
            resource,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            ExternalResourceAccessMode::Read,
            false);
    }

    ResourceId RenderGraphPassBuilder::ReadBuffer(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::NonPixelShaderResource);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadWriteUav(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::UnorderedAccess);
        AddOutput(resourceId, OutputType::UnorderedAccess);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteTexture(const ResourceId resourceId)
    {
        AddOutput(resourceId, OutputType::RenderTarget);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteUav(const ResourceId resourceId)
    {
        AddOutput(resourceId, OutputType::UnorderedAccess);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteDepth(const ResourceId resourceId, const bool readOnly)
    {
        AddOutput(resourceId, readOnly ? OutputType::DepthRead : OutputType::DepthWrite);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteCopyDestination(const ResourceId resourceId)
    {
        AddOutput(resourceId, OutputType::CopyDestination);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteToken(const ResourceId resourceId)
    {
        AddOutput(resourceId, OutputType::Token);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::ReadExternal(const ResourceId resourceId)
    {
        AddInput(resourceId, InputType::ExternalAccess);
        return resourceId;
    }

    ResourceId RenderGraphPassBuilder::WriteExternal(const ResourceId resourceId)
    {
        AddOutput(resourceId, OutputType::ExternalAccess);
        return resourceId;
    }

    void RenderGraphPassBuilder::ReadExternal(
        const Resource& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const bool insertUavBarrier)
    {
        AddExternalAccess(resource, stateAfter, ExternalResourceAccessMode::Read, insertUavBarrier);
    }

    void RenderGraphPassBuilder::WriteExternal(
        const Resource& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const bool insertUavBarrier)
    {
        AddExternalAccess(resource, stateAfter, ExternalResourceAccessMode::Write, insertUavBarrier);
    }

    ResourceId RenderGraphPassBuilder::ReadImported(
        const ImportedResourceHandle& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const bool insertUavBarrier)
    {
        AddInput(resource.GetId(), InputType::ExternalAccess);
        AddImportedAccess(resource, stateAfter, ExternalResourceAccessMode::Read, insertUavBarrier);
        return resource.GetId();
    }

    ResourceId RenderGraphPassBuilder::WriteImported(
        const ImportedResourceHandle& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const bool insertUavBarrier)
    {
        AddOutput(resource.GetId(), OutputType::ExternalAccess);
        AddImportedAccess(resource, stateAfter, ExternalResourceAccessMode::Write, insertUavBarrier);
        return resource.GetId();
    }

    ResourceId RenderGraphPassBuilder::ReadWriteImported(
        const ImportedResourceHandle& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const bool insertUavBarrier)
    {
        AddInput(resource.GetId(), InputType::ExternalAccess);
        AddOutput(resource.GetId(), OutputType::ExternalAccess);
        AddImportedAccess(resource, stateAfter, ExternalResourceAccessMode::Write, insertUavBarrier);
        return resource.GetId();
    }

    void RenderGraphPassBuilder::UseAsyncComputeWhenSupported()
    {
        if (m_Options.AsyncComputeSupported)
        {
            m_Queue = RenderPassQueue::AsyncCompute;
        }
    }

    void RenderGraphPassBuilder::UseCopyQueue()
    {
        Assert(m_Options.CopyQueueSupported, "Render graph copy queue is not available.");
        m_Queue = RenderPassQueue::Copy;
    }

    void RenderGraphPassBuilder::SetParallelRecordingEligible(const bool enabled)
    {
        m_ParallelRecordingEligible = enabled;
    }

    std::unique_ptr<RenderPass> RenderGraphPassBuilder::Build(
        const wchar_t* passName,
        RenderPass::ExecuteFuncT executeFunc)
    {
        ValidateCanBuild(false);
        m_Built = true;

        std::unique_ptr<RenderPass> renderPass = RenderPass::Create(
            passName,
            m_Inputs,
            m_Outputs,
            executeFunc,
            m_Queue);
        renderPass->SetParallelRecordingEligible(m_ParallelRecordingEligible);
        ApplyExternalAccesses(*renderPass);
        return renderPass;
    }

    std::unique_ptr<RenderPass> RenderGraphPassBuilder::BuildExternal(
        const wchar_t* passName,
        RenderPass::ExternalExecuteFuncT executeFunc)
    {
        ValidateCanBuild(true);
        m_Built = true;

        std::unique_ptr<RenderPass> renderPass = RenderPass::CreateExternal(
            passName,
            m_Inputs,
            m_Outputs,
            executeFunc);
        ApplyExternalAccesses(*renderPass);
        return renderPass;
    }

    void RenderGraphPassBuilder::AddInput(const ResourceId resourceId, const InputType type)
    {
        Assert(!m_Built, "Cannot modify a render pass after it is built.");
        Assert(resourceId != 0u, "Render-pass input resource ID is invalid.");
        Assert(type != InputType::Invalid, "Render-pass input type is invalid.");
        const auto duplicate = std::ranges::find_if(
            m_Inputs,
            [resourceId](const Input& input) { return input.m_Id == resourceId; });
        Assert(duplicate == m_Inputs.end(), "Render-pass input resource was declared more than once.");
        m_Inputs.push_back({ resourceId, type });
    }

    void RenderGraphPassBuilder::AddOutput(const ResourceId resourceId, const OutputType type)
    {
        Assert(!m_Built, "Cannot modify a render pass after it is built.");
        Assert(resourceId != 0u, "Render-pass output resource ID is invalid.");
        Assert(type != OutputType::Invalid, "Render-pass output type is invalid.");
        const auto duplicate = std::ranges::find_if(
            m_Outputs,
            [resourceId](const Output& output) { return output.m_Id == resourceId; });
        Assert(duplicate == m_Outputs.end(), "Render-pass output resource was declared more than once.");
        m_Outputs.push_back({ resourceId, type });
    }

    void RenderGraphPassBuilder::AddExternalAccess(
        const Resource& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const ExternalResourceAccessMode mode,
        const bool insertUavBarrier)
    {
        Assert(!m_Built, "Cannot modify a render pass after it is built.");
        Assert(resource.IsValid(), "External render-pass resource must be initialized.");
        m_ExternalAccesses.push_back({
            .StaticResource = &resource,
            .StateAfter = stateAfter,
            .Mode = mode,
            .InsertUavBarrier = insertUavBarrier,
        });
    }

    void RenderGraphPassBuilder::AddImportedAccess(
        const ImportedResourceHandle& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const ExternalResourceAccessMode mode,
        const bool insertUavBarrier)
    {
        Assert(!m_Built, "Cannot modify a render pass after it is built.");
        Assert(resource.IsValid(), "Imported render-pass resource handle is invalid.");
        static_cast<void>(resource.Resolve());
        m_ExternalAccesses.push_back({
            .Imported = resource,
            .StateAfter = stateAfter,
            .Mode = mode,
            .InsertUavBarrier = insertUavBarrier,
        });
    }

    void RenderGraphPassBuilder::ApplyExternalAccesses(RenderPass& renderPass) const
    {
        for (const PendingExternalAccess& access : m_ExternalAccesses)
        {
            if (access.Imported.IsValid())
            {
                renderPass.AddImportedResourceAccess(
                    access.Imported,
                    access.StateAfter,
                    access.Mode,
                    access.InsertUavBarrier);
            }
            else
            {
                Assert(access.StaticResource != nullptr, "External render-pass resource is null.");
                renderPass.AddExternalResourceAccess(
                    *access.StaticResource,
                    access.StateAfter,
                    access.Mode,
                    access.InsertUavBarrier);
            }
        }
    }

    void RenderGraphPassBuilder::ValidateCanBuild(const bool external) const
    {
        Assert(!m_Built, "A render pass can only be built once.");
        Assert(!external || m_Queue == RenderPassQueue::Direct, "External render passes must use the direct queue.");
        Assert(
            !m_ParallelRecordingEligible || (!external && m_Queue == RenderPassQueue::Direct),
            "Only direct non-external passes can record in parallel.");
    }

    RenderGraphBuilder::RenderGraphBuilder(const RenderGraphBuildOptions options)
        : m_Options(options)
    {
    }

    void RenderGraphBuilder::AddPass(std::unique_ptr<RenderPass> renderPass)
    {
        Assert(renderPass != nullptr, "Cannot add a null render pass.");
        m_RenderPasses.push_back(std::move(renderPass));
    }

    ImportedResourceHandle RenderGraphBuilder::ImportResource(
        const wchar_t* diagnosticName,
        std::function<const Resource&()> resolver)
    {
        Assert(diagnosticName != nullptr && diagnosticName[0] != L'\0',
            "Imported render-graph resource requires a diagnostic name.");
        Assert(static_cast<bool>(resolver), "Imported render-graph resource requires a resolver.");
        const ResourceId resourceId = ResourceIds::GetResourceId(diagnosticName);
        const auto duplicate = std::ranges::find_if(
            m_ImportedResources,
            [resourceId](const std::shared_ptr<const ImportedResourceHandle::Definition>& definition)
            {
                return definition->Id == resourceId;
            });
        Assert(duplicate == m_ImportedResources.end(),
            "Imported render-graph resource was registered more than once.");

        auto definition = std::make_shared<ImportedResourceHandle::Definition>();
        definition->Id = resourceId;
        definition->Resolver = std::move(resolver);
        const ImportedResourceHandle handle(definition);
        static_cast<void>(handle.Resolve());
        m_ImportedResources.push_back(std::move(definition));
        return handle;
    }

    ResourceId RenderGraphBuilder::CreateTexture(
        const wchar_t* diagnosticName,
        RenderMetadataExpression<uint32_t> widthExpression,
        RenderMetadataExpression<uint32_t> heightExpression,
        const DXGI_FORMAT format,
        const ClearValue::COLOR clearColor,
        const ResourceInitAction initAction,
        const D3D12_RESOURCE_FLAGS extraResourceFlags,
        const D3D12_HEAP_FLAGS heapFlags,
        const bool dedicatedResource)
    {
        Assert(diagnosticName != nullptr && diagnosticName[0] != L'\0',
            "Render-graph texture requires a diagnostic name.");
        Assert(static_cast<bool>(widthExpression) && static_cast<bool>(heightExpression),
            "Render-graph texture requires width and height expressions.");
        Assert(format != DXGI_FORMAT_UNKNOWN, "Render-graph texture format is invalid.");
        const ResourceId resourceId = ResourceIds::GetResourceId(diagnosticName);
        const auto duplicate = std::ranges::find_if(
            m_TextureDescriptions,
            [resourceId](const TextureDescription& texture) { return texture.m_Id == resourceId; });
        Assert(duplicate == m_TextureDescriptions.end(),
            "Render-graph texture was registered more than once by this builder.");

        m_TextureDescriptions.emplace_back(
            resourceId,
            widthExpression,
            heightExpression,
            format,
            clearColor,
            initAction,
            extraResourceFlags,
            heapFlags,
            dedicatedResource);
        return resourceId;
    }

    ResourceId RenderGraphBuilder::CreateToken(const wchar_t* diagnosticName)
    {
        Assert(diagnosticName != nullptr && diagnosticName[0] != L'\0',
            "Render-graph token requires a diagnostic name.");
        const ResourceId resourceId = ResourceIds::GetResourceId(diagnosticName);
        const auto duplicate = std::ranges::find_if(
            m_TokenDescriptions,
            [resourceId](const TokenDescription& token) { return token.m_Id == resourceId; });
        Assert(duplicate == m_TokenDescriptions.end(), "Render-graph token was registered more than once.");
        m_TokenDescriptions.push_back({ resourceId });
        return resourceId;
    }

    std::vector<std::unique_ptr<RenderPass>> RenderGraphBuilder::ReleasePasses()
    {
        return std::move(m_RenderPasses);
    }

    std::vector<TextureDescription> RenderGraphBuilder::ReleaseTextureDescriptions()
    {
        return std::move(m_TextureDescriptions);
    }

    std::vector<TokenDescription> RenderGraphBuilder::ReleaseTokenDescriptions()
    {
        return std::move(m_TokenDescriptions);
    }
}
//Modify End
