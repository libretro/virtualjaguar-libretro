/* Extra symbols beyond retro_* are exported intentionally so that
   test harnesses (test_cd_boot, test_gpu_ops, test_rom_smoke, etc.)
   can dlsym() into internal emulator state for white-box testing. */
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
   local: *;
};

