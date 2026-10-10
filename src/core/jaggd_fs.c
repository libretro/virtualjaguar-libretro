/*
 * jaggd_fs.c -- JagGD SD-card file API against a host directory (#783)
 *
 * See jaggd_fs.h for the contract.  Semantics follow FatFs, which is what
 * the GameDrive firmware wraps: the open dispositions, the "any create or
 * write flag needs a writable card" rule, short reads at end of file as
 * success, and the FILINFO date/time encoding.  Constants and struct
 * offsets come from RetroHQ's published gdbios.h (github.com/RetroHQ/JagGD)
 * -- facts only; none of RetroHQ's files are in this tree.
 */

#include "jaggd_fs.h"

#include <string.h>
#include <time.h>

#include <boolean.h>
#include <libretro.h>
#include <streams/file_stream.h>
#include <vfs/vfs_implementation.h>
#include <compat/strl.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/stat.h>
#define JGDFS_HAVE_POSIX_STAT 1
#endif

#include "state.h"

#define JGDFS_MAGIC 0x4A474631u   /* "JGF1" */
#define JGDFS_HOST_MAX 1400

typedef struct
{
   RFILE   *fp;
   uint8_t  in_use;
   uint8_t  readable;
   uint8_t  writable;
   char     rel[JGDFS_PATH_MAX];   /* card-relative, host-cased */
} jgdfs_file;

typedef struct
{
   libretro_vfs_implementation_dir *dir;
   uint8_t  in_use;
   uint32_t index;                 /* entries handed out so far */
   char     rel[JGDFS_PATH_MAX];
} jgdfs_dir;

static jgdfs_file fs_files[JGDFS_MAX_FILES];
static jgdfs_dir  fs_dirs[JGDFS_MAX_DIRS];
static char       fs_root[JGDFS_HOST_MAX];
static int        fs_access = JGDFS_ACCESS_OFF;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int jgdfs_ci_equal(const char *a, const char *b)
{
   while (*a && *b)
   {
      int ca = (unsigned char)*a++;
      int cb = (unsigned char)*b++;
      if (ca >= 'a' && ca <= 'z') ca -= 32;
      if (cb >= 'a' && cb <= 'z') cb -= 32;
      if (ca != cb)
         return 0;
   }
   return *a == *b;
}

/* root + "/" + rel (rel may be ""). */
static int jgdfs_host_path(const char *rel, char *out, size_t out_sz)
{
   if (strlcpy(out, fs_root, out_sz) >= out_sz)
      return 0;
   if (rel[0])
   {
      if (strlcat(out, "/", out_sz) >= out_sz)
         return 0;
      if (strlcat(out, rel, out_sz) >= out_sz)
         return 0;
   }
   return 1;
}

/* 1 = exists (is_dir set), 0 = missing. */
static int jgdfs_stat(const char *host, int *is_dir, uint32_t *size)
{
   int32_t sz = 0;
   int flags = retro_vfs_stat_impl(host, &sz);

   if (!(flags & RETRO_VFS_STAT_IS_VALID))
      return 0;
   if (is_dir)
      *is_dir = (flags & RETRO_VFS_STAT_IS_DIRECTORY) ? 1 : 0;
   if (size)
      *size = (sz > 0) ? (uint32_t)sz : 0u;
   return 1;
}

/* A symlink anywhere under the root could point outside it: refuse all
 * of them on hosts where we can tell. */
static int jgdfs_is_symlink(const char *host)
{
#ifdef JGDFS_HAVE_POSIX_STAT
   struct stat st;
   if (lstat(host, &st) == 0 && S_ISLNK(st.st_mode))
      return 1;
#else
   (void)host;
#endif
   return 0;
}

static int jgdfs_root_ready(void)
{
   int is_dir = 0;

   if (fs_access == JGDFS_ACCESS_OFF || !fs_root[0])
      return 0;
   if (jgdfs_stat(fs_root, &is_dir, NULL))
      return is_dir;
   /* Lazily created; a read-only card is never created on the host. */
   if (fs_access != JGDFS_ACCESS_READWRITE)
      return 0;
   retro_vfs_mkdir_impl(fs_root);
   return jgdfs_stat(fs_root, &is_dir, NULL) && is_dir;
}

