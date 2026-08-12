/*
 * BLIT_MEMO.C
 *
 * Titledb-gated blitter memoization.  See blit_memo.h for the model
 * and soundness boundaries, and issue #411 for the measurements that
 * motivated it (Alien vs Predator re-issues one bit-identical
 * 1,446-blit stream per 5-field engine cycle while the player idles).
 *
 * Data model: a round-robin pool of entries, each holding the full
 * pre- and post-launch blitter register file, 512-bit read/write page
 * bitmaps over the 2MB of main RAM (4KB pages), a generation stamp,
 * and a successor link.  A global per-page generation array is bumped
 * by every tracked write (the hooks in jaguar.c / blitter.c); an
 * entry is skippable when its register file matches and every page in
 * its footprint carries a generation <= its stamp.
 */

#include <stdlib.h>
#include <string.h>

#include "blit_memo.h"
#include "blitter_internal.h"
#include "settings.h"
#include "vjag_memory.h"
#include "shadowfb.h"
#include "state.h"     /* BlitterStateSave/Load: the canonical state blob */
#include "vjtrace.h"
#include "log.h"
#include "jaguar.h"    /* jaguarMemTrackInserted, jaguarMainROMCRC32 */
#include "jaggd.h"     /* jgdActive */

/* Cart space is writable when a GameDrive or a Memory Track backs it
 * (same presence test as jaguar.c's MEMTRACK_PRESENT). */
#define BM_CART_MUTABLE() \
   (jgdActive || jaguarMemTrackInserted || jaguarMainROMCRC32 == 0xFDF37F47)

#define BM_ENTRIES   4096            /* power of two */
#define BM_HASH_SIZE 8192            /* power of two */
#define BM_HASH_PROBES 8
/* Pre/post snapshots hold the CANONICAL blitter state blob
 * (BlitterStateSave): blitter_ram plus every engine file-scope
 * variable.  blitter_ram alone is NOT the blit's full input -- the
 * engines carry decode/iterator state across launches (measured:
 * verify mode flagged 1,967 would-be-unsound skips on AvP's
 * $41802801 texture blits when only blitter_ram was compared). */
#define BM_STATE_MAX 768
#define BM_PAGE_SHIFT 12             /* 4KB pages */
#define BM_PAGES     512             /* 2MB / 4KB */
#define BM_BMWORDS   (BM_PAGES / 64)

/* Per-entry write log: every tracked write the blit performed, as
 * (addr | len<<28, value) pairs.  Needed because a skipped PREFIX of a
 * chain leaves the previous pass's FINAL bytes in RAM -- including
 * overwrites by later blits of that pass.  If the chain then diverges
 * (or the frame ends), the skipped blits' outputs are materialized by
 * replaying the logs in chain order; applying bytes that are already
 * there is an idempotent no-op, so this is always sound and costs
 * memcpy-class time, not blit time.  Blits whose log overflows the cap
 * (full-buffer clears) are marked exec-through instead. */
#define BM_LOG_RECS  256

#define BM_F_EXEC_THROUGH 0x01

typedef struct
{
   uint8_t  pre[BM_STATE_MAX];
   uint8_t  post[BM_STATE_MAX];
   uint64_t rd[BM_BMWORDS];
   uint64_t wr[BM_BMWORDS];
   /* Two stamps, because a recorded stream's own blits interleave
    * writes on shared destination pages: a single execution-time stamp
    * would see every entry's dest pages dirtied by its successors and
    * nothing would ever skip.
    *  - rdStamp: bmGen when THIS blit finished executing.  Source
    *    pages must be untouched since the blit actually read them --
    *    a foreign write to a source page later in the same frame must
    *    invalidate.
    *  - wrStamp: bmGen at the recording pass's finalize (frame
    *    boundary).  Dest pages written by LATER blits of the same
    *    pass are benign: when the whole pass skips, RAM holds the
    *    pass's final bytes, which is the definition of correct.  0
    *    until finalized (never skippable mid-pass). */
   uint32_t rdStamp;
   uint32_t wrStamp;
   uint32_t hash;                    /* FNV-1a of pre[] */
   uint32_t epoch;                   /* != bmFlushEpoch -> invalid */
   int32_t  next;                    /* successor entry, -1 = none */
   uint32_t log[BM_LOG_RECS * 2];    /* (addr|len<<28, value) pairs */
   uint32_t logN;                    /* record count */
   uint8_t  flags;
} bm_entry;

