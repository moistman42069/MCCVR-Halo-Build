#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// THE SEARCHES OF A TITLE'S INSTALL, SAVED FOR THE NEXT LAUNCH (src/dll/install_search_cache.cpp).
//
// A title's install asks a few hundred searches of its game DLL, and the answers depend on nothing but that DLL's
// bytes. Within one load of the DLL the scanner already remembers them (sigscan.h, ImageMemo); this saves what it
// remembers to a file, per BUILD of the DLL, so the first install of the next launch starts from them instead of
// from nothing. Every answer taken from the file is still checked as the scanner checks one it remembers in the
// same load: a match is compared again, in place, by the compare the search itself makes there, and searched for
// again when it no longer stands; "no match" is searched for again; only "no second match after the first" is
// taken as saved, which the build identity below is what vouches for.
//
// WHICH BUILD. An answer is kept for exactly one build of the DLL: its PE TimeDateStamp, SizeOfImage and CheckSum,
// the CodeView (PDB) GUID and age the linker writes on every link, and a hash of its executable sections as they
// stand in memory with this mod's own hooks undone. Another store's copy of the same file, a patched DLL under the
// same stamp, or a host that patches the game's code before the install, all hash differently and get their own
// entry. The scanner's own rules (what a search answers) carry a number too: a mod whose searches answer
// differently does not take another's answers.
//
// THE FILE. Binary, little-endian; a header, up to kMaxBuilds builds each with its entries, and a checksum of all
// of it. Anything that does not read back whole (wrong magic or format, a count out of range, an entry outside its
// image, a checksum that does not match) is not used at all, and the next save replaces it. Pure logic here, no
// Windows calls: the tests run it on bytes.
namespace install_search_cache
{
    // The scanner's rules: what sig::Find answers (the first match in the range, of the image with this mod's
    // enabled hooks undone). A change to what a search answers changes this, and every saved answer is dropped.
    constexpr uint32_t kSearchRules = 1;
    constexpr uint32_t kFormat = 1;
    constexpr uint32_t kMaxBuilds = 4;
    constexpr uint32_t kMaxEntries = 1024;
    constexpr uint32_t kNoMatch = 0xFFFFFFFFu;
    constexpr char kMagic[8] = {'H', 'M', 'V', 'R', 'I', 'S', 'C', 'H'};

    struct Identity
    {
        char title[16]{};            // the title's name, zero-padded
        uint32_t timeDateStamp = 0;
        uint32_t sizeOfImage = 0;
        uint32_t checkSum = 0;
        uint32_t pdbAge = 0;
        uint8_t pdbGuid[16]{};
        uint64_t codeHash = 0;       // the executable sections, this mod's hooks undone (see above)
        uint64_t codeBytes = 0;      // how many bytes that hash covers
        uint32_t rules = kSearchRules;

        bool operator==(const Identity& o) const
        {
            return !std::memcmp(title, o.title, sizeof(title)) && timeDateStamp == o.timeDateStamp &&
                   sizeOfImage == o.sizeOfImage && checkSum == o.checkSum && pdbAge == o.pdbAge &&
                   !std::memcmp(pdbGuid, o.pdbGuid, sizeof(pdbGuid)) && codeHash == o.codeHash &&
                   codeBytes == o.codeBytes && rules == o.rules;
        }
        bool operator!=(const Identity& o) const { return !(*this == o); }
    };

    // One search and its answer, as sigscan's memo keeps it: a hash of the pattern's text, the searched range as
    // offsets in the image, and the offset of the first match (kNoMatch: none).
    struct Entry
    {
        uint64_t pattern = 0;
        uint32_t start = 0;
        uint32_t length = 0;
        uint32_t match = kNoMatch;
    };

    struct Build
    {
        Identity identity;
        std::vector<Entry> entries;
    };

    // A hash for the identity: 64-bit, word at a time. Not a defence against anyone; it tells builds apart.
    struct CodeHash
    {
        uint64_t state = 0x6A09E667F3BCC908ull;
        uint64_t bytes = 0;
        void Add(const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            bytes += size;
            while (size >= 8)
            {
                uint64_t word;
                std::memcpy(&word, p, 8);
                Mix(word);
                p += 8;
                size -= 8;
            }
            if (size)
            {
                uint64_t word = 0;
                std::memcpy(&word, p, size);
                Mix(word ^ (static_cast<uint64_t>(size) << 56));
            }
        }
        uint64_t Final() const
        {
            uint64_t h = state ^ bytes;
            h ^= h >> 33;
            h *= 0xFF51AFD7ED558CCDull;
            h ^= h >> 33;
            h *= 0xC4CEB9FE1A85EC53ull;
            h ^= h >> 33;
            return h;
        }

    private:
        void Mix(uint64_t word)
        {
            state ^= word;
            state *= 0x9E3779B97F4A7C15ull;
            state ^= state >> 29;
        }
    };

