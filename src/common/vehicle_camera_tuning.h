#pragma once
// THE PLAYER'S LAG TUNING (F1 > Vehicles), laid over the camera file: for a camera of an entry, the lag and its
// damping the player set from the menu while sitting in it, in place of the file's ("lag", "lagDamping"). Kept in
// halomccvr_vehicle_cameras_tuning.txt beside the camera file -- a file of its own, so the mod never rewrites the
// player's camera file and the notes and layout in it.
//
// A camera is keyed by its game, its entry and its name (vehicle_camera_select::Selector::TuningKey): one line each,
//   game <tab> entry <tab> camera <tab> lag seconds <tab> damping
// Lines starting with '#' are comments. Pure: the caller reads and writes the file.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "vehicle_camera_file.h"

namespace vehicle_camera_tuning
{
struct Tuning
{
    float lag = 0.0f;
    float damping = 1.0f;
};

// The values a slider can give, held to what the camera file accepts.
inline Tuning Held(Tuning tuning) noexcept
{
    if (!std::isfinite(tuning.lag) || tuning.lag < 0.0f)
        tuning.lag = 0.0f;
    if (tuning.lag > vehicle_camera_file::kMaxLagSeconds)
        tuning.lag = vehicle_camera_file::kMaxLagSeconds;
    if (!std::isfinite(tuning.damping))
        tuning.damping = 1.0f;
    if (tuning.damping < vehicle_camera_file::kMinLagDamping)
        tuning.damping = vehicle_camera_file::kMinLagDamping;
    if (tuning.damping > vehicle_camera_file::kMaxLagDamping)
        tuning.damping = vehicle_camera_file::kMaxLagDamping;
    return tuning;
}

class Overlay
{
public:
    bool Find(const std::string& key, Tuning& out) const
    {
        const auto found = tunings_.find(key);
        if (found == tunings_.end())
            return false;
        out = found->second;
        return true;
    }

    void Set(const std::string& key, Tuning tuning)
    {
        if (key.empty())
            return;
        tuning = Held(tuning);
        const auto found = tunings_.find(key);
        if (found != tunings_.end() && found->second.lag == tuning.lag && found->second.damping == tuning.damping)
            return;
        tunings_[key] = tuning;
        dirty_ = true;
    }

    // Back to the camera file's own. False when the camera had no tuning.
    bool Clear(const std::string& key)
    {
        if (tunings_.erase(key) == 0)
            return false;
        dirty_ = true;
        return true;
    }

    std::string Text() const
    {
        std::string text =
            "# Your lag tuning for the vehicle cameras, set in F1 > Vehicles while sitting in the camera -- written by\n"
            "# the mod, read when it starts. It takes the place of the camera file's lag and lagDamping for that\n"
            "# camera. Delete a line, or this file, to go back to the camera file's own.\n"
            "# game\tentry\tcamera\tlag seconds\tdamping\n";
        char numbers[64];
        for (const auto& [key, tuning] : tunings_)
        {
            std::snprintf(numbers, sizeof(numbers), "\t%.3g\t%.3g\n", static_cast<double>(tuning.lag),
                          static_cast<double>(tuning.damping));
            text += key + numbers;
        }
        return text;
    }

    // Replaces what is held with the file's lines; returns how many were read. A line that does not read is
    // skipped.
    int Load(const std::string& text)
    {
        tunings_.clear();
        int read = 0;
        std::size_t at = 0;
        while (at < text.size())
        {
            std::size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            // The key is the first three fields; the numbers are the last two.
            const std::size_t damp = line.rfind('\t');
            const std::size_t lag = damp == std::string::npos || damp == 0 ? std::string::npos
                                                                           : line.rfind('\t', damp - 1);
            if (lag == std::string::npos)
                continue;
            const std::string key = line.substr(0, lag);
            int tabs = 0;
            for (const char c : key)
                tabs += c == '\t' ? 1 : 0;
            if (tabs != 2)
                continue;
            char* stop = nullptr;
            const std::string lagText = line.substr(lag + 1, damp - lag - 1);
            const float lagValue = std::strtof(lagText.c_str(), &stop);
            if (stop == lagText.c_str() || *stop != '\0')
                continue;
            const std::string dampText = line.substr(damp + 1);
            const float dampValue = std::strtof(dampText.c_str(), &stop);
            if (stop == dampText.c_str() || *stop != '\0')
                continue;
            tunings_[key] = Held(Tuning{lagValue, dampValue});
            ++read;
        }
        dirty_ = false;
        return read;
    }

    bool Dirty() const noexcept { return dirty_; }
    void Saved() noexcept { dirty_ = false; }
    std::size_t Size() const noexcept { return tunings_.size(); }

private:
    std::map<std::string, Tuning> tunings_;
    bool dirty_ = false;
};
} // namespace vehicle_camera_tuning
