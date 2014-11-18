/**************************************************//**
@file fc/fc0flu.c
Flash Cache for InnoDB

Created	24/10/2013 Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0flu.h"

#ifdef UNIV_NONINL
#include "fc0flu.ic"
#endif

#include "log0recv.h"
#include "fc0log.h"

/* the last time commit the fc log when flush dirty pages */
UNIV_INTERN ulint  srv_fc_flush_last_commit = 0;

/* the last time when dump the block metadata to dump file */
UNIV_INTERN ulint  srv_fc_flush_last_dump = 0;

/* whether  the fc log should be commit to ssd, if not zero, the log should be commited */
UNIV_INTERN ulint srv_fc_flush_should_commit_log_flush = 0;

/* whether  the fc log should be commit to ssd, if not zero, the log should be commited */
UNIV_INTERN ulint srv_fc_flush_should_commit_log_write = 0;

#define FLASH_CACHE_FLUSH_LOG_PERIOD 60000 /* 60s */

#define FLASH_CACHE_DUMP_BLOCK_META_PERIOD 60000 /* 60s */

/********************************************************************//**
Flush a batch of writes to the datafiles that have already been
written by the OS. */
UNIV_INTERN
void
fc_flush_sync_dbfile(void)
/*==========================*/
{
	/* Wake possible simulated aio thread to actually post the
	writes to the operating system */
	os_aio_simulated_wake_handler_threads();

	/* Wait that all async writes to tablespaces have been posted to
	the OS */
	os_aio_wait_until_no_pending_writes();

	/* Now we flush the data to disk (for example, with fsync) */
	fil_flush_file_spaces(FIL_TABLESPACE);

	return;
}

