/**************************************************//**
@file fc/fc0fc.c
Flash Cache(L2 Cache) for InnoDB

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0fc.h"

#ifdef UNIV_NONINL
#include "fc0fc.ic"
#endif

#include "ut0ut.h"
#include "os0file.h"
#include "os0sync.h"
#include "fc0log.h"
#include "srv0srv.h"
#include "log0recv.h"
#include "ibuf0ibuf.h"
#include "fc0recv.h"
#include "fc0fill.h"
#include "fc0flu.h"
#include "fc0warmup.h"
#include "fc0backup.h"
#include "srv0start.h"
#include "fsp0types.h"

/* flash cache size, with n byte */
UNIV_INTERN long long	srv_flash_cache_size = LONGLONG_MAX; 
/* flash cache block size, with n byte */
UNIV_INTERN ulong	srv_flash_cache_block_size = 4096; 
/* flash cache file */
UNIV_INTERN char*	srv_flash_cache_file = NULL;
/* flash cache warmup table when startup */
UNIV_INTERN char*	srv_flash_cache_warmup_table = NULL;
/* flash cache warmup from file when startup */
UNIV_INTERN char*	srv_flash_cache_warmup_file = NULL;
/* read number of page per operation in recovery */
UNIV_INTERN ulint	srv_flash_cache_pages_per_read = 512;
/* flash cache write cache percentage */
UNIV_INTERN ulong	srv_flash_cache_write_cache_pct = 80;
/* flash cache do full IO percentage */
UNIV_INTERN ulong	srv_flash_cache_do_full_io_pct = 90;
/* flush that many pct of dirty pages of  io capacity when doing full io flush */
UNIV_INTERN ulong	srv_fc_full_flush_pct = 100;
/* flush that many pct of dirty pages of  io capacity when doing write cache flush */
UNIV_INTERN ulong	srv_fc_write_cache_flush_pct = 10;
/* flash cache block move limit */
UNIV_INTERN ulong	srv_flash_cache_move_limit = 50;
/* whether enable flash cache block move */
UNIV_INTERN my_bool	srv_flash_cache_enable_move = TRUE;
/* whether enable flash cache safest recovery */
UNIV_INTERN my_bool	srv_flash_cache_safest_recovery = FALSE;
/* whether enable flash cache block migrate */
UNIV_INTERN my_bool	srv_flash_cache_enable_migrate = TRUE;
/* whether enable flash cache block dump */
UNIV_INTERN my_bool	srv_flash_cache_enable_dump = FALSE;
/* use flash cache device as raw */
UNIV_INTERN my_bool	srv_flash_cache_is_raw = FALSE;
/* adaptive flush for flash cache */
UNIV_INTERN my_bool srv_flash_cache_adaptive_flushing = FALSE;
/* flash cache io capacity */
UNIV_INTERN ulong 	srv_fc_io_capacity = 10000;
/* doublewrite buffer flush to flash cache or behave as default */
UNIV_INTERN my_bool srv_flash_cache_enable_write = TRUE;
/* When 'innodb_flash_cache_enable_write' is first set to FALSE and then 
     'innodb_flash_cache_backup' is set to TRUE, begin flash backup. */
UNIV_INTERN my_bool srv_flash_cache_backuping = FALSE;
/* which directory to store  ib_fc_file, must including the terminating '/' */
UNIV_INTERN char*  	srv_flash_cache_backup_dir = NULL;
/* which directory to store  flash_cache.log, must including the terminating '/' */
UNIV_INTERN char*  	srv_flash_cache_log_dir = NULL;
/* write-back: WRITE_BACK, write-through: WRITE_THROUGH*/
UNIV_INTERN ulong  	srv_flash_cache_write_mode = WRITE_BACK;
/* whether use fast shutdown when MySQL is close */
UNIV_INTERN my_bool srv_flash_cache_fast_shutdown = TRUE;
/* whether use quicklz or zlib to compress the uncompress InnoDB data page */
UNIV_INTERN my_bool srv_flash_cache_enable_compress = TRUE;
/* whether flush thread has been exited when shutdown */
UNIV_INTERN my_bool srv_fc_flush_thread_exited = FALSE;
/* snappy, quicklz or zlib use to compress the uncompress InnoDB data page */
UNIV_INTERN ulong 	srv_flash_cache_compress_algorithm = FC_BLOCK_COMPRESS_SNAPPY;
/* whether use malloc or buf_block_alloc to buffer the compress InnoDB data page */
UNIV_INTERN my_bool srv_flash_cache_decompress_use_malloc = FALSE;
/* flash cache version info */
UNIV_INTERN ulong srv_flash_cache_version = FLASH_CACHE_VERSION_INFO_V5;



/** flash cache status */
/* pages reads from flash cache */
UNIV_INTERN ulint	srv_flash_cache_read = 0;
/* pages async read from flash cache */
UNIV_INTERN ulint	srv_flash_cache_aio_read = 0;
/* pages async read from flash cache */
UNIV_INTERN ulint	srv_flash_cache_wait_aio = 0;
/* pages write to doublewrite from flash cache */
UNIV_INTERN ulint	srv_flash_cache_write = 0;
/* pages write to flash cache from single flush*/
UNIV_INTERN ulint	srv_flash_cache_single_write = 0;
/* pages flush to disk from flash cache */
UNIV_INTERN ulint	srv_flash_cache_flush = 0;
/* pages merged in flash cache */
UNIV_INTERN ulint	srv_flash_cache_merge_write = 0;
/* pages move */
UNIV_INTERN ulint	srv_flash_cache_move = 0;
/* pages migrate */
UNIV_INTERN ulint	srv_flash_cache_migrate = 0;
/* read detail info */
UNIV_INTERN ulint	srv_flash_cache_read_detail[FIL_PAGE_TYPE_ZBLOB2+1];
/* write detail info */
UNIV_INTERN ulint	srv_flash_cache_write_detail[FIL_PAGE_TYPE_ZBLOB2+1];
/* flush detail info */
UNIV_INTERN ulint	srv_flash_cache_flush_detail[FIL_PAGE_TYPE_ZBLOB2+1];
/* used flash cache block count */
UNIV_INTERN ulint	srv_flash_cache_used = 0;
/* used flash cache block count if not compressed by L2 Cache */
UNIV_INTERN ulint	srv_flash_cache_used_nocompress = 0;
/* dirty flash cache block count*/
UNIV_INTERN ulint	srv_flash_cache_dirty = 0;
/* internal compress count */
UNIV_INTERN ulint 	srv_flash_cache_compress = 0;
/* internal decompress count */
UNIV_INTERN ulint 	srv_flash_cache_decompress = 0;
/* internal pack count if need compress*/
UNIV_INTERN ulint 	srv_flash_cache_compress_pack = 0;

/* flash cache log file name */
UNIV_INTERN const char 	srv_flash_cache_log_file_name[16] = "flash_cache.log";
/* flash cache thread info */
UNIV_INTERN const char* srv_flash_cache_thread_op_info = "";

/* whether flash cache has warmuped from dump file */
UNIV_INTERN my_bool srv_flash_cache_load_from_dump_file = FALSE;


/* flash cache structure */
UNIV_INTERN fc_t* fc = NULL;

/** flash cache status info for 'show engine innodb status' */
UNIV_INTERN flash_cache_stat_t flash_cache_stat;

/** flash cache status info for 'show global status' */
UNIV_INTERN flash_cache_stat_t flash_cache_stat_global;

/**************************************************************//**
Initialize flash cache struct.*/
UNIV_INTERN
void
fc_create(void)
/*=========*/
{
	ulint i;
	ulint fc_size;
	ulint blk_size;
	fc_block_array_t* tmp_ptr;

#ifdef UNIV_FLASH_CACHE_TRACE
	char debug_filename[OS_FILE_MAX_PATH];
#endif

	switch (srv_flash_cache_block_size) {
	case 1024:
	case 2048:
	case 4096:
	case 8192:
	case 16384:
		break;
	default:
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 Cache: the block size value is wrong, please reset it.\n");
		ut_error;	
	}
	
	fc = (fc_t*)ut_malloc(sizeof(fc_t));

	if (fc == NULL) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: Can not allocate memory for L2 Cache.\n");
		ut_error;
	}

	memset(fc, '0', sizeof(fc_t));

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_snprintf(debug_filename, sizeof(debug_filename),
		"%s/%s", srv_data_home, "flash_cache_debug.txt");
	srv_normalize_path_for_win(debug_filename);
	fc->f_debug = fopen(debug_filename, "w");
	if (fc->f_debug == NULL) {
		fprintf(stderr," InnoDB: L2 Cache: cannot open '%s' for writing: %s.\n",
			debug_filename, strerror(errno));
	}

#endif

	fc->is_finding_block = 0;
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	fc->is_doing_doublewrite = 0;
#endif
	fc->write_off = 0;
	fc->flush_off = 0;
	fc->write_round = 0;
	fc->flush_round = 0;

	fc->n_flush_cur = 0;

	fc->block_size = srv_flash_cache_block_size >> KILO_BYTE_SHIFT;
	fc->size = srv_flash_cache_size >> KILO_BYTE_SHIFT; 
	fc->size = fc->size / fc->block_size;

	/* create hash table with twice more flash cache block numbers */
	fc->hash_table = hash_create(fc->size * 2);

	mutex_create(PFS_NOT_INSTRUMENTED, &fc->mutex, SYNC_FC_MUTEX);
	rw_lock_create(PFS_NOT_INSTRUMENTED, &fc->hash_rwlock, SYNC_FC_HASH_RW);
	
	fc_size = fc_get_size();
	fc->block_array = (fc_block_array_t*)ut_malloc(sizeof(fc_block_array_t) * fc_size);
#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr, " L2 Cache size: %luMB, fc_block_t: %luB, fc_block_ptr: %luB, mutex_t: %luKB,event_t:%luKB \n", 
		(ulint)(srv_flash_cache_size / 1024 / 1024), (ulint)sizeof(fc_block_t),
		 (ulint)sizeof(fc_block_array_t) ,
		sizeof(ib_mutex_t)*fc_size/1024, sizeof(os_event_t)*fc_size/1024);
#endif
	memset(fc->block_array, '0', sizeof(fc_block_array_t) * fc_size);
	
	blk_size = fc_get_block_size();
	tmp_ptr = fc->block_array;	
	for (i=0; i < fc_size; i++) {
		tmp_ptr->block = NULL;
		tmp_ptr++;
	}

	fc->wait_space_event = os_event_create();
	os_event_set(fc->wait_space_event);

#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	fc->wait_doublewrite_event = os_event_create();
	os_event_set(fc->wait_doublewrite_event);
