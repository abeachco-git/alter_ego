#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace FractalCardLayout {
struct Card {
    std::size_t index;
    float minimum;
    bool fixed;
    bool visible;
};
struct Placement {
    std::size_t index;
    int row;
    float x, width;
};
struct Layout {
    int rows = 0;
    std::vector<Placement> cards;
};
// Pack visible cards in their original order. Give spare row space only
// to flexible daily cards; the recommended card never grows.
inline Layout Pack(const std::vector<Card>& input, float width, float gap) {
    Layout out;
    if (width <= 0) return out;
    std::vector<Card> row;
    float used = 0;
    auto flush = [&] {
        if (row.empty()) return;
        int flexible = 0;
        for (const auto& c : row) if (!c.fixed) ++flexible;
        const float extra = flexible ? std::max(0.0f, width - used) / flexible : 0.0f;
        float x = 0;
        for (const auto& c : row) {
            const float w = c.minimum + (c.fixed ? 0.0f : extra);
            out.cards.push_back({c.index, out.rows, x, w});
            x += w + gap;
        }
        ++out.rows;
        row.clear();
        used = 0;
    };
    for (auto c : input) {
        if (!c.visible) continue;
        c.minimum = std::clamp(c.minimum, 0.0f, width);
        if (!row.empty() && used + gap + c.minimum > width) flush();
        used += (row.empty() ? 0.0f : gap) + c.minimum;
        row.push_back(c);
    }
    flush();
    return out;
}
}
