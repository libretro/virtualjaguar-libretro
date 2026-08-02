/*
 * nvmbios.c — HLE of the Memory Track "NVM BIOS" module.
 *
 * See nvmbios.h for the big picture.  Behaviour is transcribed from the
 * Atari NVM BIOS v1.01 68K source (asmnvm.s, preserved in
 * https://github.com/cubanismo/skunk_mtrk with thanks to James Jones for
 * releasing it and to the MiSTer Jaguar core for the hardware notes that
 * led there).  Where the original has quirks, we keep them — games were
 * written against the quirks:
 *
 *   - Initialize on a cart with a bad first-block checksum silently
 *     ZEROES the FAT + directory (first 4K) and reports success.  A
 *     freshly-blank cart therefore self-formats on first use, and 510
 *     zero bytes sum to the stored zero checksum, so no checksum write
 *     is needed.
 *   - Delete always reports success: the module recalculates the
 *     checksum after the delete attempt, clobbering D0, so even a
 *     missing file returns non-negative.  We return 0 unconditionally.
 *   - Close performs no handle validation.
 *   - Empty files are represented by startblock == FAT_EMPTYFILE (2)
 *     with 0 blocks.
 *   - Names use a 40-character set packed 3 chars/word; characters past
 *     the terminator (and any character not in the set) pack as index 39
 *     (space), and unpacking preserves the trailing spaces.
 *
 * All filesystem access goes straight to mtMem (the same 128K image the
 * flash device exposes at $900000), so anything the module writes is
 * what a real Memory Track dump would hold, and mt_dirty_cb keeps the
 * libretro save buffer current.
 */

#include "nvmbios.h"
#include "memtrack.h"
#include "m68000/m68kinterface.h"

#include <string.h>

/* Kept to direct externs (not jaguar.h) so unit tests can link this file
 * with a stub 68K and a bare RAM array.  NOTE: jaguarMainRAM is a POINTER
 * (see vjag_memory.h), not an array — declaring it as an array here would
 * silently write into the data segment at the pointer variable itself. */
extern uint8_t *jaguarMainRAM;
extern bool jaguarMemTrackInserted;

/* --------------------------------------------------------------------
 * Module state (the original keeps this in its BSS at $2400+; ours
 * lives host-side and is save-stated via NVMBiosStateSave/Load).
 * -------------------------------------------------------------------- */

typedef struct
{
   uint16_t startblock;   /* 0 = handle free/closed */
   uint32_t offset;
} nvm_handle;

static bool     nvm_initialized = false;    /* Initialize called */
static uint16_t nvm_appname[NVM_APPPACKSIZE];
static nvm_handle nvm_handles[NVM_NUMFILES];
static uint16_t nvm_search_location = 0;
static uint16_t nvm_search_flags = 0;

/* FAT entry values */
#define FAT_FREE      0
#define FAT_END       1
#define FAT_EMPTYFILE 2

/* NVRAM byte offsets */
#define CHKSUM_OFFSET    0
#define USEDSPACE_OFFSET 3

/* --------------------------------------------------------------------
 * NVRAM byte access — mtMem is the logical byte space.
 * -------------------------------------------------------------------- */

static uint8_t nv_get(uint32_t addr)
{
   return mtMem[addr & (MT_MEM_SIZE - 1)];
}

static void nv_put(uint32_t addr, uint8_t val)
{
   mtMem[addr & (MT_MEM_SIZE - 1)] = val;
}

static void nv_flush(void)
{
   if (mt_dirty_cb)
      mt_dirty_cb();
}

/* 16-bit sum of bytes 2..511, the module's first-block checksum. */
static uint16_t calc_chksum(void)
{
   uint32_t a;
   uint16_t sum = 0;
   for (a = 2; a < NVM_BLOCKSIZE; a++)
      sum += nv_get(a);
   return sum;
}

static void set_chksum(void)
{
   uint16_t sum = calc_chksum();
   nv_put(CHKSUM_OFFSET,     (uint8_t)(sum >> 8));
   nv_put(CHKSUM_OFFSET + 1, (uint8_t)(sum & 0xFF));
}

/* --------------------------------------------------------------------
 * Packed names: 40-character set, 3 chars per word.
 * -------------------------------------------------------------------- */