#endif

	fc->dw_pages = (fc_page_info_t*) ut_malloc(sizeof(fc_page_info_t) * 2 * FSP_EXTENT_SIZE);
	memset(fc->dw_pages, '0', sizeof(fc_page_info_t) * 2 * FSP_EXTENT_SIZE);
 	
	/* 
	 * init for dirty page flush. we use srv_io_capacity to init the flush_buf size, 
	 * each flush operation can flush no more than this blocks.
	 */
	fc->flush_buf = (fc_buf_t*)ut_malloc(sizeof(fc_buf_t));
	fc->flush_buf->unalign = (byte*)ut_malloc((srv_io_capacity + 1) * UNIV_PAGE_SIZE);
	fc->flush_buf->buf = (byte*)ut_align(fc->flush_buf->unalign, UNIV_PAGE_SIZE);
	memset(fc->flush_buf->buf, '0', srv_io_capacity * UNIV_PAGE_SIZE);
	fc->flush_buf->size = srv_io_capacity * PAGE_SIZE_KB / blk_size; 
	fc->flush_buf->free_pos = 0;

	/* we malloc flush_compress_read_buf only when the enable_compress is work */
	if (srv_flash_cache_enable_compress == TRUE) {
		if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY) {
#ifndef _WIN32
			fc->dw_zip_state = (void*)ut_malloc(sizeof(struct snappy_env));
			snappy_init_env((struct snappy_env*)fc->dw_zip_state);
#endif
		} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ){
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, "fc_qlz_state_compress: %luB \n", sizeof(fc_qlz_state_compress));
#endif
			fc->dw_zip_state = ut_malloc(sizeof(fc_qlz_state_compress));
				
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, "fc_qlz_state_decompress: %luB \n", sizeof(fc_qlz_state_decompress));
#endif
			fc->flush_dezip_state = ut_malloc(sizeof(fc_qlz_state_decompress));
			fc->recv_dezip_state = ut_malloc(sizeof(fc_qlz_state_decompress));
		}

		fc->flush_zip_read_buf_unalign = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
		fc->flush_zip_read_buf =
				(byte*)ut_align(fc->flush_zip_read_buf_unalign, UNIV_PAGE_SIZE);
		memset(fc->flush_zip_read_buf, '0', UNIV_PAGE_SIZE);

		/*
     		* each compressed page need UNIZ_PAGE_SIZE + 400B
     		* max pages flush to doublewrite currently is 128
     		* so we need 4*FSP_EXTENT_SIZE for page to be compressed
     		*/
		fc->dw_zip_buf_unalign = (byte*)ut_malloc((4 * FSP_EXTENT_SIZE + 1) * UNIV_PAGE_SIZE);
		fc->dw_zip_buf = (byte*)ut_align(fc->dw_zip_buf_unalign, UNIV_PAGE_SIZE);
		memset(fc->dw_zip_buf, '0',  4 * FSP_EXTENT_SIZE * UNIV_PAGE_SIZE);

	}

}

/**************************************************************//**
Start flash cache.*/
UNIV_INTERN
void
fc_start(ulint fc_need_recv)
/*=========*/
{
	ut_ad(srv_flash_cache_size > 0);

	fc_create();
	fc_log_create();

	if (access("flash_cache.warmup",F_OK ) != -1) {
		/* the code handle this, is at other place */
	} else {
		if (srv_flash_cache_load_from_dump_file == FALSE) {
			/* warmup L2 Cache use file flash_cache.dump. */
			fc_load();
		}
		if (srv_flash_cache_load_from_dump_file == FALSE) {
			/*
             * if we run here, it means we need scan L2 Cache file
             * to recovery the flash cache block.
             */
            ut_a(fc_need_recv);
            fil_load_single_table_tablespaces();
			fc_recv();
		}
		if (recv_needed_recovery) {
            /*
			 * Note: if in redo log recovery, 
             * new dirty page may be written to L2 cache
             * so we start flush thread to assure 
             * that new dirty page can be written to L2 cache
             * TODO: maybe we can start a new recovery_flush_thread 
             * and close it when recovery finishing
			 */
			os_thread_create(&srv_fc_flush_thread, NULL, NULL);
		}
	}

	/* L2 Cache has started. we should set fc_log been_shutdown FALSE, and commit log */
	fc_log_update_commit_status();	
	if (srv_flash_cache_enable_dump == FALSE) {
		fc_log_reset_dump_stat();
	}
		
	fc_log_update(TRUE, FLASH_CACHE_LOG_WRITE);		
	fc_log_commit();

}

/******************************************************************//**
Dump blocks from L2 Cache to file*/
UNIV_INTERN
void
fc_dump(void)
/*==================*/
{

	char	full_filename[OS_FILE_MAX_PATH];
	char	tmp_filename[OS_FILE_MAX_PATH];
	FILE*	f;
	ulint	i;
	int		ret;
	ulint 	fc_size;
	fc_block_t* b;

	ut_snprintf(full_filename, sizeof(full_filename),
		"%s/%s", srv_data_home, "flash_cache.dump");
	srv_normalize_path_for_win(full_filename);

	ut_snprintf(tmp_filename, sizeof(tmp_filename),
		"%s.incomplete", full_filename);
	srv_normalize_path_for_win(tmp_filename);

	f = fopen(tmp_filename, "w");
	if (f == NULL) {
		fprintf(stderr," InnoDB: Cannot open '%s' for writing: %s.\n", tmp_filename, strerror(errno));
		return;
	}

	fc_size = fc_get_size();
	/* dump L2 Cache block info */
	i = 0;
	while (i < fc_size) {
		b = fc_get_block(i);

		if (b == NULL) {
			i++;
			continue;
		}
		
		if (b->state != BLOCK_NOT_USED) {
			/* only dump block with state BLOCK_READ_CACHE, BLOCK_FLUSHED or BLOCK_READ_FOR_FLUSH */
			ret = fprintf(f, "%lu,%lu,%lu,%lu,%lu,%lu\n",
				(unsigned long)b->space,
				(unsigned long)b->offset,
				(unsigned long)b->fil_offset,
				(unsigned long)b->state,
				(unsigned long)b->size,
				(unsigned long)b->raw_zip_size);
			
			if (ret < 0) {
				fclose(f);
				fprintf(stderr, " InnoDB: Cannot write to '%s': %s.\n", tmp_filename, strerror(errno));
				/* leave tmp_filename to exist */
				return;
			}
		}

		i += fc_block_get_data_size(b);
	}
	
	ret = fclose(f);
	if (ret != 0) {
		fprintf(stderr, " InnoDB: Cannot close '%s': %s.\n", tmp_filename, strerror(errno));
		return;
	}
	
	ret = unlink(full_filename);
	if (ret != 0 && errno != ENOENT) {
		fprintf(stderr, " InnoDB: Cannot delete '%s': %s.\n", full_filename, strerror(errno));
		/* leave tmp_filename to exist */
		return;
	}
	
	ret = rename(tmp_filename, full_filename);
	if (ret != 0) {
		fprintf(stderr, " InnoDB: Cannot rename '%s' to '%s': %s.\n",
			tmp_filename, full_filename, strerror(errno));
		/* leave tmp_filename to exist */
		return;
	}

	ut_print_timestamp(stderr);
	fprintf(stderr, " InnoDB: L2 Cache dump completed.\n");
}

/**************************************************************//**
Free L2 Cache struct.*/
UNIV_INTERN
void
fc_destroy(void)
/*=========*/
{
	ulong i;
	ulint fc_size;
	fc_block_t* tmp_block;
	fc_block_array_t* block_ptr;

#ifdef UNIV_FLASH_CACHE_TRACE
	ulint ret = fclose(fc->f_debug);
	if (ret != 0) {
		fprintf(stderr, " InnoDB: Cannot close dubeg file: %s. %s:%d\n", 
                        strerror(errno), __FILE__, __LINE__);
	}
#endif
	
	fc_dump();
	fc_log_update(FALSE, FLASH_CACHE_LOG_UPDATE_SHUTDOWN);
	fc_log_commit();
	
	ut_free(fc->flush_buf->unalign);
	ut_free(fc->flush_buf);

	if (srv_flash_cache_enable_compress == TRUE) {
		if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY) {
#ifndef _WIN32
			snappy_free_env((struct snappy_env*)fc->dw_zip_state);
			ut_free(fc->dw_zip_state);
#endif
		} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
			ut_free(fc->dw_zip_state);
			ut_free(fc->flush_dezip_state);
			ut_free(fc->recv_dezip_state);
		}
		
		ut_free(fc->flush_zip_read_buf_unalign);

		/* we free this buf when finish recovery */
		//ut_free(fc->recv_dezip_buf_unalign);

	}

	ut_free(fc->dw_zip_buf_unalign);
	ut_free(fc->dw_pages);

	fc_size = fc_get_size();

	block_ptr = fc->block_array;
	for (i = 0; i < fc_size; i++) {
		tmp_block = block_ptr->block;
		if (tmp_block) {
			mutex_free(&tmp_block->mutex);
			ut_free((void*)tmp_block);
			block_ptr->block = NULL;
		}
		block_ptr++;
	}
	
	ut_free(fc->block_array);
	hash_table_free(fc->hash_table);
	
	rw_lock_free(&fc->hash_rwlock);
	mutex_free(&fc->mutex);

	os_event_free(fc->wait_space_event);
	
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	os_event_free(fc->wait_doublewrite_event);
#endif
	
	ut_free(fc);
}

