// VJ_PERF_CSV (perf.h): one row per measured UI iteration, with the audio-underrun and input-latency columns the #967
// measurement uses.  No window: Perf is driven by hand.
//
//   vj_perf_csv_test <scratch-dir>
#include "perf.h"

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_perf_csv_test <scratch-dir>\n");
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/vj_perf_csv_test.csv";
#ifdef _WIN32
    _putenv_s("VJ_PERF_CSV", path.c_str());
#else
    setenv("VJ_PERF_CSV", path.c_str(), 1);
#endif
    vj::Perf p;
    p.init(3, false);
    p.warmup = 0;
    for (int i = 0; i < 4; i++) {   // iteration i's values are recorded by the frameBegin() of iteration i + 1
        p.frameBegin();
        p.emulated();
        p.curUnder = i == 2 ? 1u : 0u;
        p.curLat = i == 1 ? 4.5f : -1.0f;
        p.frameEnd();
    }
    p.report("perf_csv_test");
    std::ifstream f(path);
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) lines.push_back(l);
    CHECK(lines.size() == 4, "a header and three rows (%zu lines)", lines.size());
    if (lines.size() == 4) {
        CHECK(lines[0] == "iter_ms,emu_frames,swap_sync,underruns,input_lat_ms", "header: %s", lines[0].c_str());
        CHECK(lines[1].find(",1,1,0,-1.000") != std::string::npos, "row 1, one frame and no input: %s", lines[1].c_str());
        CHECK(lines[2].find(",0,4.500") != std::string::npos, "row 2, a 4.5 ms input latency: %s", lines[2].c_str());
        CHECK(lines[3].find(",1,-1.000") != std::string::npos, "row 3, one underrun: %s", lines[3].c_str());
    }
    std::printf(g_fail ? "perf_csv_test: %d FAILED\n" : "perf_csv_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
