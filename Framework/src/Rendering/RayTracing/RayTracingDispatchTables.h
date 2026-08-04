#pragma once

//Modify Begin:2026-07-27 by BestHui

#include <Framework/Rendering/RayTracing/RayTracingPipelineStateBuilder.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/RayTracing/RayTracingShaderTable.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

class RayTracingDispatchTables final
{
public:
    explicit RayTracingDispatchTables(FrameworkDeviceContext& deviceContext)
        : m_DeviceContext(deviceContext)
    {
    }
    const RayTracingShaderPassDesc& ResolvePass(const RayTracingPipelineDesc& desc, std::string_view passName);
    void EnsureBuilt(const RayTracingPipelineState& pipelineState, const RayTracingShaderPassDesc& pass);
    D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(uint32_t width, uint32_t height, uint32_t depth) const;

private:
    std::vector<RayTracingShaderRecord> BuildShaderRecords(
        const RayTracingPipelineState& pipelineState,
        const std::vector<RayTracingShaderRecordDesc>& recordDescs) const;

//Modify Begin:2026-07-27 by BestHui
    struct BuiltPassTables
    {
        RayTracingShaderTable RayGenerationShaderTable;
        RayTracingShaderTable MissShaderTable;
        RayTracingShaderTable HitGroupShaderTable;
    };

    std::unordered_map<std::string, BuiltPassTables> m_PassTables;
    const BuiltPassTables* m_CurrentPassTables = nullptr;
//Modify End
    RayTracingShaderPassDesc m_DefaultPass;
//Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext& m_DeviceContext;
//Modify End
};

//Modify End
