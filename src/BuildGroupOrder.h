#pragma once
// Stable, dependency-free grouping over the existing flat build-library format.
// The order of builds inside every profession survives group reorder, including
// for older library files in which professions were interleaved.
#include <algorithm>
#include <string>
#include <vector>
namespace AlterEgo::BuildGroupOrder {
template<class T>
std::vector<std::string> Groups(const std::vector<T>& items) {
    std::vector<std::string> groups;
    for (const auto& item : items)
        if (std::find(groups.begin(), groups.end(), item.profession) == groups.end())
            groups.push_back(item.profession);
    return groups;
}
template<class T>
void Regroup(std::vector<T>& items, const std::vector<std::string>& order) {
    std::vector<T> grouped;
    grouped.reserve(items.size());
    for (const auto& prof : order)
        for (const auto& item : items)
            if (item.profession == prof) grouped.push_back(item);
    items.swap(grouped);
}
template<class T>
bool MoveGroup(std::vector<T>& items,
               const std::string& movedProf, const std::string& targetProf, bool after) {
    if (movedProf == targetProf) return false;
    auto order = Groups(items);
    const auto a = std::find(order.begin(), order.end(), movedProf);
    const auto b = std::find(order.begin(), order.end(), targetProf);
    if (a == order.end() || b == order.end()) return false;
    order.erase(a);
    auto target = std::find(order.begin(), order.end(), targetProf);
    if (after) ++target;
    order.insert(target, movedProf);
    Regroup(items, order);
    return true;
}
template<class T>
bool MoveWithinGroup(std::vector<T>& items,
                     const std::string& movedId, const std::string& targetId, bool after) {
    if (movedId == targetId) return false;
    const auto moved = std::find_if(items.begin(), items.end(),
        [&](const T& b) { return b.id == movedId; });
    const auto target = std::find_if(items.begin(), items.end(),
        [&](const T& b) { return b.id == targetId; });
    if (moved == items.end() || target == items.end() ||
        moved->profession != target->profession) return false;
    const auto groups = Groups(items);
    std::vector<T> arranged;
    arranged.reserve(items.size());
    for (const auto& prof : groups) {
        if (prof != moved->profession) {
            for (const auto& b : items)
                if (b.profession == prof) arranged.push_back(b);
            continue;
        }
        for (const auto& b : items) {
            if (b.profession != prof || b.id == movedId) continue;
            if (b.id == targetId && !after) arranged.push_back(*moved);
            arranged.push_back(b);
            if (b.id == targetId && after) arranged.push_back(*moved);
        }
    }
    items.swap(arranged);
    return true;
}
} // namespace AlterEgo::BuildGroupOrder
