/*
 * test_jaggd_fs.c -- host unit test for the JagGD SD-card layer (#783)
 *
 * Links src/core/jaggd_fs.c + the libretro-common file/VFS sources
 * directly; no core, no ROMs, CI-safe.  Covers what the end-to-end probe
 * in test_jgd.c cannot reach cheaply: every open disposition, read-only
 * cards, sandbox refusals (.., drive letters, symlinks), the FILINFO
 * record layout, directory iteration and its end marker, handle limits,
 * and savestate restore that must never truncate a file.  c99 allowed.
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/core/jaggd_fs.h"

static int fails = 0;
static char root[512];

#define CHECK(cond, msg) do { \
   if (cond) printf("  PASS: %s\n", msg); \
   else { printf("  FAIL: %s\n", msg); fails++; } \
} while (0)

static void host_write(const char *rel, const char *data)
{
   char p[700];
   FILE *f;
   snprintf(p, sizeof(p), "%s/%s", root, rel);
   f = fopen(p, "wb");
   if (f) { fputs(data, f); fclose(f); }
}

static long host_size(const char *rel)
{
   char p[700];
   struct stat st;
   snprintf(p, sizeof(p), "%s/%s", root, rel);
   return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static uint32_t be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | p[3];
}

int main(void)
{
   const char *tmp = getenv("TMPDIR");
   char p[700];
   uint8_t buf[64];
   uint8_t info[JGDFS_INFO_LONG_SIZE];
   uint8_t *state;
   uint32_t got = 0;
   int32_t h, h2, d, i;
   int32_t many[JGDFS_MAX_FILES + 1];

   if (!tmp || !tmp[0])
      tmp = "/tmp";
   snprintf(root, sizeof(root), "%s/vj_jgdfs_%ld/card", tmp, (long)getpid());

   /* ---- no card ---- */
   JGDFSSetRoot(root);
   JGDFSSetAccess(JGDFS_ACCESS_OFF);
   CHECK(JGDFSCardIn() == 0, "access off: no card");
   CHECK(JGDFSOpen("/x", JGDFS_FOPEN_READ) == -1, "access off: open fails");

   /* ---- read/write card, created lazily ---- */
   snprintf(p, sizeof(p), "%s/vj_jgdfs_%ld", tmp, (long)getpid());
   mkdir(p, 0700);
   JGDFSSetAccess(JGDFS_ACCESS_READWRITE);
   CHECK(JGDFSCardIn() == 1, "read/write: root created lazily");

   host_write("Hello.TXT", "hello");
   h = JGDFSOpen("0:/HELLO.txt", JGDFS_FOPEN_READ);
   CHECK(h >= 0, "open: drive prefix + case-insensitive match");
   CHECK(JGDFSSize(h) == 5, "size");
   CHECK(JGDFSRead(h, buf, 3, &got) == 0 && got == 3
         && memcmp(buf, "hel", 3) == 0, "read 3");
   CHECK(JGDFSTell(h) == 3, "tell");
   CHECK(JGDFSRead(h, buf, 10, &got) == 0 && got == 2,
         "short read at EOF is success");
   CHECK(JGDFSSeek(h, JGDFS_SEEK_END, -1) == 0
         && JGDFSRead(h, buf, 1, &got) == 0 && buf[0] == 'o',
         "seek from end");
   CHECK(JGDFSSeek(h, JGDFS_SEEK_SET, -1) == -1, "seek before start fails");
   CHECK(JGDFSWrite(h, (const uint8_t *)"x", 1) == -1,
         "write on a read-only handle fails");
   CHECK(JGDFSClose(h) == 0 && JGDFSClose(h) == -1, "close, double close");

   CHECK(JGDFSOpen("/missing.bin", JGDFS_FOPEN_READ) == -1,
         "OPEN_EXISTING on a missing file fails");
   h = JGDFSOpen("/new.bin", JGDFS_FOPEN_WRITE | JGDFS_FOPEN_CREATE_NEW);
   CHECK(h >= 0, "CREATE_NEW creates");
   CHECK(JGDFSWrite(h, (const uint8_t *)"abc", 3) == 0, "write");
   JGDFSClose(h);
   CHECK(JGDFSOpen("/new.bin", JGDFS_FOPEN_WRITE | JGDFS_FOPEN_CREATE_NEW)
         == -1, "CREATE_NEW on an existing file fails");
   h = JGDFSOpen("/new.bin", JGDFS_FOPEN_WRITE | JGDFS_FOPEN_OPEN_APPEND);
   CHECK(h >= 0 && JGDFSWrite(h, (const uint8_t *)"de", 2) == 0,
         "OPEN_APPEND writes at the end");
   JGDFSClose(h);
   CHECK(host_size("new.bin") == 5, "append result is 5 bytes");
   h = JGDFSOpen("/new.bin", JGDFS_FOPEN_WRITE | JGDFS_FOPEN_CREATE_ALWAYS);
   JGDFSClose(h);
   CHECK(h >= 0 && host_size("new.bin") == 0, "CREATE_ALWAYS truncates");
   CHECK(JGDFSOpen("/new.bin", 0x20) == -1, "undefined disposition refused");

   /* ---- sandbox ---- */
   CHECK(JGDFSOpen("../escape", JGDFS_FOPEN_READ) == -1, "'..' refused");
   CHECK(JGDFSOpen("/a/../../escape", JGDFS_FOPEN_READ) == -1,
         "nested '..' refused");
   CHECK(JGDFSOpen("C:/hello.txt", JGDFS_FOPEN_READ) == -1,
         "drive letter refused");
   CHECK(JGDFSOpen("/he*lo.txt", JGDFS_FOPEN_READ) == -1,
         "FAT-invalid character refused");
   CHECK(JGDFSOpen("/hello.txt/x", JGDFS_FOPEN_READ) == -1,
         "a file used as a directory refused");
   snprintf(p, sizeof(p), "%s/escape_link", root);
   CHECK(symlink("/etc", p) == 0, "test setup: symlink to /etc");
   CHECK(JGDFSOpen("/escape_link/hosts", JGDFS_FOPEN_READ) == -1,
         "symlink out of the root refused");
   CHECK(JGDFSInfo("/escape_link", info, 0) == -1,
         "symlink refused by FileInfo too");
   unlink(p);

   /* ---- FILINFO record ---- */
   host_write("A Very Long Name.data", "123456");
   CHECK(JGDFSInfo("/a very long name.data", info, 1) == 0, "info (long)");
   CHECK(be32(info) == 6, "info size, big-endian");
   CHECK(info[8] == 0x20, "info attrib = archive");
   CHECK(strcmp((char *)info + 9, "AVERYL~1.DAT") == 0, "8.3 alias");
   CHECK(strcmp((char *)info + 22, "A Very Long Name.data") == 0,
         "long name");
   CHECK(JGDFSInfo("/hello.txt", info, 0) == 0
         && strcmp((char *)info + 9, "HELLO.TXT") == 0, "info (short)");

   /* ---- directories ---- */
   snprintf(p, sizeof(p), "%s/Sub", root);
   mkdir(p, 0700);
   host_write("Sub/one.bin", "1");
   host_write("Sub/two.bin", "22");
   d = JGDFSDirOpen("/sub");
   CHECK(d >= 0, "dir open (case-insensitive)");
   {
      int n = 0, saw_one = 0, saw_two = 0;
      while (JGDFSDirRead(d, info, 1) == 0)
      {
         n++;
         if (strcmp((char *)info + 22, "one.bin") == 0) saw_one = 1;
         if (strcmp((char *)info + 22, "two.bin") == 0
             && be32(info) == 2) saw_two = 1;
      }
      CHECK(n == 2 && saw_one && saw_two, "dir lists both entries");
      CHECK(info[9] == 0 && info[22] == 0,
            "end of directory: -1 with the name zeroed");
   }
   CHECK(JGDFSDirClose(d) == 0, "dir close");
   CHECK(JGDFSDirOpen("/hello.txt") == -1, "dir open on a file fails");

   /* ---- handle limit ---- */
   for (i = 0; i <= JGDFS_MAX_FILES; i++)
      many[i] = JGDFSOpen("/hello.txt", JGDFS_FOPEN_READ);
   CHECK(many[JGDFS_MAX_FILES - 1] >= 0 && many[JGDFS_MAX_FILES] == -1,
         "open fails past the handle limit");
   JGDFSCloseAll();

   /* ---- savestate ---- */
   state = (uint8_t *)malloc(JGDFSStateSize());
   host_write("save.dat", "0123456789");
   h  = JGDFSOpen("/save.dat", JGDFS_FOPEN_READ | JGDFS_FOPEN_WRITE);
   h2 = JGDFSOpen("/hello.txt", JGDFS_FOPEN_READ);
   JGDFSSeek(h, JGDFS_SEEK_SET, 4);
   CHECK(JGDFSStateSave(state) == JGDFSStateSize(), "state save size");
   JGDFSSeek(h, JGDFS_SEEK_SET, 8);
   CHECK(JGDFSStateLoad(state) == JGDFSStateSize()
         && JGDFSTell(h) == 4, "state load re-seeks an open handle");
   JGDFSCloseAll();
   CHECK(JGDFSTell(h) == 0xFFFFFFFFu, "closed before restore");
   JGDFSStateLoad(state);
   CHECK(JGDFSTell(h) == 4 && JGDFSTell(h2) == 0,
         "state load reopens closed handles at their positions");
   CHECK(host_size("save.dat") == 10, "restore never truncates");
   CHECK(JGDFSRead(h, buf, 2, &got) == 0 && memcmp(buf, "45", 2) == 0,
         "restored handle reads from its position");
   CHECK(JGDFSWrite(h, (const uint8_t *)"Z", 1) == 0,
         "restored handle keeps its write access");
   JGDFSCloseAll();
   snprintf(p, sizeof(p), "%s/save.dat", root);
   unlink(p);
   JGDFSStateLoad(state);
   CHECK(JGDFSTell(h) == 0xFFFFFFFFu, "deleted file comes back closed");
   memset(state, 0, JGDFSStateSize());
   JGDFSStateLoad(state);
   CHECK(JGDFSTell(h2) == 0xFFFFFFFFu,
         "a zero (pre-chunk) state closes everything");
   free(state);

   /* ---- read-only card ---- */
   JGDFSSetAccess(JGDFS_ACCESS_READONLY);
   CHECK(JGDFSOpen("/hello.txt", JGDFS_FOPEN_READ) >= 0,
         "read-only card: read works");
   CHECK(JGDFSOpen("/hello.txt", JGDFS_FOPEN_READ | JGDFS_FOPEN_WRITE)
         == -1, "read-only card: write access refused");
   CHECK(JGDFSOpen("/hello.txt", JGDFS_FOPEN_READ | JGDFS_FOPEN_OPEN_ALWAYS)
         == -1, "read-only card: any create flag refused (FatFs rule)");
   CHECK(JGDFSInfo("/hello.txt", info, 0) == 0 && (info[8] & 0x01),
         "read-only card: attrib carries AM_RDO");

   JGDFSDone();
   CHECK(JGDFSCardIn() == 0, "done: card gone");

   /* cleanup */
   snprintf(p, sizeof(p), "rm -rf '%s/vj_jgdfs_%ld'", tmp, (long)getpid());
   if (system(p) != 0)
      printf("  (cleanup of the temp card failed)\n");

   printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
   return fails ? 1 : 0;
}
