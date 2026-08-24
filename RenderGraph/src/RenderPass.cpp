#include "RenderPass.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

namespace RenderGraph
{
    class LambdaRenderPass final : public RenderPass
    {
    public:
        LambdaRenderPass(
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExecuteFuncT& executeFunc)
            : m_ExecuteFunc(executeFunc)
        {
            for (auto& input : inputs)
            {
                RegisterInput(input);
            }

            for (auto& output : outputs)
            {
                RegisterOutput(output);
            }
        }

        ~LambdaRenderPass() override = default;

    protected:
        void InitImpl(CommandList& commandList) override
        {}

        void ExecuteImpl(const RenderContext& context, CommandList& commandList) override
        {
            m_ExecuteFunc(context, commandList);
        }

    private:
        ExecuteFuncT m_ExecuteFunc;
    };

//Modify Begin:2026-07-28 by Hui
    class LambdaExternalRenderPass final : public RenderPass
    {
    public:
        LambdaExternalRenderPass(
            const std::vector<Input>& inputs,
            const std::vector<Output>& outputs,
            const ExternalExecuteFuncT& executeFunc)
            : m_ExecuteFunc(executeFunc)
        {
            for (auto& input : inputs)
            {
                RegisterInput(input);
            }

            for (auto& output : outputs)
            {
                RegisterOutput(output);
            }
        }

        ~LambdaExternalRenderPass() override = default;

        bool IsExternal() const override { return true; }

    protected:
        void InitImpl(CommandList& commandList) override
        {}

        void ExecuteImpl(const RenderContext& context, CommandList& commandList) override
        {
            Assert(false, "External render passes must be executed through ExecuteExternal.");
        }

        void ExecuteExternalImpl(const RenderContext& context) override
        {
            m_ExecuteFunc(context);
        }

    private:
        ExternalExecuteFuncT m_ExecuteFunc;
    };
//Modify End
}

std::unique_ptr<RenderGraph::RenderPass> RenderGraph::RenderPass::Create(
    const wchar_t* passName,
    const std::vector<Input>& inputs,
    const std::vector<Output>& outputs,
    const ExecuteFuncT& executeFunc,
    const RenderPassQueue queue)
{
    const auto pRenderPass = new LambdaRenderPass(inputs, outputs, executeFunc);
    pRenderPass->SetPassName(passName);
//Modify Begin:2026-08-03 by Hui
    pRenderPass->SetQueue(queue);
//Modify End
    return std::unique_ptr<RenderPass>(pRenderPass);
}

//Modify Begin:2026-07-28 by Hui
std::unique_ptr<RenderGraph::RenderPass> RenderGraph::RenderPass::CreateExternal(
    const wchar_t* passName,
    const std::vector<Input>& inputs,
    const std::vector<Output>& outputs,
    const ExternalExecuteFuncT& executeFunc)
{
    const auto pRenderPass = new LambdaExternalRenderPass(inputs, outputs, executeFunc);
    pRenderPass->SetPassName(passName);
    return std::unique_ptr<RenderPass>(pRenderPass);
}
//Modify End

void RenderGraph::RenderPass::Init(CommandList& commandList)
{
    InitImpl(commandList);
}

void RenderGraph::RenderPass::Execute(const RenderContext& context, CommandList& commandList)
{
    ExecuteImpl(context, commandList);
}

//Modify Begin:2026-07-28 by Hui
void RenderGraph::RenderPass::ExecuteExternal(const RenderContext& context)
{
    ExecuteExternalImpl(context);
}

void RenderGraph::RenderPass::ExecuteExternalImpl(const RenderContext& context)
{
    Assert(false, "This render pass does not support external execution.");
}
//Modify End

void RenderGraph::RenderPass::RegisterInput(const Input& input)
{
    Assert(input.m_Type != InputType::Invalid, "Input is invalid.");
    Assert(std::ranges::find_if(m_Inputs, [input](const auto& i) { return i.m_Id == input.m_Id; }) == m_Inputs.end(), "Input with such ID is already registered.");

    m_Inputs.push_back(input);
}

