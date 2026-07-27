//Modify Begin:2026-07-27 by BestHui

#include "RayTracingDispatchTables.h"

#include "RayTracingShaderInternal.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>

#include <algorithm>

const RayTracingShaderPassDesc& RayTracingDispatchTables::ResolvePass(
    const RayTracingPipelineDesc& desc,
    std::string_view passName)
{
    if (desc.Passes.empty())
    {
        m_DefaultPass.Name = std::string(passName);
        m_DefaultPass.RayGenerationShader = std::wstring(passName.begin(), passName.end());
        m_DefaultPass.MissShaderRecords = { { RayTracingShaderInternal::DefaultMissShaderName, {} } };
        m_DefaultPass.HitGroupRecords = { { RayTracingShaderInternal::DefaultHitGroupName, {} } };
        return m_DefaultPass;
    }

    const auto passFindResult = std::find_if(
        desc.Passes.begin(),
        desc.Passes.end(),
        [passName](const RayTracingShaderPassDesc& pass)
        {
            return pass.Name == passName;
        });

    Assert(passFindResult != desc.Passes.end(), "Ray tracing shader pass was not found.");
    return *passFindResult;
}

std::vector<RayTracingShaderRecord> RayTracingDispatchTables::BuildShaderRecords(
    const RayTracingPipelineState& pipelineState,
    const std::vector<RayTracingShaderRecordDesc>& recordDescs) const
{
    std::vector<RayTracingShaderRecord> records;
    records.reserve(recordDescs.size());
    for (const RayTracingShaderRecordDesc& recordDesc : recordDescs)
    {
        const void* shaderIdentifier = pipelineState.GetShaderIdentifier(recordDesc.ExportName);
        Assert(shaderIdentifier != nullptr, "Ray tracing shader identifier was not found.");
        records.push_back({ shaderIdentifier, recordDesc.LocalRootArguments });
    }
    return records;
}

void RayTracingDispatchTables::EnsureBuilt(
    const RayTracingPipelineState& pipelineState,
    const RayTracingShaderPassDesc& pass)
{
    if (m_CurrentPassName == pass.Name)
    {
        return;
    }

    const auto device = Application::Get().GetDevice();
    m_RayGenerationShaderTable.Reset(
        device,
        BuildShaderRecords(pipelineState, { { pass.RayGenerationShader, {} } }),
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
        L"Ray Generation Shader Table");
    m_MissShaderTable.Reset(
        device,
        BuildShaderRecords(pipelineState, pass.MissShaderRecords),
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
        L"Miss Shader Table");
    m_HitGroupShaderTable.Reset(
        device,
        BuildShaderRecords(pipelineState, pass.HitGroupRecords),
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
        L"Hit Group Shader Table");

    m_CurrentPassName = pass.Name;
}

D3D12_DISPATCH_RAYS_DESC RayTracingDispatchTables::BuildDispatchDesc(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = m_RayGenerationShaderTable.GetGpuVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenerationShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StartAddress = m_MissShaderTable.GetGpuVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_MissShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StrideInBytes = m_MissShaderTable.GetStrideInBytes();
    dispatchDesc.HitGroupTable.StartAddress = m_HitGroupShaderTable.GetGpuVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_HitGroupShaderTable.GetSizeInBytes();
    dispatchDesc.HitGroupTable.StrideInBytes = m_HitGroupShaderTable.GetStrideInBytes();
    dispatchDesc.Width = width;
    dispatchDesc.Height = height;
    dispatchDesc.Depth = depth;
    return dispatchDesc;
}

//Modify End
