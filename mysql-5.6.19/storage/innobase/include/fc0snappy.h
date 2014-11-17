#ifndef _LINUX_SNAPPY_H
#define _LINUX_SNAPPY_H 1

#include <stdbool.h>
#include <stddef.h>

//copy for compat.h for snappy-c-master
#ifdef __FreeBSD__
#  include <sys/endian.h>
#elif !defined(__WIN32__)
#  include <endian.h>
#endif

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#ifndef __WIN32__
#include <sys/uio.h>
#endif

#include "univ.i"
#include "fc0fc.h"

#define get_unaligned_memcpy(x) ({ \
		typeof(*(x)) _ret; \
		memcpy(&_ret, (x), sizeof(*(x))); \
		_ret; })
#define put_unaligned_memcpy(v,x) ({ \
		typeof((v)) _v = (v); \
		memcpy((x), &_v, sizeof(*(x))); })

#define get_unaligned_direct(x) (*(x))
#define put_unaligned_direct(v,x) (*(x) = (v))

// Potentially unaligned loads and stores.
// x86 and PowerPC can simply do these loads and stores native.
#if defined(__i386__) || defined(__x86_64__) || defined(__powerpc__)

#define get_unaligned get_unaligned_direct
#define put_unaligned put_unaligned_direct
#define get_unaligned64 get_unaligned_direct
#define put_unaligned64 put_unaligned_direct
#endif

#define get_unaligned_le32(x) (le32toh(get_unaligned((u32 *)(x))))
#define put_unaligned_le16(v,x) (put_unaligned(htole16(v), (u16 *)(x)))

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;
typedef unsigned long long u64;

#define BUG_ON(x) assert(!(x))

#define vmalloc(x) malloc(x)
#define vfree(x) free(x)

#define EXPORT_SYMBOL(x)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

#define min_t(t,x,y) ((x) < (y) ? (x) : (y))
#define max_t(t,x,y) ((x) > (y) ? (x) : (y))

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define __LITTLE_ENDIAN__ 1
#endif

#if __LITTLE_ENDIAN__ == 1 && (defined(__LSB_VERSION__) || defined(__WIN32__))
#define htole16(x) (x)
#define le32toh(x) (x)
#endif

#define BITS_PER_LONG (__SIZEOF_LONG__ * 8)

/* Only needed for compression. This preallocates the worst case */
struct snappy_env {
	unsigned short *hash_table;
	void *scratch;
	void *scratch_output;
}; 

int snappy_init_env(struct snappy_env *env);
void snappy_free_env(struct snappy_env *env);
int snappy_uncompress(const char *compressed, size_t n, char *uncompressed);
int snappy_compress(struct snappy_env *env,
		    const char *input, size_t input_length,
		    char *compressed, size_t *compressed_length);
bool snappy_uncompressed_length(const char *buf, size_t len, size_t *result);
size_t snappy_max_compressed_length(size_t source_len);

#endif
