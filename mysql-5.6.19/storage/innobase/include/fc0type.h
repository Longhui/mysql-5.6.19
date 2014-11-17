/**************************************************//**
@file fc/fc0fc.c
Flash cache global type definitions

Created	24/4/2012 David Jiang (jiangchengyao@gmail.com)
Modified by Thomas Wen (wenzhenghu.zju@gmail.com)
*******************************************************/

#ifndef fc0type_h
#define fc0tye_h

#include "srv0srv.h"


/* flash cache space id */
#define FLASH_CACHE_SPACE 0xAAAAAAAUL

typedef struct fc_buf_struct		fc_buf_t;
typedef struct fc_block_struct	fc_block_t;
typedef struct fc_block_array_struct	fc_block_array_t;

typedef struct fc_page_info_struct	fc_page_info_t;
typedef struct fc_struct		fc_t;

typedef struct flash_cache_stat_struct flash_cache_stat_t;

#define UNIV_FLASH_CACHE_TRACE	//should undefine in release
#define UNIV_FLASH_CACHE_FOR_RECOVERY_SAFE //should define in release, undefine just for compare preformance with v1

#endif