/******************************************************************//**
Load L2 Cache from dump file */
UNIV_INTERN
void
fc_load(void)
/*==================*/
{
	char	full_filename[OS_FILE_MAX_PATH];
	ulint	i;
	FILE*	f;
	ulint	dump_n;
	int		fscanf_ret;
	ulint	space_id = 0;
	ulint	page_no = 0;
	ulint	fil_offset = 0;
	ulint	state = 0;
	ulint 	size = 0;
	ulint 	compress_size = 0;
	ulint 	version_count;
	ibool	is_v4_dump_file = FALSE;
	fc_block_t* b;

	ut_snprintf(full_filename, sizeof(full_filename),
		"%s/%s", srv_data_home, "flash_cache.dump");
	srv_normalize_path_for_win(full_filename);

	f = fopen(full_filename, "r");
	if (f == NULL) {
		if (fc_log->first_use) {
			/* no recovery is needed */
			srv_flash_cache_load_from_dump_file = TRUE;
			/* prevent error output below */
			return ;
		}

		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: Cannot open '%s' for reading: %s.\n",
			full_filename, strerror(errno));
		fprintf(stderr, "InnoDB: L2 Cache did not shutdown correctly,"
			"scan L2 Cache file %s to recover.\n", srv_flash_cache_file);
		return;
	}

	/* 
	 * the flash_cache.dump file exist, it mean that the enable_dump is enabled when load,
	 * or L2 Cache shutdown correctly. if the dump file is too old, just skip and do recovery
	 */
	if (fc_log->been_shutdown == FALSE && srv_flash_cache_enable_dump 
		&& (fc_log->current_stat->write_round >= (fc_log->dump_stat->write_round + 1))
		&& (fc_log->current_stat->write_offset >= fc_log->dump_stat->write_offset)) {
		return;
	}

	/* if the log version is not 55305, the compress algorithm must be quicklz or not */
	if ((fc_log->log_verison < FLASH_CACHE_VERSION_INFO_V5)
			&& (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY)) {
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: Cannot use snappy compress algorithm to load L2 Cache"
			" low than InnoSQL-5.5.30-v5.\n");
		ut_error;
	}

	dump_n = 0;

	if (fc_log->log_verison == 0) {
		version_count = 4;		
		while (fscanf(f, "%lu,%lu,%lu,%lu", 
			&space_id, &page_no, &fil_offset, &state) == version_count) {
			dump_n++;
		}

	} else {
		version_count = 6;
		while (fscanf(f, "%lu,%lu,%lu,%lu,%lu,%lu",
			&space_id, &page_no, &fil_offset, &state, &size, &compress_size) == version_count) {
			dump_n++;
		}	

		if (fc_log->log_verison < FLASH_CACHE_VERSION_INFO_V5) {
			is_v4_dump_file = TRUE;
			ut_a(srv_flash_cache_compress_algorithm <= FC_BLOCK_COMPRESS_QUICKLZ);
		}
	}
	

	if (!feof(f)) {
		/* fscanf() returned != version_count */
		const char*	what;
		if (ferror(f)) {
			what = "reading";
		} else {
			what = "parsing";
		}
		fclose(f);
		fprintf(stderr, " InnoDB: Error %s '%s', unable to load buffer pool (stage 1).\n",
			what, full_filename);
		return;
	}

	rewind(f);

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr, "InnoDB: L2 Cache loading %d L2 Cache blocks from dump file.\n", (int)dump_n);
#endif

	for (i = 0; i < dump_n; i++) {
		if (fc_log->log_verison == 0) {
			fscanf_ret = fscanf(f, "%lu,%lu,%lu,%lu", 
				&space_id, &page_no, &fil_offset, &state);
			size = 1;
		} else {
			fscanf_ret = fscanf(f, "%lu,%lu,%lu,%lu,%lu,%lu",
				&space_id, &page_no, &fil_offset, &state, &size, &compress_size);
		}
		
		if (fscanf_ret != version_count) {
			if (feof(f)) {
				break;
			}
			fclose(f);
			fprintf(stderr," InnoDB: Error parsing '%s', unable "
				"to load buffer pool (stage 2).\n", full_filename);
			return;
		}

		if (space_id > ULINT32_MASK || page_no > ULINT32_MASK) {
			fclose(f);
			/* error found, we should not continue */
			ut_error;
		}

		ut_a(state != BLOCK_NOT_USED);
		


#ifdef UNIV_FLASH_CACHE_TRACE
		b = fc_get_block(fil_offset);
		ut_a(b == NULL);
#endif
		
		b = fc_block_init(fil_offset);
		ut_a(b == fc_get_block(fil_offset));
		
		b->offset = page_no;
		b->space = space_id;
		b->state = state;
		b->size = size;
		b->raw_zip_size = compress_size;
		b->is_v4_blk = is_v4_dump_file;
		
		/* insert to hash table */
		fc_block_insert_into_hash(b);

		srv_flash_cache_used += fc_block_get_data_size(b);
		srv_flash_cache_used_nocompress += fc_block_get_orig_size(b);		

		if (state == BLOCK_READY_FOR_FLUSH) {
			srv_flash_cache_dirty += fc_block_get_data_size(b);
		}
	}

	fclose(f);

	if (srv_flash_cache_enable_dump == FALSE) {
		ut_a(os_file_delete(innodb_file_data_key, full_filename));
	}

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB:L2 Cache load block metadata from dump file completed.\n");
#endif

	/*
	 * when the write/flush_offset/round_dump info is equal with
	 * write/flush_offset/round, it means that the L2 Cache shutdown correctly
	 * no need to do recovery job
	 */
	//flash_cache_log_mutex_enter();
	//if (fc_log->been_shutdown == TRUE) {
		srv_flash_cache_load_from_dump_file = TRUE;		

#ifdef UNIV_FLASH_CACHE_TRACE
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 Cache shutdown correctly, no need to do recovery.\n");
#endif
	//}
	//flash_cache_log_mutex_exit();	

}

/********************************************************************//**
Flush a batch of writes to the datafiles that have already been
written by the L2 Cache. */
UNIV_INTERN
void
fc_sync_fcfile(void)
/*===========================*/
{
	/* Wake possible simulated aio thread to actually post the
	writes to the operating system */
	os_aio_simulated_wake_handler_threads();

	/* Wait that all async writes to tablespaces have been posted to the OS */
	os_aio_wait_until_no_pending_fc_writes();

	/* Now we flush the data to disk (for example, with fsync) */
	fil_flush_file_spaces(FIL_FLASH_CACHE);
}

/********************************************************************//**
Test if is_doing_doublewrite equal to 1, if so, commit log, else just do --. */
static
void
fc_test_and_commit_log(void)
/*===========================*/
{
	/* if is_doing_doublewrite > 1, just  sub it and do not commit the log */
	/* if is_doing_doublewrite == 1, first commit the log, and then set is_doing_doublewrite zero, so move/migrate can go on */
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	flash_cache_mutex_enter();

	ut_a(fc->is_doing_doublewrite == 1);

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_a((srv_fc_flush_should_commit_log_write != 0)
	// this means at this time enable_write has been set off
		|| (srv_flash_cache_enable_write == 0));
#endif

	/* this function will release the fc mutex */
	fc_log_commit_when_update_writeoff();
	flash_cache_mutex_enter();
	fc->is_doing_doublewrite = 0;
	os_event_set(fc->wait_doublewrite_event);	
	flash_cache_mutex_exit();
#endif
}

/********************************************************************//**
Flush a doublewrite block to flashcache block.
@return:NULL*/
static
void
fc_sync_hash_table(
/*===========================*/
	buf_dblwr_t* trx_dw, 	/*!< in: doublewrite structure */
	ulint pos) 				/*!< in: the doublewrite buffer index */
{
	ulint page_type;
	ulint wf_size;
	fc_block_t* wf_block = NULL;

	ut_ad(mutex_own(&fc->mutex));

	wf_block = fc_get_block(fc->write_off);

	/*at this time the wf_block could not be in hash table.*/
	ut_a(wf_block->state == BLOCK_NOT_USED);

	wf_size = fc_block_get_data_size(wf_block);

	/* insert the wf_block into hash table and set block statement */
	fc_block_insert_into_hash(wf_block);
	wf_block->io_fix |= IO_FIX_DOUBLEWRITE;
	wf_block->state = BLOCK_READY_FOR_FLUSH;

	/* inc the fc status counts */
	srv_flash_cache_dirty += wf_size;
	srv_flash_cache_used += wf_size;
	srv_flash_cache_used_nocompress += fc_block_get_orig_size(wf_block);
	srv_flash_cache_write++;

	/* get page type from doublewrite buffer */
    page_type = fil_page_get_type(trx_dw->write_buf + pos * UNIV_PAGE_SIZE);
	if (page_type == FIL_PAGE_INDEX) {
		page_type = 1;
	}
	srv_flash_cache_write_detail[page_type]++;
}

/********************************************************************//**
Calc and store each block compressed size or zip size if no need compress
by L2 Cache, and calc the block size need by each page in doublewrite buffer
@return: NULL */
static
void
fc_write_compress_and_calc_size(
/*===========================*/
	buf_dblwr_t* trx_dw)	/*!< in: doublewrite structure */
{
	ulint i;
	ulint blk_size = fc_get_block_size();
	ulint need_compress;

	byte* compress_data = NULL;
    ulint zip_size;
	ulint cp_size;
	
	buf_block_t* dw_block = NULL;
	buf_page_t* dw_page = NULL;
	fc_page_info_t* page_info = NULL;

	for (i = 0; i < trx_dw->first_free; i++) {
		zip_size = 0;
		dw_page = trx_dw->buf_block_arr[i];
		dw_block = (buf_block_t*)dw_page;
		need_compress = fc_block_need_compress(dw_page->space);
		
		if (need_compress == TRUE) {
			compress_data = fc->dw_zip_buf + i * FC_ZIP_COMPRESS_BUF_SIZE;
			cp_size = fc_block_do_compress(TRUE, dw_page, compress_data);
			//printf("cp_size %lu \n", cp_size);
			if (fc_block_compress_successed(cp_size) == FALSE) {
				need_compress = FALSE;
			} 
		}
	
		page_info = &(fc->dw_pages[i]);
			
		page_info->space = dw_page->space;
		page_info->offset = dw_page->offset;
	
		if (need_compress == TRUE) {
			page_info->raw_zip_size = cp_size;
			page_info->size = PAGE_SIZE_KB / blk_size;
		} else {
			zip_size = fil_space_get_zip_size(dw_block->page.space);
			page_info->size = fc_calc_block_size(zip_size) / blk_size;
			page_info->raw_zip_size = 0;
		}
	
#ifdef UNIV_FLASH_CACHE_TRACE
		if (zip_size == 0) {
			if (fc_dw_page_corrupted(dw_block)) {
				ut_error;
			}
		} else {
			if (buf_page_is_corrupted(true, trx_dw->write_buf + i * UNIV_PAGE_SIZE, zip_size)) {
				buf_page_print(trx_dw->write_buf + i * UNIV_PAGE_SIZE,
						zip_size, BUF_PAGE_PRINT_NO_CRASH);
				ut_error;
			}
		}
#endif
			
	}

}

/********************************************************************//**
Find cache blocks for store the doublewrite buffer data
set this block with BLOCK_READ_FOR_WRITE, remove old block from hash table
and insert new block into hash table
@return: NULL */
static
void
fc_write_find_block_and_sync_hash(
/*===========================*/
	buf_dblwr_t* trx_dw)	/*!< in: doublewrite structure */
{
	ulint i;
	ulint data_size;	
	
	fc_block_t* old_block;	
	fc_block_t* wf_block;
	buf_page_t* dw_page;
	fc_page_info_t* page_info;
	
	for (i = 0; i < trx_dw->first_free; i++) {
		page_info = NULL;
		page_info = &(fc->dw_pages[i]);
		ut_a(page_info);
		
		/* find fc block(s) and write the buf_block into the block(s) */
		rw_lock_x_lock(&fc->hash_rwlock);
		old_block = NULL;
		old_block = fc_block_search_in_hash(page_info->space, page_info->offset);

		if (old_block != NULL) {
      ulint old_size;
			/* first remove the old block, can seal with a function*/
			flash_block_mutex_enter(old_block->fil_offset);	
			ut_a((old_block->space == page_info->space)
				&& (old_block->offset == page_info->offset));
		
			ut_a(old_block->state != BLOCK_NOT_USED);
			ut_a((old_block->io_fix & IO_FIX_READ) == IO_FIX_NO_IO);

			old_size = fc_block_get_data_size(old_block);

			if (old_block->state == BLOCK_READY_FOR_FLUSH) {
				srv_flash_cache_merge_write++;
				srv_flash_cache_dirty -= old_size;
			}

			fc_block_delete_from_hash(old_block);
			
#ifdef UNIV_FLASH_CACHE_TRACE
			fc_print_used();
#endif
			srv_flash_cache_used -= old_size;
			srv_flash_cache_used_nocompress -= fc_block_get_orig_size(old_block);
			flash_block_mutex_exit(old_block->fil_offset);

			fc_block_free(old_block);		
		}

		if (page_info->raw_zip_size > 0) {
			data_size = fc_block_compress_align(page_info->raw_zip_size);
		} else {
			data_size = page_info->size;
		}

		ut_a(data_size);
		
		wf_block = NULL;
		wf_block = fc_block_find_replaceable(TRUE, data_size);

		ut_a(wf_block != NULL);

		/* the block mutex will release when io compelete, so flush thread will not do wrong flush */
		flash_block_mutex_enter(wf_block->fil_offset);
		ut_a(fc_block_get_data_size(wf_block) == data_size);
		page_info->fil_offset = wf_block->fil_offset;
		wf_block->space = page_info->space;
		wf_block->offset = page_info->offset;

		wf_block->size = page_info->size;
		wf_block->raw_zip_size = page_info->raw_zip_size;

		fc_sync_hash_table(trx_dw, i);

		dw_page = trx_dw->buf_block_arr[i];
		ut_a(dw_page->fc_block == NULL);
		dw_page->fc_block = wf_block;
		
		/* so InnoDB read operation can into L2 Cache*/
		rw_lock_x_unlock(&fc->hash_rwlock);
				
		/* ok, we haved insert a dw_block into hash table. inc write_off */
		fc_inc_write_off(data_size);
		srv_fc_flush_should_commit_log_write += data_size;
	}

}

