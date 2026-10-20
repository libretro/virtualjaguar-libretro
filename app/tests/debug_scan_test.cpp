// Unit tests for the cheat scanner: the engine (app/src/debug/cheat_scan.*) against a brute-force
// reference over random RAM images, every mode at every width / signedness / alignment, narrowing, undo, the
// address window, and the path from a result to a cheat the CORE accepts (the code is replayed through the
// core's own cheat_parse_one / cheat_list_apply, so a mistake in the byte order or the code layout fails here).
// Also the actions the window performs (scanner_actions.*) against the mock core interface and a real
// CheatManager: hardcore must keep a result from becoming a cheat.  No window, no GL, no core.
#include "cheat_scan.h"
#include "scanner_actions.h"

#include "cheat.h"
#include "cheats.h"
#include "debug_mock.h"
#include "debug_session.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace vj;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

// ---- reference model ---------------------------------------------------------------------------------------

static uint32_t rng_ = 0x12345678u;
static uint32_t rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

// Independent of CheatScan::decode: shift and add, then sign-extend by arithmetic.
static int64_t refValue(const std::vector<uint8_t>& m, size_t a, unsigned w, bool sg) {
    uint64_t v = 0;
    for (unsigned i = 0; i < w; i++) v = (v << 8) | m[a + i];
    if (sg && (v >> (8 * w - 1)) & 1) return (int64_t)v - ((int64_t)1 << (8 * w));
    return (int64_t)v;
}

static bool refKeep(ScanMode mode, int64_t cur, int64_t prv, int64_t a, int64_t b, unsigned w) {
    const int64_t mod = (int64_t)1 << (8 * w);
    switch (mode) {
    case ScanMode::Unknown: return true;
    case ScanMode::Equal: return cur == a;
    case ScanMode::NotEqual: return cur != a;
    case ScanMode::Greater: return cur > a;
    case ScanMode::Less: return cur < a;
    case ScanMode::Range: return cur >= a && cur <= b;
    case ScanMode::Changed: return cur != prv;
    case ScanMode::Unchanged: return cur == prv;
    case ScanMode::Increased: return cur > prv;
    case ScanMode::Decreased: return cur < prv;
    case ScanMode::IncreasedBy: return (((cur - prv) % mod) + mod) % mod == a;
    case ScanMode::DecreasedBy: return (((prv - cur) % mod) + mod) % mod == a;
    }
    return false;
}

static std::set<uint32_t> asSet(const CheatScan& s) {
    std::vector<uint32_t> v;
    s.results(0, s.count() + 1, v);
    CHECK(v.size() == s.count());
    for (size_t i = 1; i < v.size(); i++) CHECK(v[i - 1] < v[i]);   // ascending, no duplicates
    return std::set<uint32_t>(v.begin(), v.end());
}

static std::vector<uint8_t> randomImage(size_t n) {
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = (uint8_t)(rnd() >> 11);
    return v;
}

// B = A with a share of the bytes changed -- by +1, -1, +7, wrap-around steps, or noise -- so every
// relative mode has candidates on both sides.
static std::vector<uint8_t> mutate(const std::vector<uint8_t>& a) {
    std::vector<uint8_t> b = a;
    for (size_t i = 0; i < b.size(); i++) {
        switch (rnd() % 8) {
        case 0: b[i] = (uint8_t)(b[i] + 1); break;
        case 1: b[i] = (uint8_t)(b[i] - 1); break;
        case 2: b[i] = (uint8_t)(b[i] + 7); break;
        case 3: b[i] = (uint8_t)rnd(); break;
        default: break;
        }
    }
    return b;
}

