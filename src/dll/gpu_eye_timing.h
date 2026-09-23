#pragma once
#include <d3d11.h>
#include <cstdint>

// Render-thread diagnostic. Queries are allocated in Present, never in an
// eye hook. Busy samples are dropped, and readback never flushes or waits.
class GpuEyeTiming
{
    struct Slot
    {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* stamp[3]{};
        bool pending = false;
        bool rasterEnded = false;
        bool valid = false;
        bool dlss = false;
        unsigned width = 0, height = 0;
    };
    Slot slots_[8]{};
    int cursor_ = 0, active_ = -1;
    bool ready_ = false;
public:
    struct Result
    {
        double rasterUs = 0, prepareUs = 0;
        unsigned eyes = 0, width = 0, height = 0;
        bool dlss = false;
    };
    bool Init(ID3D11Device* device)
    {
        if (ready_) return true;
        for (auto& slot : slots_)
        {
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(device->CreateQuery(&desc, &slot.disjoint))) { Release(); return false; }
            desc.Query = D3D11_QUERY_TIMESTAMP;
            for (auto& stamp : slot.stamp)
                if (FAILED(device->CreateQuery(&desc, &stamp))) { Release(); return false; }
        }
        ready_ = true;
        return true;
    }
    void Release()
    {
        for (auto& slot : slots_)
        {
            if (slot.disjoint) slot.disjoint->Release();
            for (auto* stamp : slot.stamp) if (stamp) stamp->Release();
            slot = {};
        }
        ready_ = false; active_ = -1; cursor_ = 0;
    }
    void Begin(ID3D11DeviceContext* context, bool dlss, unsigned width, unsigned height)
    {
        if (!ready_ || !context) return;
        if (active_ >= 0) // abandoned eye: close the query and discard it
        {
            slots_[active_].valid = false;
            EndRaster(context);
            End(context);
        }
        auto& slot = slots_[cursor_];
        if (slot.pending) return;
        slot.dlss = dlss; slot.width = width; slot.height = height;
        slot.rasterEnded = false;
        slot.valid = true;
        context->Begin(slot.disjoint);
        context->End(slot.stamp[0]);
        active_ = cursor_;
    }
    void EndRaster(ID3D11DeviceContext* context)
    {
        if (active_ < 0 || !context) return;
        auto& slot = slots_[active_];
        if (!slot.rasterEnded) context->End(slot.stamp[1]);
        slot.rasterEnded = true;
    }
    void End(ID3D11DeviceContext* context)
    {
        if (active_ < 0 || !context) return;
        auto& slot = slots_[active_];
        EndRaster(context);
        context->End(slot.stamp[2]);
        context->End(slot.disjoint);
        slot.pending = true;
        active_ = -1; cursor_ = (cursor_ + 1) % 8;
    }
    Result Collect(ID3D11DeviceContext* context, bool dlss, unsigned width, unsigned height)
    {
        Result result{}; result.dlss = dlss; result.width = width; result.height = height;
        if (!ready_ || !context) return result;
        for (auto& slot : slots_)
        {
            if (!slot.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            if (context->GetData(slot.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
            uint64_t t[3]{}; bool complete = true;
            for (int i = 0; i < 3; ++i)
                complete &= context->GetData(slot.stamp[i], &t[i], sizeof(t[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (!complete) continue;
            slot.pending = false;
            if (!slot.valid || dj.Disjoint || !dj.Frequency || t[1] < t[0] || t[2] < t[1] ||
                slot.dlss != dlss || slot.width != width || slot.height != height) continue;
            result.rasterUs += (t[1] - t[0]) * 1e6 / double(dj.Frequency);
            result.prepareUs += (t[2] - t[1]) * 1e6 / double(dj.Frequency);
            ++result.eyes;
        }
        return result;
    }
};