int blitMemoMode = BLIT_MEMO_OFF;
int blitMemoRecording = 0;

uint32_t blitMemoHits = 0;
uint32_t blitMemoMisses = 0;
uint32_t blitMemoDirty = 0;
uint32_t blitMemoExecThrough = 0;
uint32_t blitMemoVerifyFails = 0;

static bm_entry *bmPool = NULL;
static int32_t bmHashTab[BM_HASH_SIZE];
static uint32_t bmPoolNext = 0;
/* Two write-generation domains sharing one counter.  CHAIN writes are
 * those made while a chain member executes (recording, exec-through,
 * verify): their ordering relative to skipped entries is preserved by
 * the log replay, so they must not invalidate other entries' WRITE
 * footprints -- otherwise a single live full-buffer clear re-dirties
 * the whole stream every cycle and nothing ever skips.  They DO
 * invalidate READ footprints (a chain member rewriting a page someone
 * sources from changes that blit's input).  FOREIGN writes (68K, GPU,
 * DSP, OP, cheats -- anything outside blit execution) invalidate
 * both. */
static uint32_t bmPageGen[BM_PAGES];        /* foreign writes */
static uint32_t bmPageGenChain[BM_PAGES];   /* chain-member writes */
static int bmChainExec = 0;                 /* inside exec-through run */
static uint32_t bmGen = 1;
static uint32_t bmFlushEpoch = 1;
static int32_t bmCursor = -1;
static bm_entry *bmRec = NULL;       /* entry being recorded */
static int bmCartMutable = 0;
static uint32_t bmFrame = 1;
static uint32_t bmRestampFrame[BM_PAGES];
static int bmVerifyLogged = 0;

/* Entries recorded (or verified) since the last finalize; their
 * wrStamp is assigned at the frame boundary so a pass's own
 * interleaved dest writes never poison it. */
static int32_t bmPending[BM_ENTRIES];
static uint32_t bmPendingN = 0;

static void bm_finalize_pending(void)
{
   uint32_t i;
   for (i = 0; i < bmPendingN; i++)
      bmPool[bmPending[i]].wrStamp = bmGen;
   bmPendingN = 0;
}

static void bm_pending_add(int32_t idx)
{
   if (bmPendingN == BM_ENTRIES)
      bm_finalize_pending();   /* overflow: conservative early finalize */
   bmPending[bmPendingN++] = idx;
}

/* Blits skipped since the last live write / frame boundary, in chain
 * order.  Their outputs are materialized (log replay) before any live
 * engine execution and at the frame tick, so RAM always reaches the
 * exact final state a live run would have produced. */
static int32_t bmSkipRun[BM_ENTRIES];
static uint32_t bmSkipRunN = 0;

/* Replay one entry's write log.  Raw stores, deliberately WITHOUT
 * page-generation bumps: the bytes are identical to what the skipped
 * blit would have written, so content is unchanged for cleanliness
 * purposes. */
static void bm_apply_log(const bm_entry *e)
{
   uint32_t i, rec, addr, len, val;
   for (i = 0; i < e->logN; i++)
   {
      rec  = e->log[i * 2];
      val  = e->log[i * 2 + 1];
      addr = rec & 0x1FFFFF;
      len  = rec >> 28;
      switch (len)
      {
         case 1:
            jaguarMainRAM[addr] = (uint8_t)val;
            break;
         case 2:
            jaguarMainRAM[addr]                  = (uint8_t)(val >> 8);
            jaguarMainRAM[(addr + 1) & 0x1FFFFF] = (uint8_t)val;
            break;
         default:
            jaguarMainRAM[addr]                  = (uint8_t)(val >> 24);
            jaguarMainRAM[(addr + 1) & 0x1FFFFF] = (uint8_t)(val >> 16);
            jaguarMainRAM[(addr + 2) & 0x1FFFFF] = (uint8_t)(val >> 8);
            jaguarMainRAM[(addr + 3) & 0x1FFFFF] = (uint8_t)val;
            break;
      }
   }
}

