
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define DEFAULT_CONNECTIONS  512


extern ngx_module_t ngx_kqueue_module;
extern ngx_module_t ngx_eventport_module;
extern ngx_module_t ngx_devpoll_module;
extern ngx_module_t ngx_epoll_module;
extern ngx_module_t ngx_rtsig_module;
extern ngx_module_t ngx_select_module;


static char *ngx_event_init_conf(ngx_cycle_t *cycle, void *conf);
static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle);
static char *ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd,
                                   void *conf);
static char *ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd,
                                        void *conf);

static void *ngx_event_core_create_conf(ngx_cycle_t *cycle);
static char *ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf);


static ngx_uint_t     ngx_timer_resolution;
sig_atomic_t          ngx_event_timer_alarm;

static ngx_uint_t     ngx_event_max_module;

ngx_uint_t            ngx_event_flags;
ngx_event_actions_t   ngx_event_actions;


static ngx_atomic_t   connection_counter = 1;
ngx_atomic_t         *ngx_connection_counter = &connection_counter;


ngx_atomic_t         *ngx_accept_mutex_ptr;
ngx_shmtx_t           ngx_accept_mutex;
ngx_uint_t            ngx_use_accept_mutex;
ngx_uint_t            ngx_accept_events;
ngx_uint_t            ngx_accept_mutex_held;
ngx_msec_t            ngx_accept_mutex_delay;
ngx_int_t             ngx_accept_disabled;


#if (NGX_STAT_STUB)

ngx_atomic_t   ngx_stat_accepted0;
ngx_atomic_t  *ngx_stat_accepted = &ngx_stat_accepted0;
ngx_atomic_t   ngx_stat_handled0;
ngx_atomic_t  *ngx_stat_handled = &ngx_stat_handled0;
ngx_atomic_t   ngx_stat_requests0;
ngx_atomic_t  *ngx_stat_requests = &ngx_stat_requests0;
ngx_atomic_t   ngx_stat_active0;
ngx_atomic_t  *ngx_stat_active = &ngx_stat_active0;
ngx_atomic_t   ngx_stat_reading0;
ngx_atomic_t  *ngx_stat_reading = &ngx_stat_reading0;
ngx_atomic_t   ngx_stat_writing0;
ngx_atomic_t  *ngx_stat_writing = &ngx_stat_writing0;
ngx_atomic_t   ngx_stat_waiting0;
ngx_atomic_t  *ngx_stat_waiting = &ngx_stat_waiting0;

#endif



static ngx_command_t  ngx_events_commands[] = {

    {   ngx_string("events"),
        NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
        ngx_events_block,
        0,
        0,
        NULL
    },

    ngx_null_command
};


static ngx_core_module_t  ngx_events_module_ctx = {
    ngx_string("events"),
    NULL,
    ngx_event_init_conf
};


