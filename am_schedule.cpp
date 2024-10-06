#include "am_coroutine.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>


static inline int am_coroutine_sleep_cmp(am_coroutine* co1,am_coroutine* co2){
	if (co1->sleep_usecs < co2->sleep_usecs) {
		return -1;//返回-1，表示co1应该排在co2之前
	}
	if (co1->sleep_usecs == co2->sleep_usecs) {
		return 0;
	}
	return 1;
}


static inline int am_coroutine_wait_cmp(am_coroutine* co1,am_coroutine* co2){
	if (co1->fd < co2->fd) return -1;
	else if (co1->fd == co2->fd) return 0;
	else return 1;
}


RB_GENERATE(_am_coroutine_rbtree_sleep, _am_coroutine, sleep_node, am_coroutine_sleep_cmp);
RB_GENERATE(_am_coroutine_rbtree_wait, _am_coroutine, wait_node, am_coroutine_wait_cmp);


//将当前协程设置为睡眠状态，并等待指定的毫秒数
void am_schedule_sched_sleepdown(am_coroutine* co,uint64_t msecs){
	//转为微秒
	uint64_t usecs = msecs * 1000u;
	//如果当前协程已经在睡眠树中，则将其从树中移除
	am_coroutine* co_tmp = RB_FIND(_am_coroutine_rbtree_sleep,&co->sched->sleeping,co);
	if(co_tmp!=NULL){
		RB_REMOVE(_am_coroutine_rbtree_sleep,&co->sched->sleeping,co_tmp);
	}
	//已经运行的时间+额外的睡眠时间
	co->sleep_usecs = am_coroutine_diff_usecs(co->sched->birth,am_coroutine_usec_now()) + usecs;
	while(msecs){
		co_tmp = RB_INSERT(_am_coroutine_rbtree_sleep,&co->sched->sleeping,co);
		//插入失败
		if(co_tmp){
			printf("sleep_usecs %u\n",co->sleep_usecs);
			//
			co->sleep_usecs++;
			continue;
		}
		//如果返回NULL，表示插入成功
		co->status |= BIT(AM_COROUTINE_STATUS_SLEEPING);
		break;
	}
}


//从睡眠树中移除一个协程，将其状态改为就绪
void am_schedule_desched_sleepdown(am_coroutine* co){
	if(co->status & BIT(AM_COROUTINE_STATUS_SLEEPING)){
		RB_REMOVE(_am_coroutine_rbtree_sleep,&co->sched->sleeping,co);
		co->status &= CLEARBIT(AM_COROUTINE_STATUS_SLEEPING);
		co->status |= BIT(AM_COROUTINE_STATUS_READY);
	}
}

//在等待树中查找给定描述符的协程
am_coroutine* am_schedule_search_wait(int fd){
	am_coroutine find_it = {0};
	find_it.fd = fd;
	am_schedule* sched = am_coroutine_get_sched();
	am_coroutine* co = RB_FIND(_am_coroutine_rbtree_wait,&sched->waiting,&find_it);
	//清除状态位
	co->status = (am_coroutine_status)0;
	return co;
}

//从等待树中移除一个协程，将其状态改为就绪
am_coroutine* am_schedule_desched_wait(int fd){
	am_coroutine find_it = {0};
	find_it.fd = fd;

	am_schedule *sched = am_coroutine_get_sched();
	
	am_coroutine *co = RB_FIND(_am_coroutine_rbtree_wait, &sched->waiting, &find_it);
	if (co != NULL) {
		RB_REMOVE(_am_coroutine_rbtree_wait, &co->sched->waiting, co);
	}
	co->status = 0;
	//将协程从调度器的睡眠红黑树中移除，并将其状态从睡眠状态转换为就绪状态
	am_schedule_desched_sleepdown(co);
	
	return co;
}


//将协程设为等待状态，等待特定的文件描述符和事件发生
void am_schedule_sched_wait(am_coroutine* co,int fd,unsigned short events,uint64_t timeout){
	if(events & EPOLLIN){
		co->status |= AM_COROUTINE_STATUS_WAIT_READ;
	}else if(events & EPOLLOUT){
		co->status |= AM_COROUTINE_STATUS_WAIT_WRITE;
	}else{
		printf("unknown events\n");
		assert(0);
	}

	co->fd = fd;
	co->events = events;
	am_coroutine* co_tmp = RB_INSERT(_am_coroutine_rbtree_wait,&co->sched->waiting,co);
	//返回NULL，表示插入成功
	assert(co_tmp==NULL);
	if(timeout==1)return;
	//将协程设为睡眠状态
	am_schedule_sched_sleepdown(co,timeout);
}


//释放调度器资源
void am_schedule_free(am_schedule* sched){
	if(sched->epoll_fd > 0){
		close(sched->epoll_fd);
	}
	if(sched->eventfd > 0){
		close(sched->eventfd);
	}
	free(sched);
	sched = NULL;
	assert(0 == pthread_setspecific(global_sched_key,NULL));
}


