
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    ngx_socket_t        fd;

    struct sockaddr    *sockaddr;
    socklen_t           socklen;    /* size of sockaddr */
    size_t              addr_text_max_len;  
    ngx_str_t           addr_text;  // IP地址串

    int                 type;   // 套接字类型

    int                 backlog;  // 最大连接个数
    int                 rcvbuf;   // 内核对于这个套接字接收缓冲区大小
    int                 sndbuf;   // 内核对于这个套接字发送缓冲区大小
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                 keepidle;
    int                 keepintvl;
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;  // 当新的tcp连接成功建立后的处理方法

    void               *servers;  /* array of ngx_http_in_addr_t, 监听端口对应着的所有主机名 */

    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;  // 新创建连接的内存池初始化大小
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;  // 未收到用户数据超时后，直接丢弃连接

    ngx_listening_t    *previous;
    ngx_connection_t   *connection;

    unsigned            open:1;   // 1时表示监听句柄有效，用于live升级
    unsigned            remain:1; // 用于在线升级程序
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    unsigned            listen:1;   //  表示当前结构体对应的套接字已经监听
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1; // 表示nginx会将网络地址转变为字符串形式的地址

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned            ipv6only:1;
#endif
    unsigned            keepalive:2;

#if (NGX_HAVE_DEFERRED_ACCEPT)
    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#ifdef SO_ACCEPTFILTER
    char               *accept_filter;
#endif
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    int                 fastopen;
#endif

};


typedef enum {
     NGX_ERROR_ALERT = 0,
     NGX_ERROR_ERR,
     NGX_ERROR_INFO,
     NGX_ERROR_IGNORE_ECONNRESET,
     NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
     NGX_TCP_NODELAY_UNSET = 0,
     NGX_TCP_NODELAY_SET,
     NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
     NGX_TCP_NOPUSH_UNSET = 0,
     NGX_TCP_NOPUSH_SET,
     NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01
#define NGX_SPDY_BUFFERED      0x02


struct ngx_connection_s {
    void               *data;  // lgx_mark  链接未使用时，指向下一个 ngx_connection_t。链接使用时，由nginx模块决定。
    ngx_event_t        *read;  // 对应的读事件
    ngx_event_t        *write; // 对应的写事件

    ngx_socket_t        fd;    // 套接字句柄

    ngx_recv_pt         recv;  // 接收网络字符流的方法
    ngx_send_pt         send;  // 发送网络字符流的方法
    
    ngx_recv_chain_pt   recv_chain;
    ngx_send_chain_pt   send_chain;

    ngx_listening_t    *listening;

    off_t               sent; // 这个链接已经发送的字节数

    ngx_log_t          *log;  // 可以记录日志的ngx_log_t对象

    ngx_pool_t         *pool; // 新连接创建后，会申请一个pool，链接关闭后会销毁。  pool_size

    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    ngx_str_t           addr_text; // 字符串IP地址

    ngx_str_t           proxy_protocol_addr;

#if (NGX_SSL)
    ngx_ssl_connection_t  *ssl;
#endif

    struct sockaddr    *local_sockaddr; // 监听端口对应的sockaddr
    socklen_t           local_socklen;

    ngx_buf_t          *buffer;   // 用于接收和缓存从客户端发来的数据

    ngx_queue_t         queue;    // 可重用链接

    ngx_atomic_uint_t   number;   // 链接使用的次数

    ngx_uint_t          requests; // 处理的请求次数

    unsigned            buffered:8;

    unsigned            log_error:3;     /* ngx_connection_log_error_e */

    unsigned            unexpected_eof:1;
    unsigned            timedout:1;
    unsigned            error:1;
    unsigned            destroyed:1;

    unsigned            idle:1;
    unsigned            reusable:1;
    unsigned            close:1;

    unsigned            sendfile:1;
    unsigned            sndlowat:1;
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

    unsigned            need_last_buf:1;

#if (NGX_HAVE_IOCP)
    unsigned            accept_context_updated:1;
#endif

#if (NGX_HAVE_AIO_SENDFILE)
    unsigned            aio_sendfile:1; // 使用异步IO的方式将磁盘上文件发送给网络连接的另一端
    unsigned            busy_count:2;
    ngx_buf_t          *busy_sendfile; // 异步IO时，缓冲区保存待发送文件的信息
#endif

#if (NGX_THREADS)
    ngx_atomic_t        lock;
#endif
};


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, void *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */
