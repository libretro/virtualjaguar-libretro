/* Production ABI export script.
 *
 * Only the retro_* libretro entry points are exported from the
 * shipped .so / .dll.  Internal emulator state (DSP, GPU, blitter,
 * 68K registers, jaguarMainRAM, etc.) is hidden so frontends and
 * future refactors don't accidentally pin themselves to internal
 * symbols.
 *
 * White-box test harnesses need the wider symbol set; the Makefile
 * switches to link-test.T when TEST_EXPORTS=1 (which the `test`
 * target sets automatically).
 */
{
   global:
      retro_*;
   local: *;
};