/********************************************************************//**
Flush pages from flash cache.
@return	number of pages have been flushed to tablespace */
UNIV_INTERN
ulint	
fc_flush_to_disk(
/*==================*/
	ibool do_full_io)	/*!< in: whether do full io capacity */
{
	ulint distance;
	byte* page;
	ulint ret;
	ulint space;
	ulint offset;
	ulint page_type;
	ulint i, j;
	ulint pos;
	ulint zip_size;
	ulint block_offset, byte_offset;
	ulint fc_size = fc_get_size();
	ulint fc_blk_size = fc_get_block_size_byte();
	ulint start_offset;
   	ulint data_size;
	fc_block_t *flush_block = NULL;
	ulint c_flush = 0;
    
	ut_ad(!mutex_own(&fc->mutex));
	ut_a(fc->flush_buf->free_pos == 0);

	/* step 1: get the number of blocks need to flush to tablespace */
	flash_cache_mutex_enter();

	distance = fc_get_distance();
	start_offset = fc->flush_off;
    
	if ( distance == 0 ) {
		flash_cache_mutex_exit();
		return 0;
	} else if ( recv_recovery_on ) {
		if ( distance < (( 1.0 * srv_flash_cache_write_cache_pct /100 ) * fc_size)) {
			fc->n_flush_cur = 0;
		} else if ( distance < ( ( 1.0*srv_flash_cache_do_full_io_pct /100 ) * fc_size)) {
			fc->n_flush_cur = ut_min(PCT_IO_FC(10), distance);
		} else {
			fc->n_flush_cur = ut_min(PCT_IO_FC(100), distance);
		}
	} else if ( distance < (( 1.0 * srv_flash_cache_write_cache_pct /100 ) * fc_size)
		&& !do_full_io ) {
		flash_cache_mutex_exit();
		return 0;
	} else if ( distance < (( 1.0 * srv_flash_cache_do_full_io_pct/100 ) * fc_size)
		&& !do_full_io ) {
		fc->n_flush_cur = PCT_IO_FC(srv_fc_write_cache_flush_pct);
	} else {
		ut_ad((distance > ( 1.0 * srv_flash_cache_do_full_io_pct/100 ) * fc_size) 
			|| do_full_io );
		fc->n_flush_cur = ut_min(PCT_IO_FC(srv_fc_full_flush_pct), distance);
	}

	flash_cache_mutex_exit();

	/* step 2: start to flush blocks use async io, set block io_fix IO_FIX_FLUSH */
	i = 0;
	while (i < fc->n_flush_cur) {
		ulint b_space;
		ulint b_offset;
		ulint raw_zip_size;
		ulint size;
		ulint fil_offset;
#ifdef UNIV_FLASH_CACHE_TRACE
		ulint is_v4_blk;
#endif
		byte* page_io;

		flash_cache_mutex_enter();
		pos = ( start_offset + i ) % fc_size;
		flush_block = fc_get_block(pos);

		if (flush_block == NULL) {
			i++;
			flash_cache_mutex_exit();
			continue;
		}

		/* we should get the mutex, as doublewrite may hit this block and invalid the block */
		flash_block_mutex_enter(flush_block->fil_offset);

		flash_cache_mutex_exit();
		
		data_size = fc_block_get_data_size(flush_block);

		if (flush_block->state != BLOCK_READY_FOR_FLUSH) {
			/* if readonly or merge write or already flushed*/
			ut_a (flush_block->state == BLOCK_NOT_USED
				|| flush_block->state == BLOCK_READ_CACHE
				|| flush_block->state == BLOCK_FLUSHED);
			
			i += data_size;

			flash_block_mutex_exit(flush_block->fil_offset);
			if (flush_block->state == BLOCK_NOT_USED) {
				//fc_block_detach(FALSE, flush_block);
				fc_block_free(flush_block);
			}
			
			continue;
		}

		zip_size = fil_space_get_zip_size(flush_block->space);
		if (zip_size == ULINT_UNDEFINED) {
			/* table has been droped, just set it BLOCK_FLUSHED */
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(fc->f_debug);
			fprintf(fc->f_debug, "space:%lu is droped, the page(%lu, %lu) need not to be flushed.\n",
			(ulong)flush_block->space, (ulong)flush_block->space, (ulong)flush_block->offset);
#endif
			flush_block->state = BLOCK_FLUSHED;
			i += data_size;
			c_flush += data_size;
			flash_block_mutex_exit(flush_block->fil_offset);
			continue;
		}

#ifdef UNIV_FLASH_CACHE_TRACE
		if (flush_block->state != BLOCK_READY_FOR_FLUSH) {
			fc_block_print(flush_block);
			ut_error;
		}
#endif

		flush_block->io_fix |= IO_FIX_FLUSH;

		/* 
		 * we should set block state BLOCK_FLUSHED,  if not, doublewrite may hit this block 
		 * and invalid this block and reduce the dirty count, but when finish flush ,we will 
		 * reduce the dirty count too, so it may reduce twice.
		 */
		flush_block->state = BLOCK_FLUSHED;
		
		/* save the block info, as the block may be invalided by doublewrite after release mutex */
		b_space = flush_block->space;
		b_offset = flush_block->offset;

		raw_zip_size = flush_block->raw_zip_size;
		size = flush_block->size;
		fil_offset = flush_block->fil_offset;
#ifdef UNIV_FLASH_CACHE_TRACE
		is_v4_blk = flush_block->is_v4_blk;
#endif
		/* release the block now, so read can hit in this blocks and read the data */
		flash_block_mutex_exit(flush_block->fil_offset);
		
		/*
		 * Only flush thread will update read_buf and flush_off/round. 
		 * there only single flush thread no need to lock read_buf
		 */
		page = fc->flush_buf->buf + fc->flush_buf->free_pos * fc_blk_size;

		if (raw_zip_size > 0) {
			ut_a((size * fc_blk_size) == UNIV_PAGE_SIZE);
			page_io = fc->flush_zip_read_buf;
		} else {
			page_io = page;
		}

		fc_io_offset(fil_offset, &block_offset, &byte_offset);
		ret = fil_io(OS_FILE_READ, TRUE, FLASH_CACHE_SPACE, 0,
				block_offset, byte_offset, data_size * fc_blk_size,
				page_io, NULL);
	
		if (ret != DB_SUCCESS) {
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: Flash cache [Error]: unable to read page from flash cache.\n"
				"flash cache flush offset is:%lu.\n", (ulong)(start_offset + i));
			ut_error;
		}		

		if ((flush_block != NULL) && (flush_block->state == BLOCK_NOT_USED)) {
			goto skip;
		}

		/* decompress the compress data */
		if (raw_zip_size > 0) {
#ifdef UNIV_FLASH_CACHE_TRACE
			ulint blk_zip_size_byte;
			if (is_v4_blk) {
				blk_zip_size_byte = raw_zip_size * fc_get_block_size_byte();
			} else {
				blk_zip_size_byte = fc_block_compress_align(raw_zip_size) * fc_get_block_size_byte();
				ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_ZIP_RAW_SIZE) == raw_zip_size);				
			} 

			ut_a(page_io);
			ut_a(page);
			ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_HEADER) == FC_ZIP_PAGE_CHECKSUM);
			ut_a((ulint)mach_read_from_4(page_io + blk_zip_size_byte - FC_ZIP_PAGE_TAILER)
				== FC_ZIP_PAGE_CHECKSUM);	
			ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_SIZE) == blk_zip_size_byte);
			ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_ORIG_SIZE) == UNIV_PAGE_SIZE);		
			ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_SPACE) == b_space);
			ut_a((ulint)mach_read_from_4(page_io + FC_ZIP_PAGE_OFFSET) == b_offset);	

			/* only qlz can do this check  */
			if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
				if (is_v4_blk) {
					ut_a(raw_zip_size * fc_get_block_size_byte()
						>= (ulint)fc_qlz_size_compressed((const char *)(page_io + FC_ZIP_PAGE_DATA)));
				} else {
					ut_a(raw_zip_size 
						== (ulint)fc_qlz_size_compressed((const char *)(page_io + FC_ZIP_PAGE_DATA)));
				}
				
				ut_a(UNIV_PAGE_SIZE == fc_qlz_size_decompressed((const char *)(page_io + FC_ZIP_PAGE_DATA)));
			}
