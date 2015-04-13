#!/bin/sh

prefix=/mnt/ddb/1/wenzhh/version_work_v5/build/jemalloc
exec_prefix=/mnt/ddb/1/wenzhh/version_work_v5/build/jemalloc
libdir=${exec_prefix}/lib

LD_PRELOAD=${libdir}/libjemalloc.so.1
export LD_PRELOAD
exec "$@"