static const char nvm_charset[40] =
   "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:'. ";

/* One character to its 5-ish-bit index (fivebit in the original). */
static uint16_t nvm_charindex(char c)
{
   int i;
   if (c >= 'a' && c <= 'z')
      return (uint16_t)(c - 'a');
   for (i = 0; i < 39; i++)
      if (nvm_charset[i] == c)
         return (uint16_t)i;
   return 39;                       /* not in set -> space */
}

/* ascii2pack: chars past the NUL pack as space (39). */
static void ascii2pack(const char *src, uint16_t *dst, unsigned words)
{
   unsigned w;
   bool eos = false;
   for (w = 0; w < words; w++)
   {
      uint16_t packed = 0;
      unsigned k;
      for (k = 0; k < 3; k++)
      {
         uint16_t idx;
         char c = eos ? '\0' : *src;
         if (c == '\0')
            eos = true;
         idx = eos ? 39 : nvm_charindex(c);
         if (!eos)
            src++;
         packed = (uint16_t)(packed * 40 + idx);
      }
      dst[w] = packed;
   }
}

/* pack2ascii: trailing spaces are preserved, NUL-terminated. */
static void pack2ascii(const uint16_t *src, char *dst, unsigned words)
{
   unsigned w;
   for (w = 0; w < words; w++)
   {
      uint16_t v = src[w];
      dst[w * 3 + 0] = nvm_charset[(v / 1600) % 40];
      dst[w * 3 + 1] = nvm_charset[(v / 40) % 40];
      dst[w * 3 + 2] = nvm_charset[v % 40];
   }
   dst[words * 3] = '\0';
}

/* --------------------------------------------------------------------
 * Directory entries: 18 bytes at DIROFFSET + n*18.
 *   [0] startblock  [1] numblocks  [2..11] appname  [12..17] filename
 * -------------------------------------------------------------------- */

typedef struct
{
   uint8_t  startblock;
   uint8_t  numblocks;
   uint16_t appname[NVM_APPPACKSIZE];
   uint16_t filename[NVM_FILEPACKSIZE];
} nvm_dirent;

static void fetch_dir(unsigned n, nvm_dirent *d)
{
   uint32_t a = NVM_DIROFFSET + n * NVM_DIRENTRYSIZE;
   unsigned i;
   d->startblock = nv_get(a + 0);
   d->numblocks  = nv_get(a + 1);
   for (i = 0; i < NVM_APPPACKSIZE; i++)
      d->appname[i] = (uint16_t)((nv_get(a + 2 + i * 2) << 8)
                                 | nv_get(a + 3 + i * 2));
   for (i = 0; i < NVM_FILEPACKSIZE; i++)
      d->filename[i] = (uint16_t)((nv_get(a + 12 + i * 2) << 8)
                                  | nv_get(a + 13 + i * 2));
}

static void put_dir(unsigned n, const nvm_dirent *d)
{
   uint32_t a = NVM_DIROFFSET + n * NVM_DIRENTRYSIZE;
   unsigned i;
   nv_put(a + 0, d->startblock);
   nv_put(a + 1, d->numblocks);
   for (i = 0; i < NVM_APPPACKSIZE; i++)
   {
      nv_put(a + 2 + i * 2, (uint8_t)(d->appname[i] >> 8));
      nv_put(a + 3 + i * 2, (uint8_t)(d->appname[i] & 0xFF));
   }
   for (i = 0; i < NVM_FILEPACKSIZE; i++)
   {
      nv_put(a + 12 + i * 2, (uint8_t)(d->filename[i] >> 8));
      nv_put(a + 13 + i * 2, (uint8_t)(d->filename[i] & 0xFF));
   }
}

/* --------------------------------------------------------------------
 * FAT: one byte per block at offset 0..255 (first 8 reserved/system).
 * -------------------------------------------------------------------- */

/* AllocBlocks: returns first block, FAT_EMPTYFILE for 0 blocks, or error.
 * Mirrors the original's order: the used-space byte is bumped before the
 * scan; an exhausted scan (corrupt FAT) leaves it bumped and reports
 * ERANGE, exactly as the 68K code does. */
