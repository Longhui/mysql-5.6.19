/**************************************************//**
@file fc/fc0recv.c
Flash Cache log recovery

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0recv.h"

#ifdef UNIV_NONINL
#include "fc0recv.ic"
#endif

#include "page0page.h"
#include "trx0sys.h"
#include "fc0fc.h"


ib_uint64_t* lsns_in_fc;
/* continue report corrupted count, should inited before use */
ulint conti_corr_count = 0;

/*********************************************************************//**
Read L2 Cache blocks(8MB at most) to hash table when recovery.*/
static
ulint
fc_recv_read_block_to_hash_table(
/*==========================================*/
	ulint f_offset,			/*<! in: flash cache offset */
	ulint n_read,			/*<! in: number of flash cache block to read */
	ulint end_offset,		/*<! end offset of flash cache block */
	byte* buf,				/*<! in: read buffer */
	ulint* n_pages_recovery,/*<! in/out: number of pages recovered */
	ulint state)			/*<! in: flash cache block state */
{
	ulint ret;
	ulint j;
	byte* page;
	ulint space;
	ulint offset;
	ulint block_offset;
	ulint byte_offset;
	ulint zip_size = 0; /* for InnoDB compress */
	ulint raw_compress_size = 0; /* for L2 cache compress */
	ulint data_size; /* the data store size */
	ulint blk_size = fc_get_block_size();
	fc_block_t* found_block;
	fc_block_t* wf_block;
	ibool need_remove;
	
	/* read n_read fc blocks */
	fc_io_offset(f_offset, &block_offset, &byte_offset);
	ret = fil_io(OS_FILE_READ, TRUE, FLASH_CACHE_SPACE, 0, block_offset, byte_offset,
				n_read * blk_size * KILO_BYTE, buf, NULL);
	if (ret != DB_SUCCESS) {
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB [Error]: Can not read L2 Cache, offset is %lu, read %lu pages.\n",
			(ulong)f_offset, (ulong)srv_flash_cache_pages_per_read);
		ut_error;
	}

	j = 0;
	while (j < n_read) {		
		/* we reserve 2 UNIV_PAGE_SIZE(32kb) in buf, in order to make the page check more easy */
		if (((f_offset + n_read) < end_offset) /* not the last batch read */
			&& ((n_read - j) < 2 * PAGE_SIZE_KB / blk_size)) {
			return j;
		}
		
		page = buf + j * fc_get_block_size_byte();

		/* handle the l2 cache compress page */
		if (mach_read_from_4(page + FC_ZIP_PAGE_HEADER) == FC_ZIP_PAGE_CHECKSUM) {
			ulint page_size = mach_read_from_4(page + FC_ZIP_PAGE_SIZE);
			raw_compress_size = mach_read_from_4(page + FC_ZIP_PAGE_ZIP_RAW_SIZE);			
			if (page_size <= (UNIV_PAGE_SIZE - fc_get_block_size_byte())) {	
				ulint checksum2 = mach_read_from_4(page + page_size - FC_ZIP_PAGE_TAILER);
				ulint page_size_orign = mach_read_from_4(page + FC_ZIP_PAGE_ORIG_SIZE);
				if ((checksum2 == FC_ZIP_PAGE_CHECKSUM) && (page_size_orign == UNIV_PAGE_SIZE)) {
					/* we find a L2 cache compress page */
          			ulint sdc;
					space = mach_read_from_4(page + FC_ZIP_PAGE_SPACE);
					offset = mach_read_from_4(page + FC_ZIP_PAGE_OFFSET);

					/* quicklz check */
					if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
						ulint sc = (ulint)fc_qlz_size_compressed((const char*)(page + FC_ZIP_PAGE_DATA));
						if (raw_compress_size != sc) {
							ut_print_timestamp(stderr);
							fprintf(stderr, "InnoDB:L2 cache: recv compress page size:%d"
								" orig size:%d is wrong.\n\n\n", (int)sc, (int)raw_compress_size);
							ut_error;
						}

						sdc = (ulint)fc_qlz_size_decompressed((const char*)(page + FC_ZIP_PAGE_DATA));
						if (UNIV_PAGE_SIZE != sdc) {
							ut_print_timestamp(stderr);
							fprintf(stderr, "InnoDB:L2 cache: recv decompress page size:%d"
								" should be 16384.\n\n\n", (int)sdc);
							ut_error;
						}
					}

					ut_a((page_size / fc_get_block_size_byte()) 
							== fc_block_compress_align(raw_compress_size));

					ut_a((j +  fc_block_compress_align(raw_compress_size)) <= n_read);

					zip_size = fil_space_get_zip_size(space);
					if (zip_size != ULINT_UNDEFINED) {
						ut_a(zip_size == 0);
						ut_a(UNIV_PAGE_SIZE == 
							fc_block_do_decompress(DECOMPRESS_RECOVERY, page, raw_compress_size, fc->recv_dezip_buf));
						
						if (buf_page_is_corrupted(fc->recv_dezip_buf, zip_size)) {
							ut_print_timestamp(stderr);
							fprintf(stderr, "InnoDB:L2 cache: recv find a l2 compress"
								" corrupted page.space%d, offset%d\n", (int)space, (int)offset);
							ut_error;
						}
						lsns_in_fc[f_offset + j] = mach_read_from_8(fc->recv_dezip_buf + FIL_PAGE_LSN);
						goto do_next;						
					} else {
						ut_print_timestamp(stderr);
						fprintf(stderr, "InnoDB:L2 cache: recv find a l2 compress"
							" droped page.space%d, offset%d.skip\n", (int)space, (int)offset);
						j += fc_block_compress_align(raw_compress_size);
						conti_corr_count = 0;
						raw_compress_size = 0;
						zip_size = 0;
						continue;
					}
				}
			}
			/* FIXME: maybe we should do something here */			
		}

		/* this is not a l2 cache compress page or the page data is overlapped, read space, offset from page header */
		space = mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
		offset = mach_read_from_4(page + FIL_PAGE_OFFSET);

		/* try to get the zip_size with space id, warning:the id may be wrong */
		zip_size = fil_space_get_zip_size(space);

		if ((zip_size != ULINT_UNDEFINED) && (FALSE == buf_page_is_corrupted(page, zip_size))) {
			lsns_in_fc[f_offset + j] = mach_read_from_8(page + FIL_PAGE_LSN);
			raw_compress_size = 0;
			goto do_next;
		} else {
			/* recv find a page belong to droped table or it is invalid block.*/

			/*
			 * we should calc the page size from the checksum or lsn, if it is the
			 * end of the fc file, just use the size left. else we use 16kb for check,
			 * so if the buf size left is smaller than 16kb, it is not enough for us to
			 * check. then if buf size left is smaller than 32 kb, it is not enough for us
			 * to make sure if we have encounter a corrupted page
			 */
			int page_len = -1;
      		ulint block_len;

			if ((j + PAGE_SIZE_KB / blk_size) > n_read) {
				/*
				 * if it is in the end of the current read,
				 * just read from the offset to the end of buf
				 */
				page_len = (n_read - j) * blk_size;
			} else {
				page_len = PAGE_SIZE_KB;
			}
						
			block_len = fc_calc_drop_page_size(page, page_len);
			if (block_len != ULINT_UNDEFINED) {
				/* ok we find a right droped page, just skip it */
				ut_print_timestamp(stderr);
				fprintf(stderr, "InnoDB:L2 cache: recv find a droped page.space%d, offset%d."
					"skip fos%d len%d, blen%d\n", (int)space, (int)offset,
					(int)(j + f_offset), (int)page_len, (int)block_len);

				/*  keep the block state not used */
				ut_a(block_len >= blk_size);
				j += block_len / blk_size;
				conti_corr_count = 0;
				raw_compress_size = 0;
				zip_size = 0;
				continue;
			}

			/*
			 * we have not find a right page this loop, mark the state.
			 */

			/*
			 *  we have change the type to find the replace block again. now we will skip the reading pages,
			 * so it is possible that we found a large to 16kb data that is not a complete page because of
			 * page skip. 
			 */
			//FIXME: is it right?
			conti_corr_count++;

			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: L2 cache: recv find a invalid page(maybe), count%d, "
				"space:%lu, offset:%lu,j:%lu .\n",
				(int)conti_corr_count, (ulong)space, (ulong)offset, (ulong)j);
			
			//FIXME:should we delete the codes below? 
			/* if have not find a complete block in PAGE_SIZE_KB / blk_size loop, can not continue
			if (conti_corr_count == PAGE_SIZE_KB / blk_size) {
				ut_print_timestamp(stderr);
				fprintf(stderr, "InnoDB:L2 cache: recv find a corrupted page.\n");
				ut_error;
			}
			*/
			j++;
			raw_compress_size = 0;
			zip_size = 0;
			continue;
		}
		
