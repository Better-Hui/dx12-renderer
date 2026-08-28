#include "DX12LibPCH.h"

#include "Window.h"

#include "CommandQueue.h"
#include "CommandList.h"
//Modify Begin:2026-08-24 by Hui
#include "CommandListInternalAccess.h"
//Modify End
//Modify Begin:2026-08-19 by Hui
#include "D3D12DeviceContext.h"
//Modify End
#include "Game.h"
#include "RenderTarget.h"
#include "ResourceStateTracker.h"
#include "Texture.h"
#include "Helpers.h"

//Modify Begin:2026-08-19 by Hui
#include <cwchar>
#include <algorithm>
#include <utility>
//Modify End
//Modify Begin:2026-08-28 by Hui
#include <cmath>
#include <stdexcept>
//Modify End

//Modify Begin:2026-08-28 by Hui
namespace
{
    constexpr DXGI_COLOR_SPACE_TYPE Hdr10ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

    uint16_t ToChromaticity(const float value)
    {
        return static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 50000.0f));
    }

    DXGI_HDR_METADATA_HDR10 BuildHdr10Metadata(const Hdr10OutputCapabilities& capabilities)
    {
        const float maxLuminance = std::clamp(capabilities.MaxLuminanceNits, 200.0f, 10000.0f);
        const float fullFrameLuminance = std::clamp(
            capabilities.MaxFullFrameLuminanceNits > 0.0f
                ? capabilities.MaxFullFrameLuminanceNits
                : maxLuminance,
            50.0f,
            maxLuminance);
        DXGI_HDR_METADATA_HDR10 metadata{};
        metadata.RedPrimary[0] = ToChromaticity(0.708f);
        metadata.RedPrimary[1] = ToChromaticity(0.292f);
        metadata.GreenPrimary[0] = ToChromaticity(0.170f);
        metadata.GreenPrimary[1] = ToChromaticity(0.797f);
        metadata.BluePrimary[0] = ToChromaticity(0.131f);
        metadata.BluePrimary[1] = ToChromaticity(0.046f);
        metadata.WhitePoint[0] = ToChromaticity(0.3127f);
        metadata.WhitePoint[1] = ToChromaticity(0.3290f);
        metadata.MaxMasteringLuminance = static_cast<UINT>(std::lround(maxLuminance));
        metadata.MinMasteringLuminance = 1u;
        metadata.MaxContentLightLevel = static_cast<UINT16>(std::lround(maxLuminance));
        metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(std::lround(fullFrameLuminance));
        return metadata;
    }
}
//Modify End

//Modify Begin:2026-08-19 by Hui
Window::Window(
	HWND hWnd,
	const std::wstring& windowName,
	const int clientWidth,
	const int clientHeight,
	const bool vSync,
	WindowD3D12Context d3d12Context)
//Modify End
	: HWnd(hWnd)
	, WindowName(windowName)
	, ClientWidth(clientWidth)
	, ClientHeight(clientHeight)
	, VSync(vSync)
	, Fullscreen(false)
	, m_D3d12Context(std::move(d3d12Context))
{
//Modify Begin:2026-08-19 by Hui
	Assert(m_D3d12Context.DeviceContext != nullptr, "Window requires a D3D12 device context.");
	Assert(m_D3d12Context.DirectCommandQueue != nullptr, "Window requires a direct command queue.");
	Assert(m_D3d12Context.ComputeCommandQueue != nullptr, "Window requires a compute command queue.");
	Assert(m_D3d12Context.CopyCommandQueue != nullptr, "Window requires a copy command queue.");
	IsTearingSupported = m_D3d12Context.IsTearingSupported;
//Modify End

	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
//Modify Begin:2026-08-19 by Hui
		BackBufferTextures[i] = std::make_shared<Texture>(
			TextureUsageType::Other,
			L"",
			m_D3d12Context.DeviceContext);
//Modify End
		BackBufferTextures[i]->SetName(L"Backbuffer[" + std::to_wstring(i) + L"]");
	}

	DxgiSwapChain = CreateSwapChain();
//Modify Begin:2026-08-28 by Hui
    RefreshHdr10OutputCapabilities();
