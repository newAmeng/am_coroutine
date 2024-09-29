

#include "am_coroutine.h"
#include "am_socket.h"
#include <arpa/inet.h>



void am_schedule_run(void);


#define MAX_CLIENT_NUM			1000000
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


void server_reader(void *arg) {
	int fd = *(int *)arg;
	int ret = 0;

 
	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = EPOLLIN;

	while (1) {
		
		char buf[1024] = {0};
		ret = am_recv(fd, buf, 1024, 0);
		if (ret > 0) {
			if(fd > MAX_CLIENT_NUM) 
			printf("read from server: %.*s\n", ret, buf);

			ret = am_send(fd, buf, strlen(buf), 0);
			if (ret == -1) {
				am_close(fd);
				break;
			}
		} else if (ret == 0) {	
			am_close(fd);
			break;
		}

	}
}


void server(void *arg) {

	unsigned short port = *(unsigned short *)arg;
	free(arg);

	int fd = am_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return ;

	struct sockaddr_in local, remote;
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = INADDR_ANY;
	bind(fd, (struct sockaddr*)&local, sizeof(struct sockaddr_in));

	listen(fd, 20);
	printf("listen port : %d\n", port);

	
	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	while (1) {
		socklen_t len = sizeof(struct sockaddr_in);
		int cli_fd = am_accept(fd, (struct sockaddr*)&remote, &len);
		if (cli_fd % 1000 == 999) {

			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			
			printf("client fd : %d, time_used: %d\n", cli_fd, time_used);
		}
		//printf("new client comming\n");

		am_coroutine *read_co;
		am_coroutine_create(&read_co, server_reader, &cli_fd);

	}
	
}



int main(int argc, char *argv[]) {
	am_coroutine *co = NULL;

	int i = 0;
	unsigned short base_port = 8888;
	for (i = 0;i < 100;i ++) {
		unsigned short *port = (unsigned short*)calloc(1, sizeof(unsigned short));
		*port = base_port + i;
		am_coroutine_create(&co, server, port); ////////no run
	}

	am_schedule_run(); //run

	return 0;
}