static void testAllModes() {
    const size_t N = 600;   // not a multiple of 64 or of 4: the tail of the bitset and of the image get exercised
    const unsigned widths[] = {1, 2, 4};
    for (unsigned w : widths)
        for (int sg = 0; sg < 2; sg++)
            for (int al = 0; al < 2; al++) {
                std::vector<uint8_t> A = randomImage(N), B = mutate(A);
                // Pick values that occur, so Equal / Range / By have survivors.
                std::vector<int64_t> pickVals;
                for (int i = 0; i < 6; i++) {
                    size_t a = (rnd() % (N - w));
                    pickVals.push_back(refValue(A, a, w, sg));
                }
                const ScanMode firstModes[] = {ScanMode::Unknown, ScanMode::Equal, ScanMode::NotEqual,
                                               ScanMode::Greater, ScanMode::Less,  ScanMode::Range};
                const ScanMode relModes[] = {ScanMode::Changed,   ScanMode::Unchanged,   ScanMode::Increased,
                                             ScanMode::Decreased, ScanMode::IncreasedBy, ScanMode::DecreasedBy,
                                             ScanMode::Equal,     ScanMode::NotEqual,    ScanMode::Greater,
                                             ScanMode::Less,      ScanMode::Range};
                ScanOptions o;
                o.width = w;
                o.isSigned = sg != 0;
                o.aligned = al != 0;

                // A first pass in every first-pass mode, against the reference.
                for (ScanMode fm : firstModes) {
                    ScanSpec s;
                    s.mode = fm;
                    s.value = pickVals[0];
                    s.value2 = pickVals[1];
                    if (fm == ScanMode::Range && s.value > s.value2) std::swap(s.value, s.value2);
                    CheatScan sc;
                    std::string err;
                    CHECK(sc.begin(A.data(), A.size(), o, s, &err));
                    std::set<uint32_t> want;
                    for (size_t a = 0; a + w <= N; a++) {
                        if (o.aligned && a % w) continue;
                        if (refKeep(fm, refValue(A, a, w, sg), 0, s.value, s.value2, w) || fm == ScanMode::Unknown)
                            want.insert((uint32_t)a);
                    }
                    CHECK(asSet(sc) == want);
                    CHECK(sc.count() == want.size());
                }

                // A second pass in every mode (from Unknown, so the candidate set is the whole image).
                for (ScanMode rm : relModes) {
                    ScanSpec s;
                    s.mode = rm;
                    if (rm == ScanMode::IncreasedBy || rm == ScanMode::DecreasedBy) s.value = (rnd() & 1) ? 1 : 7;
                    else {
                        s.value = pickVals[2];
                        s.value2 = pickVals[3];
                        if (s.value > s.value2) std::swap(s.value, s.value2);
                    }
                    CheatScan sc;
                    std::string err;
                    ScanSpec unk;
                    CHECK(sc.begin(A.data(), A.size(), o, unk, &err));
                    CHECK(sc.next(B.data(), B.size(), s, &err));
                    std::set<uint32_t> want;
                    for (size_t a = 0; a + w <= N; a++) {
                        if (o.aligned && a % w) continue;
                        if (refKeep(rm, refValue(B, a, w, sg), refValue(A, a, w, sg), s.value, s.value2, w))
                            want.insert((uint32_t)a);
                    }
                    CHECK(asSet(sc) == want);
                    if (rm == ScanMode::Changed || rm == ScanMode::Increased) CHECK(!want.empty());
                    // "previous" is the image at the pass before NEXT scan, i.e. B now.
                    for (uint32_t a : want) CHECK(sc.previous(a) == refValue(B, a, w, sg));
                }
            }
}

// ---- hand-checked cases ------------------------------------------------------------------------------------

