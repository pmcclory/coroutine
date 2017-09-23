#include "coroutine.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>

struct pp_arg {
	int rfd;
	int wfd;
	int ping;
};

void pingpong(struct co_context *context, void *p)
{
	int i;
	char buf[100];
	struct pp_arg *arg = (struct pp_arg *)p;
	memset(buf, 0, 5);

	for (i = 0; i < 100; i++) {
		printf("in coroutine sending %p\n", context);
		if (arg->ping) {
			write(arg->wfd, "ping", strlen("ping"));
		} else {
			write(arg->wfd, "pong", strlen("pong"));
		}
		co_yield_fd(context, arg->rfd);
		read(arg->rfd, buf, 4);
		printf("in coroutine %p %s (%d)\n", context, buf, i);
		memset(buf, 0, 4);		
	}
	co_exit(context, 0);
	// shouldn't ever get here
}

int main(void)
{
	struct pp_arg args[2];
	struct co_loop *loop = co_init();
	int pipefds[4];
	pipe(pipefds);
	pipe(&pipefds[2]);
	args[0].rfd = pipefds[0];
	args[0].wfd = pipefds[3];
	args[0].ping = 1;
	args[1].rfd = pipefds[2];
	args[1].wfd = pipefds[1];
	args[1].ping = 0;

	struct co_context *c1 = co_create(loop, pingpong, (void *)&(args[0]));
	struct co_context *c2 = co_create(loop, pingpong, (void *)&(args[1]));
	// shutup gcc
	if (c1) {}
	if (c2) {}
	return co_run(loop);
}

/* vim: ts=4: */

