/**************************************************//**
@file fc/fc0log.c
Flash Cache log

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0log.h"

#ifdef UNIV_NONINL
#include "fc0log.ic"
#endif

#include "fc0fc.h"
#include "srv0start.h"

/* flash cache log structure */
UNIV_INTERN fc_log_t* fc_log = NULL;
/* flash cache key */
 UNIV_INTERN  mysql_pfs_key_t innodb_flash_cache_file_key;

/*********************************************************************//**
Creates or opens the flash cache data files and closes them.
@return	DB_SUCCESS or error code */
static
ulint
fc_open_or_create_file(void)
/*==========================*/
{	
	ibool	ret;
//	ulint	size;
//	ulint	size_high;
//	ulint	low32;
//	ulint	high32;
	os_offset_t	size;
	os_file_t file;
	os_offset_t flash_cache_size = (os_offset_t)srv_flash_cache_size;
	
//	low32 = (0xFFFFFFFFUL & (flash_cache_size << UNIV_PAGE_SIZE_SHIFT));
//	high32 = (flash_cache_size >> (32 - UNIV_PAGE_SIZE_SHIFT));
	
	file = os_file_create(innodb_flash_cache_file_key, srv_flash_cache_file,
		OS_FILE_CREATE, OS_FILE_NORMAL, OS_LOG_FILE, &ret);
	
	if (ret == FALSE) {
		if (os_file_get_last_error(FALSE) != OS_FILE_ALREADY_EXISTS
#ifdef UNIV_AIX
		/* AIX 5.1 after security patch ML7 may have errno set
		to 0 here, which causes our function to return 100;
		work around that AIX problem */
		&& os_file_get_last_error(FALSE) != 100
#endif
		) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: Error in creating or opening %s\n", srv_flash_cache_file);
			return(DB_ERROR);
		}
			
		file = os_file_create(innodb_flash_cache_file_key, srv_flash_cache_file,
					OS_FILE_OPEN, OS_FILE_AIO,OS_LOG_FILE, &ret);
		if (!ret) {
			ut_print_timestamp(stderr);
			fprintf(stderr,	" InnoDB: Error in opening %s\n", srv_flash_cache_file);
			return(DB_ERROR);
		}
		//ret = os_file_get_size(file, &size, &size_high);
		size = os_file_get_size(file);
		if (size < flash_cache_size) {
			ut_print_timestamp(stderr);
			fprintf(stderr,
				" InnoDB: Error: L2 Cache file %s is of smaller size %lu bytes\n"
				"InnoDB: than specified in the .cnf file %lu bytes!\n",
				srv_flash_cache_file, (ulong)size, (ulong)flash_cache_size);
			return(DB_ERROR);
		}
	} else {
		ut_print_timestamp(stderr);
		fprintf(stderr,"  InnoDB: L2 Cache file %s did not exist:new to be created\n",
				srv_flash_cache_file);
		
		fprintf(stderr, "InnoDB: Setting L2 Cache file %s size to %lu MB\n",
				srv_flash_cache_file, (ulong)flash_cache_size >> 20);
		
		fprintf(stderr, "InnoDB: Database physically writes the file full: wait...\n");
		
		ret = os_file_set_size(srv_flash_cache_file, file, flash_cache_size);
		if (!ret) {
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: Error in creating %s: probably out of disk space\n",
					srv_flash_cache_file);
			return(DB_ERROR);
		}
	}

	ret = os_file_close(file);
	return(DB_SUCCESS);
}

