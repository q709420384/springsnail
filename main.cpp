#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <vector>

#include "log.h"
#include "conn.h"
#include "mgr.h"
#include "processpool.h"

using std::vector;

static const char* version = "1.0";

static void usage( const char* prog )
{
    log( LOG_INFO, __FILE__, __LINE__,  "usage: %s [-h] [-v] [-f config_file]", prog );				
}
//ANSI C标准中几个标准预定义宏
// __FILE__ :在源文件中插入当前原文件名
// __LINE__ ：在源文件中插入当前源代码行号
// _DATE_: 在源文件插入当前的编译日期
// _TIME_: 在源文件中插入的编译时间
// _STDC_ : 当要求程序严格遵循ANSI C 标准时刻标示被赋值为1
// _cplusplus: 当编写C++程序时该标识符被定义
int main( int argc, char* argv[] )
{
    char cfg_file[1024];						//配置文件
    memset( cfg_file, '\0', 100 );
    int option;
    while ( ( option = getopt( argc, argv, "f:xvh" ) ) != -1 )					//getopt函数用来分析命令行参数
    {
        switch ( option )
        {
            case 'x':
            {
                set_loglevel( LOG_DEBUG );										//log.cpp
                break;
            }
            case 'v':  //版本信息
            {
                log( LOG_INFO, __FILE__, __LINE__, "%s %s", argv[0], version );
                return 0;
            }
            case 'h':  //帮助 help
            {
                usage( basename( argv[ 0 ] ) );
                return 0;
            }
            case 'f':
            {
                memcpy( cfg_file, optarg, strlen( optarg ) );					// cfg_file  此时的内容是 config.xml  比如运行时：./main -fconfig.xml
                break;
            }
            case '?':  //无效的参数或者缺少参数的选项值
            {
                log( LOG_ERR, __FILE__, __LINE__, "un-recognized option %c", option );
                usage( basename( argv[ 0 ] ) );
                return 1;
            }
        }
    }    

    if( cfg_file[0] == '\0' )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "please specifiy the config file" );
        return 1;
    }
    int cfg_fd = open( cfg_file, O_RDONLY );			//打开配置文件 config.xml， cfg_fd是config.xml文件的文件描述符
    if( !cfg_fd )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
    struct stat ret_stat;
    if( fstat( cfg_fd, &ret_stat ) < 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
    char* buf = new char [ret_stat.st_size + 1];
    memset( buf, '\0', ret_stat.st_size + 1 );
    ssize_t read_sz = read( cfg_fd, buf, ret_stat.st_size );			//buf此时的内容是config.xml文件的内容
    if ( read_sz < 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
    vector< host > balance_srv;							//host在前面的mgr.h文件中定义   //一个是负载均衡服务器,另一个是逻辑服务器
    vector< host > logical_srv;
    host tmp_host;
    memset( tmp_host.m_hostname, '\0', 1024 );
    char* tmp_hostname;
    char* tmp_port;
    char* tmp_conncnt;
    bool opentag = false;
    char* tmp = buf;				//此时tem指向config.xml文件的内容
    char* tmp2 = NULL;
    char* tmp3 = NULL;
    char* tmp4 = NULL;
    while( tmp2 = strpbrk( tmp, "\n" ) )	//在源字符串tmp中找出最先含有搜索字符串"\n"中任一字符的位置并返回，若没找到则返回空指针		
    {
        *tmp2++ = '\0';
        if( strstr( tmp, "<logical_host>" ) )		//strstr(str1,str2)函数用于判断字符串str2是否是str1的子串。如果是，则该函数返回str2在str1首次出现的地址，否则返回null
        {
            if( opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            opentag = true;
        }
        else if( strstr( tmp, "</logical_host>" ) )								// 有/	符号	
        {
            if( !opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            logical_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
            opentag = false;
        }
        else if( tmp3 = strstr( tmp, "<name>" ) )
        {
            tmp_hostname = tmp3 + 6;										//将tmp_hostname指针指向<name>后面的IP地址的首个地址
            tmp4 = strstr( tmp_hostname, "</name>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            memcpy( tmp_host.m_hostname, tmp_hostname, strlen( tmp_hostname ) );
        }
        else if( tmp3 = strstr( tmp, "<port>" ) )
        {
            tmp_port = tmp3 + 6;
            tmp4 = strstr( tmp_port, "</port>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_port = atoi( tmp_port );
        }
        else if( tmp3 = strstr( tmp, "<conns>" ) )
        {
            tmp_conncnt = tmp3 + 7;
            tmp4 = strstr( tmp_conncnt, "</conns>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_conncnt = atoi( tmp_conncnt );
        }
        else if( tmp3 = strstr( tmp, "Listen" ) )
        {
            tmp_hostname = tmp3 + 6;
            tmp4 = strstr( tmp_hostname, ":" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4++ = '\0';
            tmp_host.m_port = atoi( tmp4 );
            memcpy( tmp_host.m_hostname, tmp3, strlen( tmp3 ) );
            balance_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
        }
        tmp = tmp2;
    }

    if( balance_srv.size() == 0 || logical_srv.size() == 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
        return 1;
    }
    const char* ip = balance_srv[0].m_hostname;			//balance_srv数组里只有一个元素
    int port = balance_srv[0].m_port;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
 
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
    {
        return -1;
    }
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    //memset( cfg_host.m_hostname, '\0', 1024 );
    //memcpy( cfg_host.m_hostname, "127.0.0.1", strlen( "127.0.0.1" ) );
    //cfg_host.m_port = 54321;
    //cfg_host.m_conncnt = 5;
    processpool< conn, host, mgr >* pool = processpool< conn, host, mgr >::create( listenfd, logical_srv.size() );
    if( pool )
    {
        pool->run( logical_srv );
        delete pool;
    }

    close( listenfd );
    return 0;
}