ngx_module_t  ngx_events_module = {  // lgx_mark  解析和管理时间模块配置项
    NGX_MODULE_V1,
    &ngx_events_module_ctx,                /* module context */
    ngx_events_commands,                   /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_str_t  event_core_name = ngx_string("event_core");


static ngx_command_t  ngx_event_core_commands[] = {

    {   ngx_string("worker_connections"),  //  lgx_mark   一个worker进程的最大TCP连接数
        NGX_EVENT_CONF|NGX_CONF_TAKE1,
        ngx_event_connections,
        0,
        0,
        NULL
    },

    {   ngx_string("connections"),
        NGX_EVENT_CONF|NGX_CONF_TAKE1,
        ngx_event_connections,
        0,
        0,
        NULL
    },

    {   ngx_string("use"),      /*  lgx_mark  确定选择哪一个事件模块作为事件驱动机制 */
        NGX_EVENT_CONF|NGX_CONF_TAKE1,
        ngx_event_use,
        0,
        0,
        NULL
    },

    {   ngx_string("multi_accept"),   /*  lgx_mark  当有连接事件时，尽可能多地建立连接 */
        NGX_EVENT_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        0,
        offsetof(ngx_event_conf_t, multi_accept),
        NULL
    },

    {   ngx_string("accept_mutex"),    /*  lgx_mark  确定是否使用负载均衡锁 */
        NGX_EVENT_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        0,
        offsetof(ngx_event_conf_t, accept_mutex),
        NULL
    },

    {   ngx_string("accept_mutex_delay"),   /*  lgx_mark  启用负载均衡锁后，延迟accept_mutex_delay毫秒再处理新连接事件 */
        NGX_EVENT_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        0,
        offsetof(ngx_event_conf_t, accept_mutex_delay),
        NULL
    },

    {   ngx_string("debug_connection"),  /*  lgx_mark  对指定IP的TCP连接打印debug级别的调试日志 */
        NGX_EVENT_CONF|NGX_CONF_TAKE1,
        ngx_event_debug_connection,
        0,
        0,
        NULL
    },

    ngx_null_command
};


ngx_event_module_t  ngx_event_core_module_ctx = {

    &event_core_name,
    ngx_event_core_create_conf,            /* create configuration */
    ngx_event_core_init_conf,              /* init configuration */

    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};


ngx_module_t  ngx_event_core_module = {  // lgx_mark 创建连接池; 决定使用哪种事件驱动机制; 初始化将要使用的事件模块. 管理更下层的事件驱动模块.
    NGX_MODULE_V1,
    &ngx_event_core_module_ctx,            /* module context */
    ngx_event_core_commands,               /* module directives */
    NGX_EVENT_MODULE,                      /* module type */
    NULL,                                  /* init master */
    ngx_event_module_init,                 /* init module  fork出worker进程之前调用*/
    ngx_event_process_init,                /* init process fork出worker进程之后调用*/
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_process_events_and_timers(ngx_cycle_t *cycle)  // Nginx事件处理的入口函数
{
    ngx_uint_t  flags;
    ngx_msec_t  timer, delta;

    if (ngx_timer_resolution)
    {
        timer = NGX_TIMER_INFINITE;
        flags = 0;

    }
    else
    {
        timer = ngx_event_find_timer(); // lgx_mark 从rbtree中找出最小时间，并计算与当前时间之差。查找timer，返回距离最近超时事件的时间，若有事件超时则返回0
        flags = NGX_UPDATE_TIME;

#if (NGX_THREADS)

        if (timer == NGX_TIMER_INFINITE || timer > 500)
        {
            timer = 500;
        }

#endif
    }

    if (ngx_use_accept_mutex)   // accept mutex的作用就是避免惊群，同时实现负载均衡。 
    {

        /* 
         ngx_accept_disabled变量在ngx_event_accept函数中计算。 
         如果ngx_accept_disabled大于了0，就表示该进程接受的 
         连接过多，因此就放弃一次争抢accept mutex的机会，同时将 
         自己减1。然后，继续处理已有连接上的事件。Nginx就借用 
         此变量实现了进程关于连接的基本负载均衡。 
         */ 
         
        if (ngx_accept_disabled > 0)
        {
            ngx_accept_disabled--;

        }
        else
        {
            /* 
              尝试锁accept mutex，只有成功获取锁的进程，才会将listen 
              套接字放入epoll中。因此，这就保证了只有一个进程拥有 
              监听套接口，故所有进程阻塞在epoll_wait时，不会出现惊群现象。 
            */  

            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR)
            {
                return;
            }

            if (ngx_accept_mutex_held)
            {
                 /* 
                  获取锁的进程，将添加一个NGX_POST_EVENTS标志， 
                  此标志的作用是将所有产生的事件放入一个队列中， 
                  等释放锁后，再慢慢来处理事件。因为，处理事件可能 
                  会很耗时，如果不先释放锁再处理的话，该进程就长 
                  时间霸占了锁，导致其他进程无法获取锁，这样accept 
                  的效率就低了。 
                */  

                flags |= NGX_POST_EVENTS;

            }
            else
            {
                /* 
                  没有获得锁的进程，当然不需要NGX_POST_EVENTS标志了。 
                  但需要设置最长延迟多久，再次去争抢锁。 
                */

                if (timer == NGX_TIMER_INFINITE
                        || timer > ngx_accept_mutex_delay)
                {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }

    delta = ngx_current_msec;

    /* 
      lgx_mark
      epoll开始wait事件,ngx_process_events的具体实现是对应到 
      epoll模块中的ngx_epoll_process_events函数。单独分析epoll 
      模块的时候，再具体看看。 
    */  

    (void) ngx_process_events(cycle, timer, flags); 

    delta = ngx_current_msec - delta;  // 统计本次wait事件的耗时

    if (ngx_posted_accept_events)
    {
        ngx_event_process_posted(cycle, &ngx_posted_accept_events);  // 这里完成对队列中的accept事件的处理，实际就是调用ngx_event_accept函数来获取一个新的连接，然后放入epoll中。
    }

    if (ngx_accept_mutex_held)  // 所有accept事件处理完成，如果拥有锁的话，就赶紧释放了。其他进程还等着抢了。
    {
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    /*delta是上文对epoll wait事件的耗时统计，存在毫秒级的耗时 
      就对所有事件的timer进行检查，如果time out就从timer rbtree中， 
      删除到期的timer，同时调用相应事件的handler函数完成处理。*/  

    if (delta)
    {
        ngx_event_expire_timers();
    }

    /*
      处理普通事件（连接上获得的读写事件）队列上的所有事件， 
      因为每个事件都有自己的handler方法，该怎么处理事件就 
      依赖于事件的具体handler了。 
    */  

    if (ngx_posted_events)
    {
        if (ngx_threaded)
        {
            ngx_wakeup_worker_thread(cycle);

        }
        else
        {
            ngx_event_process_posted(cycle, &ngx_posted_events);
        }
    }
}


ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags) // lgx_mark  读事件添加到事件驱动模块
{
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {

        /* kqueue, epoll */

        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT)
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {

        /* select, poll, /dev/poll */

        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT)
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->active && (rev->ready || (flags & NGX_CLOSE_EVENT))) {
            if (ngx_del_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT | flags)
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {

        /* event ports */

        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->oneshot && !rev->ready) {
            if (ngx_del_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* aio, iocp, rtsig */

    return NGX_OK;
}


ngx_int_t
ngx_handle_write_event(ngx_event_t *wev, size_t lowat) // lgx_mark 写事件添加到时间驱动模块
{
    ngx_connection_t  *c;

    if (lowat) 
    {
        c = wev->data;

        if (ngx_send_lowat(c, lowat) == NGX_ERROR) 
        {
            return NGX_ERROR;
        }
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) 
    {

        /* kqueue, epoll */

        if (!wev->active && !wev->ready) 
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT,
                              NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0))
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } 
    else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) 
    {

        /* select, poll, /dev/poll */

        if (!wev->active && !wev->ready) 
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->active && wev->ready) 
        {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                    == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

    } 
    else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) 
    {

        /* event ports */

        if (!wev->active && !wev->ready)
        {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->oneshot && wev->ready) 
        {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) 
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* aio, iocp, rtsig */

    return NGX_OK;
}


static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{
    if (ngx_get_conf(cycle->conf_ctx, ngx_events_module) == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "no \"events\" section in configuration");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_event_module_init(ngx_cycle_t *cycle)
{
    void              ***cf;
    u_char              *shared;
    size_t               size, cl;
    ngx_shm_t            shm;
    ngx_time_t          *tp;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;

    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);
    ecf = (*cf)[ngx_event_core_module.ctx_index];

    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER)
    {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "using the \"%s\" event method", ecf->name);
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_timer_resolution = ccf->timer_resolution;

#if !(NGX_WIN32)
    {
        ngx_int_t      limit;
        struct rlimit  rlmt;

        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) failed, ignored");

        } else {
            if (ecf->connections > (ngx_uint_t) rlmt.rlim_cur
                    && (ccf->rlimit_nofile == NGX_CONF_UNSET
                        || ecf->connections > (ngx_uint_t) ccf->rlimit_nofile))
            {
                limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ?
                        (ngx_int_t) rlmt.rlim_cur : ccf->rlimit_nofile;

                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                              "%ui worker_connections exceed "
                              "open file resource limit: %i",
                              ecf->connections, limit);
            }
        }
    }