/********************************************************************//**
Add the block to buf_page_t, and launch the write io
@return: NULL */
static
void
fc_write_launch_io(
/*===========================*/
	buf_dblwr_t* trx_dw)	/*!< in: doublewrite structure */
{
	ulint i;
	ulint ret;
	ulint block_offset, byte_offset;
	ulint blk_size_byte = fc_get_block_size_byte();
	ulint page_size; /*n Byte*/
 
	byte* compress_data = NULL;

	fc_block_t* wf_block = NULL;
	buf_page_t* dw_page = NULL;
	fc_page_info_t* page_info = NULL;
	
	for (i = 0; i < trx_dw->first_free; i++) {
		page_info = &(fc->dw_pages[i]);	
		compress_data = fc->dw_zip_buf + i * FC_ZIP_COMPRESS_BUF_SIZE;
		dw_page = trx_dw->buf_block_arr[i];
		wf_block = (fc_block_t*)dw_page->fc_block;
		ut_a(wf_block != NULL);
		ut_a(wf_block->space == page_info->space);
		ut_a(wf_block->offset == page_info->offset);
		ut_a(wf_block->fil_offset == page_info->fil_offset);		
		
		if (page_info->raw_zip_size > 0) {
			page_size = fc_block_compress_align(page_info->raw_zip_size) * blk_size_byte;
			
			/* this function should rewrite with no page data copy */
			fc_block_pack_compress(wf_block, compress_data);
		} else {
			page_size = page_info->size * blk_size_byte;
		} 

		fc_io_offset(wf_block->fil_offset, &block_offset, &byte_offset);
				
		if (page_info->raw_zip_size > 0) {
#ifdef UNIV_FLASH_CACHE_TRACE
			fc_block_compress_check(compress_data, wf_block);	
#endif
			ret = fil_io(OS_FILE_WRITE, FALSE, FLASH_CACHE_SPACE, 0,
					block_offset, byte_offset, page_size,
					compress_data, NULL);
		} else {
			ret = fil_io(OS_FILE_WRITE, FALSE, FLASH_CACHE_SPACE, 0,
					block_offset, byte_offset, page_size,
					trx_dw->write_buf + i * UNIV_PAGE_SIZE, NULL);
		}

		if (ret != DB_SUCCESS) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB Error: L2 Cache fail to launch aio. page(%lu, %lu) in %lu.\n", 
				(ulong)wf_block->space, (ulong)wf_block->offset, (ulong)wf_block->fil_offset);
		}
				
	}

}

/********************************************************************//**
Add the block to buf_page_t, and launch the write io
@return: NULL */
static
void
fc_write_complete_io(
/*===========================*/
	buf_dblwr_t* trx_dw)	/*!< in: doublewrite structure */
{
	ulint i, first_free;
	
	fc_block_t* wf_block = NULL;
	buf_page_t* dw_page = NULL;

	first_free = trx_dw->first_free;

	/*push from buf_dblwr_flush_buffered_writes */
	/* Up to this point first_free and buf_dblwr->first_free are
	same because we have set the buf_dblwr->batch_running flag
	disallowing any other thread to post any request but we
	can't safely access buf_dblwr->first_free in the loop below.
	This is so because it is possible that after we are done with
	the last iteration and before we terminate the loop, the batch
	gets finished in the IO helper thread and another thread posts
	a new batch setting buf_dblwr->first_free to a higher value.
	If this happens and we are using buf_dblwr->first_free in the
	loop termination condition then we'll end up dispatching
	the same block twice from two different threads. */
	for (i = 0; i < first_free; i++) {
		dw_page = trx_dw->buf_block_arr[i];
		wf_block = (fc_block_t*)dw_page->fc_block;
		ut_a(wf_block != NULL);
		dw_page->fc_block = NULL;
		ut_a(wf_block->io_fix & IO_FIX_DOUBLEWRITE);
		wf_block->io_fix &= ~IO_FIX_DOUBLEWRITE;
		flash_block_mutex_exit(wf_block->fil_offset);
		buf_page_io_complete(dw_page, TRUE);	
	}
}

/********************************************************************//**
Sync hash table after find a block for doublewrite single flush page.
@return:NULL*/
static
void
fc_sync_hash_single_page(
/*===========================*/
	buf_page_t* bpage) 	/*!< in: doublewrite single page */
{
	ulint page_type;
	ulint wf_size;
	fc_block_t* wf_block = NULL;

	ut_ad(mutex_own(&fc->mutex));

	wf_block = fc_get_block(fc->write_off);

	/*at this time the wf_block could not be in hash table.*/
	ut_a(wf_block->state == BLOCK_NOT_USED);

	wf_size = fc_block_get_data_size(wf_block);

	/* insert the wf_block into hash table and set block statement */
	fc_block_insert_into_hash(wf_block);
	wf_block->io_fix |= IO_FIX_DOUBLEWRITE;
	wf_block->state = BLOCK_READY_FOR_FLUSH;

	/* inc the fc status counts */
	srv_flash_cache_dirty += wf_size;
	srv_flash_cache_used += wf_size;
	srv_flash_cache_used_nocompress += fc_block_get_orig_size(wf_block);
	srv_flash_cache_write++;

	/* get page type from doublewrite buffer */
    page_type = fil_page_get_type(((buf_block_t*) bpage)->frame);
	if (page_type == FIL_PAGE_INDEX) {
		page_type = 1;
	}
	srv_flash_cache_write_detail[page_type]++;
}



/********************************************************************//**
Writes a page to the to Cache and sync it. if sync write, call io complete */
UNIV_INTERN
void
fc_write_single_page(
/*========================*/
	buf_page_t*	bpage,	/*!< in: buffer block to write */
	bool		sync)	/*!< in: true if sync IO requested */
{
	dberr_t err;
    ulint zip_size;
	ulint cp_size = 0;
	ulint data_size;	
	ulint need_compress;
	ulint block_offset, byte_offset;

	byte* zip_buf = NULL;
	byte* zip_buf_unalign = NULL;

	ulint blk_size = fc_get_block_size();

	fc_block_t* old_block = NULL;	
	fc_block_t* wf_block = NULL;

#ifdef UNIV_FLASH_CACHE_TRACE	
	ulint skip_count; 
	ulint old_write_off;
	ulint old_write_round;
#endif	 

	zip_size = fil_space_get_zip_size(bpage->space);

	#ifdef UNIV_FLASH_CACHE_TRACE
	if (zip_size == 0) {
		if (fc_dw_page_corrupted((buf_block_t*)bpage)) {
			ut_error;
		}
	} else {
		if (buf_page_is_corrupted(true, ((buf_block_t*) bpage)->frame, zip_size)) {
			buf_page_print(((buf_block_t*) bpage)->frame, zip_size, BUF_PAGE_PRINT_NO_CRASH);
			ut_error;
		}
	}
#endif

	/* 
	 * step 1: calc and store each block compressed size or zip size if no need compress
	 * by L2 Cache, and calc the block size need by each page in doublewrite buffer
	 */

	need_compress = fc_block_need_compress(bpage->space);
		
	if (need_compress == TRUE) {
		zip_buf_unalign = (byte*)ut_malloc(3 * UNIV_PAGE_SIZE);
		zip_buf = (byte*)ut_align(zip_buf_unalign, UNIV_PAGE_SIZE);
		memset(zip_buf, '0', 2 * UNIV_PAGE_SIZE);
		/* we should set fc_block_do_compress variable is_dw = FALSE, as it is not batch  */
		cp_size = fc_block_do_compress(FALSE, bpage, zip_buf);
		//printf("cp_size %lu \n", cp_size);
		if (fc_block_compress_successed(cp_size) == FALSE) {
			need_compress = FALSE;
		} 
	}
		
	/* 
	 * step 2: find cache blocks for store the doublewrite buffer data
	 * set this block with BLOCK_READ_FOR_WRITE, remove old block from hash table
	 * and insert new block into hash table
	 */
retry:

	/* get fc mutex, so move/migrate can`t go on */
	flash_cache_mutex_enter();

	if (fc_get_available() <= FC_LEAST_AVIABLE_BLOCK_FOR_RECV) {
		fc_wait_for_space();	/* this function will release fc mutex*/		
		goto retry;
	}

	if (fc->is_finding_block == 1) {
		flash_cache_mutex_exit();
		goto retry;
	}

	if (fc->is_doing_doublewrite > 0) {
		/*
		* we wait here to avoid the single flush commit the writeoff/writeround
		* (which update by batch flush but data have not been synced)
		*/
		fc_wait_for_aio_dw_launch();
		goto retry;
	}

#ifdef UNIV_FLASH_CACHE_TRACE	 
	old_write_off = fc->write_off;
	old_write_round = fc->write_round;
#endif

	/* set  is_doing_doublewrite = 1, so move/migrate should not commit the writeoff until doublewrite fsynced */
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	fc->is_doing_doublewrite++;
#endif

	/* find fc block(s) and write the buf_block into the block(s) */
	rw_lock_x_lock(&fc->hash_rwlock);

	old_block = fc_block_search_in_hash(bpage->space, bpage->offset);

	if (old_block != NULL) {
		ulint old_size;
		/* first remove the old block, can seal with a function*/
		flash_block_mutex_enter(old_block->fil_offset);	
		ut_a((old_block->space == bpage->space)
				&& (old_block->offset == bpage->offset));
		
		ut_a(old_block->state != BLOCK_NOT_USED);
		ut_a((old_block->io_fix & IO_FIX_READ) == IO_FIX_NO_IO);

		old_size = fc_block_get_data_size(old_block);

		if (old_block->state == BLOCK_READY_FOR_FLUSH) {
			srv_flash_cache_merge_write++;
			srv_flash_cache_dirty -= old_size;
		}

		fc_block_delete_from_hash(old_block);
			
#ifdef UNIV_FLASH_CACHE_TRACE
		fc_print_used();
#endif
		srv_flash_cache_used -= old_size;
		srv_flash_cache_used_nocompress -= fc_block_get_orig_size(old_block);
		flash_block_mutex_exit(old_block->fil_offset);

		fc_block_free(old_block);		
	}

	if (need_compress == TRUE) {
		data_size = fc_block_compress_align(cp_size);
	} else {
		data_size = fc_calc_block_size(zip_size) / blk_size;
	}

	ut_a(data_size);
		
	wf_block = NULL;
	wf_block = fc_block_find_replaceable(TRUE, data_size);

	ut_a(wf_block != NULL);
	ut_a(wf_block->fil_offset == fc_get_block(fc->write_off)->fil_offset);

	/* the block mutex will release when io compelete, so flush thread will not do wrong flush */
	flash_block_mutex_enter(wf_block->fil_offset);
	ut_a(fc_block_get_data_size(wf_block) == data_size);
	wf_block->space = bpage->space;
	wf_block->offset = bpage->offset;

	if (need_compress == TRUE) {
		wf_block->raw_zip_size = cp_size;
		wf_block->size = PAGE_SIZE_KB / blk_size;
	} else {
		wf_block->size = fc_calc_block_size(zip_size) / blk_size;
		wf_block->raw_zip_size = 0;
	}

	fc_sync_hash_single_page(bpage);

	ut_a(bpage->fc_block == NULL);
	bpage->fc_block = wf_block;
		
	/* so InnoDB read operation can into L2 Cache*/
	rw_lock_x_unlock(&fc->hash_rwlock);
				
	/* ok, we haved insert a dw_block into hash table. inc write_off */
	fc_inc_write_off(data_size);
	srv_fc_flush_should_commit_log_write += data_size;

	/* 
	 * we should check if 
	 * write some blocks into ssd and  update the log, we should release the mutex
	 * after all the async io is launched
	 */	
#ifdef UNIV_FLASH_CACHE_TRACE	 
	if (old_write_round == fc->write_round) {
		skip_count = fc->write_off - old_write_off;
	} else {
		skip_count = fc->write_off + fc_get_size() - old_write_off;
	}

	if (skip_count >= FC_FIND_BLOCK_SKIP_COUNT) {
		flash_cache_log_mutex_enter();
		fc_log->blk_find_skip = skip_count;
		fc_log_commit_for_skip_block();
		flash_cache_log_mutex_exit();

		fprintf(fc->f_debug, "doublewrite skip %lu blocks, need commit log\n", (ulong)skip_count);
	}
#endif

	/* 
	 * it is not safe to release fc mutex this time, as lru move may get the mutex and
	 * write some blocks into ssd and  update the log, we should release the mutex
	 * after all the async io is launched. so make sure is_doing_doublewrite = 1
	 */

	/*  now flush thread can go on */
	flash_cache_mutex_exit();

	/* 
	 * step 3: add the block to buf_page_t, and launch the write io when the async io compelete,
	 * release the block mutex from the buf_page_t
	 */	
	if (need_compress == TRUE) {
		fc_block_pack_compress(wf_block, zip_buf);
	} 

	fc_io_offset(wf_block->fil_offset, &block_offset, &byte_offset);
				
	if (need_compress == TRUE) {
#ifdef UNIV_FLASH_CACHE_TRACE
		fc_block_compress_check(zip_buf, wf_block);	
#endif
		err = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0,
				block_offset, byte_offset, data_size * blk_size * KILO_BYTE,
				zip_buf, NULL);
	} else {
		err = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0,
				block_offset, byte_offset, data_size * blk_size * KILO_BYTE,
				((buf_block_t*)bpage)->frame, NULL);
	}

	if (err != DB_SUCCESS) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB Error: L2 Cache fail to launch aio. page(%lu, %lu) in %lu.\n", 
			(ulong)wf_block->space, (ulong)wf_block->offset, (ulong)wf_block->fil_offset);
	}

	fc_sync_fcfile();

	//fprintf(stderr,"   single write space %lu offset %lu filoff %lu zip size %lu\n", 
	//	(ulong)wf_block->space, (ulong)wf_block->offset, (ulong)wf_block->fil_offset, zip_size);

	/* at this time, io complete, set the block state READY_FOR_FLUSH and release block mutex */
	ut_a(bpage->fc_block != NULL);
	bpage->fc_block = NULL;
	ut_a(wf_block->io_fix & IO_FIX_DOUBLEWRITE);
	wf_block->io_fix &= ~IO_FIX_DOUBLEWRITE;
	flash_block_mutex_exit(wf_block->fil_offset);
	
	buf_page_io_complete(bpage, TRUE);

	fc_test_and_commit_log();

	srv_flash_cache_single_write++;

	ut_free(zip_buf_unalign);

}

