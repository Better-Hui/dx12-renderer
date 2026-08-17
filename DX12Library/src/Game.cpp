#include "Game.h"

#include <DirectXMath.h>

#include "Application.h"
#include "Window.h"
#include "Helpers.h"

//Modify Begin:2026-07-30 by Hui
Game::Game(
	Application& application,
	const std::wstring& name,
	const int width,
	const int height,
	const bool vSync)
	: m_Application(application)
	, Name(name)
	, Width(width)
	, Height(height)
	, VSync(vSync)
{
}
//Modify End

Game::~Game()
{
	assert(!PWindow && "Use Game::Destroy() before destruction.");
}

bool Game::Initialize()
{
	// Check for DirectX Math library support.
	if (!DirectX::XMVerifyCPUSupport())
	{
		MessageBoxA(nullptr, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
		return false;
	}

	PWindow = m_Application.CreateRenderWindow(Name, Width, Height, VSync);
	PWindow->RegisterCallbacks(shared_from_this());
	PWindow->Show();

	return true;
}

void Game::Destroy()
{
	m_Application.DestroyWindow(PWindow);
	PWindow.reset();
}

int Game::GetClientWidth() const
{
	return Width;
}

int Game::GetClientHeight() const
{
	return Height;
}


void Game::OnUpdate(UpdateEventArgs& e)
{
	PIXScopeCPU("OnUpdate");
}

void Game::OnRender(RenderEventArgs& e)
{
	PIXScopeCPU("OnRender");
}

void Game::OnKeyPressed(KeyEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnKeyReleased(KeyEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnMouseMoved(class MouseMotionEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnMouseButtonPressed(MouseButtonEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnMouseButtonReleased(MouseButtonEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnMouseWheel(MouseWheelEventArgs& e)
{
	// By default, do nothing.
}

void Game::OnResize(ResizeEventArgs& e)
{
	Width = e.Width;
	Height = e.Height;
}

//Modify Begin:2026-08-07 by Hui
void Game::OnWindowDestroy()
{
	// Application::Run flushes the queues and unloads content after the message loop exits.
}
//Modify End
