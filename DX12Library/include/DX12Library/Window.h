#pragma once

/*
 *  Copyright(c) 2018 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

 /**
  *  @file Window.h
  *  @date October 24, 2018
  *  @author Jeremiah van Oosten
  *
  *  @brief A window for our application.
  */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "Events.h"
//Modify Begin:2026-07-28 by Hui
#include "FrameResourceRing.h"
//Modify End
#include "HighResolutionClock.h"
#include "RenderTarget.h"
#include "Texture.h"

#include <cstdint>
#include <memory>

class Game;
class Texture;
//Modify Begin:2026-08-12 by Hui
class CommandList;
class CommandQueue;
class D3D12DeviceContext;
//Modify End

//Modify Begin:2026-08-12 by Hui
struct WindowD3D12Context
{
    std::shared_ptr<D3D12DeviceContext> DeviceContext;
    std::shared_ptr<CommandQueue> DirectCommandQueue;
    std::shared_ptr<CommandQueue> ComputeCommandQueue;
    std::shared_ptr<CommandQueue> CopyCommandQueue;
    bool IsTearingSupported = false;
};
//Modify End

//Modify Begin:2026-08-28 by Hui
struct Hdr10OutputCapabilities
{
    bool IsSupported = false;
    float MaxLuminanceNits = 0.0f;
    float MaxFullFrameLuminanceNits = 0.0f;
};
//Modify End

class Window : public std::enable_shared_from_this<Window>
{
public:
	// Number of swapchain back buffers.
	static constexpr UINT BUFFER_COUNT = 3;
    static constexpr DXGI_FORMAT BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT BUFFER_FORMAT_SRGB = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	/**
	* Get a handle to this window's instance.
	* @returns The handle to the window instance or nullptr if this is not a valid window.
	*/
	HWND GetWindowHandle() const;

	/**
	 * Initialize the window.
	 */
	void Initialize();

	/**
	* Destroy this window.
	*/
	void Destroy();

	const std::wstring& GetWindowName() const;
//Modify Begin:2026-08-18 by Hui
	double GetFramesPerSecond() const { return FramesPerSecond; }
	double GetFrameMilliseconds() const { return FrameMilliseconds; }
	double GetProfilerDisplayRefreshIntervalSeconds() const { return ProfilerDisplayRefreshIntervalSeconds; }
	void SetProfilerDisplayRefreshIntervalSeconds(double refreshIntervalSeconds);
//Modify End

	int GetClientWidth() const;
	int GetClientHeight() const;

	/**
	* Should this window be rendered with vertical refresh synchronization.
	*/
	bool IsVSync() const;
	void SetVSync(bool vSync);
	void ToggleVSync();

	/**
	* Is this a windowed window or full-screen?
	*/
	bool IsFullScreen() const;

	// Set the fullscreen state of the window.
	void SetFullscreen(bool fullscreen);
	void ToggleFullscreen();

	/**
	 * Show this window.
	 */
	void Show();

	/**
	 * Hide the window.
	 */
	void Hide();

	/**
	 * Get the render target of the window. This method should be called every
	 * frame since the color attachment point changes depending on the window's
	 * current back buffer.
	 */
	const RenderTarget& GetRenderTarget() const;

//Modify Begin:2026-08-28 by Hui
    [[nodiscard]] const Hdr10OutputCapabilities& GetHdr10OutputCapabilities() const { return m_Hdr10OutputCapabilities; }
    [[nodiscard]] bool IsHdr10OutputEnabled() const { return m_Hdr10OutputEnabled; }
    [[nodiscard]] DXGI_FORMAT GetBackBufferFormat() const
    {
        return m_Hdr10OutputEnabled ? DXGI_FORMAT_R10G10B10A2_UNORM : BUFFER_FORMAT;
    }
//Modify End

//Modify Begin:2026-08-24 by Hui
    void PrepareBackBufferForRenderTarget(CommandList& commandList) const;
    void PrepareBackBufferForCopyDestination(CommandList& commandList) const;
    void PrepareBackBufferForResolveDestination(CommandList& commandList) const;
//Modify End