static void testHandChecked() {
    std::string err;
    // 8-bit, one counter and a decoy.  addr 0: counter 10 -> 11 -> 12; addr 1: constant; addr 2: 200 -> 100 -> 150.
    std::vector<uint8_t> r0 = {10, 77, 200, 0}, r1 = {11, 77, 100, 0}, r2 = {12, 77, 150, 0};
    ScanOptions o;
    CheatScan s;
    ScanSpec unk;
    CHECK(s.begin(r0.data(), r0.size(), o, unk, &err));
    CHECK(s.count() == 4);
    ScanSpec ch;
    ch.mode = ScanMode::Changed;
    CHECK(s.next(r1.data(), r1.size(), ch, &err));
    CHECK(asSet(s) == (std::set<uint32_t>{0, 2}));
    ScanSpec inc;
    inc.mode = ScanMode::Increased;
    CHECK(s.next(r2.data(), r2.size(), inc, &err));
    CHECK(asSet(s) == (std::set<uint32_t>{0, 2}));     // 11 -> 12 up, 100 -> 150 up
    ScanSpec by1;
    by1.mode = ScanMode::IncreasedBy;
    by1.value = 1;
    // Undo gives the pre-narrowing set AND the earlier "previous" back, so the same pass again gives the same answer.
    CHECK(s.canUndo() && s.undo());
    CHECK(asSet(s) == (std::set<uint32_t>{0, 2}));
    CHECK(s.previous(0) == 11 && s.previous(2) == 100);
    CHECK(s.next(r2.data(), r2.size(), by1, &err));
    CHECK(asSet(s) == (std::set<uint32_t>{0}));
    CHECK(s.undo() && s.undo());
    CHECK(s.count() == 4 && s.passes() == 1 && !s.canUndo() && !s.undo());
    CHECK(s.previous(0) == 10 && s.previous(2) == 200);     // the snapshot came back too

    // Modular "by": a byte counter wrapping 255 -> 1 went up by 2; Increased says it went down.
    std::vector<uint8_t> w0 = {255}, w1 = {1};
    CHECK(s.begin(w0.data(), 1, o, unk, &err));
    ScanSpec by2;
    by2.mode = ScanMode::IncreasedBy;
    by2.value = 2;
    CHECK(s.next(w1.data(), 1, by2, &err) && s.count() == 1);
    CHECK(s.undo());
    CHECK(s.next(w1.data(), 1, inc, &err) && s.count() == 0);
    CHECK(s.undo());
    ScanSpec dec;
    dec.mode = ScanMode::Decreased;
    CHECK(s.next(w1.data(), 1, dec, &err) && s.count() == 1);
    // ...and a 16-bit one wraps at 16 bits.
    ScanOptions o16;
    o16.width = 2;
    std::vector<uint8_t> h0 = {0xFF, 0xFE}, h1 = {0x00, 0x01};
    CHECK(s.begin(h0.data(), 2, o16, unk, &err));
    by2.value = 3;
    CHECK(s.next(h1.data(), 2, by2, &err) && s.count() == 1);

    // Signed ordering: 0xF0 is -16 signed (less than 5) and 240 unsigned (greater).
    std::vector<uint8_t> sg = {0xF0, 0x05};
    ScanOptions os;
    os.isSigned = true;
    ScanSpec lt;
    lt.mode = ScanMode::Less;
    lt.value = 5;
    CHECK(s.begin(sg.data(), 2, os, lt, &err));
    CHECK(asSet(s) == (std::set<uint32_t>{0}));
    ScanSpec eqn;
    eqn.mode = ScanMode::Equal;
    eqn.value = -16;
    CHECK(s.begin(sg.data(), 2, os, eqn, &err) && s.count() == 1 && s.isCandidate(0) && !s.isCandidate(1));
    eqn.value = 0xF0;
    CHECK(!s.begin(sg.data(), 2, os, eqn, &err) && !err.empty());         // not a signed byte
    os.isSigned = false;
    lt.value = 5;
    CHECK(s.begin(sg.data(), 2, os, lt, &err) && s.count() == 0);
    eqn.value = 0xF0;
    CHECK(s.begin(sg.data(), 2, os, eqn, &err) && s.count() == 1);
    eqn.value = -1;
    CHECK(!s.begin(sg.data(), 2, os, eqn, &err));

    // Range, inclusive; and rejected when reversed.
    std::vector<uint8_t> rg = {1, 5, 9, 10, 11};
    ScanSpec range;
    range.mode = ScanMode::Range;
    range.value = 5;
    range.value2 = 10;
    CHECK(s.begin(rg.data(), rg.size(), ScanOptions(), range, &err));
    CHECK(asSet(s) == (std::set<uint32_t>{1, 2, 3}));
    range.value = 10;
    range.value2 = 5;
    CHECK(!s.begin(rg.data(), rg.size(), ScanOptions(), range, &err) && !err.empty());

    // The first pass needs no previous value; later passes cannot start over with Unknown.
    ScanSpec chg;
    chg.mode = ScanMode::Changed;
    CHECK(!s.begin(rg.data(), rg.size(), ScanOptions(), chg, &err) && !err.empty());
    CHECK(s.begin(rg.data(), rg.size(), ScanOptions(), unk, &err));
    CHECK(!s.next(rg.data(), rg.size(), unk, &err));
    std::vector<uint8_t> shorter(4);
    CHECK(!s.next(shorter.data(), shorter.size(), chg, &err));             // image size changed
    CHECK(!s.next(nullptr, 0, chg, &err));
    CheatScan none;
    CHECK(!none.next(rg.data(), rg.size(), chg, &err) && none.count() == 0 && !none.active());
    // A "by" larger than the width is out of range.
    ScanSpec big;
    big.mode = ScanMode::IncreasedBy;
    big.value = 256;
    CHECK(s.begin(rg.data(), rg.size(), ScanOptions(), unk, &err));
    CHECK(!s.next(rg.data(), rg.size(), big, &err));
    // Bad width, empty / null image, empty window.
    ScanOptions bad;
    bad.width = 3;
    CHECK(!s.begin(rg.data(), rg.size(), bad, unk, &err));
    CHECK(!s.begin(nullptr, 0, ScanOptions(), unk, &err));
    ScanOptions win;
    win.first = 10;
    win.last = 20;
    CHECK(!s.begin(rg.data(), rg.size(), win, unk, &err));
    CHECK(s.begin(rg.data(), rg.size(), ScanOptions(), unk, &err));
    s.reset();
    CHECK(!s.active() && s.count() == 0);
}

