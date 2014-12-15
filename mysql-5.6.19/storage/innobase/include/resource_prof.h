#ifndef __RESOURCE_PROF__
#define __RESOURCE_PROF__

#ifdef __cplusplus
extern "C"
{
#endif
  typedef int (*_resource_profiler_func_ptr)(int with_io_read);
  extern _resource_profiler_func_ptr resource_profiler_func_ptr;
#ifdef __cplusplus
};
#endif
#endif // __RESOURCE_PROF__