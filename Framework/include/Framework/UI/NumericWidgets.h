#pragma once

#include <imgui.h>

//Modify Begin:2026-08-18 by Hui
namespace FrameworkImGui
{
    // Renders a label, a range slider, and a separately clickable numeric input.
    bool SliderFloat(
        const char* label,
        float* value,
        float minValue,
        float maxValue,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);
    bool SliderFloat2(
        const char* label,
        float values[2],
        float minValue,
        float maxValue,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);
    bool SliderFloat3(
        const char* label,
        float values[3],
        float minValue,
        float maxValue,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);
    bool SliderFloat4(
        const char* label,
        float values[4],
        float minValue,
        float maxValue,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);
    bool SliderInt(
        const char* label,
        int* value,
        int minValue,
        int maxValue,
        const char* format = "%d",
        ImGuiSliderFlags flags = 0);
}
//Modify End
