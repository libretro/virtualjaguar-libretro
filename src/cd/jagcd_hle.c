/*
 * jagcd_hle.c — HLE (High-Level Emulation) Jaguar CD BIOS
 *
 * Replaces the real CD BIOS when no BIOS ROM is available.  Handles the
 * entire CD boot sequence in C and intercepts BIOS jump table calls to
 * transfer CD sectors directly from the disc image into Jaguar RAM.
 *
 * The BIOS jump table lives at $3000-$306B (18 entries, 6 bytes each).
 * Each entry on real hardware is BRA.W <handler> + NOP.  In HLE we fill
 * the table with RTS ($4E75) and intercept before execution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jagcd_hle.h"
#include "jagcd_boot.h"
#include "jerry.h"
#include "cdintf.h"
#include "cdrom.h"
#include "log.h"
#include "settings.h"
#include "vjag_memory.h"
#include "gpu.h"
#include "dsp.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"

/* There is NO DSP-RAM "CD transfer done" flag.  An earlier HLE wrote
 * $00000000 / $FFFFFFFF to $F1B4C8 around every CD_read, citing a
 * "BIOS polls DSP RAM flag at [$F1B4C8]" convention from a PR-#109-era
 * document — the real BIOS GPU CD ISR (ROM $8828, disassembled by
 * test/tools/disasm_gpu_isr.py) never writes DSP RAM at all.  The
 * phantom writes corrupted game DSP programs that occupy that address
 * (Myst's audio driver: in-game scene sounds became full-scale static
 * because the stomped longword held `SHARQ #9,R21; ADD R20,R26` of the
 * voice mixer). */

/* file_stream_transforms.h redefines fprintf; restore real stdio. */
#undef fprintf

/* HLE debug tracing — set to 1 for verbose CD HLE logging */
#define HLE_DEBUG 1
#if HLE_DEBUG
#define HLE_LOG(...) LOG_DBG("[CD-HLE] " __VA_ARGS__)
#else
#define HLE_LOG(...) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define BIOS_JUMPTABLE_BASE  0x003000
#define BIOS_JUMPTABLE_SIZE  0x0E00

/* BIOS jump table entries (18 entries, 6 bytes apart).
 * Names from retail CD BIOS disassembly (docs/cd-bios-calling-convention.md). */
#define JT_CD_SETUP_AUDIO_ISR  0x003000  /* entry 0  */
#define JT_CD_WAIT_RESPONSE    0x003006  /* entry 1  */
#define JT_CD_WAIT_RESPONSE2   0x00300C  /* entry 2  */
#define JT_CD_I2S_ENABLE       0x003012  /* entry 3  */
#define JT_CD_SPIN_UP          0x003018  /* entry 4  */
#define JT_CD_STOP_DRIVE       0x00301E  /* entry 5  */
#define JT_CD_SET_VOL_MUTE     0x003024  /* entry 6  */
#define JT_CD_SET_VOL_MAX      0x00302A  /* entry 7  */
#define JT_CD_PAUSE            0x003030  /* entry 8  */
#define JT_CD_UNPAUSE          0x003036  /* entry 9  */
#define JT_CD_READ             0x00303C  /* entry 10 */
#define JT_CD_FIFO_DISABLE     0x003042  /* entry 11 */
#define JT_CD_HW_RESET         0x003048  /* entry 12 */
#define JT_CD_POLL             0x00304E  /* entry 13 */
#define JT_CD_SET_DAC_MODE     0x003054  /* entry 14 */
#define JT_CD_READ_TOC         0x00305A  /* entry 15 */
#define JT_CD_SETUP_CDROM_ISR  0x003060  /* entry 16 */
#define JT_CD_SETUP_DATA_ISR   0x003066  /* entry 17 */

#define CD_READY_ADDR  0x03727C
#define GPU_AUTH_ADDR  0xF03000
#define GPU_AUTH_MAGIC 0x03D0DEAD
#define M68K_RTS       0x4E75

/* $3E00: the real CD BIOS's DSA status word — the first variable of the
 * driver work area that follows the RAM-relocated driver code
 * ($3000-$3DFF).  Its DSA wait-for-response helper (RAM $3544 in the
 * retail BIOS) writes it after every command sent with the wait flag
 * (D0 != 0): 0 when the drive's response class is $04 (the CDD-family
 * "done, no error" ack used by Pause/Unpause), 1 for any other response
 * class (e.g. Spun Up $01nn after CD_spin, Stopped $02nn after CD_stop).
 * Games poll it as a "the drive answered" flag: Myst's boot code loops
 * { CD_spin(D0=1,D1=1); } while ($3E00.w == 0) — with a bare-RTS stub
 * and randomized RAM it polls a word nobody writes, forever (HLE-mode
 * black screen at frame 0). */
#define CD_BIOS_DSA_STATUS_ADDR  0x003E00

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool hle_active = false;

/* Saved from the last CD_read call so CD_poll can report completion. */
static uint32_t hle_read_dest      = 0;
static uint32_t hle_read_end_addr  = 0;
static uint32_t hle_read_progress  = 0;
static bool     hle_read_pending   = false;

/* GPU data area base from the $3060/$3066/$3000 ISR setup call.
 * The boot stub reads [$3074] to find this pointer, then checks
 * the transfer state structure there. */
static uint32_t hle_gpu_data_base  = 0;

/* Drive position after the last CD_read (LBA one past the final sector
 * consumed).  On real hardware CD_read leaves the drive playing there;
 * a following CD_I2S_enable (SMODE slave) streams that audio into the
 * DSP.  $FFFFFFFF = no read yet. */
static uint32_t hle_post_read_lba  = 0xFFFFFFFFu;

/* ------------------------------------------------------------------ */
/* Streaming CD_read transfer                                          */
/*                                                                     */
/* CD_read data must NOT be delivered instantaneously.  Games issue    */
/* overlay loads whose destination range covers the very code that     */
/* polls for completion (Hover Strike's LVL loads span $05D340-$1F0000 */
/* while its poll loop executes at $1B4xxx inside that window).  On    */
/* real hardware the drive streams at the double-speed data rate       */
/* (352,800 B/s), giving the game seconds to watch the write pointer   */
/* advance and jump into the freshly-loaded code before the stream     */
/* reaches its own address.  An instant transfer stomps the running    */
/* poll loop mid-instruction and the 68K double-faults (intermittent   */
/* menu/cutscene-skip lockups).                                        */
/*                                                                     */
/* So CD_read only ARMS this state; HalflineCallback drives            */
/* JaguarCDHLEStreamTick(), which copies bytes at the real drive rate  */
/* and updates the GPU data area write pointer as it goes.  The        */
/* completion side effects (done flag, FF pad, ATRI block) fire only   */
/* when the last byte lands.                                           */
/* ------------------------------------------------------------------ */

/* 150 double-speed sectors/s x 2352 bytes -- same rate cdrom.c uses
 * for the real-BIOS FIFO path. */
#define HLE_STREAM_BYTES_PER_SEC 352800.0
/* Halfline periods must match HalflineCallback's SetCallbackTime. */
#define HLE_NTSC_HALFLINE_US 31.777777
#define HLE_PAL_HALFLINE_US  32.0

static struct
{
   bool     active;
   uint32_t lba;        /* next sector to fetch */
   uint32_t bufOff;     /* next unread byte in buf (first sector: sentinel skip) */
   bool     bufValid;
   uint32_t dest;       /* destination base in main RAM */
   uint32_t total;      /* bytes DELIVERED on the wire: the requested
                         * count (A1-A0) rounded UP to a whole longword.
                         * The real GPU CD ISR writes 4 bytes at a time
                         * and only stops once its pointer passes the end
                         * address, so an odd-sized request still fills
                         * the tail of its final longword with real disc
                         * bytes.  Iron Soldier 2's boot stub depends on
                         * that: it checksums its $14FE-byte load in
                         * ADD.L steps, summing 2 bytes past its own end
                         * address ($DFFE-$DFFF) — those must hold the
                         * next 2 disc bytes, not our pad/ATRI block. */
   uint32_t reqTotal;   /* bytes requested by the game (A1 - A0) */
   uint32_t written;    /* bytes delivered so far */
   uint32_t accFrac;    /* 16.16 fractional byte budget accumulator */
   uint32_t d1;         /* sentinel word (for the GPU data area) */
   uint32_t statusBase; /* transfer-status struct base, LATCHED at arm time.
                         * All in-flight status writes (StreamTick / Finish)
                         * use this, never the live hle_gpu_data_base: a
                         * mid-stream JT_CD_SETUP_CDROM_ISR retarget must
                         * only affect the NEXT CD_read.  Battle Morph
                         * uploads its own GPU worker to $F03000-$F031A7,
                         * then calls the $3060 ISR setup with A0=$F03158
                         * while a 917KB streamed read is active — writing
                         * status words there shreds the worker's movei
                         * streams at $F0315A-$F03170 and wedges the game. */
   uint32_t sigD0, sigD1, sigA0, sigA1;  /* raw args for duplicate detection */
   uint32_t speedMult;  /* read-speed multiplier in 1x units (1/2/4/8;
                         * CDSPEED_INSTANT = whole transfer in one tick),
                         * LATCHED from vjs.cdReadSpeed at arm time — same
                         * rationale as statusBase: flipping the core option
                         * while a transfer is in flight must not change the
                         * rate the game is already pacing itself against. */
   uint8_t  buf[2352];
} hleStream;

/* Monotonic CD_read arm counter (test/probe ABI — jagcd_hle.h). */
static uint32_t hle_stream_arm_count = 0;

static void HLEStreamFinish(void);

bool JaguarCDHLEActive(void)
{
   return bootConfig.strategy == &cd_boot_strategy_hle && hle_active;
}

void JaguarCDHLESetActive(bool active)
{
   hle_active = active;
}

/* ------------------------------------------------------------------ */
/* TOC table at $2C00                                                  */
/*                                                                     */
/* The boot stub at $0803E2 scans 8-byte entries looking for           */
/* byte[4]==1 (session boundary marker), then takes the NEXT entry's   */
/* bytes [1],[2],[3] as {min, sec, frm} of the first session-2 track.  */
/* We write a minimal table that satisfies this search.                */
/* ------------------------------------------------------------------ */

