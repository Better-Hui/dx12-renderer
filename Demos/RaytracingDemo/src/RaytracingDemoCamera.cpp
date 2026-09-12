//Modify Begin:2026-09-10 by Hui
#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/Events.h>

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float SceneCameraAngularSpeedStep = 0.05f;
    constexpr float SceneCameraConeHalfAngleStep = XMConvertToRadians(1.0f);

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
    if (!IsStartupLoadComplete())
    {
        return;
    }
    const auto directCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_ActivePixels.CollectCountReadback(*directCommandQueue);
    m_Denoisers.PollOIDN(*directCommandQueue);
    m_DiagnosticsImageCapture.Poll();
    UpdateRuntimeAutomation(e.TotalTime);
    UpdateShowreel();

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
        ResetAccumulation(false, false, false);
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

    GetSceneCamera().Translate(cameraTranslate, Space::Local);
    GetSceneCamera().Translate(cameraPan, Space::Local);

    const XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(m_CameraController.Pitch),
        XMConvertToRadians(m_CameraController.Yaw),
        0.0f);
    GetSceneCamera().SetRotation(cameraRotation);

    const bool sceneRuntimeUpdated = m_SceneRuntime.Update(
        m_Lights,
        GetSceneCamera(),
        m_DeltaTime,
        static_cast<float>(e.TotalTime));
    if (sceneRuntimeUpdated)
    {
        if (m_SceneRuntime.HasActiveSceneBehavior() &&
            m_SceneRuntime.IsSceneBehaviorEnabled() &&
            m_SceneRuntime.IsSceneCameraControlEnabled())
        {
            const XMVECTOR sceneForward = XMVector3Normalize(XMVector3Rotate(
                XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                GetSceneCamera().GetRotation()));
            m_CameraController.Yaw = XMConvertToDegrees(std::atan2(
                XMVectorGetX(sceneForward),
                XMVectorGetZ(sceneForward)));
            m_CameraController.Pitch = XMConvertToDegrees(std::asin(std::clamp(
                XMVectorGetY(sceneForward),
                -1.0f,
                1.0f)));
        }
        // Keep ReSTIR history across scene motion; temporal compatibility tests handle reprojection.
        ResetAccumulation(false, false, false);
    }
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
        GetApplication().Quit(0);
        break;
    case KeyCode::Up:
        {
            float newAngularSpeed = 0.0f;
            if (m_SceneRuntime.AdjustSceneCameraAngularSpeed(
                    SceneCameraAngularSpeedStep,
                    newAngularSpeed))
            {
                if (m_Diagnostics.IsEnabled())
                {
                    m_Diagnostics.Record(
                        "scene.runtime",
                        "camera_speed_changed",
                        DiagnosticTelemetrySeverity::Info,
                        {
                            { "radians_per_second", static_cast<double>(newAngularSpeed) },
                            { "trigger", "Up" },
                        });
                }
                break;
            }
            m_CameraController.Forward = 1.0f;
        }
        break;
    case KeyCode::W:
        m_CameraController.Forward = 1.0f;
        break;
    case KeyCode::Left:
        {
            float newConeHalfAngle = 0.0f;
            if (m_SceneRuntime.AdjustSceneCameraConeHalfAngle(
                    -SceneCameraConeHalfAngleStep,
                    newConeHalfAngle))
            {
                if (m_Diagnostics.IsEnabled())
                {
                    m_Diagnostics.Record(
                        "scene.runtime",
                        "camera_cone_half_angle_changed",
                        DiagnosticTelemetrySeverity::Info,
                        {
                            { "degrees", static_cast<double>(XMConvertToDegrees(newConeHalfAngle)) },
                            { "trigger", "Left" },
                        });
                }
                break;
            }
            m_CameraController.Left = 1.0f;
        }
        break;
    case KeyCode::A:
        m_CameraController.Left = 1.0f;
        break;
    case KeyCode::Down:
        {
            float newAngularSpeed = 0.0f;
            if (m_SceneRuntime.AdjustSceneCameraAngularSpeed(
                    -SceneCameraAngularSpeedStep,
                    newAngularSpeed))
            {
                if (m_Diagnostics.IsEnabled())
                {
                    m_Diagnostics.Record(
                        "scene.runtime",
                        "camera_speed_changed",
                        DiagnosticTelemetrySeverity::Info,
                        {
                            { "radians_per_second", static_cast<double>(newAngularSpeed) },
                            { "trigger", "Down" },
                        });
                }
                break;
            }
            m_CameraController.Backward = 1.0f;
        }
        break;
    case KeyCode::S:
        m_CameraController.Backward = 1.0f;
        break;
    case KeyCode::Right:
        {
            float newConeHalfAngle = 0.0f;
            if (m_SceneRuntime.AdjustSceneCameraConeHalfAngle(
                    SceneCameraConeHalfAngleStep,
                    newConeHalfAngle))
            {
                if (m_Diagnostics.IsEnabled())
                {
                    m_Diagnostics.Record(
                        "scene.runtime",
                        "camera_cone_half_angle_changed",
                        DiagnosticTelemetrySeverity::Info,
                        {
                            { "degrees", static_cast<double>(XMConvertToDegrees(newConeHalfAngle)) },
                            { "trigger", "Right" },
                        });
                }
                break;
            }
            m_CameraController.Right = 1.0f;
        }
        break;
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
    if (e.Key == KeyCode::F10 && m_ShowreelEnabled)
    {
        ToggleShowreelPlayback();
        return;
    }

    if (e.Key == KeyCode::F11 && m_SceneRuntime.HasActiveSceneBehavior())
    {
        const bool lightAnimationEnabled = !m_SceneRuntime.IsSceneLightAnimationEnabled();
        m_SceneRuntime.SetSceneLightAnimationEnabled(lightAnimationEnabled);
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.Record(
                "scene.runtime",
                "light_animation_changed",
                DiagnosticTelemetrySeverity::Info,
                {
                    { "behavior", std::string(m_SceneRuntime.GetActiveSceneBehaviorName()) },
                    { "light_animation_enabled", lightAnimationEnabled },
                    { "trigger", "F11" },
                });
        }
        ResetAccumulation(false, true);
        return;
    }

    if (e.Key == KeyCode::F12 && m_SceneRuntime.HasActiveSceneBehavior())
    {
        const bool cameraControlEnabled = !m_SceneRuntime.IsSceneCameraControlEnabled();
        m_SceneRuntime.SetSceneCameraControlEnabled(cameraControlEnabled);
        float angularSpeed = 0.0f;
        if (cameraControlEnabled && m_SceneRuntime.SetSceneCameraAngularSpeedToMaximum(angularSpeed) &&
            m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.Record(
                "scene.runtime",
                "camera_speed_changed",
                DiagnosticTelemetrySeverity::Info,
                {
                    { "radians_per_second", static_cast<double>(angularSpeed) },
                    { "trigger", "F12" },
                });
        }
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.Record(
                "scene.runtime",
                "camera_control_changed",
                DiagnosticTelemetrySeverity::Info,
                {
                    { "behavior", std::string(m_SceneRuntime.GetActiveSceneBehaviorName()) },
                    { "camera_control_enabled", cameraControlEnabled },
                    { "trigger", "F12" },
                });
        }
        // Toggling script control does not invalidate ReSTIR or OIDN history.
        ResetAccumulation(false, false, false);
        return;
    }

    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureKeyboard())
    {
        return;
    }

    Base::OnKeyReleased(e);

    switch (e.Key)
    {
    case KeyCode::Up:
        if (!m_SceneRuntime.HasActiveSceneBehavior())
        {
            m_CameraController.Forward = 0.0f;
        }
        break;
    case KeyCode::W:
        m_CameraController.Forward = 0.0f;
        break;
    case KeyCode::Left:
        if (!m_SceneRuntime.HasActiveSceneBehavior())
        {
            m_CameraController.Left = 0.0f;
        }
        break;
    case KeyCode::A:
        m_CameraController.Left = 0.0f;
        break;
    case KeyCode::Down:
        if (!m_SceneRuntime.HasActiveSceneBehavior())
        {
            m_CameraController.Backward = 0.0f;
        }
        break;
    case KeyCode::S:
        m_CameraController.Backward = 0.0f;
        break;
    case KeyCode::Right:
        if (!m_SceneRuntime.HasActiveSceneBehavior())
        {
            m_CameraController.Right = 0.0f;
        }
        break;
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
            m_LeftMouseDragSincePress = true;
            m_CameraController.Pitch = ClampCameraValue(m_CameraController.Pitch + e.RelY * m_MouseRotateSpeed, -90.0f, 90.0f);
            m_CameraController.Yaw += e.RelX * m_MouseRotateSpeed;
            ResetAccumulation(false, false, false);
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
            GetSceneCamera().Translate(cameraPan, Space::Local);
            ResetAccumulation(false, false, false);
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
            GetSceneCamera().Translate(cameraForward, Space::Local);
            ResetAccumulation(false, false, false);
        }
    }
}

