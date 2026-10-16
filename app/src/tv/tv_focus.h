// TV mode (#991): the focus model.  Pure index arithmetic, no ImGui and no SDL: a shelf or list
// (LinearFocus), a cover grid (GridFocus), what each screen remembers (FocusMemory) and the A-Z helpers
// behind the shoulder/trigger jump and the letter picker.
//
// A move that runs off an end reports it and changes nothing; whoever owns the screen decides what leaving
// means (up to the section bar, over to the neighbouring shelf).  Focus never wraps.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace vj::tv {

// A shelf (horizontal) or a list (vertical).
struct LinearFocus {
    int count = 0, index = 0, first = 0;
    bool move(int delta);                // false = at an end, nothing moved (focus may leave)
    void clampTo(int newCount);          // the list changed: keep index and first inside it
    void scrollToShow(int visible);      // first..first+visible-1 contains index
};

struct GridFocus {
    int count = 0, cols = 6, index = 0, firstRow = 0;
    enum Edge { None, Left, Right, Top, Bottom };
    // None = moved; otherwise the edge that was hit (nothing moved).  Down onto a short last row lands on
    // the last item.
    Edge move(int dx, int dy);
    int row() const;
    int col() const;
    int rows() const;
    void scrollToShow(int visibleRows);  // firstRow..firstRow+visibleRows-1 contains row()
};

// Where focus was on each screen/shelf, so coming back lands there.  Keys are the caller's ("home/favorites").
class FocusMemory {
public:
    int get(const std::string& key, int def = 0) const;
    void put(const std::string& key, int index);

private:
    std::unordered_map<std::string, int> m_;
};

char initialOf(const std::string& title);                                     // 'A'..'Z', '0'..'9', else '#'
// LT/RT: the first item of the previous (dir < 0) / next (dir > 0) initial in an already sorted list;
// `from` when there is none.
int jumpInitial(const std::vector<std::string>& titles, int from, int dir);
int firstWithInitial(const std::vector<std::string>& titles, char initial);   // letter picker; -1 = none

}  // namespace vj::tv