#endif /* !(NGX_WIN32) */


    if (ccf->master == 0) {
        return NGX_OK;
    }

    if (ngx_accept_mutex_ptr) {
        return NGX_OK;
    }


    /* cl should be equal to or greater than cache line size */

    cl = 128;

    size = cl            /* ngx_accept_mutex */
           + cl          /* ngx_connection_counter */
           + cl;         /* ngx_temp_number */

#if (NGX_STAT_STUB)

    size += cl           /* ngx_stat_accepted */
            + cl          /* ngx_stat_handled */
            + cl          /* ngx_stat_requests */
            + cl          /* ngx_stat_active */
            + cl          /* ngx_stat_reading */
            + cl          /* ngx_stat_writing */
            + cl;         /* ngx_stat_waiting */

#endif

    shm.size = size;
    shm.name.len = sizeof("nginx_shared_zone");
    shm.name.data = (u_char *) "nginx_shared_zone";
    shm.log = cycle->log;

    if (ngx_shm_alloc(&shm) != NGX_OK) {
        return NGX_ERROR;
    }

    shared = shm.addr;

    ngx_accept_mutex_ptr = (ngx_atomic_t *) shared;
    ngx_accept_mutex.spin = (ngx_uint_t) -1;

    if (ngx_shmtx_create(&ngx_accept_mutex, (ngx_shmtx_sh_t *) shared,
                         cycle->lock_file.data)
            != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_connection_counter = (ngx_atomic_t *) (shared + 1 * cl);

    (void) ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "counter: %p, %d",
                   ngx_connection_counter, *ngx_connection_counter);

    ngx_temp_number = (ngx_atomic_t *) (shared + 2 * cl);

    tp = ngx_timeofday();

    ngx_random_number = (tp->msec << 16) + ngx_pid;

