// ReSharper disable CppRedundantQualifier
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
  *  @file Application.h
  *  @date October 22, 2018
  *  @author Jeremiah van Oosten
  *
  *  @brief The application class is used to create windows for our application.
  */

#include "DescriptorAllocation.h"
//Modify Begin:2026-07-28 by Hui
#include "D3D12RenderContext.h"
//Modify End
//Modify Begin:2026-08-07 by Hui
#include "PresentationController.h"
//Modify End

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>


class CommandQueue;
class D3D12DeviceContext;
class D3D12RuntimeLifecycle;
class DiagnosticReporter;
//Modify Begin:2026-08-21 by Hui
class DiagnosticTelemetrySink;
struct DiagnosticTelemetryEvent;
//Modify End
class DescriptorAllocator;
class Game;
class ResourceStateRegistry;
class Window;

//Modify Begin:2026-08-07 by Hui
struct ApplicationCreateDesc
{
    std::shared_ptr<D3D12RuntimeLifecycle> RuntimeLifecycle;
    std::filesystem::path DiagnosticsDirectory;
};
//Modify End

class Application : public PresentationController
{
public:
    /**
    * Create the application singleton with the application instance handle.
    */
    static void Create(HINSTANCE hInst);
//Modify Begin:2026-08-07 by Hui
    static void Create(HINSTANCE hInst, const ApplicationCreateDesc& createDesc);
//Modify End
//Modify Begin:2026-07-21 by Hui
    static void Create(HINSTANCE hInst, const ExternalD3D12Context& externalContext);
//Modify End
//Modify Begin:2026-08-07 by Hui
    static void Create(
        HINSTANCE hInst,
        const ExternalD3D12Context& externalContext,
        const ApplicationCreateDesc& createDesc);
//Modify End

    /**
    * Destroy the application instance and all windows created by this application instance.
    */
    static void Destroy();
    /**
    * Get the application singleton.
    */
    static Application& Get();

    /**
     * Check to see if VSync-off is supported.
     */
    bool IsTearingSupported() const;

    /**
     * Check if the requested multisample quality is supported for the given format.
     */
    DXGI_SAMPLE_DESC GetMultisampleQualityLevels(DXGI_FORMAT format, UINT numSamples,
        D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags =
        D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) const;

    /**
    * Create a new DirectX11 render window instance.
    * @param windowName The name of the window. This name will appear in the title bar of the window. This name should be unique.
    * @param clientWidth The width (in pixels) of the window's client area.
    * @param clientHeight The height (in pixels) of the window's client area.
    * @param vSync Should the rendering be synchronized with the vertical refresh rate of the screen.
    * @param windowed If true, the window will be created in windowed mode. If false, the window will be created full-screen.
    * @returns The created window instance. If an error occurred while creating the window an invalid
    * window instance is returned. If a window with the given name already exists, that window will be
    * returned.
    */
    std::shared_ptr<Window> CreateRenderWindow(const std::wstring& windowName, int clientWidth, int clientHeight,
        bool vSync = true);

    /**
    * Destroy a window given the window name.
    */
    void DestroyWindow(const std::wstring& windowName);
    /**
    * Destroy a window given the window reference.
    */
    void DestroyWindow(std::shared_ptr<Window> window);

    /**
    * Find a window by the window name.
    */
    std::shared_ptr<Window> GetWindowByName(const std::wstring& windowName);

    /**
    * Run the application loop and message pump.
    * @return The error code if an error occurred.
    */
    int Run(std::shared_ptr<Game> pGame);

    /**
    * Request to quit the application and close all windows.
    * @param exitCode The error code to return to the invoking process.
    */
    void Quit(int exitCode = 0);

    /**
     * Get the Direct3D 12 device
     */
    Microsoft::WRL::ComPtr<ID3D12Device2> GetDevice() const;
//Modify Begin:2026-08-07 by Hui
    std::shared_ptr<D3D12DeviceContext> GetD3D12DeviceContext() const;
//Modify End
//Modify Begin:2026-07-21 by Hui
    bool UsesExternalDevice() const;
//Modify End
    /**
     * Get a command queue. Valid types are:
     * - D3D12_COMMAND_LIST_TYPE_DIRECT : Can be used for draw, dispatch, or copy commands.
     * - D3D12_COMMAND_LIST_TYPE_COMPUTE: Can be used for dispatch or copy commands.
     * - D3D12_COMMAND_LIST_TYPE_COPY   : Can be used for copy commands.
     */
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<ResourceStateRegistry> GetResourceStateRegistry() const;
    void SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept;
//Modify End
    bool ReconfigurePresentation(const std::function<bool()>& configureRuntime) override;
//Modify Begin:2026-08-28 by Hui
    bool SetHdr10Output(Window& window, bool enabled);
//Modify End

    /**
     * Flush all command queues.
     */
    void Flush();

    /**
     * Allocate a number of CPU visible descriptors.
     */
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors = 1);

    /**
     * Release stale descriptors. This should only be called with a completed frame counter.
     */
    void ReleaseStaleDescriptors(uint64_t finishedFrame);

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
    UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

    static uint64_t GetFrameCount()
    {
        return s_FrameCount;
    }

    using WndProcHandler = LRESULT (*)(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void AddWndProcHandler(WndProcHandler handler);
    static void RemoveWndProcHandler(WndProcHandler handler);

    using KeyDownListener = void (*)(unsigned int c);
    static void AddKeyDownListener(KeyDownListener listener);
    static void RemoveKeyDownListener(KeyDownListener listener);

protected:
    // Create an application instance.
    Application(HINSTANCE hInst);
//Modify Begin:2026-07-21 by Hui
    Application(HINSTANCE hInst, const ExternalD3D12Context* externalContext);
//Modify End
    // Destroy the application instance and all windows associated with this application.
    virtual ~Application();

    // Initialize the application instance.
//Modify Begin:2026-07-21 by Hui
    void Initialize(const ExternalD3D12Context* externalContext, const ApplicationCreateDesc& createDesc);
//Modify End

    Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp);
    Microsoft::WRL::ComPtr<ID3D12Device2> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);
    bool CheckTearingSupport();
    void WriteDiagnostic(std::string_view reportName, std::string_view contents) const noexcept;
    void RecordDiagnosticTelemetry(DiagnosticTelemetryEvent event) const noexcept;

private:
    friend LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    Application(const Application& copy) = delete;
    Application& operator=(const Application& other) = delete;

    HINSTANCE m_hInstance;

//Modify Begin:2026-08-21 by Hui
    std::unique_ptr<DiagnosticReporter> m_DiagnosticReporter;
    std::atomic<DiagnosticTelemetrySink*> m_DiagnosticTelemetrySink = nullptr;
    std::shared_ptr<D3D12RuntimeLifecycle> m_RuntimeLifecycle;
    D3D12RenderContext m_RenderContext;
    std::atomic<DWORD> m_MessageThreadId = 0;
    std::atomic_bool m_QuitRequested = false;
    std::atomic_int m_RequestedExitCode = 0;
//Modify End

    bool m_TearingSupported;

    static uint64_t s_FrameCount;

    static inline std::vector<WndProcHandler> s_WndProcHandlers = {};
    static inline std::vector<KeyDownListener> s_KeyDownListeners = {};
};
