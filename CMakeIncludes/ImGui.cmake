# Modify Begin:2026-08-16 by BestHui
include(FetchContent)

FetchContent_Declare(dx12_renderer_imgui
        URL "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip"
        URL_HASH "SHA256=3fe9e93066e37c630cbbada821a45812cd86df0f21a476cf74c925e1b649878d"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
FetchContent_MakeAvailable(dx12_renderer_imgui)

set(DX12_RENDERER_IMGUI_SOURCE_DIRECTORY "${dx12_renderer_imgui_SOURCE_DIR}")
set(DX12_RENDERER_IMGUI_PATCHED_WIDGETS_SOURCE "${CMAKE_BINARY_DIR}/Generated/ImGui/imgui_widgets.cpp")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Generated/ImGui")
file(READ "${DX12_RENDERER_IMGUI_SOURCE_DIRECTORY}/imgui_widgets.cpp" DX12_RENDERER_IMGUI_WIDGETS_CONTENT)
set(DX12_RENDERER_IMGUI_SLIDER_INPUT_SOURCE [=[        // Tabbing or CTRL+click on Slider turns it into an input box
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool make_active = (clicked || g.NavActivateId == id);
        if (make_active && clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if ((clicked && g.IO.KeyCtrl) || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;
]=])
set(DX12_RENDERER_IMGUI_SLIDER_INPUT_REPLACEMENT [=[        // Tabbing, CTRL+click, or double-click on Slider turns it into an input box.
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool double_clicked = hovered && IsMouseDoubleClicked(0, id);
        const bool ctrl_clicked = clicked && g.IO.KeyCtrl;
        bool drag_clicked = false;
        if (clicked && !double_clicked && g.ActiveId != id)
        {
            ImRect initial_grab_bb;
            SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, flags, &initial_grab_bb);
            drag_clicked = initial_grab_bb.Contains(g.IO.MousePos);
        }
        const bool make_active = (drag_clicked || ctrl_clicked || double_clicked || g.NavActivateId == id);
        if (make_active && (drag_clicked || ctrl_clicked || double_clicked))
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if (ctrl_clicked || double_clicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;
]=])
string(FIND "${DX12_RENDERER_IMGUI_WIDGETS_CONTENT}" "${DX12_RENDERER_IMGUI_SLIDER_INPUT_SOURCE}" DX12_RENDERER_IMGUI_PATCH_OFFSET)
if (DX12_RENDERER_IMGUI_PATCH_OFFSET EQUAL -1)
        message(FATAL_ERROR "Dear ImGui ${IMGUI_VERSION} SliderScalar source no longer matches the required double-click input patch.")
endif()
string(REPLACE "${DX12_RENDERER_IMGUI_SLIDER_INPUT_SOURCE}" "${DX12_RENDERER_IMGUI_SLIDER_INPUT_REPLACEMENT}" DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT "${DX12_RENDERER_IMGUI_WIDGETS_CONTENT}")
# Modify Begin:2026-08-16 by BestHui
set(DX12_RENDERER_IMGUI_GRAB_OFFSET_SOURCE [=[                    g.SliderGrabClickOffset = (clicked_around_grab && is_floating_point) ? mouse_abs_pos - grab_pos : 0.0f;
]=])
set(DX12_RENDERER_IMGUI_GRAB_OFFSET_REPLACEMENT [=[                    g.SliderGrabClickOffset = clicked_around_grab ? mouse_abs_pos - grab_pos : 0.0f;
]=])
string(FIND "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT}" "${DX12_RENDERER_IMGUI_GRAB_OFFSET_SOURCE}" DX12_RENDERER_IMGUI_GRAB_OFFSET_PATCH_OFFSET)
if (DX12_RENDERER_IMGUI_GRAB_OFFSET_PATCH_OFFSET EQUAL -1)
        message(FATAL_ERROR "Dear ImGui v1.91.9 SliderBehavior source no longer matches the required grab-only slider patch.")
endif()
string(REPLACE "${DX12_RENDERER_IMGUI_GRAB_OFFSET_SOURCE}" "${DX12_RENDERER_IMGUI_GRAB_OFFSET_REPLACEMENT}" DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT}")
# Modify End
set(DX12_RENDERER_IMGUI_VSLIDER_INPUT_SOURCE [=[    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(frame_bb, id))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
    if (clicked || g.NavActivateId == id)
    {
        if (clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
    }
]=])
set(DX12_RENDERER_IMGUI_VSLIDER_INPUT_REPLACEMENT [=[    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(frame_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        // Tabbing, CTRL+click, or double-click on VSlider turns it into an input box.
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool double_clicked = hovered && IsMouseDoubleClicked(0, id);
        const bool ctrl_clicked = clicked && g.IO.KeyCtrl;
        bool drag_clicked = false;
        if (clicked && !double_clicked && g.ActiveId != id)
        {
            ImRect initial_grab_bb;
            SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, flags | ImGuiSliderFlags_Vertical, &initial_grab_bb);
            drag_clicked = initial_grab_bb.Contains(g.IO.MousePos);
        }
        const bool make_active = (drag_clicked || ctrl_clicked || double_clicked || g.NavActivateId == id);
        if (make_active && (drag_clicked || ctrl_clicked || double_clicked))
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if (ctrl_clicked || double_clicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;

        if (make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
        }
    }

    if (temp_input_is_active)
    {
        const bool clamp_enabled = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
        return TempInputScalar(frame_bb, id, label, data_type, p_data, format, clamp_enabled ? p_min : NULL, clamp_enabled ? p_max : NULL);
    }
]=])
string(FIND "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT}" "${DX12_RENDERER_IMGUI_VSLIDER_INPUT_SOURCE}" DX12_RENDERER_IMGUI_VSLIDER_PATCH_OFFSET)
if (DX12_RENDERER_IMGUI_VSLIDER_PATCH_OFFSET EQUAL -1)
        message(FATAL_ERROR "Dear ImGui v1.91.9 VSliderScalar source no longer matches the required double-click input patch.")
endif()
string(REPLACE "${DX12_RENDERER_IMGUI_VSLIDER_INPUT_SOURCE}" "${DX12_RENDERER_IMGUI_VSLIDER_INPUT_REPLACEMENT}" DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT}")
file(CONFIGURE OUTPUT "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_SOURCE}" CONTENT "${DX12_RENDERER_IMGUI_PATCHED_WIDGETS_CONTENT}" @ONLY)
# Modify End
