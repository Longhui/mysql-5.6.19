/**************************************************//**
@file fc/fc0fill.c
Flash Cache(L2 Cache) for InnoDB

Created	24/10/2013 Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/
#include "fc0fill.h"
#include "fc0log.h"

#ifdef UNIV_NONINL
#include "fc0fill.ic"
#endif

#include "log0recv.h"
#include "ibuf0ibuf.h"


/**********************************************************************//**
Sync L2 Cache hash table from LRU remove page opreation */ 
UNIV_INTERN
void
fc_LRU_sync_hash_table(
/*==========================*/
	buf_page_t* bpage) /*!< in: frame to be written to L2 Cache */
{
	/* block to be written */
	fc_block_t* wf_block;

	ut_ad(mutex_own(&fc->mutex));

	/* the fc->write_off has not update by fc_block_find_replaceable, is just the block we find */
	wf_block = fc_get_block(fc->write_off);

	ut_a(wf_block->state == BLOCK_NOT_USED);

	/* the block have attached in fc_block_find_replaceable,
	  just need update space, offset and statement */
	wf_block->space = bpage->space;
	wf_block->offset = bpage->offset;
	
	wf_block->state = BLOCK_READ_CACHE;

	/* insert to hash table */
	fc_block_insert_into_hash(wf_block);

	srv_flash_cache_used += fc_block_get_data_size(wf_block);
	srv_flash_cache_used_nocompress += fc_block_get_orig_size(wf_block);

#ifdef UNIV_FLASH_DEBUG
	ut_print_timestamp(stderr);
	fprintf(stderr,"	InnoDB: lru + %lu, %lu.\n", wf_block->space, wf_block->offset);
#endif

}

/**********************************************************************//**
Move to L2 Cache if possible */
UNIV_INTERN
void
fc_LRU_move(
/*=========================*/
	buf_page_t* bpage)	/*!< in: page LRU out from buffer pool */
{
	fc_block_t *old_block;

    page_t*	page;
	dberr_t ret;
	ulint zip_size;
	ulint blk_size;
	ulint fc_blk_size;
	ulint block_offset;
	ulint byte_offset;
	ulint need_compress;
	fc_block_t* wf_block = NULL;
	byte* zip_buf_unalign = NULL;
	byte* zip_buf = NULL;
	ulint move_flag = 0;
	ulint cp_size = 0;

	ut_ad(!mutex_own(&fc->mutex));

	if (recv_no_ibuf_operations) {
		return;
	}
	
	zip_size = fil_space_get_zip_size(bpage->space);
	if (zip_size == ULINT_UNDEFINED) {
		/* table has been droped, do not need move to L2 Cache */

#ifdef UNIV_FLASH_CACHE_TRACE
		ut_print_timestamp(fc->f_debug);
		fprintf(fc->f_debug, "space:%lu is droped, the page(%lu, %lu) will not move to L2 Cache.\n",
			(ulong)bpage->space, (ulong)bpage->space, (ulong)bpage->offset);
#endif
		return;
	}

	if (zip_size) {
		ut_a(bpage->zip.data);
		page = bpage->zip.data;
	} else {
		page = ((buf_block_t*)bpage)->frame;
	}

#ifdef UNIV_FLASH_CACHE_TRACE
	if (buf_page_is_corrupted(true, page, zip_size)) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: page is corrupted in LRU_move. page type %lu, size %lu\n",
			(ulong)fil_page_get_type(page), (ulong)zip_size);
		/* the page into lru move may be dirty(in case when dropping a table and so on) , we dump mysqld just for debug */
		ut_error;
	}
#endif

	fc_blk_size = fc_get_block_size();
	blk_size = fc_calc_block_size(zip_size) / fc_blk_size;
	
	if ((fil_page_get_type(page) != FIL_PAGE_INDEX)
		&& (fil_page_get_type(page) != FIL_PAGE_INODE)) {
		return;
	}
	
	rw_lock_x_lock(&fc->hash_rwlock);	

	/* find if this bpage should move or migrate to L2 Cache */
	old_block = fc_block_search_in_hash(bpage->space, bpage->offset);
	if (fc_LRU_need_migrate(old_block, bpage)) {
		/* go on */
		move_flag = 2;
	} else if (old_block && srv_flash_cache_enable_move) {
		flash_block_mutex_enter(old_block->fil_offset);	
		if (!fc_LRU_need_move(old_block)) {
			flash_block_mutex_exit(old_block->fil_offset);
			rw_lock_x_unlock(&fc->hash_rwlock);
			return;
		}
		
		flash_block_mutex_exit(old_block->fil_offset);
		/* go on */
		move_flag = 1;
	} else {
		rw_lock_x_unlock(&fc->hash_rwlock);
		return;
	}
	
	rw_lock_x_unlock(&fc->hash_rwlock);
	
	/* the bpage should move or migrate to L2 Cache */

	/* if need compress, compress the data now */
	need_compress = fc_block_need_compress(bpage->space);
	if (need_compress == TRUE) {
		zip_buf_unalign = (byte*)ut_malloc(3 * UNIV_PAGE_SIZE);
		zip_buf = (byte*)ut_align(zip_buf_unalign, UNIV_PAGE_SIZE);
		memset(zip_buf, '0', 2 * UNIV_PAGE_SIZE);
		cp_size = fc_block_do_compress(FALSE, bpage, zip_buf);
		if (fc_block_compress_successed(cp_size) == FALSE) {
			need_compress = FALSE;
		} else {
			blk_size = fc_block_compress_align(cp_size);
		}
	}