static void bm_repair(void)
{
   uint32_t i;
   for (i = 0; i < bmSkipRunN; i++)
      bm_apply_log(&bmPool[bmSkipRun[i]]);
   bmSkipRunN = 0;
}

/* ---------------------------------------------------------------- */

/* Current launch's pre-state blob, captured once at BlitMemoLaunch. */
static uint8_t bmCur[BM_STATE_MAX];
static uint32_t bmStateLen = 0;     /* constant after first capture */

static uint32_t bm_hash_mem(const uint8_t *p, uint32_t len)
{
   uint32_t h = 2166136261u;
   uint32_t i;
   for (i = 0; i < len; i++)
   {
      h ^= p[i];
      h *= 16777619u;
   }
   return h;
}

static void bm_bits_set(uint64_t *bm, uint32_t addr, uint32_t len)
{
   uint32_t p0 = addr >> BM_PAGE_SHIFT;
   uint32_t p1 = (addr + len - 1) >> BM_PAGE_SHIFT;
   uint32_t p;
   for (p = p0; p <= p1 && p < BM_PAGES; p++)
      bm[p >> 6] |= (uint64_t)1 << (p & 63);
}

static int bm_pages_clean(const bm_entry *e)
{
   uint32_t w, b, p;
   if (e->wrStamp == 0)
      return 0;                      /* pass not finalized yet */
   for (w = 0; w < BM_BMWORDS; w++)
   {
      uint64_t m = e->rd[w];
      for (b = 0; m; b++, m >>= 1)
      {
         p = w * 64 + b;
         if ((m & 1) && (bmPageGen[p] > e->rdStamp
                         || bmPageGenChain[p] > e->rdStamp))
            return 0;
      }
      m = e->wr[w];
      for (b = 0; m; b++, m >>= 1)
         if ((m & 1) && bmPageGen[w * 64 + b] > e->wrStamp)
            return 0;
   }
   return 1;
}

static void bm_run_engine(void)
{
   if (vjs.useFastBlitter)
      blitter_blit(GET32(blitter_ram, 0x38));   /* COMMAND */
   else
      BlitterMidsummer2();
}

/* Hash table: open-addressed, bounded linear probe; slots hold entry
 * index or -1.  Entries are validated by epoch + hash + memcmp at use,
 * so stale slots (evicted / re-recorded entries) are harmless. */

static void bm_hash_insert(uint32_t h, int32_t idx)
{
   uint32_t i, slot;
   for (i = 0; i < BM_HASH_PROBES; i++)
   {
      slot = (h + i) & (BM_HASH_SIZE - 1);
      if (bmHashTab[slot] < 0 || bmHashTab[slot] == idx
          || bmPool[bmHashTab[slot]].epoch != bmFlushEpoch
          || bmPool[bmHashTab[slot]].hash == h)
      {
         bmHashTab[slot] = idx;
         return;
      }
   }
   bmHashTab[h & (BM_HASH_SIZE - 1)] = idx;
}

static int32_t bm_lookup(uint32_t h)
{
   uint32_t i, slot;
   int32_t idx;
   for (i = 0; i < BM_HASH_PROBES; i++)
   {
      slot = (h + i) & (BM_HASH_SIZE - 1);
      idx = bmHashTab[slot];
      if (idx < 0)
         return -1;
      if (bmPool[idx].epoch == bmFlushEpoch && bmPool[idx].hash == h
          && memcmp(bmPool[idx].pre, bmCur, bmStateLen) == 0)
         return idx;
   }
   return -1;
}

