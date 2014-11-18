/**************************************************//**
@file fc/fc0log.c
Flash Cache log

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0log_h
#define fc0log_h

#include "univ.i"
#include "fc0fc.h"

#define FLASH_CACHE_LOG_WRITE        0
#define FLASH_CACHE_LOG_UPDATE_WRITE		1
#define FLASH_CACHE_LOG_UPDATE_DUMP			2
#define FLASH_CACHE_LOG_UPDATE_SHUTDOWN		4
#define FLASH_CACHE_LOG__INIT		128

#define FLASH_CACHE_BUFFER_SIZE			512UL

#define FLASH_CACHE_LOG_CHKSUM				0

/* current write offset/round and flush offset/round*/
#define FLASH_CACHE_LOG_FLUSH_OFFSET			4
#define FLASH_CACHE_LOG_WRITE_OFFSET			8
#define FLASH_CACHE_LOG_FLUSH_ROUND			12
#define FLASH_CACHE_LOG_WRITE_ROUND			16

#define FLASH_CACHE_LOG_WRITE_MODE			20
#define FLASH_CACHE_LOG_ENABLE_WRITE			24	

#define FLASH_CACHE_LOG_WRITE_ROUND_BCK		28
#define FLASH_CACHE_LOG_WRITE_OFFSET_BCK		32

/* write offset/round and flush offset/round when dump block metadata*/
#define FLASH_CACHE_LOG_FLUSH_OFFSET_DUMP	36
#define FLASH_CACHE_LOG_WRITE_OFFSET_DUMP	40
#define FLASH_CACHE_LOG_FLUSH_ROUND_DUMP		44
#define FLASH_CACHE_LOG_WRITE_ROUND_DUMP	48

#define FLASH_CACHE_LOG_BLOCK_BYTE_SIZE		52
#define FLASH_CACHE_LOG_VERSION				56
#define FLASH_CACHE_LOG_BEEN_SHUTDOWN		60
#define FLASH_CACHE_LOG_SKIPED_BLOCKS		64
#define FLASH_CACHE_LOG_COMPRESS_ALGORITHM	68

#define FLASH_CACHE_LOG_CHKSUM2			(FLASH_CACHE_BUFFER_SIZE - 4)

#define FLASH_CACHE_LOG_CHECKSUM		4294967291UL
#define FLASH_CACHE_VERSION_INFO_V4	55304UL
#define FLASH_CACHE_VERSION_INFO_V5	55305UL
#define FLASH_CACHE_VERSION_INFO_V61 56191UL


typedef struct fc_log_stat_struct		 fc_log_stat_t;
typedef struct fc_log_struct 			fc_log_t;

extern fc_log_t* fc_log;

/** flash cache log statment of write/flush_offset/round info struct */
struct fc_log_stat_struct{
	ulint		write_round; 		
	ulint		flush_round;			
	ulint		write_offset;			
	ulint		flush_offset;			
};

/** Flash cache log */
struct fc_log_struct
{
#ifdef __WIN__
	void*	file;				/*<! file handle */
#else
	int			file;			/*<! file handle */
#endif
	char*		log_file_path_name;
	ib_mutex_t	log_mutex;	/*!< mutex protecting fc_log_struct */
	byte*		buf;			/*<! log buffer(512 bytes) */
	byte*		buf_unaligned;	/*<! unaligned log buffer */

	/*<! flash cache flush offset/round write offset/round current */	
	fc_log_stat_t* current_stat;
	
	/*<! flush offset/round write offset/round when dump block metadata to dump file */
	fc_log_stat_t* dump_stat;
	
	/*<! write offset/round when last time
	 	 switch enable_write from false to true, for recovery */
	ulint		write_offset_bck;	
	ulint		write_round_bck;
	
	/*<! current enable_write value, update when enable_write is changed */
	ulint		enable_write_curr;

	ulint 		blk_size;		/* <! flash cache block size in KB */
	ulint 		blk_find_skip;	/* <! how many blocks have been skip for aio read when write page to cache a time, for recovery */   
	ulint		log_verison;	/* <! log verison, start from innosql-5.5.30-v4,
									before this the version is zero */

	/* <! compress algorithm, start from innosql-5.5.30-v5, before this the value is zero */								
	ulint		compress_algorithm;

	/*<!if L2 Cache shutdown correctly, this value is  TRUE, else is FALSE,
	 	set it TRUE when shutdown*/
	ibool		been_shutdown;

	/*<! whether flash cache is used the first time.
	 	if true, can do tablespace warmup,and no recovery is done */
	ibool		first_use;
};

/****************************************************************//**
Initialize flash cache log.*/
UNIV_INTERN
void
fc_log_update(
/*==================*/
	ulint init,	/*!< in: TRUE if this is first update after L2 Cache start */
	ulint flag); /*!< in: fc log update type */

/*********************************************************************//**
write flash cache log to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit(void);
/*=====================*/

/****************************************************************//**
Initialize flash cache log.*/
UNIV_INTERN
void
fc_log_create(void);
/*=====================*/

/****************************************************************//**
Free flash cache log.*/
UNIV_INTERN
void
fc_log_destroy(void);
/*===================*/

/*********************************************************************//**
write flash cache log blk_find_skip value to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit_for_skip_block(void);
/*=====================*/

/*********************************************************************//**
write flash cache log flush_off/flush_round value to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit_when_update_flushoff(void);
/*=====================*/

/*********************************************************************//**
Commit log when write_off is updated by doublewrite or lru move&migrate.*/
UNIV_INTERN
void
fc_log_commit_when_update_writeoff(void);
/*==========================*/

/********************************************************************//**
Update status after flush the fc log to disk, fc mutex should hold out of the function. */
UNIV_INTERN
void
fc_log_update_commit_status(void);
/*==========================*/

/******************************************************************//**
Reset fc_log dump_stat when change enable_dump flag from TRUE to FALSE */
UNIV_INLINE
void
fc_log_reset_dump_stat(void);
/*==============================*/

#ifndef UNIV_NONINL
#include "fc0log.ic"
#endif

#endif
