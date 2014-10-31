
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {   // lgx_mark  可以支持文件与内存   ngx_pool_cleanup_add
    ngx_pool_cleanup_pt   handler;
    void                 *data;  
    ngx_pool_cleanup_t   *next;
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;
};


typedef struct {
    u_char               *last;    // 内存池数据区的开始地址
    u_char               *end;     // 内存池结束位置
    ngx_pool_t           *next;    // 内存池里面有很多内存，这些内存就是通过该指针链成链表
    ngx_uint_t            failed;  // 内存池分配失败次数
} ngx_pool_data_t;


struct ngx_pool_s {      // 内存池头部结构

    ngx_pool_data_t       d;       // 内存池数据块，从操作系统申请内存。内存小于等于max字段
    size_t                max;     // 赋值为size-sizeof(ngx_pool_t)和NGX_MAX_ALLOC_FROM_POOL这两者中比较小的。
    ngx_pool_t           *current; // 表示当前可分配内存的内存池
    ngx_chain_t          *chain;   // 该指针挂接一个ngx_chain_t 结构
    ngx_pool_large_t     *large;   // 继续从操作系统申请内存。内存大于等于max字段,如果大于pool预申请的内存，独立申请加入这个链表
    ngx_pool_cleanup_t   *cleanup; // 通过回调函数，对外部资源的清理
    ngx_log_t            *log;     // 日志信息
	
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