/* Find `comp` inside host directory `parent` case-insensitively; writes
 * the host's spelling to `found`.  1 on a match. */
static int jgdfs_ci_lookup(const char *parent, const char *comp,
                           char *found, size_t found_sz)
{
   libretro_vfs_implementation_dir *d;
   int hit = 0;

   d = retro_vfs_opendir_impl(parent, true);
   if (!d)
      return 0;
   while (retro_vfs_readdir_impl(d))
   {
      const char *n = retro_vfs_dirent_get_name_impl(d);
      if (n && jgdfs_ci_equal(n, comp))
      {
         strlcpy(found, n, found_sz);
         hit = 1;
         break;
      }
   }
   retro_vfs_closedir_impl(d);
   return hit;
}

static int jgdfs_bad_char(int c)
{
   return c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"'
       || c == '<' || c == '>' || c == '|';
}

/*
 * Resolve a GD path to a card-relative path (host casing) and a host path.
 * Accepts an optional "0:" drive prefix and '/' or '\' separators; empty
 * and "." components are skipped.  Refuses "..", FAT-invalid characters,
 * symlinks, a missing intermediate directory, and anything too long.
 *
 * Returns 1 = exists (*is_dir set), 0 = only the last component is
 * missing (rel/host name it as given, for create), -1 = refused.
 * An empty path resolves to the root itself (exists, directory).
 */
static int jgdfs_resolve(const char *gdpath, char *rel, char *host,
                         int *is_dir)
{
   char comp[JGDFS_PATH_MAX];
   char found[JGDFS_PATH_MAX];
   const char *p = gdpath;
   size_t clen;
   int last_missing = 0;

   rel[0] = '\0';
   *is_dir = 1;
   if (!gdpath || !jgdfs_root_ready())
      return -1;
   if (!jgdfs_host_path("", host, JGDFS_HOST_MAX))
      return -1;

   if (p[0] >= '0' && p[0] <= '9' && p[1] == ':')
      p += 2;

   while (*p)
   {
      while (*p == '/' || *p == '\\')
         p++;
      if (!*p)
         break;
      clen = 0;
      while (*p && *p != '/' && *p != '\\')
      {
         if (jgdfs_bad_char((unsigned char)*p) || clen + 1 >= sizeof(comp))
            return -1;
         comp[clen++] = *p++;
      }
      comp[clen] = '\0';

      if (strcmp(comp, ".") == 0)
         continue;
      if (strcmp(comp, "..") == 0)
         return -1;
      /* A component after a missing one: an intermediate dir is absent. */
      if (last_missing)
         return -1;

      if (jgdfs_ci_lookup(host, comp, found, sizeof(found)))
         strlcpy(comp, found, sizeof(comp));
      else
         last_missing = 1;

      if (rel[0] && strlcat(rel, "/", JGDFS_PATH_MAX) >= JGDFS_PATH_MAX)
         return -1;
      if (strlcat(rel, comp, JGDFS_PATH_MAX) >= JGDFS_PATH_MAX)
         return -1;
      if (strlcat(host, "/", JGDFS_HOST_MAX) >= JGDFS_HOST_MAX
          || strlcat(host, comp, JGDFS_HOST_MAX) >= JGDFS_HOST_MAX)
         return -1;

      if (!last_missing)
      {
         if (jgdfs_is_symlink(host))
            return -1;
         if (!jgdfs_stat(host, is_dir, NULL))
            return -1;
         /* Only a directory can have children. */
         if (!*is_dir)
         {
            const char *q = p;
            while (*q == '/' || *q == '\\' || *q == '.')
               q++;
            if (*q)
               return -1;
         }
      }
   }

   if (last_missing)
   {
      *is_dir = 0;
      return 0;
   }
   return 1;
}

static void jgdfs_put_be32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v >> 24);
   p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);
   p[3] = (uint8_t)v;
}