#if (NGX_STAT_STUB)

    ngx_stat_accepted = (ngx_atomic_t *) (shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *) (shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *) (shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *) (shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *) (shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *) (shared + 8 * cl);
    ngx_stat_waiting = (ngx_atomic_t *) (shared + 9 * cl);

#endif

    return NGX_OK;
}


#if !(NGX_WIN32)

static void
ngx_timer_signal_handler(int signo)
{
    ngx_event_timer_alarm = 1;

#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif


static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{
    ngx_uint_t           m, i;
    ngx_event_t         *rev, *wev;
    ngx_listening_t     *ls;
    ngx_connection_t    *c, *next, *old;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;
    ngx_event_module_t  *module;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);

    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex) {
        ngx_use_accept_mutex = 1;  // lgx_mark 启用负载均衡锁
        ngx_accept_mutex_held = 0;
        ngx_accept_mutex_delay = ecf->accept_mutex_delay;

    }
    else
    {
        ngx_use_accept_mutex = 0;
    }

#if (NGX_WIN32)

    /*
     * disable accept mutex on win32 as it may cause deadlock if
     * grabbed by a process which can't accept connections
     */

    ngx_use_accept_mutex = 0;

#endif

#if (NGX_THREADS)
    ngx_posted_events_mutex = ngx_mutex_init(cycle->log, 0);
    if (ngx_posted_events_mutex == NULL) {
        return NGX_ERROR;
    }
