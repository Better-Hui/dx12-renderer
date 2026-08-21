//Modify Begin:2026-08-21 by Hui
#pragma once

#include <Windows.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace RendererDiagnosticsTool
{
    struct JsonNumber
    {
        std::string Text;
    };

    struct JsonValue
    {
        using Object = std::map<std::string, JsonValue, std::less<>>;
        using Array = std::vector<JsonValue>;
        std::variant<std::nullptr_t, bool, JsonNumber, std::string, Array, Object> Data = nullptr;

        [[nodiscard]] const Object& AsObject() const { return std::get<Object>(Data); }
        [[nodiscard]] const Array& AsArray() const { return std::get<Array>(Data); }
    };

    class JsonParser final
    {
    public:
        explicit JsonParser(const std::string_view input) : m_Input(input) {}

        JsonValue Parse()
        {
            JsonValue result = ParseValue();
            SkipWhitespace();
            if (m_Position != m_Input.size()) Fail("Unexpected trailing JSON data.");
            return result;
        }

    private:
        [[noreturn]] void Fail(const std::string& message) const
        {
            throw std::runtime_error(message + " Offset=" + std::to_string(m_Position) + ".");
        }

        void SkipWhitespace()
        {
            while (m_Position < m_Input.size() &&
                (m_Input[m_Position] == ' ' || m_Input[m_Position] == '\t' ||
                    m_Input[m_Position] == '\r' || m_Input[m_Position] == '\n'))
            {
                ++m_Position;
            }
        }

        bool Consume(const char expected)
        {
            SkipWhitespace();
            if (m_Position < m_Input.size() && m_Input[m_Position] == expected)
            {
                ++m_Position;
                return true;
            }
            return false;
        }

        JsonValue ParseValue()
        {
            SkipWhitespace();
            if (m_Position >= m_Input.size()) Fail("Unexpected end of JSON input.");
            switch (m_Input[m_Position])
            {
            case '{': return ParseObject();
            case '[': return ParseArray();
            case '"': return JsonValue{ ParseString() };
            case 't': ParseLiteral("true"); return JsonValue{ true };
            case 'f': ParseLiteral("false"); return JsonValue{ false };
            case 'n': ParseLiteral("null"); return JsonValue{};
            default:
                if (m_Input[m_Position] == '-' ||
                    (m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9'))
                {
                    return JsonValue{ ParseNumber() };
                }
                Fail("Unexpected JSON token.");
            }
        }

        JsonValue ParseObject()
        {
            if (!Consume('{')) Fail("Expected JSON object.");
            JsonValue::Object object;
            if (Consume('}')) return JsonValue{ std::move(object) };
            for (;;)
            {
                SkipWhitespace();
                if (m_Position >= m_Input.size() || m_Input[m_Position] != '"') Fail("Expected JSON object key.");
                std::string key = ParseString();
                if (!Consume(':')) Fail("Expected ':' after JSON object key.");
                if (!object.emplace(std::move(key), ParseValue()).second) Fail("Duplicate JSON object key.");
                if (Consume('}')) break;
                if (!Consume(',')) Fail("Expected ',' in JSON object.");
            }
            return JsonValue{ std::move(object) };
        }

        JsonValue ParseArray()
        {
            if (!Consume('[')) Fail("Expected JSON array.");
            JsonValue::Array array;
            if (Consume(']')) return JsonValue{ std::move(array) };
            for (;;)
            {
                array.push_back(ParseValue());
                if (Consume(']')) break;
                if (!Consume(',')) Fail("Expected ',' in JSON array.");
            }
            return JsonValue{ std::move(array) };
        }

        std::string ParseString()
        {
            if (!Consume('"')) Fail("Expected JSON string.");
            std::string output;
            while (m_Position < m_Input.size())
            {
                const char character = m_Input[m_Position++];
                if (character == '"') return output;
                if (character != '\\')
                {
                    output.push_back(character);
                    continue;
                }
                if (m_Position >= m_Input.size()) Fail("Incomplete JSON escape.");
                const char escape = m_Input[m_Position++];
                switch (escape)
                {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u':
                {
                    if (m_Position + 4u > m_Input.size()) Fail("Incomplete JSON unicode escape.");
                    uint32_t codePoint = 0;
                    for (size_t index = 0; index < 4u; ++index)
                    {
                        const char digit = m_Input[m_Position++];
                        codePoint <<= 4u;
                        if (digit >= '0' && digit <= '9') codePoint += digit - '0';
                        else if (digit >= 'a' && digit <= 'f') codePoint += digit - 'a' + 10u;
                        else if (digit >= 'A' && digit <= 'F') codePoint += digit - 'A' + 10u;
                        else Fail("Invalid JSON unicode escape.");
                    }
                    if (codePoint <= 0x7fu) output.push_back(static_cast<char>(codePoint));
                    else if (codePoint <= 0x7ffu)
                    {
                        output.push_back(static_cast<char>(0xc0u | (codePoint >> 6u)));
                        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
                    }
                    else
                    {
                        output.push_back(static_cast<char>(0xe0u | (codePoint >> 12u)));
                        output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu)));
                        output.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
                    }
                    break;
                }
                default: Fail("Invalid JSON escape.");
                }
            }
            Fail("Unterminated JSON string.");
        }

        JsonNumber ParseNumber()
        {
            SkipWhitespace();
            const size_t begin = m_Position;
            if (m_Input[m_Position] == '-') ++m_Position;
            while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9') ++m_Position;
            if (m_Position < m_Input.size() && m_Input[m_Position] == '.')
            {
                ++m_Position;
                while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9') ++m_Position;
            }
            if (m_Position < m_Input.size() && (m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
            {
                ++m_Position;
                if (m_Position < m_Input.size() && (m_Input[m_Position] == '+' || m_Input[m_Position] == '-')) ++m_Position;
                while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9') ++m_Position;
            }
            if (begin == m_Position) Fail("Invalid JSON number.");
            return { std::string(m_Input.substr(begin, m_Position - begin)) };
        }

        void ParseLiteral(const std::string_view literal)
        {
            if (m_Input.substr(m_Position, literal.size()) != literal) Fail("Invalid JSON literal.");
            m_Position += literal.size();
        }

        std::string_view m_Input;
        size_t m_Position = 0;
    };

    inline std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input.is_open()) throw std::runtime_error("Cannot open file: " + path.string());
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    inline const JsonValue* Find(const JsonValue::Object& object, const std::string_view key)
    {
        const auto value = object.find(key);
        return value != object.end() ? &value->second : nullptr;
    }

    inline std::string ScalarToString(const JsonValue& value)
    {
        if (const auto text = std::get_if<std::string>(&value.Data)) return *text;
        if (const auto number = std::get_if<JsonNumber>(&value.Data)) return number->Text;
        if (const auto boolean = std::get_if<bool>(&value.Data)) return *boolean ? "true" : "false";
        if (std::holds_alternative<std::nullptr_t>(value.Data)) return "null";
        return {};
    }

    inline uint64_t ToUint64(const JsonValue* value, const uint64_t fallback = 0)
    {
        if (value == nullptr) return fallback;
        const std::string text = ScalarToString(*value);
        uint64_t output = fallback;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
        return error == std::errc{} && end == text.data() + text.size() ? output : fallback;
    }

    inline std::optional<double> ToDouble(const std::string& text)
    {
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        return end != text.c_str() && *end == '\0' ? std::optional<double>(value) : std::nullopt;
    }

    inline std::string EscapeJson(const std::string_view value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20u)
                {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<uint32_t>(character) << std::dec;
                }
                else output << static_cast<char>(character);
                break;
            }
        }
        return output.str();
    }

    inline void WriteJsonString(std::ostream& output, const std::string_view value)
    {
        output << '"' << EscapeJson(value) << '"';
    }

    inline std::string WideToUtf8(const std::wstring_view value)
    {
        if (value.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string output(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length, nullptr, nullptr);
        return output;
    }

    inline std::wstring Utf8ToWide(const std::string_view value)
    {
        if (value.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length == 0) throw std::runtime_error("Invalid UTF-8 value.");
        std::wstring output(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), length);
        return output;
    }
}
//Modify End
