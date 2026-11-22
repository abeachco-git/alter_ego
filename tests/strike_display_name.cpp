#include "../src/StrikeDisplayName.h"
#include <cassert>
using namespace AlterEgo;
int main() {
    assert(StrikeDisplayName("Voice of the Fallen and Claw of the Fallen") == "Bears");
    assert(StrikeDisplayName("Voice and Claw of the Fallen") == "Bears");
    assert(StrikeDisplayName("Voice and Claw") == "Bears");
    assert(StrikeDisplayName("Bears") == "Bears");
    assert(StrikeDisplayName("Whisper of Jormag") == "Whisper");
    assert(StrikeDisplayName("Boneskinner") == "Boneskinner");
    return 0;
}
