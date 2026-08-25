#pragma once

//Modify Begin:2026-08-25 by Hui
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/Meshlet.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <memory>
#include <string>
#include <vector>

class FrameworkDeviceContext;

class RaytracingDemoShaderPipelineController final
{
public:
    explicit RaytracingDemoShaderPipelineController(FrameworkDeviceContext& deviceContext);

    void CreateGeometryPipelines();
    void CreatePostProcessPipelines();
    void CreateLightingPipelines();
    void Reset();

    [[nodiscard]] const std::shared_ptr<Shader>& GetGBufferShader() const { return m_GBufferShader; }
    [[nodiscard]] const std::shared_ptr<Shader>& GetMeshletIndirectGBufferShader() const { return m_GBufferMeshletIndirectShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetMeshletCullShader() const { return m_MeshletCullShader; }
    [[nodiscard]] IndirectCommandSignature* GetMeshletDrawCommandSignature() { return m_MeshletDrawCommandSignature.get(); }
    [[nodiscard]] const std::shared_ptr<MeshShader>& GetTaskMeshGBufferShader() const { return m_GBufferTaskMeshShader; }
    [[nodiscard]] const std::shared_ptr<Shader>& GetDisplayCompositeShader() const { return m_DisplayCompositeShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetSkyboxComputeShader() const { return m_SkyboxComputeShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetSkyboxEquirectangularComputeShader() const { return m_SkyboxEquirectangularComputeShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetSkyboxCubemapStripComputeShader() const { return m_SkyboxCubemapStripComputeShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetDLSSRayReconstructionPrepareShader() const { return m_DLSSRayReconstructionPrepareShader; }
    [[nodiscard]] const std::shared_ptr<ComputeShader>& GetCopyQueueValidationShader() const { return m_CopyQueueValidationShader; }
    [[nodiscard]] const std::shared_ptr<Shader>& GetLightBillboardShader() const { return m_LightBillboardShader; }

private:
    std::shared_ptr<ShaderBlob> LoadShaderVariant(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {});

    FrameworkDeviceContext& m_DeviceContext;
    ShaderVariantManager m_ShaderVariants;
    std::shared_ptr<Shader> m_GBufferShader;
    std::shared_ptr<Shader> m_GBufferMeshletIndirectShader;
    std::shared_ptr<ComputeShader> m_MeshletCullShader;
    std::unique_ptr<IndirectCommandSignature> m_MeshletDrawCommandSignature;
    std::shared_ptr<MeshShader> m_GBufferTaskMeshShader;
    std::shared_ptr<Shader> m_DisplayCompositeShader;
    std::shared_ptr<ComputeShader> m_SkyboxComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxEquirectangularComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxCubemapStripComputeShader;
    std::shared_ptr<ComputeShader> m_DLSSRayReconstructionPrepareShader;
    std::shared_ptr<ComputeShader> m_CopyQueueValidationShader;
    std::shared_ptr<Shader> m_LightBillboardShader;
};
//Modify End
