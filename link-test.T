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
      busArbiter;
      gpuPCSample;
      m68kPCSample;
      m68kPCSampleIdx;
      pcQueue;
      pcQPtr;
      a6Queue;
      d0Queue;
      GPU*;
      gpu_*;
      JERRY*;
      UART*;
      JLink*;
      TOM*;
      OP*;
      tomRam8;
      regs;
      ltxd;
      rtxd;
      lrxd;
      rrxd;
      sclk;
      smode;
      SetSSIWordsXmittedFromButch;
      lowerField;
      vjs;
      videoBuffer;
      game_width;
      game_height;
      perf_counters_dump;
      perf_counters_reset;
      perf_counters_register;
      perf_counters_find;
      CDROM*;
      CDIntf*;
      CDTrace*;
      BUTCH*;
      GetRamPtr;
      bootConfig;
      cd_boot_strategy_*;
      jaguar_cd_*;
      ResolveBootConfig;
      BlitterCompare*;
      DAC*;
      Joystick*;
      joypad0Buttons;
      joypad1Buttons;
      InputDev*;
      Quad*;
      MTState*;
      NVMBios*;
      NVM_*;
      JGD*;
      jgd*;
      shadowHiresResolveHits;
      shadowHiresResolveMissValue;
      shadowHiresResolveMissEpoch;
      shadowHiresResolveMissNoPage;
      shadowHiresN;
      shadowFBActive;
      blitMemo*;
      TitleDB*;
      /* Texture dump/replacement (#369) probes + the shadow-framebuffer
       * presentation functions the replacement tests drive directly. */
      TexDump*;
      TexReplace*;
      texDumpEnabled;
      texReplaceEnabled;
      ShadowFB*;
      shadowLineRGB;
      shadowLineTag;
      /* TitleHook* is NOT covered by TitleDB* -- the applier lives in its
       * own translation unit and its own name prefix. */
      TitleHook*;
      vjtrace_*;
   local: *;
};
