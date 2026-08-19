#pragma once

//Modify Begin:2026-08-18 by Hui
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "RenderPass.h"

class Resource;

namespace RenderGraph
{
    struct RenderGraphBuildOptions
    {
        bool AsyncComputeSupported = false;
        bool CopyQueueSupported = false;
    };

    class RenderGraphPassBuilder final
    {
    public:
        explicit RenderGraphPassBuilder(RenderGraphBuildOptions options = {});

        ResourceId ReadToken(ResourceId resourceId);
        ResourceId ReadTexture(ResourceId resourceId);
        ResourceId ReadBuffer(ResourceId resourceId);
        ResourceId ReadUav(ResourceId resourceId);
        ResourceId ReadCopySource(ResourceId resourceId);
        ResourceId ReadIndirectArgument(ResourceId resourceId);
        void ReadIndirectArgument(const Resource& resource);
        ResourceId ReadWriteUav(ResourceId resourceId);
        ResourceId WriteTexture(ResourceId resourceId);
        ResourceId WriteUav(ResourceId resourceId);
        ResourceId WriteDepth(ResourceId resourceId, bool readOnly = false);
        ResourceId WriteCopyDestination(ResourceId resourceId);
        ResourceId WriteToken(ResourceId resourceId);

        ResourceId ReadExternal(ResourceId resourceId);
        ResourceId WriteExternal(ResourceId resourceId);
        void ReadExternal(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            bool insertUavBarrier = false);
        void WriteExternal(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            bool insertUavBarrier = false);

        void UseAsyncComputeWhenSupported();
        void UseCopyQueue();
        void SetParallelRecordingEligible(bool enabled = true);

        std::unique_ptr<RenderPass> Build(
            const wchar_t* passName,
            RenderPass::ExecuteFuncT executeFunc);
        std::unique_ptr<RenderPass> BuildExternal(
            const wchar_t* passName,
            RenderPass::ExternalExecuteFuncT executeFunc);

    private:
        struct PendingExternalAccess
        {
            const Resource* Resource = nullptr;
            D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
            ExternalResourceAccessMode Mode = ExternalResourceAccessMode::Read;
            bool InsertUavBarrier = false;
        };

        void AddInput(ResourceId resourceId, InputType type);
        void AddOutput(ResourceId resourceId, OutputType type);
        void AddExternalAccess(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            ExternalResourceAccessMode mode,
            bool insertUavBarrier);
        void ApplyExternalAccesses(RenderPass& renderPass) const;
        void ValidateCanBuild(bool external) const;

        RenderGraphBuildOptions m_Options = {};
        std::vector<Input> m_Inputs;
        std::vector<Output> m_Outputs;
        std::vector<PendingExternalAccess> m_ExternalAccesses;
        RenderPassQueue m_Queue = RenderPassQueue::Direct;
        bool m_ParallelRecordingEligible = false;
        bool m_Built = false;
    };

    class RenderGraphBuilder final
    {
    public:
        explicit RenderGraphBuilder(RenderGraphBuildOptions options = {});

        void AddPass(std::unique_ptr<RenderPass> renderPass);

        template <typename PassDataT, typename SetupFuncT, typename ExecuteFuncT>
        void AddPass(
            const wchar_t* passName,
            SetupFuncT&& setupFunc,
            ExecuteFuncT&& executeFunc)
        {
            auto passData = std::make_shared<PassDataT>();
            RenderGraphPassBuilder passBuilder(m_Options);
            std::invoke(std::forward<SetupFuncT>(setupFunc), passBuilder, *passData);

            using ExecuteT = std::decay_t<ExecuteFuncT>;
            auto execute = [passData, executeFunc = ExecuteT(std::forward<ExecuteFuncT>(executeFunc))](
                const RenderContext& context,
                CommandList& commandList) mutable
            {
                std::invoke(executeFunc, static_cast<const PassDataT&>(*passData), context, commandList);
            };
            AddPass(passBuilder.Build(passName, std::move(execute)));
        }

        template <typename PassDataT, typename SetupFuncT, typename ExecuteFuncT>
        void AddExternalPass(
            const wchar_t* passName,
            SetupFuncT&& setupFunc,
            ExecuteFuncT&& executeFunc)
        {
            auto passData = std::make_shared<PassDataT>();
            RenderGraphPassBuilder passBuilder(m_Options);
            std::invoke(std::forward<SetupFuncT>(setupFunc), passBuilder, *passData);

            using ExecuteT = std::decay_t<ExecuteFuncT>;
            auto execute = [passData, executeFunc = ExecuteT(std::forward<ExecuteFuncT>(executeFunc))](
                const RenderContext& context) mutable
            {
                std::invoke(executeFunc, static_cast<const PassDataT&>(*passData), context);
            };
            AddPass(passBuilder.BuildExternal(passName, std::move(execute)));
        }

        std::vector<std::unique_ptr<RenderPass>> ReleasePasses();

    private:
        RenderGraphBuildOptions m_Options = {};
        std::vector<std::unique_ptr<RenderPass>> m_RenderPasses;
    };
}
//Modify End
