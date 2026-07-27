#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <Framework/Mesh.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateLightBillboardPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Light Billboards",
        {
            { DemoResourceIds::DenoiseFinishedToken, InputType::Token },
        },
        {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, OutputType::RenderTarget },
            { DemoResourceIds::LightBillboardFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            if (demo.m_LightBillboardShader == nullptr || demo.m_LightBillboardMesh == nullptr)
            {
                return;
            }

            const XMVECTOR cameraRight = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), demo.m_Camera.GetRotation());
            const XMVECTOR cameraUp = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), demo.m_Camera.GetRotation());

            XMFLOAT4 cameraRightFloat{};
            XMFLOAT4 cameraUpFloat{};
            XMStoreFloat4(&cameraRightFloat, XMVectorSetW(cameraRight, 0.0f));
            XMStoreFloat4(&cameraUpFloat, XMVectorSetW(cameraUp, 0.0f));

            demo.m_LightBillboardShader->Bind(cmd);
            demo.m_LightBillboardShader->SetConstantBuffer(cmd, "PipelineCBuffer", demo.BuildPipelineConstants());

            for (const PointLight& light : demo.m_Lights.GetPointLights())
            {
                RaytracingDemo::LightBillboardConstants constants{};
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

                demo.m_LightBillboardShader->SetConstantBuffer(cmd, "MaterialCBuffer", constants);
                demo.m_LightBillboardShader->ApplyBindings(cmd);
                demo.m_LightBillboardMesh->Draw(cmd);
            }

            for (const AreaLightData& light : demo.m_Lights.GetAreaLights())
            {
                RaytracingDemo::LightBillboardConstants constants{};
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

                demo.m_LightBillboardShader->SetConstantBuffer(cmd, "MaterialCBuffer", constants);
                demo.m_LightBillboardShader->ApplyBindings(cmd);
                demo.m_LightBillboardMesh->Draw(cmd);
            }

            demo.m_LightBillboardShader->Unbind(cmd);
        });
}