	/**
	 * Present the swapchain's back buffer to the screen.
	 * Returns the current back buffer index after the present.
	 *
	 * @param texture The texture to copy to the swap chain's backbuffer before
	 * presenting. By default, this is an empty texture. In this case, no copy
	 * will be performed. Use the Window::GetRenderTarget method to get a render
	 * target for the window's color buffer.
	 */
	UINT Present(const Texture& texture = Texture());

protected:
	// The Window procedure needs to call protected methods of this class.
	friend LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Only the application can create a window.
	friend class Application;
	// The DirectXTemplate class needs to register itself with a window.
	friend class Game;

	Window() = delete;
//Modify Begin:2026-08-12 by Hui
	Window(
		HWND hWnd,
		const std::wstring& windowName,
		int clientWidth,
		int clientHeight,
		bool vSync,
		WindowD3D12Context d3d12Context);
//Modify End
	virtual ~Window();

	// Register a Game with this window. This allows
	// the window to callback functions in the Game class.
	void RegisterCallbacks(std::shared_ptr<Game> pGame);

	// Update and Draw can only be called by the application.
	virtual void OnUpdate(UpdateEventArgs& e);
	virtual void OnRender(RenderEventArgs& e);

	// A keyboard key was pressed
	virtual void OnKeyPressed(KeyEventArgs& e);
	// A keyboard key was released
	virtual void OnKeyReleased(KeyEventArgs& e);

	// The mouse was moved
	virtual void OnMouseMoved(MouseMotionEventArgs& e);
	// A button on the mouse was pressed
	virtual void OnMouseButtonPressed(MouseButtonEventArgs& e);
	// A button on the mouse was released
	virtual void OnMouseButtonReleased(MouseButtonEventArgs& e);
	// The mouse wheel was moved.
	virtual void OnMouseWheel(MouseWheelEventArgs& e);

	// The window was resized.
	virtual void OnResize(ResizeEventArgs& e);

	// Create the swapchain.
	Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain();
	void ReleaseSwapChainResources();
	void RecreateSwapChain();
//Modify Begin:2026-08-28 by Hui
    bool PrepareHdr10Output(bool enabled);
    void RefreshHdr10OutputCapabilities();
//Modify End

	// Update the render target views for the swapchain back buffers.
	void UpdateRenderTargetViews();
//Modify Begin:2026-07-21 by Hui
	void UpdateFrameStatistics(double elapsedSeconds);
//Modify End

//Modify Begin:2026-08-12 by Hui
	void BeginFrame(uint64_t frameNumber);
//Modify End

private:
	// Windows should not be copied.
	Window(const Window& copy) = delete;
	Window& operator=(const Window& other) = delete;

	HWND HWnd;

	std::wstring WindowName;

	int ClientWidth;
	int ClientHeight;
	bool VSync;
	bool Fullscreen;

	HighResolutionClock UpdateClock;
	HighResolutionClock RenderClock;
//Modify Begin:2026-08-18 by Hui
	double FrameStatisticsElapsedSeconds = 0.0;
	uint32_t FrameStatisticsCount = 0;
	double ProfilerDisplayRefreshIntervalSeconds = 1.0;
	double FramesPerSecond = 0.0;
	double FrameMilliseconds = 0.0;
//Modify End

//Modify Begin:2026-07-28 by Hui
	FrameResourceRing FrameResources;
//Modify End

	std::weak_ptr<Game> PGame;

	Microsoft::WRL::ComPtr<IDXGISwapChain4> DxgiSwapChain;
//Modify Begin:2026-08-28 by Hui
    Hdr10OutputCapabilities m_Hdr10OutputCapabilities;
    bool m_Hdr10OutputRequested = false;
    bool m_Hdr10OutputEnabled = false;
//Modify End
	std::shared_ptr<Texture> BackBufferTextures[BUFFER_COUNT];
	// Marked mutable to allow modification in a const function.
	mutable RenderTarget MRenderTarget;

	UINT CurrentBackBufferIndex;

	RECT WindowRect;
	bool IsTearingSupported;

//Modify Begin:2026-08-12 by Hui
	WindowD3D12Context m_D3d12Context;
//Modify End

	int PreviousMouseX;
	int PreviousMouseY;
};
