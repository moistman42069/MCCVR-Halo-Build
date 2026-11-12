#pragma once
// A SMALL JSON READER for the hand-edited files a player keeps beside halomccvr.cfg.
//
// Enough of RFC 8259, strict about syntax, header-only and pure. Two things a hand-edited file needs and the
// release reader (release_update_policy.h) does not give: object members keep their FILE ORDER (the first of two
// equal matches wins, so order is part of the file's meaning), and every value remembers its byte offset, so an
// error can say "line L col C" to the person who typed it.
//
// Bounds: nesting stops at kMaxDepth, numbers must be finite doubles. The caller bounds the text's size.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace json_lite
{
inline constexpr int kMaxDepth = 16;

struct Value
{
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind{Kind::Null};
    double number{0.0};
    bool boolean{false};
    std::string text;
    std::vector<Value> items;      // array elements, or object values
    std::vector<std::string> keys; // object keys, parallel to items
    std::size_t at{0};             // byte offset in the text, for error positions

    bool IsNull() const noexcept { return kind == Kind::Null; }
    bool IsBool() const noexcept { return kind == Kind::Bool; }
    bool IsNumber() const noexcept { return kind == Kind::Number; }
    bool IsString() const noexcept { return kind == Kind::String; }
    bool IsArray() const noexcept { return kind == Kind::Array; }
    bool IsObject() const noexcept { return kind == Kind::Object; }

    // The member called `key`, or null. Linear: these files are small.
    const Value* Get(const char* key) const
    {
        if (kind != Kind::Object)
            return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }
};

// The 1-based line and column of byte offset `at`.
inline void LineCol(const char* text, std::size_t at, int& line, int& col) noexcept
{
    line = 1;
    col = 1;
    for (std::size_t k = 0; k < at && text[k] != 0; ++k)
    {
        if (text[k] == '\n')
        {
            ++line;
            col = 1;
        }
        else
        {
            ++col;
        }
    }
}

namespace detail
{
struct Reader
{
    const char* p;
    std::size_t len;
    std::size_t i{0};
    std::string error;
    std::size_t errorAt{0};

    bool Fail(const char* message)
    {
        if (error.empty())
        {
            error = message;
            errorAt = i;
        }
        return false;
    }

    void Space() noexcept
    {
        while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n'))
            ++i;
    }

    bool Literal(const char* word) noexcept
    {
        const std::size_t n = std::strlen(word);
        if (len - i < n || std::strncmp(p + i, word, n) != 0)
            return false;
        i += n;
        return true;
    }

    bool String(std::string& out)
    {
        if (i >= len || p[i] != '"')
            return Fail("expected a string");
        ++i;
        while (i < len && p[i] != '"')
        {
            char c = p[i++];
            if (static_cast<unsigned char>(c) < 0x20)
                return Fail("control character inside a string");
            if (c == '\\')
            {
                if (i >= len)
                    break;
                const char e = p[i++];
                switch (e)
                {
                case '"':
                case '\\':
                case '/':
                    c = e;
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'u':
                {
                    // Names and notes only: four hex digits, of which the low seven bits are kept (ASCII).
                    if (len - i < 4)
                        return Fail("bad \\u escape");
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const char h = p[i + k];
                        unsigned digit = 0;
                        if (h >= '0' && h <= '9')
                            digit = static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            digit = static_cast<unsigned>(h - 'a') + 10u;
                        else if (h >= 'A' && h <= 'F')
                            digit = static_cast<unsigned>(h - 'A') + 10u;
                        else
                            return Fail("bad \\u escape");
                        code = code * 16u + digit;
                    }
                    c = static_cast<char>(code & 0x7Fu);
                    i += 4;
                    break;
                }
                default:
                    return Fail("bad escape in a string");
                }
            }
            out.push_back(c);
        }
        if (i >= len)
            return Fail("unterminated string");
        ++i;
        return true;
    }

    bool Read(Value& value, int depth)
    {
        if (depth > kMaxDepth)
            return Fail("nested too deeply");
        Space();
        value.at = i;
        if (i >= len)
            return Fail("unexpected end of file");
        const char c = p[i];
        if (c == '{')
        {
            value.kind = Value::Kind::Object;
            ++i;
            Space();
            if (i < len && p[i] == '}')
            {
                ++i;
                return true;
            }
            for (;;)
            {
                Space();
                std::string key;
                if (!String(key))
                    return false;
                Space();
                if (i >= len || p[i] != ':')
                    return Fail("expected ':' after a key");
                ++i;
                value.keys.push_back(key);
                value.items.emplace_back();
                if (!Read(value.items.back(), depth + 1))
                    return false;
                Space();
                if (i < len && p[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < len && p[i] == '}')
                {
                    ++i;
                    return true;
                }
                return Fail("expected ',' or '}' (a missing comma, or a trailing one?)");
            }
        }
        if (c == '[')
        {
            value.kind = Value::Kind::Array;
            ++i;
            Space();
            if (i < len && p[i] == ']')
            {
                ++i;
                return true;
            }
            for (;;)
            {
                value.items.emplace_back();
                if (!Read(value.items.back(), depth + 1))
                    return false;
                Space();
                if (i < len && p[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < len && p[i] == ']')
                {
                    ++i;
                    return true;
                }
                return Fail("expected ',' or ']' (a missing comma, or a trailing one?)");
            }
        }
        if (c == '"')
        {
            value.kind = Value::Kind::String;
            return String(value.text);
        }
        if (Literal("true"))
        {
            value.kind = Value::Kind::Bool;
            value.boolean = true;
            return true;
        }
        if (Literal("false"))
        {
            value.kind = Value::Kind::Bool;
            value.boolean = false;
            return true;
        }
        if (Literal("null"))
        {
            value.kind = Value::Kind::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            // strtod alone would read past the token; bound it to the characters a JSON number may hold.
            std::size_t j = i;
            while (j < len && std::strchr("+-.eE0123456789", p[j]) != nullptr)
                ++j;
            const std::string token(p + i, j - i);
            char* end = nullptr;
            const double parsed = std::strtod(token.c_str(), &end);
            if (end == nullptr || *end != 0)
                return Fail("bad number");
            if (!std::isfinite(parsed))
                return Fail("number out of range");
            value.kind = Value::Kind::Number;
            value.number = parsed;
            i = j;
            return true;
        }
        return Fail("expected a value (a number, [ ], { }, \"text\", true, false or null)");
    }
};
} // namespace detail

// Parse the whole of `text`. On failure `error` reads "line L col C: what", and `out` is unspecified.
inline bool Parse(const char* text, std::size_t len, Value& out, std::string& error)
{
    detail::Reader reader{text, len};
    out = Value{};
    bool ok = reader.Read(out, 0);
    if (ok)
    {
        reader.Space();
        if (reader.i < len)
            ok = reader.Fail("unexpected text after the end of the document");
    }
    if (!ok)
    {
        int line = 0, col = 0;
        LineCol(text, reader.errorAt, line, col);
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "line %d col %d: %s", line, col, reader.error.c_str());
        error = buffer;
    }
    return ok;
}
} // namespace json_lite