#endif
			fc_block_do_decompress(DECOMPRESS_FLUSH, page_io, raw_zip_size, page);
		}

		space = mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
		offset = mach_read_from_4(page + FIL_PAGE_OFFSET);

		if ((space != b_space) || (offset != b_offset)) {
			ut_print_timestamp(stderr); 
			fc_block_print(flush_block);
			ut_error;
		}

		if (buf_page_is_corrupted(true, page, zip_size)) {
			buf_page_print(page, zip_size, BUF_PAGE_PRINT_NO_CRASH);
			ut_error;
		}		
		
		page_type = fil_page_get_type(page);
		if (page_type == FIL_PAGE_INDEX) {
			page_type = 1;
		}
		srv_flash_cache_flush_detail[page_type]++;
		
		ret = fil_io(OS_FILE_WRITE | OS_AIO_SIMULATED_WAKE_LATER, FALSE, space, 
				zip_size, offset, 0, zip_size ? zip_size : UNIV_PAGE_SIZE, page, NULL);
		if (ret != DB_SUCCESS && ret != DB_TABLESPACE_DELETED) {
			ut_print_timestamp(stderr); 
			fc_block_print(flush_block);
			ut_error;
		}

		/* add  UNIV_PAGE_SIZE / fc_blk_size for safe */
		fc->flush_buf->free_pos += UNIV_PAGE_SIZE / fc_blk_size;	

