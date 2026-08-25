#include <Framework/UI/NumericWidgets.h>

#include <algorithm>
#include <cstddef>

//Modify Begin:2026-08-18 by Hui
namespace
{
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

    const char* FindRenderedTextEnd(const char* text)
    {
        const char* textEnd = text;
        while (textEnd[0] != '\0' && (textEnd[0] != '#' || textEnd[1] != '#'))
        {
            ++textEnd;
        }
        return textEnd;
    }

    void DisplayValue(const ImGuiDataType dataType, const void* value, const char* format)
    {
        if (dataType == ImGuiDataType_Float)
        {
            ImGui::Text(format, *static_cast<const float*>(value));
        }
        else if (dataType == ImGuiDataType_S32)
        {
            ImGui::Text(format, *static_cast<const int*>(value));
        }
    }

    bool DrawNumericControl(
        const ImGuiDataType dataType,
        void* value,
        const void* minValue,
        const void* maxValue,
        const char* format,
        const ImGuiSliderFlags flags)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* inputFormat = format != nullptr
            ? format
            : (dataType == ImGuiDataType_Float ? "%.3f" : "%d");
        const float totalWidth = ImGui::CalcItemWidth();
        const float inputWidth = std::clamp(totalWidth * 0.35f, 72.0f, 128.0f);
        const float sliderWidth = std::max(totalWidth - inputWidth - style.ItemInnerSpacing.x, 1.0f);

        ImGui::SetNextItemWidth(sliderWidth);
        bool changed = ImGui::SliderScalar(
            "##range", dataType, value, minValue, maxValue, "", flags);
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

        if ((flags & ImGuiSliderFlags_NoInput) != 0)
        {
            DisplayValue(dataType, value, inputFormat);
            return changed;
        }

        ImGui::SetNextItemWidth(inputWidth);
        changed |= ImGui::InputScalar("##value", dataType, value, nullptr, nullptr, inputFormat);
        return changed;
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
        static constexpr const char* componentLabels[] = { "X", "Y", "Z", "W" };
        const char* labelEnd = FindRenderedTextEnd(label);
        bool changed = false;

        ImGui::PushID(label);
        if (componentCount == 1)
        {
            if (label != labelEnd)
            {
                ImGui::TextUnformatted(label, labelEnd);
                ImGui::SameLine();
            }
            changed = DrawNumericControl(dataType, values, minValue, maxValue, format, flags);
        }
        else
        {
            if (label != labelEnd)
            {
                ImGui::TextUnformatted(label, labelEnd);
            }

            ImGui::Indent();
            auto* componentValue = static_cast<std::byte*>(values);
            for (int componentIndex = 0; componentIndex < componentCount; ++componentIndex)
            {
                ImGui::PushID(componentIndex);
                ImGui::TextUnformatted(componentLabels[componentIndex]);
                ImGui::SameLine();
                changed |= DrawNumericControl(dataType, componentValue, minValue, maxValue, format, flags);
                ImGui::PopID();
                componentValue += componentSize;
            }
            ImGui::Unindent();
        }
        ImGui::PopID();

        if (changed && (flags & ImGuiSliderFlags_AlwaysClamp) != 0)
        {
            ClampInputValues(dataType, values, componentCount, minValue, maxValue);
        }
        return changed;
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