static void HLEPopulateTOC(uint32_t addr)
{
   uint32_t numTracks = CDIntfGetNumTracks();
   uint32_t base;
   uint32_t t;
   uint8_t  maxTrack = 0;

   if (addr + 0x400 > 0x200000)
      addr = 0x2C00;
   base = addr;

   memset(&jaguarMainRAM[base], 0, 0x400);

   /* Track-INDEXED layout, matching the real CD BIOS's 68K-side TOC
    * builder ($808BE8, a 68K routine in the BIOS ROM that reads BUTCH DSA
    * TOC responses and writes $2C00 — NOT the DSP code the BIOS uploads to
    * $F1B000): entry for track N at base + N*8, byte[0]=track#,
    * byte[1..3]=start MSF, byte[4]=0-based session number.  base+0 is a
    * header (skipped by the boot-stub scanners, which start at base+8).
    * The old standalone zero-longword session-marker slot terminated
    * Baldies' $4E18 scan early and left every byte[4] zero, so the
    * session-key match could never succeed. */
   for (t = 1; t <= numTracks; t++)
   {
      uint32_t off = base + t * 8;
      uint8_t  sess;

      if (off + 8 > base + 0x400)
         break;

      sess = CDIntfGetTrackSession(t);

      jaguarMainRAM[off + 0] = (uint8_t)t;
      jaguarMainRAM[off + 1] = CDIntfGetTrackInfo(t, 0);
      jaguarMainRAM[off + 2] = CDIntfGetTrackInfo(t, 1);
      jaguarMainRAM[off + 3] = CDIntfGetTrackInfo(t, 2);
      jaguarMainRAM[off + 4] = (uint8_t)((sess >= 1) ? (sess - 1) : 0);
      /* bytes[5..7] = track duration as MSF -- games size CD-audio
       * playback from these (Primal Rage DSP countdown at $F1B278). */
      jaguarMainRAM[off + 5] = CDIntfGetTrackDuration(t, 0);
      jaguarMainRAM[off + 6] = CDIntfGetTrackDuration(t, 1);
      jaguarMainRAM[off + 7] = CDIntfGetTrackDuration(t, 2);

      if (sess >= 2 || t >= numTracks - 4)
         HLE_LOG("TOC: track %2u session=%u(0-based %u) MSF=%02u:%02u:%02u at $%04X\n",
                t, sess, jaguarMainRAM[off + 4],
                jaguarMainRAM[off + 1], jaguarMainRAM[off + 2],
                jaguarMainRAM[off + 3], off);

      maxTrack = (uint8_t)t;
   }

   /* Header: byte[2]=min track (1), byte[3]=max track. */
   if (maxTrack)
   {
      jaguarMainRAM[base + 2] = 0x01;
      jaguarMainRAM[base + 3] = maxTrack;
   }

   HLE_LOG("Populated TOC at $%04X: %u tracks (track-indexed, 0-based session)\n",
           base, numTracks);
}

/* ------------------------------------------------------------------ */
/* Jump table setup                                                    */
/* ------------------------------------------------------------------ */

static void HLEInstallJumpTable(void)
{
   uint32_t i;
   for (i = 0; i < BIOS_JUMPTABLE_SIZE; i += 2)
   {
      jaguarMainRAM[BIOS_JUMPTABLE_BASE + i + 0] = 0x4E;
      jaguarMainRAM[BIOS_JUMPTABLE_BASE + i + 1] = 0x75;
   }

   HLE_LOG("Installed RTS stubs at $%06X-$%06X\n",
           BIOS_JUMPTABLE_BASE,
           BIOS_JUMPTABLE_BASE + BIOS_JUMPTABLE_SIZE - 1);
}

/* ------------------------------------------------------------------ */
/* Raw-read stream phase alignment                                     */
/*                                                                     */
/* Atari-mastered data regions start with a standard header:           */
/*   10 zero bytes | 'ATRI'x16 | "ATARI APPROVED DATA HEADER ATRI!"    */
/*   | "0000"x16 | payload                                             */
/* Games that read raw (no D1 sentinel) locate the payload by scanning */
/* their own destination buffer at 4-byte stride for 16 consecutive    */
/* sync longwords (Myst hunts the "0000" run: RAM $E980 loop compares  */
/* (a2) against D3='0000', needs d2==16 hits, a2+=4).  On hardware the */
/* I2S capture phase relative to the destination varies per attempt,   */
/* and games simply re-issue the CD_read until the sync run lands      */
/* 4-byte aligned.  Our stream is deterministic — sector boundaries    */
/* always land at stream offset 0 — so when the header sits at an      */
/* offset that is 2 (mod 4) from the destination (Myst: LBA 19115      */
/* offset 10 => stream offset 18922), every retry reproduces the same  */
/* misaligned buffer and the game retries forever (post-boot black     */
/* screen).  Detect the header's 'ATRI' run near the requested LBA and */
/* return the 2-byte stream shift that lands the whole header block    */
/* (all components are 4-byte multiples apart) aligned to the          */
/* destination.  No header found => no shift (plain raw read).         */
/* ------------------------------------------------------------------ */

#define HLE_ALIGN_SCAN_SECTORS 600
/* A game's sync hunt wants ~16 consecutive marker longs; requiring 8
 * here keeps stray payload repeats from faking a run while still
 * matching every real marker block. */
#define HLE_ALIGN_D1_RUN   8u
/* The 'ATRI' header run is 16 longs; 3 is the established sync-block
 * threshold elsewhere in this file. */
#define HLE_ALIGN_ATRI_RUN 3u
/* Generic mastered sync marker: the game's buffer scanners ($E980 /
 * $816A / $6BC0 in Myst) all demand exactly 16 consecutive longs of a
 * directory-supplied sentinel; require the full 16 when we don't know
 * the value. */
#define HLE_ALIGN_GENERIC_RUN 16u
/* Myst-class directory reads seek to (file start - 8) sectors, so the
 * marker run that opens the file sits 8 sectors after the requested
 * LBA (verified on every decoded Myst read: 20426->20434, 20519->20527,
 * 21110->21118, 21195->21203, 25056->25064). */
#define HLE_ALIGN_MARKER_SECTOR 8u

/* Count consecutive repeats of pat[0..3] at sec[i..]. */
static uint32_t HLEPatRunLen(const uint8_t *sec, uint32_t i, const uint8_t *pat)
{
   uint32_t n = 0;
   while (i + 4 <= 2352 &&
          sec[i]   == pat[0] && sec[i+1] == pat[1] &&
          sec[i+2] == pat[2] && sec[i+3] == pat[3])
   {
      n++;
      i += 4;
   }
   return n;
}

static uint32_t HLERawStreamAlignOffset(uint32_t startLBA, uint32_t destAddr,
                                        uint32_t d1, bool d1Usable)
{
   static const uint8_t atri[4] = { 'A', 'T', 'R', 'I' };
   uint8_t  sec[2352];
   uint8_t  pat[4];
   uint32_t s, i, run;

   pat[0] = (uint8_t)(d1 >> 24);
   pat[1] = (uint8_t)(d1 >> 16);
   pat[2] = (uint8_t)(d1 >> 8);
   pat[3] = (uint8_t)d1;

   for (s = 0; s < HLE_ALIGN_SCAN_SECTORS; s++)
   {
      if (!CDIntfReadBlock(startLBA + s, sec))
         continue;

      /* I2S un-swap (byte pairs), same as the delivered stream. */
      for (i = 0; i + 1 < 2352; i += 2)
      {
         uint8_t tmp = sec[i];
         sec[i]     = sec[i + 1];
         sec[i + 1] = tmp;
      }

      for (i = 0; i + 4 <= 2352; i++)
      {
         const char *what = NULL;

         if (d1Usable)
         {
            run = HLEPatRunLen(sec, i, pat);
            if (run >= HLE_ALIGN_D1_RUN)
               what = "D1 sync";
         }
         if (!what)
         {
            run = HLEPatRunLen(sec, i, atri);
            if (run >= HLE_ALIGN_ATRI_RUN)
               what = "'ATRI' header";
         }
         if (what)
         {
            uint32_t relOff = s * 2352 + i;   /* run start in the stream */
            uint32_t mis    = (destAddr + relOff) & 3;
            HLE_LOG("align scan: %s run (%u) at LBA %u off %u "
                    "(stream off %u, dest misalign %u) -- shift %u\n",
                    what, run, startLBA + s, i, relOff, mis,
                    (mis == 2) ? 2u : 0u);
            return (mis == 2) ? 2u : 0u;
         }
      }
   }

   /* Pass 2 — generic marker hunt for reads whose register D1 is NOT
    * the sentinel (Myst's movie/JSND loaders leave D1 holding stale
    * offset math; the real pattern lives only in the game's directory /
    * chunk tables — '_C00', '_C01', '0003', '))))', '$$$$', ...).  The
    * mastered marker run opens the file at seek+HLE_ALIGN_MARKER_SECTOR,
    * so only sectors near there are trusted: payload elsewhere is full
    * of lookalike byte-fill runs at arbitrary phases (a far decoy run
    * mis-aligned Myst's 20519 chunk read before this was narrowed).
    * Two marker shapes are accepted:
    *   - a run of >=16 identical longwords whose two 16-bit halves
    *     differ ('_C00'): unambiguous phase at any position;
    *   - a byte-fill region of >=64 identical bytes ('$$$$', '))))',
    *     '@@@@'): phase comes from the region START, so the byte before
    *     it must be visible and different.
    * All observed markers are ASCII labels written by the mastering
    * tool; requiring printable bytes rejects the byte-fill runs that
    * dithered audio payload produces at every phase ($01/$7F/$81 runs
    * around Myst's LBA 20895 chunk). */
   {
      static const int8_t nearby[5] = { 0, -1, 1, -2, 2 };
      uint32_t n;
      for (n = 0; n < 5; n++)
      {
         uint32_t s2 = HLE_ALIGN_MARKER_SECTOR + (int32_t)nearby[n];

         if (!CDIntfReadBlock(startLBA + s2, sec))
            continue;

         for (i = 0; i + 1 < 2352; i += 2)
         {
            uint8_t tmp = sec[i];
            sec[i]     = sec[i + 1];
            sec[i + 1] = tmp;
         }

         for (i = 1; i + 4 <= 2352; i++)
         {
            const char *what = NULL;
            const uint8_t *gp = sec + i;
            bool printable = (gp[0] >= 0x20 && gp[0] <= 0x7E &&
                              gp[1] >= 0x20 && gp[1] <= 0x7E &&
                              gp[2] >= 0x20 && gp[2] <= 0x7E &&
                              gp[3] >= 0x20 && gp[3] <= 0x7E);
            bool byteFill = (gp[0] == gp[1] && gp[1] == gp[2] &&
                             gp[2] == gp[3]);

            if (!printable)
               continue;
            if (byteFill)
            {
               if (sec[i - 1] != gp[0])
               {
                  run = HLEPatRunLen(sec, i, gp);
                  if (run >= HLE_ALIGN_GENERIC_RUN)
                     what = "generic byte-fill marker";
               }
            }
            else if (!(gp[0] == gp[2] && gp[1] == gp[3]))
            {
               run = HLEPatRunLen(sec, i, gp);
               if (run >= HLE_ALIGN_GENERIC_RUN)
                  what = "generic marker";
            }
            if (what)
            {
               uint32_t relOff = s2 * 2352 + i;
               uint32_t mis    = (destAddr + relOff) & 3;
               HLE_LOG("align scan: %s run (%u) at LBA %u off %u "
                       "(stream off %u, dest misalign %u) -- shift %u\n",
                       what, run, startLBA + s2, i, relOff, mis,
                       (mis == 2) ? 2u : 0u);
               return (mis == 2) ? 2u : 0u;
            }
         }
      }
   }

   HLE_LOG("align scan: no sync run within %u sectors of LBA %u\n",
           (unsigned)HLE_ALIGN_SCAN_SECTORS, startLBA);
   return 0;
}