/****************************************************************//**
Initialize flash cache log.*/
UNIV_INTERN
void
fc_log_create(void)
/*=====================*/
{
	ulint ret;
	ulint path_len;
	char* log_dir;
	
	fc_log = (fc_log_t*)ut_malloc(sizeof(fc_log_t));
	memset(fc_log, '0', sizeof(fc_log_t));
	
	fc_log->current_stat = (fc_log_stat_t*)ut_malloc(sizeof(fc_log_stat_t));
	memset(fc_log->current_stat, '0', sizeof(fc_log_stat_t));
	
	fc_log->dump_stat = (fc_log_stat_t*)ut_malloc(sizeof(fc_log_stat_t));
	memset(fc_log->dump_stat, '0', sizeof(fc_log_stat_t));
	
	fc_log->buf_unaligned = (byte*)ut_malloc(FLASH_CACHE_BUFFER_SIZE * 2);
	fc_log->buf = (byte*)ut_align(fc_log->buf_unaligned,FLASH_CACHE_BUFFER_SIZE);

	mutex_create(PFS_NOT_INSTRUMENTED, &fc_log->log_mutex, SYNC_DOUBLEWRITE);

	log_dir = srv_flash_cache_log_dir ? srv_flash_cache_log_dir : srv_data_home;
	path_len = strlen(log_dir) + strlen(srv_flash_cache_log_file_name) + 2;

	fc_log->log_file_path_name = (char *)ut_malloc(path_len);
	ut_snprintf(fc_log->log_file_path_name, path_len, 
		"%s%s", log_dir, srv_flash_cache_log_file_name);
	
	srv_normalize_path_for_win(fc_log->log_file_path_name);

	fc_log->file = os_file_create(innodb_file_data_key, fc_log->log_file_path_name,
						OS_FILE_CREATE, OS_FILE_NORMAL, OS_DATA_FILE, &ret);
	
	if (ret) {
		/* create file success, it is the first time to create log file. */
		memset(fc_log->buf, '\0', FLASH_CACHE_BUFFER_SIZE);
		
		fc_log->enable_write_curr = (ulint)srv_flash_cache_enable_write;
		fc_log->write_round_bck = 0;		
		fc_log->write_offset_bck = 0XFFFFFFFFUL;/* for recovery */
		
		/* init the flush/write offset/round, this is update by doublewrite or flush thread */
		fc_log->current_stat->flush_offset = 0;
		fc_log->current_stat->flush_round = 0;
		fc_log->current_stat->write_offset = 0;
		fc_log->current_stat->write_round = 0;
		
		/* init the flush/write offset/round of last block metadata dump*/
		fc_log->dump_stat->flush_offset = 0;
		fc_log->dump_stat->flush_round = 0;
		fc_log->dump_stat->write_offset = 0;
		fc_log->dump_stat->write_round = 0;	
		
		fc_log->blk_find_skip = 0;	
		fc_log->blk_size = srv_flash_cache_block_size;
		fc_log->log_verison = srv_flash_cache_version;
		fc_log->compress_algorithm = srv_flash_cache_compress_algorithm;		

		/* log will be flushed when finish fc_start */
		//os_file_write(fc_log->log_file_path_name, fc_log->file, fc_log->buf, 0, 0, 
		//	FLASH_CACHE_BUFFER_SIZE);
		//os_file_flush(fc_log->file);
		
		fc_log->first_use = TRUE;

	} else {
		/* we need to open the file */
		fc_log->file = os_file_create(innodb_file_data_key, fc_log->log_file_path_name,
							OS_FILE_OPEN, OS_FILE_NORMAL, OS_DATA_FILE, &ret);
		if (!ret) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB [Error]: Can't open L2 Cache log: %lu.\n", ret);
			ut_error;
		}
		os_file_read(fc_log->file, fc_log->buf, 0, FLASH_CACHE_BUFFER_SIZE);

		ut_a(mach_read_from_4(fc_log->buf+FLASH_CACHE_LOG_CHKSUM) == 
			 mach_read_from_4(fc_log->buf+FLASH_CACHE_LOG_CHKSUM2));

		/* don't allow to change block size */
		fc_log->blk_size = mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_BLOCK_BYTE_SIZE);
		if (fc_log->blk_size == 0) {
			fc_log->blk_size = UNIV_PAGE_SIZE;
		}

		/* don't allow to change cache block size */ 
		if (srv_flash_cache_block_size != fc_log->blk_size) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: Error!!!cann't change L2 Cache block size from"
				"%lu to %lu! we can't continue.\n", fc_log->blk_size, 
				srv_flash_cache_block_size);
			ut_error;
		} 

		/* don't allow to change compress algorithm */ 
		if ((srv_flash_cache_compress == TRUE) && (fc_log->compress_algorithm) 
			&& (srv_flash_cache_compress_algorithm != fc_log->compress_algorithm)) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: Error!!!cann't change L2 Cache compress algorithm from"
				"%lu to %lu! we can't continue.\n", fc_log->compress_algorithm, 
				srv_flash_cache_compress_algorithm);
			ut_error;
		} 
		
		/* don't allow to change write mode */
		if (srv_flash_cache_write_mode != (ulong)mach_read_from_4(fc_log->buf
			+ FLASH_CACHE_LOG_WRITE_MODE)) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: cann't change L2 Cache write mode from %lu to %lu,"
				" just ignore the change\n", 
				mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_MODE), 
				srv_flash_cache_write_mode);
			
			srv_flash_cache_write_mode = 
				(ulong)mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_MODE);
		}

		/* use fc_log in disk to init the fc_log memory object */

		/* load the flush/write offset/round, this is update by doublewrite or flush thread */
		fc_log->current_stat->flush_offset = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_OFFSET);
		fc_log->current_stat->flush_round = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_ROUND);
		fc_log->current_stat->write_offset = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET);
		fc_log->current_stat->write_round = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND);
		
		/* load the enable write flag when shutdown */
		fc_log->enable_write_curr = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_ENABLE_WRITE);
		
		fc_log->write_offset_bck = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET_BCK);
		fc_log->write_round_bck = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND_BCK);
		
		/* load the flush/write offset/round of last block metadata dump */
		fc_log->dump_stat->flush_offset = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_OFFSET_DUMP);
		fc_log->dump_stat->flush_round = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_ROUND_DUMP);
		fc_log->dump_stat->write_offset = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET_DUMP);
		fc_log->dump_stat->write_round = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND_DUMP);

		fc_log->log_verison = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_VERSION);

		fc_log->been_shutdown = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_BEEN_SHUTDOWN);

		fc_log->blk_find_skip = 
			mach_read_from_4(fc_log->buf + FLASH_CACHE_LOG_SKIPED_BLOCKS);	
		
		fc_log->first_use = FALSE;

		/* use fc_log in memory object to init the fc memory object */
		fc->write_round = fc_log->current_stat->write_round;
		fc->write_off = fc_log->current_stat->write_offset;
		fc->flush_off = fc_log->current_stat->flush_offset;
		fc->flush_round = fc_log->current_stat->flush_round;
		
	}
	
	if (!srv_flash_cache_is_raw) {
		fc_open_or_create_file();
	}

	ret = fil_space_create(srv_flash_cache_file, FLASH_CACHE_SPACE, 0, FIL_FLASH_CACHE);
	if (!ret) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB [Error]: fail to create L2 Cache file.\n");
		ut_error;
	} 

	fil_node_create(srv_flash_cache_file, srv_flash_cache_size, FLASH_CACHE_SPACE, 
			srv_flash_cache_is_raw);

}

