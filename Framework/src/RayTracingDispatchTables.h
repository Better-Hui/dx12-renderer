#pragma once

//Modify Begin:2026-07-27 by BestHui

#include <Framework/RayTracingPipelineStateBuilder.h>
#include <Framework/RayTracingShader.h>
#include <Framework/RayTracingShaderTable.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class RayTracingDispatchTables final
{
public:
    const RayTracingShaderPassDesc& ResolvePass(const RayTracingPipelineDesc& desc, std::string_view passName);
    void EnsureBuilt(const RayTracingPipelineState& pipelineState, const RayTracingShaderPassDesc& pass);
    D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(uint32_t width, uint32_t height, uint32_t depth) const;

private:
    std::vector<RayTracingShaderRecord> BuildShaderRecords(
        const RayTracingPipelineState& pipelineState,
        const std::vector<RayTracingShaderRecordDesc>& recordDescs) const;

    RayTracingShaderTable m_RayGenerationShaderTable;
    RayTracingShaderTable m_MissShaderTable;
    RayTracingShaderTable m_HitGroupShaderTable;
    RayTracingShaderPassDesc m_DefaultPass;
    std::string m_CurrentPassName;
};

//Modify End