//Modify End
//Modify Begin:2026-08-19 by Hui
	FrameResources.Reset(BUFFER_COUNT);
	FrameResources.SetCurrentIndex(CurrentBackBufferIndex);
//Modify End
	UpdateRenderTargetViews();
}

Window::~Window()
{
	// Window should be destroyed with Application::DestroyWindow before
	// the window goes out of scope.
	assert(!HWnd && "Use Application::DestroyWindow before destruction.");
}

void Window::Initialize()
{
}


HWND Window::GetWindowHandle() const
{
	return HWnd;
}

const std::wstring& Window::GetWindowName() const
{
	return WindowName;
}

void Window::Show()
{
	ShowWindow(HWnd, SW_SHOW);
}

/**
* Hide the window.
*/
void Window::Hide()
{
	ShowWindow(HWnd, SW_HIDE);
}

void Window::Destroy()
{
	if (auto pGame = PGame.lock())
	{
		// Notify the registered game that the window is being destroyed.
		pGame->OnWindowDestroy();
	}

	if (HWnd)
	{
		DestroyWindow(HWnd);
		HWnd = nullptr;
	}
}

int Window::GetClientWidth() const
{
	return ClientWidth;
}

int Window::GetClientHeight() const
{
	return ClientHeight;
}

bool Window::IsVSync() const
{
	return VSync;
}

void Window::SetVSync(bool vSync)
{
	VSync = vSync;
}

void Window::ToggleVSync()
{
	SetVSync(!VSync);
}

bool Window::IsFullScreen() const
{
	return Fullscreen;
}

