/**************************************************//**
@file fc/fc0fc.h
Flash Cache for InnoDB

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0fc_h
#define fc0fc_h

#include "univ.i"
#include "fil0fil.h"
#include "os0sync.h"
#include "ha0ha.h"
#include "fc0type.h"
#include "trx0sys.h"
#include "buf0buf.h"
#include "fc0quicklz.h"

#ifndef _WIN32
#include "fc0snappy.h"
#endif

#define KILO_BYTE 					1024
#define PAGE_SIZE_KB				(UNIV_PAGE_SIZE / KILO_BYTE)
#define KILO_BYTE_SHIFT 			10
#define FC_LEAST_AVIABLE_BLOCK_FOR_RECV		(256 * (PAGE_SIZE_KB / fc_get_block_size()))
#define FC_FIND_BLOCK_SKIP_COUNT 	1024
#define FC_BLOCK_MM_NO_COMMIT 		8192

#define FC_SRV_PEND_IO_THRESHOLD	(PCT_IO(3))
#define FC_SRV_RECENT_IO_ACTIVITY	(PCT_IO(5))
#define FC_SRV_PAST_IO_ACTIVITY		(PCT_IO(200))

#define FC_BATCH_WRITE				1
#define FC_SINGLE_WRITE				2

/*type of the compress algorithm supported in L2 cache, */
#define FC_BLOCK_COMPRESS_QUICKLZ	1	/*the default compress algorithm*/
#define FC_BLOCK_COMPRESS_SNAPPY	2	/*will support soon*/
#define FC_BLOCK_COMPRESS_ZLIB		5	/*will support soon*/


#define	FC_ZIP_PAGE_CHECKSUM	4294967291UL
#define	FC_ZIP_PAGE_HEADER		0
#define	FC_ZIP_PAGE_SIZE		4
#define	FC_ZIP_PAGE_SPACE		8
#define	FC_ZIP_PAGE_OFFSET		12
#define	FC_ZIP_PAGE_ORIG_SIZE	16
#define	FC_ZIP_PAGE_ZIP_ALG		20
#define	FC_ZIP_PAGE_ZIP_RAW_SIZE	24

#define	FC_ZIP_PAGE_DATA		64
#define	FC_ZIP_PAGE_TAILER		4 /*back from the page tail*/

#define FC_ZIP_PAGE_META_SIZE	68
#define FC_ZIP_COMPRESS_BUF_SIZE	(2 * UNIV_PAGE_SIZE)


/*
 * review:
 * take buf_io_fix as example
 * FC_IO_NONE  0
 * FC_IO_READ  1
 * FC_IO_WRITE 2
 * FC_IO_DOUBLEWRITE 32
 * FC_IO_LRU	64
 * use FC_IO_WRITE & FC_IO_DOUBLEWRITE to distinguish write type
 */
/** flash cache block io fix state */
#define 	IO_FIX_NO_IO		0x00	/*!< block is not in io */
#define 	IO_FIX_READ			0x01	/*!< block read hit in L2 Cache */
#define 	IO_FIX_DOUBLEWRITE	0x02	/*!< block data is writing in by doublewrite */
#define 	IO_FIX_FLUSH 		0x04	/*!< this block is dirty and is being flushed into disk */
//#define 	IO_FIX_READING 		0x10	/*!< text */

/** flash cache block decompress state when doing decompress */
#define 	DECOMPRESS_READ_SSD	1	/*!< read hit decompress */
#define 	DECOMPRESS_FLUSH	2	/*!< decompress for flush compress dirty page */
#define 	DECOMPRESS_BACKUP	3	/*!< decompress for backup compress dirty page */
#define 	DECOMPRESS_RECOVERY	4	/*!< decompress when recovery */

/** flash cache block decompress state when doing decompress */
#define 	UPDATE_GLOBAL_STATUS	1	/*!< for 'show global status' */
#define 	UPDATE_INNODB_STATUS	2	/*!< for 'show engine innodb status' */

