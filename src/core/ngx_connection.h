
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
    ngx_str_t           addr_text;  // IP��ַ��

    int                 type;   // �׽�������

    int                 backlog;  // ������Ӹ���
    int                 rcvbuf;   // �ں˶�������׽��ֽ��ջ�������С
    int                 sndbuf;   // �ں˶�������׽��ַ��ͻ�������С
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                 keepidle;
    int                 keepintvl;
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;  // ���µ�tcp���ӳɹ�������Ĵ�������

    void               *servers;  /* array of ngx_http_in_addr_t, �����˿ڶ�Ӧ�ŵ����������� */

    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;  // �´������ӵ��ڴ�س�ʼ����С
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;  // δ�յ��û����ݳ�ʱ��ֱ�Ӷ�������

    ngx_listening_t    *previous;
    ngx_connection_t   *connection;

    unsigned            open:1;   // 1ʱ��ʾ���������Ч������live����
    unsigned            remain:1; // ����������������
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    unsigned            listen:1;   //  ��ʾ��ǰ�ṹ���Ӧ���׽����Ѿ�����
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1; // ��ʾnginx�Ὣ�����ַת��Ϊ�ַ�����ʽ�ĵ�ַ

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
    void               *data;  // lgx_mark  ����δʹ��ʱ��ָ����һ�� ngx_connection_t������ʹ��ʱ����nginxģ�������
    ngx_event_t        *read;  // ��Ӧ�Ķ��¼�
    ngx_event_t        *write; // ��Ӧ��д�¼�

    ngx_socket_t        fd;    // �׽��־��

    ngx_recv_pt         recv;  // ���������ַ����ķ���
    ngx_send_pt         send;  // ���������ַ����ķ���
    
    ngx_recv_chain_pt   recv_chain;
    ngx_send_chain_pt   send_chain;

    ngx_listening_t    *listening;

    off_t               sent; // ��������Ѿ����͵��ֽ���

    ngx_log_t          *log;  // ���Լ�¼��־��ngx_log_t����

    ngx_pool_t         *pool; // �����Ӵ����󣬻�����һ��pool�����ӹرպ�����١�  pool_size

    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    ngx_str_t           addr_text; // �ַ���IP��ַ

    ngx_str_t           proxy_protocol_addr;

#if (NGX_SSL)
    ngx_ssl_connection_t  *ssl;
#endif

    struct sockaddr    *local_sockaddr; // �����˿ڶ�Ӧ��sockaddr
    socklen_t           local_socklen;

    ngx_buf_t          *buffer;   // ���ڽ��պͻ���ӿͻ��˷���������

    ngx_queue_t         queue;    // ����������

    ngx_atomic_uint_t   number;   // ����ʹ�õĴ���

    ngx_uint_t          requests; // �������������

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
    unsigned            aio_sendfile:1; // ʹ���첽IO�ķ�ʽ���������ļ����͸��������ӵ���һ��
    unsigned            busy_count:2;
    ngx_buf_t          *busy_sendfile; // �첽IOʱ������������������ļ�����Ϣ
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