static int bm_alloc_pool(void)
{
   if (bmPool)
      return 1;
   bmPool = (bm_entry *)malloc(sizeof(bm_entry) * BM_ENTRIES);
   if (!bmPool)
   {
      LOG_WRN("[BLITMEMO] pool allocation failed (%u bytes); memo disabled\n",
              (unsigned)(sizeof(bm_entry) * BM_ENTRIES));
      blitMemoMode = BLIT_MEMO_OFF;
      return 0;
   }
   memset(bmHashTab, 0xFF, sizeof(bmHashTab));
   return 1;
}

/* FNV over the RAM bytes of every page in a bitmap (verify mode). */
static uint32_t bm_hash_pages(const uint64_t *bm)
{
   uint32_t h = 2166136261u;
   uint32_t w, b, i;
   const uint8_t *page;
   for (w = 0; w < BM_BMWORDS; w++)
   {
      uint64_t m = bm[w];
      for (b = 0; m; b++, m >>= 1)
      {
         if (!(m & 1))
            continue;
         page = jaguarMainRAM + ((w * 64 + b) << BM_PAGE_SHIFT);
         for (i = 0; i < (1u << BM_PAGE_SHIFT); i++)
         {
            h ^= page[i];
            h *= 16777619u;
         }
      }
   }
   return h;
}

/* Re-stamp the hi-res shadow epoch for the pages a skipped blit wrote,
 * once per page per frame.  Without this, skipping the re-blit for
 * longer than HIRES_EPOCH_WINDOW would silently age Stage 2 content
 * out of the OP resolve -- degrading 2x to box replication exactly
 * when the memo is doing its job.  Sound because the pages are
 * verified clean at skip time: their bytes are bit-frozen since the
 * live blit that stored the shadow content. */
static void bm_restamp(const bm_entry *e)
{
   uint32_t w, b, p;
   if (!shadowHiresActive)
      return;
   for (w = 0; w < BM_BMWORDS; w++)
   {
      uint64_t m = e->wr[w];
      for (b = 0; m; b++, m >>= 1)
      {
         if (!(m & 1))
            continue;
         p = w * 64 + b;
         if (bmRestampFrame[p] != bmFrame)
         {
            bmRestampFrame[p] = bmFrame;
            ShadowHiresRestampRamPage(p);
         }
      }
   }
}

static void bm_record(int32_t idx, uint32_t h, int keep_next)
{
   bm_entry *e = &bmPool[idx];
   int32_t oldnext = keep_next ? e->next : -1;

   memcpy(e->pre, bmCur, bmStateLen);
   e->hash  = h;
   e->epoch = bmFlushEpoch;
   e->next  = oldnext;
   e->flags = 0;
   memset(e->rd, 0, sizeof(e->rd));
   memset(e->wr, 0, sizeof(e->wr));
   e->logN = 0;
   bm_hash_insert(h, idx);
   if (bmCursor >= 0 && bmCursor != idx)
      bmPool[bmCursor].next = idx;

   bmRec = e;
   blitMemoRecording = 1;
   bm_run_engine();
   blitMemoRecording = 0;
   bmRec = NULL;

   BlitterStateSave(e->post);
   e->rdStamp = bmGen;
   e->wrStamp = 0;
   bm_pending_add(idx);
   bmCursor = idx;
}

/* Verify a would-be skip against skip+repair semantics: a skip's
 * effect is exactly "materialize the recorded write log and restore
 * the recorded post-state", so run the blit live while re-capturing
 * its write log into a scratch entry, then require both the log and
 * the post-state to be identical to what the memo would have
 * replayed. */
static bm_entry bmScratch;

static void bm_verify(bm_entry *e)
{
   uint8_t poststate[BM_STATE_MAX];
   bmScratch.logN = 0;
   bmScratch.flags = 0;
   bmRec = &bmScratch;
   blitMemoRecording = 1;
   bm_run_engine();
   blitMemoRecording = 0;
   bmRec = NULL;
   BlitterStateSave(poststate);
   if (bmScratch.logN != e->logN
       || memcmp(bmScratch.log, e->log, e->logN * 2 * sizeof(uint32_t)) != 0
       || memcmp(e->post, poststate, bmStateLen) != 0)
   {
      blitMemoVerifyFails++;
      if (!bmVerifyLogged)
      {
         LOG_WRN("[BLITMEMO] VERIFY divergence: a skip would have been "
                 "unsound (cmd=%08X); this title must not be tagged\n",
                 (unsigned)GET32(e->pre, 0x38));
         bmVerifyLogged = 1;
      }
   }
   /* Same stamp discipline as recording, so verify mode exercises the
    * deep stream instead of dirtying it with its own live writes. */
   e->rdStamp = bmGen;
   e->wrStamp = 0;
   bm_pending_add((int32_t)(e - bmPool));
}