static int32_t alloc_blocks(unsigned nblocks)
{
   unsigned used, cur, prev = 0, first = 0, left;

   if (nblocks == 0)
      return FAT_EMPTYFILE;

   used = nv_get(USEDSPACE_OFFSET);
   if (used + nblocks > NVM_DATABLOCKS)
      return NVM_ENOSPC;
   nv_put(USEDSPACE_OFFSET, (uint8_t)(used + nblocks));

   left = nblocks;
   for (cur = NVM_FIRSTDATABLOCK; left > 0; cur++)
   {
      if (cur >= NVM_TOTALBLOCKS)
         return NVM_ERANGE;         /* "this should NEVER happen" */
      if (nv_get(cur) != FAT_FREE)
         continue;
      if (prev == 0)
         first = cur;
      else
         nv_put(prev, (uint8_t)cur);
      prev = cur;
      left--;
   }
   nv_put(prev, FAT_END);
   return (int32_t)first;
}

/* FreeBlocks: walk the chain, marking entries free; stops at chain end
 * markers (<= FAT_EMPTYFILE) or when the used count hits zero. */
static void free_blocks(unsigned startblock)
{
   unsigned cur = startblock;
   unsigned used = nv_get(USEDSPACE_OFFSET);
   while (used > 0 && cur > FAT_EMPTYFILE)
   {
      unsigned next = nv_get(cur);
      nv_put(cur, FAT_FREE);
      cur = next;
      used--;
   }
   nv_put(USEDSPACE_OFFSET, (uint8_t)used);
}

/* file_offset: byte offset in a file -> logical NVRAM address, walking
 * the FAT chain.  Quirks kept: blocks 0/1 abort with ERANGE, but a
 * FAT_EMPTYFILE block passes through (the original's documented bug for
 * zero-length files). */
static int32_t file_offset(unsigned startblock, uint32_t off)
{
   unsigned cur = startblock;
   if ((int32_t)off < 0)
      return NVM_ERANGE;
   while (off >= NVM_BLOCKSIZE)
   {
      if (cur < FAT_EMPTYFILE)
         return NVM_ERANGE;
      cur = nv_get(cur);
      off -= NVM_BLOCKSIZE;
   }
   if (cur < FAT_EMPTYFILE)
      return NVM_ERANGE;
   return (int32_t)(cur * NVM_BLOCKSIZE + off);
}

/* FindFile: match app+file name against the directory; fills *d. */
static int32_t find_file(const uint16_t *app, const uint16_t *file,
                         nvm_dirent *d)
{
   unsigned n;
   for (n = 0; n < NVM_NUMDIRENTRIES; n++)
   {
      fetch_dir(n, d);
      if (d->startblock == 0)
         continue;
      if (memcmp(d->appname, app, sizeof(d->appname)) != 0)
         continue;
      if (memcmp(d->filename, file, sizeof(d->filename)) != 0)
         continue;
      return (int32_t)n;
   }
   return NVM_EFILNF;
}

static int32_t delete_file(const uint16_t *app, const uint16_t *file)
{
   nvm_dirent d;
   int32_t n = find_file(app, file, &d);
   if (n < 0)
      return n;
   nv_put(NVM_DIROFFSET + (uint32_t)n * NVM_DIRENTRYSIZE + 0, 0);
   nv_put(NVM_DIROFFSET + (uint32_t)n * NVM_DIRENTRYSIZE + 1, 0);
   free_blocks(d.startblock);
   return 0;
}

static int32_t get_handle(void)
{
   int i;
   for (i = 0; i < NVM_NUMFILES; i++)
      if (nvm_handles[i].startblock == 0)
         return i;
   return NVM_ENFILES;
}

/* --------------------------------------------------------------------
 * The eleven calls
 * -------------------------------------------------------------------- */

