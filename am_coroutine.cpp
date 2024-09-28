#include "am_coroutine.h"

pthread_key_t global_sched_key;
static pthread_once_t sched_key_once = PTHREAD_ONCE_INIT;

#if defined(__GNUC__) && defined(__GNUG__)
    // G++
    extern "C" int _switch(am_cpu_ctx *new_ctx, am_cpu_ctx *cur_ctx);

#elif defined(__GNUC__)
	// GCC
	int _switch(am_cpu_ctx *new_ctx, am_cpu_ctx *cur_ctx);

#endif


__asm__(
".text\n"
".p2align 4,,15\n"
".global _switch\n"
".global __switch\n"
"_switch:\n"
"__switch:\n"
"movq %rsp,0(%rsi)\n"
"movq %rbp,8(%rsi)\n"
"movq (%rsp),%rax\n"
"movq %rax,16(%rsi)\n"
"movq %rbx,24(%rsi)\n"
"movq %r12,32(%rsi)\n"
"movq %r13,40(%rsi)\n"
"movq %r14,48(%rsi)\n"
"movq %r15,56(%rsi)\n"

"movq 56(%rdi),%r15\n"
"movq 48(%rdi),%r14\n"
"movq 40(%rdi),%r13\n"
"movq 32(%rdi),%r12\n"
"movq 24(%rdi),%rbx\n"

"movq 8(%rdi),%rbp\n"
"movq 0(%rdi),%rsp\n"

"movq 16(%rdi),%rax\n"
"movq %rax,(%rsp)\n"
"ret\n"
);


static void _exec(void* lt){
	am_coroutine* co = (am_coroutine*)lt;
	co->func(co->arg);
	co->status |= (BIT(AM_COROUTINE_STATUS_EXITED));
	am_coroutine_yield(co);
}

extern int am_schedule_create(int stack_size);

//释放一个协程的资源
void am_coroutine_free(am_coroutine* co){
	if(co==NULL)return;
	co->sched->spawned_coroutines--;
	if(co->stack){
		free(co->stack);
		co->stack = NULL;
	}
	if(co){
		free(co);
		co = NULL;
	}
}


//初始化协程的上下文环境
static void am_coroutine_init(am_coroutine* co){
	void** stack = (void**)((uintptr_t)co->stack + co->stack_size);
	stack[-3] = NULL;
	stack[-2] = (void*)co;
	co->ctx.rsp = (void*)(stack-(4*sizeof(void*)));
	co->ctx.rbp = (void*)(stack-(3*sizeof(void*)));
	//下一条指令在内存中的地址
	co->ctx.rip = (void*)_exec;
	co->status = BIT(AM_COROUTINE_STATUS_READY);
}

void am_coroutine_yield(am_coroutine* co){
	co->ops = 0;
	_switch(&co->sched->ctx,&co->ctx);
}


static inline void am_coroutine_madvise(am_coroutine* co){
	//
}

//恢复一个协程的运行
int am_coroutine_resume(am_coroutine* co){
	if(co->status & BIT(AM_COROUTINE_STATUS_NEW)){
		am_coroutine_init(co);
	}

	am_schedule* sched = am_coroutine_get_sched();
	sched->curr_thread = co;
	//从当前协程的上下文切换到调度器的上下文
	_switch(&co->ctx,&co->sched->ctx);
	sched->curr_thread = NULL;
	//am_coroutine_madvise(co);
	if(co->status & BIT(AM_COROUTINE_STATUS_EXITED)){
		printf("协程已经结束\n");
		am_coroutine_free(co);
		return -1;
	}
	return 0;
}


void am_coroutine_renice(am_coroutine* co){
	//
}

void am_coroutine_sleep(uint64_t msecs){
	//
}

void am_coroutine_detach(void){
	//
}


static void am_coroutine_sched_key_destructor(void* data){
	free(data);
}


static void am_coroutine_sched_key_creator(void){
	assert(0 == pthread_key_create(&global_sched_key,am_coroutine_sched_key_destructor));
	assert(0 == pthread_setspecific(global_sched_key,NULL));
	return;
}


//创建协程
int am_coroutine_create(am_coroutine** new_co,proc_coroutine func,void* arg){
	assert(0 == pthread_once(&sched_key_once,am_coroutine_sched_key_creator));

	am_schedule* sched = am_coroutine_get_sched();
	if(sched == NULL){
		am_schedule_create(0);
		sched = am_coroutine_get_sched();
		if(sched == NULL){
			printf("fail to create scheduler\n");
			return -1;
		}
	}

	am_coroutine* co = (am_coroutine*)calloc(1,sizeof(am_coroutine));
	if(co == NULL){
		printf("calloc err\n");
		return -1;
	}

	int ret = posix_memalign(&co->stack,getpagesize(),sched->stack_size);
	if(ret!=0){
		printf("posix_memalign err\n");
		free(co);
		return -1;
	}

	co->sched = sched;
	co->stack_size = sched->stack_size;
	co->status = BIT(AM_COROUTINE_STATUS_NEW);
	co->id = sched->spawned_coroutines++;
	co->func = func;
	co->fd = -1;
	co->events = 0;
	co->arg = arg;
	co->birth = am_coroutine_usec_now();
	*new_co = co;
	TAILQ_INSERT_TAIL(&co->sched->ready,co,ready_next);
	return 0;

}