/** flash cache variables */
extern long long	srv_flash_cache_size;
extern ulong	srv_flash_cache_block_size;
extern ulint	srv_flash_read_cache_size;
extern char*	srv_flash_cache_file;
extern char*	srv_flash_cache_warmup_table;
extern char*	srv_flash_cache_warmup_file;
extern my_bool	srv_flash_cache_enable_move;
extern my_bool	srv_flash_cache_enable_migrate;
extern my_bool	srv_flash_cache_enable_dump;
extern my_bool	srv_flash_cache_is_raw;
extern my_bool	srv_flash_cache_adaptive_flushing;
extern ulong	srv_fc_io_capacity;
extern my_bool  srv_flash_cache_enable_write;
extern my_bool  srv_flash_cache_safest_recovery;
extern my_bool  srv_flash_cache_backuping;
extern char*    srv_flash_cache_backup_dir;
extern char*    srv_flash_cache_log_dir;
extern ulong  	srv_flash_cache_write_mode;
extern my_bool  srv_flash_cache_fast_shutdown;
extern ulint 	srv_fc_flush_last_commit;
extern ulint  	srv_fc_flush_last_dump;
extern ulint 	srv_fc_flush_should_commit_log_flush;
extern ulint 	srv_fc_flush_should_commit_log_write;
extern my_bool 	srv_flash_cache_enable_compress;
extern my_bool 	srv_fc_flush_thread_exited;
extern ulong 	srv_flash_cache_compress_algorithm;
extern my_bool 	srv_flash_cache_decompress_use_malloc;
extern ulong 	srv_flash_cache_version;

/** flash cache status */
extern ulint	srv_flash_cache_read;
extern ulint	srv_flash_cache_write;
extern ulint	srv_flash_cache_single_write;
extern ulint	srv_flash_cache_flush;
extern ulint	srv_flash_cache_dirty;
extern ulint	srv_flash_cache_merge_write;
extern ulint	srv_flash_cache_move;
extern ulint	srv_flash_cache_pages_per_read;
extern ulong	srv_flash_cache_write_cache_pct;
extern ulong	srv_flash_cache_do_full_io_pct;
extern ulong	srv_fc_full_flush_pct;
extern ulong	srv_fc_write_cache_flush_pct;

extern ulong	srv_flash_cache_move_limit;
extern ulint	srv_flash_read_cache_page;
extern ulint	srv_flash_cache_read_detail[FIL_PAGE_TYPE_LAST+1];
extern ulint	srv_flash_cache_write_detail[FIL_PAGE_TYPE_LAST+1];
extern ulint	srv_flash_cache_flush_detail[FIL_PAGE_TYPE_LAST+1];
extern ulint	srv_flash_cache_used;
extern ulint	srv_flash_cache_used_nocompress;
extern ulint	srv_flash_cache_migrate;
extern ulint	srv_flash_cache_aio_read;
extern ulint	srv_flash_cache_wait_aio;
extern ulint 	srv_flash_cache_compress;
extern ulint 	srv_flash_cache_decompress;
extern ulint	srv_flash_cache_compress_pack;

extern my_bool		srv_flash_cache_load_from_dump_file;
extern const char 	srv_flash_cache_log_file_name[16];
extern const char*	srv_flash_cache_thread_op_info;

extern flash_cache_stat_t flash_cache_stat_global;
extern flash_cache_stat_t flash_cache_stat;

extern fc_t*	fc;

enum flash_cache_write_mode_enum{
	WRITE_BACK,
	WRITE_THROUGH
};

/* flash cache block status */
enum flash_cache_block_state{
	BLOCK_NOT_USED,			/*!< block not used */
	BLOCK_READY_FOR_FLUSH,	/*!< ready for flush to disk */
	BLOCK_READ_CACHE,		/*!< block migrate or warmup to flash cache */
	BLOCK_FLUSHED,			/*!< block has been flushed */
};

/** flash cache buffer struct for buffer flash cache blocks when flush or lru move */
struct fc_buf_struct{
	byte*	unalign; 		/*!< unalign buf */
	byte*	buf;			/*!< buf for block data */
	ulint	size;			/*!< buf size, with n flash cache blocks */
	ulint	free_pos;		/*!< first buf free position, with n flash cache blocks */
};

