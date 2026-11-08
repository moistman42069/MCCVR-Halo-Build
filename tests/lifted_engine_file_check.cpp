// The offline check of lifted_engine_logic.h against a real engine.code: the table reads, and every adapter in it
// decodes. The file is HALOMCCVR_RECLAIMER_ENGINE_CODE, or Project Reclaimer's own cache under %LOCALAPPDATA%. Without
// either it exits 77, which ctest reports as skipped.

#include "../src/common/lifted_engine_logic.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main()
{
    std::string path;
    if (const char* named = std::getenv("HALOMCCVR_RECLAIMER_ENGINE_CODE"))
        path = named;
    else if (const char* local = std::getenv("LOCALAPPDATA"))
        path = std::string(local) + "\\ProjectReclaimer\\Cache\\native\\retail-engine\\engine.code";
    FILE* f = nullptr;
    if (path.empty() || fopen_s(&f, path.c_str(), "rb") != 0 || !f)
    {
        std::printf("no engine.code (%s): skipped\n", path.c_str());
        return 77;
    }
    std::vector<uint8_t> data;
    uint8_t buffer[1 << 16];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
        data.insert(data.end(), buffer, buffer + n);
    fclose(f);

    lifted_engine::Table table;
    const lifted_engine::ParseResult result = lifted_engine::Parse(data.data(), data.size(), table);
    std::printf("%s: %s, %zu records, code %#zx bytes at %#zx\n", path.c_str(), lifted_engine::ParseResultName(result),
                table.records.size(), table.codeLength, table.codeOffset);
    if (result != lifted_engine::ParseResult::Ok)
        return 1;
    size_t failed = 0, returning = 0, bodiless = 0, args[5]{};
    for (const lifted_engine::Record& record : table.records)
    {
        if (record.hostOffset + record.hostSize > table.codeLength)
        {
            std::printf("record %#x: its block runs past the code\n", record.guestRva);
            return 1;
        }
        if (!lifted_engine::HasBody(record))
        {
            ++bodiless;
            continue;
        }
        lifted_engine::Adapter adapter;
        const uint8_t* block = data.data() + table.codeOffset + record.hostOffset;
        if (!lifted_engine::DecodeAdapter(block, record.bodyOffset, adapter))
        {
            if (failed++ < 10)
                std::printf("record %#x: adapter not decoded\n", record.guestRva);
            continue;
        }
        returning += adapter.returnsValue;
        int count = 0;
        for (const int8_t reg : adapter.hostRegOfGuestArg)
            count += reg != lifted_engine::kNoReg;
        ++args[count];
    }
    std::printf("adapters: %zu not decoded, %zu records without a body; %zu return a value; integer arguments "
                "0/1/2/3/4: %zu/%zu/%zu/%zu/%zu\n", failed, bodiless, returning, args[0], args[1], args[2], args[3],
                args[4]);
    return failed ? 1 : 0;
}
