/**************************************************//**
@file fc/fc0warmup.c
Flash Cache warmup

Created by 24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by 24/10/2013 Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#include "fc0warmup.h"

#ifdef UNIV_NONINL
#include "fc0warmup.ic"
#endif

#include "srv0srv.h"
#include "srv0start.h"
#include "os0file.h"
#include "fil0fil.h"
#include "fc0log.h"

/********************************************************************//**
Compress the buffer, return the size of compress data.
the buf memory has alloced
@return the compressed size of page */
UNIV_INTERN
ulint
fc_block_do_compress_warmup(
/*==================*/
	byte* page, 	/*!< in: the data need compress */
	void* buf)	/*!< out: the buf contain the compressed data,
							must be the size of frame + 400 */
{
#ifndef _WIN32
	size_t cp_size; 
#endif

	srv_flash_cache_compress++;	
	
	if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_QUICKLZ) {
		/*
		 * we compress the page data to the buf offset  FC_ZIP_PAGE_DATA, so when we do pack,
		 * the compressed data need not to be moved, and avoid a memcpy operation
		 */
		return fc_qlz_compress(page, (char*)buf + FC_ZIP_PAGE_DATA, UNIV_PAGE_SIZE, (fc_qlz_state_compress*)fc->dw_zip_state);
		
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_SNAPPY) {
#ifndef _WIN32
		/*
		 * we compress the page data to the buf offset  FC_ZIP_PAGE_DATA, so when we do pack,
		 * the compressed data need not to be moved, and avoid a memcpy operation
		 */
		if (0 != snappy_compress((struct snappy_env*)fc->dw_zip_state, (const char*)page, 
				UNIV_PAGE_SIZE, (char*)buf + FC_ZIP_PAGE_DATA, &cp_size)) {
			ut_print_timestamp(stderr);
			fprintf(stderr, " InnoDB: [warning]L2 Cache snappy when compress page. \n");
			return UNIV_PAGE_SIZE;
		}

		return (ulint)cp_size;
#else
		return UNIV_PAGE_SIZE;
#endif
	} else if (srv_flash_cache_compress_algorithm == FC_BLOCK_COMPRESS_ZLIB) {
		//FIXME:if use zlib, how to compress the page;
		return UNIV_PAGE_SIZE;
	} else {
		return UNIV_PAGE_SIZE;
	}

}

