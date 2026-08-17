#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/UI/ImGuiImpl.h>
#include <Framework/Geometry/Mesh.h>

#include <algorithm>

using namespace DirectX;

//Modify Begin:2026-07-27 by Hui
void RaytracingDemo::DrawLightBillboards(CommandList& cmd)
{
    if (m_LightBillboardShader == nullptr || m_LightBillboardMesh == nullptr)
    {
        return;
    }

    const XMVECTOR cameraRight = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), GetSceneCamera().GetRotation());
    const XMVECTOR cameraUp = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), GetSceneCamera().GetRotation());

    XMFLOAT4 cameraRightFloat{};
    XMFLOAT4 cameraUpFloat{};
    XMStoreFloat4(&cameraRightFloat, XMVectorSetW(cameraRight, 0.0f));
    XMStoreFloat4(&cameraUpFloat, XMVectorSetW(cameraUp, 0.0f));

//Modify Begin:2026-07-29 by Hui
    CommandContext commandContext(cmd);
    commandContext.BindPipeline(*m_LightBillboardShader);
//Modify End
    commandContext.SetConstantBuffer(*m_LightBillboardShader, "PipelineCBuffer", BuildPipelineConstants());

//Modify Begin:2026-07-30 by Hui
    XMFLOAT3 cameraPosition{};
    XMStoreFloat3(&cameraPosition, GetSceneCamera().GetTranslation());
    for (const DirectionalLight& light : m_Lights.GetDirectionalLights())
    {
        const XMVECTOR direction = XMVector3Normalize(XMLoadFloat4(&light.m_DirectionWs));
        XMFLOAT3 billboardPosition{};
        XMStoreFloat3(&billboardPosition, XMLoadFloat3(&cameraPosition) - direction * 8.0f);

        LightBillboardConstants constants{};
        constants.PositionAndSize = {
            billboardPosition.x,
            billboardPosition.y,
            billboardPosition.z,
            1.25f
        };
        constants.ColorAndAlpha = {
            light.m_Color.x,
            light.m_Color.y,
            light.m_Color.z,
            0.50f
        };
        constants.CameraRight = cameraRightFloat;
        constants.CameraUp = cameraUpFloat;
        constants.TypeAndParams = { 2.0f, 0.0f, 0.0f, 0.0f };

        commandContext.SetConstantBuffer(*m_LightBillboardShader, "MaterialCBuffer", constants);
        commandContext.BindDescriptorSet(m_LightBillboardShader->GetDescriptorSet());
        m_LightBillboardMesh->Draw(cmd);
    }
//Modify End

    for (const PointLight& light : m_Lights.GetPointLights())
    {
        LightBillboardConstants constants{};
        constants.PositionAndSize = {
            light.PositionWs.x,
            light.PositionWs.y,
            light.PositionWs.z,
//Modify Begin:2026-08-06 by Hui
            std::clamp(light.Range * 0.006f, 0.075f, 0.18f)
//Modify End
        };
        constants.ColorAndAlpha = {
            light.Color.x,
            light.Color.y,
            light.Color.z,
            0.48f
        };
        constants.CameraRight = cameraRightFloat;
        constants.CameraUp = cameraUpFloat;
//Modify Begin:2026-07-30 by Hui
        constants.TypeAndParams = { 0.0f, 0.0f, 0.0f, 0.0f };
//Modify End

        commandContext.SetConstantBuffer(*m_LightBillboardShader, "MaterialCBuffer", constants);
        commandContext.BindDescriptorSet(m_LightBillboardShader->GetDescriptorSet());
        m_LightBillboardMesh->Draw(cmd);
    }

    for (const AreaLightData& light : m_Lights.GetAreaLights())
    {
        LightBillboardConstants constants{};
        constants.PositionAndSize = {
            light.PositionAndRange.x,
            light.PositionAndRange.y,
            light.PositionAndRange.z,
            std::clamp(light.PositionAndRange.w * 0.07f, 1.1f, 2.8f)
        };
        constants.ColorAndAlpha = {
            light.ColorAndIntensity.x,
            light.ColorAndIntensity.y,
            light.ColorAndIntensity.z,
            0.42f
        };
        constants.CameraRight = cameraRightFloat;
        constants.CameraUp = cameraUpFloat;
//Modify Begin:2026-07-30 by Hui
        constants.TypeAndParams = { 1.0f, 0.0f, 0.0f, 0.0f };
//Modify End

        commandContext.SetConstantBuffer(*m_LightBillboardShader, "MaterialCBuffer", constants);
        commandContext.BindDescriptorSet(m_LightBillboardShader->GetDescriptorSet());
        m_LightBillboardMesh->Draw(cmd);
    }
}

void RaytracingDemo::DrawPostBloomOverlays(CommandList& cmd)
{
    DrawLightBillboards(cmd);
    if (m_ImGui != nullptr)
    {
        m_ImGui->DrawToRenderTarget(cmd);
    }
}

//Modify End
