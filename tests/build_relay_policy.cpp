#include "../src/BuildRelayPolicy.h"
#include <cassert>
#include <string_view>

int case_build_relay_policy(int, char**) {
    using namespace AlterEgo::BuildRelay;
    assert(HasCodeShape("aB09"));
    assert(!HasCodeShape("abc"));
    assert(!HasCodeShape("abcde"));
    assert(!HasCodeShape("a/09"));
    assert(!HasCodeShape("AE2:"));
    assert(!AcceptsCode("aB09", false));
    assert(AcceptsCode("aB09", true));
    assert(!AcceptsCode("a/09", true));
    assert(AcceptsCode("aB09") == Enabled);
    for (const bool enabled : {false, true}) {
        for (const char* text : {ImportHint(enabled), ImportTooltip(enabled),
                                 EmptyImportError(enabled), ImportNameLabel(enabled)}) {
            assert((std::string_view(text).find("one-time code") != std::string_view::npos) == enabled);
        }
        assert(std::string_view(ImportHint(enabled)).find("AE2") != std::string_view::npos);
        assert(std::string_view(ImportTooltip(enabled)).find("Snow Crows") != std::string_view::npos);
    }
    return 0;
}
