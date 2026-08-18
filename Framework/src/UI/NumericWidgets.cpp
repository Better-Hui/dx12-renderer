#include <Framework/UI/NumericWidgets.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

//Modify Begin:2026-08-18 by Hui
namespace
{
    constexpr ImGuiID EditingStateSalt = 0x5a17c9e3u;
    constexpr ImGuiID FocusRequestSalt = 0xb6e2431du;

    template<typename T>
    void ClampValues(T* values, const int componentCount, const T minValue, const T maxValue)
    {
        for (int componentIndex = 0; componentIndex < componentCount; ++componentIndex)
        {
            values[componentIndex] = std::clamp(values[componentIndex], minValue, maxValue);
        }
    }

    void ClampInputValues(
        const ImGuiDataType dataType,
        void* values,
        const int componentCount,
        const void* minValue,
        const void* maxValue)
    {
        if (dataType == ImGuiDataType_Float)
        {
            ClampValues(
                static_cast<float*>(values),
                componentCount,
                *static_cast<const float*>(minValue),
                *static_cast<const float*>(maxValue));
        }
        else if (dataType == ImGuiDataType_S32)
        {
            ClampValues(
                static_cast<int*>(values),
                componentCount,
                *static_cast<const int*>(minValue),
                *static_cast<const int*>(maxValue));
        }
    }

    bool SliderScalarWithInput(
        const char* label,
        const ImGuiDataType dataType,
        void* values,
        const int componentCount,
        const size_t componentSize,
        const void* minValue,
        const void* maxValue,
        const char* format,
        const ImGuiSliderFlags flags)
    {
        auto* storage = ImGui::GetStateStorage();
        const ImGuiID itemId = ImGui::GetID(label);
        const ImGuiID editingStateId = itemId ^ EditingStateSalt;
        const ImGuiID focusRequestId = itemId ^ FocusRequestSalt;

        if (storage->GetBool(editingStateId))
        {
            if (storage->GetBool(focusRequestId))
            {
                ImGui::SetKeyboardFocusHere();
                storage->SetBool(focusRequestId, false);
            }

            const bool changed = componentCount == 1
                ? ImGui::InputScalar(label, dataType, values, nullptr, nullptr, format)
                : ImGui::InputScalarN(label, dataType, values, componentCount, nullptr, nullptr, format);
            if (changed && (flags & ImGuiSliderFlags_AlwaysClamp) != 0)
            {
                ClampInputValues(dataType, values, componentCount, minValue, maxValue);
            }
            if (ImGui::IsItemDeactivated())
            {
                storage->SetBool(editingStateId, false);
            }
            return changed;
        }

        std::array<std::byte, sizeof(float) * 4> originalValues{};
        const size_t valueByteCount = componentSize * static_cast<size_t>(componentCount);
        std::memcpy(originalValues.data(), values, valueByteCount);

        const bool changed = componentCount == 1
            ? ImGui::SliderScalar(label, dataType, values, minValue, maxValue, format, flags)
            : ImGui::SliderScalarN(label, dataType, values, componentCount, minValue, maxValue, format, flags);
        const bool requestTextInput =
            (flags & ImGuiSliderFlags_NoInput) == 0 &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (!requestTextInput)
        {
            return changed;
        }

        std::memcpy(values, originalValues.data(), valueByteCount);
        storage->SetBool(editingStateId, true);
        storage->SetBool(focusRequestId, true);
        return false;
    }
}

bool FrameworkImGui::SliderFloat(
    const char* label,
    float* value,
    const float minValue,
    const float maxValue,
    const char* format,
    const ImGuiSliderFlags flags)
{
    return SliderScalarWithInput(
        label, ImGuiDataType_Float, value, 1, sizeof(float), &minValue, &maxValue, format, flags);
}

bool FrameworkImGui::SliderFloat2(
    const char* label,
    float values[2],
    const float minValue,
    const float maxValue,
    const char* format,
    const ImGuiSliderFlags flags)
{
    return SliderScalarWithInput(
        label, ImGuiDataType_Float, values, 2, sizeof(float), &minValue, &maxValue, format, flags);
}

bool FrameworkImGui::SliderFloat3(
    const char* label,
    float values[3],
    const float minValue,
    const float maxValue,
    const char* format,
    const ImGuiSliderFlags flags)
{
    return SliderScalarWithInput(
        label, ImGuiDataType_Float, values, 3, sizeof(float), &minValue, &maxValue, format, flags);
}

bool FrameworkImGui::SliderFloat4(
    const char* label,
    float values[4],
    const float minValue,
    const float maxValue,
    const char* format,
    const ImGuiSliderFlags flags)
{
    return SliderScalarWithInput(
        label, ImGuiDataType_Float, values, 4, sizeof(float), &minValue, &maxValue, format, flags);
}

bool FrameworkImGui::SliderInt(
    const char* label,
    int* value,
    const int minValue,
    const int maxValue,
    const char* format,
    const ImGuiSliderFlags flags)
{
    return SliderScalarWithInput(
        label, ImGuiDataType_S32, value, 1, sizeof(int), &minValue, &maxValue, format, flags);
}
//Modify End