/********************************************************************//**
Flush double write buffer to L2 Cache block.no io will read or write the ssd block which
is writed from doublewriter buffer. as io will hit the buf pool or
doublewriter until the function exit
@return: count of async read L2 Cache block */
UNIV_INTERN
void
fc_write(
/*===========================*/
	buf_dblwr_t* trx_dw)	/*!< in: doublewrite structure */
{
#ifdef UNIV_FLASH_CACHE_TRACE	
	ulint skip_count; 
	ulint old_write_off;
	ulint old_write_round;
#endif	 

	memset(fc->dw_pages, '0', sizeof(fc_page_info_t) * 2 * FSP_EXTENT_SIZE);

	/* 
	 * step 1: calc and store each block compressed size or zip size if no need compress
	 * by L2 Cache, and calc the block size need by each page in doublewrite buffer
	 */
	fc_write_compress_and_calc_size(trx_dw);

	/* 
	 * step 2: find cache blocks for store the doublewrite buffer data
	 * set this block with BLOCK_READ_FOR_WRITE, remove old block from hash table
	 * and insert new block into hash table
	 */
retry:

	/* get fc mutex, so move/migrate can`t go on */
	flash_cache_mutex_enter();

	if (fc_get_available() <= FC_LEAST_AVIABLE_BLOCK_FOR_RECV) {
		fc_wait_for_space();	/* this function will release fc mutex*/		
		goto retry;
	}

	if (fc->is_finding_block == 1) {

		flash_cache_mutex_exit();
		goto retry;
	}

	if (fc->is_doing_doublewrite > 0) {
		/*
		* we wait here to avoid the batch flush commit the writeoff/writeround
		* (which update by single flush but data have not been synced)
		*/
		fc_wait_for_aio_dw_launch();
		goto retry;
	}

#ifdef UNIV_FLASH_CACHE_TRACE	 
	old_write_off = fc->write_off;
	old_write_round = fc->write_round;
#endif

	/* set  is_doing_doublewrite = 1, so move/migrate should not commit the writeoff until doublewrite fsynced */
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	fc->is_doing_doublewrite++;
#endif

	fc_write_find_block_and_sync_hash(trx_dw);

	/* 
	 * we should check if 
	 * write some blocks into ssd and  update the log, we should release the mutex
	 * after all the async io is launched
	 */	
#ifdef UNIV_FLASH_CACHE_TRACE	 
	if (old_write_round == fc->write_round) {
		skip_count = fc->write_off - old_write_off;
	} else {
		skip_count = fc->write_off + fc_get_size() - old_write_off;
	}

	if (skip_count >= FC_FIND_BLOCK_SKIP_COUNT) {
		flash_cache_log_mutex_enter();
		fc_log->blk_find_skip = skip_count;
		fc_log_commit_for_skip_block();
		flash_cache_log_mutex_exit();

		fprintf(fc->f_debug, "doublewrite skip %lu blocks, need commit log\n", (ulong)skip_count);
	}
#endif

	/* 
	 * it is not safe to release fc mutex this time, as lru move may get the mutex and
	 * write some blocks into ssd and  update the log, we should release the mutex
	 * after all the async io is launched. so make sure is_doing_doublewrite = 1
	 */

	/*  now flush thread can go on */
	flash_cache_mutex_exit();

	/* 
	 * step 3: add the block to buf_page_t, and launch the write io when the async io compelete,
	 * release the block mutex from the buf_page_t
	 */
	fc_write_launch_io(trx_dw);

	fc_sync_fcfile();

	/* at this time, io complete, set the block state READY_FOR_FLUSH and release block mutex */
	fc_write_complete_io(trx_dw);

	fc_test_and_commit_log();
}