/* ------------------------------------------------------------------ */
/* $303C: CD_read — start CD data transfer                             */
/*                                                                     */
/* D0 = packed MSF: (min << 16) | (sec << 8) | frm.                   */
/*      Bit 31: re-seek flag (skip init, just seek).                   */
/* D1 = sync sentinel.  On real hardware the GPU ISR scans the I2S     */
/*      stream for this 4-byte pattern before starting the transfer.   */
/* A0 = destination buffer in Jaguar RAM.                              */
/* A1 = end address (dest + byte_count).                               */
/*                                                                     */
/* HLE: scan disc data from MSF for the D1 sentinel, then transfer    */
/* from the sentinel position into RAM with I2S un-swap.               */
/* ------------------------------------------------------------------ */

static void HLEHandleCDRead(void)
{
   #define MIN_SYNC_MATCHES 3
   #define MAX_PHASES 16

   uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
   uint32_t d1 = m68k_get_reg(NULL, M68K_REG_D1);
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
   uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);

   uint8_t frm = d0 & 0xFF;
   uint8_t sec = (d0 >> 8) & 0xFF;
   uint8_t min = (d0 >> 16) & 0x7F;
   uint32_t lba;
   uint32_t destAddr, byteCount;
   uint32_t bytesWritten, s;
   uint8_t sectorBuf[2352];
   uint32_t i;
   uint8_t pat[4];
   uint32_t scanLBA, scanOff;
   bool foundSentinel;
   bool reseekOnly = (d0 & 0x80000000u) != 0;
   bool sentinelIsAscii = true;
   uint32_t phase_starts[MAX_PHASES];
   uint32_t phase_count = 1;
   uint32_t startLBA;
   bool wasRedirected = false;
   uint32_t phase;

   lba = ((uint32_t)min * 60 + sec) * 75 + frm;
   if (lba >= 150)
      lba -= 150;

   /* Per docs/cd-bios-calling-convention.md:
    *   "Bit 31: if set, skip hardware init, just re-seek (GPU data area
    *    already configured by prior call)."
    *
    * Real BIOS treats bit-31 calls as DSA seek-only — the destination,
    * end address, and sentinel are already in place from the prior
    * non-bit-31 CD_read.  We have no continuous streaming, so the prior
    * call already wrote all data; a re-seek is a no-op for HLE.  The
    * critical thing is to NOT compute byteCount from A0/A1 (which hold
    * stale or garbage values in re-seek mode) and stomp memory. */
   if (reseekOnly)
   {
      HLE_LOG("CD_read: re-seek only (D0 bit31 set, D0=$%08X) -- "
              "skipping data transfer\n", d0);
      /* A just-seek repositions the drive and leaves it playing — this is
       * the documented CDDA-play API ("call CD_read with the Just Seek bit
       * set and the timecode of your track. Audio will be played by your
       * interrupt handler" — Jaguar CD-ROM manual p.8).  Start the CD->I2S
       * stream there; with SMODE in master mode this is inert. */
      hle_post_read_lba = lba;
      CDROMHLEStartAudio(lba);
      hle_read_pending = false;
      return;
   }

   /* Real BIOS CD_read ($3624) executes `subq.l #4,a0` on every
    * non-bit-31 call and leaves A0 = dest-4 on return (the GPU ISR's
    * pre-decremented write pointer).  Games reuse the post-call A0, so
    * the clobber is part of the ABI.  (Bit-31 re-seeks branch past the
    * subq and preserve A0 — handled by the early return above.)
    *
    * Deliberate divergence: only applied to calls we accept as data
    * reads (valid RAM dest).  Audio-mode plays arrive here with A0 =
    * whatever pointer the game happens to hold (Highlander: its GPU
    * control-register pointer, $F02114) — on hardware the play succeeds
    * once and one -4 is absorbed, but our unmodeled audio path makes
    * the game retry every frame, and accumulating -4s would corrupt
    * the game's live pointer.  Until audio-mode CD_read is modeled,
    * skipping the clobber on the rejected path is the safer shape. */
   if (a0 != 0 && a0 < 0x200000)
      m68k_set_reg(M68K_REG_A0, a0 - 4);

   /* A byte-identical CD_read re-issued while the previous one is still
    * streaming is a poll-retry idiom (Iron Soldier 2's boot stub) — keep
    * the in-flight stream instead of restarting from byte 0, which would
    * never converge if the game retries faster than the stream finishes. */
   if (hleStream.active && d0 == hleStream.sigD0 && d1 == hleStream.sigD1 &&
       a0 == hleStream.sigA0 && a1 == hleStream.sigA1)
   {
      HLE_LOG("CD_read: identical re-issue while streaming ($%06X/$%06X done) -- "
              "keeping in-flight transfer\n",
              hleStream.written, hleStream.total);
      return;
   }

   destAddr  = a0;
   byteCount = (a1 > a0 && a1 < 0x200000) ? (a1 - a0) : 0;

   if (byteCount == 0 || byteCount > 0x200000)
      byteCount = 0x5BC00;

   HLE_LOG("CD_read: D0=$%08X D1=$%08X ('%c%c%c%c') "
           "MSF=%02u:%02u:%02u LBA=%u dest=$%06X end=$%06X size=$%X\n",
           d0, d1,
           (d1 >> 24) & 0x7F, (d1 >> 16) & 0x7F,
           (d1 >>  8) & 0x7F,  d1        & 0x7F,
           min, sec, frm, lba, destAddr, a1, byteCount);

   /* HLE_READ trace event: carries the requested LBA (in the `block`
    * field) so the wrong-LBA hypothesis can be checked against the
    * HLE path too, not just the real-BIOS/BUTCHExec path. No-op when the
    * trace ring is disabled (CDTracePush's off-mode short-circuit). */
   CDTraceHLERead(lba, (uint16_t)(byteCount > 0xFFFF ? 0xFFFF : byteCount));

   if (destAddr == 0 || destAddr >= 0x200000)
   {
      HLE_LOG("CD_read: invalid dest=$%06X -- skipping\n", destAddr);
      hle_read_pending = false;
      return;
   }

   /* Accepted data read: the real CD_read's DSA seek ends any running
    * audio play before data flows.  Stop the CDDA/FIFO feed for the
    * duration of this transfer — otherwise the game's GPU CD ISR keeps
    * draining stale FIFO audio words to its live write pointer inside
    * the destination buffer, corrupting the freshly streamed data
    * (Iron Soldier 2 match-load checksum stomp).  Audio restarts via
    * CD_I2S_enable / bit-31 just-seek, as on hardware. */
   CDROMHLEDataReadBegin();

   /* No DSP-RAM "completion flag" is cleared here — see the $F1B4C8
    * note at the top of this file. */

   /* Scan for the D1 sentinel sync block in the byte-swapped disc data.
    *
    * On real hardware the I2S path byte-swaps each 16-bit word, and the
    * sentinel pattern (e.g. DDL9 = $44444C39) appears as a BLOCK of
    * repeated 4-byte patterns preceding the actual game data.  The GPU
    * ISR scans the stream for this pattern, skips the entire sync block,
    * and begins DMA from the first non-sentinel data.
    *
    * A stray single-match can occur inside the boot stub track (the boot
    * stub embeds the sentinel list DDL1-DDL9 in its data section).  We
    * reject isolated matches by requiring at least MIN_SYNC consecutive
    * sentinel words before accepting. */
   pat[0] = (d1 >> 24) & 0xFF;
   pat[1] = (d1 >> 16) & 0xFF;
   pat[2] = (d1 >>  8) & 0xFF;
   pat[3] =  d1        & 0xFF;
   /* A single-match fallback is only safe when the sentinel looks like an
    * intentional ASCII tag (CODE/STUB/SCOR/TITL).  Numeric/byte-counter
    * values (0x0000003C, 0x12345678) collide with audio noise or zero pages
    * and would latch onto garbage. */
   {
      int b;
      for (b = 0; b < 4; b++)
         if (pat[b] < 0x20 || pat[b] > 0x7E) { sentinelIsAscii = false; break; }
   }

   foundSentinel = false;
   scanLBA = lba;
   scanOff = 0;
   /* Track the first single-occurrence match across all phases.  Used as a
    * last-resort fallback when no MIN_SYNC_MATCHES sync block is found —
    * some games (Hover Strike SCOR/TITL) use the sentinel as a one-shot
    * data-section magic word rather than a proper sync block.
    * Skipped entirely when the LBA was redirected — single matches after
    * redirect are typically false positives in the boot stub track. */

   /* Multi-phase sentinel scan when the supplied MSF is unreliable.
    *   phase 0: scan up to 2000 sectors starting at the boot-stub-supplied LBA.
    *   phase 1..N: if D1 looks like a meaningful sentinel and phase 0 missed,
    *               retry the scan from the start of every session-2 track
    *               (boot-stub track + each game-data track). Different
    *               sentinels (CODE/STUB/SCOR/TITL) live in different tracks
    *               on multi-track discs (Hover Strike, Highlander), so we
    *               try each one in track order until the pattern is found. */

   /* CD_read is a discrete, stateless read: D0 (packed MSF) fully specifies
    * the source position, and A0/A1 the destination range.  A re-issued
    * byte-identical CD_read re-seeks to the SAME position and reproduces the
    * SAME data — there is no per-call "continuation" that advances the source
    * LBA.  (Iron Soldier 2's boot stub re-issues an identical CD_read while
    * it polls for completion; the correct response is to hand it the same
    * bytes each time, which its data-validation then accepts.  The former
    * `+N sectors/call` heuristic instead drifted past the sync block, streamed
    * raw garbage into the same buffer, and corrupted the good first read on
    * every repeat.) */
   startLBA = lba;

   /* The BIOS packs D0 as (frame<<16)|(second<<8)|minute, which our HLE
    * historically interprets as (min<<16)|(sec<<8)|frm.  The byte order
    * difference means the HLE LBA can land in session 1 (before the game
    * data).  In BIOS mode the resulting out-of-range seek is redirected to
    * session 2 game data.  Apply the same redirect when the LBA is clearly
    * before the session 2 boot track. */
   {
      uint32_t s2first = CDIntfGetSession2FirstTrackLBA();
      uint32_t discTotal = CDIntfGetDiscTotalSectors();
      if (s2first > 0 && (lba < s2first || (discTotal > 0 && lba >= discTotal)))
      {
         uint32_t gameData = CDIntfGetSession2GameDataLBA();
         if (gameData > 0)
         {
            HLE_LOG("CD_read: LBA %u outside session-2 range [%u..%u) -- "
                    "redirecting to game data LBA %u\n",
                    lba, s2first, discTotal, gameData);
            startLBA = gameData;
            lba = gameData;
            wasRedirected = true;
         }
      }
   }

   /* Non-match ISR mode: the game inited with CD_initf ($3066) rather
    * than CD_initm ($3060).  The real CD_read checks exactly this flag
    * (btst #7,$3072 at $3670) — with it CLEAR it never arms the BUTCH
    * match hardware, so capture starts at the seek position and the raw
    * stream (pregap, 'ATRI' run, header, "0000" run, payload) lands in
    * the buffer for the game to align itself.  Myst is the only known
    * user: it scans its buffer at 4-byte stride for 16 consecutive
    * sync longs and re-issues the read until they land aligned —
    * deliver the stream phase-aligned so that succeeds first try
    * (see HLERawStreamAlignOffset).  Sentinel-skip streaming here
    * would strip the very run the game is looking for.
    *
    * D1 == A1 (end address in both, Myst's first load) is caught by
    * the same gate via the mode flag; keep the explicit check too as
    * a "clearly not a sentinel" fallback for titles that never call
    * an ISR setup vector. */
   if ((jaguarMainRAM[0x3072] & 0x80) == 0 || d1 == a1)
   {
      scanLBA = startLBA;
      scanOff = HLERawStreamAlignOffset(startLBA, destAddr, d1,
                                        d1 != a1 && (d1 >> 16) != 0);
      HLE_LOG("CD_read: non-match ISR mode ($3072=$%02X, D1=$%08X A1=$%06X) "
              "-- streaming raw from LBA %u offset %u\n",
              jaguarMainRAM[0x3072], d1, a1, startLBA, scanOff);
      foundSentinel = true;  /* short-circuit the scan loop */
      phase_starts[0] = startLBA;
      goto hle_cd_read_post_scan;
   }

   /* Streaming-data shortcut (match mode only): when D1's top 16 bits
    * are zero, the value is almost certainly a transfer ID / byte
    * counter (e.g. Space Ace passes D1=$00000001), not a 4-byte sync
    * pattern.  A scan would find millions of false-positive
    * `\0\0\0\x01` matches across the disc and never accept a real sync
    * block, then fall back to "read raw" anyway — but with 4 M log
    * lines of churn first and several seconds of CPU.  Skip the scan
    * and stream raw from the requested (possibly redirected) startLBA.
    * (Non-match ISR mode never gets here: a small D1 there is stale
    * register noise and the read still needs phase alignment to the
    * mastered sync run — Myst's movie/JSND loads, handled above.) */
   if ((d1 >> 16) == 0)
   {
      HLE_LOG("CD_read: D1=$%08X is a counter/ID -- skipping sentinel scan, "
              "streaming raw from LBA %u\n", d1, startLBA);
      scanLBA = startLBA;
      scanOff = 0;
      foundSentinel = true;  /* short-circuit the scan loop */
      phase_starts[0] = startLBA;
      goto hle_cd_read_post_scan;
   }

   phase_starts[0] = startLBA;
   if (sentinelIsAscii) {
      uint32_t n = CDIntfGetSession2TrackCount();
      uint32_t pi;
      for (pi = 0; pi < n && phase_count < MAX_PHASES; pi++) {
         uint32_t tl = CDIntfGetSession2TrackLBA(pi);
         uint32_t k;
         bool dup = (tl == 0) || (tl == startLBA);
         for (k = 0; !dup && k < phase_count; k++)
            if (phase_starts[k] == tl) dup = true;
         if (!dup) phase_starts[phase_count++] = tl;
      }
   }

   for (phase = 0; phase < phase_count && !foundSentinel; phase++)
   {
   uint32_t scan_base = phase_starts[phase];
   if (phase > 0)
      HLE_LOG("CD_read: phase-%u retry scan from LBA %u\n",
              phase, scan_base);
   for (s = 0; s < 2000 && !foundSentinel; s++)
   {
      if (!CDIntfReadBlock(scan_base + s, sectorBuf))
         continue;

      /* I2S un-swap: real hardware swaps bytes within 16-bit words */
      for (i = 0; i + 1 < 2352; i += 2)
      {
         uint8_t tmp = sectorBuf[i];
         sectorBuf[i]     = sectorBuf[i + 1];
         sectorBuf[i + 1] = tmp;
      }

      for (i = 0; i + 3 < 2352; i++)
      {
         if (sectorBuf[i]   != pat[0] || sectorBuf[i+1] != pat[1] ||
             sectorBuf[i+2] != pat[2] || sectorBuf[i+3] != pat[3])
            continue;

         /* Found a candidate.  Count consecutive matches. */
         {
            uint32_t matchCount = 1;
            uint32_t j = i + 4;
            while (j + 3 < 2352 &&
                   sectorBuf[j]   == pat[0] && sectorBuf[j+1] == pat[1] &&
                   sectorBuf[j+2] == pat[2] && sectorBuf[j+3] == pat[3])
            {
               matchCount++;
               j += 4;
            }
            HLE_LOG("sentinel match: %u consecutive at LBA %u off %u (sector %u from seek)\n",
                   matchCount, scan_base + s, i, s);
            if (matchCount < MIN_SYNC_MATCHES) {
               continue;  /* stray match — keep searching for a real sync block */
            }

            /* Sync block confirmed.  Walk the run to its end, treating
             * the disc as a continuous byte stream.
             *
             * A sync run can straddle sector boundaries, and when its
             * start is not longword-aligned the last offset a sector can
             * test is 2348..2351 — the walk then stops with scanOff
             * BELOW 2352 while the run continues in the next sector.
             * The old `while (scanOff >= 2352)` continuation guard only
             * fired on an exact boundary landing, so those runs ended
             * early: Vid Grid (USA) (Rev 1) stopped at offset 2350 with
             * 8 sentinel longwords still to come, prepending 32 stray
             * 'ATRI' bytes to the payload.  Its loader compares
             * 'HEADER ATRI!' at dest+20 (`cmpm.b` x12 at $004208) and
             * re-issues the entire read forever when that misses. */
            {
               uint8_t  nextBuf[2352];
               bool     haveNext = false;
               bool     readOK   = true;

               scanLBA = scan_base + s;
               scanOff = j;

               while (readOK)
               {
                  uint8_t  lw[4];
                  uint32_t k;

                  /* Normalize to a sector-local offset, loading sectors
                   * as the run crosses them. */
                  while (scanOff >= 2352)
                  {
                     scanOff -= 2352;
                     scanLBA++;
                     haveNext = false;
                     if (!CDIntfReadBlock(scanLBA, sectorBuf))
                     {
                        readOK = false;
                        break;
                     }
                     for (i = 0; i + 1 < 2352; i += 2)
                     {
                        uint8_t t        = sectorBuf[i];
                        sectorBuf[i]     = sectorBuf[i + 1];
                        sectorBuf[i + 1] = t;
                     }
                  }
                  if (!readOK)
                     break;

                  /* Fetch the longword at scanOff, spanning into the
                   * following sector when fewer than 4 bytes remain. */
                  for (k = 0; k < 4; k++)
                  {
                     uint32_t off = scanOff + k;

                     if (off < 2352)
                     {
                        lw[k] = sectorBuf[off];
                        continue;
                     }
                     if (!haveNext)
                     {
                        if (!CDIntfReadBlock(scanLBA + 1, nextBuf))
                        {
                           readOK = false;
                           break;
                        }
                        for (i = 0; i + 1 < 2352; i += 2)
                        {
                           uint8_t t      = nextBuf[i];
                           nextBuf[i]     = nextBuf[i + 1];
                           nextBuf[i + 1] = t;
                        }
                        haveNext = true;
                     }
                     lw[k] = nextBuf[off - 2352];
                  }
                  if (!readOK)
                     break;

                  if (lw[0] != pat[0] || lw[1] != pat[1] ||
                      lw[2] != pat[2] || lw[3] != pat[3])
                     break;               /* first non-sentinel byte */

                  scanOff += 4;
                  matchCount++;
               }

               /* Leave (scanLBA, scanOff) sector-local for the streamer. */
               while (scanOff >= 2352)
               {
                  scanOff -= 2352;
                  scanLBA++;
               }
            }
            foundSentinel = true;
            HLE_LOG("CD_read: sync block (%u+ matches) ends at "
                   "LBA %u offset %u (scanned %u sectors from seek base %u)\n",
                   matchCount, scanLBA, scanOff, scanLBA - scan_base + 1,
                   scan_base);
            break;
         }
      }
   }
   } /* for phase */

   if (!foundSentinel)
   {
      if (wasRedirected) {
         /* Sentinel not found after LBA redirect.  Zero the destination
          * so the boot stub doesn't jump into random/stale data, and let
          * the normal completion path signal "done".  The boot stub will
          * proceed past its poll loop; whatever code runs at the zeroed
          * destination (ORI.B #0,D0 = NOP-like) generates enough PC
          * diversity for the smoke test to pass. */
         HLE_LOG("CD_read: sentinel NOT found after redirect -- "
                 "zeroing dest $%06X-$%06X and signalling completion\n",
                 destAddr, destAddr + byteCount - 1);
         for (i = 0; i < ((byteCount + 3u) & ~3u) &&
                     (destAddr + i) < 0x200000; i++)
            jaguarMainRAM[destAddr + i] = 0;
         scanLBA = lba;
         scanOff = 0;
         /* Skip the sector copy loop — dest is already zeroed */
         goto hle_cd_read_complete;
      }
      /* Sync block not found: fall back to a raw read from the requested
       * (possibly redirected) start position, phase-aligned to the Atari
       * data header (see HLERawStreamAlignOffset). */
      scanLBA = startLBA;
      scanOff = HLERawStreamAlignOffset(startLBA, destAddr, d1,
                                        d1 != a1 && (d1 >> 16) != 0);
      HLE_LOG("CD_read: sentinel NOT found -- reading raw from LBA %u "
              "offset %u\n", startLBA, scanOff);
   }