struct fc_block_array_struct{
	fc_block_t* block;		/** flash cache block */
};

/** flash cache block struct */
struct fc_block_struct{
	ib_mutex_t	mutex;		/*!<mutex protecting the block */
	ulint	space:32;		/*!< tablespace id */
	ulint	offset:32;		/*!< page number */
	ulint	fil_offset:32;	/*!< flash cache page number */
	ulint	size:6;			/*!< size of innodb page before l2 cahce zip, with n flash blocks */
	//ulint	zip_size:8;		/*!< size of innodb page after l2 cache zip, with n flash blocks */
	ulint	is_v4_blk:1;	/*!< if this block is load from InnoSQL 5.5.30-v4 */
	ulint	raw_zip_size:15;/*!< size of innodb page after l2 cache zip, with n byte */
	ulint	state:5;		/*!< flash cache block state */
	ulint	io_fix:5; 		/*!< the io status */
	void*	read_io_buf; /*!<used in read to store the compressed data from ssd*/
	fc_block_t* hash;		/*!< hash chain */
};

struct fc_page_info_struct{
	ulint	space:32;		/*!< tablespace id */
	ulint	offset:32;		/*!< page number */
	ulint	size:8;		/*!< size of innodb page before l2 cahce zip, with n flash blocks */
//	ulint	zip_size:8;	/*!< size of innodb page after l2 cache zip, with n flash blocks */
	ulint	raw_zip_size:24;
	ulint   fil_offset:32; /*!< offset of the cache file to store this (space, offset) page */
};

/** flash cache struct */
struct fc_struct{
    byte            fc_pad1[64];	/* padding to prevent other memory update
								 hotspots from residing on the same memory cache line */
	rw_lock_t		hash_rwlock;	/* rw lock protecting flash cache hash table */								 
	hash_table_t*	hash_table; 	/*!< hash table of flash cache blocks */
	ulint			size; 			/*!< flash cache size, with n flash cache blocks */
	ulint			block_size; 		/*!< the init block size set by user, with n KB*/
	
    byte            fc_pad2[64];
	ulint			write_off; 		/*!< write to flash cache offset , with n cache blocks */
	ulint			flush_off; 		/*!< flush to disk this offset, with n cache blocks */
	ulint			write_round; 	/*!< write round */
	ulint			flush_round; 	/*!< flush round */
	ib_mutex_t		mutex; 			/*!< mutex protecting write/flush_off/round */
	
	fc_block_array_t* 	block_array; 			/*!< flash cache block array */

	/******** used for flush dirty L2 Cache data to disk */
	ulint			n_flush_cur;	/* how many block will be flush at this flush ops */
	fc_buf_t*		flush_buf; /*!< store the flush async (decompressed) data
													when write dirty page to disk */
	void*	flush_dezip_state;	/*!< used to buf the state of decompress
									when doing decompress in flush */
	byte*	flush_zip_read_buf_unalign;
	byte*	flush_zip_read_buf; /*!< store the compressed data read from ssd when flush */
	os_event_t		wait_space_event;/*!< Condition event to wait fc space for writing */
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	os_event_t		wait_doublewrite_event;/*!< Condition event to wait doublewrite launched aio for move */
#endif

	/******** used for doublewrite data compress */
	fc_page_info_t*	dw_pages;/*!< temply store the doublewrite blocks info of compressed */
	byte*	dw_zip_buf_unalign;
	byte*	dw_zip_buf;	/*!< temply store the compressed data for doublewrite buffer */
	void*	dw_zip_state;	/*!< used to buf the state of compress
							when doing compress in doublewrite */

	/******** used for recovery or backup data decompress */
	void*	recv_dezip_state;	/*!< used to buf the state of decompress */
	byte*	recv_dezip_buf_unalign;
	byte*	recv_dezip_buf; /*!< store the decompressed data read from ssd when recovery */
	ulint	is_finding_block;
	
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	ulint	is_doing_doublewrite;
#endif
	
#ifdef UNIV_FLASH_CACHE_TRACE
	FILE*	f_debug;
#endif
};