#endif

    if (ngx_event_timer_init(cycle->log) == NGX_ERROR) {  // 初始化rbtree 时间计时器
        return NGX_ERROR;
    }

    for (m = 0; ngx_modules[m]; m++)
    {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE)
        {
            continue;
        }

        if (ngx_modules[m]->ctx_index != ecf->use)
        {
            continue;
        }

        module = ngx_modules[m]->ctx;

        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK)  // lgx_mark 事件模块初始化
        {
            /* fatal */
            exit(2);
        }

        break;
    }

#if !(NGX_WIN32)

    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT))
    {
        struct sigaction  sa;
        struct itimerval  itv;

        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);

        if (sigaction(SIGALRM, &sa, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) failed");
            return NGX_ERROR;
        }

        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000 ) * 1000;

        if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setitimer() failed");
        }
    }

    if (ngx_event_flags & NGX_USE_FD_EVENT) {
        struct rlimit  rlmt;

        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) failed");
            return NGX_ERROR;
        }

        cycle->files_n = (ngx_uint_t) rlmt.rlim_cur;

        cycle->files = ngx_calloc(sizeof(ngx_connection_t *) * cycle->files_n,
                                  cycle->log);
        if (cycle->files == NULL) {
            return NGX_ERROR;
        }
    }

#endif

    cycle->connections =
        ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);  // lgx_mark 分配连接缓冲池

    if (cycle->connections == NULL) {
        return NGX_ERROR;
    }

    c = cycle->connections;

    cycle->read_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,   // lgx_mark 预分配读事件缓冲池
                                   cycle->log);
    if (cycle->read_events == NULL) {
        return NGX_ERROR;
    }

    rev = cycle->read_events;
    for (i = 0; i < cycle->connection_n; i++)
    {
        rev[i].closed = 1;
        rev[i].instance = 1;

#if (NGX_THREADS)
        rev[i].lock = &c[i].lock;
        rev[i].own_lock = &c[i].lock;
#endif

    }

    cycle->write_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,
                                    cycle->log);

	if (cycle->write_events == NULL)
    {
        return NGX_ERROR;
    }

    wev = cycle->write_events;
    for (i = 0; i < cycle->connection_n; i++) 
	{
        wev[i].closed = 1;
		时间 
#if (NGX_THREADS)
        wev[i].lock = &c[i].lock;
        wev[i].own_lock = &c[i].lock;
#endif
    }

    i = cycle->connection_n;
    next = NULL;

    do {
        i--;

        c[i].data = next;
        c[i].read = &cycle->read_events[i];   // lgx_mark 一个连接一个读事件结构对应读事件数组的一个元素,一个连接对应一个读事件和一个写事件
        c[i].write = &cycle->write_events[i];
        c[i].fd = (ngx_socket_t) -1;

        next = &c[i];

#if (NGX_THREADS)
        c[i].lock = 0;
#endif

    } while (i);

    cycle->free_connections = next;
    cycle->free_connection_n = cycle->connection_n;

    /* for each listening socket */

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {

        c = ngx_get_connection(ls[i].fd, cycle->log); 

        if (c == NULL) {
            return NGX_ERROR;
        }

        c->log = &ls[i].log;

        c->listening = &ls[i];
        ls[i].connection = c;

        rev = c->read; // 连接对应的读事件

        rev->log = c->log;
        rev->accept = 1;

#if (NGX_HAVE_DEFERRED_ACCEPT)
        rev->deferred_accept = ls[i].deferred_accept;
#endif

        if (!(ngx_event_flags & NGX_USE_IOCP_EVENT)) {
            if (ls[i].previous) {

                /*
                 * delete the old accept events that were bound to
                 * the old cycle read events array
                 */

                old = ls[i].previous->connection;

                if (ngx_del_event(old->read, NGX_READ_EVENT, NGX_CLOSE_EVENT)
                        == NGX_ERROR)
                {
                    return NGX_ERROR;
                }

                old->fd = (ngx_socket_t) -1;
            }
        }

        rev->handler = ngx_event_accept;  // lgx_mark 设置读事件的处理方法 accept

        if (ngx_use_accept_mutex) {
            continue;
        }

        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) { // 将监听连接的读事件添加到epoll模块
                return NGX_ERROR;
            }
        }

    }

    return NGX_OK;
}


