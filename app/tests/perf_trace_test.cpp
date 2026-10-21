// VJ_PERF_TRACE (perf_trace.h): the -emu.csv header and rows.  fill_ns (the Tools windows' view copy, timed on its own
// because end_ns is taken before it) is the LAST column, appended so existing readers keep working.
//
//   vj_perf_trace_test <scratch-dir>
#include "perf_trace.h"

#include <stdlib.h>   // setenv / _putenv_s

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(',', a);
        out.push_back(s.substr(a, b == std::string::npos ? b : b - a));
        if (b == std::string::npos) return out;
        a = b + 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_perf_trace_test <scratch-dir>\n");
        return 2;
    }
    const std::string prefix = std::string(argv[1]) + "/vj_perf_trace_test";
#ifdef _WIN32
    _putenv_s("VJ_PERF_TRACE", prefix.c_str());
#else
    setenv("VJ_PERF_TRACE", prefix.c_str(), 1);
#endif
    vj::PerfTrace t;
    t.init(3);
    CHECK(t.enabled(), "enabled once VJ_PERF_TRACE is set");
    for (unsigned i = 0; i < 3; i++) {
        vj::PerfTrace::Emu e{};
        e.dueNs = 100 + i;
        e.startNs = 200 + i;
        e.endNs = 300 + i;
        e.serial = 7 + i;
        e.fillNs = i == 1 ? 4500 : 0;
        t.emu(e);
    }
    t.write();

    std::ifstream f(prefix + "-emu.csv");
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) lines.push_back(l);
    CHECK(lines.size() == 4, "a header and three rows (%zu lines)", lines.size());
    if (lines.size() == 4) {
        const std::vector<std::string> hdr = split(lines[0]);
        CHECK(hdr.size() == 10 && hdr.back() == "fill_ns", "fill_ns is the last of ten header columns: %s",
              lines[0].c_str());
        CHECK(lines[0].rfind("due_ns,start_ns,end_ns,serial,trim,filt_q_ms,queued_ms,audio,refill,", 0) == 0,
              "the columns before fill_ns are unchanged: %s", lines[0].c_str());
        for (size_t i = 1; i < lines.size(); i++) {
            const std::vector<std::string> row = split(lines[i]);
            CHECK(row.size() == hdr.size(), "row %zu has %zu fields, header %zu", i, row.size(), hdr.size());
            if (row.size() != hdr.size()) continue;
            const long long fill = std::atoll(row.back().c_str());
            CHECK(fill >= 0 && !row.back().empty() && row.back()[0] != '-', "row %zu fill_ns >= 0 (%s)", i,
                  row.back().c_str());
            CHECK(fill == (i == 2 ? 4500 : 0), "row %zu fill_ns round-trips (%lld)", i, fill);
        }
    }
    std::remove((prefix + "-emu.csv").c_str());
    std::remove((prefix + "-ui.csv").c_str());
    if (g_fail) return 1;
    std::printf("perf_trace_test: ok\n");
    return 0;
}