/****************************************************************//**
Initialize flash cache log.
 * fc_log_update can use only one variables
 * variable init can be merged to flag with ORed FLASH_CACHE_LOG_INIT
 * for example:
 * fc_log_update(FLASH_CACHE_LOG_WRITE)
 * fc_log_update(FLASH_CACHE_LOG_WRITE & FLASH_CACHE_LOG_INIT)
 */
UNIV_INTERN
void
fc_log_update(
/*==================*/
	ulint init,	/*!< in: TRUE if this is first update after L2 Cache start */
	ulint flag) /*!< in: fc log update type */
{
	
	fc_log->current_stat->flush_offset = fc->flush_off;
	fc_log->current_stat->flush_round = fc->flush_round;
	fc_log->current_stat->write_offset = fc->write_off;
	fc_log->current_stat->write_round = fc->write_round;
	
	fc_log->enable_write_curr = srv_flash_cache_enable_write;

	fc_log->blk_find_skip = 0;

	/* 
	 * set the been_shutdown flag FALSE, so we can know if the L2 Cache
	 * is shutdown correctly when next start
	 */
	if (init == TRUE) {
		fc_log->write_offset_bck = 0XFFFFFFFFUL;
		fc_log->been_shutdown = FALSE;
	}

	/* update log when change enable_write from FALSE to TRUE */
	if (flag == FLASH_CACHE_LOG_UPDATE_WRITE) {

		fc_log->write_offset_bck = fc->write_off;
		fc_log->write_round_bck = fc->write_round;
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 Cache log write_offset/round_bck(%lu,%lu) updated.\n",
			(ulong)fc->write_off, (ulong)fc->write_round);
	}

	/* we should update the dump_stat when compelete period block metadata dump */
	if (flag == FLASH_CACHE_LOG_UPDATE_DUMP) {
		fc_log->dump_stat->flush_offset = fc->flush_off;
		fc_log->dump_stat->flush_round = fc->flush_round;
		fc_log->dump_stat->write_offset = fc->write_off;
		fc_log->dump_stat->write_round = fc->write_round;
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 Cache log write_offset/round updated when dump.\n");
	}

	/* we should update the been_shutdown flag when L2 Cache is shutdown correctly */
	if (flag == FLASH_CACHE_LOG_UPDATE_SHUTDOWN) {
		fc_log->been_shutdown = TRUE;
		/*if enabled dump, we also should update the dump_stat */
		if (srv_flash_cache_enable_dump == TRUE) {
			fc_log->dump_stat->flush_offset = fc->flush_off;
			fc_log->dump_stat->flush_round = fc->flush_round;
			fc_log->dump_stat->write_offset = fc->write_off;
			fc_log->dump_stat->write_round = fc->write_round;
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: L2 Cache log write_offset/round updated when shutdown.\n");
			fc_round_print();
		}
	}

	if (fc_log->dump_stat->flush_round == fc_log->current_stat->flush_round) {
		ut_a(fc_log->dump_stat->flush_offset <= fc_log->current_stat->flush_offset);
	}

	if (fc_log->dump_stat->write_round == fc_log->current_stat->write_round) {
		ut_a(fc_log->dump_stat->write_offset <= fc_log->current_stat->write_offset);
	}
	
	return;
}