do_next:
		/*
		 * we find a valid block, should add it to hash table, except there is a block
		 * in hash table with newer lsn
		 */

		/* if the block is compressed by L2 Cache, it must not compressed by InnoDB */
		if (raw_compress_size > 0) {
			ut_a(zip_size == 0);
		}

		found_block = fc_block_search_in_hash(space, offset);

		/* if recv base on dump the lsn of found_block may be zero, as no data read from ssd */
		need_remove = found_block && 
			(lsns_in_fc[found_block->fil_offset] < lsns_in_fc[f_offset + j]);
		
		if (need_remove) {
			data_size = fc_block_get_data_size(found_block);

#ifdef UNIV_FLASH_CACHE_TRACE
			fc_print_used();
#endif
			srv_flash_cache_used -= data_size;

			srv_flash_cache_used_nocompress -= fc_block_get_orig_size(found_block);
			
			if (found_block->state == BLOCK_READY_FOR_FLUSH) {
				srv_flash_cache_dirty -= data_size;
			}

			fc_block_delete_from_hash(found_block);		
			fc_block_free(found_block);
			/* for different versions of same page, just count it once */
			--*n_pages_recovery;	
		}
		
		if(!found_block || need_remove) {
			wf_block = fc_block_init(f_offset + j);
			ut_a(wf_block == fc_get_block(f_offset + j));

			
			/* init the block, and insert it to hash table */		
			wf_block->size = fc_calc_block_size(zip_size) / blk_size;
			wf_block->raw_zip_size = raw_compress_size;

			wf_block->state = state;		
			wf_block->space = space;
			wf_block->offset = offset;

			fc_block_insert_into_hash(wf_block);
			
			/* new block size */			
			data_size = fc_block_get_data_size(wf_block);	
			
			srv_flash_cache_used += data_size;
			srv_flash_cache_used_nocompress += fc_block_get_orig_size(wf_block);

			if (wf_block->state == BLOCK_READY_FOR_FLUSH) {
				srv_flash_cache_dirty += data_size;
			}

			++*n_pages_recovery;		
		} else {
			/*
			 * there is already have a block with same space and offset in hash table
			 * that block have newer lsn, so remain current block fil_offset not used
			 */
			if (raw_compress_size == 0) { 
				data_size = fc_calc_block_size(zip_size) / blk_size;
			} else {
				data_size = fc_block_compress_align(raw_compress_size);
			}
		}
		
		j += data_size;
		raw_compress_size = 0;
		zip_size = 0;
		conti_corr_count = 0;
	}
	
