#include "../src/AutoRefreshPolicy.h"
#include <cassert>
int main() {
    using namespace AlterEgo::AutoRefreshPolicy;
    assert(!Enabled());
    assert(!AllowRequest(false));
    assert(AllowRequest(true));
    SetEnabled(true);
    assert(Enabled());
    assert(AllowRequest(false));
    assert(AllowRequest(true));
    SetEnabled(false);
    assert(!Enabled());
    assert(!AllowRequest(false));
    assert(AllowRequest(true));
    return 0;
}
