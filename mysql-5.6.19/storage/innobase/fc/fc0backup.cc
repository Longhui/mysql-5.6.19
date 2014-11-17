/**************************************************//**
@file fc/fc0backup.c
Backup Flash Cache dirty blocks to other file

Created	24/10/2013 Thomas Wen(wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0backup.h"

#ifdef UNIV_NONINL
#include "fc0backup.ic"
#endif

#include "srv0start.h"

/********************************************************************//**
Print error info if creating backup file failed */
UNIV_INTERN
void
fc_bkp_print_create_error(
/*==================*/
	char* bkp_file_path)	/*!< in: backup file name */
{
	ut_print_timestamp(stderr);
	fprintf(stderr, " Innodb: Error: create file '%s' failed, "
		"check if it has already existed,\n", bkp_file_path);
	fprintf(stderr, " Innodb: Error: which means the L2 Cache backup "
		"may have started already.\n");
}

/********************************************************************//**
Make file path for backup file */
UNIV_INTERN
void
fc_bkp_finish(
/*==================*/
	byte* unaligned_buf,	/*!< in: data buf used to buffer the backup page data */
	byte* compress_buf_unalign,	/*!< in: data buf used to buffer compressed page data */
	char* bkp_file_path,	/*!< in: backup file name */
	char* bkp_file_path_final)	/*!< in: backup file name final */
{
	ut_free(unaligned_buf);
	ut_free(bkp_file_path);
	ut_free(bkp_file_path_final);

	if (srv_flash_cache_enable_compress == TRUE) {
		ut_free(compress_buf_unalign);
	}
}

/********************************************************************//**
Print error info if write dirty pages to backup file failed */
UNIV_INTERN
void
fc_bkp_print_io_error(void)
/*==================*/
{
	if(os_file_get_last_error(FALSE) == OS_FILE_DISK_FULL) {
		ut_print_timestamp(stderr);
		fprintf(stderr, "InnoDB: Error: disk is full, reset srv_flash_cache_backup_dir\n");
		fprintf(stderr, "InnoDB: Error: to another disk or partion where space is large enough\n");
		fprintf(stderr, "InnoDB: Error: to store the flash backup pages, and then reset\n"); 
		fprintf(stderr, "InnoDB: Error:  innodb_flash_cache_backup to TRUE to backup again.\n");
	} else {
		ut_print_timestamp(stderr);
		fprintf(stderr, "InnoDB: Error: error occured while backuping unflushed flash \n");
		fprintf(stderr, "InnoDB: Error: cache pages try reset innodb_flash_cache_backup\n");
		fprintf(stderr, "InnoDB: Error:  to TRUE if wanna do backup again.\n");
	}

}