/********************************************************************//**
Warm up tablespace to flash cache block.
@return: if we can still warmup other tablespace */
static
ibool
fc_warmup_tablespace(
/*=============================*/
	os_file_t	file,		/*!< in: file of tablespace */
	const char* dbname,		/*!< in: database name */
	const char*	tablename,	/*!< in: tablespace to load tpcc.*:test.mysql */
	ulint space_id)			/*!< in: tablespace space id */
{

	void*	buf_unaligned = NULL;
	void*	buf;
	byte*	page;
	byte*	compress_data;
	char*	token;
	char*	name;
	char*	name2;
	char*	str;
	
	ibool	success = FALSE;
	ulint	n_blocks;
	ulint	i, j, ret = TRUE;
	ulint	zip_size, blk_size;
	ulint 	fc_blk_size_byte = fc_get_block_size_byte();
	ulint 	need_compress, cp_size;
	ulint	offset, foffset, foffset_high;
	ulint 	block_offset, byte_offset;

	fc_block_t* b;

	name = (char*)ut_malloc(strlen(dbname) + strlen(tablename) + 3);
	sprintf(name,"%s.%s",dbname,tablename);
		
	name2 = (char*)ut_malloc(strlen(dbname) + 3);
	sprintf(name2,"%s.*",dbname);

	str = (char*)ut_malloc(strlen(srv_flash_cache_warmup_table) + 1);
	ut_strcpy(str, srv_flash_cache_warmup_table);
		
	token = strtok(str,":");
	while (token != NULL && !success) {
		if (ut_strcmp(token, name) == 0 || ut_strcmp(token, name2) == 0)
			success = TRUE;
			
		token = strtok(NULL,":");
	}

	if (!success) {
		ut_free((void*)name);
		ut_free((void*)name2);
		ut_free((void*)str);
		return ret;
	}

	/* get zip size */
	zip_size = fil_space_get_zip_size(space_id);
	if (zip_size == 0) {
		zip_size = UNIV_PAGE_SIZE;
	}

	/* malloc memory for page to read */
	buf_unaligned = (byte*)ut_malloc((2 * FSP_EXTENT_SIZE + 1) * zip_size);
	buf = (byte*)ut_align(buf_unaligned, zip_size);

	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: start to warm up tablespace %s.%s to L2 Cache.\n",
		dbname, tablename);

	i = 0;
	n_blocks = zip_size / fc_blk_size_byte;
	while (fc_get_available() > n_blocks) {
		foffset = ((ulint)(i * zip_size)) & 0xFFFFFFFFUL;
		foffset_high = (ib_uint64_t)(i * zip_size) >> 32;
		success = os_file_read_no_error_handling(file, buf, foffset, foffset_high, 
					zip_size * 2 * FSP_EXTENT_SIZE);
			
		if (!success) {
			ut_print_timestamp(stderr);
			fprintf(stderr," InnoDB: failed to read %s.%s pages when doing L2 Cache warmup.\n",
				dbname, tablename);
			goto exit;
		}
			
		for (j = 0; j < 2 * FSP_EXTENT_SIZE; j++) {
			page = (unsigned char*)buf + j * zip_size;
			if ((fil_page_get_type(page) != FIL_PAGE_INDEX)
					&& (fil_page_get_type(page) != FIL_PAGE_INODE)) {
				continue;
			}

			offset = mach_read_from_4(page + FIL_PAGE_OFFSET);
			ut_a(mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID) == space_id);	

			//rw_lock_x_lock(&fc->hash_rwlock);
			b = fc_block_search_in_hash(space_id, offset);
			
			if (b) {
				/* page in flash cache always newer than page in disk */
				//rw_lock_x_unlock(&fc->hash_rwlock);
				continue;
			} 

			/* if need compress, compress the data now */
			need_compress = fc_block_need_compress(space_id);
			if (need_compress == TRUE) {
				compress_data = fc->dw_zip_buf + j * FC_ZIP_COMPRESS_BUF_SIZE;
				cp_size = fc_block_do_compress_warmup(page, compress_data);

#ifdef UNIV_FLASH_CACHE_TRACE
				//fprintf(stderr," (%lu, %lu) cps %lu ", space_id, offset, cp_size);
#endif

				if (fc_block_compress_successed(cp_size) == FALSE) {
					need_compress = FALSE;
				} else {
					blk_size = fc_block_compress_align(cp_size);
				}
			}
			
			b = fc_block_init(fc->write_off);
			ut_a(b == fc_get_block(fc->write_off));
					
			b->offset = offset;
			b->space = space_id;
			b->state = BLOCK_READ_CACHE;
			b->size = n_blocks;			
			if (need_compress == FALSE) {
				b->raw_zip_size = 0;
				compress_data = page;
				blk_size = n_blocks;
			} else {
				b->raw_zip_size = cp_size;
				fc_block_pack_compress(b, compress_data);
			}
			
#ifdef UNIV_FLASH_CACHE_TRACE
			//fprintf(stderr," warmup (%lu, %lu)  ", space_id, offset);
#endif

			fc_io_offset(b->fil_offset, &block_offset, &byte_offset);
			success = fil_io(OS_FILE_WRITE | OS_AIO_SIMULATED_WAKE_LATER, FALSE,
						FLASH_CACHE_SPACE, 0, block_offset, byte_offset, 
						blk_size * fc_blk_size_byte, compress_data, NULL);
			
			if (success != DB_SUCCESS) {
				ut_print_timestamp(stderr);
				fprintf(stderr," InnoDB [Error]: Can not write page(%lu,%lu) to L2 Cache.\n", 
					space_id, offset);
				ut_error;
			}
					
			srv_flash_cache_used += blk_size;
			srv_flash_cache_used_nocompress += n_blocks;

			/* insert to hash table */
			fc_block_insert_into_hash(b);					

			fc_inc_write_off(blk_size);
			fc_inc_flush_off(blk_size);
				
			//rw_lock_x_unlock(&fc->hash_rwlock);
				
			if (((fc->write_off + zip_size / fc_blk_size_byte) > fc_get_size())
					|| (fc->write_off == 0)) {
				ret = FALSE;
				goto l2cache_full;
			}
		}

		os_aio_simulated_wake_handler_threads();
		os_aio_wait_until_no_pending_fc_writes();
		fil_flush_file_spaces(FIL_FLASH_CACHE);
		i = i + 2 * FSP_EXTENT_SIZE;
	}

l2cache_full:
	if (ret == FALSE) {
		os_aio_simulated_wake_handler_threads();
		os_aio_wait_until_no_pending_fc_writes();
		fil_flush_file_spaces(FIL_FLASH_CACHE);

		ut_print_timestamp(stderr);
		fprintf(stderr,
			" InnoDB: warm up table %s.%s to space: %lu offset %lu.(100%%)\n",
			dbname,tablename,space_id,i);
		
		ut_print_timestamp(stderr);
		fprintf(stderr," InnoDB: L2 Cache is full, warmup stop.\n");
	}

exit:

	ut_free(buf_unaligned);
	ut_free((void*)name);
	ut_free((void*)name2);
	ut_free((void*)str);
	
	return ret;
	
}

