// Halo 3 string ids (src/common/halo3_string_ids.h): a map's strings read from a synthetic global table and buffer
// laid out as retail's (globals first in id order, then the map's own from namespace 0's next index), every refusal
// of a buffer or table, the text of global, map and unknown ids, and the decode of the three signatures on images
// built from their own patterns -- with each disagreement between them refused.
#include "halo3_string_ids.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace halo3_string_ids;

// A small world shaped like retail: namespace 0 holds indexes 0..4 ("", "default", "head", "r_hand", "l_hand"),
// namespace 1 two more; the map adds "canopy" and "hull" (namespace 0, indexes 5 and 6).
struct World
{
    std::vector<uint64_t> ids{0, 1, 2, 3, 4, (1ull << 19) | 0, (1ull << 19) | 1};
    std::vector<std::string> strings{"", "default", "head", "r_hand", "l_hand", "primary", "secondary", "canopy",
                                     "hull"};
    std::string Buffer() const
    {
        std::string out;
        for (const std::string& s : strings)
        {
            out += s;
            out += '\0';
        }
        return out;
    }
};

void TestText()
{
    World w;
    const std::string buffer = w.Buffer();
    Strings s;
    Check(s.Build(w.ids.data(), 7, buffer.data(), static_cast<uint32_t>(buffer.size()), 9) == Fault::None,
          "a buffer shaped like retail's is read");
    Check(s.Ready() && s.Count() == 9 && s.GlobalCount() == 7 && s.FirstMapIndex() == 5,
          "nine strings, seven globals, the map's own from namespace 0 index 5");
    Check(s.Text(0).empty(), "id 0 is the empty string");
    Check(s.Text(2) == "head" && s.Text(3) == "r_hand", "a namespace 0 global");
    Check(s.Text((1u << 19) | 1) == "secondary", "a global of another namespace");
    Check(s.Text(5) == "canopy" && s.Text(6) == "hull", "the map's own ids follow the globals");
    Check(s.Text(7).empty(), "an id past the map's own has no text");
    Check(s.Text((1u << 19) | 2).empty(), "an id the table does not hold has no text");
    Check(s.Text((2u << 19) | 0).empty(), "a namespace the table does not hold has no text");
    uint32_t n = 0;
    Check(s.Number(6, n) && n == 8, "a map id's number");
    Check(s.At(1) == "default" && s.At(9).empty(), "strings by number, and past the end");
}

void TestRefusals()
{
    World w;
    const std::string buffer = w.Buffer();
    const uint32_t size = static_cast<uint32_t>(buffer.size());
    Strings s;
    Check(s.Build(nullptr, 7, buffer.data(), size, 9) == Fault::NoGlobals && !s.Ready(), "no table");
    Check(s.Build(w.ids.data(), 0, buffer.data(), size, 9) == Fault::NoGlobals, "an empty table");
    std::vector<uint64_t> unordered = w.ids;
    std::swap(unordered[1], unordered[2]);
    Check(s.Build(unordered.data(), 7, buffer.data(), size, 9) == Fault::GlobalsOrder, "a table out of order");
    std::vector<uint64_t> wide = w.ids;
    wide[6] = (16ull << 19);
    Check(s.Build(wide.data(), 7, buffer.data(), size, 9) == Fault::GlobalsOrder, "a namespace past 15");
    std::vector<uint64_t> noZero{(1ull << 19), (1ull << 19) | 1};
    Check(s.Build(noZero.data(), 2, buffer.data(), size, 9) == Fault::NoMapIds, "no namespace 0");
    Check(s.Build(w.ids.data(), 7, buffer.data(), size, 6) == Fault::Count, "fewer strings than globals");
    Check(s.Build(w.ids.data(), 7, buffer.data(), size, 8) == Fault::Strings, "more strings than the header says");
    Check(s.Build(w.ids.data(), 7, buffer.data(), size, 10) == Fault::Strings, "fewer strings than the header says");
    std::string open = buffer;
    open.back() = 'x';
    Check(s.Build(w.ids.data(), 7, open.data(), size, 9) == Fault::Size, "an unterminated buffer");
    Check(s.Build(w.ids.data(), 7, buffer.data(), 0, 9) == Fault::Size, "an empty buffer");
    Check(s.Build(w.ids.data(), 7, buffer.data(), kMaxBufferCopy + 1, 9) == Fault::Size, "a buffer too large");
    Check(!s.Ready() && s.Text(2).empty(), "a refused buffer answers nothing");
    for (int i = 0; i < static_cast<int>(Fault::Total); ++i)
        Check(std::strcmp(FaultName(static_cast<Fault>(i)), "?") != 0, "every fault is named");
}