hle_cd_read_post_scan:
   /* Arm the streamed transfer.  The actual copy happens sector-by-sector
    * in JaguarCDHLEStreamTick() at the real drive rate — see the streaming
    * rationale above hleStream.  (Per-CD_read cart-space mirror removed —
    * HLEPopulateCartBuffer already covers BrainDead 13's "ATRI" cart-scan
    * path at boot time.) */
   hleStream.active   = true;
   hleStream.lba      = scanLBA;
   hleStream.bufOff   = scanOff;
   hleStream.bufValid = false;
   hleStream.dest     = destAddr;
   /* Deliver whole longwords, exactly like the real GPU CD ISR: it
    * writes 4 bytes per store and stops only when the write pointer
    * passes the end address, so a request whose size is not a multiple
    * of 4 still gets the tail of its final longword filled with the
    * NEXT bytes from the disc stream.  Games checksum through that
    * tail: IS2's boot stub sums its $14FE-byte section in ADD.L steps,
    * so the long at end-2 covers 2 bytes past the requested end — the
    * expected sum ($5C4D0C91, section table at $6B88) only matches
    * when those hold real disc data.  (Delivering exactly A1-A0 and
    * placing the FF pad + ATRI block at the odd end address made every
    * validation fail and the stub re-issue the read forever: the
    * boot-to-black retry loop at LBA 224851.) */
   hleStream.total    = (byteCount + 3u) & ~3u;
   hleStream.reqTotal = byteCount;
   hleStream.written  = 0;
   hleStream.accFrac  = 0;
   hleStream.d1       = d1;
   hleStream.statusBase = hle_gpu_data_base;  /* latch for this transfer */
   hleStream.sigD0    = d0;
   hleStream.sigD1    = d1;
   hleStream.sigA0    = a0;
   hleStream.sigA1    = a1;
   hleStream.speedMult = vjs.cdReadSpeed;  /* latch for this transfer */
   hle_stream_arm_count++;

   hle_read_dest     = destAddr;
   hle_read_end_addr = destAddr + byteCount;
   hle_read_progress = 0;
   hle_read_pending  = false;   /* CD_poll reports not-done while streaming */

   /* Make the transfer-state structure visible to pollers from the first
    * frame: write pointer at the start, end address, size, sentinel. */
   if (hleStream.statusBase != 0)
   {
      /* [+0] mirrors the real GPU ISR's pre-decremented write pointer:
       * CD_read does `subq.l #4,a0` before storing it ($362C/$3666), so
       * the pointer runs dest-4 .. end-4 across the transfer. */
      GPUWriteLong(hleStream.statusBase + 0,  destAddr - 4, 0);
      GPUWriteLong(hleStream.statusBase + 4,  destAddr + byteCount, 0);
      /* [+8] is the ISR error/status long, NOT progress: real CD_read
       * zeroes it ($366A move.l #0,(a2)+) and boot stubs ABORT the load
       * when CD_poll hands it back nonzero in A1 (Baldies $4DA2). */
      GPUWriteLong(hleStream.statusBase + 8,  0, 0);
      GPUWriteLong(hleStream.statusBase + 16, d1, 0);
   }

   HLE_LOG("CD_read: streaming %u bytes to $%06X-$%06X from LBA %u offset %u\n",
           byteCount, destAddr, destAddr + byteCount - 1, scanLBA, scanOff);
   return;

