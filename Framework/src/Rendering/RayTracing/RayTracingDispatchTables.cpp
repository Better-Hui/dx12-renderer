//Modify Begin:2026-08-12 by Hui

#include "RayTracingDispatchTables.h"

#include "RayTracingShaderInternal.h"

#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <algorithm>
#include <stdexcept>

namespace
{
    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int byteCount = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string result(static_cast<size_t>(byteCount), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            byteCount,
            nullptr,
            nullptr);
        return result;
    }
}

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
        if (shaderIdentifier == nullptr)
        {
            throw std::runtime_error(
                "Ray tracing pipeline does not expose the shader-table export '" +
                ToUtf8(recordDesc.ExportName) +
                "'.");
        }
        records.push_back({ shaderIdentifier, recordDesc.LocalRootArguments });
    }
    return records;
}

void RayTracingDispatchTables::EnsureBuilt(
    const RayTracingPipelineState& pipelineState,
    const RayTracingShaderPassDesc& pass)
{
    auto tableFindResult = m_PassTables.find(pass.Name);
    if (tableFindResult == m_PassTables.end())
    {
        BuiltPassTables builtTables;
        const auto& device = m_DeviceContext.GetDevice();
        const std::wstring passName(pass.Name.begin(), pass.Name.end());

        const std::wstring rayGenName = L"Ray Generation Shader Table " + passName;
        const std::wstring missName = L"Miss Shader Table " + passName;
        const std::wstring hitGroupName = L"Hit Group Shader Table " + passName;
        builtTables.RayGenerationShaderTable.Reset(
            device,
            BuildShaderRecords(pipelineState, { { pass.RayGenerationShader, {} } }),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            rayGenName.c_str());
        builtTables.MissShaderTable.Reset(
            device,
            BuildShaderRecords(pipelineState, pass.MissShaderRecords),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            missName.c_str());
        builtTables.HitGroupShaderTable.Reset(
            device,
            BuildShaderRecords(pipelineState, pass.HitGroupRecords),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            hitGroupName.c_str());

        tableFindResult = m_PassTables.insert({ pass.Name, std::move(builtTables) }).first;
    }

    m_CurrentPassTables = &tableFindResult->second;
}

D3D12_DISPATCH_RAYS_DESC RayTracingDispatchTables::BuildDispatchDesc(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    Assert(m_CurrentPassTables != nullptr, "Ray tracing dispatch tables have not been prepared.");
    dispatchDesc.RayGenerationShaderRecord.StartAddress = m_CurrentPassTables->RayGenerationShaderTable.GetGpuVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_CurrentPassTables->RayGenerationShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StartAddress = m_CurrentPassTables->MissShaderTable.GetGpuVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_CurrentPassTables->MissShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StrideInBytes = m_CurrentPassTables->MissShaderTable.GetStrideInBytes();
    dispatchDesc.HitGroupTable.StartAddress = m_CurrentPassTables->HitGroupShaderTable.GetGpuVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_CurrentPassTables->HitGroupShaderTable.GetSizeInBytes();
    dispatchDesc.HitGroupTable.StrideInBytes = m_CurrentPassTables->HitGroupShaderTable.GetStrideInBytes();
    dispatchDesc.Width = width;
    dispatchDesc.Height = height;
    dispatchDesc.Depth = depth;
    return dispatchDesc;
}

//Modify End