// ---- the signatures ----
void Put(std::vector<uint8_t>& image, size_t at, const char* pattern)
{
    size_t i = 0;
    for (const char* p = pattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        image[at + i++] = p[0] == '?' ? 0 : static_cast<uint8_t>(std::stoul(std::string(p, 2), nullptr, 16));
        p += 2;
    }
}
void PutRel(std::vector<uint8_t>& image, size_t site, size_t disp, int64_t target)
{
    const int32_t rel = static_cast<int32_t>(target - static_cast<int64_t>(site + disp + 4));
    std::memcpy(image.data() + site + disp, &rel, 4);
}
void PutCall(std::vector<uint8_t>& image, size_t site, size_t opcode, int64_t target)
{
    const int32_t rel = static_cast<int32_t>(target - static_cast<int64_t>(site + opcode + 5));
    std::memcpy(image.data() + site + opcode + 1, &rel, 4);
}

struct Image
{
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x40000, 0xCC);
    size_t loader = 0x1014, builder = 0x2000, call = 0x3000;
    int64_t header = 0x10000, count = 0x20000, buffer = 0x21000, table = 0x30000;
    uint32_t globals = 0xD24;
    void Build()
    {
        Put(bytes, loader, kLoaderPattern);
        Put(bytes, builder, kBuilderPattern);
        Put(bytes, call, kCallPattern);
        PutRel(bytes, loader, kLoaderCount, count);
        PutRel(bytes, loader, kLoaderBuffer, buffer);
        PutRel(bytes, builder, kBuilderTable, table);
        PutRel(bytes, builder, kBuilderBuffer, buffer);
        std::memcpy(bytes.data() + builder + kBuilderGlobals, &globals, 4);
        PutRel(bytes, call, kCallHeader, header);
        PutCall(bytes, call, kCallLoader, static_cast<int64_t>(loader) - 0x14);
    }
    Bind Decode(Sites& out) const
    {
        return DecodeSites(bytes.data() + loader, static_cast<int64_t>(loader), bytes.data() + builder,
                           static_cast<int64_t>(builder), bytes.data() + call, static_cast<int64_t>(call),
                           static_cast<int64_t>(bytes.size()), out);
    }
};

void TestDecode()
{
    {
        Image image;
        image.Build();
        Sites sites;
        Check(image.Decode(sites) == Bind::Bound, "an image built from the three patterns binds");
        Check(sites.headerRva == 0x10000 && sites.countRva == 0x20000 && sites.bufferRva == 0x21000 &&
                  sites.tableRva == 0x30000 && sites.globals == 0xD24,
              "each address decoded from its own site");
    }
    {
        Image image;
        image.Build();
        PutCall(image.bytes, image.call, kCallLoader, 0x500);
        Sites sites;
        Check(image.Decode(sites) == Bind::CallDisagrees, "a cache loader call that lands elsewhere is refused");
    }
    {
        Image image;
        image.Build();
        PutCall(image.bytes, image.call, kCallLoader, static_cast<int64_t>(image.loader) + 4);
        Sites sites;
        Check(image.Decode(sites) == Bind::CallDisagrees, "a call landing after the match is refused");
    }
    {
        Image image;
        image.Build();
        PutRel(image.bytes, image.builder, kBuilderBuffer, 0x22000);
        Sites sites;
        Check(image.Decode(sites) == Bind::BufferDisagrees, "the loader and builder naming two buffers is refused");
    }
    {
        Image image;
        image.table = 0x3FFF8; // its 0xD24 entries run past the image
        image.Build();
        Sites sites;
        Check(image.Decode(sites) == Bind::Implausible, "a table past the image is refused");
    }
    {
        Image image;
        image.globals = 0;
        image.Build();
        Sites sites;
        Check(image.Decode(sites) == Bind::Implausible, "no globals is refused");
    }
    {
        Image image;
        image.count = 0x20002;
        image.Build();
        Sites sites;
        Check(image.Decode(sites) == Bind::Implausible, "a misaligned count is refused");
    }
    Image image;
    image.Build();
    Sites sites;
    Check(DecodeSites(nullptr, 0, image.bytes.data(), 0, image.bytes.data(), 0, 1, sites) == Bind::LoaderMissing &&
              DecodeSites(image.bytes.data(), 0, nullptr, 0, image.bytes.data(), 0, 1, sites) ==
                  Bind::BuilderMissing &&
              DecodeSites(image.bytes.data(), 0, image.bytes.data(), 0, nullptr, 0, 1, sites) == Bind::CallMissing,
          "a missing site is named");
    for (int i = 0; i < static_cast<int>(Bind::Total); ++i)
        Check(std::strcmp(BindName(static_cast<Bind>(i)), "?") != 0, "every bind result is named");
}
} // namespace

int main()
{
    TestText();
    TestRefusals();
    TestDecode();
    std::printf("halo3_string_ids_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