static void testAlignmentWindowAndBounds() {
    std::string err;
    std::vector<uint8_t> m(32, 0);
    ScanSpec unk;
    CheatScan s;
    for (unsigned w : {1u, 2u, 4u}) {
        ScanOptions o;
        o.width = w;
        o.aligned = true;
        CHECK(s.begin(m.data(), m.size(), o, unk, &err));
        CHECK(s.count() == m.size() / w);
        o.aligned = false;
        CHECK(s.begin(m.data(), m.size(), o, unk, &err));
        CHECK(s.count() == m.size() - w + 1);                  // the last whole value fits at size - w
        CHECK(s.isCandidate((uint32_t)(m.size() - w)) && !s.isCandidate((uint32_t)(m.size() - w + 1)));
        CHECK(!s.isCandidate(1000));
    }
    // An address window: [8, 20), 4-byte aligned -> 8, 12, 16.
    ScanOptions o;
    o.width = 4;
    o.first = 8;
    o.last = 20;
    CHECK(s.begin(m.data(), m.size(), o, unk, &err));
    std::vector<uint32_t> v;
    s.results(0, 100, v);
    CHECK((v == std::vector<uint32_t>{8, 12, 16}));
    o.aligned = false;
    CHECK(s.begin(m.data(), m.size(), o, unk, &err));
    CHECK(s.count() == 9);                                     // 8..16
    // A window past the image is clamped to it.
    o.first = 28;
    o.last = 1000;
    CHECK(s.begin(m.data(), m.size(), o, unk, &err));
    CHECK(s.count() == 1 && s.isCandidate(28));

    // Paging: skip and max, over a bitset that crosses word boundaries.
    std::vector<uint8_t> big(300, 0);
    ScanOptions ob;
    CHECK(s.begin(big.data(), big.size(), ob, unk, &err));
    CHECK(s.count() == 300);
    s.results(0, 5, v);
    CHECK((v == std::vector<uint32_t>{0, 1, 2, 3, 4}));
    s.results(62, 4, v);
    CHECK((v == std::vector<uint32_t>{62, 63, 64, 65}));
    s.results(298, 100, v);
    CHECK((v == std::vector<uint32_t>{298, 299}));
    s.results(300, 10, v);
    CHECK(v.empty());
    s.results(0, 0, v);
    CHECK(v.empty());
}

static void testUndoDepth() {
    std::string err;
    std::vector<uint8_t> a(64, 0);
    CheatScan s;
    ScanOptions o;
    ScanSpec unk, un;
    un.mode = ScanMode::Unchanged;
    CHECK(s.begin(a.data(), a.size(), o, unk, &err));
    for (size_t i = 0; i < CheatScan::kMaxUndo + 5; i++) CHECK(s.next(a.data(), a.size(), un, &err));
    CHECK(s.passes() == CheatScan::kMaxUndo + 1);               // the oldest passes fell off the end
    size_t undone = 0;
    while (s.undo()) undone++;
    CHECK(undone == CheatScan::kMaxUndo);
    CHECK(s.active() && s.count() == a.size());
}