// Set the fullscreen state of the window.
void Window::SetFullscreen(bool fullscreen)
{
	if (Fullscreen != fullscreen)
	{
		Fullscreen = fullscreen;

		if (Fullscreen) // Switching to fullscreen.
		{
			// Store the current window dimensions so they can be restored
			// when switching out of fullscreen state.
			GetWindowRect(HWnd, &WindowRect);

			// Set the window style to a borderless window so the client area fills
			// the entire screen.
			const UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX |
				WS_MAXIMIZEBOX);

			SetWindowLongW(HWnd, GWL_STYLE, windowStyle);

			// Query the name of the nearest display device for the window.
			// This is required to set the fullscreen dimensions of the window
			// when using a multi-monitor setup.
			const HMONITOR hMonitor = MonitorFromWindow(HWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEX monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			::GetMonitorInfo(hMonitor, &monitorInfo);

			SetWindowPos(HWnd, HWND_TOP,
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			ShowWindow(HWnd, SW_MAXIMIZE);
		}
		else
		{
			// Restore all the window decorators.
			::SetWindowLong(HWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

			SetWindowPos(HWnd, HWND_NOTOPMOST,
				WindowRect.left,
				WindowRect.top,
				WindowRect.right - WindowRect.left,
				WindowRect.bottom - WindowRect.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			ShowWindow(HWnd, SW_NORMAL);
		}
	}
}

void Window::ToggleFullscreen()
{
	SetFullscreen(!Fullscreen);
}


void Window::RegisterCallbacks(std::shared_ptr<Game> pGame)
{
	PGame = pGame;
}

void Window::OnUpdate(UpdateEventArgs& e)
{
	UpdateClock.Tick();

	if (auto pGame = PGame.lock())
	{
		UpdateEventArgs updateEventArgs(UpdateClock.GetDeltaSeconds(), UpdateClock.GetTotalSeconds(),
			e.FrameNumber);
		pGame->OnUpdate(updateEventArgs);
	}
}

void Window::OnRender(RenderEventArgs& e)
{
	RenderClock.Tick();

	if (auto pGame = PGame.lock())
	{
		RenderEventArgs renderEventArgs(RenderClock.GetDeltaSeconds(), RenderClock.GetTotalSeconds(),
			e.FrameNumber);
		pGame->OnRender(renderEventArgs);
//Modify Begin:2026-08-19 by Hui
		UpdateFrameStatistics(renderEventArgs.ElapsedTime);
//Modify End
	}
}

//Modify Begin:2026-08-19 by Hui
void Window::SetProfilerDisplayRefreshIntervalSeconds(const double refreshIntervalSeconds)
{
	ProfilerDisplayRefreshIntervalSeconds = (std::max)(0.1, refreshIntervalSeconds);
	FrameStatisticsElapsedSeconds = 0.0;
	FrameStatisticsCount = 0;
}

void Window::UpdateFrameStatistics(const double elapsedSeconds)
{
	FrameStatisticsElapsedSeconds += elapsedSeconds;
	++FrameStatisticsCount;

	if (FrameStatisticsElapsedSeconds < ProfilerDisplayRefreshIntervalSeconds)
	{
		return;
	}

	FramesPerSecond = static_cast<double>(FrameStatisticsCount) / FrameStatisticsElapsedSeconds;
	FrameMilliseconds = FramesPerSecond > 0.0 ? 1000.0 / FramesPerSecond : 0.0;

	wchar_t title[256] = {};
	const int written = swprintf_s(
		title,
		_countof(title),
		L"%s - %.1f FPS (%.2f ms)",
		WindowName.c_str(),
		FramesPerSecond,
		FrameMilliseconds);
	if (written > 0)
	{
		SetWindowTextW(HWnd, title);
	}

	FrameStatisticsElapsedSeconds = 0.0;
	FrameStatisticsCount = 0;
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void Window::BeginFrame(const uint64_t frameNumber)
{
	m_D3d12Context.DeviceContext->SetDescriptorRetirementFrame(frameNumber);
}
//Modify End

void Window::OnKeyPressed(KeyEventArgs& e)
{
	if (auto pGame = PGame.lock())
	{
		pGame->OnKeyPressed(e);
	}
}

void Window::OnKeyReleased(KeyEventArgs& e)
{
	if (auto pGame = PGame.lock())
	{
		pGame->OnKeyReleased(e);
	}
}

// The mouse was moved
void Window::OnMouseMoved(MouseMotionEventArgs& e)
{
	e.RelX = e.X - PreviousMouseX;
	e.RelY = e.Y - PreviousMouseY;

	PreviousMouseX = e.X;
	PreviousMouseY = e.Y;

	if (auto pGame = PGame.lock())
	{
		pGame->OnMouseMoved(e);
	}
}

// A button on the mouse was pressed
void Window::OnMouseButtonPressed(MouseButtonEventArgs& e)
{
	PreviousMouseX = e.X;
	PreviousMouseY = e.Y;

	if (auto pGame = PGame.lock())
	{
		pGame->OnMouseButtonPressed(e);
	}
}

// A button on the mouse was released
void Window::OnMouseButtonReleased(MouseButtonEventArgs& e)
{
	if (auto pGame = PGame.lock())
	{
		pGame->OnMouseButtonReleased(e);
	}
}

// The mouse wheel was moved.
void Window::OnMouseWheel(MouseWheelEventArgs& e)
{
	if (auto pGame = PGame.lock())
	{
		pGame->OnMouseWheel(e);
	}
}

void Window::OnResize(ResizeEventArgs& e)
{
//Modify Begin:2026-08-19 by Hui
	if (e.Width <= 0 || e.Height <= 0)
	{
		return;
	}
	//Modify End

	// Update the client size.
	if (ClientWidth != e.Width || ClientHeight != e.Height)
	{
		ClientWidth = std::max(1, e.Width);
		ClientHeight = std::max(1, e.Height);

		m_D3d12Context.DirectCommandQueue->Flush();
		m_D3d12Context.ComputeCommandQueue->Flush();
		m_D3d12Context.CopyCommandQueue->Flush();
//Modify End

		// Release all references to back buffer textures.
//Modify Begin:2026-08-19 by Hui
		MRenderTarget.AttachTexture(Color0, std::make_shared<Texture>(
			TextureUsageType::Other,
			L"",
			m_D3d12Context.DeviceContext));
//Modify End
		for (int i = 0; i < BUFFER_COUNT; ++i)
		{
			BackBufferTextures[i]->Reset();
		}

		DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
		ThrowIfFailed(DxgiSwapChain->GetDesc(&swapChainDesc));
		ThrowIfFailed(DxgiSwapChain->ResizeBuffers(BUFFER_COUNT, ClientWidth,
			ClientHeight, swapChainDesc.BufferDesc.Format,
			swapChainDesc.Flags));

		CurrentBackBufferIndex = DxgiSwapChain->GetCurrentBackBufferIndex();
//Modify Begin:2026-08-19 by Hui
		FrameResources.Reset(BUFFER_COUNT);
		FrameResources.SetCurrentIndex(CurrentBackBufferIndex);
//Modify End

		UpdateRenderTargetViews();
	}

	if (auto pGame = PGame.lock())
	{
		pGame->OnResize(e);
	}
}

void Window::ReleaseSwapChainResources()
{
//Modify Begin:2026-08-19 by Hui
	MRenderTarget.AttachTexture(Color0, std::make_shared<Texture>(
		TextureUsageType::Other,
		L"",
		m_D3d12Context.DeviceContext));
//Modify End
	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		BackBufferTextures[i]->Reset();
	}
	DxgiSwapChain.Reset();
}

void Window::RecreateSwapChain()
{
	DxgiSwapChain = CreateSwapChain();
//Modify Begin:2026-08-28 by Hui
    RefreshHdr10OutputCapabilities();
//Modify End
	FrameResources.Reset(BUFFER_COUNT);
	FrameResources.SetCurrentIndex(CurrentBackBufferIndex);
	UpdateRenderTargetViews();
}

ComPtr<IDXGISwapChain4> Window::CreateSwapChain()
{
//Modify Begin:2026-08-28 by Hui
	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = ClientWidth;
	swapChainDesc.Height = ClientHeight;
    const bool useHdr10 = m_Hdr10OutputRequested && m_Hdr10OutputCapabilities.IsSupported;
	swapChainDesc.Format = useHdr10 ? DXGI_FORMAT_R10G10B10A2_UNORM : BUFFER_FORMAT;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = BUFFER_COUNT;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	// It is recommended to always allow tearing if tearing support is available.
	swapChainDesc.Flags = IsTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	ID3D12CommandQueue* pCommandQueue = m_D3d12Context.DirectCommandQueue->GetD3D12CommandQueue().Get();

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		pCommandQueue,
		HWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1));

	// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
	// will be handled manually.
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(HWnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

    if (useHdr10)
    {
        UINT colorSpaceSupport = 0u;
        ThrowIfFailed(dxgiSwapChain4->CheckColorSpaceSupport(Hdr10ColorSpace, &colorSpaceSupport));
        if ((colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0u)
        {
            throw std::runtime_error("The current output rejected the HDR10/PQ swapchain color space.");
        }

        ThrowIfFailed(dxgiSwapChain4->SetColorSpace1(Hdr10ColorSpace));
        DXGI_HDR_METADATA_HDR10 metadata = BuildHdr10Metadata(m_Hdr10OutputCapabilities);
        ThrowIfFailed(dxgiSwapChain4->SetHDRMetaData(
            DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof(metadata),
            &metadata));
    }
    m_Hdr10OutputEnabled = useHdr10;
	CurrentBackBufferIndex = dxgiSwapChain4->GetCurrentBackBufferIndex();

	return dxgiSwapChain4;
//Modify End
}

//Modify Begin:2026-08-28 by Hui
bool Window::PrepareHdr10Output(const bool enabled)
{
    RefreshHdr10OutputCapabilities();
    if (enabled && !m_Hdr10OutputCapabilities.IsSupported)
    {
        return false;
    }

    m_Hdr10OutputRequested = enabled;
    return true;
}

void Window::RefreshHdr10OutputCapabilities()
{
    m_Hdr10OutputCapabilities = {};
    if (DxgiSwapChain == nullptr)
    {
        return;
    }

    ComPtr<IDXGIOutput> output;
    if (FAILED(DxgiSwapChain->GetContainingOutput(&output)) || output == nullptr)
    {
        return;
    }

    ComPtr<IDXGIOutput6> output6;
    if (FAILED(output.As(&output6)) || output6 == nullptr)
    {
        return;
    }

    DXGI_OUTPUT_DESC1 outputDesc{};
    if (FAILED(output6->GetDesc1(&outputDesc)))
    {
        return;
    }

    m_Hdr10OutputCapabilities.MaxLuminanceNits = outputDesc.MaxLuminance;
    m_Hdr10OutputCapabilities.MaxFullFrameLuminanceNits = outputDesc.MaxFullFrameLuminance;
    m_Hdr10OutputCapabilities.IsSupported =
        outputDesc.BitsPerColor >= 10u &&
        outputDesc.ColorSpace == Hdr10ColorSpace &&
        outputDesc.MaxLuminance > 0.0f;
}
//Modify End

void Window::UpdateRenderTargetViews()
{
	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(DxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		BackBufferTextures[i]->SetD3D12Resource(backBuffer);
		BackBufferTextures[i]->CreateViews();
	}
}

const RenderTarget& Window::GetRenderTarget() const
{
	MRenderTarget.AttachTexture(Color0, BackBufferTextures[CurrentBackBufferIndex]);
	return MRenderTarget;
}

//Modify Begin:2026-08-24 by Hui
void Window::PrepareBackBufferForRenderTarget(CommandList& commandList) const
{
    CommandListInternalAccess::TransitionBarrier(
        commandList,
        *BackBufferTextures[CurrentBackBufferIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        true);
}

void Window::PrepareBackBufferForCopyDestination(CommandList& commandList) const
{
    CommandListInternalAccess::TransitionBarrier(
        commandList,
        *BackBufferTextures[CurrentBackBufferIndex],
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        true);
}

void Window::PrepareBackBufferForResolveDestination(CommandList& commandList) const
{
    CommandListInternalAccess::TransitionBarrier(
        commandList,
        *BackBufferTextures[CurrentBackBufferIndex],
        D3D12_RESOURCE_STATE_RESOLVE_DEST,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        true);
}
//Modify End

UINT Window::Present(const Texture& texture)
{
	auto commandQueue = m_D3d12Context.DirectCommandQueue;
	auto commandList = commandQueue->GetCommandList();

	PIXScopeCPU("Present");

	{
		PIXScope(*commandList, "Present");
		auto& backBuffer = BackBufferTextures[CurrentBackBufferIndex];

		if (texture.IsValid())
		{
			if (texture.GetD3D12ResourceDesc().SampleDesc.Count > 1)
			{
				CommandListInternalAccess::TransitionBarrier(
					*commandList,
					texture,
					D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				PrepareBackBufferForResolveDestination(*commandList);
				commandList->ResolveSubresource(*backBuffer, texture);
			}
			else
			{
				CommandListInternalAccess::TransitionBarrier(
					*commandList,
					texture,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				PrepareBackBufferForCopyDestination(*commandList);
				commandList->CopyResource(*backBuffer, texture);
			}
		}

//Modify Begin:2026-08-24 by Hui
		CommandListInternalAccess::TransitionBarrier(
			*commandList,
			*backBuffer,
			D3D12_RESOURCE_STATE_PRESENT);
//Modify End
	}

	commandQueue->ExecuteCommandList(commandList);

	UINT syncInterval = VSync ? 1 : 0;
	UINT presentFlags = IsTearingSupported && !VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(DxgiSwapChain->Present(syncInterval, presentFlags));

//Modify Begin:2026-08-19 by Hui
	FrameResources.MarkSubmitted(
		CurrentBackBufferIndex,
		commandQueue->Signal(),
		m_D3d12Context.DeviceContext->GetDescriptorRetirementFrame());
	CurrentBackBufferIndex = DxgiSwapChain->GetCurrentBackBufferIndex();
	FrameResources.SetCurrentIndex(CurrentBackBufferIndex);
	const uint64_t reusableFrame = FrameResources.WaitForSlot(*commandQueue, CurrentBackBufferIndex);
	m_D3d12Context.DeviceContext->ReleaseStaleDescriptors(reusableFrame);
//Modify End

	return CurrentBackBufferIndex;
}
