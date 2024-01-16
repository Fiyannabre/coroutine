#ifndef MY_COROUTINE_HEAD
#define MY_COROUTINE_HEAD

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>

#include <pthread.h>

#include <errno.h>
#include <assert.h>

#include "nty_queue.h"
#include "nty_tree.h"


#define MAX_EVENTS 1024 * 1024              //epoll最大事件数
#define STACK_SIZE 1024 * 8                 //每个协程保留上下文的栈大小 8kb

//链表 头尾指针
TAILQ_HEAD(coroutine_queue, _my_coroutine);

//树 根节点
RB_HEAD(_co_wait_tree, _my_coroutine);
RB_HEAD(_co_sleep_tree, _my_coroutine);

typedef struct coroutine_queue co_queue;

typedef struct _co_wait_tree co_wait_tree;
typedef struct _co_sleep_tree co_sleep_tree;

//状态
typedef enum {
	CO_NEW,
    CO_READY,
	CO_WAIT,        //wait event
    CO_SLEEP,       //wait time
    CO_END
} corotine_status;

//协程函数
typedef void (*co_func)(void*);

typedef struct _my_schedule my_schedule;

typedef struct _my_coroutine{
//init when created
    co_func func;                   //协程执行的函数
    void *args;                     //函数的参数

    ucontext_t ctx;                 //协程的上下文
    void *stack;                    //存储上下文的栈
    size_t stack_size;              //栈的大小
    
    int id;
    uint64_t sleep_time;

    my_schedule* sched;             //调度器
    corotine_status status;         //状态

    RB_ENTRY(_my_coroutine) sleep_node;     //
	RB_ENTRY(_my_coroutine) wait_node;

    TAILQ_ENTRY(_my_coroutine) ready_next;

//
    int fd;                         //

}my_coroutine;


struct _my_schedule{
    int spawned_coroutines;     //创建的协程数量

    ucontext_t ctx;             //resume存储主循环的上下文
    void *stack;                
	size_t stack_size;

    co_wait_tree wait_tree;
    co_sleep_tree sleep_tree;

    co_queue ready_queue;


    int epoll_fd;               //epoll文件描述符
    struct epoll_event eventlist[MAX_EVENTS]; //存储就绪事件的数组
    int nevents;                //就绪的事件数

    my_coroutine* current_co;

};

extern pthread_key_t global_sched_key;
static inline my_schedule *get_sched(void) {
    return (my_schedule *)pthread_getspecific(global_sched_key);
}

my_schedule* init_schedule();
void create_coroutine(co_func func, void *args);
void run(my_schedule*);
void resume(my_schedule*, my_coroutine*);
void yield(my_schedule*, my_coroutine*);


//hook
int socket(int domian, int type, int protocol);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t send(int fd, const void *buf, size_t len, int flags);
int close(int fd);


#endif