#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
retry:
	flash_cache_mutex_enter();
	if (fc->is_doing_doublewrite > 0) {
		/*
		* we wait here to avoid the doublewrite commit the writeoff/writeround
		* (which update by move/migrate but data have not been synced)
		*/
		if (move_flag == 1) {
			fc_wait_for_aio_dw_launch();
			goto retry;
		} else {
			if (zip_buf_unalign) {
				ut_free(zip_buf_unalign);
			}
			
			flash_cache_mutex_exit();
			return;
		}
	}
#else
	flash_cache_mutex_enter();
#endif

	/* to reduce the risk that doublewrite should wait for space */
	if (fc_get_available() <= (FC_LEAST_AVIABLE_BLOCK_FOR_RECV / 2
							+ PAGE_SIZE_KB / fc_get_block_size())) {
		/*no enough space for fill the block*/
		if (zip_buf_unalign) {
			ut_free(zip_buf_unalign);
		}

		flash_cache_mutex_exit();
		return;
	}

	if (fc->is_finding_block == 1) {
		if (zip_buf_unalign) {
			ut_free(zip_buf_unalign);
		}
		flash_cache_mutex_exit();
		return;
	}
	
	rw_lock_x_lock(&fc->hash_rwlock);
	/* search the same space and offset in hash table again to make sure */
	old_block = fc_block_search_in_hash(bpage->space, bpage->offset);

	if (fc_LRU_need_migrate(old_block, bpage)) {
		/* 
		 * migrate: the page have not in flash cache
		 * move page not changed in buffer pool to L2 Cache block
		 */

		wf_block = fc_block_find_replaceable(TRUE, blk_size);
		ut_a(wf_block != NULL);

		flash_block_mutex_enter(wf_block->fil_offset);
		ut_a(fc_block_get_data_size(wf_block) == blk_size);

		if (need_compress == TRUE) {
			wf_block->size = PAGE_SIZE_KB / fc_blk_size;
			wf_block->raw_zip_size = cp_size;
		}

		fc_LRU_sync_hash_table(bpage);
		srv_flash_cache_write++;
		srv_flash_cache_migrate++;
		
		/* block state is safe as block mutex hold */
		rw_lock_x_unlock(&fc->hash_rwlock); 
		fc_inc_write_off(blk_size);
		srv_fc_flush_should_commit_log_write += blk_size;
		flash_cache_mutex_exit();	

		/* do compress package if necessary */
		if (need_compress == TRUE) {
			fc_block_pack_compress(wf_block, zip_buf);
		}

		fc_io_offset(wf_block->fil_offset, &block_offset, &byte_offset);

		if (need_compress == TRUE) {
#ifdef UNIV_FLASH_CACHE_TRACE
				byte* tmp_unalign = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
				byte* tmp = (byte*)ut_align(tmp_unalign, UNIV_PAGE_SIZE);
				fc_block_compress_check(zip_buf, wf_block);
				fc_block_do_decompress(DECOMPRESS_READ_SSD, zip_buf, wf_block->raw_zip_size, tmp);
				if (buf_page_is_corrupted(true, tmp, zip_size)) {
					fc_block_print(wf_block);
					ut_error;
				}
				ut_free(tmp_unalign);
				
#endif		
			ret = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0, block_offset,
					byte_offset, blk_size * KILO_BYTE * fc_blk_size, zip_buf, NULL);
		} else {
			ret = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0, block_offset,
					byte_offset, blk_size * KILO_BYTE * fc_blk_size, page, NULL);
		}

		if (ret != DB_SUCCESS) {
			ut_print_timestamp(stderr);
			fprintf(stderr, "InnoDB: Error to migrate from buffer pool to L2 Cache," 
				"space:%u, offset %u", bpage->space, bpage->offset);
			ut_error;
		}

		goto commit_log;
		
	} else if (old_block && srv_flash_cache_enable_move) {
		/* 
		 * move:
		 * move page already in L2 Cache block to new location
		 * for the sake of geting more high read ratio
		 */
		flash_block_mutex_enter(old_block->fil_offset);
		
		if (fc_LRU_need_move(old_block)) {
			ut_ad(old_block->state == BLOCK_FLUSHED
					|| old_block->state == BLOCK_READ_CACHE);
			fc_block_delete_from_hash(old_block);

#ifdef UNIV_FLASH_CACHE_TRACE
			fc_print_used();
#endif
			srv_flash_cache_used -= fc_block_get_data_size(old_block);
			srv_flash_cache_used_nocompress -= fc_block_get_orig_size(old_block);

			flash_block_mutex_exit(old_block->fil_offset);

			fc_block_free(old_block);

			wf_block = fc_block_find_replaceable(TRUE, blk_size);
			ut_a(wf_block != NULL);

			flash_block_mutex_enter(wf_block->fil_offset);
			ut_a(fc_block_get_data_size(wf_block) == blk_size);

			if (need_compress == TRUE) {
				wf_block->size = PAGE_SIZE_KB / fc_blk_size;
				wf_block->raw_zip_size = cp_size;
			}

			fc_LRU_sync_hash_table(bpage);
			srv_flash_cache_write++;
			srv_flash_cache_move++;

			rw_lock_x_unlock(&fc->hash_rwlock);
			fc_inc_write_off(blk_size);	
			srv_fc_flush_should_commit_log_write += blk_size;

			flash_cache_mutex_exit();

			/* do compress package if necessary */
			if (need_compress == TRUE) {
				fc_block_pack_compress(wf_block, zip_buf);
			}

			fc_io_offset(wf_block->fil_offset, &block_offset, &byte_offset);
			
			if (need_compress == TRUE) {
#ifdef UNIV_FLASH_CACHE_TRACE
				byte* tmp_unalign = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
				byte* tmp = (byte*)ut_align(tmp_unalign, UNIV_PAGE_SIZE);
				fc_block_compress_check(zip_buf, wf_block);
				fc_block_do_decompress(DECOMPRESS_READ_SSD, zip_buf, wf_block->raw_zip_size, tmp);
				if (buf_page_is_corrupted(true, tmp, zip_size)) {
					fc_block_print(wf_block);
					ut_error;
				}
				ut_free(tmp_unalign);
#endif
				ret = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0, block_offset,
						byte_offset, blk_size * KILO_BYTE * fc_blk_size, zip_buf, NULL);
			} else {
				ret = fil_io(OS_FILE_WRITE, TRUE, FLASH_CACHE_SPACE, 0, block_offset,
						byte_offset, blk_size * KILO_BYTE * fc_blk_size, page, NULL);
			}
			
			if ( ret != DB_SUCCESS ){
				ut_print_timestamp(stderr);
				fprintf(stderr,"InnoDB: Error to migrate from buffer pool to L2 Cache,"
					"space:%lu, offset %lu",
					(unsigned long)bpage->space, (unsigned long)bpage->offset);
				ut_error;
			}
				
			goto commit_log;
		} else {
			flash_block_mutex_exit(old_block->fil_offset);
			rw_lock_x_unlock(&fc->hash_rwlock);
			flash_cache_mutex_exit();
			if (zip_buf_unalign) {
				ut_free(zip_buf_unalign);
			}
			return;
		}
	} else {
		rw_lock_x_unlock(&fc->hash_rwlock);
		flash_cache_mutex_exit();
		if (zip_buf_unalign) {
			ut_free(zip_buf_unalign);
		}
		return;
	}

commit_log:
	fc_sync_fcfile();

	if (zip_buf_unalign) {
		ut_free(zip_buf_unalign);
	}

	/* async io compeleted, so the data has write into ssd, now, release the mutex */
	flash_block_mutex_exit(wf_block->fil_offset);

	/* 
	  * if we commit the log here, may be at this time the doublewrite has update the writeoff, and after commit the 
	  * server corrupted, and doublewrite has not finished, when recv, we may recovery the unfinished doublewrite page
	  * we should make sure at this time, doublewrite has finished fsync or has not yet enter.
	  */
#ifdef UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE
	if ((srv_fc_flush_should_commit_log_write >= FC_BLOCK_MM_NO_COMMIT)
			&& (fc->is_doing_doublewrite == 0)) {
		flash_cache_mutex_enter();
		/* make sure is_doing_doublewrite is 0 */
		if ((fc->is_doing_doublewrite != 0)) {
			flash_cache_mutex_exit();
			return;
		}
		/* this function will release the fc mutex */
		fc_log_commit_when_update_writeoff();
	}
#endif

}