void RaytracingDemo::OnMouseButtonPressed(MouseButtonEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseButtonPressed(e);
    if (e.Button != MouseButtonEventArgs::Left)
    {
        return;
    }

    m_LeftMouseDragSincePress = false;
    m_LeftMouseNativeDoubleClick = e.DoubleClick;
    m_LeftMousePressX = e.X;
    m_LeftMousePressY = e.Y;
}

void RaytracingDemo::OnMouseButtonReleased(MouseButtonEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseButtonReleased(e);
    if (e.Button != MouseButtonEventArgs::Left)
    {
        return;
    }

    constexpr int DoubleClickDragThreshold = 3;
    const int deltaX = e.X - m_LeftMousePressX;
    const int deltaY = e.Y - m_LeftMousePressY;
    const bool isClick =
        !m_LeftMouseDragSincePress &&
        deltaX * deltaX + deltaY * deltaY <= DoubleClickDragThreshold * DoubleClickDragThreshold;
    if (m_LeftMouseNativeDoubleClick && m_LastLeftClickWasClick && isClick)
    {
        ResetCameraToInitialSceneState();
    }

    m_LastLeftClickWasClick = isClick;
    m_LeftMouseDragSincePress = false;
    m_LeftMouseNativeDoubleClick = false;
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
        GetSceneCamera().Translate(cameraForward, Space::Local);
        ResetAccumulation(false, false, false);
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
    GetSceneCamera().SetProjection(m_CameraFov, aspectRatio, m_CameraNearClipPlane, m_CameraFarClipPlane);

    // Render and display dimensions are part of the pipeline configuration.
    // The next frame retires the old graph, DLSS feature, and CUDA interop resource together.
}
//Modify End
