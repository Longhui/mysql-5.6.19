#ifndef SQL_IOSTAT_INCLUDED
#define SQL_IOSTAT_INCLUDED

#include "my_global.h"
#include "my_sys.h"

#ifdef __cplusplus
extern "C" {
#endif
enum
{
  LOG_READ      = 0,
  PHY_READ
};

extern void thd_io_incr(uint stat_type);
extern void set_io_stat_flag(const my_bool *flag);

#ifdef __cplusplus
}
#endif
#endif /*SQL_IOSTAT_INCLUDED*/