/********************************************************************//**
Backup pages haven't flushed to disk from flash cache file.
@return number of pages backuped if success */
UNIV_INTERN
ulint
fc_backup(
/*==================*/
	ibool* success)		/*!< out: if backup is sucessed */
{
	char* bkp_dir = NULL;
	char* bkp_file_path = NULL;
	char* bkp_file_path_final = NULL;
	
	ulint file_offset, buf_offset;
	os_file_t bkp_fd;
	ibool flag = FALSE;
	byte* unaligned_buf = NULL;
	byte* buf = NULL;	/* buf the page data(decompressed), will soon write to backup file */
	byte* compress_buf_unalign = NULL;
	byte* compress_buf = NULL; /* buf the compressed page data read from ssd, if enable compress */
	fc_block_t** sorted_blocks = NULL;
	fc_block_t* blk = NULL;

	fc_bkp_info_t* bkp_info = NULL;
	fc_bkp_blkmeta_t* blk_metas = NULL;
	
	ulint i, pos;	
	ulint blk_metas_size;
	ulint flush_distance;
	ulint n_dirty_pages;
	ulint n_backup_pages;
	ulint flush_off;
	//ulint offset_high, offset_low;
	ulint block_offset, byte_offset;
	ulint fc_size = fc_get_size();
	ulint fc_blk_size = fc_get_block_size();

	/* get the mutex when get distance and offset, actually, now enable write is disabled, it is safe to mutex free */
	flash_cache_mutex_enter();
	flush_off = fc->flush_off;
	flush_distance = fc_get_distance();
	flash_cache_mutex_exit();
	
	if (flush_distance) {
		sorted_blocks = (fc_block_t**)ut_malloc(flush_distance * sizeof(*sorted_blocks));
		n_dirty_pages = 0;

		/*collect dirty pages for backup, should get mutex */
		flash_cache_mutex_enter();
		rw_lock_s_lock(&fc->hash_rwlock);
		
		i = 0;
		while (i < flush_distance) {
			pos = (flush_off + i) % fc_size;
			blk = fc_get_block(pos);

			if (blk == NULL) {
				i++;
				continue;
			}

			flash_block_mutex_enter(blk->fil_offset);
			if (blk->state != BLOCK_READY_FOR_FLUSH) {
				i += fc_block_get_data_size(blk);
				flash_block_mutex_exit(blk->fil_offset);
				continue;
			}
			sorted_blocks[n_dirty_pages++] = blk;
			i += fc_block_get_data_size(blk);
			flash_block_mutex_exit(blk->fil_offset);
		}

		rw_lock_s_unlock(&fc->hash_rwlock);
		flash_cache_mutex_exit();

		ut_a(n_dirty_pages <= flush_distance);
		
		if (n_dirty_pages) {
     		ulint len_tmp;
      		ulint len_final;
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: flash cache is backuping, dirty_page:%d ...\n", (int)n_dirty_pages);
			
			fc_block_sort(sorted_blocks, n_dirty_pages, ASCENDING);

			bkp_dir = srv_flash_cache_backup_dir ? 
				srv_flash_cache_backup_dir : srv_data_home;

			/* alloc memory for backup */
			bkp_info = (fc_bkp_info_t*)ut_malloc(sizeof(fc_bkp_info_t));
			blk_metas = (fc_bkp_blkmeta_t*)ut_malloc(n_dirty_pages * sizeof(fc_bkp_blkmeta_t));

#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache backup:alloced blk metadata size %d\n", 
				(int)n_dirty_pages * sizeof(fc_bkp_blkmeta_t));
#endif				
			
			len_tmp = strlen(bkp_dir) + sizeof("/ib_fc_backup_creating");
			len_final = strlen(bkp_dir) + sizeof("/ib_fc_backup");
			bkp_file_path = (char *)ut_malloc(len_tmp);
			bkp_file_path_final = (char *)ut_malloc(len_final);
			ut_snprintf(bkp_file_path, len_tmp, 
				"%s/%s", bkp_dir, "ib_fc_backup_creating");
			ut_snprintf(bkp_file_path_final, len_final, 
				"%s/%s", bkp_dir, "ib_fc_backup");
			
			srv_normalize_path_for_win(bkp_file_path);
			srv_normalize_path_for_win(bkp_file_path_final);

			/* alloc backup buffer to store the backup page data  */
			unaligned_buf = (unsigned char *)ut_malloc((FSP_EXTENT_SIZE + 1) * UNIV_PAGE_SIZE);
			buf = (unsigned char *)ut_align(unaligned_buf, UNIV_PAGE_SIZE);	

			if (srv_flash_cache_enable_compress == TRUE) {
				compress_buf_unalign = (unsigned char *)ut_malloc(2 * UNIV_PAGE_SIZE);
				compress_buf = (unsigned char *)ut_align(compress_buf_unalign, UNIV_PAGE_SIZE);
			}

			/* create the backup tmp file */
			bkp_fd = os_file_create(innodb_file_data_key, bkp_file_path, 
				OS_FILE_CREATE, OS_FILE_NORMAL, OS_DATA_FILE, &flag);
			if (!flag) {
				fc_bkp_print_create_error(bkp_file_path);
				fc_bkp_finish(unaligned_buf, compress_buf_unalign,
						bkp_file_path, bkp_file_path_final);
				*success = FALSE;
				return 0;
			}

			i = 0;
			n_backup_pages = 0;
			file_offset = FC_BACKUP_FILE_PAGE_OFFSET;
			while (i < n_dirty_pages) {
				/* first read pages from ssd to buf, buf size is 1MB */
				buf_offset = 0;
				while (i < n_dirty_pages) {
         	 		ulint data_size;
          			byte *read_buf;

					blk = sorted_blocks[i];
					flash_block_mutex_enter(blk->fil_offset);
					
					/* check if doublewrite ops have disable the block or flush thread have flush the block */
					if (blk->state != BLOCK_READY_FOR_FLUSH) {
						i++;
						flash_block_mutex_exit(blk->fil_offset);
#ifdef UNIV_FLASH_CACHE_TRACE
						ut_a(blk->state == BLOCK_NOT_USED);
						fc_block_free(blk);
#endif
						continue;
					}

					/* check if the buffer have enough space for the block data */
					if ((buf_offset + blk->size * fc_blk_size) > FC_ONE_EXTENT) {
						flash_block_mutex_exit(blk->fil_offset);
						break;
					}
					
					data_size = fc_block_get_data_size(blk);
					fc_io_offset(blk->fil_offset, &block_offset, &byte_offset);
					if (blk->raw_zip_size > 0) {
						ut_a(compress_buf);
						read_buf = compress_buf;
					} else {
						read_buf = buf + buf_offset * KILO_BYTE;
					}
					fil_io(OS_FILE_READ, TRUE, FLASH_CACHE_SPACE, 0, 
						block_offset, byte_offset, (data_size * fc_get_block_size_byte()),
						read_buf, NULL);
					
					if (blk->raw_zip_size > 0) {
#ifdef UNIV_FLASH_CACHE_TRACE
						fc_block_compress_check(read_buf, blk);
			
						/* only qlz can do this check  */
						if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
							if (blk->is_v4_blk) {
								ut_a(blk->raw_zip_size * fc_get_block_size_byte()
									>=(ulint)fc_qlz_size_compressed((const char *)(read_buf + FC_ZIP_PAGE_DATA)));
							} else {
								ut_a(blk->raw_zip_size 
									==(ulint)fc_qlz_size_compressed((const char *)(read_buf + FC_ZIP_PAGE_DATA)));
							}
							ut_a(UNIV_PAGE_SIZE 
								== fc_qlz_size_decompressed((const char *)(read_buf + FC_ZIP_PAGE_DATA)));
						}
#endif
						fc_block_do_decompress(DECOMPRESS_BACKUP, read_buf, blk->raw_zip_size, buf + buf_offset * KILO_BYTE);
					}

					blk_metas[n_backup_pages].blk_space = blk->space;
					blk_metas[n_backup_pages].blk_offset = blk->offset;
					blk_metas[n_backup_pages].blk_size = blk->size * fc_blk_size;
					
					buf_offset += blk->size * fc_blk_size;
					n_backup_pages++;					
					flash_block_mutex_exit(blk->fil_offset);
					
					i++;

					if (i >= n_dirty_pages) {
						break;	
					}
				}

				/* then write 1MB data to backup file */
				//offset_high = (file_offset >> (32 - KILO_BYTE_SHIFT));
				//offset_low  = ((file_offset << KILO_BYTE_SHIFT) & 0xFFFFFFFFUL);
				if (os_file_write(bkp_file_path, bkp_fd, buf, file_offset * KILO_BYTE, 
					buf_offset * KILO_BYTE) == 0) {
					fc_bkp_print_io_error();
					ut_free(sorted_blocks);
					*success = FALSE;
					os_file_close(bkp_fd);
					os_file_delete(innodb_file_data_key, bkp_file_path);
					fc_bkp_finish(unaligned_buf, compress_buf_unalign,
							bkp_file_path, bkp_file_path_final);
					return 0;
				}

				fprintf(stderr,".%.0f%%.", (i * 100.0) / n_dirty_pages);
				
				file_offset += buf_offset;
			}	

			ut_free(sorted_blocks);
			
			/* then write block metadata to backup file */
			//offset_high = (file_offset >> (32 - KILO_BYTE_SHIFT));
			//offset_low  = ((file_offset << KILO_BYTE_SHIFT) & 0xFFFFFFFFUL);

			blk_metas_size = ((n_backup_pages * sizeof(fc_bkp_blkmeta_t)) 
								/ KILO_BYTE + 1) * KILO_BYTE;
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache backup:blk metadata size %d\n", (int)blk_metas_size);
#endif		
			if (os_file_write(bkp_file_path, bkp_fd, blk_metas, file_offset * KILO_BYTE, 
					blk_metas_size) == 0) {
				fc_bkp_print_io_error();
				ut_free((void *)blk_metas);
				*success = FALSE;
				os_file_close(bkp_fd);
				os_file_delete(innodb_file_data_key, bkp_file_path);
				fc_bkp_finish(unaligned_buf, compress_buf_unalign,
						bkp_file_path, bkp_file_path_final);
				return 0;
			}

			ut_free((void *)blk_metas);

			/* last write backup info header to backup file */
			bkp_info->bkp_blk_count = n_backup_pages;
			bkp_info->bkp_page_pos = FC_BACKUP_FILE_PAGE_OFFSET;
			bkp_info->bkp_blk_meta_pos = file_offset;
#ifdef UNIV_FLASH_CACHE_TRACE
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache backup:blk_count:%d, page_pos:%d, meta_pos:%d\n", 
				(int)bkp_info->bkp_blk_count, (int)bkp_info->bkp_page_pos, (int)bkp_info->bkp_blk_meta_pos);
