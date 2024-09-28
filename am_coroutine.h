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
#define AM_CO_MAX_STACKSIZE (16*1024)
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


//协程在执行I/O操作时的状态管理
typedef enum {
	AM_COROUTINE_EV_READ,
	AM_COROUTINE_EV_WRITE
} am_coroutine_event;


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
	//系统的内存页的大小
	int page_size;
	//epoll文件描述符
	int epoll_fd;
	int eventfd;
	struct epoll_event eventlist[AM_CO_MAX_EVENTS];
	//当前事件列表中的事件数量
	int nevents;
	//新的就绪事件数量
	int num_new_events;
	//一个互斥锁，用于保护就绪事件计数和事件列表
	pthread_mutex_t defer_mutex;

	//协程队列，存储就绪的协程
	am_coroutine_queue ready;
	//协程队列，存储延迟执行的协程
	am_coroutine_queue defer;
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
	//上一个协程的栈的大小
	size_t last_stack_size;
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
	//协程主体的函数名
	char funcname[64];
	//等待其他协程结束的协程列表
	struct _am_coroutine* co_join;
	//用于协程间的退出通知
	//当一个协程完成时，它可以设置这个指针，以便其他协程可以知道它已经结束
	void** co_exit_ptr;
	//协程的栈指针，指向协程的栈空间
	void* stack;
	//协程的EBP（栈底指针）寄存器的值，用于协程的上下文恢复
	void* ebp;
	//一个计数器，用于跟踪协程的操作次数
	uint32_t ops;
	//协程下一次需要唤醒的时间，以微秒为单位
	uint64_t sleep_usecs;

	//用于在调度器的睡眠队列中排序协程
	RB_ENTRY(_am_coroutine) sleep_node;
	//用于在调度器的等待队列中排序协程
	RB_ENTRY(_am_coroutine) wait_node;
	//用于在调度器的忙队列中排序协程
	LIST_ENTRY(_am_coroutine) busy_next; //
	//用于在调度器的就绪队列中排序协程
	TAILQ_ENTRY(_am_coroutine) ready_next;
	//用于在调度器的延迟队列中排序协程
	TAILQ_ENTRY(_am_coroutine) defer_next;
	//用于在调度器的条件队列中排序协程
	TAILQ_ENTRY(_am_coroutine) cond_next;
	//用于在调度器的I/O队列中排序协程
	TAILQ_ENTRY(_am_coroutine) io_next;
	//用于在调度器的计算队列中排序协程
	TAILQ_ENTRY(_am_coroutine) compute_next;

	//一个结构体，用于存储与I/O操作相关的数据
	struct {
		//指向I/O操作的缓冲区的指针
		void *buf;
		//缓冲区的大小
		size_t nbytes;
		//与I/O操作相关的文件描述符
		int fd;
		//I/O操作的返回值
		int ret;
		//I/O操作的错误码
		int err;
	} io;
	
	
	//准备就绪的文件描述符的数量
	int ready_fds;
	//指向一个pollfd结构的数组，
	//其中每个结构体都包含了一个文件描述符和一个指向事件集合的指针
	struct pollfd *pfds;
	//pfds数组的长度
	nfds_t nfds;
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


int am_epoller_create(void);


void am_schedule_cancel_event(am_coroutine *co);
void am_schedule_sched_event(am_coroutine *co, int fd, am_coroutine_event e, uint64_t timeout);

void am_schedule_desched_sleepdown(am_coroutine *co);
void am_schedule_sched_sleepdown(am_coroutine *co, uint64_t msecs);

am_coroutine* am_schedule_desched_wait(int fd);
void am_schedule_sched_wait(am_coroutine *co, int fd, unsigned short events, uint64_t timeout);

int am_epoller_ev_register_trigger(void);
int am_epoller_wait(struct timespec t);
int am_coroutine_resume(am_coroutine *co);
void am_coroutine_free(am_coroutine *co);
int am_coroutine_create(am_coroutine **new_co, proc_coroutine func, void *arg);
void am_coroutine_yield(am_coroutine *co);

void am_coroutine_sleep(uint64_t msecs);

//兼容POSIX API
int am_socket(int domain, int type, int protocol);
int am_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t am_recv(int fd, void *buf, size_t len, int flags);
ssize_t am_send(int fd, const void *buf, size_t len, int flags);
int am_close(int fd);
int am_poll(struct pollfd *fds, nfds_t nfds, int timeout);


ssize_t am_sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t am_recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);


#endif