/* it is possible to happen, when finished recv, but conti_corr_count is not zero;
	if (conti_corr_count != 0) {
		ut_print_timestamp(stderr);
		fprintf(stderr, "InnoDB:L2 cache: find a corrupted page at end of recv.\n");
		ut_error;
	}
*/
	flash_cache_mutex_enter();
	fc_validate();
	flash_cache_mutex_exit();

	return n_read;
}


/****************************************************************//**
Recovery L2 Cache blocks between start and end offset
@return:NULL*/
static
void
fc_recv_blocks(
/*======================*/
	ulint start_offset,	/*<! start offset of flash cache block */
	ulint end_offset,	/*<! end offset of flash cache block */
	ulint state)		/*<! flash cache block state */
{
	ulint i;
	byte* buf_unaligned;
	byte* buf;

	ulint n_read;
	ulint blk_pre_read;
	ulint actual_operator_number;
	ulint n_pages_recovery = 0;

	blk_pre_read = srv_flash_cache_pages_per_read * UNIV_PAGE_SIZE / fc_get_block_size_byte();

	i = start_offset;

	buf_unaligned = (byte*)ut_malloc(UNIV_PAGE_SIZE * (srv_flash_cache_pages_per_read + 1));
	buf = (byte*)ut_align(buf_unaligned, UNIV_PAGE_SIZE);

	while (i + blk_pre_read < end_offset) {
		/*
		 * may be there are some block across two reads, in this case ,
		 * read the whole block next time
		 */
		actual_operator_number = fc_recv_read_block_to_hash_table(i, blk_pre_read, end_offset, 
									buf, &n_pages_recovery, state);
		i = i + actual_operator_number;
	}

	if ((end_offset - i) != 0) {
		n_read = end_offset - i;
		actual_operator_number = fc_recv_read_block_to_hash_table(i, n_read, end_offset, 
									buf, &n_pages_recovery, state);

		if (actual_operator_number != n_read) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: Should read pages %lu, but only read %lu\n", 
				(ulong)n_read, (ulong)actual_operator_number);
			ut_free((void *)buf_unaligned);
			ut_error;
		}
	}

	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: Should recover pages %lu, actually recovered %lu\n", 
		(ulong)(end_offset - start_offset), (ulong)n_pages_recovery);

	ut_free(buf_unaligned);
}