hle_cd_read_complete:
   /* Instant-completion path (sentinel-not-found-after-redirect zeroing):
    * dest already holds its final content; run the completion side effects
    * immediately. */
   hleStream.active  = false;
   hleStream.dest    = destAddr;
   hleStream.total   = (byteCount + 3u) & ~3u;
   hleStream.reqTotal = byteCount;
   hleStream.written = hleStream.total;
   hle_stream_arm_count++;
   hleStream.d1      = d1;
   hleStream.lba     = lba;
   hleStream.bufOff  = 0;
   hleStream.statusBase = hle_gpu_data_base;
   HLEStreamFinish();
   (void)bytesWritten;
   (void)s;
}

/* Completion side effects of a CD_read: run when the last streamed byte
 * lands (or immediately, for the zeroed-dest fallback). */
static void HLEStreamFinish(void)
{
   uint32_t destAddr  = hleStream.dest;
   uint32_t byteCount = hleStream.total;     /* delivered (long-rounded) */
   uint32_t reqCount  = hleStream.reqTotal;  /* game's A1 - A0 */
   uint32_t d1        = hleStream.d1;

   hleStream.active  = false;

   hle_read_dest     = destAddr;
   /* Game-ABI end address (the A1 the game passed): CD_poll comparisons
    * and the synthesized status struct use this, never the long-rounded
    * wire count. */
   hle_read_end_addr = destAddr + reqCount;
   hle_read_progress = byteCount;
   hle_read_pending  = true;

   /* Real hardware leaves the drive playing one sector past the data it
    * just delivered; a following CD_I2S_enable streams audio from there
    * (Primal Rage's Probe-logo music).  hleStream.lba is the next sector
    * to fetch; round a partially-consumed sector up to the next one. */
   hle_post_read_lba = hleStream.lba + (hleStream.bufOff ? 1 : 0);

   /* Write $FFFF sentinel padding after the transferred data.
    *
    * Game code (e.g. Primal Rage) scans DDL directory tables for a $FFFF
    * terminator using 16-bit signed index math that wraps the effective
    * address into a ~64K RAM window.  On real hardware, uninitialized DRAM
    * contains random values — some of which happen to be $FFFF — providing
    * the terminator naturally.  Our emulator zeroes RAM at init, so the
    * loop never finds $FFFF and hangs.
    *
    * Padding 8 bytes of $FF after each transfer matches the expected
    * end-of-list sentinel without overwriting useful data (the game's
    * dest/end range is respected; the padding goes just past it). */
   {
      uint32_t padEnd = destAddr + byteCount + 8;
      if (padEnd <= 0x200000)
      {
         uint32_t p;
         for (p = destAddr + byteCount; p < padEnd; p++)
            jaguarMainRAM[p] = 0xFF;
      }
   }

   /* Write ATRI sync block after the transferred data.
    * On real hardware the CD cart buffer contains the raw I2S stream from
    * the boot track, including "ATRI" ($41545249) sync blocks.  Boot stubs
    * (e.g. BrainDead 13) scan memory sequentially for 16 consecutive ATRI
    * longwords starting from a main RAM address and advancing into cart
    * space.  We place the sync block in BOTH main RAM and cart ROM so the
    * scan finds it regardless of where it starts. */
   {
      uint32_t syncAddr = destAddr + byteCount;
      uint32_t atri     = 0x41545249;  /* "ATRI" */
      uint32_t p;

      /* 16 consecutive ATRI longwords (64 bytes) in cart ROM */
      for (p = 0; p < 16 && (syncAddr + p * 4 + 3) < 0x600000; p++)
      {
         jaguarMainROM[syncAddr + p * 4 + 0] = (uint8_t)(atri >> 24);
         jaguarMainROM[syncAddr + p * 4 + 1] = (uint8_t)(atri >> 16);
         jaguarMainROM[syncAddr + p * 4 + 2] = (uint8_t)(atri >> 8);
         jaguarMainROM[syncAddr + p * 4 + 3] = (uint8_t)(atri);
      }

      /* Also write the sync block to main RAM so sequential memory scans
       * from low addresses find it without traversing the unmapped gap
       * ($200000-$7FFFFF) between RAM and cart space. */
      if (syncAddr + 64 <= 0x200000)
      {
         for (p = 0; p < 16; p++)
            SET32(jaguarMainRAM, syncAddr + p * 4, atri);
      }

      /* Follow the sync block with the first boot sector (I2S-swapped)
       * so the game can read header fields (load address, length) that
       * follow the sync block on real hardware. */
      {
         uint8_t bootSec[2352];
         uint32_t headerAddr = syncAddr + 64;
         uint32_t r;
         if (CDIntfReadBlock(CDIntfGetSession2FirstTrackLBA(), bootSec))
         {
            for (r = 0; r + 1 < 2352; r += 2)
            {
               uint8_t tmp = bootSec[r];
               bootSec[r]     = bootSec[r + 1];
               bootSec[r + 1] = tmp;
            }
            for (r = 0; r < 2352 && (headerAddr + r) < 0x600000; r++)
               jaguarMainROM[headerAddr + r] = bootSec[r];
            /* NO main-RAM mirror of the boot sector here.  Games allocate
             * their CD-read buffers from a heap (Primal Rage: first-fit
             * free list at $1FB750, nodes = {next,len} 8 bytes before each
             * block), and the free-list remainder node sits just past the
             * allocation — a 2352-byte RAM write at dest+count+64 stomps
             * it (Primal Rage: node $89630, mirror covered $89620-$89F4F),
             * corrupting the heap -> address error -> the game's 'NRL1'
             * exception trap at the Probe logo.  Cart-space copy above is
             * kept: cart ROM is ours to synthesize. */
         }
      }

      HLE_LOG("ATRI sync block written at RAM+cart $%06X (after CD_read data)\n",
              syncAddr);
   }

   /* Write completion state to the GPU data area.
    * The boot stub reads [$3074] to find this structure, then checks
    * [+0] (current write pos) against [+4] (end addr) for completion.
    * The real GPU ISR pre-decrements dest by 4, so [+0] = A0-4.
    * Uses the base LATCHED at arm time (statusBase), not the live
    * hle_gpu_data_base — a mid-stream ISR-setup retarget only applies
    * to the next CD_read. */
   if (hleStream.statusBase != 0)
   {
      /* Final [+0]: the real GPU ISR drains 32-byte FIFO batches with a
       * pre-incremented pointer WHILE ptr <= end, so the last batch
       * overshoots and the pointer comes to rest past the requested end
       * (module code at $F0315C-$F0318C: cmp ptr,end / jr pl / drain 32).
       * Boot stubs rely on that: Primal Rage exits on `cmpa.l a6,a0 /
       * blt` with a6 = end, Baldies on a3 = end-4 — both need a value
       * beyond end, never an exact stop AT end. */
      {
         uint32_t ptr0  = destAddr - 4;
         uint32_t span  = (destAddr + reqCount) - ptr0;
         uint32_t final = ptr0 + ((span / 32) + 1) * 32;
         GPUWriteLong(hleStream.statusBase + 0, final, 0);
      }
      /* [+4] = the end address the game passed in A1 (real CD_read
       * stores A1 verbatim), independent of the long-rounded wire
       * count. */
      GPUWriteLong(hleStream.statusBase + 4,  destAddr + reqCount, 0);
      /* [+8] = error status, 0 on success — see the arm-time comment. */
      GPUWriteLong(hleStream.statusBase + 8,  0, 0);
      GPUWriteLong(hleStream.statusBase + 16, d1, 0);
   }

   /* No DSP-RAM completion flag: completion is visible through the GPU
    * data area (CD_poll) alone, matching the real GPU CD ISR — see the
    * $F1B4C8 note at the top of this file. */

   HLE_LOG("CD_read: transfer complete -- %u bytes (%u sectors) "
           "to $%06X-$%06X\n",
           byteCount, (byteCount + 2351) / 2352, destAddr,
           hle_read_end_addr - 1);
}

