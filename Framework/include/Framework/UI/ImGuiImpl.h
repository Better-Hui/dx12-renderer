#pragma once

#include <memory>
//Modify Begin:2026-08-19 by Hui
#include <vector>
//Modify End

#include <imgui.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <DX12Library/Window.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/Shader.h>

//Modify Begin:2026-08-19 by Hui
struct ImGui_ImplDX12_InitInfo;
class FrameworkDeviceContext;
//Modify End

class ImGuiImpl final
{
public:
    constexpr static DXGI_FORMAT BUFFER_FORMAT = Window::BUFFER_FORMAT;

    ImGuiImpl(FrameworkDeviceContext& deviceContext, CommandList& commandList, const Window& window);
    ~ImGuiImpl();

    bool WantsToCaptureMouse() const;
    bool WantsToCaptureKeyboard() const;

    void BeginFrame() const;
    void Render() const;
    void DrawToRenderTarget(CommandList& commandList);
    void BlitCombine(CommandList& commandList, const std::shared_ptr<Texture>& pSourceTexture) const;

private:
//Modify Begin:2026-08-19 by Hui
    static constexpr UINT ImGuiSrvDescriptorCount = 64;

    static void AllocateSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptor,
        D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptor);
    static void FreeSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor);

    FrameworkDeviceContext& m_DeviceContext;
    UINT m_SrvDescriptorSize = 0;
    std::vector<bool> m_FreeSrvDescriptors;
    //Modify End

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvDescHeap;
    std::shared_ptr<Shader> m_CombineShader;
    std::shared_ptr<Mesh> m_BlitMesh;
};
//Modify End