/********************************************************************//**
when perform recovery, if any page in doublewrite buffer is newer than that in disk,
then write it to disk. After calling this function, there may be pages in L2 Cache older than that
in disk, if this is TRUE, this page shoule be removed from L2 Cache's hash table */
static
void
fc_recv_dwb_pages_to_disk(void)
/*======================*/
{
	ulint i;
	ulint space_id;
	ulint page_no;
	unsigned zip_size;
	ib_uint64_t lsn_in_dwb;
	ib_uint64_t lsn_in_disk;
	byte  unaligned_read_buf[2 * UNIV_PAGE_SIZE];
	byte* read_buf = ut_align(unaligned_read_buf, UNIV_PAGE_SIZE);
	byte* page;

	
	fil_io(OS_FILE_READ, TRUE, TRX_SYS_SPACE, 0, trx_doublewrite->block1, 0,
	       	TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE,
	      	 trx_doublewrite->write_buf, NULL);
	fil_io(OS_FILE_READ, TRUE, TRX_SYS_SPACE, 0, trx_doublewrite->block2, 0,
	      	 TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE,
	      	 trx_doublewrite->write_buf + 
	      	 	TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE, NULL);
	
	for (i = 0; i < TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * 2; ++i) {
		page = trx_doublewrite->write_buf + i * UNIV_PAGE_SIZE;
		space_id = mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
		page_no = mach_read_from_4(page + FIL_PAGE_OFFSET);
		if (!fil_tablespace_exists_in_mem(space_id)) {
			/* do something */
		} else if (!fil_check_adress_in_tablespace(space_id,page_no)) {
			fprintf(stderr,
				"InnoDB: Warning: a page in the doublewrite buffer is not within space\n"
				"InnoDB: bounds; space id %lu page number %lu, page %lu in doublewrite buf.\n",
				(ulong) space_id, (ulong) page_no, (ulong) i);
		} else {
			lsn_in_dwb = mach_read_from_8(page + FIL_PAGE_LSN);
			zip_size = fil_space_get_zip_size(space_id);
			ut_ad(ULINT_UNDEFINED != zip_size);

			if (buf_page_is_corrupted(page, zip_size)) {
				ut_print_timestamp(stderr);
				fprintf(stderr,
					" InnoDB: The page in the doublewrite buffer is corrupt.\n"
					"InnoDB: Cannot continue operation.\n"
					"InnoDB: You can try to recover the database with the my.cnf\n"
					"InnoDB: option: innodb_force_recovery=6\n");
				exit(1);
			}
			
			fil_io(OS_FILE_READ, TRUE, space_id, zip_size, page_no, 0,
                   zip_size ? zip_size : UNIV_PAGE_SIZE, read_buf, NULL);
			lsn_in_disk  = mach_read_from_8(read_buf + FIL_PAGE_LSN);
			
			if (buf_page_is_corrupted(read_buf, zip_size)
					|| lsn_in_dwb > lsn_in_disk) {
                /* write back from doublewrite buffer to disk */
				fil_io(OS_FILE_WRITE, TRUE, space_id, zip_size, page_no, 0,
                       zip_size ? zip_size : UNIV_PAGE_SIZE, page, NULL);
			}		 
		}
	}
}