/********************************************************************//**
When srv_flash_cache_enable_write is FALSE, doublewrite buffer will behave as deault. 
So if page need flush now(newer) is also in L2 Cache already(olded),
it must be removed  from the L2 Cache before doublewrite write to disk.
@return: if removed in L2 Cache */
UNIV_INTERN
ulint
fc_block_remove_single_page(
/*==================*/
	buf_page_t* bpage)/*!< in: bpage need flush */
{
	ulint space;
	ulint offset;
	ulint free_block;
	ulint removed_pages = 0;
	fc_block_t* out_block = NULL;

	ut_a(bpage);

	space = mach_read_from_4(((buf_block_t*) bpage)->frame
			+ FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
	offset = mach_read_from_4(((buf_block_t*) bpage)->frame + FIL_PAGE_OFFSET);
		
	rw_lock_x_lock(&fc->hash_rwlock);
	out_block = fc_block_search_in_hash(space, offset);
	if (out_block) {
		ulint data_size;
		flash_block_mutex_enter(out_block->fil_offset);
		data_size = fc_block_get_data_size(out_block);

#ifdef UNIV_FLASH_CACHE_TRACE
		fc_print_used();
#endif
		srv_flash_cache_used -= data_size;
		srv_flash_cache_used_nocompress -= fc_block_get_orig_size(out_block);
			
		if (out_block->state == BLOCK_READY_FOR_FLUSH) {
			/* this block may have be added into backup array, so we will  not free the dirty block */
			srv_flash_cache_dirty -= data_size;
			free_block = 0;
		} else {
			free_block = 1;
		}

		++removed_pages;
		
		fc_block_delete_from_hash(out_block);
		
		flash_block_mutex_exit(out_block->fil_offset);
		/* free the block mutex for save memory */
			
		if (free_block) {
			fc_block_free(out_block);
		}	
	}	

	rw_lock_x_unlock(&fc->hash_rwlock);

	return removed_pages;
}
/********************************************************************//**
When srv_flash_cache_enable_write is FALSE, doublewrite buffer will behave as default. 
So if any page in doublewrite buffer now(newer) is also in L2 Cache already(olded),
it must be removed  from the L2 Cache before doublewrite buffer write to disk.
@return: pages removed in L2 Cache */
UNIV_INTERN
ulint
fc_block_remove_from_hash(
/*==================*/
	buf_dblwr_t* trx_dw)/*!< in: doublewrite structure */
{
	ulint i;
	ulint removed_pages = 0;
	fc_block_t* out_block = NULL;
	ulint space;
	ulint offset;
	ulint free_block;

	for (i = 0; i < trx_dw->first_free; ++i) {
		space = mach_read_from_4(trx_dw->write_buf
			+ i * UNIV_PAGE_SIZE + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
		offset = mach_read_from_4(trx_dw->write_buf
			+ i * UNIV_PAGE_SIZE + FIL_PAGE_OFFSET);
		
		rw_lock_x_lock(&fc->hash_rwlock);
		out_block = fc_block_search_in_hash(space, offset);
		if (out_block) {
			ulint data_size;
			flash_block_mutex_enter(out_block->fil_offset);
			data_size = fc_block_get_data_size(out_block);

#ifdef UNIV_FLASH_CACHE_TRACE
			fc_print_used();
#endif
			srv_flash_cache_used -= data_size;
			srv_flash_cache_used_nocompress -= fc_block_get_orig_size(out_block);
			
			if (out_block->state == BLOCK_READY_FOR_FLUSH) {
				/* this block may have be added into backup array, so we will  not free the dirty block */
				srv_flash_cache_dirty -= data_size;
				free_block = 0;
			} else {
				free_block = 1;
			}

			++removed_pages;
			fc_block_delete_from_hash(out_block);
		
			flash_block_mutex_exit(out_block->fil_offset);
			/* free the block mutex for save memory */
			
			if (free_block) {
				fc_block_free(out_block);
			}
			
		}	

		rw_lock_x_unlock(&fc->hash_rwlock);
	}
	
	return removed_pages;
}

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
	buf_page_t*	bpage)	/*!< in/out: read L2 Cache block to this page */
{
	dberr_t err;
	ulint blk_size;
	ulint block_offset, byte_offset;
	ibool using_ibuf_aio = false;
	fc_block_t *block = NULL;
	
	err = DB_SUCCESS;

	if (fc == NULL) {
		err = fil_io(OS_FILE_READ | wake_later, sync, space, zip_size, offset, 0, 
			zip_size ? zip_size : UNIV_PAGE_SIZE, buf, bpage);        
		return err;	
	}

	ut_ad(!mutex_own(&fc->mutex));

	rw_lock_s_lock(&fc->hash_rwlock);
	block = fc_block_search_in_hash(space, offset);
	if (block) {
		void* read_buf = NULL;
 		ut_a(block->state != BLOCK_NOT_USED);

        if (block->raw_zip_size) {
			if (block->is_v4_blk) {
				blk_size = block->raw_zip_size * fc_get_block_size();
			} else {
            	blk_size = fc_block_compress_align(block->raw_zip_size)
						* fc_get_block_size();
			}
        } else {
            blk_size = fc_calc_block_size(zip_size);
        }

		srv_flash_cache_read++;

		/* read hit in SSD */
		flash_block_mutex_enter(block->fil_offset);
		ut_a((block->io_fix & IO_FIX_READ) == IO_FIX_NO_IO);
		ut_a(bpage->fc_block == NULL);

		block->io_fix |= IO_FIX_READ;
		bpage->fc_block = block;
		
		if (ibuf_bitmap_page(zip_size, block->offset)
			|| trx_sys_hdr_page(block->space, block->offset)) {

			sync = TRUE;
		}
		
		if (!sync) {
			srv_flash_cache_aio_read++;
		}

		/* unlock before io is safe as io_fix is set */
		rw_lock_s_unlock(&fc->hash_rwlock);

		/* as the hit block must not be freed by fill or doublewrite, so it is safe to use this block out mutex */
		flash_block_mutex_exit(block->fil_offset);

		if (block->raw_zip_size) {
			if (srv_flash_cache_decompress_use_malloc == TRUE) {
				block->read_io_buf = ut_malloc(2 * UNIV_PAGE_SIZE);
			} else {
				block->read_io_buf = (void*)buf_block_alloc(NULL);
			}

		} else {
			block->read_io_buf = NULL;
		}


		if (block->raw_zip_size) {
			if (srv_flash_cache_decompress_use_malloc == TRUE) {
				read_buf = ut_align(block->read_io_buf, UNIV_PAGE_SIZE);
			} else {
				read_buf = ((buf_block_t*)block->read_io_buf)->frame;
			}
		} else {
			read_buf = buf;
		}
		
		if (!recv_no_ibuf_operations
			&& ibuf_page(block->space, zip_size, block->offset, NULL)) {
            /* ibuf page in L2 cache must use ibuf aio thread */
			using_ibuf_aio = TRUE;
			//fprintf(stderr, "fc_read_page (%lu, %lu) is ibuf page sync %lu\n", block->space, block->offset, sync);
		}

		fc_io_offset(block->fil_offset, &block_offset, &byte_offset);
		if (using_ibuf_aio) {
			err = fil_io(OS_FILE_READ | wake_later | OS_FORCE_IBUF_AIO,
					sync, FLASH_CACHE_SPACE, 0, block_offset, byte_offset,
					blk_size * KILO_BYTE, read_buf, bpage);
		} else {
			err = fil_io(OS_FILE_READ | wake_later ,
					sync, FLASH_CACHE_SPACE, 0, block_offset, byte_offset,
					blk_size * KILO_BYTE, read_buf, bpage);
		}

		if (sync) {
			fc_complete_read(bpage);
		}
		
	} else {
		rw_lock_s_unlock(&fc->hash_rwlock);
		err = fil_io(OS_FILE_READ | wake_later, sync, space, zip_size, offset, 0, 
				zip_size ? zip_size : UNIV_PAGE_SIZE, buf, bpage);
	}

	return err;
}

/********************************************************************//**
Compelete L2 Cache read. only read hit in ssd,
not move_migrate will into this function*/
UNIV_INTERN
void
fc_complete_read(
/*==============*/
	buf_page_t* bpage)	/*!< in: page to compelete io */
{
	ulint page_type;
	byte* buf;
	void* block_buf = NULL;			
	ulint raw_zip_size;
	fc_block_t* fb = (fc_block_t*)(bpage->fc_block);
#ifdef UNIV_FLASH_CACHE_TRACE
	ulint _offset;
	ulint _space;
#endif

	ut_ad(!mutex_own(&fc->mutex));
    ut_a(fb->io_fix & IO_FIX_READ);

	flash_block_mutex_enter(fb->fil_offset);
	
	if (bpage->zip.data) {
		buf = bpage->zip.data;
	} else {
		buf = ((buf_block_t*)bpage)->frame;
	}
	
	if (fb->raw_zip_size > 0) {
		void* read_buf = NULL;

		if (srv_flash_cache_decompress_use_malloc == TRUE) {
			read_buf = (byte*)ut_align(fb->read_io_buf, UNIV_PAGE_SIZE);
		} else {
			read_buf = ((buf_block_t*)fb->read_io_buf)->frame;
		}

#ifdef UNIV_FLASH_CACHE_TRACE
		ut_a(read_buf);
		ut_a(bpage->zip.data == NULL);
		ut_a(buf);
		fc_block_compress_check((unsigned char *)read_buf, fb);	
		/* only qlz can do this check  */
		if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
			if (fb->is_v4_blk) {
				ut_a(fb->raw_zip_size * fc_get_block_size_byte()
					>= (ulint)fc_qlz_size_compressed(((const char *)read_buf + FC_ZIP_PAGE_DATA)));
			} else {
				ut_a(fb->raw_zip_size 
					== (ulint)fc_qlz_size_compressed(((const char *)read_buf + FC_ZIP_PAGE_DATA)));
			}
			ut_a(UNIV_PAGE_SIZE 
				== fc_qlz_size_decompressed(((const char *)read_buf + FC_ZIP_PAGE_DATA)));
		}
#endif

		fc_block_do_decompress(DECOMPRESS_READ_SSD, read_buf, fb->raw_zip_size, buf);
		srv_flash_cache_decompress++;
	} else {
		ut_a(fb->read_io_buf == NULL);
	}

	/* if not compressed by L2 Cache, page data has already in buf after io */

#ifdef UNIV_FLASH_CACHE_TRACE
	_offset = mach_read_from_4(buf + FIL_PAGE_OFFSET);
	_space = mach_read_from_4(buf + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
	if (_offset != fb->offset || _space != fb->space) {
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: L2 Cache read error space:%lu-%lu,offset:%lu-%lu\n",
			(ulong)fb->space, (ulong)_space, (ulong)fb->offset, (ulong)_offset);
		fc_block_print(fb);
		ut_error;
	}

	if (buf_page_is_corrupted(true, buf, fil_space_get_zip_size(bpage->space))) {
		fc_block_print(fb);
		ut_error;
	}
#endif

	page_type = fil_page_get_type(buf);
	if (page_type == FIL_PAGE_INDEX) {
		page_type = 1;
	}
	srv_flash_cache_read_detail[page_type]++;

	bpage->fc_block = NULL;
	fb->io_fix &= ~IO_FIX_READ;
	if (fb->raw_zip_size > 0) {
		ut_a(fb->read_io_buf != NULL);
		block_buf = fb->read_io_buf;
		fb->read_io_buf = NULL;
	} else {
		ut_a(fb->read_io_buf == NULL);

	}

	raw_zip_size = fb->raw_zip_size;
	
	flash_block_mutex_exit(fb->fil_offset);

	if (raw_zip_size > 0) {
		if (srv_flash_cache_decompress_use_malloc == TRUE) {
			ut_free(block_buf);
		} else {
			buf_block_free((buf_block_t*)block_buf);
		}
	}
}

/********************************************************************//**
Compress the buf page bpage with quicklz, return the size of compress data.
 the buf memory has alloced
@return the compressed size of page */
static
ulint
fc_block_do_compress_quicklz(
/*==================*/
	ulint is_dw,		/*!< in: TRUE if compress for doublewrite buffer */
	buf_page_t* bpage, /*!< in: the data need compress is bpage->frame */
	void*	buf)	/*!< out: the buf contain the compressed data,
						must be the size of frame + 400 */
{
	ulint cp_size = UNIV_PAGE_SIZE;
	ulint page_size = UNIV_PAGE_SIZE;
	fc_qlz_state_compress *state_compress = NULL;

#ifdef UNIV_FLASH_CACHE_TRACE
	byte* tmp_unalign;
	byte* tmp;	
	ulint zip_size;
#endif

	if (is_dw == TRUE) {
		state_compress = (fc_qlz_state_compress*)fc->dw_zip_state;
	} else {
		state_compress = (fc_qlz_state_compress*)ut_malloc(sizeof(fc_qlz_state_compress));
	}

	/*
	 *  we compress the page data to the buf offset  FC_ZIP_PAGE_DATA, so when we do pack,
	 * the compressed data need not to be moved, and avoid a memcpy operation
	 */
	cp_size = fc_qlz_compress((const void*)((buf_block_t*)bpage)->frame, ((char*)buf + FC_ZIP_PAGE_DATA), 
					page_size, state_compress);

	if (is_dw == FALSE) {
		ut_free(state_compress);
	}

#ifdef UNIV_FLASH_CACHE_TRACE
	tmp_unalign = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
	tmp = (byte*)ut_align(tmp_unalign, UNIV_PAGE_SIZE);
	fc_block_do_decompress(DECOMPRESS_READ_SSD, buf, 0, tmp);
	
	zip_size = fil_space_get_zip_size(bpage->space);
	if (buf_page_is_corrupted(true, tmp, zip_size)) {
		ut_error;
	}

	ut_free(tmp_unalign);
#endif

	return cp_size;
}

/********************************************************************//**
Compress the buf page bpage with zlib, return the size of compress data.
 the buf memory has alloced
@return the compressed size of page */
static
ulint
fc_block_do_compress_zlib(
/*==================*/
	ulint is_dw,		/*!< in: TRUE if compress for doublewrite buffer */
	buf_page_t* bpage, /*!< in: the data need compress is bpage->frame */
	void*	buf)	/*!< out: the buf contain the compressed data,
						must be the size of frame + 400 */
{
	return UNIV_PAGE_SIZE;
}

#ifndef _WIN32
/********************************************************************//**
Compress the buf page bpage with snappy, return the size of compress data.
 the buf memory has alloced
@return the compressed size of page */
static
ulint
fc_block_do_compress_snappy(
/*==================*/
	ulint is_dw,		/*!< in: TRUE if compress for doublewrite buffer */
	buf_page_t* bpage, /*!< in: the data need compress is bpage->frame */
	void*	buf)	/*!< out: the buf contain the compressed data,
						must be the size of frame + 400 */
{
	ulint cp_size = UNIV_PAGE_SIZE;
	ulint page_size = UNIV_PAGE_SIZE;
	struct snappy_env* compress_env = NULL;

#ifdef UNIV_FLASH_CACHE_TRACE
	char* tmp_unalign;
	char* tmp;
	ulint zip_size;
#endif
	
	if (is_dw == TRUE) {
		compress_env = (struct snappy_env*)fc->dw_zip_state;
	} else {
		compress_env = (struct snappy_env*)ut_malloc(sizeof(struct snappy_env));
		snappy_init_env(compress_env);
	}

	ut_a((snappy_max_compressed_length(page_size) + FC_ZIP_PAGE_META_SIZE)
		<= 2 * page_size);
	/*
	 *	we compress the page data to the buf offset  FC_ZIP_PAGE_DATA, so when we do pack,
	 * the compressed data need not to be moved, and avoid a memcpy operation
	 */
	if (0 != snappy_compress(compress_env, (const char*)((buf_block_t*)bpage)->frame, page_size,
					(char*)buf + FC_ZIP_PAGE_DATA, &cp_size)) {
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: [warning]L2 Cache snappy when compress page:(%lu,%lu)\n",
			(ulong)bpage->space, (ulong)bpage->offset);
		goto exit;
	}

