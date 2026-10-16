#include "tv/tv_widgets.h"

#include <algorithm>
#include <cfloat>

#include "theme.h"

namespace vj::tv {
namespace widgets {

namespace {

std::vector<std::string>* g_probe = nullptr;

// The UTF-8 string cut to the longest prefix (plus an ellipsis) that fits in maxW.
std::string fit(float px, const std::string& s, float maxW) {
    if (maxW <= 0 || textWidth(px, s) <= maxW) return s;
    const std::string dots = "...";
    size_t n = s.size();
    while (n > 0) {
        n--;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;   // back to a character boundary
        if (textWidth(px, s.substr(0, n) + dots) <= maxW) return s.substr(0, n) + dots;
    }
    return dots;
}

ImVec2 scaledAbout(ImVec2 pos, ImVec2 size, float k, ImVec2* outSize) {
    const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    *outSize = ImVec2(size.x * k, size.y * k);
    return ImVec2(c.x - outSize->x * 0.5f, c.y - outSize->y * 0.5f);
}

}  // namespace

void setTextProbe(std::vector<std::string>* sink) { g_probe = sink; }

void FocusTweens::update(int count, int focusIndex, int pageKey, double now) {
    if (pageKey != key || (int)t.size() != count) {
        key = pageKey;
        t.assign((size_t)std::max(count, 0), Tween());
        focus = -1;
    }
    if (focusIndex == focus) return;
    if (focus >= 0 && focus < (int)t.size()) t[(size_t)focus].retarget(0, now);
    if (focusIndex >= 0 && focusIndex < (int)t.size()) {
        Tween& n = t[(size_t)focusIndex];
        if (focus < 0) {   // first focus of a page: already there
            n.from = n.to = 1;
            n.start = now;
        } else {
            n.retarget(1, now);
        }
    }
    focus = focusIndex;
}

float FocusTweens::at(int i, double now) const {
    return i >= 0 && i < (int)t.size() ? std::min(1.0f, std::max(0.0f, t[(size_t)i].at(now))) : 0.0f;
}

void scrim(ImDrawList* dl, ImVec2 pos, ImVec2 size, float alpha) {
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, (int)(255 * alpha)));
}

void panel(ImDrawList* dl, const Metrics& m, ImVec2 pos, ImVec2 size) {
    const theme::Palette& p = theme::pal();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col(p.homeBg, 0.96f), 20 * m.u);
}

float textWidth(float px, const std::string& s) {
    return ImGui::GetFont()->CalcTextSizeA(px, FLT_MAX, 0.0f, s.c_str(), s.c_str() + s.size()).x;
}

ImVec2 text(ImDrawList* dl, float px, ImVec2 pos, ImU32 c, const std::string& s, float maxW, float wrapW) {
    ImFont* f = ImGui::GetFont();
    const std::string shown = wrapW > 0 ? s : fit(px, s, maxW);
    if (g_probe) g_probe->push_back(shown);
    dl->AddText(f, px, pos, c, shown.c_str(), shown.c_str() + shown.size(), wrapW);
    return f->CalcTextSizeA(px, FLT_MAX, wrapW, shown.c_str(), shown.c_str() + shown.size());
}