ngx_int_t
ngx_send_lowat(ngx_connection_t *c, size_t lowat)
{
    int  sndlowat;

#if (NGX_HAVE_LOWAT_EVENT)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        c->write->available = lowat;
        return NGX_OK;
    }

#endif

    if (lowat == 0 || c->sndlowat) {
        return NGX_OK;
    }

    sndlowat = (int) lowat;

    if (setsockopt(c->fd, SOL_SOCKET, SO_SNDLOWAT,
                   (const void *) &sndlowat, sizeof(int))
            == -1)
    {
        ngx_connection_error(c, ngx_socket_errno,
                             "setsockopt(SO_SNDLOWAT) failed");
        return NGX_ERROR;
    }

    c->sndlowat = 1;

    return NGX_OK;
}


static char *
ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_event_module_t   *m;

    if (*(void **) conf) {
        return "is duplicate";
    }

    /* count the number of the event modules and set up their indices */

    ngx_event_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }

        ngx_modules[i]->ctx_index = ngx_event_max_module++;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(void **) conf = ctx;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }

        m = ngx_modules[i]->ctx;

        if (m->create_conf) {
            (*ctx)[ngx_modules[i]->ctx_index] = m->create_conf(cf->cycle);
            if ((*ctx)[ngx_modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK)
        return rv;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }

        m = ngx_modules[i]->ctx;

        if (m->init_conf) {
            rv = m->init_conf(cf->cycle, (*ctx)[ngx_modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

    ngx_str_t  *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (ngx_strcmp(cmd->name.data, "connections") == 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"connections\" directive is deprecated, "
                           "use the \"worker_connections\" directive instead");
    }

    value = cf->args->elts;
    ecf->connections = ngx_atoi(value[1].data, value[1].len);
    if (ecf->connections == (ngx_uint_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number \"%V\"", &value[1]);

        return NGX_CONF_ERROR;
    }

    cf->cycle->connection_n = ecf->connections;

    return NGX_CONF_OK;
}


static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             m;
    ngx_str_t            *value;
    ngx_event_conf_t     *old_ecf;
    ngx_event_module_t   *module;

    if (ecf->use != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (cf->cycle->old_cycle->conf_ctx) {
        old_ecf = ngx_event_get_conf(cf->cycle->old_cycle->conf_ctx,
                                     ngx_event_core_module);
    } else {
        old_ecf = NULL;
    }


    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {
                ecf->use = ngx_modules[m]->ctx_index;
                ecf->name = module->name->data;

                if (ngx_process == NGX_PROCESS_SINGLE
                        && old_ecf
                        && old_ecf->use != ecf->use)
                {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "when the server runs without a master process "
                                       "the \"%V\" event type must be the same as "
                                       "in previous configuration - \"%s\" "
                                       "and it cannot be changed on the fly, "
                                       "to change it you need to stop server "
                                       "and start it again",
                                       &value[1], old_ecf->name);

                    return NGX_CONF_ERROR;
                }

                return NGX_CONF_OK;
            }
        }
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid event type \"%V\"", &value[1]);

    return NGX_CONF_ERROR;
}


static char *
ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_DEBUG)
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             rc;
    ngx_str_t            *value;
    ngx_url_t             u;
    ngx_cidr_t            c, *cidr;
    ngx_uint_t            i;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    value = cf->args->elts;

#if (NGX_HAVE_UNIX_DOMAIN)

    if (ngx_strcmp(value[1].data, "unix:") == 0) {
        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL) {
            return NGX_CONF_ERROR;
        }

        cidr->family = AF_UNIX;
        return NGX_CONF_OK;
    }