#ifdef UNIV_FLASH_CACHE_TRACE
	tmp_unalign = (char*)ut_malloc(2 * UNIV_PAGE_SIZE);
	tmp = (char*)ut_align(tmp_unalign, UNIV_PAGE_SIZE);
	fc_block_do_decompress(DECOMPRESS_READ_SSD, buf, cp_size, tmp);
	
	zip_size = fil_space_get_zip_size(bpage->space);
	ut_a(zip_size == 0);
	if (buf_page_is_corrupted(false, (const byte*)tmp, zip_size)) {
		ut_error;
	}
	
	ut_free(tmp_unalign);
#endif

exit:
	if (is_dw == FALSE) {
		snappy_free_env(compress_env);
		ut_free(compress_env);
	}	
	
	return cp_size;

}
#endif

/********************************************************************//**
Compress the buf page bpage, return the size of compress data.
the buf memory has alloced
@return the compressed size of page */
UNIV_INTERN
ulint
fc_block_do_compress(
/*==================*/
	ulint is_dw,		/*!< in: TRUE if compress for doublewrite buffer */
	buf_page_t* bpage, /*!< in: the data need compress is bpage->frame */
	void*	buf)	/*!< out: the buf contain the compressed data,
						must be the size of frame + 400 */
{
	srv_flash_cache_compress++;	
	
	if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
		return fc_block_do_compress_quicklz(is_dw, bpage, buf);
		
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY) {
#ifndef _WIN32
		return fc_block_do_compress_snappy(is_dw, bpage, buf);
#else
		return UNIV_PAGE_SIZE;
#endif
		
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_ZLIB) {
		return fc_block_do_compress_zlib(is_dw, bpage, buf);
		
	} else {
		return UNIV_PAGE_SIZE;
	}
}

/********************************************************************//**
Decompress the page in the block with quicklz, return the decompressed data size. */
UNIV_INTERN
ulint
fc_block_do_decompress_quicklz(
/*==================*/
	ulint decompress_type, /*!< in: decompress for read or backup or flush or recovery */
	void *buf_compressed,	/*!< in: contain the compressed data */
	void *buf_decompressed)	/*!< out: contain the data that have decompressed */
{
	ulint decp_size = 0;
	ulint count = 0;
	fc_qlz_state_decompress* state_decompress = NULL;

	ut_a(buf_compressed);
	ut_a(buf_decompressed);
	ut_a(UNIV_PAGE_SIZE == fc_qlz_size_decompressed((const char*)buf_compressed + FC_ZIP_PAGE_DATA));

	if (decompress_type == DECOMPRESS_READ_SSD) {
retry:
		state_decompress = (fc_qlz_state_decompress*)
				ut_malloc(sizeof(fc_qlz_state_decompress));
		if (state_decompress == NULL) {
			ut_print_timestamp(stderr);
			count++;
			if (count >= 10) {
				fprintf(stderr,
					" InnoDB: L2 cache: alloc memory for fc_qlz_state_decompress failed.\n");
				ut_error;
			} else {
				goto retry;
			}
		}
	} else if (decompress_type == DECOMPRESS_FLUSH) {
		state_decompress = (fc_qlz_state_decompress*)fc->flush_dezip_state;
	} else if (decompress_type == DECOMPRESS_RECOVERY) {
		state_decompress = (fc_qlz_state_decompress*)fc->recv_dezip_state;
	} else if (decompress_type == DECOMPRESS_BACKUP) {
		/* when backup the recovery do not work */
		state_decompress = (fc_qlz_state_decompress*)fc->recv_dezip_state;
	}

	ut_a(state_decompress);

	decp_size = fc_qlz_decompress((const char*)buf_compressed + FC_ZIP_PAGE_DATA,
					buf_decompressed, state_decompress);

	if (decompress_type == DECOMPRESS_READ_SSD) {
		ut_free((void *)state_decompress);
	}
	
	if (decp_size != UNIV_PAGE_SIZE) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 cache:decompress size is not right dz%d.\n", 
			(int)decp_size);
		ut_error;
	}
	
	return decp_size;
}

#ifndef _WIN32
/********************************************************************//**
Decompress the page in the block with snappy, return the decompressed data size. */
static
ulint
fc_block_do_decompress_snappy(
/*==================*/
	void *buf_compressed,	/*!< in: contain the compressed data */
	ulint compressed_size,	/*!< in: the compressed data buffer size */	
	void *buf_decompressed)	/*!< out: contain the data that have decompressed */
{
	int ret;
	size_t decp_size;
	
	ut_a(buf_compressed);
	ut_a(buf_decompressed);
	
	snappy_uncompressed_length((const char*)buf_compressed + FC_ZIP_PAGE_DATA, 
		compressed_size, &decp_size);
	
	if (UNIV_PAGE_SIZE != decp_size) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 cache: snappy decompress check failed %d.\n", (int)decp_size);
		ut_error;
	}
	
	ret = snappy_uncompress((const char*)buf_compressed + FC_ZIP_PAGE_DATA,
				compressed_size, (char*)buf_decompressed);
		
	if (ret != 0) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 cache: snappy decompress failed %d.\n", ret);
		ut_error;
	}
		
	return decp_size;

}
#endif

/********************************************************************//**
Decompress the page in the block with zlib, return the decompressed data size. */
static
ulint
fc_block_do_decompress_zlib(
/*==================*/
	ulint decompress_type, /*!< in: decompress for read or backup or flush or recovery */
	void *buf_compressed,	/*!< in: contain the compressed data */
	void *buf_decompressed)	/*!< out: contain the data that have decompressed */
{
	
	return UNIV_PAGE_SIZE;
}

/********************************************************************//**
Decompress the page in the block, return the decompressed data size.
@return the decompressed size of page, must be UNIV_PAGE_SIZE */
UNIV_INTERN
ulint
fc_block_do_decompress(
/*==================*/
	ulint decompress_type, /*!< in: decompress for read or backup or flush or recovery */
	void *buf_compressed,	/*!< in: contain the compressed data */
	ulint compressed_size,	/*!< in: the compressed data buffer size */	
	void *buf_decompressed)	/*!< out: contain the data that have decompressed */
{
	/* we add the decompress counts out of the function to skip the flush decompress */
	//srv_flash_cache_decompress++;

	if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
		return fc_block_do_decompress_quicklz(decompress_type,
					buf_compressed, buf_decompressed);
		
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY) {
#ifndef _WIN32
		return fc_block_do_decompress_snappy(buf_compressed, 
			compressed_size, buf_decompressed);
#else
		return UNIV_PAGE_SIZE;
#endif
		
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_ZLIB) {
		return fc_block_do_decompress_zlib(decompress_type,
					buf_compressed, buf_decompressed);
	} else {
		return UNIV_PAGE_SIZE;
	}

}

/********************************************************************//**
Write compress algrithm to the compress data buffer. */
UNIV_INTERN
void
fc_block_write_compress_alg(
/*==================*/
	ulint compress_algrithm,/*!< in: the compress algrithm to write */
	void *buf)				/*!< in/out: the compressed data buf need to write */
{
	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_ZIP_ALG, compress_algrithm);
	//FIXME: add some debug code here
}

/********************************************************************//**
Read compress algrithm from compressed data buffer. 
@return the compress algrithm of page */
UNIV_INTERN
ulint
fc_block_read_compress_alg(
/*==================*/
	void *buf)				/*!< in: the compressed data buf */
{
	return mach_read_from_4((const unsigned char*)buf + FC_ZIP_PAGE_ZIP_ALG);
	//FIXME: add some debug code here
}


/********************************************************************//**
Pack the compressed data with block header and tailer. */
UNIV_INTERN
void
fc_block_pack_compress(
/*==================*/
	fc_block_t* block,	/*!< in: the block which is compressed, ready for pack */
	void *buf)			/*!< in/out: the compressed data buf need to pack */
{
	ulint page_size; /* bytes of the block */

#ifdef UNIV_FLASH_CACHE_TRACE
	void* tmp_unalign = ut_malloc(2 * UNIV_PAGE_SIZE);
	void* tmp = ut_align(tmp_unalign, UNIV_PAGE_SIZE);
	ulint zip_size = fil_space_get_zip_size(block->space);	
	fc_block_do_decompress(DECOMPRESS_READ_SSD, buf, block->raw_zip_size, tmp);

#ifndef _WIN32
	if (buf_page_is_corrupted(true, (const unsigned char*)tmp, zip_size)) {
		ut_error;
	} 
#else
	if (buf_page_is_corrupted(false, (const unsigned char*)tmp, zip_size)) {
		ut_error;
	} 
#endif
	
	if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
		if(fc_block_get_data_size(block) * fc_get_block_size_byte()
			< (fc_qlz_size_compressed((const char*)buf + FC_ZIP_PAGE_DATA) + FC_ZIP_PAGE_META_SIZE)){
			//printf("block size %lu, compressed size %lu \n", 
			//	fc_block_get_data_size(block) * fc_get_block_size_byte(),
			//	fc_qlz_size_compressed(buf + FC_ZIP_PAGE_DATA));
			ut_a(0);
		}
	}
#endif

	/* calc the zip size of the block */
	page_size = fc_block_compress_align(block->raw_zip_size) * fc_get_block_size_byte();

	/*
	  *  we have already compress the page data to the buf + FC_ZIP_PAGE_DATA, so need not to
	  *  do malloc and memcpy the page data to tmp buf
	  */

	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_HEADER, FC_ZIP_PAGE_CHECKSUM);
	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_SIZE, page_size);
	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_SPACE, block->space);
	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_OFFSET, block->offset);
	mach_write_to_4((unsigned char*)buf + page_size - FC_ZIP_PAGE_TAILER, FC_ZIP_PAGE_CHECKSUM);

	/* calc the original size of the block */
	page_size = block->size * fc_get_block_size_byte();
	ut_a(page_size == UNIV_PAGE_SIZE);
	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_ORIG_SIZE, UNIV_PAGE_SIZE);

	mach_write_to_4((unsigned char*)buf + FC_ZIP_PAGE_ZIP_RAW_SIZE, block->raw_zip_size);

#ifdef UNIV_FLASH_CACHE_TRACE
	fc_block_compress_check((unsigned char*)buf, block);
	fc_block_do_decompress(DECOMPRESS_READ_SSD, buf, block->raw_zip_size, tmp);
	
#ifndef _WIN32
	if (buf_page_is_corrupted(true, (const unsigned char*)tmp, zip_size)) {
		ut_error;
	} 
#else
	if (buf_page_is_corrupted(false, (const unsigned char*)tmp, zip_size)) {
		ut_error;
	} 
#endif

	ut_free(tmp_unalign);
#endif

	srv_flash_cache_compress_pack++;
}

