//
// FILE.H
//
// File support
//

#ifndef __FILE_H__
#define __FILE_H__

#include <stdint.h>
#include <stddef.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

enum FileType { FT_SOFTWARE=0, FT_EEPROM, FT_LABEL, FT_BOXART, FT_OVERLAY };
/* JST = Jaguar Software Type */
enum { JST_NONE = 0, JST_ROM, JST_ALPINE, JST_ABS_TYPE1, JST_ABS_TYPE2, JST_JAGSERVER, JST_WTFOMGBBQ, JST_RAW_BINARY };

bool JaguarLoadFile(uint8_t *buffer, size_t bufsize);
/* Replicate a 1/2/4 MiB cart image across the cart window (#851) and clear
 * the rest.  Called by the loader; call again after patching the flat window
 * so the repeats carry the patch (title hooks). */
void JaguarMirrorCart(void);
uint32_t DetectPrependedHeaderSize(uint8_t *buffer, uint32_t size);
/* GPU-only / jagcrypt BootIntro: no 68K program at $802000. */
bool JaguarCartNeedsBIOS(const uint8_t *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif	// __FILE_H__