struct flash_cache_stat_struct{
	ulint n_pages_write;
	ulint n_pages_flush;
	ulint n_pages_merge_write;
	ulint n_pages_read;
	ulint n_pages_migrate;
	ulint n_pages_move;
	ulint n_buf_pages_read;
	time_t last_printout_time;
};


#define flash_cache_mutex_enter() (mutex_enter(&fc->mutex))
#define flash_cache_mutex_exit()  (mutex_exit(&fc->mutex))

/* we should first get fc mutex and then log_mutex to avoid deadlock */
#define flash_cache_log_mutex_enter() (mutex_enter(&fc_log->log_mutex))
#define flash_cache_log_mutex_exit()  (mutex_exit(&fc_log->log_mutex))

#define flash_block_mutex_enter(offset) mutex_enter(&(fc->block_array[offset].block->mutex))
#define flash_block_mutex_exit(offset) mutex_exit(&(fc->block_array[offset].block->mutex))

/**************************************************************//**
Check whether flash cache is enable.*/
UNIV_INLINE
my_bool
fc_is_enabled(void);
/*=========*/

/**************************************************************//**
Get flash cache size
@return number of flash cache blocks */
UNIV_INLINE
ulint
fc_get_size(void);
/*=========*/

/**************************************************************//**
Get flash cache block size
@return size of flash cache block, with n KB*/
UNIV_INLINE
ulint
fc_get_block_size(void);
/*=========*/

/**************************************************************//**
Get flash cache block size
@return size of flash cache block, with n B*/
UNIV_INLINE
ulint
fc_get_block_size_byte(void);
/*=========*/

/**************************************************************//**
Set flash cache size, in n cache blocks. */
UNIV_INLINE
void
fc_set_size(
/*================*/
	ulint size);	/*!< in: flash cache size */

/* for use of fc_block  sort*/
#define ASCENDING 	0
#define DESCENDING 	1

/**************************************************************//**
For use of fc_block_sort, sort block with space and offset in ascend order
@return: the compare result of two pointers */
UNIV_INLINE
int 
fc_page_cmp_ascending(
/*=====================*/
	const void* p1, /*!< in: data pointer */
	const void* p2); /*!< in:  data pointer */

/**************************************************************//**
For use of fc_block_sort, sort block with space and offset in descend order
@return: the compare result of two pointers */
UNIV_INLINE
int 
fc_page_cmp_descending(
/*=====================*/
	const void* p1, /*!< in: data pointer */
	const void* p2); /*!< in:  data pointer */

/**************************************************************//**
Get flash cache available block numbers
@return number of available flash cache blocks*/
UNIV_INLINE
ulint
fc_get_available(void);
/*=========*/

/******************************************************************//**
Get distance between flush offset and write offset .
@return	number of pages*/
UNIV_INLINE
ulint
fc_get_distance(void);
/*==================*/

/**************************************************************//**
Sort block with space and offset in descend or ascend order*/
UNIV_INLINE
void 
fc_block_sort(
/*=======================*/
	fc_block_t** base, 	/*!< in: blocks pointers array for sort */
	ulint len, 			/*!< in: the number of blocks to be sorted */
	ulint type);		/*!< in: sort with descend or ascend order */

/******************************************************************//**
Delete the delete_block from hash table, make sure the caller
have hold the hash table lock. */
UNIV_INLINE
void
fc_block_delete_from_hash(
/*============================*/
	fc_block_t* delete_block);	/*!< in: the L2 Cache block need to
								be delete from hash table */

/******************************************************************//**
Search the block in hash table, make sure the caller have hold the hash table lock.
@return	the L2 Cache block, if is in hash table. else return NULL*/
UNIV_INLINE
fc_block_t*
fc_block_search_in_hash(
/*==================*/
	ulint space,	/*!< in: the space id which we search in block */
	ulint offset);	/*!< in: the space offset which we search in block */

