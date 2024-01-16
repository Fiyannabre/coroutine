#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>

#include "coroutine.h"

#define ushort unsigned short
#define TIME_SUB_MS(tv1, tv2) ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

void io_coroutine(void *arg) {
	int fd = *(int *)arg;
	int ret = 0;

	struct epoll_event ev;
	ev.data.fd = fd;
	ev.events = POLLIN;

	while (1) {
		char buf[1024] = {0};
		ret = recv(fd, buf, 1024, 0);
		if (ret > 0) {
			//printf("read from server: %.*s\n", ret, buf);

			ret = send(fd, buf, strlen(buf), 0);
			if (ret == -1) {
				close(fd);
				break;
			}
		} else if (ret == 0) {	
			close(fd);
			break;
		}

	}
}

void listen_co(void *arg){
    ushort port = *(ushort *)arg;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    //设置非阻塞
    int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    //端口复用
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

	//监听端口
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
		int cli_fd = accept(fd, (struct sockaddr*)&remote, &len);
//		if (cli_fd % 1000 == 999) {

			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			
			printf("server fd : %d client fd : %d, time_used: %d\n", fd, cli_fd, time_used);
//		}

		create_coroutine(io_coroutine, &cli_fd);
	}
}

int main(int argc, char *argv[]) {
	ushort base_port = 8000;
	my_schedule *schedule = init_schedule();

	int i = 0;
	for (i = 0;i < 100;i ++) {
		unsigned short *port = calloc(1, sizeof(unsigned short));
		*port = base_port + i;
		create_coroutine(listen_co, port);
	}
	run(schedule);

	return 0;
}