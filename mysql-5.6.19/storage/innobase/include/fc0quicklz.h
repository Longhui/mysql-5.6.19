#ifndef FC_QLZ_HEADER
#define FC_QLZ_HEADER

#include "univ.i"
#include "fc0fc.h"

// Fast data compression library
// Copyright (C) 2006-2011 Lasse Mikkel Reinhold
// lar@quicklz.com
//
// QuickLZ can be used for free under the GPL 1, 2 or 3 license (where anything 
// released into public must be open source) or under a commercial license if such 
// has been acquired (see http://www.quicklz.com/order.html). The commercial license 
// does not cover derived or ported versions created by third parties under GPL.

// You can edit following user settings. Data must be decompressed with the same 
// setting of QLZ_COMPRESSION_LEVEL and QLZ_STREAMING_BUFFER as it was compressed
// (see manual). If QLZ_STREAMING_BUFFER > 0, scratch buffers must be initially
// zeroed out (see manual). First #ifndef makes it possible to define settings from 
// the outside like the compiler command line.

// 1.5.0 final

#ifndef FC_QLZ_COMPRESSION_LEVEL

	// 1 gives fastest compression speed. 3 gives fastest decompression speed and best
	// compression ratio. 
	#define FC_QLZ_COMPRESSION_LEVEL 1
	//#define FC_QLZ_COMPRESSION_LEVEL 2
	//#define FC_QLZ_COMPRESSION_LEVEL 3

	// If > 0, zero out both states prior to first call to qlz_compress() or qlz_decompress() 
	// and decompress packets in the same order as they were compressed
	#define FC_QLZ_STREAMING_BUFFER 0
	//#define QLZ_STREAMING_BUFFER 100000
	//#define QLZ_STREAMING_BUFFER 1000000

	// Guarantees that decompression of corrupted data cannot crash. Decreases decompression
	// speed 10-20%. Compression speed not affected.
	//#define FC_QLZ_MEMORY_SAFE
#endif

#define FC_QLZ_VERSION_MAJOR 1
#define FC_QLZ_VERSION_MINOR 5
#define FC_QLZ_VERSION_REVISION 0

// Using size_t, memset() and memcpy()
#include <string.h>

// Verify compression level
#if FC_QLZ_COMPRESSION_LEVEL != 1 && FC_QLZ_COMPRESSION_LEVEL != 2 && FC_QLZ_COMPRESSION_LEVEL != 3
#error FC_QLZ_COMPRESSION_LEVEL must be 1, 2 or 3
#endif

typedef unsigned int ui32;
typedef unsigned short int ui16;

// Decrease FC_QLZ_POINTERS for level 3 to increase compression speed. Do not touch any other values!
#if FC_QLZ_COMPRESSION_LEVEL == 1
#define FC_QLZ_POINTERS 1
#define FC_QLZ_HASH_VALUES 4096
#elif FC_QLZ_COMPRESSION_LEVEL == 2
#define FC_QLZ_POINTERS 4
#define FC_QLZ_HASH_VALUES 2048
#elif FC_QLZ_COMPRESSION_LEVEL == 3
#define FC_QLZ_POINTERS 16
#define FC_QLZ_HASH_VALUES 4096
#endif

// Detect if pointer size is 64-bit. It's not fatal if some 64-bit target is not detected because this is only for adding an optional 64-bit optimization.
#if defined _LP64 || defined __LP64__ || defined __64BIT__ || _ADDR64 || defined _WIN64 || defined __arch64__ || __WORDSIZE == 64 || (defined __sparc && defined __sparcv9) || defined __x86_64 || defined __amd64 || defined __x86_64__ || defined _M_X64 || defined _M_IA64 || defined __ia64 || defined __IA64__
	#define FC_QLZ_PTR_64
#endif

// hash entry
typedef struct 
{
#if FC_QLZ_COMPRESSION_LEVEL == 1
	ui32 cache;
#if defined FC_QLZ_PTR_64 && FC_QLZ_STREAMING_BUFFER == 0
	unsigned int offset;
#else
	const unsigned char *offset;
#endif
#else
	const unsigned char *offset[FC_QLZ_POINTERS];
#endif

} fc_qlz_hash_compress;

typedef struct 
{
#if FC_QLZ_COMPRESSION_LEVEL == 1
	const unsigned char *offset;
#else
	const unsigned char *offset[FC_QLZ_POINTERS];
#endif
} fc_qlz_hash_decompress;


// states
typedef struct
{
	#if FC_QLZ_STREAMING_BUFFER > 0
		unsigned char stream_buffer[FC_QLZ_STREAMING_BUFFER];
	#endif
	size_t stream_counter;
	fc_qlz_hash_compress hash[FC_QLZ_HASH_VALUES];
	unsigned char hash_counter[FC_QLZ_HASH_VALUES];
} fc_qlz_state_compress;


#if FC_QLZ_COMPRESSION_LEVEL == 1 || FC_QLZ_COMPRESSION_LEVEL == 2
	typedef struct
	{
#if FC_QLZ_STREAMING_BUFFER > 0
		unsigned char stream_buffer[FC_QLZ_STREAMING_BUFFER];
#endif
		fc_qlz_hash_decompress hash[FC_QLZ_HASH_VALUES];
		unsigned char hash_counter[FC_QLZ_HASH_VALUES];
		size_t stream_counter;
	} fc_qlz_state_decompress;
#elif FC_QLZ_COMPRESSION_LEVEL == 3
	typedef struct
	{
#if FC_QLZ_STREAMING_BUFFER > 0
		unsigned char stream_buffer[FC_QLZ_STREAMING_BUFFER];
#endif
#if FC_QLZ_COMPRESSION_LEVEL <= 2
		fc_qlz_hash_decompress hash[FC_QLZ_HASH_VALUES];
#endif
		size_t stream_counter;
	} fc_qlz_state_decompress;
#endif


#if defined (__cplusplus)
extern "C" {
#endif

// Public functions of QuickLZ
size_t fc_qlz_size_decompressed(const char *source);
size_t fc_qlz_size_compressed(const char *source);
size_t fc_qlz_compress(const void *source, char *destination, size_t size, fc_qlz_state_compress *state);
size_t fc_qlz_decompress(const char *source, void *destination, fc_qlz_state_decompress *state);
int fc_qlz_get_setting(int setting);

#if defined (__cplusplus)
}
#endif

#endif