/****************************************************************//**
Free flash cache log.*/
UNIV_INTERN
void
fc_log_destroy(void)
/*===================*/
{
	mutex_free(&fc_log->log_mutex);
	ut_free(fc_log->buf_unaligned);
	os_file_close(fc_log->file);
	ut_free((char*)fc_log->log_file_path_name);
	ut_free(fc_log);
}

/*********************************************************************//**
write flash cache log to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit(void)
/*=====================*/
{
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_CHKSUM, 
		FLASH_CACHE_LOG_CHECKSUM);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_OFFSET, 
		fc_log->current_stat->flush_offset);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_ROUND, 
		fc_log->current_stat->flush_round);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET, 
		fc_log->current_stat->write_offset);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND, 
		fc_log->current_stat->write_round);

	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_ENABLE_WRITE, 
		fc_log->enable_write_curr);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET_BCK, 
		fc_log->write_offset_bck);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND_BCK, 
		fc_log->write_round_bck);

	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_OFFSET_DUMP, 
		fc_log->dump_stat->flush_offset);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_ROUND_DUMP, 
		fc_log->dump_stat->flush_round);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_OFFSET_DUMP, 
		fc_log->dump_stat->write_offset);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_WRITE_ROUND_DUMP, 
		fc_log->dump_stat->write_round);
	
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_BLOCK_BYTE_SIZE, 
		srv_flash_cache_block_size);
	
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_SKIPED_BLOCKS, 
		0);
	
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_VERSION, 
		fc_log->log_verison);	
	
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_BEEN_SHUTDOWN, 
		fc_log->been_shutdown);	

	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_COMPRESS_ALGORITHM, 
		fc_log->compress_algorithm);	

	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_CHKSUM2, 
		FLASH_CACHE_LOG_CHECKSUM);

	os_file_write(fc_log->log_file_path_name, fc_log->file, fc_log->buf, 
		0, FLASH_CACHE_BUFFER_SIZE);
	os_file_flush(fc_log->file);

}

/*********************************************************************//**
write flash cache log blk_find_skip value to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit_for_skip_block(void)
/*=====================*/
{
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_SKIPED_BLOCKS, 
		fc_log->blk_find_skip);
	
	os_file_write(fc_log->log_file_path_name, fc_log->file, fc_log->buf, 
		0, FLASH_CACHE_BUFFER_SIZE);
	os_file_flush(fc_log->file);
}

/*********************************************************************//**
write flash cache log flush_off/flush_round value to log buffer and commit.*/
UNIV_INTERN
void
fc_log_commit_when_update_flushoff(void)
/*=====================*/
{
	flash_cache_log_mutex_enter();
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_OFFSET, 
		fc_log->current_stat->flush_offset);
	mach_write_to_4(fc_log->buf + FLASH_CACHE_LOG_FLUSH_ROUND, 
		fc_log->current_stat->flush_round);
	
	os_file_write(fc_log->log_file_path_name, fc_log->file, fc_log->buf, 
		0, FLASH_CACHE_BUFFER_SIZE);
	os_file_flush(fc_log->file);
	srv_fc_flush_should_commit_log_flush = 0;
	flash_cache_log_mutex_exit();
}


/*********************************************************************//**
Commit log when write_off is updated by doublewrite or lru move&migrate.*/
UNIV_INTERN
void
fc_log_commit_when_update_writeoff(void)
/*==========================*/
{
	//flash_cache_mutex_enter();
	flash_cache_log_mutex_enter();
	fc_log_update(FALSE, FALSE);
	fc_log_update_commit_status();
	flash_cache_mutex_exit();
	
	fc_log_commit();
	flash_cache_log_mutex_exit();
}

/********************************************************************//**
Update status after flush the fc log to disk, fc mutex should hold out of the function. */
UNIV_INTERN
void
fc_log_update_commit_status(void)
/*==========================*/
{
	srv_fc_flush_last_commit = ut_time_ms();
	srv_fc_flush_should_commit_log_flush = 0;
	srv_fc_flush_should_commit_log_write = 0;
}
