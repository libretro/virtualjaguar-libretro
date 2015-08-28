//
// FILE.H
//
// File support
//

#ifndef __FILE_H__
#define __FILE_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum FileType { FT_SOFTWARE=0, FT_EEPROM, FT_LABEL, FT_BOXART, FT_OVERLAY };
// JST = Jaguar Software Type
enum { JST_NONE = 0, JST_ROM, JST_ALPINE, JST_ABS_TYPE1, JST_ABS_TYPE2, JST_JAGSERVER, JST_WTFOMGBBQ };

bool JaguarLoadFile(uint8_t *buffer, size_t bufsize);
bool AlpineLoadFile(uint8_t *buffer, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif	// __FILE_H__