void RenderGraph::RenderPass::RegisterOutput(const Output& output)
{
    Assert(output.m_Type != OutputType::Invalid, "Output is invalid.");
    Assert(std::ranges::find_if(m_Outputs, [output](const auto& o) { return o.m_Id == output.m_Id; }) == m_Outputs.end(), "Output with such ID is already registered.");

    m_Outputs.push_back(output);
}

void RenderGraph::RenderPass::SetPassName(const wchar_t* passName)
{
    m_PassName = passName;
}

void RenderGraph::RenderPass::SetPassName(const std::wstring& passName)
{
    m_PassName = passName;
}

//Modify Begin:2026-08-24 by Hui
RenderGraph::ResourceId RenderGraph::ImportedResourceHandle::GetId() const
{
    Assert(m_Definition != nullptr, "Imported render-graph resource handle is invalid.");
    return m_Definition->Id;
}

const Resource& RenderGraph::ImportedResourceHandle::Resolve() const
{
    Assert(m_Definition != nullptr && static_cast<bool>(m_Definition->Resolver),
        "Imported render-graph resource resolver is invalid.");
    const Resource& resource = m_Definition->Resolver();
    Assert(resource.IsValid(), "Imported render-graph resource resolver returned an invalid resource.");
    return resource;
}

const Resource& RenderGraph::ExternalResourceAccess::Resolve() const
{
    if (Imported.IsValid())
    {
        return Imported.Resolve();
    }
    Assert(StaticResource != nullptr && StaticResource->IsValid(), "External render-pass resource is invalid.");
    return *StaticResource;
}

void RenderGraph::RenderPass::AddExternalResourceAccess(
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const ExternalResourceAccessMode mode,
    const bool insertUavBarrier)
{
    resource.ForEachResourceRecursive(
        [this, stateAfter, mode, insertUavBarrier](const Resource& nestedResource)
        {
            Assert(nestedResource.IsValid(), "External render-pass resource must be initialized.");
            const auto existingAccess = std::ranges::find_if(
                m_ExternalResourceAccesses,
                [&nestedResource](const ExternalResourceAccess& access)
                {
                    return access.StaticResource == &nestedResource;
                });
            Assert(
                existingAccess == m_ExternalResourceAccesses.end() ||
                    (existingAccess->StateAfter == stateAfter &&
                     existingAccess->Mode == mode &&
                     existingAccess->InsertUavBarrier == insertUavBarrier),
                "A render pass cannot declare conflicting external resource states.");
            if (existingAccess == m_ExternalResourceAccesses.end())
            {
                m_ExternalResourceAccesses.push_back({
                    .StaticResource = &nestedResource,
                    .StateAfter = stateAfter,
                    .Mode = mode,
                    .InsertUavBarrier = insertUavBarrier,
                });
            }
        });
}

void RenderGraph::RenderPass::AddImportedResourceAccess(
    const ImportedResourceHandle& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const ExternalResourceAccessMode mode,
    const bool insertUavBarrier)
{
    Assert(resource.IsValid(), "Imported render-pass resource handle is invalid.");
    const ResourceId resourceId = resource.GetId();
    const auto existingAccess = std::ranges::find_if(
        m_ExternalResourceAccesses,
        [resourceId](const ExternalResourceAccess& access)
        {
            return access.Id == resourceId;
        });
    Assert(
        existingAccess == m_ExternalResourceAccesses.end() ||
            (existingAccess->StateAfter == stateAfter &&
             existingAccess->Mode == mode &&
             existingAccess->InsertUavBarrier == insertUavBarrier),
        "A render pass cannot declare conflicting imported resource states.");
    if (existingAccess == m_ExternalResourceAccesses.end())
    {
        m_ExternalResourceAccesses.push_back({
            .Id = resourceId,
            .Imported = resource,
            .StateAfter = stateAfter,
            .Mode = mode,
            .InsertUavBarrier = insertUavBarrier,
        });
    }
}
//Modify End
