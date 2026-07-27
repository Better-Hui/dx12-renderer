#pragma once

#include <memory>
//Modify Begin:2026-07-21 by BestHui
#include <vector>
//Modify End

#include <imgui.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <DX12Library/Window.h>

#include <Framework/Mesh.h>
#include <Framework/Shader.h>

//Modify Begin:2026-07-21 by BestHui
struct ImGui_ImplDX12_InitInfo;
//Modify End

class ImGuiImpl final
{
public:
    constexpr static DXGI_FORMAT BUFFER_FORMAT = Window::BUFFER_FORMAT;

    ImGuiImpl(CommandList& commandList, const Window& window);
    ~ImGuiImpl();

    bool WantsToCaptureMouse() const;
    bool WantsToCaptureKeyboard() const;

    void BeginFrame() const;
    void Render() const;
    void DrawToRenderTarget(CommandList& commandList);
    void BlitCombine(CommandList& commandList, const std::shared_ptr<Texture>& pSourceTexture) const;

private:
    //Modify Begin:2026-07-21 by BestHui
    static constexpr UINT ImGuiSrvDescriptorCount = 64;

    static void AllocateSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptor,
        D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptor);
    static void FreeSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor);

    UINT m_SrvDescriptorSize = 0;
    std::vector<bool> m_FreeSrvDescriptors;
    //Modify End

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvDescHeap;
    std::shared_ptr<Shader> m_CombineShader;
    std::shared_ptr<Mesh> m_BlitMesh;
};