static void jgdfs_put_be16(uint8_t *p, uint16_t v)
{
   p[0] = (uint8_t)(v >> 8);
   p[1] = (uint8_t)v;
}

/* FatFs 8.3 alias: upper-case base (<= 8) and extension (<= 3); a name
 * that did not fit gets "~1" like FatFs's first numbered alias. */
static void jgdfs_short_name(const char *name, char *out13)
{
   char base[9];
   char ext[4];
   const char *dot = strrchr(name, '.');
   size_t bl = 0, el = 0, i;
   int lossy = 0;
   const char *s;

   if (dot == name)
      dot = NULL;            /* ".profile": no extension */
   for (s = name; *s && s != dot; s++)
   {
      int c = (unsigned char)*s;
      if (c == ' ' || c == '.')
      {
         lossy = 1;
         continue;
      }
      if (c >= 'a' && c <= 'z')
         c -= 32;
      if (bl < 8)
         base[bl++] = (char)c;
      else
         lossy = 1;
   }
   if (dot)
      for (s = dot + 1; *s; s++)
      {
         int c = (unsigned char)*s;
         if (c >= 'a' && c <= 'z')
            c -= 32;
         if (el < 3)
            ext[el++] = (char)c;
         else
            lossy = 1;
      }
   if (bl == 0)
   {
      base[bl++] = '_';
      lossy = 1;
   }
   if (lossy)
   {
      if (bl > 6)
         bl = 6;
      base[bl++] = '~';
      base[bl++] = '1';
   }
   base[bl] = '\0';
   ext[el] = '\0';

   memset(out13, 0, 13);
   for (i = 0; i < bl; i++)
      out13[i] = base[i];
   if (el)
   {
      out13[bl] = '.';
      for (i = 0; i < el; i++)
         out13[bl + 1 + i] = ext[i];
   }
}

/* FILINFO date/time: date = (year-1980)<<9 | month<<5 | day,
 * time = hour<<11 | minute<<5 | second/2.  (gdbios.h's two comments on
 * these fields are swapped; this is the FatFs encoding.) */
static void jgdfs_mtime(const char *host, uint16_t *date, uint16_t *tim)
{
   *date = (uint16_t)((0u << 9) | (1u << 5) | 1u);   /* 1980-01-01 */
   *tim  = 0;
#ifdef JGDFS_HAVE_POSIX_STAT
   {
      struct stat st;
      struct tm *t;
      if (stat(host, &st) == 0 && (t = localtime(&st.st_mtime)) != NULL
          && t->tm_year >= 80)
      {
         *date = (uint16_t)(((unsigned)(t->tm_year - 80) << 9)
               | ((unsigned)(t->tm_mon + 1) << 5) | (unsigned)t->tm_mday);
         *tim  = (uint16_t)(((unsigned)t->tm_hour << 11)
               | ((unsigned)t->tm_min << 5) | ((unsigned)t->tm_sec / 2));
      }
   }
#else
   (void)host;
#endif
}

static void jgdfs_fill_info(uint8_t *out, int long_name, const char *host,
                            const char *name, int is_dir, uint32_t size)
{
   uint16_t date, tim;
   uint8_t attrib = is_dir ? 0x10 : 0x20;          /* AM_DIR / AM_ARC */
   size_t n;

   if (fs_access != JGDFS_ACCESS_READWRITE)
      attrib |= 0x01;                               /* AM_RDO */
   memset(out, 0, long_name ? JGDFS_INFO_LONG_SIZE : JGDFS_INFO_SHORT_SIZE);
   jgdfs_mtime(host, &date, &tim);
   jgdfs_put_be32(out + 0, is_dir ? 0u : size);
   jgdfs_put_be16(out + 4, date);
   jgdfs_put_be16(out + 6, tim);
   out[8] = attrib;
   jgdfs_short_name(name, (char *)out + 9);
   if (long_name)
   {
      n = strlen(name);
      if (n > 255)
         n = 255;
      memcpy(out + 22, name, n);
   }
}

