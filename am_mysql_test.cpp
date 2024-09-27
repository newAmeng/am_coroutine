#include <dlfcn.h>

#include "am_coroutine.h"

#include <stdio.h>
#include <string.h>
#include <mysql.h>

typedef ssize_t(*recv_t)(int sockfd,void* buf,size_t len,int flags);
typedef ssize_t(*send_t)(int sockfd,const void* buf,size_t len,int flags);

recv_t recv_f = NULL;
send_t send_f = NULL;


int init_hook(void){
	recv_f = (recv_t)dlsym(RTLD_NEXT,"recv");
	send_f = (send_t)dlsym(RTLD_NEXT,"send");

}

ssize_t recv(int fd,void* buf,size_t len,int flags){
	printf("[%s:%s:%d]--> \n",__FILE__,__func__,__LINE__);
	return recv_f(fd,buf,len,flags);
	
}

ssize_t send(int fd,const void* buf,size_t len,int flags){
	printf("[%s:%s:%d]--> \n",__FILE__,__func__,__LINE__);
	return send_f(fd,buf,len,flags);
}


void func (void *arg) {

	
	MYSQL* mysql = mysql_init(NULL);
	if (!mysql) {
		printf("mysql_init failed\n");
		return ;
	}

	if (!mysql_real_connect(mysql, "10.157.237.160", "root", "@WangMeng9872", 
		"scott", 0, NULL, 0)){
		printf("mysql_real_connect failed: %s\n", mysql_error(mysql));
		return ;
	} else{
		printf("mysql_real_connect success\n");
	}

	
}

extern void am_schedule_run(void);

int main() {

	init_hook();

	am_coroutine *co = NULL;
	am_coroutine_create(&co, func, NULL);
	am_schedule_run(); //run




}