//Modify Begin:2026-07-28 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <Framework/Mesh.h>
#include <Framework/ShaderResourceView.h>
#include <RenderGraph/RenderPass.h>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateSkyboxPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Skybox",
        {
            { DemoResourceIds::DenoiseFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::SceneColor, OutputType::RenderTarget },
            { DemoResourceIds::DepthBuffer, OutputType::DepthRead },
            { DemoResourceIds::SkyboxFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            const XMMATRIX modelMatrix = XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslationFromVector(demo.m_Camera.GetTranslation());
            RaytracingDemo::ModelConstants modelConstants{};
            modelConstants.Model = modelMatrix;
            modelConstants.ModelViewProjection = modelMatrix * viewProjection;
            modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, modelMatrix));

            demo.m_SkyboxShader->Bind(cmd);
            demo.m_SkyboxShader->SetConstantBuffer(cmd, "ModelCBuffer", modelConstants);
            cmd.SetTexture(demo.m_SkyboxShader, "SkyboxTexture", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
            demo.m_SkyboxShader->ApplyBindings(cmd);
            demo.m_SkyboxMesh->Draw(cmd);
            demo.m_SkyboxShader->Unbind(cmd);
        });
}
//Modify End