/********************************************************************//**
Warm up tablespaces to flash cache block.,stop if no space left. */
UNIV_INTERN
void
fc_warmup_tablespaces(void)
/*=============================*/
{
	int		ret;
	char*	dbpath	= NULL;
	ulint	dbpath_len	= 100;
	ulint	err	= DB_SUCCESS;
	os_file_dir_t	dir;
	os_file_dir_t	dbdir;
	os_file_stat_t	dbinfo;
	os_file_stat_t	fileinfo;

	if (srv_flash_cache_size == 0 || !fc_log->first_use) {
		return;
	}

	/* The datadir of MySQL is always the default directory of mysqld */

	dir = os_file_opendir(fil_path_to_mysql_datadir, TRUE);

	if (dir == NULL) {
		return;
	}

	dbpath = (char*)mem_alloc(dbpath_len);

	/* Scan all directories under the datadir. They are the database
	directories of MySQL. */

	ret = fil_file_readdir_next_file(&err, fil_path_to_mysql_datadir, dir,
					 &dbinfo);
	while (ret == 0) {
		ulint len;
		//printf("Looking at %s in datadir\n", dbinfo.name); 

		if (dbinfo.type == OS_FILE_TYPE_FILE || dbinfo.type == OS_FILE_TYPE_UNKNOWN) {
			goto next_datadir_item;
		}

		/*
		 * We found a symlink or a directory; try opening it to see
		 * if a symlink is a directory
		 */

		len = strlen(fil_path_to_mysql_datadir) + strlen (dbinfo.name) + 2;
		if (len > dbpath_len) {
			dbpath_len = len;

			if (dbpath) {
				mem_free(dbpath);
			}

			dbpath = (char*)mem_alloc(dbpath_len);
		}
		sprintf(dbpath, "%s/%s", fil_path_to_mysql_datadir, dbinfo.name);
		srv_normalize_path_for_win(dbpath);

		dbdir = os_file_opendir(dbpath, FALSE);

		if (dbdir != NULL) {
			//printf("Opened dir %s\n", dbinfo.name); 

			/*
			 * We found a database directory; loop through it,
			 * looking for possible .ibd files in it
			 */
			ret = fil_file_readdir_next_file(&err, dbpath, dbdir, &fileinfo);
			while (ret == 0) {
				//printf(" Looking at file %s\n", fileinfo.name); 

				if (fileinfo.type == OS_FILE_TYPE_DIR) {
					goto next_file_item;
				}

				/* We found a symlink or a file */
				if (strlen(fileinfo.name) > 4
				    && 0 == strcmp(fileinfo.name + strlen(fileinfo.name) - 4, ".ibd")) {
					/* The name ends in .ibd; try opening the file */
				   	char*		filepath;
					os_file_t	file;
					ibool		success;
					byte*		buf2;
					byte*		page;
					ulint		space_id;
					/* Initialize file path */
					filepath = (char*)mem_alloc(strlen(dbinfo.name) + strlen(fileinfo.name)
									+ strlen(fil_path_to_mysql_datadir) + 3);
					sprintf(filepath, "%s/%s/%s", fil_path_to_mysql_datadir, dbinfo.name,
						fileinfo.name);
					srv_normalize_path_for_win(filepath);
					//dict_casedn_str(filepath);

					/* Get file handler */
					file = os_file_create_simple_no_error_handling(innodb_file_data_key,
						filepath, OS_FILE_OPEN, OS_FILE_READ_ONLY, &success);
#ifdef UNIV_FLASH_CACHE_TRACE
					//printf(" filepath %s, success %lu\n", filepath, (ulong)success);
#endif
					/* Get space id */
					buf2 = (byte*)ut_malloc(2 * UNIV_PAGE_SIZE);
					/* Align the memory for file i/o if we might have O_DIRECT set */
					page = (byte*)ut_align(buf2, UNIV_PAGE_SIZE);
					os_file_read(file, page, 0, 0, UNIV_PAGE_SIZE);
					/* We have to read the tablespace id from the file */
					space_id = fsp_header_get_space_id(page);

					/* Preload to L2 Cache */
					if (fc_warmup_tablespace(file, dbinfo.name, strtok(fileinfo.name,"."), space_id)
							== FALSE) {
						goto finish;	
					}
					
					os_file_close(file);
					ut_free(buf2);
					mem_free(filepath);
				}
next_file_item:
				ret = fil_file_readdir_next_file(&err, dbpath, dbdir, &fileinfo);
			}

			if (0 != os_file_closedir(dbdir)) {
				fputs("InnoDB: Warning: could not close database directory ", stderr);
				ut_print_filename(stderr, dbpath);
				putc('\n', stderr);
				err = DB_ERROR;
			}
		}

next_datadir_item:
		ret = fil_file_readdir_next_file(&err, fil_path_to_mysql_datadir, dir, &dbinfo);
	}

finish:

	ut_print_timestamp(stderr);
	fprintf(stderr," InnoDB: flash cache warm up finish.\n");
	
#ifdef UNIV_FLASH_CACHE_TRACE
	fc_validate();
#endif

	mem_free(dbpath);
}
