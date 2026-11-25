#pragma once
#include <algorithm>
#include <cmath>
namespace AlterEgo::UI {
inline float FitSplitWidth(float preferred,float available,float overhead,float minLeft,float minRight) {
    const float usable=std::max(2.0f,available-overhead);
    const float low=std::min(minLeft,usable*0.5f);
    const float high=std::max(low,usable-std::min(minRight,usable*0.5f));
    return std::clamp(std::isfinite(preferred)?preferred:low,low,high);
}
}