#endif
			ut_memcpy((void *)buf, (const void*)bkp_info, sizeof(fc_bkp_info_t));

			if (os_file_write(bkp_file_path, bkp_fd, buf, 0, KILO_BYTE) == 0) {
				fc_bkp_print_io_error();
				ut_free((void *)bkp_info);
				*success = FALSE;
				os_file_close(bkp_fd);
				os_file_delete(innodb_file_data_key, bkp_file_path);
				fc_bkp_finish(unaligned_buf, compress_buf_unalign,
						bkp_file_path, bkp_file_path_final);
				return 0;
			}

			ut_free((void *)bkp_info);
			
			os_file_flush(bkp_fd);
			os_file_close(bkp_fd);
			os_file_rename(innodb_file_data_key, bkp_file_path, bkp_file_path_final);
			fc_bkp_finish(unaligned_buf, compress_buf_unalign,
					bkp_file_path, bkp_file_path_final);
			*success = TRUE;

			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: L2 Cache completed backup.\n");
			return i;	
		} else {
			ut_free(sorted_blocks);
		}
	}
	
	/* no pages need to be backuped */
	ut_print_timestamp(stderr);
	fprintf(stderr, " InnoDB: L2 Cache no pages need to be backuped\n");
	*success = TRUE;
	return 0;
}

/********************************************************************//**
fc_backup_thread */
UNIV_INTERN
os_thread_ret_t
fc_backup_thread(
/*==================*/
	void* args)	/*!< in: arguments for the thread */
{
	ibool sucess;

	fc_backup(&sucess);
	if (!sucess) {
		ut_print_timestamp(stderr);
		fprintf(stderr, " InnoDB: Error: backup L2 Cache file failed\n");
	}
	srv_flash_cache_backuping = FALSE;
	os_thread_exit(NULL);

	OS_THREAD_DUMMY_RETURN;
}