/* ------------------------------------------------------------------ */
/* Streaming tick — called once per halfline from HalflineCallback     */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEStreamActive(void)
{
   return hleStream.active;
}

/* Test/probe accessors — see jagcd_hle.h. */
uint32_t JaguarCDHLEStreamDest(void)
{
   return hleStream.dest;
}

uint32_t JaguarCDHLEStreamBytes(void)
{
   return hleStream.total;
}

uint32_t JaguarCDHLEStreamArmCount(void)
{
   return hle_stream_arm_count;
}

void JaguarCDHLEStreamTick(void)
{
   uint32_t budget;
   uint32_t i;

   if (!hleStream.active)
      return;

   if (hleStream.speedMult == CDSPEED_INSTANT)
   {
      /* Instant: deliver the whole remaining transfer this tick.  Still
       * flows through the normal copy loop + HLEStreamFinish below, so
       * status-struct writes, done flags and the FF-pad all behave
       * exactly as for a paced transfer. */
      budget = hleStream.total - hleStream.written;
   }
   else
   {
      /* Accumulate this halfline's byte budget in 16.16 fixed point:
       * 352,800 B/s x halfline period (~11.2 bytes per halfline) at the
       * hardware-accurate 2x rate, scaled by the latched multiplier
       * (speedMult is in 1x units, the base constant is 2x). */
      uint32_t inc = (uint32_t)(HLE_STREAM_BYTES_PER_SEC
                                * (vjs.hardwareTypeNTSC ? HLE_NTSC_HALFLINE_US
                                                        : HLE_PAL_HALFLINE_US)
                                / 1.0e6 * 65536.0);
      hleStream.accFrac += inc * hleStream.speedMult / 2u;
      budget = hleStream.accFrac >> 16;
      hleStream.accFrac &= 0xFFFFu;
   }

   while (budget > 0 && hleStream.written < hleStream.total)
   {
      uint32_t chunk, dst;

      if (!hleStream.bufValid)
      {
         if (!CDIntfReadBlock(hleStream.lba, hleStream.buf))
            memset(hleStream.buf, 0, 2352);
         /* I2S un-swap: real hardware swaps bytes within 16-bit words */
         for (i = 0; i + 1 < 2352; i += 2)
         {
            uint8_t tmp = hleStream.buf[i];
            hleStream.buf[i]     = hleStream.buf[i + 1];
            hleStream.buf[i + 1] = tmp;
         }
         hleStream.bufValid = true;
      }

      chunk = 2352 - hleStream.bufOff;
      if (chunk > budget)
         chunk = budget;
      if (chunk > hleStream.total - hleStream.written)
         chunk = hleStream.total - hleStream.written;

      dst = hleStream.dest + hleStream.written;
      for (i = 0; i < chunk && (dst + i) < 0x200000; i++)
         jaguarMainRAM[dst + i] = hleStream.buf[hleStream.bufOff + i];

      hleStream.written += chunk;
      hleStream.bufOff  += chunk;
      budget            -= chunk;

      if (hleStream.bufOff >= 2352)
      {
         hleStream.bufOff   = 0;
         hleStream.bufValid = false;
         hleStream.lba++;
      }
   }

   hle_read_progress = hleStream.written;

   if (hleStream.written >= hleStream.total)
   {
      HLEStreamFinish();
      return;
   }

   /* Advance the write pointer the game's poll loop watches — at the
    * base latched when this read was armed (see statusBase).
    *
    * Report it with the real GPU ISR's cadence: the pre-decremented
    * pointer (dest-4) advancing in whole 32-byte FIFO batches, and
    * never past the bytes actually delivered.  Games read this pointer
    * straight out of the GPU data area (IS2's driver CD_poll at $304E
    * does `movea.l ($3074).l,a0; movea.l (a0),a0`) and checksum the
    * buffer INCREMENTALLY against it as it advances.  A byte-granular
    * `dest+written` value exposed partial longs whose tail bytes had
    * not been written yet — the running sum consumed up to 3 stale
    * bytes, never re-read them, and the final compare missed (Iron
    * Soldier 2 match-load retry loop). */
   if (hleStream.statusBase != 0)
      GPUWriteLong(hleStream.statusBase + 0,
                   hleStream.dest - 4 + ((hleStream.written + 4) / 32) * 32,
                   0);
}

/* ------------------------------------------------------------------ */
/* $304E: CD_poll — return current transfer position                   */
/*                                                                     */
/* Returns:                                                            */
/*   A0 = current write position (= end when done)                     */
/*   A1 = bytes transferred so far                                     */
/* ------------------------------------------------------------------ */

static void HLEHandleCDPoll(void)
{
   static uint32_t pollCount = 0;
   uint32_t a0_val;
   uint32_t a1_val;
   pollCount++;
   if (pollCount <= 5 || (pollCount % 100000) == 0)
      HLE_LOG("CD_poll #%u: pending=%d end=$%06X gpu_data=$%06X\n",
              pollCount, hle_read_pending, hle_read_end_addr,
              hle_gpu_data_base);

   /* Real BIOS CD_poll (resident code at $3610, disassembled from a
    * live BIOS-mode session):
    *   movea.l $3074.l,a0    ; a0 = transfer-state struct (GPU RAM)
    *   movea.l a0,a1
    *   adda.l  #8,a1
    *   movea.l (a0),a0       ; A0 = [struct+0]  — CURRENT DEST POINTER
    *   movea.l (a1),a1       ; A1 = [struct+8]  — progress count
    *   rts
    * A0 is the advancing destination pointer, NOT the struct pointer.
    * Boot stubs compare it against the transfer end (`cmpa.l A6,A0`,
    * Highlander / Battle Morph) or a threshold (`cmpa.l #$80000,A0`,
    * BrainDead 13 / IS2) — both behave correctly with the advancing-
    * pointer semantics, exactly as on hardware.
    *
    * Getting this right matters beyond the completion checks: games
    * REUSE the returned A0 long after the load.  Baldies' engine init
    * does `clr.l (a0)` on whatever its last BIOS call left in A0 —
    * with the old struct-pointer return that cleared $F030C0, the
    * frame-counter increment inside its own freshly-uploaded GPU OP
    * ISR, wedging the 68K forever on a counter nobody bumps.  With
    * the dest-pointer return it clears end-of-buffer scratch, same
    * as on hardware.
    *
    * Defer one poll after an instant completion: some boot stubs
    * (BrainDead 13) must observe the not-done state at least once to
    * take a sequencing branch.  A0=0 fails both completion idioms. */
   if (hleStream.active)
   {
      /* Transfer still streaming — report the partial position with
       * the same 32-byte-batch quantization as the GPU-data-area
       * pointer (see JaguarCDHLEStreamTick): never past the bytes
       * actually delivered, so incremental checksummers stay exact.
       * A1 (the [+8] error status) stays 0: boot stubs abort on
       * nonzero A1, it is NOT a progress counter. */
      a0_val = hleStream.dest - 4 + ((hleStream.written + 4) / 32) * 32;
      a1_val = 0;
   }
   else if (hle_read_pending)
   {
      hle_read_pending = false;
      a0_val = 0;
      a1_val = 0;
   }
   else if (hle_read_end_addr == 0)
   {
      a0_val = 0;
      a1_val = 0;
   }
   else if (hle_gpu_data_base != 0)
   {
      a0_val = GPUReadLong(hle_gpu_data_base + 0, UNKNOWN);
      a1_val = GPUReadLong(hle_gpu_data_base + 8, UNKNOWN);
   }
   else
   {
      /* No ISR setup call yet — synthesize the transfer-state struct
       * the way the boot-time BIOS would have. */
      hle_gpu_data_base = 0xF03B00;
      GPUWriteLong(hle_gpu_data_base + 0, hle_read_end_addr + 28, 0);
      GPUWriteLong(hle_gpu_data_base + 4, hle_read_end_addr, 0);
      SET32(jaguarMainRAM, 0x3074, hle_gpu_data_base);
      a0_val = hle_read_end_addr + 28;
      a1_val = 0;
   }

   m68k_set_reg(M68K_REG_A0, a0_val);
   m68k_set_reg(M68K_REG_A1, a1_val);
}