/****************************************************************//**
Start flash cache log recovery, it means L2 Cache is not shutdown correctly.*/
UNIV_INTERN
void
fc_recv(void)
/*=======================*/
{

	ulint block_num;
	
	ulint flush_offset;
	ulint write_offset;
	ulint flush_round;
	ulint write_round;
	
	unsigned 	i;
	byte		unaligned_disk_buf[2 * UNIV_PAGE_SIZE];
	byte*		disk_buf;
	ib_uint64_t lsn_in_disk;
	unsigned 	zip_size;
	ulint 		space_id;
	ulint 		page_no;
	fc_block_t* fc_block;
	ulint		n_removed_pages_for_wrong_version;
	fc_block_t** sorted_fc_blocks;
  	ulint invalid_blocks;
	
	/* after scanning flash cache file in, the number of page in flash cache hash table */
	ulint		n_newest_version_in_fcl;
	
	if (fc_log->blk_find_skip == 0) {
		fc_log->blk_find_skip = FC_FIND_BLOCK_SKIP_COUNT + FC_BLOCK_MM_NO_COMMIT;
	}
	
	invalid_blocks = FC_LEAST_AVIABLE_BLOCK_FOR_RECV / 2 + fc_log->blk_find_skip;
	
	ut_a(fc_log->first_use == FALSE);
	ut_a(fc_log->been_shutdown == FALSE);

	if (fc_log->log_verison != FLASH_CACHE_VERSION_INFO_V5) {
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: L2 Cache: current version is %lu, \n"
			"  but the Cache need recovery is %lu. please use the same version to do recovery. \n", 
			FLASH_CACHE_VERSION_INFO_V5, fc_log->log_verison);
		ut_error;
	}

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr, " InnoDB: BEGIN L2 Cache recovery!!!\n");
#endif

	ut_ad(trx_doublewrite);
    
	if (fc_log->enable_write_curr == FALSE) {
		 /* we should first check doublewrite disk buffer, and write data to tablespace*/
		fc_recv_dwb_pages_to_disk();
		fil_flush_file_spaces(FIL_TABLESPACE);
	}
	
	/* we malloc flush_compress_read_buf only when the enable_compress is work */
	if (srv_flash_cache_enable_compress == TRUE) {
		fc->recv_dezip_buf_unalign = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
		fc->recv_dezip_buf =
				(byte*)ut_align(fc->recv_dezip_buf_unalign, UNIV_PAGE_SIZE);
	}


	flush_offset = fc->flush_off;
	write_offset = fc->write_off;
	flush_round = fc->flush_round;
	write_round = fc->write_round;
	
	block_num = fc_get_size();

#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr,
		" InnoDB: L2 Cache log info:\n "
		"   current: write round: %lu flush round: %lu, write offset: %lu, flush offset:%lu;\n "
		"   write round bck: %lu, write offset bck: %lu\n",
		(ulong)write_round, (ulong)flush_round, (ulong)write_offset, (ulong)flush_offset,
		(ulong)fc_log->write_round_bck, (ulong)fc_log->write_offset_bck);
