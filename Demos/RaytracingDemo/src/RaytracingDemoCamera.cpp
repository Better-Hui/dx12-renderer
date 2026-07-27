//Modify Begin:2026-07-27 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/Events.h>

#include <algorithm>

using namespace DirectX;

namespace
{
    template<typename T>
    constexpr const T& ClampCameraValue(const T& val, const T& min, const T& max)
    {
        return val < min ? min : val > max ? max : val;
    }
}

void RaytracingDemo::OnUpdate(UpdateEventArgs& e)
{
    Base::OnUpdate(e);
    m_DeltaTime = static_cast<float>(e.ElapsedTime);
    if (m_Lights.IsPointLightAnimationEnabled())
    {
        m_Lights.UpdateDynamicLights(static_cast<float>(e.TotalTime));
        ResetAccumulation(false);
    }

    const float speedMultiplier = m_CameraController.Shift ? 16.0f : 4.0f;
    const float speed = speedMultiplier * m_DeltaTime;
    const bool movedByKeyboard =
        m_CameraController.Right != 0.0f ||
        m_CameraController.Left != 0.0f ||
        m_CameraController.Forward != 0.0f ||
        m_CameraController.Backward != 0.0f ||
        m_CameraController.Up != 0.0f ||
        m_CameraController.Down != 0.0f;
    if (movedByKeyboard)
    {
        ResetAccumulation(false);
    }

    const XMVECTOR cameraTranslate = XMVectorSet(
        m_CameraController.Right - m_CameraController.Left,
        0.0f,
        m_CameraController.Forward - m_CameraController.Backward,
        1.0f) * speed;
    const XMVECTOR cameraPan = XMVectorSet(
        0.0f,
        m_CameraController.Up - m_CameraController.Down,
        0.0f,
        1.0f) * speed;

    m_Camera.Translate(cameraTranslate, Space::Local);
    m_Camera.Translate(cameraPan, Space::Local);

    const XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(m_CameraController.Pitch),
        XMConvertToRadians(m_CameraController.Yaw),
        0.0f);
    m_Camera.SetRotation(cameraRotation);
}

void RaytracingDemo::OnKeyPressed(KeyEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureKeyboard())
    {
        return;
    }

    Base::OnKeyPressed(e);

    switch (e.Key)
    {
    case KeyCode::Escape:
        Application::Get().Quit(0);
        break;
    case KeyCode::Up:
    case KeyCode::W:
        m_CameraController.Forward = 1.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_CameraController.Left = 1.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_CameraController.Backward = 1.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_CameraController.Right = 1.0f;
        break;
    case KeyCode::Q:
        m_CameraController.Down = 1.0f;
        break;
    case KeyCode::E:
        m_CameraController.Up = 1.0f;
        break;
    case KeyCode::ShiftKey:
        m_CameraController.Shift = true;
        break;
    }
}

void RaytracingDemo::OnKeyReleased(KeyEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureKeyboard())
    {
        return;
    }

    Base::OnKeyReleased(e);

    switch (e.Key)
    {
    case KeyCode::Up:
    case KeyCode::W:
        m_CameraController.Forward = 0.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_CameraController.Left = 0.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_CameraController.Backward = 0.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_CameraController.Right = 0.0f;
        break;
    case KeyCode::Q:
        m_CameraController.Down = 0.0f;
        break;
    case KeyCode::E:
        m_CameraController.Up = 0.0f;
        break;
    case KeyCode::ShiftKey:
        m_CameraController.Shift = false;
        break;
    }
}

void RaytracingDemo::OnMouseMoved(MouseMotionEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseMoved(e);

    if (e.LeftButton)
    {
        if (e.RelX != 0 || e.RelY != 0)
        {
            m_CameraController.Pitch = ClampCameraValue(m_CameraController.Pitch + e.RelY * m_MouseRotateSpeed, -90.0f, 90.0f);
            m_CameraController.Yaw += e.RelX * m_MouseRotateSpeed;
            ResetAccumulation(false);
        }
        return;
    }

    if (e.MiddleButton)
    {
        if (e.RelX != 0 || e.RelY != 0)
        {
            const XMVECTOR cameraPan = XMVectorSet(
                static_cast<float>(-e.RelX) * m_MousePanSpeed,
                static_cast<float>(e.RelY) * m_MousePanSpeed,
                0.0f,
                0.0f);
            m_Camera.Translate(cameraPan, Space::Local);
            ResetAccumulation(false);
        }
        return;
    }

    if (e.RightButton)
    {
        if (e.RelX != 0)
        {
            const XMVECTOR cameraForward = XMVectorSet(
                0.0f,
                0.0f,
                static_cast<float>(e.RelX) * m_MouseDollySpeed,
                0.0f);
            m_Camera.Translate(cameraForward, Space::Local);
            ResetAccumulation(false);
        }
    }
}

void RaytracingDemo::OnMouseWheel(MouseWheelEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseWheel(e);
    if (e.WheelDelta != 0.0f)
    {
        const XMVECTOR cameraForward = XMVectorSet(
            0.0f,
            0.0f,
            e.WheelDelta * m_MouseWheelDollySpeed,
            0.0f);
        m_Camera.Translate(cameraForward, Space::Local);
        ResetAccumulation(false);
    }
}

void RaytracingDemo::OnResize(ResizeEventArgs& e)
{
    Base::OnResize(e);

    if (m_Width == e.Width && m_Height == e.Height)
    {
        return;
    }

    m_Width = (std::max)(1, e.Width);
    m_Height = (std::max)(1, e.Height);
    m_FrameIndex = 0;
    ResetAccumulation();
    m_HasPreviousViewProjection = false;

    const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    m_Camera.SetProjection(m_CameraFov, aspectRatio, 0.1f, 1000.0f);

    if (m_RenderGraph != nullptr)
    {
        m_RenderGraph->MarkDirty();
    }
}
//Modify End
