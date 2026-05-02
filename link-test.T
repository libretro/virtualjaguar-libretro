/* Wide symbol export script for white-box test builds.
 *
 * The release / production ABI is link.T, which exports only retro_*.
 * This script is used when the Makefile is invoked with
 * TEST_EXPORTS=1 (set automatically by the `test` target).  It
 * exposes the internal symbols our headless test harnesses dlsym
 * into for white-box testing of the emulator state.
 *
 * If you add a new symbol here, that's an extension to the test ABI
 * only — it does NOT change the production-shipped .so / .dll.
 */
{
   global:
      retro_*;
      DSP*;
      dsp_*;
      m68k_*;
      Jaguar*;
      jaguar*;
      Halfline*;
      jaguarMainRAM;
      jaguarMainROM;
      jagMemSpace;
      pcQueue;
      pcQPtr;
      a6Queue;
      d0Queue;
      GPU*;
      gpu_*;
      JERRY*;
      TOM*;
      OP*;
      tomRam8;
      regs;
      sclk;
      smode;
      lowerField;
      vjs;
      perf_counters_dump;
      perf_counters_reset;
      perf_counters_register;
      perf_counters_find;
   local: *;
};
