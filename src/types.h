//
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Removal of unsafe macros and addition of typdefs by James Hammons
//

#ifndef __TYPES_H__
#define __TYPES_H__

// This should be portable, since it's part of the C99 standard...!

#include <stdint.h>

typedef uint8_t		uint8;
typedef int8_t		int8;
typedef uint16_t	uint16;
typedef int16_t		int16;
typedef uint32_t	uint32;
typedef int32_t		int32;
typedef uint64_t	uint64;
typedef int64_t		int64;

#endif	// __TYPES_H__
