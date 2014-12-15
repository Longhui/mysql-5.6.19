#ifndef _RESOURCE_PROFILE_H_
#define _RESOURCE_PROFILE_H_

#ifndef __WIN__
#include<unistd.h>
#endif

#ifdef __cplusplus

class THD;
extern "C"
{
#endif
  void start_trx_statistics(THD *thd);
  int  resource_statistics(int with_io_read);
#ifdef __cplusplus
}
#endif
#endif