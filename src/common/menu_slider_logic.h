#pragma once
#include <algorithm>
#include <cmath>

// One click changes the last displayed digit, including sliders whose menu
// value is a percentage or another conversion of the saved setting.
inline double MenuSliderStep(const char* format) noexcept
{
    if(!format) return 0.001;
    for(const char* p=format; *p; ++p)
    {
        if(*p!='%') continue;
        if(p[1]=='%') { ++p; continue; }
        for(++p; *p && *p!='f' && *p!='F' && *p!='d' && *p!='i'; ++p)
        {
            if(*p!='.') continue;
            unsigned precision=0;
            for(++p; *p>='0' && *p<='9'; ++p)
                precision=std::min(6u,precision*10+unsigned(*p-'0'));
            double step=1;
            while(precision--) step*=0.1;
            return step;
        }
        return 1.0;
    }
    return 1.0;
}

inline float MenuSliderNudge(float value, float minimum, float maximum,
    double step, int direction) noexcept
{
    if(!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum>maximum || !std::isfinite(step) || step<=0 || !direction)
        return value;
    const double tick=std::round(double(value)/step)+(direction>0 ? 1 : -1);
    return float(std::clamp(tick*step,double(minimum),double(maximum)));
}

// Unit-fraction <-> percent mapping for sliders that store a 0..1 setting but
// display a 0..100% label (the offhand-influence control). The percent value
// is what the slider shows and edits, so the default 0.50 fraction reads
// "50%" and never the raw fraction. Both directions clamp to the valid range
// and map non-finite input to 0, so a malformed config can never print or save
// a garbage value.
inline float MenuSliderPercentFromUnit(float unit) noexcept
{
    if(!std::isfinite(unit)) return 0.0f;
    return std::clamp(unit,0.0f,1.0f)*100.0f;
}

inline float MenuSliderUnitFromPercent(float percent) noexcept
{
    if(!std::isfinite(percent)) return 0.0f;
    return std::clamp(percent,0.0f,100.0f)/100.0f;
}
