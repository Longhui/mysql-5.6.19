/**************************************************//**
@file fc/fc0fill.h
Flash Cache(L2 Cache) for InnoDB

Created	24/10/2013 Thomas Wen(wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0fill_h
#define fc0fill_h

#include "univ.i"
#include "fc0fc.h"

/******************************************************************//**
whether bpage should be moved in flash cache
@return TRUE if need do move operation */
UNIV_INLINE
ibool
fc_LRU_need_move(
/*=====================*/
	fc_block_t* b); /*<! in: L2 Cache block if should move */

/******************************************************************//**
whether bpage should be migrate to flash cache
@return TRUE if need do migrate operation */
UNIV_INLINE
ibool
fc_LRU_need_migrate(
/*=====================*/
	fc_block_t* b, 	   /*<! in: block 'returned' by HASH_SEARCH */
	buf_page_t* bpage); /*<! in: the buf page replaced by buf LRU */

/**********************************************************************//**
Sync L2 Cache hash table from LRU remove page opreation */
UNIV_INTERN
void
fc_LRU_sync_hash_table(
/*==========================*/
	buf_page_t* bpage); /*!< in: frame to be written to L2 Cache */

/**********************************************************************//**
Move to L2 Cache if possible */
UNIV_INTERN
void
fc_LRU_move(
/*=========================*/
	buf_page_t* bpage);	/*!< in: page LRU out from buffer pool */

#ifndef UNIV_NONINL
#include "fc0fill.ic"
#endif

#endif