int32_t NVMInitialize(const char *appname)
{
   uint16_t stored;
   ascii2pack(appname, nvm_appname, NVM_APPPACKSIZE);
   memset(nvm_handles, 0, sizeof(nvm_handles));
   nvm_initialized = true;

   stored = (uint16_t)((nv_get(CHKSUM_OFFSET) << 8)
                       | nv_get(CHKSUM_OFFSET + 1));
   if (calc_chksum() != stored)
   {
      /* Bad checksum: zero FAT + directory (first 8 blocks).  510 zero
       * bytes sum to the stored zero checksum, so this is consistent. */
      uint32_t a;
      for (a = 0; a < (uint32_t)NVM_FIRSTDATABLOCK * NVM_BLOCKSIZE; a++)
         nv_put(a, 0);
   }
   nv_flush();
   return 0;
}

int32_t NVMCreate(const char *filename, int32_t size)
{
   uint16_t packed[NVM_FILEPACKSIZE];
   nvm_dirent d;
   int32_t handle, start;
   unsigned n, nblocks;

   if (!nvm_initialized)
      return NVM_ENOINIT;
   handle = get_handle();
   if (handle < 0)
      return handle;
   ascii2pack(filename, packed, NVM_FILEPACKSIZE);
   delete_file(nvm_appname, packed);

   for (n = 0; n < NVM_NUMDIRENTRIES; n++)
   {
      fetch_dir(n, &d);
      if (d.startblock == 0)
         break;
   }
   if (n == NVM_NUMDIRENTRIES)
      return NVM_ENOSPC;

   nblocks = ((uint32_t)size + NVM_BLOCKSIZE - 1) / NVM_BLOCKSIZE;
   start = alloc_blocks(nblocks);
   if (start < 0)
   {
      nv_flush();
      return NVM_ENOSPC;
   }
   d.startblock = (uint8_t)start;
   d.numblocks  = (uint8_t)nblocks;
   memcpy(d.appname, nvm_appname, sizeof(d.appname));
   memcpy(d.filename, packed, sizeof(d.filename));
   put_dir(n, &d);
   set_chksum();
   nv_flush();

   nvm_handles[handle].startblock = (uint16_t)start;
   nvm_handles[handle].offset     = 0;
   return handle;
}

int32_t NVMOpen(const char *filename)
{
   uint16_t packed[NVM_FILEPACKSIZE];
   nvm_dirent d;
   int32_t handle, n;

   if (!nvm_initialized)
      return NVM_ENOINIT;
   handle = get_handle();
   if (handle < 0)
      return handle;
   ascii2pack(filename, packed, NVM_FILEPACKSIZE);
   n = find_file(nvm_appname, packed, &d);
   if (n < 0)
      return n;
   nvm_handles[handle].startblock = d.startblock;
   nvm_handles[handle].offset     = 0;
   return handle;
}

int32_t NVMClose(int16_t handle)
{
   /* The original does no validation here either. */
   unsigned h = (uint16_t)handle;
   if (!nvm_initialized)
      return NVM_ENOINIT;
   if (h < NVM_NUMFILES)
   {
      nvm_handles[h].startblock = 0;
      nvm_handles[h].offset     = 0;
   }
   return 0;
}

int32_t NVMDelete(const char *appname_or_null, const char *filename)
{
   uint16_t app[NVM_APPPACKSIZE];
   uint16_t file[NVM_FILEPACKSIZE];

   if (!nvm_initialized)
      return NVM_ENOINIT;
   ascii2pack(filename, file, NVM_FILEPACKSIZE);
   if (appname_or_null)
      ascii2pack(appname_or_null, app, NVM_APPPACKSIZE);
   else
      memcpy(app, nvm_appname, sizeof(app));
   delete_file(app, file);
   set_chksum();
   nv_flush();
   /* The module clobbers D0 recalculating the checksum, so even a
    * missing file reports success on hardware. */
   return 0;
}

static int32_t nvm_io(int16_t handle, uint8_t *buf, const uint8_t *src,
                      int32_t count)
{
   unsigned h = (uint16_t)handle;
   nvm_handle *fh;
   int32_t done = 0;

   if (!nvm_initialized)
      return NVM_ENOINIT;
   if (h >= NVM_NUMFILES || nvm_handles[h].startblock == 0)
      return NVM_EIHNDL;
   fh = &nvm_handles[h];

   while (done < count)
   {
      int32_t addr = file_offset(fh->startblock, fh->offset);
      uint32_t left, chunk, i;
      if (addr < 0)
         break;                       /* partial transfer, like hardware */
      left = NVM_BLOCKSIZE - ((uint32_t)addr & (NVM_BLOCKSIZE - 1));
      chunk = (uint32_t)(count - done);
      if (chunk > left)
         chunk = left;
      for (i = 0; i < chunk; i++)
      {
         if (buf)
            buf[done + i] = nv_get((uint32_t)addr + i);
         else
            nv_put((uint32_t)addr + i, src[done + i]);
      }
      fh->offset += chunk;
      done += (int32_t)chunk;
   }
   if (!buf && done > 0)
      nv_flush();
   return done;
}

