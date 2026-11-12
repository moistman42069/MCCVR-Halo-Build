// The small JSON reader (src/common/json_lite.h): what it accepts, what it refuses and where it says the fault
// is, member order, and its bounds.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../src/common/json_lite.h"

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool Reads(const char* text, json_lite::Value& out, std::string* error = nullptr)
{
    std::string scratch;
    return json_lite::Parse(text, std::strlen(text), out, error != nullptr ? *error : scratch);
}

int main()
{
    using json_lite::Value;
    Value v;
    std::string error;

    // Every kind of value, and members in the order the file gives them.
    Check(Reads("{ \"zeta\": 1, \"alpha\": [true, false, null, \"t\", -2.5e1], \"mid\": { } }", v), "a document reads");
    Check(v.IsObject() && v.keys.size() == 3, "three members");
    Check(v.keys[0] == "zeta" && v.keys[1] == "alpha" && v.keys[2] == "mid", "members keep the file's order");
    Check(v.Get("zeta") != nullptr && v.Get("zeta")->IsNumber() && v.Get("zeta")->number == 1.0, "a number");
    const Value* list = v.Get("alpha");
    Check(list != nullptr && list->IsArray() && list->items.size() == 5, "a list of five");
    Check(list->items[0].IsBool() && list->items[0].boolean, "true");
    Check(list->items[1].IsBool() && !list->items[1].boolean, "false");
    Check(list->items[2].IsNull(), "null");
    Check(list->items[3].IsString() && list->items[3].text == "t", "text");
    Check(list->items[4].IsNumber() && list->items[4].number == -25.0, "a negative number with an exponent");
    Check(v.Get("mid") != nullptr && v.Get("mid")->IsObject() && v.Get("mid")->keys.empty(), "an empty object");
    Check(v.Get("absent") == nullptr && list->Get("zeta") == nullptr, "a missing member, and a member of a list");

    // Escapes.
    Check(Reads("\"a\\\"b\\\\c\\/d\\n\\t\\u0041\"", v) && v.text == "a\"b\\c/d\n\tA", "string escapes");
    Check(!Reads("\"\\q\"", v, &error) && error.find("bad escape") != std::string::npos, "an unknown escape is refused");
    Check(!Reads("\"\\u00G1\"", v, &error) && error.find("\\u") != std::string::npos, "a bad \\u escape is refused");
    Check(!Reads("\"open", v, &error) && error.find("unterminated") != std::string::npos, "an unterminated string");
    Check(!Reads("\"a\tb\"", v, &error) && error.find("control character") != std::string::npos,
          "a raw control character inside a string");

    // Where the fault is.
    Check(!Reads("{\n  \"a\": 1,\n  \"b\" 2\n}", v, &error), "a missing colon is refused");
    Check(error.rfind("line 3 col 7:", 0) == 0, "and placed on its line and column");
    Check(!Reads("[1, 2,]", v, &error) && error.find("expected a value") != std::string::npos, "a trailing comma");
    Check(!Reads("[1 2]", v, &error) && error.find("missing comma") != std::string::npos, "a missing comma");
    Check(!Reads("{ \"a\": 1 } x", v, &error) && error.find("after the end") != std::string::npos,
          "text after the document");
    Check(!Reads("", v, &error) && error.find("end of file") != std::string::npos, "an empty file");
    Check(!Reads("{ a: 1 }", v, &error) && error.find("expected a string") != std::string::npos, "an unquoted key");

    // Numbers stay finite and inside their own token.
    Check(!Reads("1e999", v, &error) && error.find("out of range") != std::string::npos, "an infinite number");
    Check(!Reads("1.2.3", v, &error) && error.find("bad number") != std::string::npos, "two decimal points");
    Check(!Reads("nan", v, &error) && !Reads("Infinity", v, &error), "nan and Infinity are not JSON");
    Check(Reads("[10]", v) && v.items[0].number == 10.0, "a number ends at its bracket");

    // Nesting is bounded.
    std::string deep;
    for (int i = 0; i < json_lite::kMaxDepth + 2; ++i)
        deep += '[';
    Check(!Reads(deep.c_str(), v, &error) && error.find("nested too deeply") != std::string::npos,
          "nesting past the bound is refused");
    std::string allowed;
    for (int i = 0; i < json_lite::kMaxDepth; ++i)
        allowed += '[';
    allowed += "0";
    for (int i = 0; i < json_lite::kMaxDepth; ++i)
        allowed += ']';
    Check(Reads(allowed.c_str(), v), "nesting at the bound reads");

    // Only `len` bytes are read.
    const char* tail = "[1, 2] trailing";
    Check(json_lite::Parse(tail, 6, v, error) && v.items.size() == 2, "the length bounds the read");

    int line = 0, col = 0;
    json_lite::LineCol("ab\ncd", 4, line, col);
    Check(line == 2 && col == 2, "LineCol counts from one");

    std::printf("PASS: %u json_lite checks\n", g_checks);
    return 0;
}
