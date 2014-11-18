#include <m_string.h>
#include "semisync_vsr.h"
#include "my_sys.h"
#include "mysql.h"
#include "sql_common.h"
#include "mysql_com.h"
#include "replication.h"


#ifndef MYSQL_SERVER
#define MYSQL_SERVER
#endif

extern void sql_print_error(const char *format, ...);
extern void sql_print_warning(const char *format, ...);
extern void sql_print_information(const char *format, ...);

LOG_CALLBACK_FUNC binlog_append_cb= NULL;
void set_binlog_append_event_cb(LOG_CALLBACK_FUNC func)
{
  binlog_append_cb= func;
}

static int my_binlog_append_cb(IO_CACHE *log)
{
  if (binlog_append_cb)
    return binlog_append_cb(log);

  return 1;
}

static int get_slave_sync_info(char *host, uint port, char *user, char* passwd, 
                               char** fname __attribute__((unused)), 
                               ulonglong* pos __attribute__((unused)))
{
  MYSQL *mysql;
  const char *buf;
  int error= 0;
  int reconnect= 0;
  ulong len= 0;
  uint net_timeout= 3600;
  if (!(mysql = mysql_init(NULL)))
  {
    return -1;
  }
  mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *) &net_timeout);
  mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, (char *) &net_timeout);
  mysql_options(mysql, MYSQL_SET_CHARSET_NAME, default_charset_info->csname);
  while( 0 == mysql_real_connect(mysql, host, user,  
           passwd, 0, port, 0, CLIENT_REMEMBER_OPTIONS))
  {
    reconnect++;
    mysql->reconnect= 1;
    my_sleep(reconnect * 100000);
    if (reconnect >= 5)
    {
      sql_print_error("VSR HA : can't connect ha_parter (errno:%d)", (int)mysql_errno(mysql));
      error =-1;
      goto err;
    }
  }
  if (simple_command(mysql, COM_VSR_QUERY, 0, 0, 1))
  {
    sql_print_information("VSR HA : vsr query_ha parter fail!");
    error= -1;
    goto err;
  }
  len = cli_safe_read(mysql);
  if ( len==packet_error || (long)len < 1)
  {
    sql_print_error("VSR HA : net error ");
    error= -1;
    goto err;
  }
  buf= (const char*)mysql->net.read_pos;
  *pos= uint8korr(buf);
  *fname= my_strdup(buf+8, MYF(MY_WME));
err:
  if(mysql)
  {
    mysql_close(mysql);
  }
  return error;
}

static int truncate_file(const char *fname, my_off_t len)
{
  File fd;
  if (fname == 0 || len == 0)
  {
    return 0;
  }
  fd=my_open(fname, O_RDWR | O_BINARY | O_SHARE, MYF(MY_WME));
  if (fd < 0)
  {
    return -1;
  }
  if (my_chsize(fd, len, 0, MYF(MY_WME)))
  {
    return -1;
  }
  my_close(fd, MYF(MY_WME));
  return 0;
}

static int append_rollback_event(const char *fname)
{
  IO_CACHE log;
  File fd;
  my_off_t end;
  fd= my_open(fname, O_RDWR | O_BINARY | O_SHARE, MYF(MY_WME));
  if (fd < 0 )
  {
    return -1;
  }
  end= my_seek(fd, 0L, MY_SEEK_END, MYF(0));
  if (init_io_cache(&log, fd, IO_SIZE*2, WRITE_CACHE, end, 0,
                    MYF(MY_WME|MY_DONT_CHECK_FILESIZE)))
  {
    return -1;
  }
  if (my_binlog_append_cb(&log))
  {
    return -1;
  }
  end_io_cache(&log);
  my_close(fd, MYF(MY_WME));
  return 0;
}

void adjust_binlog_with_slave(char *host, uint port, char *user, char* passwd, char* last_binlog)
{
  my_off_t len;
  char* binlog= NULL;
  if (-1 == get_slave_sync_info(host, port, user ,passwd, &binlog, &len))
  {
    goto over;
  }
  sql_print_information("VSR HA : slave receive master binlog %s %lld", binlog, len);  
  if (0 != strncmp(last_binlog, binlog, strlen(last_binlog)))
  {
    sql_print_information("VSR HA : Don't truncate master's last binlog %s"
                          "for slave receive binlog %s", last_binlog, binlog);
    goto over;
  }
  if (-1 == truncate_file(binlog, len))
  {
    goto over;
  }
  sql_print_information("VSR HA : truncate binlog %s length to %lld", binlog, len);
  if (-1 == append_rollback_event(binlog))
  {
    goto over;
  }
  sql_print_information("VSR HA : append rollback to %s", binlog);
over:
  if (NULL != binlog)
    my_free(binlog);
}

void send_slave_sync_info(NET *net, const char* fname, my_off_t pos)
{
   int len= 0;
   uchar buf[100];
   bool res;
   int8store(buf, pos);
   len = strlen(fname);
   memcpy(buf + 8, fname, len + 1);
   /* Send the reply. */
   res = my_net_write(net, (const uchar*)buf, len + 8);
   if (!res)
   {
     res = net_flush(net);
     if (res)
      sql_print_warning("VSR HA : slave net_flush() reply failed");
   }
   else
   {
     sql_print_warning("VSR HA : slave send reply failed: %s (%d)",
                     net->last_error, net->last_errno);
   }
   sql_print_information("VSR HA : send replication information (%s, %lld)", fname, pos);
}