int32_t NVMReadFile(int16_t handle, uint8_t *buf, int32_t count)
{
   return nvm_io(handle, buf, NULL, count);
}

int32_t NVMWriteFile(int16_t handle, const uint8_t *buf, int32_t count)
{
   return nvm_io(handle, NULL, buf, count);
}

int32_t NVMSearchFirst(uint8_t sbuf[NVM_SEARCHBUFSIZE], int32_t flags)
{
   if (!nvm_initialized)
      return NVM_ENOINIT;
   if (flags != 0 && (flags & 0xFFFF) != 1)
      return NVM_EINVFN;
   nvm_search_flags    = (uint16_t)(flags & 0xFFFF);
   nvm_search_location = 0;
   return NVMSearchNext(sbuf);
}

int32_t NVMSearchNext(uint8_t sbuf[NVM_SEARCHBUFSIZE])
{
   nvm_dirent d;

   if (!nvm_initialized)
      return NVM_ENOINIT;
   while (nvm_search_location < NVM_NUMDIRENTRIES)
   {
      fetch_dir(nvm_search_location++, &d);
      if (d.startblock == 0)
         continue;
      if (nvm_search_flags
          && memcmp(d.appname, nvm_appname, sizeof(d.appname)) != 0)
         continue;

      {
         uint32_t size = (uint32_t)d.numblocks * NVM_BLOCKSIZE;
         char name[NVM_APPNAMELEN + 1];
         sbuf[0] = (uint8_t)(size >> 24);
         sbuf[1] = (uint8_t)(size >> 16);
         sbuf[2] = (uint8_t)(size >> 8);
         sbuf[3] = (uint8_t)size;
         pack2ascii(d.appname, name, NVM_APPPACKSIZE);
         memcpy(sbuf + 4, name, NVM_APPNAMELEN + 1);
         pack2ascii(d.filename, name, NVM_FILEPACKSIZE);
         memcpy(sbuf + 4 + NVM_APPNAMELEN + 1, name, NVM_FILENAMELEN + 1);
      }
      return 0;
   }
   return NVM_EFILNF;
}

int32_t NVMSeek(int16_t handle, int32_t offset, int16_t flag)
{
   unsigned h = (uint16_t)handle;
   nvm_handle *fh;
   int32_t target, addr;

   if (!nvm_initialized)
      return NVM_ENOINIT;
   if (h >= NVM_NUMFILES || nvm_handles[h].startblock == 0)
      return NVM_EIHNDL;
   fh = &nvm_handles[h];

   if (flag == 1)
      target = (int32_t)fh->offset + offset;
   else if (flag == 0)
      target = offset;
   else
      return NVM_EINVFN;

   addr = file_offset(fh->startblock, (uint32_t)target);
   if (addr < 0)
      return addr;
   fh->offset = (uint32_t)target;
   return target;
}

int32_t NVMInquire(uint32_t *total, uint32_t *freebytes)
{
   unsigned used;
   if (!nvm_initialized)
      return NVM_ENOINIT;
   used = nv_get(USEDSPACE_OFFSET);
   *total     = (uint32_t)NVM_DATABLOCKS * NVM_BLOCKSIZE;
   *freebytes = (uint32_t)(NVM_DATABLOCKS - used) * NVM_BLOCKSIZE;
   return 0;
}

/* --------------------------------------------------------------------
 * Emulator integration
 * -------------------------------------------------------------------- */