static void testValueHelpers() {
    uint8_t b[4];
    CheatScan::encode(0x12345678, 4, b);
    CHECK(b[0] == 0x12 && b[1] == 0x34 && b[2] == 0x56 && b[3] == 0x78);
    CheatScan::encode(-2, 2, b);
    CHECK(b[0] == 0xFF && b[1] == 0xFE);
    CheatScan::encode(0x1FF, 1, b);
    CHECK(b[0] == 0xFF);
    CHECK(CheatScan::decode(b, 1, true) == -1 && CheatScan::decode(b, 1, false) == 255);
    const uint8_t l[4] = {0x80, 0, 0, 1};
    CHECK(CheatScan::decode(l, 4, false) == 0x80000001LL);
    CHECK(CheatScan::decode(l, 4, true) == -2147483647LL);
    CHECK(CheatScan::minValue(1, true) == -128 && CheatScan::maxValue(1, true) == 127);
    CHECK(CheatScan::minValue(2, true) == -32768 && CheatScan::maxValue(2, false) == 65535);
    CHECK(CheatScan::minValue(4, true) == -2147483648LL && CheatScan::maxValue(4, false) == 4294967295LL);
    int64_t v = 0;
    CHECK(CheatScan::parseNumber("12", v) && v == 12);
    CHECK(CheatScan::parseNumber("  -12 ", v) && v == -12);
    CHECK(CheatScan::parseNumber("0x1F", v) && v == 31);
    CHECK(CheatScan::parseNumber("$ff", v) && v == 255);
    CHECK(CheatScan::parseNumber("-0x10", v) && v == -16);
    CHECK(CheatScan::parseNumber("4294967295", v) && v == 4294967295LL);
    CHECK(!CheatScan::parseNumber("", v) && !CheatScan::parseNumber("abc", v) && !CheatScan::parseNumber("0x", v));
    CHECK(!CheatScan::parseNumber("12x", v) && !CheatScan::parseNumber("-", v) && !CheatScan::parseNumber("99999999999999999999", v));
    CHECK(CheatScan::formatValue(255, 1, true) == "FF" && CheatScan::formatValue(-1, 2, true) == "FFFF");
    CHECK(CheatScan::formatValue(-1, 4, true) == "FFFFFFFF" && CheatScan::formatValue(-7, 1, false) == "-7");
    for (int m = 0; m <= (int)ScanMode::DecreasedBy; m++) CHECK(std::string(CheatScan::modeName((ScanMode)m)) != "?");
}

// ---- the cheat code path ------------------------------------------------------------------------------------

struct CodeRam {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x200000, 0);
};
static void codeWrite(uint32_t a, uint32_t v, uint8_t size, void* user) {
    CodeRam* r = (CodeRam*)user;
    for (unsigned i = 0; i < size; i++) r->b[(a + i) & 0x1FFFFF] = (uint8_t)(v >> (8 * (size - 1 - i)));   // big-endian
}

static void testCodeRoundTrip() {
    const struct {
        unsigned w;
        int64_t v;
    } vals[] = {{1, 0}, {1, 5}, {1, 0xFF}, {1, -1}, {2, 0x1234}, {2, 0xFFFF}, {2, -2}, {4, 0x12345678}, {4, 0xDEADBEEFLL}, {4, -3}};
    const uint32_t addrs[] = {0x0, 0x1, 0x7D3D, 0x7D3E, 0x1F0000, 0x1FFFF0, 0x1FFFFB, 0x1FFFFC, 0x1FFFFF};
    for (const auto& x : vals)
        for (uint32_t addr : addrs) {
            if ((uint64_t)addr + x.w > 0x200000) continue;
            std::string code = scanCheatCode(addr, x.v, x.w);
            // 1. The expected memory image.
            uint8_t want[4];
            CheatScan::encode(x.v, x.w, want);
            // 2. Every segment through the core's parser.
            CodeRam viaParse, viaList;
            size_t start = 0;
            unsigned segs = 0, covered = 0;
            for (size_t p = 0; p <= code.size(); p++) {
                if (p != code.size() && code[p] != '+') continue;
                std::string seg = code.substr(start, p - start);
                start = p + 1;
                uint32_t a, v;
                uint8_t sz;
                CHECK(cheat_parse_one(seg.c_str(), &a, &v, &sz));
                CHECK(sz == 1 || sz == 2 || sz == 4);
                if (sz > 1) CHECK((a & 1) == 0);                 // the 68000 cannot do words / longs at odd addresses
                codeWrite(a, v, sz, &viaParse);
                covered += sz;
                segs++;
            }
            CHECK(segs >= 1 && covered == x.w);
            // 3. The core's own list, applied with a big-endian writer.
            cheat_list_t list;
            cheat_list_reset(&list);
            cheat_list_set(&list, 0, true, code.c_str());
            cheat_list_apply(&list, codeWrite, &viaList);
            for (unsigned i = 0; i < x.w; i++) {
                CHECK(viaParse.b[addr + i] == want[i]);
                CHECK(viaList.b[addr + i] == want[i]);
            }
            // Nothing outside the value was written.
            unsigned dirty = 0;
            for (size_t i = 0; i < viaList.b.size(); i++)
                if (viaList.b[i]) dirty++;
            unsigned nz = 0;
            for (unsigned i = 0; i < x.w; i++)
                if (want[i]) nz++;
            CHECK(dirty == nz);
            // The manager's own validator agrees.
            CHECK(validateCode(code, nullptr, nullptr));
        }
    // Spot values: the shape of the code itself.
    CHECK(scanCheatCode(0x7D3D, 5, 1) == "00007D3D:05");
    CHECK(scanCheatCode(0x1F0000, 0x1234, 2) == "001F0000:1234");
    CHECK(scanCheatCode(0x1F0001, 0x1234, 2) == "001F0001:12+001F0002:34");     // odd address: byte by byte
    CHECK(scanCheatCode(0x1000, 0xDEADBEEFLL, 4) == "00001000:DEADBEEF");
}

