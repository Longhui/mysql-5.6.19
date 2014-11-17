/**************************************************//**
@file fc/fc0backup.h
Backup Flash Cache dirty blocks to other file

Created	24/10/2013 Thomas Wen(wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0backup_h
#define fc0backup_h

#include "univ.i"
#include "fc0fc.h"

typedef struct fc_bkp_info_struct		fc_bkp_info_t;
typedef struct fc_bkp_blkmeta_strcut	fc_bkp_blkmeta_t;

#define FC_BACKUP_FILE_PAGE_OFFSET	1
#define FC_ONE_EXTENT				(1 << 10)

/** flash cache backup file metadata struct */
struct fc_bkp_info_struct {
	ulint	bkp_page_pos;		/*!<the position store the first block data*/
	ulint	bkp_blk_meta_pos;	/*!< the position store the block metadata */
	ulint	bkp_blk_count;		/*!< the total number of blocks in backup file */
};

/** flash cache backup file block metadata struct */
struct fc_bkp_blkmeta_strcut {
	ulint	blk_space:32; 	/*!<block space id*/
	ulint	blk_offset:32; 	/*!<block offset in the space*/
	ulint	blk_size; 	  	/*!<block size, with kb*/
};

/********************************************************************//**
Print error info if creating backup file failed */
UNIV_INTERN
void
fc_bkp_print_create_error(
/*==================*/
	char* bkp_file_path);	/*!< in: backup file name */

/********************************************************************//**
Print error info if write dirty pages to backup file failed */
UNIV_INTERN
void
fc_bkp_print_io_error(void);
/*==================*/

/********************************************************************//**
Make file path for backup file */
UNIV_INTERN
void
fc_bkp_finish(
/*==================*/
	byte* unaligned_buf,	/*!< in: data buf used to buffer the backup page data */
	byte* compress_buf_unalign,	/*!< in: data buf used to buffer compressed page data */
	char* bkp_file_path,	/*!< in: backup file name */
	char* bkp_file_path_final);	/*!< in: backup file name final */

/********************************************************************//**
Backup pages haven't flushed to disk from flash cache file.
@return number of pages backuped if success */
UNIV_INTERN
ulint
fc_backup(
/*==================*/
	ibool* success);	/*!< out: if backup is sucessed */

/********************************************************************//**
fc_backup_thread */
UNIV_INTERN
os_thread_ret_t
fc_backup_thread(
/*==================*/
	void* args);	/*!< in: arguments for the thread */

#ifndef UNIV_NONINL
#include "fc0backup.ic"
#endif

#endif
