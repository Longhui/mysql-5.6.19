/* Copyright (c) 2007, 2012, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/*
  Implementation for the thread scheduler
*/

#include <sql_priv.h>
#include "unireg.h"                    // REQUIRED: for other includes
#include "scheduler.h"
#include "sql_connect.h"         // init_new_connection_handler_thread
#include "scheduler.h"
#include "sql_callback.h"
#include "global_threads.h"
#include "mysql/thread_pool_priv.h"
#include <violite.h>

/*
  End connection, in case when we are using 'no-threads'
*/

static bool no_threads_end(THD *thd, bool put_in_cache)
{
  thd_release_resources(thd);
  dec_connection_count(thd);

  // THD is an incomplete type here, so use destroy_thd() to delete it.
  mysql_mutex_lock(&LOCK_thread_count);
  remove_global_thread(thd);
  mysql_mutex_unlock(&LOCK_thread_count);
  destroy_thd(thd);

  return 1;                                     // Abort handle_one_connection
}

static scheduler_functions thread_scheduler_struct, extra_thread_scheduler_struct;
scheduler_functions *thread_scheduler= &thread_scheduler_struct,
                    *extra_thread_scheduler= &extra_thread_scheduler_struct;

/** @internal
  Helper functions to allow mysys to call the thread scheduler when
  waiting for locks.
*/

/**@{*/
extern "C"
{
static void scheduler_wait_lock_begin(void) {
  MYSQL_CALLBACK(thread_scheduler,
                 thd_wait_begin, (current_thd, THD_WAIT_TABLE_LOCK));
}

static void scheduler_wait_lock_end(void) {
  MYSQL_CALLBACK(thread_scheduler, thd_wait_end, (current_thd));
}

static void scheduler_wait_sync_begin(void) {
  MYSQL_CALLBACK(thread_scheduler,
                 thd_wait_begin, (current_thd, THD_WAIT_TABLE_LOCK));
}

static void scheduler_wait_sync_end(void) {
  MYSQL_CALLBACK(thread_scheduler, thd_wait_end, (current_thd));
}

static void scheduler_wait_net_begin(void) {
   thd_wait_begin(NULL, THD_WAIT_NET);
}

static void scheduler_wait_net_end(void) {
   thd_wait_end(NULL);
}
};
/**@}*/

/**
  Common scheduler init function.

  The scheduler is either initialized by calling
  one_thread_scheduler() or one_thread_per_connection_scheduler() in
  mysqld.cc, so this init function will always be called.
 */
void scheduler_init() {
  thr_set_lock_wait_callback(scheduler_wait_lock_begin,
                             scheduler_wait_lock_end);
  thr_set_sync_wait_callback(scheduler_wait_sync_begin,
                             scheduler_wait_sync_end);
  vio_set_wait_callback(scheduler_wait_net_begin,
    scheduler_wait_net_end);
}

/*
  Initialize scheduler for --thread-handling=one-thread-per-connection
*/

#ifndef EMBEDDED_LIBRARY
void one_thread_per_connection_scheduler(scheduler_functions *func,
    ulong *arg_max_connections,
    uint *arg_connection_count)
{
  scheduler_init();
  func->max_threads= *arg_max_connections + 1;
  func->max_connections= arg_max_connections;
  func->connection_count= arg_connection_count;
  func->init_new_connection_thread= init_new_connection_handler_thread;
  func->add_connection= create_thread_to_handle_connection;
  func->end_thread= one_thread_per_connection_end;
}

#endif

/*
  Initailize scheduler for --thread-handling=no-threads
*/

void one_thread_scheduler(scheduler_functions *func)
{
  scheduler_init();
  func->max_threads= 1;
  func->max_connections= &max_connections;
  func->connection_count= &connection_count;
#ifndef EMBEDDED_LIBRARY
  func->init_new_connection_thread= init_new_connection_handler_thread;
  func->add_connection= handle_connection_in_main_thread;
#endif
  func->end_thread= no_threads_end;
}


/*
  Initialize scheduler for --thread-handling=one-thread-per-connection
*/

/*
  thd_scheduler keeps the link between THD and events.
  It's embedded in the THD class.
*/

thd_scheduler::thd_scheduler()
  : m_psi(NULL), data(NULL)
{
}


thd_scheduler::~thd_scheduler()
{
}

//static scheduler_functions *saved_thread_scheduler;
//static uint saved_thread_handling;

/*
  no pluggable schedulers in mariadb.
  when we'll want it, we'll do it properly
*/
#if 0

extern "C"
int my_thread_scheduler_set(scheduler_functions *scheduler)
{
  DBUG_ASSERT(scheduler != 0);

  if (scheduler == NULL)
    return 1;

  saved_thread_scheduler= thread_scheduler;
  saved_thread_handling= thread_handling;
  thread_scheduler= scheduler;
  // Scheduler loaded dynamically
  thread_handling= SCHEDULER_TYPES_COUNT;
  return 0;
}


extern "C"
int my_thread_scheduler_reset()
{
  DBUG_ASSERT(saved_thread_scheduler != NULL);

  if (saved_thread_scheduler == NULL)
    return 1;

  thread_scheduler= saved_thread_scheduler;
  thread_handling= saved_thread_handling;
  saved_thread_scheduler= 0;
  return 0;
}
#else
extern "C" int my_thread_scheduler_set(scheduler_functions *scheduler)
{ return 1; }

extern "C" int my_thread_scheduler_reset()
{ return 1; }
#endif