// ---- what the window does with a result ------------------------------------------------------------------------

static void testActions() {
    fs::path tmp = fs::temp_directory_path() / "vj-scan-actions";
    fs::remove_all(tmp);
    std::string err;
    {
        MockDebugIface mock;
        DebugSession session;
        session.setIface(mock.iface());
        session.setContentLoaded(true);

        CheatManager cm;
        std::vector<std::string> sent;
        cm.commit = [&](CheatBatch b) {
            for (const CheatOp& o : b) {
                if (o.reset) sent.push_back("reset");
                else sent.push_back(std::string(o.enabled ? "on " : "off ") + o.code);
            }
        };
        bool hardcore = false;
        cm.blocked = [&] { return hardcore; };
        cm.setStateDir(tmp.string() + "/");

        // No game loaded in the cheat manager yet.
        CHECK(!scanAddCheat(cm, session, 0x1000, 5, 1, "x", err) && !err.empty());
        GameIdentity g;
        g.key = "crc-00000042";
        g.title = "Scan Test";
        cm.gameLoaded(g);

        // The normal path: it lands in the Cheats window's list, enabled, with the exact code.
        CHECK(scanAddCheat(cm, session, 0x7D3D, 5, 1, "Lives", err));
        CHECK(cm.userCheats().size() == 1);
        CHECK(cm.userCheats()[0].desc == "Lives" && cm.userCheats()[0].code == "00007D3D:05" && cm.userCheats()[0].enabled);
        CHECK(!sent.empty() && sent.back() == "on 00007D3D:05");
        CHECK(scanAddCheat(cm, session, 0x2000, 0x1234, 2, "", err));
        CHECK(cm.userCheats().size() == 2 && cm.userCheats()[1].desc == "Scanner 00002000");
        CHECK(scanAddCheat(cm, session, 0x7D3D, 5, 1, "again", err));          // same code: not duplicated
        CHECK(cm.userCheats().size() == 2);

        // Range: the last byte of RAM is fine, one past it is not (the core's cheats only reach main RAM).
        CHECK(scanAddCheat(cm, session, 0x1FFFFF, 1, 1, "edge", err));
        CHECK(!scanAddCheat(cm, session, 0x200000, 1, 1, "past", err));
        CHECK(!scanAddCheat(cm, session, 0x1FFFFE, 1, 4, "straddle", err));
        CHECK(!scanAddCheat(cm, session, 0x1000, 1, 3, "width", err));
        CHECK(cm.userCheats().size() == 3);

        // Hardcore (RetroAchievements): neither the cheat manager nor the tools allow it, and nothing is stored.
        hardcore = true;
        size_t before = cm.userCheats().size();
        CHECK(!scanAddCheat(cm, session, 0x3000, 9, 1, "cheat", err));
        CHECK(err.find("hardcore") != std::string::npos);
        CHECK(cm.userCheats().size() == before);
        hardcore = false;
        session.setHardcore(true);                                              // the session's own switch
        CHECK(!scanAddCheat(cm, session, 0x3000, 9, 1, "cheat", err));
        CHECK(err.find("hardcore") != std::string::npos || err.find("Hardcore") != std::string::npos);
        CHECK(cm.userCheats().size() == before);
        std::string perr;
        CHECK(!scanPoke(session, 0x3000, 9, 1, perr) && !perr.empty());         // and no pokes either
        session.setHardcore(false);

        // Without a core interface the tools are off, and so is adding a cheat.
        DebugSession bare;
        CHECK(!scanAddCheat(cm, bare, 0x3000, 9, 1, "cheat", err) && cm.userCheats().size() == before);

        // Pokes: big-endian into the image behind the interface, per width.
        mock.dram()[0x5000] = mock.dram()[0x5001] = mock.dram()[0x5002] = mock.dram()[0x5003] = 0;
        CHECK(scanPoke(session, 0x5000, 0x11, 1, perr));
        CHECK(mock.dram()[0x5000] == 0x11 && mock.dram()[0x5001] == 0);
        CHECK(scanPoke(session, 0x5000, 0x1234, 2, perr));
        CHECK(mock.dram()[0x5000] == 0x12 && mock.dram()[0x5001] == 0x34);
        CHECK(scanPoke(session, 0x5000, 0xCAFEBABELL, 4, perr));
        CHECK(mock.dram()[0x5000] == 0xCA && mock.dram()[0x5001] == 0xFE && mock.dram()[0x5002] == 0xBA &&
              mock.dram()[0x5003] == 0xBE);
        CHECK(scanPoke(session, 0x5001, -2, 2, perr));                          // unaligned, signed
        CHECK(mock.dram()[0x5001] == 0xFF && mock.dram()[0x5002] == 0xFE);
        CHECK(!scanPoke(session, 0x1FFFFF, 1, 2, perr));                        // runs off the end of RAM into nothing mapped
        CHECK(!scanPoke(session, 0x800000, 1, 1, perr) && !perr.empty());       // cartridge ROM refuses
    }
    fs::remove_all(tmp);
}

