#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/CommandContext.h>
#include <Framework/ImGuiImpl.h>
#include <Framework/Mesh.h>

#include <algorithm>

using namespace DirectX;

//Modify Begin:2026-07-27 by BestHui
void RaytracingDemo::DrawLightBillboards(CommandList& cmd)
{
    if (m_LightBillboardShader == nullptr || m_LightBillboardMesh == nullptr)
    {
        return;
    }

    const XMVECTOR cameraRight = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), m_Camera.GetRotation());
    const XMVECTOR cameraUp = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), m_Camera.GetRotation());

    XMFLOAT4 cameraRightFloat{};
    XMFLOAT4 cameraUpFloat{};
    XMStoreFloat4(&cameraRightFloat, XMVectorSetW(cameraRight, 0.0f));
    XMStoreFloat4(&cameraUpFloat, XMVectorSetW(cameraUp, 0.0f));

    m_LightBillboardShader->Bind(cmd);
    cmd.SetConstantBuffer(m_LightBillboardShader, "PipelineCBuffer", BuildPipelineConstants());

    for (const PointLight& light : m_Lights.GetPointLights())
    {
        LightBillboardConstants constants{};
        constants.PositionAndSize = {
            light.PositionWs.x,
            light.PositionWs.y,
            light.PositionWs.z,
            std::clamp(light.Range * 0.055f, 0.85f, 2.0f)
        };
        constants.ColorAndAlpha = {
            light.Color.x,
            light.Color.y,
            light.Color.z,
            0.48f
        };
        constants.CameraRight = cameraRightFloat;
        constants.CameraUp = cameraUpFloat;

        cmd.SetConstantBuffer(m_LightBillboardShader, "MaterialCBuffer", constants);
//Modify Begin:2026-07-29 by BestHui
        CommandContext commandContext(cmd);
        commandContext.BindDescriptorSet(m_LightBillboardShader->GetDescriptorSet(), PipelineBindPoint::Graphics);
//Modify End
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

        cmd.SetConstantBuffer(m_LightBillboardShader, "MaterialCBuffer", constants);
//Modify Begin:2026-07-29 by BestHui
        CommandContext commandContext(cmd);
        commandContext.BindDescriptorSet(m_LightBillboardShader->GetDescriptorSet(), PipelineBindPoint::Graphics);
//Modify End
        m_LightBillboardMesh->Draw(cmd);
    }

    m_LightBillboardShader->Unbind(cmd);
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
