/**************************************************//**
@file fc/fc0warmup.c
Flash Cache warmup

Created by 24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by 24/10/2013 Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0warmup_h
#define fc0warmup_h

#include "fc0fc.h"

/********************************************************************//**
Compress the buffer, return the size of compress data.
the buf memory has alloced
@return the compressed size of page */
UNIV_INTERN
ulint
fc_block_do_compress_warmup(
/*==================*/
	byte* page, 	/*!< in: the data need compress */
	void* buf);	/*!< out: the buf contain the compressed data,
							must be the size of frame + 400 */

/********************************************************************//**
Warm up tablespaces to flash cache block.,stop if no space left. */
UNIV_INTERN
void
fc_warmup_tablespaces(void);
/*=============================*/

#ifndef UNIV_NONINL
#include "fc0warmup.ic"
#endif

#endif