/* ---------------------------------------------------------------- */

int BlitMemoLaunch(void)
{
   uint32_t h;
   int32_t cand = -1;
   bm_entry *e;
   uint32_t cmd, a1base;

   if (!blitMemoMode)
      return 0;
   /* CD content: the CD HLE writes main RAM without passing the write
    * hooks, so page generations would lie.  Refuse here rather than at
    * option time -- bootConfig may not be resolved when the option is
    * first read. */
   if (bootConfig.isCDGame)
      return 0;
   if (!bmPool && !bm_alloc_pool())
      return 0;

   bmCartMutable = BM_CART_MUTABLE();

   {
      size_t len = BlitterStateSave(bmCur);
      if (len > BM_STATE_MAX)
      {
         LOG_WRN("[BLITMEMO] state blob %u > %u; memo disabled\n",
                 (unsigned)len, (unsigned)BM_STATE_MAX);
         blitMemoMode = BLIT_MEMO_OFF;
         return 0;
      }
      bmStateLen = (uint32_t)len;
   }

   h = bm_hash_mem(bmCur, bmStateLen);
   if (bmCursor >= 0)
   {
      int32_t nx = bmPool[bmCursor].next;
      if (nx >= 0 && bmPool[nx].epoch == bmFlushEpoch
          && bmPool[nx].hash == h
          && memcmp(bmPool[nx].pre, bmCur, bmStateLen) == 0)
         cand = nx;
   }
   if (cand < 0)
      cand = bm_lookup(h);

   if (cand >= 0)
   {
      e = &bmPool[cand];
      if (bmCursor >= 0 && bmCursor != cand)
         bmPool[bmCursor].next = cand;

      if (e->flags & BM_F_EXEC_THROUGH)
      {
         /* Untracked I/O: deterministic re-execution, never skipped.
          * Materialize any skipped prefix first so its live writes
          * land on top of the prefix outputs, as a live run orders
          * them. */
         bm_repair();
         bmChainExec = 1;
         bm_run_engine();
         bmChainExec = 0;
         blitMemoExecThrough++;
         bmCursor = cand;
         return 1;
      }
      if (bm_pages_clean(e))
      {
         if (blitMemoMode == BLIT_MEMO_VERIFY)
         {
            bm_repair();
            bm_verify(e);
            bmCursor = cand;
            return 1;
         }
         /* Skip: the recorded write log will be materialized at the
          * next live write or the frame boundary.  Keep trace parity
          * with the engines' own BLIT_CMD emit. */
         cmd    = GET32(blitter_ram, 0x38);   /* COMMAND */
         a1base = GET32(blitter_ram, 0x00);   /* A1_BASE */
         VJT_EMIT(VJT_EV_BLIT_CMD, BLITTER, cmd, a1base);
         (void)cmd;
         (void)a1base;
         BlitterStateLoad(e->post);
         bm_restamp(e);
         if (bmSkipRunN == BM_ENTRIES)
            bm_repair();
         bmSkipRun[bmSkipRunN++] = cand;
         blitMemoHits++;
         bmCursor = cand;
         return 1;
      }
      blitMemoDirty++;
      bm_repair();
      bm_record(cand, h, 1);   /* same pre: keep the chain link */
      return 1;
   }

   blitMemoMisses++;
   bm_repair();
   bm_record((int32_t)(bmPoolNext++ & (BM_ENTRIES - 1)), h, 0);
   return 1;
}

