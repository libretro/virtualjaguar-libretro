// The audio scope window.  See scope_ui.h.
#include "scope_ui.h"

#include "audio_ring.h"
#include "core_host.h"
#include "debug_session.h"
#include "ui_util.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace vj {

namespace {

struct State {
    DebugSession* session = nullptr;
    CoreHost* host = nullptr;
    bool open = false;
    bool tapped = false;
    AudioRing ring{16384};
    uint64_t lastTotal = 0;
    double lastMoveTime = 0;      // when new audio last arrived (ImGui time)
    int windowIdx = 2;
    PeakMeter meterL, meterR;
    std::vector<int16_t> l, r;
};
State g;

const struct {
    const char* label;
    size_t frames;
} kWindows[] = {{"256 samples", 256}, {"512", 512}, {"1024", 1024}, {"2048", 2048}, {"4096", 4096}, {"8192", 8192}};

// One channel's waveform as a min/max envelope per pixel column.
void drawWave(const char* id, const std::vector<int16_t>& s, ImVec2 size, ImU32 col) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    const float mid = p.y + size.y * 0.5f;
    dl->AddLine(ImVec2(p.x, mid), ImVec2(p.x + size.x, mid), IM_COL32(128, 128, 128, 90));
    if (s.empty()) return;
    const int cols = std::max(1, (int)size.x);
    const float half = size.y * 0.5f - 1.0f;
    for (int x = 0; x < cols; x++) {
        size_t a = (size_t)x * s.size() / cols, b = std::max(a + 1, (size_t)(x + 1) * s.size() / cols);
        b = std::min(b, s.size());
        int lo = 32767, hi = -32768;
        for (size_t i = a; i < b; i++) {
            lo = std::min<int>(lo, s[i]);
            hi = std::max<int>(hi, s[i]);
        }
        float y0 = mid - hi / 32768.0f * half, y1 = mid - lo / 32768.0f * half;
        if (y1 - y0 < 1.0f) y1 = y0 + 1.0f;
        dl->AddLine(ImVec2(p.x + x + 0.5f, y0), ImVec2(p.x + x + 0.5f, y1), col);
    }
}

void drawMeter(const char* name, const PeakMeter& m, int peak) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::SameLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ui::em(6.0f), ImGui::GetContentRegionAvail().x - ui::em(9.0f));
    const float h = ImGui::GetFrameHeight();
    ImGui::InvisibleButton(name, ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImU32 col = m.level > 0.98f ? IM_COL32(220, 60, 60, 255) : m.level > 0.7f ? IM_COL32(220, 190, 60, 255) : IM_COL32(60, 190, 90, 255);
    dl->AddRectFilled(p, ImVec2(p.x + w * m.level, p.y + h), col);
    const float hx = p.x + w * m.hold;
    dl->AddLine(ImVec2(hx, p.y), ImVec2(hx, p.y + h), IM_COL32(255, 255, 255, 220), 2.0f);
    ImGui::SameLine();
    const float db = peakDb(peak);
    if (peak <= 0) ImGui::TextDisabled("-inf dB");
    else ImGui::Text("%.1f dB", db);
}

}  // namespace

void scopeToolInit(DebugSession& session, CoreHost& host) {
    g.session = &session;
    g.host = &host;
    if (!g.tapped) {
        g.tapped = true;
        // Chain onto whatever is wired now: the scope sees every block, then the real consumer does.
        std::function<void(const int16_t*, size_t)> prev = host.onAudio;
        host.onAudio = [prev](const int16_t* d, size_t n) {
            if (g.open) g.ring.push(d, n);
            if (prev) prev(d, n);
        };
    }
    DebugTool t;
    t.id = "scope";
    t.label = "Audio scope...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] {
        g.open = !g.open;
        g.ring.clear();
        g.lastTotal = 0;
        g.meterL.reset();
        g.meterR.reset();
    };
    session.registerTool(t);
    session.onForcedClose.push_back([](const char*) { g.open = false; });
}

void scopeToolDraw() {
    if (!g.open || !g.session) return;
    ui::nextWindow(46.0f, 40.0f, 26.0f, 24.0f);
    bool shown = ImGui::Begin("Audio scope", &g.open);
    ui::keepOnScreen();
    if (shown) {
        const double now = ImGui::GetTime();
        const float dt = std::min(0.25f, ImGui::GetIO().DeltaTime);
        ui::hint("What the core sends to the audio device, taken before the app's volume and mute. Nothing is "
                 "recorded and the core is not involved.");
        if (g.ring.totalFrames() != g.lastTotal) {
            g.lastTotal = g.ring.totalFrames();
            g.lastMoveTime = now;
        }
        const bool flowing = now - g.lastMoveTime < 0.25 && g.ring.totalFrames() > 0;

        ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 8.0f, 14.0f));
        if (ImGui::BeginCombo("Window", kWindows[g.windowIdx].label)) {
            for (int i = 0; i < (int)(sizeof kWindows / sizeof kWindows[0]); i++)
                if (ImGui::Selectable(kWindows[i].label, i == g.windowIdx)) g.windowIdx = i;
            ImGui::EndCombo();
        }
        const double rate = g.host ? g.host->sampleRate() : 0.0;
        if (rate > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("= %.1f ms at %.0f Hz", kWindows[g.windowIdx].frames * 1000.0 / rate, rate);
        }

        const size_t n = kWindows[g.windowIdx].frames;
        g.ring.latest(n, g.l, g.r);
        AudioRing::Peak pk = g.ring.peak(std::min<size_t>(n, 2048));
        g.meterL.update(pk.left, dt);
        g.meterR.update(pk.right, dt);

        if (!flowing) ui::hint("No audio is arriving (paused, muted by the game, or nothing loaded). The last block is shown.");

        const float avail = ImGui::GetContentRegionAvail().y;
        const float h = std::max(ui::em(4.0f), (avail - ui::em(11.0f)) * 0.5f);
        ImGui::TextUnformatted("Left");
        drawWave("##L", g.l, ImVec2(ImGui::GetContentRegionAvail().x, h), IM_COL32(110, 200, 255, 255));
        ImGui::TextUnformatted("Right");
        drawWave("##R", g.r, ImVec2(ImGui::GetContentRegionAvail().x, h), IM_COL32(255, 170, 110, 255));
        drawMeter("L", g.meterL, pk.left);
        drawMeter("R", g.meterR, pk.right);
        if (g.meterL.clipped || g.meterR.clipped) {
            ui::coloredWrapped(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Full-scale samples were seen (clipping).");
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Reset peaks")) {
            g.meterL.reset();
            g.meterR.reset();
        }
    }
    ImGui::End();
}

}  // namespace vj
