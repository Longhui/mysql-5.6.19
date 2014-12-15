#ifndef OPSTAT_INCLUDED
#define IOSTAT_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif
  typedef void (*_io_stat_func_ptr)(uint stat_type);

  extern _io_stat_func_ptr io_stat_func_ptr;

#ifdef __cplusplus
}
#endif

#endif /*IOSTAT_INCLUDED*/