/******************************************************************//**
Insearch the insert_block into hash table, make sure the caller
have hold the hash table lock. */
UNIV_INLINE
void
fc_block_insert_into_hash(
/*========================*/
	fc_block_t* insert_block);	/*!< in: the L2 Cache block
									need to be insert into hash table */

/******************************************************************//**
Inc the fc write_off, inc the fc write_round if necessary.
make sure caller have hold the fc mutex */
UNIV_INLINE
void
fc_inc_write_off(
/*==================*/
	ulint inc_count); /*!< in: add that many offset in fc write_off */

/******************************************************************//**
Inc the fc flush_off, inc the fc flush_round if necessary.
make sure caller have hold the fc mutex */
UNIV_INLINE
void
fc_inc_flush_off(
/*==================*/
	ulint inc_count); /*!< in: add that many offset in fc flush_off */

/******************************************************************//**
Check a block metadata with the packed data buffer */ 
UNIV_INLINE
void  
fc_block_compress_check(
/*=======================*/
	byte* buf, /*!< in: the compress data buffer */
	fc_block_t* block); /*!< in: the block need to check */

/******************************************************************//**
Find a L2 Cache block to write, if the block is io_fixed, we will wait
@return: the block found for write */
UNIV_INLINE
fc_block_t*  
fc_block_find_replaceable(
/*=======================*/
	ulint must_find, 		 /*!< in: if must find a block */
	ulint block_size);  /*!< in: allocate this many block. block size,
							with n flash cache blocks*/

/******************************************************************//**
Exchange a L2 Cache block fil_offset into block_offset and byte_offset
that is suitable for fil_io */
UNIV_INLINE
void
fc_io_offset(
/*==================*/
	ulint fil_offset,   /*<! in: L2 Cache block fil_offset */
	ulint* block_offset,/*<! out: L2 Cache block block offset suitable for fil_io */
	ulint* byte_offset);	/*<! out: L2 Cache block byte offset suitable for fil_io */

/******************************************************************//**
Calc the L2 Cache block size in kb from the zip size
@return	the L2 Cache block real size in kb */
UNIV_INLINE
ulint
fc_calc_block_size(
/*==================*/
	ulint zip_size); /*<! in: L2 Cache block size in bytes */

/******************************************************************//**
Wait for space to write block, this function will not release the cache mutex */
UNIV_INLINE
void
fc_wait_for_space(void);
/*==================*/

/******************************************************************//**
Wait for doublewrite async all launched , this function will release the cache mutex */
UNIV_INLINE
void
fc_wait_for_aio_dw_launch(void);
/*==================*/

/******************************************************************//**
Validate if the dirty page is the same with the srv_flash_cache_dirty, caller should
hold the cache mutex, at this time fc_flush_to_disk is not working */
UNIV_INLINE
void
fc_validate(void);
/*==================*/

/******************************************************************//**
Init the L2 Cache block when create L2 Cache */
UNIV_INLINE
fc_block_t*
fc_block_init(
/*==================*/
	ulint fil_offset);	   /*<! in: the L2 Cache block fil_offset init to */

/******************************************************************//**
free the L2 Cache block */
UNIV_INLINE
void
fc_block_free(
/*==================*/
	fc_block_t* block);	   /*<! in: the L2 Cache block to free */

/**************************************************************//**
Get flash cache block from block offset
@return NULL */
UNIV_INLINE
fc_block_t*
fc_get_block(
/*=========*/
ulint fil_offset); /*<! in: L2 Cache block offset in ssd */


/******************************************************************//**
Print the L2 Cache block values */
UNIV_INLINE
void
fc_block_print(
/*==================*/
	fc_block_t* block); /*<! in: L2 Cache block to print out */

/******************************************************************//**
Print the L2 Cache used blocks */
UNIV_INLINE
void
fc_print_used(void);
/*==================*/


/******************************************************************//**
Print the L2 Cache round/offset values */
UNIV_INLINE
void
fc_round_print(void);
/*==================*/

/******************************************************************//**
Test if the doublewrite buf block data is corrupted
@return	TRUE if the data is corrupted*/ 
UNIV_INLINE
ulint
fc_dw_page_corrupted(
/*==================*/
	buf_block_t* dw_block); /*<! in: buf block to test if is corrupted */

