#include "am_coroutine.h"

static int am_poll_inner(struct epoll_event* evlist, int maxevents, int timeout) {
	//获取当前协程调度器sched和当前正在运行的协程co
	nty_schedule *sched = nty_coroutine_get_sched();

	if (timeout == 0){
		return epoll_wait(sched->epoll_fd,evlist, maxevents, timeout);
	}
	//如果timeout小于0，表示无限期等待
	if (timeout < 0){
		timeout = INT_MAX;
	}
	
	nty_coroutine *co = sched->curr_thread;
	//遍历所有要监视的文件描述符
	int i = 0;
	for (i = 0;i < maxevents;i ++) {
	
		struct epoll_event ev;
		//将poll事件转换为epoll事件
		ev.events = evlist[i].events;
		ev.data.fd = evlist[i].data.fd;
		epoll_ctl(sched->epoll_fd, EPOLL_CTL_ADD, evlist[i].data.fd, &ev);
		//设置当前协程的等待事件
		co->events = evlist[i].events;
		//将协程设置为等待状态
		am_schedule_sched_wait(co, evlist[i].data.fd, evlist[i].events, timeout);
	}
	//将当前协程让出CPU，使得调度器可以运行其他协程
	am_coroutine_yield(co);  //1  
	//在协程恢复执行后，再次遍历所有文件描述符
	for (i = 0;i < maxevents;i ++) {
	
		struct epoll_event ev;
		ev.events = evlist[i].events;
		ev.data.fd = evlist[i].data.fd;
		epoll_ctl(sched->epoll_fd, EPOLL_CTL_DEL, evlist[i].data.fd, &ev);
		//解除协程的等待状态
		am_schedule_desched_wait(evlist[i].data.fd);
	}
	//返回监视的文件描述符数量
	return maxevents;
}


int am_socket(int domain, int type, int protocol) {

	int fd = socket(domain, type, protocol);
	if (fd == -1) {
		printf("Failed to create a new socket\n");
		return -1;
	}
	int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(ret);
		return -1;
	}
	//端口复用，用于快速重启服务器时，
	//避免因为之前的连接尚未完全断开而无法绑定到相同的地址和端口
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	
	return fd;
}

int am_accept(int fd, struct sockaddr* addr, socklen_t* len) {
	int sockfd = -1;
	int timeout = 1;
	am_coroutine *co = am_coroutine_get_sched()->curr_thread;
	
	while (1) {
		struct epoll_event ev;
		ev.data.fd = fd;
		//可读，错误，挂起
		//对于是否有客户端与我们建立连接，就是检测listenfd的读事件，
		//就是看TCP的全连接队列中是否有数据
		ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
		//等待事件发生
		am_poll_inner(&ev, 1, timeout);

		sockfd = accept(fd, addr, len);
		if (sockfd < 0) {
			if (errno == EAGAIN) {
				continue;
			} else if (errno == ECONNABORTED) {
				printf("accept : ECONNABORTED\n");
				
			} else if (errno == EMFILE || errno == ENFILE) {
				printf("accept : EMFILE || ENFILE\n");
			}
			return -1;
		} else {
			break;
		}
	}

	int ret = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(sockfd);
		return -1;
	}
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	
	return sockfd;
}


int am_connect(int fd, struct sockaddr* name, socklen_t namelen) {

	int ret = 0;

	while (1) {

		struct epoll_event ev;
		ev.data.fd = fd;
		//可写，错误，挂起
		//跟mysql是否建立成功了？注册写事件，如果写事件触发了，则成功了
		ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
		//等待事件发生
		am_poll_inner(&ev, 1, 1);

		ret = connect(fd, name, namelen);
		if (ret == 0) break;

		if (ret == -1 && (errno == EAGAIN ||
			errno == EWOULDBLOCK || 
			errno == EINPROGRESS)) {
			continue;
		} else {
			break;
		}
	}

	return ret;
}


ssize_t am_recv(int fd, void* buf, size_t len, int flags) {
	
	struct epoll_event ev;
	ev.data.fd = fd;
	//可读，错误，挂起
	//对于clientfd什么时候给我们发送数据，则是检测缓冲区中是否有数据
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;

	am_poll_inner(&ev, 1, 1);

	int ret = recv(fd, buf, len, flags);
	if (ret < 0) {
		//if (errno == EAGAIN) return ret;
		if (errno == ECONNRESET) return -1;
		//printf("recv error : %d, ret : %d\n", errno, ret);
		
	}
	return ret;
}

ssize_t am_send(int fd, const void* buf, size_t len, int flags) {
	
	int sent = 0;

	int ret = send(fd, ((char*)buf)+sent, len-sent, flags);
	if (ret == 0) return ret;
	if (ret > 0) sent += ret;

	while (sent < len) {
		struct epoll_event ev;
		ev.data.fd = fd;
		ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
		//等待套接字准备好写操作
		am_poll_inner(&ev, 1, 1);
		ret = send(fd, ((char*)buf)+sent, len-sent, flags);
		//printf("send --> len : %d\n", ret);
		if (ret <= 0) {			
			break;
		}
		sent += ret;
	}

	if (ret <= 0 && sent == 0) return ret;
	
	return sent;
}



int am_close(int fd) {
	return close(fd);
}

