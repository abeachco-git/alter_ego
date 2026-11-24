#include "../src/StrikeDisplayName.h"
#include "../src/StrikePicker.h"
#include <algorithm>
#include <string>
#include <vector>
#include <cassert>
using namespace AlterEgo;
int case_strike_display_name(int, char**) {
    assert(StrikeDisplayName("Whisper of Jormag") == "Whisper");
    assert(StrikeDisplayName("Boneskinner") == "Boneskinner");

    // The abbreviated visual label is NOT the source Weekly Strikes API bit.
    // All currently recognized source variants must map to the first slot and
    // to Icebrood Saga, regardless of how the UI displays them.
    for (const char* bears : {
             "Bears", "Voice and Claw", "Voice and Claw of the Fallen",
             "Voice of the Fallen and Claw of the Fallen",
             "Voice of the Fallen Claw of the Fallen"}) {
        assert(StrikeDisplayName(bears) == "Bears");
        assert(std::string(StrikePickerCategory(bears)) == "Icebrood Saga");
        assert(StrikePickerOrder(bears) == 10);
    }
    assert(std::string(StrikePickerCategory("Old Lion's Court")) == "Core Game");
    assert(StrikePickerOrder("Old Lion's Court") == 1000);

    // Sort the real source bit names, NOT the display aliases. Bears must
    // occupy OLC's original first position; OLC moves to Bears' old last
    // position (before the separately rendered Nexus placeholder).
    std::vector<std::string> strikes = {
        "Old Lion's Court", "Shiverpeaks Pass", "Fraenir of Jormag",
        "Boneskinner", "Whisper of Jormag", "Cold War",
        "Aetherblade Hideout", "Xunlai Jade Junkyard", "Kaineng Overlook",
        "Harvest Temple", "Cosmic Observatory", "Temple of Febe",
        "Guardian's Glade", "Voice and Claw of the Fallen"
    };
    std::stable_sort(strikes.begin(), strikes.end(),
        [](const std::string& a, const std::string& b) {
            return StrikePickerOrder(a) < StrikePickerOrder(b);
        });
    assert(strikes.size() == 14);
    assert(StrikeDisplayName(strikes.front()) == "Bears");
    assert(strikes[1] == "Shiverpeaks Pass");
    assert(strikes[12] == "Guardian's Glade");
    assert(strikes.back() == "Old Lion's Court");
    return 0;
}
