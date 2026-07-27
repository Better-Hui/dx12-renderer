#include "ImGuiImpl.h"

#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>

#include <Framework/Blit_VS.h>
#include <Framework/ImGuiCombine_PS.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    void AddInputCharacter(const unsigned int c)
    {
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned short>(c));
    }
}

//Modify Begin:2026-07-21 by BestHui
void ImGuiImpl::AllocateSrvDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptor)
{
    auto* self = static_cast<ImGuiImpl*>(info->UserData);
    Assert(self != nullptr, "ImGui descriptor allocator is missing user data.");

    for (UINT i = 0; i < static_cast<UINT>(self->m_FreeSrvDescriptors.size()); ++i)
    {
        if (!self->m_FreeSrvDescriptors[i])
        {
            continue;
        }

        self->m_FreeSrvDescriptors[i] = false;

        const auto cpuStart = self->m_SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
        const auto gpuStart = self->m_SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
        outCpuDescriptor->ptr = cpuStart.ptr + static_cast<SIZE_T>(i) * self->m_SrvDescriptorSize;
        outGpuDescriptor->ptr = gpuStart.ptr + static_cast<UINT64>(i) * self->m_SrvDescriptorSize;
        return;
    }

    Assert(false, "ImGui SRV descriptor heap is full.");
}

void ImGuiImpl::FreeSrvDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor,
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor)
{
    (void)gpuDescriptor;

    auto* self = static_cast<ImGuiImpl*>(info->UserData);
    Assert(self != nullptr, "ImGui descriptor allocator is missing user data.");

    const auto cpuStart = self->m_SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    const auto offset = cpuDescriptor.ptr - cpuStart.ptr;
    const auto index = static_cast<UINT>(offset / self->m_SrvDescriptorSize);
    Assert(index < self->m_FreeSrvDescriptors.size(), "ImGui SRV descriptor does not belong to this heap.");
    self->m_FreeSrvDescriptors[index] = true;
}
//Modify End

ImGuiImpl::ImGuiImpl(CommandList& commandList, const Window& window)
    //Modify Begin:2026-07-21 by BestHui
    : m_FreeSrvDescriptors(ImGuiSrvDescriptorCount, true)
    //Modify End
{
//Modify Begin:2026-07-21 by BestHui
//Modify Begin:2026-07-27 by BestHui
    m_CombineShader = std::make_shared<Shader>(
        ShaderBlob(ShaderBytecode_Blit_VS, sizeof ShaderBytecode_Blit_VS),
        ShaderBlob(ShaderBytecode_ImGuiCombine_PS, sizeof ShaderBytecode_ImGuiCombine_PS),
        [](RasterPipelineStateBuilder& psb)
        {
            psb.WithAlphaBlend();
        }
    );
//Modify End

    m_BlitMesh = Mesh::CreateBlitTriangle(commandList);
//Modify End

    const auto pDevice = Application::Get().GetDevice();

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        //Modify Begin:2026-07-21 by BestHui
        desc.NumDescriptors = ImGuiSrvDescriptorCount;
        //Modify End
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SrvDescHeap)));
    }
    //Modify Begin:2026-07-21 by BestHui
    m_SrvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    //Modify End

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    //Modify Begin:2026-07-21 by BestHui
    ImGuiStyle& style = ImGui::GetStyle();
    io.FontGlobalScale = 1.25f;
    style.ScaleAllSizes(1.12f);
    style.Colors[ImGuiCol_WindowBg].w = 0.38f;
    style.Colors[ImGuiCol_ChildBg].w = 0.24f;
    style.Colors[ImGuiCol_PopupBg].w = 0.82f;
    style.Colors[ImGuiCol_TitleBg].w = 0.62f;
    style.Colors[ImGuiCol_TitleBgActive].w = 0.72f;
    style.Colors[ImGuiCol_FrameBg].w = 0.48f;
    style.Colors[ImGuiCol_FrameBgHovered].w = 0.62f;
    style.Colors[ImGuiCol_FrameBgActive].w = 0.70f;
    //Modify End

    ImGui_ImplWin32_Init(window.GetWindowHandle());
    //Modify Begin:2026-07-21 by BestHui
    const auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetD3D12CommandQueue();
    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = pDevice.Get();
    initInfo.CommandQueue = commandQueue.Get();
    initInfo.NumFramesInFlight = Window::BUFFER_COUNT;
    initInfo.RTVFormat = BUFFER_FORMAT;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = m_SrvDescHeap.Get();
    initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;
    Assert(ImGui_ImplDX12_Init(&initInfo), "Failed to initialize ImGui DX12 backend.");
    //Modify End

    Application::AddWndProcHandler(ImGui_ImplWin32_WndProcHandler);
    Application::AddKeyDownListener(AddInputCharacter);
}

ImGuiImpl::~ImGuiImpl()
{
    Application::RemoveWndProcHandler(ImGui_ImplWin32_WndProcHandler);
    Application::RemoveKeyDownListener(AddInputCharacter);
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool ImGuiImpl::WantsToCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiImpl::WantsToCaptureKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiImpl::BeginFrame() const
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiImpl::Render() const
{
    ImGui::Render();
}

void ImGuiImpl::DrawToRenderTarget(CommandList& commandList)
{
    commandList.CommitStagedDescriptors();
    commandList.FlushResourceBarriers();

    const auto pD3dCmd = commandList.GetGraphicsCommandList();
    pD3dCmd->SetDescriptorHeaps(1, m_SrvDescHeap.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pD3dCmd.Get());
}

void ImGuiImpl::BlitCombine(CommandList& commandList, const std::shared_ptr<Texture>& pSourceTexture) const
{
    m_CombineShader->Bind(commandList);
    m_CombineShader->SetTexture(commandList, "source", ShaderResourceView(pSourceTexture));
    m_CombineShader->ApplyBindings(commandList);
    m_BlitMesh->Draw(commandList);
}
