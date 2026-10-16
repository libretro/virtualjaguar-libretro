#include "tv/tv_focus.h"

#include <algorithm>

namespace vj::tv {

bool LinearFocus::move(int delta) {
    int n = index + delta;
    if (n < 0 || n >= count) return false;
    index = n;
    return true;
}

void LinearFocus::clampTo(int newCount) {
    count = std::max(0, newCount);
    index = std::clamp(index, 0, std::max(0, count - 1));
    first = std::clamp(first, 0, std::max(0, count - 1));
}

void LinearFocus::scrollToShow(int visible) {
    if (visible <= 0) return;
    if (index < first) first = index;
    else if (index >= first + visible) first = index - visible + 1;
    if (first < 0) first = 0;
}

int GridFocus::row() const { return cols > 0 ? index / cols : 0; }
int GridFocus::col() const { return cols > 0 ? index % cols : 0; }
int GridFocus::rows() const { return cols > 0 ? (count + cols - 1) / cols : 0; }

GridFocus::Edge GridFocus::move(int dx, int dy) {
    if (count <= 0 || cols <= 0) {
        if (dx) return dx < 0 ? Left : Right;
        return dy < 0 ? Top : Bottom;
    }
    int n = index;
    if (dx) {
        int c = col() + dx;
        n = index + dx;
        if (c < 0) return Left;
        if (c >= cols || n >= count) return Right;
    }
    if (dy) {
        int r = row() + dy;
        if (r < 0) return Top;
        if (r >= rows()) return Bottom;
        n = std::min(n + dy * cols, count - 1);   // a short last row: land on its last item
    }
    index = n;                                    // nothing moves unless the whole move fits
    return None;
}

void GridFocus::scrollToShow(int visibleRows) {
    if (visibleRows <= 0) return;
    int r = row();
    if (r < firstRow) firstRow = r;
    else if (r >= firstRow + visibleRows) firstRow = r - visibleRows + 1;
    if (firstRow < 0) firstRow = 0;
}

int FocusMemory::get(const std::string& key, int def) const {
    auto it = m_.find(key);
    return it == m_.end() ? def : it->second;
}

void FocusMemory::put(const std::string& key, int index) { m_[key] = index; }

char initialOf(const std::string& title) {
    if (title.empty()) return '#';
    unsigned char c = (unsigned char)title[0];
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (char)c;
    return '#';
}

int jumpInitial(const std::vector<std::string>& titles, int from, int dir) {
    int n = (int)titles.size();
    if (n == 0 || from < 0 || from >= n || dir == 0) return from;
    char cur = initialOf(titles[from]);
    if (dir > 0) {
        for (int i = from + 1; i < n; i++)
            if (initialOf(titles[i]) != cur) return i;
        return from;
    }
    int s = from;                                         // start of the current initial's run
    while (s > 0 && initialOf(titles[s - 1]) == cur) s--;
    if (s == 0) return from;
    char prev = initialOf(titles[s - 1]);
    int p = s - 1;
    while (p > 0 && initialOf(titles[p - 1]) == prev) p--;
    return p;
}

int firstWithInitial(const std::vector<std::string>& titles, char initial) {
    if (initial >= 'a' && initial <= 'z') initial = (char)(initial - 'a' + 'A');
    for (int i = 0; i < (int)titles.size(); i++)
        if (initialOf(titles[i]) == initial) return i;
    return -1;
}

}  // namespace vj::tv
