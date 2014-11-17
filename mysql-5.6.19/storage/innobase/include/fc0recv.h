/**************************************************//**
@file fc/fc0recv.c
Flash Cache log recovery

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0recv_h
#define fc0recv_h

#include "fc0log.h"
#include "fc0fc.h"

/****************************************************************//**
Start flash cache log recovery, it means L2 Cache is not shutdown correctly.*/
UNIV_INTERN
void
fc_recv(void);
/*=======================*/

/******************************************************************//**
Exchange the page size from ,
@return: the size of page, with number of L2 Cache base blocks */
UNIV_INLINE
ulint
fc_calc_drop_page_size(
/*==================*/
	byte *page,	   /*<! in: data buffer read from ssd, contain data page */
	ulint buf_len); /*<! in: the len of the data buffer */

/******************************************************************//**
Check if the page size is zip_size,
@return: TRUE if the pge size is zip_size*/
UNIV_INLINE
ulint
fc_page_calc_size(
/*==================*/
	byte *read_buf, /*<! in: data buffer read from ssd, contain data page */
	ulint zip_size); /*<! in: the page size we guess */

#ifndef UNIV_NONINL
#include "fc0recv.ic"
#endif

#endif
