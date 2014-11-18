//class NET;
#ifndef SEMISYNC_VSR_H
#define SEMISYNC_VSR_H
#define MYSQL_SERVER
typedef struct st_net NET;
#ifdef __cpluscplus
extern "C" 
{
#endif
 void send_slave_sync_info(NET *net, const char* fname, unsigned long long pos);
 void adjust_binlog_with_slave(char *host, unsigned int port, char *user, char* passwd, char* last_binlog);
#ifdef __cpluscplus
}
#endif

#endif
