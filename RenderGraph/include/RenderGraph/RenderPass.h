#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include "RenderContext.h"
#include "ResourceId.h"

class Resource;

namespace RenderGraph
{
//Modify Begin:2026-08-03 by Hui
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
//Modify Begin:2026-08-03 by Hui
        NonPixelShaderResource,
//Modify End
//Modify Begin:2026-07-28 by Hui
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
//Modify Begin:2026-07-28 by Hui
        ExternalAccess,
//Modify End
        CopyDestination,
    };

    struct Output
    {
        ResourceId m_Id = 0;
        OutputType m_Type = OutputType::Invalid;
    };

//Modify Begin:2026-08-13 by Hui
    enum class ExternalResourceAccessMode
    {
        Read,
        Write,
    };

    struct ExternalResourceAccess
    {
        const Resource* Resource = nullptr;
        D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
        ExternalResourceAccessMode Mode = ExternalResourceAccessMode::Read;
        bool InsertUavBarrier = false;
    };
//Modify End

    class RenderPass
    {
    public:
        using ExecuteFuncT = std::function<void(const RenderContext&, CommandList&)>;
//Modify Begin:2026-07-28 by Hui
        using ExternalExecuteFuncT = std::function<void(const RenderContext&)>;
//Modify End

        static std::unique_ptr<RenderPass> Create(
            const wchar_t* passName,
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExecuteFuncT& executeFunc,
            RenderPassQueue queue = RenderPassQueue::Direct
        );
//Modify Begin:2026-07-28 by Hui
        static std::unique_ptr<RenderPass> CreateExternal(
            const wchar_t* passName,
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExternalExecuteFuncT& executeFunc
        );
//Modify End

        void Init(CommandList& commandList);

        void Execute(const RenderContext& context, CommandList& commandList);
//Modify Begin:2026-07-28 by Hui
        void ExecuteExternal(const RenderContext& context);
        virtual bool IsExternal() const { return false; }
//Modify End

        const std::vector<Input>& GetInputs() const { return m_Inputs; }
        const std::vector<Output>& GetOutputs() const { return m_Outputs; }
        const std::wstring& GetPassName() const { return m_PassName; }
//Modify Begin:2026-08-13 by Hui
        void AddExternalResourceAccess(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            ExternalResourceAccessMode mode = ExternalResourceAccessMode::Read,
            bool insertUavBarrier = false);
        const std::vector<ExternalResourceAccess>& GetExternalResourceAccesses() const
        {
            return m_ExternalResourceAccesses;
        }
//Modify End
//Modify Begin:2026-08-03 by Hui
        RenderPassQueue GetQueue() const { return m_Queue; }
//Modify Begin:2026-07-30 by Hui
        void SetParallelRecordingEligible(bool enabled) { m_ParallelRecordingEligible = enabled; }
        bool IsParallelRecordingEligible() const { return m_ParallelRecordingEligible; }
//Modify End
//Modify End

        virtual ~RenderPass() = default;

    protected:
        virtual void InitImpl(CommandList& commandList) = 0;
        virtual void ExecuteImpl(const RenderContext& context, CommandList& commandList) = 0;
//Modify Begin:2026-07-28 by Hui
        virtual void ExecuteExternalImpl(const RenderContext& context);
//Modify End

        void RegisterInput(const Input& input);
        void RegisterOutput(const Output& output);

        void SetPassName(const wchar_t* passName);
        void SetPassName(const std::wstring& passName);
//Modify Begin:2026-08-03 by Hui
        void SetQueue(RenderPassQueue queue) { m_Queue = queue; }
//Modify End

    private:
        std::vector<Input> m_Inputs;
        std::vector<Output> m_Outputs;
        std::wstring m_PassName = L"Render Pass";
//Modify Begin:2026-08-03 by Hui
        RenderPassQueue m_Queue = RenderPassQueue::Direct;
//Modify End
//Modify Begin:2026-08-13 by Hui
        std::vector<ExternalResourceAccess> m_ExternalResourceAccesses;
//Modify End
//Modify Begin:2026-07-30 by Hui
        bool m_ParallelRecordingEligible = false;
//Modify End
    };
}