#endif

    rc = ngx_ptocidr(&value[1], &c);

    if (rc != NGX_ERROR) {
        if (rc == NGX_DONE) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "low address bits of %V are meaningless",
                               &value[1]);
        }

        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL) {
            return NGX_CONF_ERROR;
        }

        *cidr = c;

        return NGX_CONF_OK;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));
    u.host = value[1];

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "%s in debug_connection \"%V\"",
                               u.err, &u.host);
        }

        return NGX_CONF_ERROR;
    }

    cidr = ngx_array_push_n(&ecf->debug_connection, u.naddrs);
    if (cidr == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(cidr, u.naddrs * sizeof(ngx_cidr_t));

    for (i = 0; i < u.naddrs; i++) {
        cidr[i].family = u.addrs[i].sockaddr->sa_family;

        switch (cidr[i].family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            sin6 = (struct sockaddr_in6 *) u.addrs[i].sockaddr;
            cidr[i].u.in6.addr = sin6->sin6_addr;
            ngx_memset(cidr[i].u.in6.mask.s6_addr, 0xff, 16);
            break;
#endif

        default: /* AF_INET */
            sin = (struct sockaddr_in *) u.addrs[i].sockaddr;
            cidr[i].u.in.addr = sin->sin_addr.s_addr;
            cidr[i].u.in.mask = 0xffffffff;
            break;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"debug_connection\" is ignored, you need to rebuild "
                       "nginx using --with-debug option to enable it");

#endif

    return NGX_CONF_OK;
}


static void *
ngx_event_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_event_conf_t  *ecf;

    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL) {
        return NULL;
    }

    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *) NGX_CONF_UNSET;

#if (NGX_DEBUG)

    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4,
                       sizeof(ngx_cidr_t)) == NGX_ERROR)
    {
        return NULL;
    }

#endif

    return ecf;
}


static char *
ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    int                  fd;
#endif
#if (NGX_HAVE_RTSIG)
    ngx_uint_t           rtsig;
    ngx_core_conf_t     *ccf;
#endif
    ngx_int_t            i;
    ngx_module_t        *module;
    ngx_event_module_t  *event_module;

    module = NULL;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)

    fd = epoll_create(100);

    if (fd != -1)
    {
        (void) close(fd);
        module = &ngx_epoll_module;

    }
    else if (ngx_errno != NGX_ENOSYS)
    {
        module = &ngx_epoll_module;
    }

#endif

#if (NGX_HAVE_RTSIG)

    if (module == NULL)
    {
        module = &ngx_rtsig_module;
        rtsig = 1;
    }
    else
    {
        rtsig = 0;
    }

#endif

#if (NGX_HAVE_DEVPOLL)

    module = &ngx_devpoll_module;

#endif

#if (NGX_HAVE_KQUEUE)

    module = &ngx_kqueue_module;

#endif

#if (NGX_HAVE_SELECT)

    if (module == NULL)
    {
        module = &ngx_select_module;
    }

#endif

    if (module == NULL)
    {
        for (i = 0; ngx_modules[i]; i++)
        {

            if (ngx_modules[i]->type != NGX_EVENT_MODULE)
            {
                continue;
            }

            event_module = ngx_modules[i]->ctx;

            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0)
            {
                continue;
            }

            module = ngx_modules[i];

            break;
        }

    }

    if (module == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }

    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);
    cycle->connection_n = ecf->connections;

    ngx_conf_init_uint_value(ecf->use, module->ctx_index);

    event_module = module->ctx;
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 1);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);


#if (NGX_HAVE_RTSIG)

    if (!rtsig)
    {
        return NGX_CONF_OK;
    }

    if (ecf->accept_mutex)
    {
        return NGX_CONF_OK;
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ccf->worker_processes == 0)
    {
        return NGX_CONF_OK;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                  "the \"rtsig\" method requires \"accept_mutex\" to be on");

    return NGX_CONF_ERROR;

#else

    return NGX_CONF_OK;

#endif



}