void NVMBiosInstall(void)
{
   /* '_NVM' cookie + RTS stub the dispatcher hook sits behind. */
   jaguarMainRAM[NVM_COOKIE_ADDR + 0] = (uint8_t)(NVM_COOKIE_MAGIC >> 24);
   jaguarMainRAM[NVM_COOKIE_ADDR + 1] = (uint8_t)(NVM_COOKIE_MAGIC >> 16);
   jaguarMainRAM[NVM_COOKIE_ADDR + 2] = (uint8_t)(NVM_COOKIE_MAGIC >> 8);
   jaguarMainRAM[NVM_COOKIE_ADDR + 3] = (uint8_t)NVM_COOKIE_MAGIC;
   jaguarMainRAM[NVM_DISPATCH_ADDR + 0] = 0x4E;   /* RTS */
   jaguarMainRAM[NVM_DISPATCH_ADDR + 1] = 0x75;
}

void NVMBiosReset(void)
{
   nvm_initialized = false;
   memset(nvm_appname, 0, sizeof(nvm_appname));
   memset(nvm_handles, 0, sizeof(nvm_handles));
   nvm_search_location = 0;
   nvm_search_flags = 0;
}

/* Read a NUL-terminated name out of emulated RAM (bounded). */
static void read_ram_string(uint32_t addr, char *dst, unsigned max)
{
   unsigned i;
   for (i = 0; i + 1 < max; i++)
   {
      uint8_t c = (addr + i < 0x200000) ? jaguarMainRAM[addr + i] : 0;
      dst[i] = (char)c;
      if (c == 0)
         return;
   }
   dst[i] = '\0';
}

bool NVMBiosHook(uint32_t pc)
{
   uint32_t sp;
   uint16_t opcode;
   int32_t ret = NVM_EINVFN;

   if (pc != NVM_DISPATCH_ADDR)
      return false;
   if (!jaguarMemTrackInserted)
      return false;
   /* Only dispatch while our stub is what executes here. */
   if (((uint32_t)jaguarMainRAM[NVM_COOKIE_ADDR] << 24 |
        (uint32_t)jaguarMainRAM[NVM_COOKIE_ADDR + 1] << 16 |
        (uint32_t)jaguarMainRAM[NVM_COOKIE_ADDR + 2] << 8 |
        (uint32_t)jaguarMainRAM[NVM_COOKIE_ADDR + 3]) != NVM_COOKIE_MAGIC)
      return false;

   /* Caller: JSR $2404 with args on the stack past the return address:
    *   4(sp).w opcode, then per-op args (see the spec / asmnvm.s). */
   sp = m68k_get_reg(NULL, M68K_REG_A7);
   opcode = (uint16_t)m68k_read_memory_16(sp + 4);

   switch (opcode)
   {
   case 0:  /* Initialize(appname.l, workarea.l) */
   {
      char app[64];
      read_ram_string(m68k_read_memory_32(sp + 6), app, sizeof(app));
      /* The 16K workarea is scratch the module may trash; ours doesn't. */
      ret = NVMInitialize(app);
      break;
   }
   case 1:  /* Create(filename.l, size.l) */
   {
      char name[64];
      read_ram_string(m68k_read_memory_32(sp + 6), name, sizeof(name));
      ret = NVMCreate(name, (int32_t)m68k_read_memory_32(sp + 10));
      break;
   }
   case 2:  /* Open(filename.l) */
   {
      char name[64];
      read_ram_string(m68k_read_memory_32(sp + 6), name, sizeof(name));
      ret = NVMOpen(name);
      break;
   }
   case 3:  /* Close(handle.w) */
      ret = NVMClose((int16_t)m68k_read_memory_16(sp + 6));
      break;
   case 4:  /* Delete(appname.l or 0, filename.l) */
   {
      char app[64], name[64];
      uint32_t appp = m68k_read_memory_32(sp + 6);
      read_ram_string(m68k_read_memory_32(sp + 10), name, sizeof(name));
      if (appp)
         read_ram_string(appp, app, sizeof(app));
      ret = NVMDelete(appp ? app : NULL, name);
      break;
   }
   case 5:  /* Read(handle.w, buf.l, count.l) */
   case 6:  /* Write(handle.w, buf.l, count.l) */
   {
      int16_t  handle = (int16_t)m68k_read_memory_16(sp + 6);
      uint32_t bufptr = m68k_read_memory_32(sp + 8);
      int32_t  count  = (int32_t)m68k_read_memory_32(sp + 12);
      uint8_t  chunk[NVM_BLOCKSIZE];
      int32_t  done = 0;

      ret = 0;
      while (done < count)
      {
         int32_t want = count - done;
         int32_t got;
         if (want > (int32_t)sizeof(chunk))
            want = (int32_t)sizeof(chunk);
         if (opcode == 6)
         {
            int32_t i;
            for (i = 0; i < want; i++)
               chunk[i] = (uint8_t)m68k_read_memory_8(bufptr + done + i);
            got = NVMWriteFile(handle, chunk, want);
         }
         else
         {
            int32_t i;
            got = NVMReadFile(handle, chunk, want);
            for (i = 0; i < got; i++)
               m68k_write_memory_8(bufptr + done + i, chunk[i]);
         }
         if (got < 0)
         {
            ret = got;             /* EIHNDL/ENOINIT from the first call */
            break;
         }
         done += got;
         if (got < want)
            break;                 /* partial (end of chain), stop */
      }
      if (ret >= 0)
         ret = done;
      break;
   }
   case 7:  /* SearchFirst(sbuf.l, flags.l) */
   case 8:  /* SearchNext(sbuf.l) */
   {
      uint32_t bufptr = m68k_read_memory_32(sp + 6);
      uint8_t  sbuf[NVM_SEARCHBUFSIZE];
      int32_t  i;
      if (opcode == 7)
         ret = NVMSearchFirst(sbuf, (int32_t)m68k_read_memory_32(sp + 10));
      else
         ret = NVMSearchNext(sbuf);
      if (ret == 0)
         for (i = 0; i < NVM_SEARCHBUFSIZE; i++)
            m68k_write_memory_8(bufptr + i, sbuf[i]);
      break;
   }
   case 9:  /* Seek(handle.w, offset.l, flag.w) */
      ret = NVMSeek((int16_t)m68k_read_memory_16(sp + 6),
                    (int32_t)m68k_read_memory_32(sp + 8),
                    (int16_t)m68k_read_memory_16(sp + 12));
      break;
   case 10: /* Inquire(total.l, free.l) */
   {
      uint32_t t = 0, f = 0;
      ret = NVMInquire(&t, &f);
      if (ret == 0)
      {
         m68k_write_memory_32(m68k_read_memory_32(sp + 6), t);
         m68k_write_memory_32(m68k_read_memory_32(sp + 10), f);
      }
      break;
   }
   default:
      ret = nvm_initialized ? NVM_EINVFN : NVM_ENOINIT;
      break;
   }

   m68k_set_reg(M68K_REG_D0, (uint32_t)ret);
   return true;   /* the RTS stub at $2404 now returns to the caller */
}

