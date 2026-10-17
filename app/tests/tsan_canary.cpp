// Built only with -DVJ_APP_TSAN=ON: a deliberate data race.  Registered WILL_FAIL, so the ThreadSanitizer job proves
// its sanitizer is really on -- a build without it exits 0 here and that fails the test.
#include <cstdio>
#include <thread>

static int g_shared = 0;

int main() {
    std::thread a([] {
        for (int i = 0; i < 100000; i++) g_shared++;
    });
    std::thread b([] {
        for (int i = 0; i < 100000; i++) g_shared++;
    });
    a.join();
    b.join();
    std::printf("tsan_canary: %d\n", g_shared);
    return 0;
}