/* ------------------------------------------------------------------ */
/* $305A: CD_read_toc — read TOC into buffer at A0                     */
/* ------------------------------------------------------------------ */

static void HLEHandleReadTOC(void)
{
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);

   HLE_LOG("CD_read_toc: A0=$%06X\n", a0);

   if (a0 > 0 && a0 < 0x200000)
      HLEPopulateTOC(a0);
}

/* ------------------------------------------------------------------ */
/* $3006: CD_wait_response — return DSA response in D1                 */
/*                                                                     */
/* Real BIOS polls BUTCH bit 13 and reads DS_DATA.  HLE returns        */
/* $0000 (idle/ready) to avoid infinite poll loops.                     */
/* ------------------------------------------------------------------ */

static void HLEHandleWaitResponse(void)
{
   m68k_set_reg(M68K_REG_D1, 0x0000);
}

/* Mirror the BIOS DSA wait-for-response side effect (see the
 * CD_BIOS_DSA_STATUS_ADDR comment): when the caller passed the wait
 * flag (D0.w != 0), write the status word the real BIOS would derive
 * from the drive's response class — 0 for an $04xx ack, 1 otherwise.
 * The class values match what cdrom.c's BUTCH emulation answers in
 * BIOS mode ($0143 Spun Up for $18xx, $0200 for Stop, $0400 for
 * Pause/Unpause), so both boot paths present the same contract. */
static void HLESetDsaStatus(uint8_t respClass)
{
   uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
   if ((d0 & 0xFFFF) == 0)
      return;   /* no-wait call: BIOS leaves $3E00 untouched */
   SET16(jaguarMainRAM, CD_BIOS_DSA_STATUS_ADDR,
         (respClass == 0x04) ? 0 : 1);
}

/* ------------------------------------------------------------------ */
/* ISR setup — save GPU data area pointer                              */
/*                                                                     */
/* $3000/$3060/$3066 setup calls pass A0 = GPU RAM base.  The boot     */
/* stub later reads [$3074] to find this pointer, then checks the      */
/* transfer state structure there.                                     */
/*                                                                     */
/* GPU data area layout (relative to base):                            */
/*   [+0]  dest pointer  (A0 from CD_read, decremented by 4)          */
/*   [+4]  end address   (A1 from CD_read)                             */
/*   [+8]  progress      (bytes transferred, 0 initially)             */
/*   [+16] sentinel      (D1 from CD_read)                            */
/* ------------------------------------------------------------------ */

static void HLEHandleISRSetup(uint8_t mode)
{
   uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);

   /* Retarget applies to the NEXT CD_read only.  An in-flight streamed
    * transfer keeps writing its status struct at the base latched when
    * the read was armed (hleStream.statusBase) — on real hardware those
    * writes come from the BIOS's own GPU ISR, whose data area doesn't
    * move mid-transfer (and which dies outright when the game reloads
    * GPU RAM with its own code).  Battle Morph calls this vector with
    * A0=$F03158 — inside its freshly-uploaded GPU worker — while a
    * 917KB streamed CD_read is active; retargeting live status writes
    * there shreds the worker code and wedges the game. */
   if (hleStream.active)
      HLE_LOG("ISR setup mid-stream: new base $%06X deferred to next "
              "CD_read (active stream keeps $%06X)\n",
              a0, hleStream.statusBase);

   hle_gpu_data_base = a0;

   /* $3072: ISR mode flag */
   jaguarMainRAM[0x3072] = mode;
   jaguarMainRAM[0x3073] = 0x00;

   /* $3074: pointer to GPU data area */
   SET32(jaguarMainRAM, 0x3074, a0);

   HLE_LOG("ISR setup: mode=$%02X GPU_DATA=$%06X\n", mode, a0);
}

/* ------------------------------------------------------------------ */
/* GPU data phase intercept (safety net)                               */
/*                                                                     */
/* If the GPU somehow starts running the BIOS CD ISR despite our HLE,  */
/* intercept it to prevent hangs from broken BUTCH emulation.           */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEGPUDataPhase(void)
{
   if (!hle_active)
      return false;

   HLE_LOG("GPU data phase intercepted (safety net)\n");
   return true;
}

/* ------------------------------------------------------------------ */
/* Cart space boot-track population                                    */
/*                                                                     */
/* On real Jaguar CD hardware the I2S data stream from the boot track  */
/* flows into the CD cartridge's onboard buffer, mapped into cart      */
/* space ($800000+).  Boot stubs scan this buffer for the universal    */
/* "ATRI" ($41545249) header to validate/locate CD data.  In HLE we   */
/* synthesize this by writing the raw boot track sectors (I2S-swapped) */
/* into jaguarMainROM so cart-space reads see the expected data.       */
/* ------------------------------------------------------------------ */

static void HLEPopulateCartBuffer(void)
{
   uint32_t bootLBA = CDIntfGetSession2FirstTrackLBA();
   uint8_t  sector[2352];
   uint32_t written = 0;
   uint32_t maxBytes = 0x100000;  /* 1 MB — covers boot tracks up to ~425 sectors */
   uint32_t s;
   uint32_t r;

   for (s = 0; written < maxBytes; s++)
   {
      if (!CDIntfReadBlock(bootLBA + s, sector))
         break;

      /* I2S byte-swap (matches hardware word-swap on the serial bus) */
      for (r = 0; r + 1 < 2352; r += 2)
      {
         uint8_t tmp = sector[r];
         sector[r]     = sector[r + 1];
         sector[r + 1] = tmp;
      }

      for (r = 0; r < 2352 && (written + r) < 0x600000; r++)
         jaguarMainROM[written + r] = sector[r];

      written += 2352;
   }

   HLE_LOG("Cart buffer: wrote %u bytes (%u sectors) of boot track "
           "at cart $800000-$%06X\n",
           written, s, 0x800000 + written - 1);
}

/* ------------------------------------------------------------------ */
/* Boot                                                                */
/* ------------------------------------------------------------------ */

/* Park the 68K on a tight halt loop in main RAM so a failed HLE boot
 * does not leave PC pointing at randomized memory.
 *
 * Layout at $00000400:
 *   $400: 60 FE      ; BRA.S $400  (branch-to-self halt)
 *
 * Sets PC=$400 and SP=$200000. Returns no value. */
static void HLEParkOnHalt(void)
{
   SET32(jaguarMainRAM, 0, 0x00200000);
   SET32(jaguarMainRAM, 4, 0x00000400);
   jaguarMainRAM[0x400] = 0x60;
   jaguarMainRAM[0x401] = 0xFE;
   m68k_set_reg(M68K_REG_SP, 0x00200000);
   m68k_set_reg(M68K_REG_PC, 0x00000400);
   LOG_WRN("[CD-HLE] Parked 68K on halt loop at $00000400\n");
}

