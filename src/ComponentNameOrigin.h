#pragma once
#include <cstdint>

namespace AlterEgo::Composable {
// Name metadata is independent of component identity and exact payload.
enum class NameOrigin : int { Unknown=0, Manual=1, Automatic=2, AutomaticPending=3 };
inline bool ValidNameOrigin(NameOrigin value){
    return value==NameOrigin::Unknown||value==NameOrigin::Manual||
        value==NameOrigin::Automatic||value==NameOrigin::AutomaticPending;
}
inline bool IsAutomaticName(NameOrigin value){
    return value==NameOrigin::Automatic||value==NameOrigin::AutomaticPending;
}
}