/********************************************************************//**
Print L2 Cache status. */
UNIV_INTERN
void
fc_status(
/*=================================*/
	ulint page_read_delta,	/*!< in: page_read_delta from buf pool */
	ulint n_ra_pages_read,	/*!< in: read ahead page counts */
	ulint n_pages_read,		/*!< in: read page counts */
	FILE* file)				/*!< in: print the fc status to this file */
{
	time_t cur_time = ut_time();
	ulint distance = 0;
	ulint fc_size = fc_get_size();
	ulint fc_read_point = 0;
	ulint fc_size_mb = fc_size * fc_get_block_size() / KILO_BYTE;
	ulint can_cache = fc_size_mb;
	double pack_pst	= 0;
	double compress_ratio = 0;
	ulint z;

#ifdef UNIV_FLASH_CACHE_TRACE
	ullint start_time = ut_time_us(NULL);
	ullint end_time;
#endif

	if (fc->write_round == fc->flush_round) {
		distance = fc->write_off - fc->flush_off;
	} else {
		distance = fc_get_size() + fc->write_off - fc->flush_off;
	}

	for (z=0; z<=FIL_PAGE_TYPE_ZBLOB2; z++) {
		fc_read_point = fc_read_point + srv_flash_cache_read_detail[z];
	}

	if ((srv_flash_cache_used != 0) 
		&& (srv_flash_cache_used_nocompress != 0) 
		&& (srv_flash_cache_compress != 0)) {

		compress_ratio = srv_flash_cache_used * 100.0 / srv_flash_cache_used_nocompress;
		if (compress_ratio != 0) {
			can_cache = (ulint)((fc_size_mb * 100.0) / compress_ratio);
		} 
		
		pack_pst = (100.0 * srv_flash_cache_compress_pack) / srv_flash_cache_compress;
	}

	fputs("----------------------\n"
	"FLASH CACHE INFO\n"
	"----------------------\n", file);

	fprintf(file,	"flash cache thread status: %s \n"
					"flash cache size: %lu (%lu MB), write to %lu(%lu), flush to %lu(%lu), distance %lu (%.2f%%)\n"
					"flash cache used: %lu(%.2f%%), compress_ratio: %.2f%%, can_cache: %lu MB, io skip: %lu\n"
					"flash cache reads %lu, aio read %lu, writes %lu, single_write %lu, dirty %lu(%.2f%%), flush %lu(%lu).\n"
					"flash cache migrate %lu, move %lu, compress %lu, pack %lu(%.2f%%), decompress %lu\n"
					"FIL_PAGE_INDEX reads: %lu(%.2f%%): writes: %lu, flush: %lu, merge raio %.2f%%\n"
					"FIL_PAGE_INODE reads: %lu(%.2f%%): writes: %lu, flush: %lu, merge raio %.2f%%\n"
					"FIL_PAGE_UNDO_LOG reads: %lu(%.2f%%): writes: %lu, flush: %lu, merge raio %.2f%%\n"
					"FIL_PAGE_TYPE_SYS reads: %lu(%.2f%%): writes: %lu, flush: %lu, merge raio %.2f%%\n"
					"FIL_PAGE_TYPE_TRX_SYS reads: %lu(%.2f%%): writes: %lu, flush: %lu, merge raio %.2f%%\n"
					"FIL_PAGE_OTHER reads: %lu(%.2f%%): writes: %lu, flush: %lu\n"
					"flash cache read hit ratio %.2f%% in %lu second(total %.2f%%), merge write ratio %.2f%%\n"
					"flash cache %.2f reads/s, %.2f writes/s. %.2f flush/s, %.2f merge writes/s, %.2f migrate/s, %.2f move/s\n",
					srv_flash_cache_thread_op_info,
					(ulong)fc_size,
					(ulong)fc_size_mb,
					(ulong)fc->write_off,
					(ulong)fc->write_round,
					(ulong)fc->flush_off,
					(ulong)fc->flush_round,
					(ulong)distance,
					(100.0 * distance) / fc_size,
					(ulong)srv_flash_cache_used,
					(100.0 * srv_flash_cache_used) / fc_size,
					(100.0 - compress_ratio),
					(ulong)can_cache,
					(ulong)srv_flash_cache_wait_aio,
					(ulong)srv_flash_cache_read,
					(ulong)srv_flash_cache_aio_read,
					(ulong)srv_flash_cache_write,
					(ulong)srv_flash_cache_single_write,					
					(ulong)srv_flash_cache_dirty,
					(100.0 * srv_flash_cache_dirty) / fc_size,
					(ulong)srv_flash_cache_flush,
					(ulong)srv_flash_cache_merge_write,
					(ulong)srv_flash_cache_migrate,
					(ulong)srv_flash_cache_move,
					(ulong)srv_flash_cache_compress,
					(ulong)srv_flash_cache_compress_pack,
					 pack_pst,
					(ulong)srv_flash_cache_decompress,
					(ulong)srv_flash_cache_read_detail[1],(100.0*srv_flash_cache_read_detail[1])/(fc_read_point),
					(ulong)srv_flash_cache_write_detail[1],(ulong)srv_flash_cache_flush_detail[1],100.0-(100.0*srv_flash_cache_flush_detail[1])/srv_flash_cache_write_detail[1],
					(ulong)srv_flash_cache_read_detail[FIL_PAGE_INODE],(100.0*srv_flash_cache_read_detail[FIL_PAGE_INODE])/(fc_read_point),
					(ulong)srv_flash_cache_write_detail[FIL_PAGE_INODE],(ulong)srv_flash_cache_flush_detail[FIL_PAGE_INODE],100.0-(100.0*srv_flash_cache_flush_detail[FIL_PAGE_INODE])/srv_flash_cache_write_detail[FIL_PAGE_INODE],
					(ulong)srv_flash_cache_read_detail[FIL_PAGE_UNDO_LOG],(100.0*srv_flash_cache_read_detail[FIL_PAGE_UNDO_LOG])/(fc_read_point),
					(ulong)srv_flash_cache_write_detail[FIL_PAGE_UNDO_LOG],(ulong)srv_flash_cache_flush_detail[FIL_PAGE_UNDO_LOG],100.0-(100.0*srv_flash_cache_flush_detail[FIL_PAGE_UNDO_LOG])/srv_flash_cache_write_detail[FIL_PAGE_UNDO_LOG],
					(ulong)srv_flash_cache_read_detail[FIL_PAGE_TYPE_SYS],(100.0*srv_flash_cache_read_detail[FIL_PAGE_TYPE_SYS])/(fc_read_point),
					(ulong)srv_flash_cache_write_detail[FIL_PAGE_TYPE_SYS],(ulong)srv_flash_cache_flush_detail[FIL_PAGE_TYPE_SYS],100.0-(100.0*srv_flash_cache_flush_detail[FIL_PAGE_TYPE_SYS])/srv_flash_cache_write_detail[FIL_PAGE_TYPE_SYS],
					(ulong)srv_flash_cache_read_detail[FIL_PAGE_TYPE_TRX_SYS],(100.0*srv_flash_cache_read_detail[FIL_PAGE_TYPE_TRX_SYS])/(fc_read_point),
					(ulong)srv_flash_cache_write_detail[FIL_PAGE_TYPE_TRX_SYS],(ulong)srv_flash_cache_flush_detail[FIL_PAGE_TYPE_TRX_SYS],100.0-(100.0*srv_flash_cache_flush_detail[FIL_PAGE_TYPE_TRX_SYS])/srv_flash_cache_write_detail[FIL_PAGE_TYPE_TRX_SYS],
					(ulong)(srv_flash_cache_read_detail[FIL_PAGE_IBUF_FREE_LIST] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_ALLOCATED]
								+ srv_flash_cache_read_detail[FIL_PAGE_IBUF_BITMAP] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_FSP_HDR]
								+ srv_flash_cache_read_detail[FIL_PAGE_TYPE_XDES] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_BLOB]
								+ srv_flash_cache_read_detail[FIL_PAGE_TYPE_ZBLOB] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_ZBLOB2]
							),
					(100.0 * (srv_flash_cache_read_detail[FIL_PAGE_IBUF_FREE_LIST] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_ALLOCATED]
								+ srv_flash_cache_read_detail[FIL_PAGE_IBUF_BITMAP] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_FSP_HDR]
								+ srv_flash_cache_read_detail[FIL_PAGE_TYPE_XDES] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_BLOB]
								+ srv_flash_cache_read_detail[FIL_PAGE_TYPE_ZBLOB] + srv_flash_cache_read_detail[FIL_PAGE_TYPE_ZBLOB2]
							))/(fc_read_point),
					(ulong)(srv_flash_cache_write_detail[FIL_PAGE_IBUF_FREE_LIST] + srv_flash_cache_write_detail[FIL_PAGE_TYPE_ALLOCATED]
								+ srv_flash_cache_write_detail[FIL_PAGE_IBUF_BITMAP] + srv_flash_cache_write_detail[FIL_PAGE_TYPE_FSP_HDR]
								+ srv_flash_cache_write_detail[FIL_PAGE_TYPE_XDES] + srv_flash_cache_write_detail[FIL_PAGE_TYPE_BLOB]
								+ srv_flash_cache_write_detail[FIL_PAGE_TYPE_ZBLOB] + srv_flash_cache_write_detail[FIL_PAGE_TYPE_ZBLOB2]
							),
					(ulong)(srv_flash_cache_flush_detail[FIL_PAGE_IBUF_FREE_LIST] + srv_flash_cache_flush_detail[FIL_PAGE_TYPE_ALLOCATED]
								+ srv_flash_cache_flush_detail[FIL_PAGE_IBUF_BITMAP] + srv_flash_cache_flush_detail[FIL_PAGE_TYPE_FSP_HDR]
								+ srv_flash_cache_flush_detail[FIL_PAGE_TYPE_XDES] + srv_flash_cache_flush_detail[FIL_PAGE_TYPE_BLOB]
								+ srv_flash_cache_flush_detail[FIL_PAGE_TYPE_ZBLOB] + srv_flash_cache_flush_detail[FIL_PAGE_TYPE_ZBLOB2]
							),
					(ulong)(page_read_delta == 0)?0:100.0*( srv_flash_cache_read - flash_cache_stat.n_pages_read ) / ( page_read_delta ),
					(ulong)difftime(cur_time,flash_cache_stat.last_printout_time),
					(ulong)(srv_flash_cache_read==0)?0:(100.0*srv_flash_cache_read)/(n_pages_read + n_ra_pages_read),
					(100.0 * srv_flash_cache_merge_write)/(srv_flash_cache_write - srv_flash_cache_migrate - srv_flash_cache_move),
					( srv_flash_cache_read - flash_cache_stat.n_pages_read ) / difftime(cur_time,flash_cache_stat.last_printout_time),
					( srv_flash_cache_write - flash_cache_stat.n_pages_write ) / difftime(cur_time,flash_cache_stat.last_printout_time),
					( srv_flash_cache_flush - flash_cache_stat.n_pages_flush ) / difftime(cur_time,flash_cache_stat.last_printout_time),
					( srv_flash_cache_merge_write - flash_cache_stat.n_pages_merge_write ) / difftime(cur_time,flash_cache_stat.last_printout_time),
					( srv_flash_cache_migrate - flash_cache_stat.n_pages_migrate ) / difftime(cur_time,flash_cache_stat.last_printout_time),
					( srv_flash_cache_move - flash_cache_stat.n_pages_move ) / difftime(cur_time,flash_cache_stat.last_printout_time)
		);

		fc_update_status(UPDATE_INNODB_STATUS);
		

#ifdef UNIV_FLASH_CACHE_TRACE
  {
		end_time = ut_time_us(NULL);
		fprintf(fc->f_debug, "take %lu us to print fc status\n",
			(ulong)(end_time - start_time));
  }
#endif

	return;
}