static const char *jgdfs_basename(const char *rel)
{
   const char *s = strrchr(rel, '/');
   return s ? s + 1 : rel;
}

static jgdfs_file *jgdfs_get_file(uint32_t h)
{
   if (h >= JGDFS_MAX_FILES || !fs_files[h].in_use)
      return NULL;
   return &fs_files[h];
}

static void jgdfs_close_file(jgdfs_file *f)
{
   if (f->fp)
      filestream_close(f->fp);
   memset(f, 0, sizeof(*f));
}

static void jgdfs_close_dir(jgdfs_dir *d)
{
   if (d->dir)
      retro_vfs_closedir_impl(d->dir);
   memset(d, 0, sizeof(*d));
}

/* Open a host file with READ, WRITE or both -- never truncating. */
static RFILE *jgdfs_open_host(const char *host, int rd, int wr)
{
   unsigned acc = 0;

   if (wr)
      acc = (rd ? RETRO_VFS_FILE_ACCESS_READ_WRITE
                : RETRO_VFS_FILE_ACCESS_WRITE)
          | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
   else
      acc = RETRO_VFS_FILE_ACCESS_READ;
   return filestream_open(host, acc, RETRO_VFS_FILE_ACCESS_HINT_NONE);
}

/* Create (or truncate) a host file. */
static int jgdfs_create_host(const char *host)
{
   RFILE *fp = filestream_open(host, RETRO_VFS_FILE_ACCESS_WRITE,
                               RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!fp)
      return 0;
   filestream_close(fp);
   return 1;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

void JGDFSSetRoot(const char *dir)
{
   if (!dir || strlcpy(fs_root, dir, sizeof(fs_root)) >= sizeof(fs_root))
      fs_root[0] = '\0';
}

void JGDFSSetAccess(int access)
{
   if (access < JGDFS_ACCESS_OFF || access > JGDFS_ACCESS_READWRITE)
      access = JGDFS_ACCESS_OFF;
   fs_access = access;
   if (access == JGDFS_ACCESS_OFF)
      JGDFSCloseAll();
}

int JGDFSGetAccess(void)
{
   return fs_access;
}

int JGDFSCardIn(void)
{
   return jgdfs_root_ready();
}

/* ------------------------------------------------------------------ */
/* Files                                                              */
/* ------------------------------------------------------------------ */

int32_t JGDFSOpen(const char *gdpath, uint32_t mode)
{
   char rel[JGDFS_PATH_MAX];
   char host[JGDFS_HOST_MAX];
   int is_dir, r, i, slot = -1;
   int rd   = (mode & JGDFS_FOPEN_READ) ? 1 : 0;
   int wr   = (mode & JGDFS_FOPEN_WRITE) ? 1 : 0;
   uint32_t disp = mode & 0x3Cu;
   RFILE *fp;

   if (mode & ~0x3Fu)
      return -1;
   r = jgdfs_resolve(gdpath, rel, host, &is_dir);
   if (r < 0 || !rel[0] || (r == 1 && is_dir))
      return -1;
   /* FatFs: any write or create flag needs a writable volume. */
   if ((wr || disp) && fs_access != JGDFS_ACCESS_READWRITE)
      return -1;

   for (i = 0; i < JGDFS_MAX_FILES; i++)
      if (!fs_files[i].in_use)
      {
         slot = i;
         break;
      }
   if (slot < 0)
      return -1;

   switch (disp)
   {
   case 0x00:                                   /* OPEN_EXISTING */
      if (r == 0)
         return -1;
      break;
   case JGDFS_FOPEN_CREATE_NEW:
      if (r == 1 || !jgdfs_create_host(host))
         return -1;
      break;
   case JGDFS_FOPEN_CREATE_ALWAYS:
      if (!jgdfs_create_host(host))
         return -1;
      break;
   case JGDFS_FOPEN_OPEN_ALWAYS:
   case JGDFS_FOPEN_OPEN_APPEND:
      if (r == 0 && !jgdfs_create_host(host))
         return -1;
      break;
   default:
      return -1;
   }

   fp = jgdfs_open_host(host, rd, wr);
   if (!fp)
      return -1;
   if (disp == JGDFS_FOPEN_OPEN_APPEND)
      filestream_seek(fp, 0, RETRO_VFS_SEEK_POSITION_END);

   fs_files[slot].fp       = fp;
   fs_files[slot].in_use   = 1;
   fs_files[slot].readable = (uint8_t)rd;
   fs_files[slot].writable = (uint8_t)wr;
   strlcpy(fs_files[slot].rel, rel, sizeof(fs_files[slot].rel));
   return slot;
}

int32_t JGDFSClose(uint32_t handle)
{
   jgdfs_file *f = jgdfs_get_file(handle);
   if (!f)
      return -1;
   jgdfs_close_file(f);
   return 0;
}

int32_t JGDFSSeek(uint32_t handle, uint32_t whence, int32_t offset)
{
   jgdfs_file *f = jgdfs_get_file(handle);
   int64_t base, target;

   if (!f)
      return -1;
   switch (whence)
   {
   case JGDFS_SEEK_SET: base = 0; break;
   case JGDFS_SEEK_CUR: base = filestream_tell(f->fp); break;
   case JGDFS_SEEK_END: base = filestream_get_size(f->fp); break;
   default: return -1;
   }
   if (base < 0)
      return -1;
   target = base + (int64_t)offset;
   if (target < 0 || target > 0x7FFFFFFF)
      return -1;
   if (filestream_seek(f->fp, target, RETRO_VFS_SEEK_POSITION_START) < 0)
      return -1;
   return 0;
}

int32_t JGDFSRead(uint32_t handle, uint8_t *buf, uint32_t len,
                  uint32_t *got)
{
   jgdfs_file *f = jgdfs_get_file(handle);
   int64_t n;

   if (got)
      *got = 0;
   if (!f || !f->readable)
      return -1;
   if (len == 0)
      return 0;
   n = filestream_read(f->fp, buf, (int64_t)len);
   if (n < 0)
      return -1;
   if (got)
      *got = (uint32_t)n;
   return 0;
}

int32_t JGDFSWrite(uint32_t handle, const uint8_t *buf, uint32_t len)
{
   jgdfs_file *f = jgdfs_get_file(handle);

   if (!f || !f->writable || fs_access != JGDFS_ACCESS_READWRITE)
      return -1;
   if (len == 0)
      return 0;
   if (filestream_write(f->fp, buf, (int64_t)len) != (int64_t)len)
      return -1;
   filestream_flush(f->fp);
   return 0;
}

uint32_t JGDFSTell(uint32_t handle)
{
   jgdfs_file *f = jgdfs_get_file(handle);
   int64_t pos;

   if (!f)
      return 0xFFFFFFFFu;
   pos = filestream_tell(f->fp);
   return (pos < 0) ? 0xFFFFFFFFu : (uint32_t)pos;
}

uint32_t JGDFSSize(uint32_t handle)
{
   jgdfs_file *f = jgdfs_get_file(handle);
   int64_t sz;

   if (!f)
      return 0xFFFFFFFFu;
   sz = filestream_get_size(f->fp);
   return (sz < 0) ? 0xFFFFFFFFu : (uint32_t)sz;
}

int32_t JGDFSInfo(const char *gdpath, uint8_t *out, int long_name)
{
   char rel[JGDFS_PATH_MAX];
   char host[JGDFS_HOST_MAX];
   int is_dir = 0;
   uint32_t size = 0;

   if (!out || jgdfs_resolve(gdpath, rel, host, &is_dir) != 1 || !rel[0])
      return -1;
   jgdfs_stat(host, &is_dir, &size);
   jgdfs_fill_info(out, long_name, host, jgdfs_basename(rel), is_dir, size);
   return 0;
}

/* ------------------------------------------------------------------ */
/* Directories                                                        */
/* ------------------------------------------------------------------ */

int32_t JGDFSDirOpen(const char *gdpath)
{
   char rel[JGDFS_PATH_MAX];
   char host[JGDFS_HOST_MAX];
   int is_dir = 0, i;

   if (jgdfs_resolve(gdpath, rel, host, &is_dir) != 1 || !is_dir)
      return -1;
   for (i = 0; i < JGDFS_MAX_DIRS; i++)
      if (!fs_dirs[i].in_use)
      {
         fs_dirs[i].dir = retro_vfs_opendir_impl(host, false);
         if (!fs_dirs[i].dir)
            return -1;
         fs_dirs[i].in_use = 1;
         fs_dirs[i].index  = 0;
         strlcpy(fs_dirs[i].rel, rel, sizeof(fs_dirs[i].rel));
         return i;
      }
   return -1;
}

/* Advance to the next real entry; NULL at the end. */
static const char *jgdfs_dir_next(jgdfs_dir *d)
{
   while (retro_vfs_readdir_impl(d->dir))
   {
      const char *n = retro_vfs_dirent_get_name_impl(d->dir);
      if (!n || strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
         continue;
      return n;
   }
   return NULL;
}

int32_t JGDFSDirRead(uint32_t handle, uint8_t *out, int long_name)
{
   jgdfs_dir *d;
   const char *name;
   char host[JGDFS_HOST_MAX];
   char rel[JGDFS_PATH_MAX];
   int is_dir = 0;
   uint32_t size = 0;

   if (handle >= JGDFS_MAX_DIRS || !fs_dirs[handle].in_use || !out)
      return -1;
   d = &fs_dirs[handle];
   name = jgdfs_dir_next(d);
   if (!name)
   {
      memset(out, 0,
             long_name ? JGDFS_INFO_LONG_SIZE : JGDFS_INFO_SHORT_SIZE);
      return -1;
   }
   d->index++;
   strlcpy(rel, d->rel, sizeof(rel));
   if (rel[0])
      strlcat(rel, "/", sizeof(rel));
   strlcat(rel, name, sizeof(rel));
   if (jgdfs_host_path(rel, host, sizeof(host)))
      jgdfs_stat(host, &is_dir, &size);
   jgdfs_fill_info(out, long_name, host, name, is_dir, size);
   return 0;
}

int32_t JGDFSDirClose(uint32_t handle)
{
   if (handle >= JGDFS_MAX_DIRS || !fs_dirs[handle].in_use)
      return -1;
   jgdfs_close_dir(&fs_dirs[handle]);
   return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void JGDFSCloseAll(void)
{
   int i;
   for (i = 0; i < JGDFS_MAX_FILES; i++)
      if (fs_files[i].in_use || fs_files[i].fp)
         jgdfs_close_file(&fs_files[i]);
   for (i = 0; i < JGDFS_MAX_DIRS; i++)
      if (fs_dirs[i].in_use || fs_dirs[i].dir)
         jgdfs_close_dir(&fs_dirs[i]);
}

void JGDFSDone(void)
{
   JGDFSCloseAll();
   fs_root[0] = '\0';
   fs_access  = JGDFS_ACCESS_OFF;
}

/* ------------------------------------------------------------------ */
/* Savestate ("JGF1")                                                 */
/* ------------------------------------------------------------------ */

#define JGDFS_FILE_REC (1 + 1 + 1 + 1 + 4 + JGDFS_PATH_MAX)
#define JGDFS_DIR_REC  (1 + 3 + 4 + JGDFS_PATH_MAX)

size_t JGDFSStateSize(void)
{
   return 4u + JGDFS_MAX_FILES * JGDFS_FILE_REC
             + JGDFS_MAX_DIRS * JGDFS_DIR_REC;
}

size_t JGDFSStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   uint32_t magic = JGDFS_MAGIC;
   int i;

   memset(buf, 0, JGDFSStateSize());
   STATE_SAVE_VAR(buf, magic);
   for (i = 0; i < JGDFS_MAX_FILES; i++)
   {
      jgdfs_file *f = &fs_files[i];
      uint32_t pos = 0;
      if (f->in_use)
      {
         int64_t t = filestream_tell(f->fp);
         pos = (t < 0) ? 0u : (uint32_t)t;
         buf[0] = 1;
         buf[1] = f->readable;
         buf[2] = f->writable;
         memcpy(buf + 4, &pos, 4);
         memcpy(buf + 8, f->rel, JGDFS_PATH_MAX);
      }
      buf += JGDFS_FILE_REC;
   }
   for (i = 0; i < JGDFS_MAX_DIRS; i++)
   {
      jgdfs_dir *d = &fs_dirs[i];
      if (d->in_use)
      {
         buf[0] = 1;
         memcpy(buf + 4, &d->index, 4);
         memcpy(buf + 8, d->rel, JGDFS_PATH_MAX);
      }
      buf += JGDFS_DIR_REC;
   }
   return (size_t)(buf - start);
}

size_t JGDFSStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   uint32_t magic;
   char rel[JGDFS_PATH_MAX];
   char host[JGDFS_HOST_MAX];
   int i;
   uint32_t k;

   STATE_LOAD_VAR(buf, magic);
   if (magic != JGDFS_MAGIC)
   {
      /* A state written before this chunk existed: no open handles. */
      JGDFSCloseAll();
      return JGDFSStateSize();
   }

   for (i = 0; i < JGDFS_MAX_FILES; i++)
   {
      jgdfs_file *f = &fs_files[i];
      uint8_t in_use = buf[0], rd = buf[1] ? 1 : 0, wr = buf[2] ? 1 : 0;
      uint32_t pos;

      memcpy(&pos, buf + 4, 4);
      memcpy(rel, buf + 8, JGDFS_PATH_MAX);
      rel[JGDFS_PATH_MAX - 1] = '\0';
      buf += JGDFS_FILE_REC;

      if (!in_use)
      {
         if (f->in_use)
            jgdfs_close_file(f);
         continue;
      }
      /* Same file, same access: just re-seek (run-ahead hot path). */
      if (!(f->in_use && f->readable == rd && f->writable == wr
            && strcmp(f->rel, rel) == 0))
      {
         if (f->in_use)
            jgdfs_close_file(f);
         /* Reopen with the original access only -- never a create,
          * truncate or append.  The path is re-checked against the root
          * (a state is input too). */
         if (!rel[0] || strstr(rel, "..")
             || !jgdfs_host_path(rel, host, sizeof(host))
             || jgdfs_is_symlink(host)
             || (wr && fs_access != JGDFS_ACCESS_READWRITE))
            continue;
         f->fp = jgdfs_open_host(host, rd, wr);
         if (!f->fp)
            continue;
         f->in_use   = 1;
         f->readable = rd;
         f->writable = wr;
         strlcpy(f->rel, rel, sizeof(f->rel));
      }
      filestream_seek(f->fp, (int64_t)pos, RETRO_VFS_SEEK_POSITION_START);
   }

   for (i = 0; i < JGDFS_MAX_DIRS; i++)
   {
      jgdfs_dir *d = &fs_dirs[i];
      uint8_t in_use = buf[0];
      uint32_t index;

      memcpy(&index, buf + 4, 4);
      memcpy(rel, buf + 8, JGDFS_PATH_MAX);
      rel[JGDFS_PATH_MAX - 1] = '\0';
      buf += JGDFS_DIR_REC;

      if (!in_use)
      {
         if (d->in_use)
            jgdfs_close_dir(d);
         continue;
      }
      if (d->in_use && d->index == index && strcmp(d->rel, rel) == 0)
         continue;
      if (d->in_use)
         jgdfs_close_dir(d);
      if (strstr(rel, "..") || !jgdfs_host_path(rel, host, sizeof(host))
          || jgdfs_is_symlink(host))
         continue;
      d->dir = retro_vfs_opendir_impl(host, false);
      if (!d->dir)
         continue;
      d->in_use = 1;
      strlcpy(d->rel, rel, sizeof(d->rel));
      for (k = 0; k < index && jgdfs_dir_next(d); k++)
         ;
      d->index = k;
   }

   return (size_t)(buf - start);
}