/********************************************************************//**
Test if the compress is helpfull.
@return TRUE if compress successed */
UNIV_INLINE
ulint
fc_block_compress_successed(
/*==================*/
	ulint cp_size); /*!< in: the page size after compress */

/******************************************************************//**
Test if the buf page should be compress by L2 Cache.
@return: return TRUE if the page should be compressed */
UNIV_INLINE
ibool
fc_block_need_compress(
/*=====================*/
	ulint space_id); /*!< in: space_id of the page */

/******************************************************************//**
Test if the L2 Cache block has been compressed by L2 Cache.
@return: return TRUE if the page has been compressed */
UNIV_INLINE
ibool
fc_block_need_decompress(
/*=======================*/
	fc_block_t* block); /*!< in: L2 Cache block */

/********************************************************************//**
Compress the buf page bpage, return the size of compress data.
the buf memory has alloced
@return the compressed size of page */
UNIV_INTERN
ulint
fc_block_do_compress(
/*==================*/
	ulint is_dw,		/*!< in: TRUE if compress for doublewrite buffer */
	buf_page_t* bpage, 	/*!< in: the data need compress is bpage->frame */
	void*	buf);		/*!< out: the buf contain the compressed data,
							must be the size of frame + 400 */

/********************************************************************//**
Decompress the page in the block, return the decompressed data size.
@return the decompressed size of page, must be UNIV_PAGE_SIZE */
UNIV_INTERN
ulint
fc_block_do_decompress(
/*==================*/
	ulint decompress_type, /*!< in: decompress for read or backup or flush or recovery */
	void *buf_compressed,	/*!< in: contain the compressed data */
	ulint compressed_size,  /*!< in: the compressed buffer size */
	void *buf_decompressed);	/*!< out: contain the data that have decompressed */

/**********************************************************************//**
Align the compress size with base fc block size, return the number of blocks
@return: return the aligned size of the compressed block */
UNIV_INLINE
ulint
fc_block_compress_align(
/*=======================*/
	ulint	size_unalign); /*!< in: the compressed L2 Cache block size before align */

UNIV_INLINE
void
fc_block_set_compress_type(
	ulong	compress_type
);

UNIV_INLINE
ulong
fc_block_get_compress_type(
);

/********************************************************************//**
Write compress algrithm to the compress data buffer. */
UNIV_INTERN
void
fc_block_write_compress_alg(
/*==================*/
	ulint compress_algrithm,/*!< in: the compress algrithm to write */
	void *buf);				/*!< in/out: the compressed data buf need to write */

/********************************************************************//**
Read compress algrithm from compressed data buffer. 
@return the compress algrithm of page */
UNIV_INTERN
ulint
fc_block_read_compress_alg(
/*==================*/
	void *buf);				/*!< in: the compressed data buf */
	
/********************************************************************//**
Pack the compressed data with block header and tailer. */
UNIV_INTERN
void
fc_block_pack_compress(
/*==================*/
	fc_block_t* block,	/*!< in: the block which is compressed, ready for pack */
	void *buf); 		/*!< in/out: the compressed data buf need to pack */

/********************************************************************//**
Get the data size of the block, either blk_size or blk_zip_size.
@return: the number of base L2 Cache block */
UNIV_INLINE
ulint
fc_block_get_data_size(
/*=======================*/
	fc_block_t* block); /*!< in: the L2 Cache block we want to get its size */

/********************************************************************//**
Get the data size of the block, the size before compressed by L2 Cache.
@return: the number of base L2 Cache block */
UNIV_INLINE
ulint
fc_block_get_orig_size(
/*=======================*/
	fc_block_t* block); /*!< in: the L2 Cache block we want to
							get its original size */

/******************************************************************//**
Update L2 Cache status for innodb status or global status */
UNIV_INLINE
void
fc_update_status(
/*==================*/
ulint status_type); /*<! in: innodb status or global status*/