Hit row(ImDrawList* dl, const Metrics& m, ImVec2 pos, ImVec2 size, const RowArgs& a) {
    const theme::Palette& p = theme::pal();
    Hit hit;
    // The pointer tests the unscaled rectangle: a focused row growing under the pointer must not flicker.
    const ImGuiIO& io = ImGui::GetIO();
    hit.hovered = ImGui::IsWindowHovered() && io.MousePos.x >= pos.x && io.MousePos.x < pos.x + size.x &&
                  io.MousePos.y >= pos.y && io.MousePos.y < pos.y + size.y;

    const float f = a.focus;
    ImVec2 sz;
    const ImVec2 p0 = scaledAbout(pos, size, 1.0f + (m.focusScale - 1.0f) * f, &sz);
    const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
    const float round = 14 * m.u;
    if (f > 0.01f) {   // the drop shadow: a few soft layers, then the ring
        for (int i = 3; i >= 1; i--) {
            const float e = i * 5 * m.u * f;
            dl->AddRectFilled(ImVec2(p0.x - e, p0.y - e + 8 * m.u * f), ImVec2(p1.x + e, p1.y + e + 8 * m.u * f),
                              IM_COL32(0, 0, 0, (int)(40 * f)), round + e);
        }
    }
    dl->AddRectFilled(p0, p1, col(f > 0.5f ? p.cardBgHover : p.cardBg), round);
    if (f > 0.01f) dl->AddRect(p0, p1, col(p.text, f), round, 0, m.ring * f);

    const ImU32 tc = col(a.enabled ? p.text : p.textMuted);
    const float pad = 24 * m.u * (0.6f + 0.4f * (sz.y / size.y));
    float x = p0.x + pad;
    if (a.thumbBox || a.thumb) {
        const float th = sz.y - 2 * 16 * m.u, tw = th * 16.0f / 9.0f;
        const ImVec2 b0(x, p0.y + (sz.y - th) * 0.5f), b1(b0.x + tw, b0.y + th);
        dl->AddRectFilled(b0, b1, col(p.stateBg), 8 * m.u);
        if (a.thumb && a.thumbSrcW > 0 && a.thumbSrcH > 0) {   // the picture, fitted inside the box
            const float k = std::min(tw / a.thumbSrcW, th / a.thumbSrcH);
            const float iw = a.thumbSrcW * k, ih = a.thumbSrcH * k;
            const ImVec2 i0(b0.x + (tw - iw) * 0.5f, b0.y + (th - ih) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)a.thumb, i0, ImVec2(i0.x + iw, i0.y + ih));
        } else {
            dl->AddRect(b0, b1, col(p.emptyEdge), 8 * m.u, 0, 2 * m.u);
        }
        x = b1.x + pad;
    }
    const float maxW = p1.x - pad - x;
    const float bodyPx = m.body * (1.0f + 0.08f * f);   // the spec's body size at rest, the focus scale when focused
    if (a.sub.empty()) {
        text(dl, bodyPx, ImVec2(x, p0.y + (sz.y - bodyPx) * 0.5f), tc, a.label, maxW);
    } else {
        const float total = bodyPx + m.minText + 4 * m.u;
        const float y = p0.y + (sz.y - total) * 0.5f;
        text(dl, bodyPx, ImVec2(x, y), tc, a.label, maxW);
        text(dl, m.minText, ImVec2(x, y + bodyPx + 4 * m.u), col(p.textMuted), a.sub, maxW);
    }
    return hit;
}

void hintBar(ImDrawList* dl, const Metrics& m, ImVec2 pos, float maxW, const std::vector<Hint>& hints) {
    const theme::Palette& p = theme::pal();
    const float px = m.caption, gap = 40 * m.u, chipPad = 12 * m.u;
    // One hint's width: its chip (if any), the gap after the chip, and its label.
    auto width = [&](const Hint& h) {
        float w = textWidth(px, h.label);
        if (!h.glyph.empty()) w += std::max(textWidth(px, h.glyph) + 2 * chipPad, px * 1.6f) + 8 * m.u;
        return w;
    };
    // The last hint is the way out (Back / Resume): its room is kept, so a narrow bar drops the others first.
    const float reserved = hints.empty() ? 0.0f : width(hints.back());
    float x = pos.x;
    for (size_t i = 0; i < hints.size(); i++) {
        const Hint& h = hints[i];
        const bool last = i + 1 == hints.size();
        if (x + width(h) + (last ? 0.0f : gap + reserved) > pos.x + maxW) continue;   // does not fit (with Back's room kept)
        if (!h.glyph.empty()) {
            const float gw = textWidth(px, h.glyph) + 2 * chipPad;
            const float chipW = std::max(gw, px * 1.6f);
            dl->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + chipW, pos.y + px + 8 * m.u), col(p.text), 10 * m.u);
            text(dl, px, ImVec2(x + (chipW - textWidth(px, h.glyph)) * 0.5f, pos.y + 4 * m.u), col(p.homeBg), h.glyph);
            x += chipW + 8 * m.u;
        }
        text(dl, px, ImVec2(x, pos.y + 4 * m.u), col(p.text), h.label);
        x += textWidth(px, h.label) + gap;
    }
}

float banner(ImDrawList* dl, const Metrics& m, ImVec2 pos, float w, const std::string& s) {
    const theme::Palette& p = theme::pal();
    const float h = m.body + 2 * 14 * m.u;
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), col(p.cardBg), 14 * m.u);
    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), col(p.warning), 14 * m.u, 0, 3 * m.u);
    text(dl, m.body, ImVec2(pos.x + 24 * m.u, pos.y + 14 * m.u), col(p.text), s, w - 48 * m.u);
    return h;
}

void moreArrow(ImDrawList* dl, const Metrics& m, ImVec2 c, bool up) {
    const float r = 14 * m.u;
    const ImU32 cc = col(theme::pal().textMuted);
    if (up) dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.7f), ImVec2(c.x - r, c.y + r * 0.7f), ImVec2(c.x + r, c.y + r * 0.7f), cc);
    else dl->AddTriangleFilled(ImVec2(c.x, c.y + r * 0.7f), ImVec2(c.x + r, c.y - r * 0.7f), ImVec2(c.x - r, c.y - r * 0.7f), cc);
}

}  // namespace widgets
}  // namespace vj::tv
