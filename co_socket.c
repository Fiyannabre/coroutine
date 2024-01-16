#define _GNU_SOURCE
#include <dlfcn.h>

#include "coroutine.h"


typedef int (*socket_t)(int domain, int type, int protocol);
typedef int(*accept_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
typedef ssize_t(*recv_t)(int sockfd, void *buf, size_t len, int flags);
typedef ssize_t(*send_t)(int sockfd, const void *buf, size_t len, int flags);
typedef int(*close_t)(int);


socket_t socket_f = NULL;
accept_t accept_f = NULL;
recv_t recv_f = NULL;
send_t send_f = NULL;
close_t close_f = NULL;

void init_hook(){
    socket_f = (socket_t)dlsym(RTLD_NEXT, "socket");
	accept_f = (accept_t)dlsym(RTLD_NEXT, "accept");
	recv_f = (recv_t)dlsym(RTLD_NEXT, "recv");
	send_f = (send_t)dlsym(RTLD_NEXT, "send");
	close_f = (close_t)dlsym(RTLD_NEXT, "close");
}

int socket(int domain, int type, int protocol) {
	if (!socket_f) init_hook();

	int fd = socket_f(domain, type, protocol);
	if (fd == -1) {
		printf("Failed to create a new socket\n");
		return -1;
	}
	//设置非阻塞
	int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(ret);
		return -1;
	}
	//端口复用
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	
	return fd;
}

int accept(int fd, struct sockaddr *addr, socklen_t *len){
	if (!accept_f) init_hook();

	my_schedule *sche = get_sched();
	my_coroutine *co = sche->current_co;

	co->fd = fd;
	int client_fd = -1;

	while(1){
		//epoll
		struct epoll_event ev;
		ev.data.fd = co->fd;
		ev.events = POLLIN | POLLERR | POLLHUP;
		epoll_ctl(sche->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
		yield(sche, co);
		client_fd = accept_f(fd, addr, len);
		if(client_fd < 0){
			printf("accept connnect error.\nfd: %d\n", fd);
		}else{
			break;
		}
	}
	//非阻塞
	int ret = fcntl(client_fd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(client_fd);
		return -1;
	}
	//
	int reuse = 1;
	setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

	return client_fd;
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
	if (!recv_f) init_hook();

	my_schedule *sche = get_sched();
	my_coroutine *co = sche->current_co;
	co->fd = fd;

	//epoll
	struct epoll_event ev;
	ev.data.fd = co->fd;
	ev.events = POLLIN | POLLERR | POLLHUP;
	epoll_ctl(sche->epoll_fd, EPOLL_CTL_ADD, fd, &ev);

	yield(sche, co);

	int ret = recv_f(fd, buf, len, flags);
	if (ret < 0) {
		if (errno == ECONNRESET) return -1;	
	}

	return ret;
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
	if (!send_f) init_hook();

	int sent = 0;

	int ret = send_f(fd, ((char*)buf) + sent, len - sent, flags);
	if (ret == 0) return ret;
	if (ret > 0) sent += ret;

	my_schedule *sche = get_sched();
	my_coroutine *co = sche->current_co;
	co->fd = fd;
	
	while (sent < len) {
		struct epoll_event ev;
		ev.data.fd = co->fd;
		ev.events = POLLOUT | POLLERR | POLLHUP;
		epoll_ctl(sche->epoll_fd, EPOLL_CTL_ADD, fd, &ev);

		yield(sche, co);

		ret = send_f(fd, ((char*)buf) + sent, len - sent, flags);

		if (ret <= 0) {			
			break;
		}
		sent += ret;
	}

	if (ret <= 0 && sent == 0) return ret;
	
	return sent;
}

int close(int fd){
	return close_f(fd);
}