/**************************************************************//**
Initialize flash cache struct.*/
UNIV_INTERN
void
fc_create(void);
/*=========*/

/**************************************************************//**
Start flash cache.*/
UNIV_INTERN
void
fc_start(ulint fc_need_recv);
/*=========*/

/******************************************************************//**
Dump blocks from L2 Cache to file*/
UNIV_INTERN
void
fc_dump(void);
/*==================*/

/**************************************************************//**
Free L2 Cache struct.*/
UNIV_INTERN
void
fc_destroy(void);
/*=========*/

/******************************************************************//**
Load L2 Cache from dump file */
UNIV_INTERN
void
fc_load(void);
/*==================*/

/********************************************************************//**
When srv_flash_cache_enable_write is FALSE, doublewrite buffer will behave as deault. 
So if page need flush now(newer) is also in L2 Cache already(olded),
it must be removed  from the L2 Cache before doublewrite write to disk.
@return: if removed in L2 Cache */
UNIV_INTERN
ulint
fc_block_remove_single_page(
/*==================*/
	buf_page_t* bpage);/*!< in: bpage need flush */

/********************************************************************//**
When srv_flash_cache_enable_write is FALSE, doublewrite buffer will behave as deault. 
So if any page in doublewrite buffer now(newer) is also in L2 Cache already(olded),
it must be removed  from the L2 Cache before doublewrite buffer write to disk.
@return: pages removed in L2 Cache */
UNIV_INTERN
ulint
fc_block_remove_from_hash(
/*==================*/
	buf_dblwr_t* trx_dw);/*!< in: doublewrite structure */

/********************************************************************//**
Writes a page to the to Cache and sync it. if sync write, call io complete */
UNIV_INTERN
void
fc_write_single_page(
/*========================*/
	buf_page_t*	bpage,	/*!< in: buffer block to write */
	bool		sync);	/*!< in: true if sync IO requested */

/********************************************************************//**
Flush double write buffer to L2 Cache block.no io will read or write the ssd block which
is writed from doublewriter buffer. as io will hit the buf pool or
doublewriter until the function exit
@return: count of async read L2 Cache block*/
UNIV_INTERN
void
fc_write(
/*===========================*/
	buf_dblwr_t* trx_dw);	/*!< in: doublewrite structure */

/********************************************************************//**
Flush a batch of writes to the datafiles that have already been
written by the L2 Cache. */
UNIV_INTERN
void
fc_sync_fcfile(void);
/*===========================*/

/********************************************************************//**
Read page from L2 Cache block, if not found in L2 Cache, read from disk.
Note: ibuf page must read in aio mode to avoid deadlock
@return DB_SUCCESS is success, 1 if read request is issued. 0 if it is not */
UNIV_INTERN
dberr_t
fc_read_page(
/*==============*/
	ibool	sync,	/*!< in: TRUE if synchronous aio is desired */
	ulint	space,	/*!< in: space id */
	ulint	zip_size,/*!< in: compressed page size, or 0 */
	ibool	unzip,	/*!< in: TRUE=request uncompressed page */
	ulint	offset,	/*!< in: page number */
	ulint	wake_later,	/*!< in: wake later flag */
	void*	buf,		/*!< in/out: buffer where to store read data
				or from where to write; in aio this must be appropriately aligned */
	buf_page_t*	bpage);	/*!< in/out: read L2 Cache block to this page */

/********************************************************************//**
Compelete L2 Cache read. only read hit in ssd,
not move_migrate will into this function*/
UNIV_INTERN
void
fc_complete_read(
/*==============*/
	buf_page_t* bpage);	/*!< in: page to compelete io */

/********************************************************************//**
Print L2 Cache status. */
UNIV_INTERN
void
fc_status(
/*=================================*/
	ulint page_read_delta,	/*!< in: page_read_delta from buf pool */
	ulint n_ra_pages_read,	/*!< in: read ahead page counts */
	ulint n_pages_read,		/*!< in: read page counts */
	FILE* file);			/*!< in: print the fc status to this file */

#ifndef UNIV_NONINL
#include "fc0fc.ic"
#endif

#endif
