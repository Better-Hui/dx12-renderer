#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include "RenderContext.h"
#include "ResourceId.h"

namespace RenderGraph
{
//Modify Begin:2026-08-03 by BestHui
    enum class RenderPassQueue
    {
        Direct,
        AsyncCompute,
    };
//Modify End

    enum class InputType
    {
        Invalid,
        Token,
        ShaderResource,
//Modify Begin:2026-08-03 by BestHui
        NonPixelShaderResource,
//Modify End
//Modify Begin:2026-07-28 by BestHui
        UnorderedAccess,
        ExternalAccess,
//Modify End
        CopySource,
        IndirectArgument,
    };

    struct Input
    {
        ResourceId m_Id = 0;
        InputType m_Type = InputType::Invalid;
    };

    enum class OutputType
    {
        Invalid,
        Token,
        RenderTarget,
        DepthRead,
        DepthWrite,
        UnorderedAccess,
//Modify Begin:2026-07-28 by BestHui
        ExternalAccess,
//Modify End
        CopyDestination,
    };

    struct Output
    {
        ResourceId m_Id = 0;
        OutputType m_Type = OutputType::Invalid;
    };

    class RenderPass
    {
    public:
        using ExecuteFuncT = std::function<void(const RenderContext&, CommandList&)>;
//Modify Begin:2026-08-03 by BestHui
        using AsyncComputePrepareFuncT = std::function<void(CommandList&)>;
//Modify End
//Modify Begin:2026-07-28 by BestHui
        using ExternalExecuteFuncT = std::function<void(const RenderContext&)>;
//Modify End

        static std::unique_ptr<RenderPass> Create(
            const wchar_t* passName,
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExecuteFuncT& executeFunc,
            RenderPassQueue queue = RenderPassQueue::Direct
        );
//Modify Begin:2026-07-28 by BestHui
        static std::unique_ptr<RenderPass> CreateExternal(
            const wchar_t* passName,
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExternalExecuteFuncT& executeFunc
        );
//Modify End

        void Init(CommandList& commandList);

        void Execute(const RenderContext& context, CommandList& commandList);
//Modify Begin:2026-08-03 by BestHui
        void PrepareAsyncCompute(CommandList& commandList) const;
        void SetAsyncComputePrepare(const AsyncComputePrepareFuncT& prepareFunc) { m_AsyncComputePrepareFunc = prepareFunc; }
//Modify End
//Modify Begin:2026-07-28 by BestHui
        void ExecuteExternal(const RenderContext& context);
        virtual bool IsExternal() const { return false; }
//Modify End

        const std::vector<Input>& GetInputs() const { return m_Inputs; }
        const std::vector<Output>& GetOutputs() const { return m_Outputs; }
        const std::wstring& GetPassName() const { return m_PassName; }
//Modify Begin:2026-08-03 by BestHui
        RenderPassQueue GetQueue() const { return m_Queue; }
//Modify Begin:2026-07-30 by BestHui
        void SetParallelRecordingEligible(bool enabled) { m_ParallelRecordingEligible = enabled; }
        bool IsParallelRecordingEligible() const { return m_ParallelRecordingEligible; }
//Modify End
//Modify End

        virtual ~RenderPass() = default;

    protected:
        virtual void InitImpl(CommandList& commandList) = 0;
        virtual void ExecuteImpl(const RenderContext& context, CommandList& commandList) = 0;
//Modify Begin:2026-07-28 by BestHui
        virtual void ExecuteExternalImpl(const RenderContext& context);
//Modify End

        void RegisterInput(const Input& input);
        void RegisterOutput(const Output& output);

        void SetPassName(const wchar_t* passName);
        void SetPassName(const std::wstring& passName);
//Modify Begin:2026-08-03 by BestHui
        void SetQueue(RenderPassQueue queue) { m_Queue = queue; }
//Modify End

    private:
        std::vector<Input> m_Inputs;
        std::vector<Output> m_Outputs;
        std::wstring m_PassName = L"Render Pass";
//Modify Begin:2026-08-03 by BestHui
        RenderPassQueue m_Queue = RenderPassQueue::Direct;
        AsyncComputePrepareFuncT m_AsyncComputePrepareFunc;
//Modify End
//Modify Begin:2026-07-30 by BestHui
        bool m_ParallelRecordingEligible = false;
//Modify End
    };
}
