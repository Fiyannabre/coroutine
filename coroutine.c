#include "coroutine.h"
#include <sys/eventfd.h>

pthread_key_t global_sched_key;
static pthread_once_t sched_key_once = PTHREAD_ONCE_INIT;

//定义cmp
static inline int wait_cmp(my_coroutine *co1, my_coroutine *co2);
static inline int sleep_cmp(my_coroutine *co1, my_coroutine *co2);

RB_GENERATE(_co_wait_tree, _my_coroutine, sleep_node, wait_cmp);
RB_GENERATE(_co_sleep_tree, _my_coroutine, wait_node, sleep_cmp);

static inline int sleep_cmp(my_coroutine *co1, my_coroutine *co2) {
	if (co1->sleep_time < co2->sleep_time) {
		return -1;
	}
	if (co1->sleep_time == co2->sleep_time) {
		return 0;
	}
	return 1;
}

static inline int wait_cmp(my_coroutine *co1, my_coroutine *co2) {
	if (co1->fd < co2->fd) return -1;
	else if (co1->fd == co2->fd) return 0;
	else return 1;
}

static void coroutine_sched_key_destructor(void *data) {
	free(data);
}

static void coroutine_sched_key_creator(void) {
	assert(pthread_key_create(&global_sched_key, coroutine_sched_key_destructor) == 0);
	assert(pthread_setspecific(global_sched_key, NULL) == 0);
	
	return;
}

my_schedule* init_schedule(){
    my_schedule* schedule = (my_schedule*)malloc(sizeof(my_schedule));
    assert(pthread_once(&sched_key_once, coroutine_sched_key_creator) == 0);
    assert(pthread_setspecific(global_sched_key, schedule) == 0);
    //创建epoll对象
    schedule->epoll_fd = epoll_create(1);

    schedule->spawned_coroutines = 0;

    //设置上下文
    schedule->stack_size = STACK_SIZE;
    if(posix_memalign(&schedule->stack, getpagesize(), schedule->stack_size) != 0){
        free(schedule);
        return NULL;
    }
    schedule->ctx.uc_stack.ss_sp = schedule->stack;
    schedule->ctx.uc_stack.ss_size = schedule->stack_size;
    schedule->ctx.uc_link = NULL;

    RB_INIT(&schedule->sleep_tree);
    RB_INIT(&schedule->wait_tree);

    TAILQ_INIT(&schedule->ready_queue);

    return schedule;
}

void create_coroutine(co_func func, void *args){
    assert(pthread_once(&sched_key_once, coroutine_sched_key_creator) == 0);

    my_schedule* schedule = get_sched();
    assert(schedule != NULL);

    my_coroutine* co = (my_coroutine*) malloc(sizeof(my_coroutine));

    co->sched = schedule;
    co->id = schedule->spawned_coroutines++;
    co->status = CO_READY;
    co->func = func;
    co->args = args;

    posix_memalign(&co->stack, getpagesize(), STACK_SIZE);
    co->stack_size = STACK_SIZE;
    
    //设置上下文
    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp = co->stack;
	co->ctx.uc_stack.ss_size = co->stack_size;
	co->ctx.uc_link = &schedule->ctx;
    makecontext(&co->ctx, (void (*)(void)) co->func, 1, (void*)co->args);

    TAILQ_INSERT_TAIL(&schedule->ready_queue, co, ready_next);

    return;
}

inline static int is_end(my_schedule* schedule){
    return TAILQ_EMPTY(&schedule->ready_queue)&&
        RB_EMPTY(&schedule->wait_tree)&&
        RB_EMPTY(&schedule->sleep_tree);
}

void run(my_schedule* schedule){
    if(schedule == NULL) return;
    while(!is_end(schedule)){
        //ready queue
        while(!TAILQ_EMPTY(&schedule->ready_queue)){
            my_coroutine* co = TAILQ_FIRST(&schedule->ready_queue);
            TAILQ_REMOVE(&schedule->ready_queue, co, ready_next);
            resume(schedule, co);
            RB_INSERT(_co_wait_tree, &schedule->wait_tree, co);
        }
        //wait tree
        int ready_n = epoll_wait(schedule->epoll_fd, schedule->eventlist, MAX_EVENTS, 0);
        my_coroutine temp;
        while(ready_n--){
            temp.fd = schedule->eventlist[ready_n].data.fd;
            my_coroutine* co = RB_FIND(_co_wait_tree, &schedule->wait_tree, &temp);
            TAILQ_INSERT_TAIL(&schedule->ready_queue, co, ready_next);
            co->status = CO_READY;
        }
    }
}

void resume(my_schedule* schedule, my_coroutine* co){
    co->status = CO_READY;
    schedule->current_co = co;
    swapcontext(&schedule->ctx, &co->ctx);
}

void yield(my_schedule* schedule, my_coroutine* co){
    co->status = CO_WAIT;
    schedule->current_co = NULL;    
    swapcontext(&co->ctx, &schedule->ctx);
}