/* --------------------------------------------------------------------
 * Save states
 * -------------------------------------------------------------------- */

#include "state.h"

size_t NVMBiosStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   uint8_t init = nvm_initialized ? 1 : 0;
   int i;

   STATE_SAVE_VAR(buf, init);
   STATE_SAVE_BUF(buf, nvm_appname, sizeof(nvm_appname));
   for (i = 0; i < NVM_NUMFILES; i++)
   {
      STATE_SAVE_VAR(buf, nvm_handles[i].startblock);
      STATE_SAVE_VAR(buf, nvm_handles[i].offset);
   }
   STATE_SAVE_VAR(buf, nvm_search_location);
   STATE_SAVE_VAR(buf, nvm_search_flags);
   return (size_t)(buf - start);
}

size_t NVMBiosStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   uint8_t init = 0;
   int i;

   STATE_LOAD_VAR(buf, init);
   nvm_initialized = (init != 0);
   STATE_LOAD_BUF(buf, nvm_appname, sizeof(nvm_appname));
   for (i = 0; i < NVM_NUMFILES; i++)
   {
      STATE_LOAD_VAR(buf, nvm_handles[i].startblock);
      STATE_LOAD_VAR(buf, nvm_handles[i].offset);
   }
   STATE_LOAD_VAR(buf, nvm_search_location);
   STATE_LOAD_VAR(buf, nvm_search_flags);
   return (size_t)(buf - start);
}