//创建调度器
int am_schedule_create(int stack_size){
	if(stack_size==0){
		stack_size = AM_CO_MAX_STACKSIZE;
	}
	am_schedule* sched = (am_schedule*)calloc(1,sizeof(am_schedule));
	if(sched == NULL){
		printf("calloc err\n");
		return -1;
	}
	assert(0 == (pthread_setspecific(global_sched_key,sched)));
	sched->epoll_fd = epoll_create(1024);
	if(sched->epoll_fd==-1){
		printf("fail to initialize epoll\n");
		am_schedule_free(sched);
		return -1;
	}

	if(!sched->eventfd){
		sched->eventfd = eventfd(0,O_NONBLOCK);
		assert(-1 != sched->eventfd);
	}

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sched->eventfd;
	int ret = epoll_ctl(sched->epoll_fd,EPOLL_CTL_ADD,sched->eventfd,&ev);
	assert(-1 != ret);

	sched->stack_size = stack_size;
	sched->spawned_coroutines = 0;
	sched->default_timeout = 3000000u;

	RB_INIT(&sched->sleeping);
	RB_INIT(&sched->waiting);

	sched->birth = am_coroutine_usec_now();

	TAILQ_INIT(&sched->ready);
	LIST_INIT(&sched->busy);

	bzero(&sched->ctx,sizeof(am_cpu_ctx));
}


//移除到期的协程
static am_coroutine* am_schedule_expired(am_schedule* sched){
	uint64_t t_diff_usecs = am_coroutine_diff_usecs(sched->birth,am_coroutine_usec_now());
	//
	am_coroutine* co = RB_MIN(_am_coroutine_rbtree_sleep,&sched->sleeping);
	if(co==NULL)return NULL;
	//如果该协程已经运行的时间超过co->sleep_usecs
	if(co->sleep_usecs <= t_diff_usecs){
		RB_REMOVE(_am_coroutine_rbtree_sleep,&co->sched->sleeping,co);
		return co;
	}
	return NULL;
}


static inline int am_schedule_isdone(am_schedule* sched){
	return (RB_EMPTY(&sched->waiting) &&
		LIST_EMPTY(&sched->busy) &&
		RB_EMPTY(&sched->sleeping)&&
		TAILQ_EMPTY(&sched->ready));
}


//要唤醒的下一个协程的剩余时间
static uint64_t am_schedule_min_timeout(am_schedule* sched){
	//该协程已经运行的时间
	uint64_t t_diff_usecs = am_coroutine_diff_usecs(sched->birth,am_coroutine_usec_now());
	uint64_t min = sched->default_timeout;
	am_coroutine* co = RB_MIN(_am_coroutine_rbtree_sleep,&sched->sleeping);
	if(!co)return min;
	min = co->sleep_usecs;
	if(min > t_diff_usecs){
		//返回剩余时间
		return min - t_diff_usecs;
	}
	return 0;
}


static int am_schedule_epoll(am_schedule* sched){
	sched->num_new_events = 0;
	struct timespec t = {0,0};
	//获取睡眠协程的最小的剩余睡眠时间
	uint64_t usecs = am_schedule_min_timeout(sched);
	if(usecs && TAILQ_EMPTY(&sched->ready)){
		//转为秒
		t.tv_sec = usecs / 1000000u;
		if(t.tv_sec != 0){
			t.tv_nsec = (usecs % 1000u) * 1000u;
		}else{
			t.tv_nsec = usecs * 1000u;
		}
	}else{
		return 0;
	}

	int nready = 0;
	while(1){
		nready = epoll_wait(sched->epoll_fd,sched->eventlist,AM_CO_MAX_EVENTS,t.tv_sec*1000.0 + t.tv_nsec/1000000.0);
		if (nready == -1) {
			if (errno == EINTR) continue;
			else assert(0);
		}
		break;
	}

	sched->nevents = 0;
	sched->num_new_events = nready;
	return 0;
}


void am_schedule_run(void){
	am_schedule* sched = am_coroutine_get_sched();
	if(sched == NULL)return;
	//只要还有没有执行完的协程
	while(!am_schedule_isdone(sched)){
		//1.从睡眠树中获取已经睡完的协程^_^
		am_coroutine* expired = NULL;
		//获取睡眠时间到期的协程
		while((expired = am_schedule_expired(sched)) != NULL){
			am_coroutine_resume(expired);
		}

		//2.就绪队列中的协程
		//获取就绪队列中的最后一个协程
		am_coroutine* last_co_ready = TAILQ_LAST(&sched->ready,_am_coroutine_queue);
		while(!TAILQ_EMPTY(&sched->ready)){
			//获取就绪队列中的第一个协程
			am_coroutine* co = TAILQ_FIRST(&sched->ready);
			//从就绪队列中移除该协程
			TAILQ_REMOVE(&co->sched->ready,co,ready_next);

			am_coroutine_resume(co);
			if(co == last_co_ready){
				break;
			}
		}

		//3.等待树中的协程
		am_schedule_epoll(sched);
		//如果有新的事件到来
		while(sched->num_new_events){
			int idx = --sched->num_new_events;
			struct epoll_event* ev = sched->eventlist + idx;

			int fd = ev->data.fd;
			am_coroutine* co = am_schedule_search_wait(fd);
			if(co != NULL){
				am_coroutine_resume(co);
			}
		}
	}

	//释放资源
	am_schedule_free(sched);
	return;
}
