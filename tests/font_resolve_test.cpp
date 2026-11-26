#include "../src/FontResolve.h"
#include <cstdio>
#include <cstdlib>
using namespace AlterEgo::Font;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", #cond, __LINE__); ++g_fail; } } while (0)

int case_font_resolve_test(int, char**) {
    // Default face -> empty id, IsDefault true.
    Config d; // {NexusDefault, "", 16}
    CHECK(Identifier(d).empty());
    CHECK(IsDefault(d));

    // Original persisted face 1 (bundled Inter) now resolves to Nexus default;
    // face 2 is still a custom font. Unknown values also fall back safely.
    CHECK(DecodeFace(0)==Face::NexusDefault);
    CHECK(DecodeFace(1)==Face::NexusDefault);
    CHECK(DecodeFace(2)==Face::Custom);
    CHECK(DecodeFace(99)==Face::NexusDefault);
    Config old{DecodeFace(1), "", 16.0f};
    CHECK(Identifier(old).empty());
    CHECK(IsDefault(old));

    // Custom id depends on path; same path+px -> same id, different path -> different id.
    Config ca{Face::Custom, "/x/a.ttf", 16.0f};
    Config ca2{Face::Custom, "/x/a.ttf", 16.0f};
    Config cb{Face::Custom, "/x/b.ttf", 16.0f};
    CHECK(Identifier(ca) == Identifier(ca2));
    CHECK(Identifier(ca) != Identifier(cb));
    CHECK(Identifier(ca).rfind("AlterEgo_F_C", 0) == 0);
    Config c18{Face::Custom, "/x/a.ttf", 18.0f};
    Config c1799{Face::Custom, "/x/a.ttf", 17.99f};
    CHECK(Identifier(c18)==Identifier(c1799));
    CHECK(Identifier(ca)!=Identifier(c18));

    if (g_fail == 0) { printf("ALL PASS\n"); return 0; }
    printf("%d FAILURE(S)\n", g_fail); return 1;
}