    inline uint64_t Fnv64(const uint8_t* data, size_t size)
    {
        uint64_t h = 14695981039346656037ull;
        for (size_t i = 0; i < size; ++i)
        {
            h ^= data[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    namespace detail
    {
        inline void Put(std::vector<uint8_t>& out, const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            out.insert(out.end(), p, p + size);
        }
        inline void Put32(std::vector<uint8_t>& out, uint32_t v) { Put(out, &v, 4); }
        inline void Put64(std::vector<uint8_t>& out, uint64_t v) { Put(out, &v, 8); }

        struct Reader
        {
            const uint8_t* data;
            size_t size;
            size_t at = 0;
            bool Take(void* out, size_t n)
            {
                if (n > size - at)
                    return false;
                std::memcpy(out, data + at, n);
                at += n;
                return true;
            }
            bool Take32(uint32_t& v) { return Take(&v, 4); }
            bool Take64(uint64_t& v) { return Take(&v, 8); }
        };

        inline void PutIdentity(std::vector<uint8_t>& out, const Identity& id)
        {
            Put(out, id.title, sizeof(id.title));
            Put32(out, id.timeDateStamp);
            Put32(out, id.sizeOfImage);
            Put32(out, id.checkSum);
            Put32(out, id.pdbAge);
            Put(out, id.pdbGuid, sizeof(id.pdbGuid));
            Put64(out, id.codeHash);
            Put64(out, id.codeBytes);
            Put32(out, id.rules);
        }
        inline bool TakeIdentity(Reader& in, Identity& id)
        {
            return in.Take(id.title, sizeof(id.title)) && in.Take32(id.timeDateStamp) && in.Take32(id.sizeOfImage) &&
                   in.Take32(id.checkSum) && in.Take32(id.pdbAge) && in.Take(id.pdbGuid, sizeof(id.pdbGuid)) &&
                   in.Take64(id.codeHash) && in.Take64(id.codeBytes) && in.Take32(id.rules);
        }
    }

    // Whether an entry can belong to an image of `sizeOfImage` bytes: its range inside the image, and its match
    // inside the range.
    inline bool EntryFits(const Entry& e, uint32_t sizeOfImage)
    {
        if (!e.length || e.start >= sizeOfImage || e.length > sizeOfImage - e.start)
            return false;
        return e.match == kNoMatch || (e.match >= e.start && e.match - e.start < e.length);
    }

    inline std::vector<uint8_t> Serialize(const std::vector<Build>& builds)
    {
        using namespace detail;
        std::vector<uint8_t> out;
        Put(out, kMagic, sizeof(kMagic));
        Put32(out, kFormat);
        const uint32_t count = static_cast<uint32_t>(builds.size() < kMaxBuilds ? builds.size() : kMaxBuilds);
        Put32(out, count);
        for (uint32_t b = 0; b < count; ++b)
        {
            const Build& build = builds[builds.size() - count + b]; // the newest kMaxBuilds
            PutIdentity(out, build.identity);
            const uint32_t entries =
                static_cast<uint32_t>(build.entries.size() < kMaxEntries ? build.entries.size() : kMaxEntries);
            Put32(out, entries);
            for (uint32_t i = 0; i < entries; ++i)
            {
                Put64(out, build.entries[i].pattern);
                Put32(out, build.entries[i].start);
                Put32(out, build.entries[i].length);
                Put32(out, build.entries[i].match);
            }
        }
        Put64(out, Fnv64(out.data(), out.size()));
        return out;
    }

    // False: the bytes are not a whole file of this format. Nothing of a file that is not whole is used.
    inline bool Parse(const uint8_t* data, size_t size, std::vector<Build>& builds)
    {
        using namespace detail;
        builds.clear();
        if (!data || size < sizeof(kMagic) + 8 + 8)
            return false;
        uint64_t sum = 0;
        std::memcpy(&sum, data + size - 8, 8);
        if (sum != Fnv64(data, size - 8))
            return false;
        Reader in{data, size - 8};
        char magic[8];
        uint32_t format = 0, count = 0;
        if (!in.Take(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) || !in.Take32(format) ||
            format != kFormat || !in.Take32(count) || count > kMaxBuilds)
            return false;
        for (uint32_t b = 0; b < count; ++b)
        {
            Build build;
            uint32_t entries = 0;
            if (!TakeIdentity(in, build.identity) || !in.Take32(entries) || entries > kMaxEntries)
                return false;
            build.entries.resize(entries);
            for (Entry& e : build.entries)
                if (!in.Take64(e.pattern) || !in.Take32(e.start) || !in.Take32(e.length) || !in.Take32(e.match) ||
                    !EntryFits(e, build.identity.sizeOfImage))
                    return false;
            builds.push_back(std::move(build));
        }
        if (in.at != in.size)
            return false;
        return true;
    }

    // The build of exactly this identity, or null.
    inline const Build* Find(const std::vector<Build>& builds, const Identity& identity)
    {
        for (const Build& b : builds)
            if (b.identity == identity)
                return &b;
        return nullptr;
    }

    // This build's entries are gone; every other build is kept. False: the file held none for it.
    inline bool Drop(std::vector<Build>& builds, const Identity& identity)
    {
        for (size_t i = 0; i < builds.size(); ++i)
            if (builds[i].identity == identity)
            {
                builds.erase(builds.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    // This build's entries replace whatever the file held for it, and it becomes the newest; every other build is
    // kept, the oldest giving way past kMaxBuilds.
    inline void Put(std::vector<Build>& builds, const Build& build)
    {
        for (size_t i = 0; i < builds.size(); ++i)
            if (builds[i].identity == build.identity)
            {
                builds.erase(builds.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        builds.push_back(build);
        while (builds.size() > kMaxBuilds)
            builds.erase(builds.begin());
    }
}
