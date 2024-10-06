#ifndef __AM_COROUTINE_H__
#define __AM_COROUTINE_H__

#include "am_queue.h"
#include "am_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>
#include <sys/poll.h>

#include <errno.h>

#define AM_CO_MAX_EVENTS 	(1024*1024)
#define AM_CO_MAX_STACKSIZE (4*1024)
#define BIT(x)				(1<<(x))
#define CLEARBIT(x)			~(1<<(x))

typedef void (*proc_coroutine)(void*);

//协程的状态
//模仿进程的五状态模型
//新建、就绪、阻塞等待(等待读、等待写)、运行、终止
typedef enum{
	//等待读
	AM_COROUTINE_STATUS_WAIT_READ,
	//等待写
	AM_COROUTINE_STATUS_WAIT_WRITE,
	//新建
	AM_COROUTINE_STATUS_NEW,
	//就绪
	AM_COROUTINE_STATUS_READY,
	//退出
	AM_COROUTINE_STATUS_EXITED,
	//运行
	AM_COROUTINE_STATUS_BUSY,
	
	//睡眠
	AM_COROUTINE_STATUS_SLEEPING
}am_coroutine_status;



LIST_HEAD(_am_coroutine_link, _am_coroutine);
TAILQ_HEAD(_am_coroutine_queue, _am_coroutine);

RB_HEAD(_am_coroutine_rbtree_sleep, _am_coroutine);
RB_HEAD(_am_coroutine_rbtree_wait, _am_coroutine);



typedef struct _am_coroutine_link am_coroutine_link;
typedef struct _am_coroutine_queue am_coroutine_queue;

typedef struct _am_coroutine_rbtree_sleep am_coroutine_rbtree_sleep;
typedef struct _am_coroutine_rbtree_wait am_coroutine_rbtree_wait;

//CPU上下文
typedef struct _am_cpu_ctx{
	//当前栈帧的栈顶
	void* rsp;
	//栈底指针
	void* rbp;
	//下一条指令的地址
	void* rip;
	//第一个参数
	void* rdi;
	//第二个参数
	void* rsi;
	//数据寄存器
	void* rbx;
	void* r12;
	void* r13;
	void* r14;
	void* r15;
}am_cpu_ctx;


//调度器
typedef struct _am_schedule{
	//调度器的创建时间
	uint64_t birth;
	//CPU上下文
	am_cpu_ctx ctx;
	//栈指针，指向调度器私有栈的栈底
	void* stack;
	//栈的大小
	size_t stack_size;
	//已创建的协程数量
	int spawned_coroutines;
	//默认超时时间，单位微秒
	uint64_t default_timeout;
	//当前正在运行的协程
	struct _am_coroutine* curr_thread;

	//epoll文件描述符
	int epoll_fd;
	int eventfd;
	struct epoll_event eventlist[AM_CO_MAX_EVENTS];
	//当前事件列表中的事件数量
	int nevents;
	//新的就绪事件数量
	int num_new_events;

	//协程队列，存储就绪的协程
	am_coroutine_queue ready;

	//忙碌链表，存储忙碌的协程
	am_coroutine_link busy;
	
	//睡眠红黑树，存储睡眠的协程
	am_coroutine_rbtree_sleep sleeping;
	//等待红黑树，存储等待的协程
	am_coroutine_rbtree_wait waiting;
}am_schedule;


//协程
typedef struct _am_coroutine{
	//CPU上下文
	am_cpu_ctx ctx;
	//协程要执行的函数
	proc_coroutine func;
	//上述函数参数
	void* arg;
	//协程的私有数据
	void* data;
	//协程的栈的大小
	size_t stack_size;

	//协程的状态
	//am_coroutine_status status;
	uint32_t status;
	//调度器
	am_schedule* sched;
	//协程的创建时间
	uint64_t birth;
	//协程的唯一标识符
	uint64_t id;
	//文件描述符
	int fd;
	//文件描述符的事件类型
	unsigned short events;


	//协程的栈指针，指向协程的栈空间
	void* stack;

	//协程下一次需要唤醒的时间，以微秒为单位
	uint64_t sleep_usecs;

	//用于在调度器的睡眠队列中排序协程
	RB_ENTRY(_am_coroutine) sleep_node;
	//用于在调度器的等待队列中排序协程
	RB_ENTRY(_am_coroutine) wait_node;

	//用于在调度器的就绪队列中排序协程
	TAILQ_ENTRY(_am_coroutine) ready_next;

}am_coroutine;


//线程私有数据键
extern pthread_key_t global_sched_key;

static inline am_schedule* am_coroutine_get_sched(void){
	return (am_schedule*)pthread_getspecific(global_sched_key);
}

static inline uint64_t am_coroutine_diff_usecs(uint64_t t1,uint64_t t2){
	return t2-t1;
}

//获取当前时间
static inline uint64_t am_coroutine_usec_now(void){
	struct timeval t1 = {0,0};//{秒,微秒}
	gettimeofday(&t1,NULL);
	//返回总的微妙数
	return t1.tv_sec*1000000 + t1.tv_usec;
}




void am_schedule_desched_sleepdown(am_coroutine *co);
void am_schedule_sched_sleepdown(am_coroutine *co, uint64_t msecs);

am_coroutine* am_schedule_desched_wait(int fd);
void am_schedule_sched_wait(am_coroutine *co, int fd, unsigned short events, uint64_t timeout);

int am_epoller_ev_register_trigger(void);
int am_coroutine_resume(am_coroutine *co);
void am_coroutine_free(am_coroutine *co);
int am_coroutine_create(am_coroutine **new_co, proc_coroutine func, void *arg);
void am_coroutine_yield(am_coroutine *co);


//兼容POSIX API
int am_socket(int domain, int type, int protocol);
int am_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t am_recv(int fd, void *buf, size_t len, int flags);
ssize_t am_send(int fd, const void *buf, size_t len, int flags);
int am_close(int fd);


#endif