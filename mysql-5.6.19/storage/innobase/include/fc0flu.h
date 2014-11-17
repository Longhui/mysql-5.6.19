/**************************************************//**
@file fc/fc0flu.ic
Flash Cache for InnoDB

Created	24/10/2013 Thomas Wen(wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0flu_h
#define fc0flu_h

#include "univ.i"
#include "fc0fc.h"

#define PCT_IO_FC(p) ((ulong) (srv_fc_io_capacity * ((double) p / 100.0)))

/********************************************************************//**
Flush a batch of writes to the datafiles that have already been
written by the OS. */
UNIV_INTERN
void
fc_flush_sync_dbfile(void);
/*==========================*/

/********************************************************************//**
Flush pages from flash cache.
@return	number of pages have been flushed to tablespace */
UNIV_INTERN
ulint
fc_flush_to_disk(
/*==================*/
	ibool do_full_io);	/*!< in: whether do full io capacity */

/********************************************************************//**
Test and flush the fc log to disk if necessary. */
UNIV_INTERN
void
fc_flush_test_and_flush_log(
/*==========================*/
	ulint last_time); /*!< in: the last time when flush the fc log */

/********************************************************************//**
Test and dump block metadata to dump file if necessary. */
UNIV_INTERN
void
fc_flush_test_and_dump_blkmeta(
/*==========================*/
	ulint last_time); /*!< in: the last time when
						dump the block metadata */

#ifndef UNIV_NONINL
#include "fc0fill.ic"
#endif

#endif