skip:
		i += data_size;
		c_flush += data_size;	

		if ((fc->flush_buf->free_pos + UNIV_PAGE_SIZE / fc_blk_size) >= fc->flush_buf->size) {
			/* FIXME: is it safe to change n_flush, as step 3 will use n_flush */
			fc->n_flush_cur = i;
			break;
		}	
	}

	/* ok, now flush all async io to disk */
	fc_flush_sync_dbfile();

	/* step 3: all the flush blocks have sync to disk,  update the state and io_fix */
	j = 0;
	while (j < fc->n_flush_cur) {

		flash_cache_mutex_enter();
		pos = (start_offset + j) % fc_size;
		flush_block = fc_get_block(pos);

		if (flush_block  == NULL) {
			j++;
			flash_cache_mutex_exit();
			continue;
		}
		/* block state and io_fix may be changed by doublewrite and lru move */
		flash_block_mutex_enter(flush_block->fil_offset);
		flash_cache_mutex_exit();
		if (flush_block->io_fix & IO_FIX_FLUSH) {
			/* the block is already in BLOCK_FLUSHED state */
			flush_block->io_fix &= ~IO_FIX_FLUSH;
		} 
		
		data_size = fc_block_get_data_size(flush_block);
		flash_block_mutex_exit(flush_block->fil_offset);	
		
		j += data_size;
	}

	
	/*
	 * i and j may be different, as the last been flushed block may be invalid by doublewrite,
	 * so maybe i > j
	 */
	
	/* add the actual flushed blocks */
	srv_flash_cache_flush = srv_flash_cache_flush + c_flush; 

	/* step 4: update fc status and flush_off, and wake up threads that are sleep for space  */
	if (i > 0) {
		ut_a(i >= c_flush);

		flash_cache_mutex_enter();
		
		/*
		 * it is safe to inc flush off and sub dirty blocks at this time,
		 * as fc_validate is not work
		 */
		fc_inc_flush_off(i);
		flash_cache_log_mutex_enter();
		fc_log->current_stat->flush_offset = fc->flush_off;
		fc_log->current_stat->flush_round = fc->flush_round;	
		flash_cache_log_mutex_exit();		
		
		ut_a(srv_flash_cache_dirty >= c_flush);		
		srv_flash_cache_dirty -= c_flush;
		
		srv_fc_flush_should_commit_log_flush++;
		os_event_set(fc->wait_space_event);	

		fc->n_flush_cur = 0;
		
		flash_cache_mutex_exit();		
	}

	fc->flush_buf->free_pos = 0;
 
	return c_flush;
}

/********************************************************************//**
Test and flush the fc log to disk if necessary. */
UNIV_INTERN
void
fc_flush_test_and_flush_log(
/*==========================*/
	ulint last_time) /*!< in: the last time when flush the fc log */
{
	ulint curr_time = 0;

	curr_time = ut_time_ms();
	if (((curr_time - FLASH_CACHE_FLUSH_LOG_PERIOD) > last_time) 
		&& (srv_fc_flush_should_commit_log_flush > 0)) {

#ifdef UNIV_FLASH_CACHE_TRACE
		flash_cache_mutex_enter();

		if (fc->is_finding_block == 1) {
			flash_cache_mutex_exit();
			fc_log_commit_when_update_flushoff();
			return;
		}

		fc_validate();
		flash_cache_mutex_exit();
#endif
		fc_log_commit_when_update_flushoff();
	}
}

/********************************************************************//**
Test and dump block metadata to dump file if necessary. */
UNIV_INTERN
void
fc_flush_test_and_dump_blkmeta(
/*==========================*/
	ulint last_time) /*!< in: the last time when
						dump the block metadata */
{
	ulint curr_time = 0;

	curr_time = ut_time_ms();
	if (((curr_time - FLASH_CACHE_DUMP_BLOCK_META_PERIOD) > last_time)) {
		/*FIXME: seal with a function*/
		flash_cache_mutex_enter();
		rw_lock_s_lock(&fc->hash_rwlock);
		fc_dump();
		rw_lock_s_unlock(&fc->hash_rwlock);
		
		flash_cache_log_mutex_enter();
		fc_log_update(FALSE, FLASH_CACHE_LOG_UPDATE_DUMP);
		fc_log_update_commit_status();
		
		flash_cache_mutex_exit();
	
		fc_log_commit();
		flash_cache_log_mutex_exit();

		srv_fc_flush_last_dump = ut_time_ms();
	}
}