#endif

	ut_a(block_num);
	flash_cache_mutex_enter();
	if (invalid_blocks > fc_get_available()) {
		invalid_blocks = fc_get_available();
	}
	flash_cache_mutex_exit();
	
	lsns_in_fc = ut_malloc(sizeof(ib_uint64_t) * block_num);
	ut_a(lsns_in_fc);

	for (i = 0; i < block_num; i++) {
		lsns_in_fc[i] = 0;
	}

	i = 0;
	
	/* recovery base on data pages cached in cache file */	
	if(srv_flash_cache_write_mode == WRITE_THROUGH) {
		fc_recv_blocks(0, block_num, BLOCK_READ_CACHE);
    } else {
		/*
		 * we do not recv the 128 (tablespace) pages before current_stat->write_offset
	  	 * as this blocks data in ssd is not safe, may overlaped by failed doublewrite
	  	 */
		if (flush_round == write_round) {
			if (write_offset < flush_offset) {
				/* 
				  * it only happen when lru move write some clean page to cache, but not commit the log
				  * after that no doublewrite into cache, so write_off will not be committed, but flush_off
				  * will committed by each flush, and zero the dirty page. so at this time flush_off in log is
				  * equal to write_off in memory, but newer than write_off in log. so we should handle it
				  */
				ut_a(write_offset + FC_BLOCK_MM_NO_COMMIT >= flush_offset);
				fc->write_off = write_offset = flush_offset;
				goto exit;
			}

			//ulint start_pos = 0;
			//if ((write_round > 0) && ((write_offset + invalid_blocks) >= block_num)) {
			//	start_pos = 3 + write_offset + invalid_blocks - block_num;
			//}
				
			//fc_recv_blocks(start_pos, flush_offset, BLOCK_READ_CACHE);
			fc_recv_blocks(flush_offset, write_offset, BLOCK_READY_FOR_FLUSH);
			//if ((write_round > 0) && (start_pos == 0)) {
			//	fc_recv_blocks(write_offset + invalid_blocks, block_num, 
			//		BLOCK_READ_CACHE);//add not sub with invalid_blocks?
			//}
		} else {
			if((flush_round + 1) != write_round) {
				ut_a(write_round + 1 == flush_round);
				ut_a(block_num - write_offset + flush_offset <= FC_BLOCK_MM_NO_COMMIT);
				fc->write_off = write_offset = flush_offset;
				fc->write_round = write_round = flush_round;
				goto exit;
			}
			//fc_recv_blocks(write_offset + invalid_blocks, flush_offset, 
			//	BLOCK_READ_CACHE);
			fc_recv_blocks(flush_offset, block_num, BLOCK_READY_FOR_FLUSH);
			fc_recv_blocks(0, write_offset, BLOCK_READY_FOR_FLUSH);
		}
	}
	
	if ((fc_log->enable_write_curr == TRUE) && (srv_flash_cache_safest_recovery == FALSE)) {
#ifdef UNIV_FLASH_CACHE_TRACE
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: L2 Cache fc_log->enable_write_curr == TRUE. \n"); 	
#endif
		if (fc_log->write_offset_bck == 0XFFFFFFFFUL) {
			/* keep enable_write from innodb start, no need to compare data */
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: flash cache no need to remove pages for wrong:1.\n");
#endif
			goto exit;
		}

		if (write_round > (fc_log->write_round_bck + 1)) {
			/*
			 * it is more than a round when enable_write from FALSE to TRUE,
			 * so the data in ssd is uptodate now, just return
			 */
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache no need to remove pages for wrong:2.\n");
#endif
			goto exit;
		}

		if ((write_round == (fc_log->write_round_bck + 1)) 
				&& (write_offset >= fc_log->write_offset_bck)) {
			/*
			 * it is more than a round when enable_write from FALSE to TRUE,
			 * so the data in ssd is uptodate now, just return
			 */
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache no need to remove pages for wrong:3.\n");
#endif
			goto exit;
		}
	}

	/* compare the ssd page data to disk data, and remove the outmoded data in ssd */