void BlitMemoWriteHook(uint32_t addr, uint32_t len, uint32_t data)
{
   addr &= 0xFFFFFF;
   if (addr < 0x200000)
   {
      uint32_t p0 = addr >> BM_PAGE_SHIFT;
      uint32_t p1 = (addr + len - 1) >> BM_PAGE_SHIFT;
      uint32_t p;
      uint32_t *gens = (bmRec || bmChainExec) ? bmPageGenChain : bmPageGen;
      for (p = p0; p <= p1 && p < BM_PAGES; p++)
      {
         if (!++bmGen)
         {
            BlitMemoFlush();
            bmGen = 1;
         }
         gens[p] = bmGen;
      }
      if (bmRec)
      {
         bm_bits_set(bmRec->wr, addr, len);
         if (bmRec->logN < BM_LOG_RECS)
         {
            bmRec->log[bmRec->logN * 2]     = addr | (len << 28);
            bmRec->log[bmRec->logN * 2 + 1] = data;
            bmRec->logN++;
         }
         else
            bmRec->flags |= BM_F_EXEC_THROUGH;   /* log overflow */
      }
      return;
   }
   /* Anything else -- device space, cart flash, the unpopulated
    * $200000-$7FFFFF hole -- makes the blit non-memoizable. */
   if (bmRec)
      bmRec->flags |= BM_F_EXEC_THROUGH;
}

void BlitMemoNoteRead(uint32_t addr, uint32_t len)
{
   if (!bmRec)
      return;
   addr &= 0xFFFFFF;
   if (addr < 0x800000)
   {
      /* Main RAM; $200000-$7FFFFF reads mirror into the low 2MB. */
      bm_bits_set(bmRec->rd, addr & 0x1FFFFF, len);
      return;
   }
   if (addr < 0xE00000)
   {
      /* Cart ROM: immutable unless a write-capable device backs it. */
      if (bmCartMutable)
         bmRec->flags |= BM_F_EXEC_THROUGH;
      return;
   }
   bmRec->flags |= BM_F_EXEC_THROUGH;
}

void BlitMemoFlush(void)
{
   bmFlushEpoch++;
   memset(bmPageGen, 0, sizeof(bmPageGen));
   memset(bmPageGenChain, 0, sizeof(bmPageGenChain));
   bmCursor = -1;
   bmPendingN = 0;
   bmSkipRunN = 0;
}

void BlitMemoNotifyEngine(int useFast)
{
   static int prev = -1;
   if (prev != useFast)
   {
      if (prev != -1)
         BlitMemoFlush();
      prev = useFast;
   }
}

void BlitMemoFrame(void)
{
   bmFrame++;
   if (bmPool)
   {
      /* Materialize any skips whose pass may have ended early (a live
       * run would have re-drawn them; applying identical bytes when
       * the pass did complete is a no-op). */
      bm_repair();
      bm_finalize_pending();
   }
}

void BlitMemoSetMode(int mode)
{
   if (mode == blitMemoMode)
      return;
   blitMemoMode = mode;
   BlitMemoFlush();
   if (mode != BLIT_MEMO_OFF)
   {
      bm_alloc_pool();
      LOG_INF("[BLITMEMO] mode=%s\n",
              mode == BLIT_MEMO_VERIFY ? "verify" : "enabled");
   }
}

void BlitMemoShutdown(void)
{
   if (bmPool)
      free(bmPool);
   bmPool = NULL;
   blitMemoMode = BLIT_MEMO_OFF;
   blitMemoRecording = 0;
   bmPoolNext = 0;
   memset(bmPageGen, 0, sizeof(bmPageGen));
   memset(bmPageGenChain, 0, sizeof(bmPageGenChain));
   bmChainExec = 0;
   memset(bmRestampFrame, 0, sizeof(bmRestampFrame));
   bmGen = 1;
   bmFlushEpoch = 1;
   bmCursor = -1;
   bmRec = NULL;
   bmCartMutable = 0;
   bmFrame = 1;
   bmVerifyLogged = 0;
   bmPendingN = 0;
   bmSkipRunN = 0;
   blitMemoHits = blitMemoMisses = blitMemoDirty = 0;
   blitMemoExecThrough = blitMemoVerifyFails = 0;
}