// A scan over a mock "game": a frame counter at 0x1000 (+1 per frame, 16-bit), a timer at 0x1004 that jumps by
// 3, constant bytes everywhere else; find the counter by Changed / Increased / Increased-by narrowing and keep it.
static void testScanFlowOnMock() {
    MockDebugIface mock;
    DebugSession session;
    session.setIface(mock.iface());
    session.setContentLoaded(true);
    std::vector<uint8_t> ram(DebugSession::kDramSize, 0x5A);
    auto put16 = [&](uint32_t a, unsigned v) {
        ram[a] = (uint8_t)(v >> 8);
        ram[a + 1] = (uint8_t)v;
    };
    CheatScan sc;
    std::string err;
    ScanOptions o;
    o.width = 2;
    ScanSpec unk;
    put16(0x1000, 100);
    put16(0x1004, 300);
    CHECK(sc.begin(ram.data(), ram.size(), o, unk, &err));
    CHECK(sc.count() == DebugSession::kDramSize / 2);
    for (int frame = 1; frame <= 3; frame++) {
        put16(0x1000, 100 + (unsigned)frame);
        put16(0x1004, 300 + 3 * (unsigned)frame);
        ScanSpec s;
        s.mode = frame == 1 ? ScanMode::Changed : frame == 2 ? ScanMode::Increased : ScanMode::IncreasedBy;
        s.value = 1;
        CHECK(sc.next(ram.data(), ram.size(), s, &err));
    }
    CHECK(sc.count() == 1 && sc.isCandidate(0x1000));
    std::vector<uint32_t> r;
    sc.results(0, 10, r);
    CHECK(r.size() == 1 && r[0] == 0x1000 && sc.valueIn(ram.data(), ram.size(), 0x1000) == 103);
    CHECK(sc.previous(0x1000) == 103);
    // Poke it through the session and read it back through the same interface.
    std::string perr;
    CHECK(scanPoke(session, 0x1000, 999, 2, perr));
    unsigned back = 0;
    CHECK(session.readU(0x1000, 2, back) && back == 999);
}

int main() {
    testValueHelpers();
    testAllModes();
    testHandChecked();
    testAlignmentWindowAndBounds();
    testUndoDepth();
    testCodeRoundTrip();
    testActions();
    testScanFlowOnMock();
    std::printf("debug scanner: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