#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: L2 Cache start compare the ssd data with disk data.\n");
#endif
	
	sorted_fc_blocks = ut_malloc(block_num * sizeof(fc_block_t*));
	ut_ad(sorted_fc_blocks);

	n_newest_version_in_fcl = 0;

	/* compare all used blocks in ssd with disk data */
	i = 0;
	while (i < block_num) {
		fc_block = fc_get_block(i);
		if (fc_block == NULL) {
			i++;
			continue;
		}
		
		sorted_fc_blocks[n_newest_version_in_fcl++] = fc_block;
		i += fc_block_get_data_size(fc_block);
	}
	
    if (n_newest_version_in_fcl > 0) {
#ifdef UNIV_FLASH_CACHE_TRACE
        fprintf(stderr," InnoDB: L2 Cache should compare :%d blocks.\n", (int)n_newest_version_in_fcl);
#endif
        /* sort L2 Cache block by (space,page_no), so read page frome disk can be more sequential */
        fc_block_sort(sorted_fc_blocks, n_newest_version_in_fcl, ASCENDING);
    }
		
#ifdef UNIV_FLASH_CACHE_TRACE_RECV
	ut_print_timestamp(stderr);
	fprintf(stderr,
		" InnoDB: L2 Cache pages need removed from L2 Cache for its wrong version are listed below\n"); 				
#endif
		
	n_removed_pages_for_wrong_version = 0;
	disk_buf = ut_align(unaligned_disk_buf,UNIV_PAGE_SIZE);
	for (i = 0; i < n_newest_version_in_fcl; ++i) {
		fc_block = sorted_fc_blocks[i];
		space_id = fc_block->space;
		page_no = fc_block->offset;
		zip_size = fil_space_get_zip_size(space_id);
		ut_ad(ULINT_UNDEFINED != zip_size);
		
		fil_io(OS_FILE_READ, TRUE, space_id, zip_size, page_no, 0,
			  zip_size ? zip_size : UNIV_PAGE_SIZE, disk_buf, NULL);
		lsn_in_disk = mach_read_from_8(disk_buf + FIL_PAGE_LSN);
		if (lsn_in_disk > lsns_in_fc[fc_block->fil_offset]) {
			ulint data_size = fc_block_get_data_size(fc_block);

#ifdef UNIV_FLASH_CACHE_TRACE
			fc_print_used();
			fc_block_print(fc_block);
#endif
			srv_flash_cache_used -= data_size;

			srv_flash_cache_used_nocompress -= fc_block_get_orig_size(fc_block);
			if (fc_block->state == BLOCK_READY_FOR_FLUSH) {
				srv_flash_cache_dirty -= data_size;	
			} 
			
			fc_block_delete_from_hash(fc_block);
			++n_removed_pages_for_wrong_version;
			
#ifdef UNIV_FLASH_CACHE_TRACE
			fprintf(stderr, "InnoDB: space_id: %.10lu page_no: %.10lu lsn_in_fc: %.20llu"
				"  lsn_in_disk: %.20llu\n", (ulong)space_id, (ulong)page_no,
				(ulonglong)lsns_in_fc[fc_block->fil_offset], (ulonglong)lsn_in_disk);
#endif
		}
	}

	ut_free(sorted_fc_blocks);
#ifdef UNIV_FLASH_CACHE_TRACE
	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: L2 Cache %lu pages have been removed from L2 Cache "
			"for its wrong version\n", n_removed_pages_for_wrong_version);
#endif

exit:
	ut_free(lsns_in_fc);

	if (srv_flash_cache_enable_compress == TRUE) {
		/* we free this buf when finish recovery */
		ut_free(fc->recv_dezip_buf_unalign);
	}

#ifdef UNIV_FLASH_CACHE_TRACE	
	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: RECOVERY from L2 Cache has finished!!!!\n");
#endif

	flash_cache_mutex_enter();
	fc_validate();
	flash_cache_mutex_exit();

	return;
}