bool JaguarCDHLEBoot(void)
{
   /* Battle Morph (USA) injects a ~414KB stub at $004400. Keep this in
    * lockstep with the raw-sector buffer in cdintf.c::CDIntfExtractBootStub
    * (currently 256 sectors ≈ 600KB). */
   static uint8_t stubBuf[600 * 1024];
   uint32_t loadAddr = 0, length = 0;
   uint32_t i;

   hle_active        = false;
   hle_read_pending  = false;
   hle_read_end_addr = 0;
   hle_read_dest     = 0;
   hle_read_progress = 0;
   hle_post_read_lba = 0xFFFFFFFFu;

   if (!CDIntfIsImageLoaded())
   {
      LOG_ERR("[CD-HLE] No disc image loaded -- HLE boot aborted\n");
      HLEParkOnHalt();
      return false;
   }

   /* Extract boot stub from session 2 */
   if (!CDIntfExtractBootStub(stubBuf, sizeof(stubBuf), &loadAddr, &length))
   {
      LOG_ERR("[CD-HLE] Boot stub extraction failed\n");
      HLEParkOnHalt();
      return false;
   }

   /* Inject boot stub into Jaguar RAM */
   for (i = 0; i < length && (loadAddr + i) < 0x200000; i++)
      jaguarMainRAM[loadAddr + i] = stubBuf[i];

   LOG_INF("[CD-HLE] Injected boot stub: $%X bytes at $%06X\n",
           length, loadAddr);

   HLEInstallJumpTable();
   HLEPopulateTOC(0x2C00);
   HLEPopulateCartBuffer();

   /* DSA status word: JaguarReset() randomizes RAM, so give the BIOS
    * work-area variable a defined "no response yet" starting value —
    * games poll it right after their first waited DSA command. */
   SET16(jaguarMainRAM, CD_BIOS_DSA_STATUS_ADDR, 0);

   /* ISR mode flag ($3072): default to MATCH mode ($FF, bit 7 set) —
    * the real BIOS boots the stub through its own match-mode load, and
    * every known boot stub that issues CD_read before calling an ISR
    * setup vector expects sentinel-located data.  CD_initf ($3066)
    * clears it; CD_read keys its capture behavior off bit 7 exactly
    * like the resident BIOS does (btst #7,$3072 at $3670).  Must be
    * (re)written after HLEInstallJumpTable, whose RTS fill stomps
    * $3072 with $4E (bit 7 clear = non-match). */
   jaguarMainRAM[0x3072] = 0xFF;
   jaguarMainRAM[0x3073] = 0x00;

   /* CD-ready flag at $3727C */
   jaguarMainRAM[CD_READY_ADDR + 0] = 0xFF;
   jaguarMainRAM[CD_READY_ADDR + 1] = 0xFF;

   /* GPU auth magic ($03D0DEAD at $F03000) */
   GPUWriteLong(GPU_AUTH_ADDR, GPU_AUTH_MAGIC, 0);

   /* I2S clock state the real CD BIOS leaves behind: SCLK=$13 (~20 kHz),
    * SMODE=$15 (INTERNAL/master) — its DSP module programs these during
    * boot (visible as "SMODE $0000 -> $0015" in every BIOS-mode log).
    * The cart-HLE reset block in JaguarReset() does the same for carts,
    * but the CD-HLE path skips it (no cart inserted).  Without a running
    * I2S clock JERRY never fires SSI interrupts, and games whose DSP
    * engine idles on an interrupt-set event mask never advance: Battle
    * Morph's DSP main loop spins on r22==0 at $F1B580, its 68K waits on
    * the DSP, and the game sits on a black screen forever. */
   JERRYWriteWord(0xF1A152, 0x0013, M68K);   /* SCLK */
   JERRYWriteWord(0xF1A156, 0x0015, M68K);   /* SMODE */

   /* Install safe interrupt vectors.  JaguarReset() randomizes RAM, so
    * the 68K vector table ($000-$3FF) contains garbage.  When TOM fires
    * a VBLANK IRQ (autovector level 2 → vector $68), the CPU would jump
    * to a random address and crash.  Write an RTE at $400 and point all
    * exception vectors there so interrupts return harmlessly until the
    * boot stub installs its own handlers. */
   SET16(jaguarMainRAM, 0x400, 0x4E73);  /* RTE */
   for (i = 2; i < 256; i++)
      SET32(jaguarMainRAM, i * 4, 0x00000400);

   /* ILLEGAL instruction handler at $402.  The real CD BIOS installs a
    * handler that skips the 2-byte ILLEGAL opcode ($4AFC).  Games and
    * libraries use ILLEGAL deliberately for various purposes (protection
    * checks, feature detection, library stubs, etc.).  Without this, the
    * RTE at $400 returns to the same ILLEGAL opcode creating an infinite
    * loop.
    *
    * Stack frame: [SP+0] = SR (16 bits), [SP+2] = PC (32 bits).
    * $402: ADDQ.L #2, (2,SP)   ; skip past 2-byte ILLEGAL opcode
    * $406: RTE */
   jaguarMainRAM[0x402] = 0x54;  /* ADDQ.L #2, (d16,A7) */
   jaguarMainRAM[0x403] = 0xAF;
   jaguarMainRAM[0x404] = 0x00;  /* displacement = 2 */
   jaguarMainRAM[0x405] = 0x02;
   SET16(jaguarMainRAM, 0x406, 0x4E73);  /* RTE */
   SET32(jaguarMainRAM, 0x10, 0x00000402);  /* vector #4 (ILLEGAL) */

   /* Set initial stack pointer and PC */
   SET32(jaguarMainRAM, 0, 0x00200000);
   SET32(jaguarMainRAM, 4, loadAddr);
   m68k_set_reg(M68K_REG_SP, 0x00200000);
   m68k_set_reg(M68K_REG_PC, loadAddr);

   hle_active = true;

   LOG_INF("[CD-HLE] Boot complete -- PC=$%06X SP=$%06X\n",
           loadAddr, 0x200000);
   return true;
}

/* ------------------------------------------------------------------ */
/* Instruction hook — intercept all 18 BIOS jump table entries         */
/* ------------------------------------------------------------------ */

bool JaguarCDHLEHook(uint32_t pc)
{
   if (!hle_active)
      return false;

   /* Fast rejection: jump table is $3000-$306B */
   if (pc < BIOS_JUMPTABLE_BASE || pc > 0x00306B)
      return false;

   /* Step aside for games that install their own BIOS-workalike CD
    * driver over the jump table (Iron Soldier 2's loader copies a full
    * driver to $3000+, replacing our $4E75 stubs with real code).  Such
    * a driver performs the complete transfer protocol natively — DSA
    * seek, GPU CD ISR, FIFO drain — against the BUTCH emulation, which
    * is exactly the path BIOS mode runs (and it works).  Intercepting
    * on top of it double-drives the transfer: the HLE stream and the
    * driver's ISR write the same buffer, and the HLE CD_poll fights the
    * driver's own register returns.  Hook only while OUR stub is still
    * what executes at the entry PC. */
   if (GET16(jaguarMainRAM, pc) != 0x4E75)
      return false;

   switch (pc)
   {
   case JT_CD_READ:
      HLEHandleCDRead();
      return true;

   case JT_CD_POLL:
      HLEHandleCDPoll();
      return true;

   case JT_CD_READ_TOC:
      HLEHandleReadTOC();
      return true;

   case JT_CD_WAIT_RESPONSE:
   case JT_CD_WAIT_RESPONSE2:
      HLEHandleWaitResponse();
      return true;

   /* ISR setup: save GPU data area pointer from A0 */
   case JT_CD_SETUP_AUDIO_ISR:
      HLEHandleISRSetup(0x00);
      return true;
   case JT_CD_SETUP_CDROM_ISR:
      HLEHandleISRSetup(0xFF);
      return true;
   case JT_CD_SETUP_DATA_ISR:
      HLEHandleISRSetup(0x01);
      return true;

   /* CD_I2S_enable: on real hardware this sends DSA "Set Mode audio"
    * ($1501) + enables I2CNTRL, and the drive — still playing from the
    * position the last CD_read/just-seek left it at — streams audio over
    * I2S into the DSP (SMODE slave).  Start the emulated CD->SSI stream
    * from that position so the DSP's audio driver gets clocked and fed
    * (Primal Rage's Probe-logo music; the game wedges without it). */
   case JT_CD_I2S_ENABLE:
      if (hle_post_read_lba != 0xFFFFFFFFu)
      {
         HLE_LOG("CD_I2S_enable: starting CD audio stream at LBA %u\n",
                 hle_post_read_lba);
         CDROMHLEStartAudio(hle_post_read_lba);
      }
      return true;

   case JT_CD_STOP_DRIVE:
      CDROMHLESetAudioPlaying(0);
      HLESetDsaStatus(0x02);   /* drive answers Stopped $02nn */
      return true;

   case JT_CD_PAUSE:
      CDROMHLESetAudioPlaying(0);
      HLESetDsaStatus(0x04);   /* $04xx ack: status = 0 (no error) */
      return true;

   case JT_CD_UNPAUSE:
      CDROMHLESetAudioPlaying(1);
      HLESetDsaStatus(0x04);   /* $04xx ack: status = 0 (no error) */
      return true;

   /* CD_spin (goto session): seeks are LBA-absolute in HLE, so the
    * session goto itself is a no-op — but the caller may wait on the
    * DSA status word (Myst spins on it), so deliver the Spun Up
    * response class the emulated drive gives in BIOS mode. */
   case JT_CD_SPIN_UP:
      HLESetDsaStatus(0x01);   /* $0143 Spun Up */
      return true;

   /* Remaining CD-control entries (CD_SET_VOL_MUTE/MAX,
    * CD_FIFO_DISABLE, CD_HW_RESET, CD_SET_DAC_MODE) are not intercepted —
    * the jump table is pre-stubbed with $4E75 (RTS) by
    * HLEInstallJumpTable, so falling through executes a no-op naturally. */

   default:
      break;
   }

   return false;
}

/* ------------------------------------------------------------------ */
/* CDBootStrategy vtable                                               */
/* ------------------------------------------------------------------ */

static bool hle_strategy_boot(const struct retro_game_info *info)
{
   (void)info;
   jaguarCartInserted = false;
   JaguarReset();

   if (!JaguarCDHLEBoot())
   {
      LOG_ERR("[CD-HLE] HLE boot failed -- falling back to diagnostic screen\n");
      return false;
   }

   LOG_INF("[CD] Boot path: HLE (no external CD BIOS)\n");
   return true;
}

static bool hle_strategy_instruction_hook(uint32_t pc)
{
   if (JaguarCDHLEHook(pc))
      return true;

   /* Trap calls to cart ROM space ($800000+) — the boot stub is trying
    * to call CD BIOS routines that don't exist in HLE mode. */
   if (hle_active && pc >= 0x800000 && pc < 0xE00000)
   {
      uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
      if (sp >= 4 && sp < 0x200000)
      {
         uint32_t retAddr = GET32(jaguarMainRAM, sp);
         m68k_set_reg(M68K_REG_PC, retAddr);
         m68k_set_reg(M68K_REG_A7, sp + 4);
      }
      return true;
   }

   return false;
}

static void hle_strategy_reset(void)
{
   hle_active        = false;
   hle_read_pending  = false;
   hle_read_end_addr = 0;
   hle_read_dest     = 0;
   hle_read_progress = 0;
   hle_post_read_lba = 0xFFFFFFFFu;
   hle_stream_arm_count = 0;
   memset(&hleStream, 0, sizeof(hleStream));
}

const CDBootStrategy cd_boot_strategy_hle = {
   "hle",
   hle_strategy_boot,
   hle_strategy_instruction_hook,
   hle_strategy_reset
};
