#include "coroutine.h"

#include <stdlib.h>
#include <sys/epoll.h>
#include <ucontext.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define MAX_EVENTS 16
#define STACK_SIZE 8192

#define DEBUG 0

enum co_state {
	NOT_STARTED,
	RUNNING,
	PENDING,
	WAITING,
	COMPLETE
};

struct co_context {
	enum co_state state;
	ucontext_t ucp;
	struct co_loop *loop;
	void (*func)(struct co_context *, void *);
	int registered;
	int rc;
	int cycles;
	struct co_context *prev;
	struct co_context *next;
};

struct co_loop {
	int epollfd;
	unsigned int context_count;
	struct co_context *current;
	struct co_context *ready;
	struct co_context *waiting;
	struct co_context *not_started;
	struct co_context *done;
	struct epoll_event *epoll_events;
};

/*
 * remove a node from a linked list
 */
#define REMOVE(node, src, tmp) \
	do { \
		tmp = node; \
		if (*src == node) { \
			*src = node->next; \
		} \
		if (tmp->prev) { \
			tmp->prev->next = tmp->next; \
		} \
		if (tmp->next) { \
			tmp->next->prev = tmp->prev; \
		} \
		tmp->prev = NULL; \
		tmp->next = NULL; \
	} while (0)

/*
 * Insert a node into a linked list at the head
 */
#define INSERT(node, dst) \
	do { \
		node->next = *dst; \
		if (*dst) { \
			(*dst)->prev = node; \
		} \
		*dst = node; \
	} while (0)

/*
 * Move a node from one linked list to another
 */
#define MOVE(node, src, dst, tmp) \
	do { \
		REMOVE(node, src, tmp); \
		INSERT(tmp, dst); \
	} while (0)

/*
 * library initialization
 */
struct co_loop* co_init()
{
	struct co_loop *ret = calloc(1, sizeof(*ret));
	if (!ret) {
		return NULL;
	}

	//size arg is ignored but must be > 0
	ret->epollfd = epoll_create(1);
	if (ret->epollfd == -1) {
		free(ret);
		return NULL;
	}

	ret->epoll_events = calloc(MAX_EVENTS, sizeof(*(ret->epoll_events)));
	if (!ret->epoll_events) {
		free(ret);
		return NULL;
	}
	return ret;
}

#if __x86_64__
static void wrapper(int f_head, int f_tail, int a_head, int a_tail, int c_head, int c_tail)
{
	long long int f;
	long long int a;
	long long int c;

	void *(*func)(struct co_context *c, void *);
	void *arg;
	struct co_context *context;

	f = f_head;
	f = f << 32;
	f = f | ( (long long int) f_tail & 0xffffffff );
	a = a_head;
	a = a << 32;
	a = a | ( (long long int) a_tail & 0xffffffff );
	c = c_head;
	c = c << 32;
	c = c | ( (long long int) c_tail & 0xffffffff );

	func = (void *(*)(struct co_context *, void *))f;
	arg = (void *)a;
	context = (struct co_context *)c;

	context->state = RUNNING;
	context->cycles += 1;
	func(context, arg);
}
#elif __x86_32__

#else
#error Only x86 and x86_64 supported
#endif

static inline void hack_makecontext(ucontext_t *ucp, void (*func)(struct co_context *, void *), void *arg, struct co_context * context)
{
#if  __x86_64__
	int f_head = (int)((long long int)(func) >> 32) & 0xffffffff;
	int f_tail = (int)((long long int)func & 0xffffffff);
	int a_head = (int)((long long int)(arg) >> 32) & 0xffffffff;
	int a_tail = (int)((long long int)arg & 0xffffffff);
	int c_head = (int)((long long int)(context) >> 32) & 0xffffffff;
	int c_tail = (int)((long long int)context & 0xffffffff);
	makecontext(ucp, (void (*)(void))wrapper, 6, f_head, f_tail, a_head, a_tail, c_head, c_tail);
#elif __x86_32__

#else
#error Only x86 and x86_64 supported
#endif
	return;
}

/*
 * Create a new coroutine
 */
struct co_context *co_create(struct co_loop *loop, void (*func)(struct co_context *, void *), void *arg)
{
	struct co_context *ret;
	if (loop == NULL) {
		return NULL;
	}

	ret = calloc(1, sizeof(*ret));
	ret->func = func;
	if (getcontext(&(ret->ucp))) {
		return NULL;
	}
	ret->ucp.uc_stack.ss_sp = calloc(1, STACK_SIZE);
	ret->ucp.uc_stack.ss_size = STACK_SIZE;
	ret->ucp.uc_link = NULL; // TODO we do want a successor

	hack_makecontext(&ret->ucp, func, arg, ret);

	INSERT(ret, &loop->not_started);
	ret->loop = loop;
	ret->state = WAITING;

	return ret;
}

static int co_sched(struct co_loop *loop)
{
	/*
	 * choose the coroutine to run
	 *
	 * prioritize those that haven't been started
	 * otherwise call whoever has data available
	 */

	// TODO check if data is available, and move
	// any of them to the ready list
	int ready_fds;
	struct co_context *next = NULL;
	struct co_context *tmp = NULL;
	// if we have nothing pending we're done
	// exit the event loop altogether
	if (loop->not_started == NULL && !loop->waiting && !loop->ready) {
		//figure out how to do this properly
		exit(0);
	}
	if (loop->not_started) {
		next = loop->not_started;
		MOVE(next, &loop->not_started, &loop->ready, tmp);
	} else if (!loop->ready) {
		//if we don't have any ready
		///we gotta wait
		if ((ready_fds = epoll_wait(loop->epollfd, loop->epoll_events, MAX_EVENTS, -1)) == -1) {
			printf("epoll_wait failed %s\n", strerror(errno));
			exit(-1);
			return -1;
		}
		for ( ; ready_fds ; ready_fds--) {
			next = (struct co_context *)loop->epoll_events[ready_fds-1].data.ptr;
			MOVE(next, &loop->waiting, &loop->ready, tmp);
		}
	}

	MOVE(loop->ready, &loop->ready, &loop->current, tmp);
	loop->current->state = PENDING;
#if DEBUG
	printf("scheduling %p\n", loop->current);
#endif
	//like exec doesn't return if successful
	setcontext(&loop->current->ucp);
	//failed to schedule what do we do?
	return -1;
}

void co_exit(struct co_context *context, int rc)
{
	//TODO - free the stack
	struct co_context *tmp;
	struct co_loop *loop = context->loop;
	context->rc = rc;

	MOVE(context, &loop->current, &loop->done, tmp);

#if DEBUG
	printf("exiting %p\n", context);
#endif
	//then schedule a new coroutine
	co_sched(context->loop);
	//wth do we do if co_sched doesn't return successfully?
}

/*
 * Yield execution until there's data
 * available for the fd
 */
int co_yield_fd(struct co_context *context, int fd)
{
	int op;
	struct epoll_event event;
	event.data.ptr = context;
	event.events = EPOLLIN|EPOLLONESHOT;
	struct co_context *tmp;

	if (fd == -1) {
		return -1;
	}

	if (context->registered) {
		op = EPOLL_CTL_MOD;
	} else {
		op = EPOLL_CTL_ADD;
		context->registered = 1;
	}

	// add the fd to the epoll watchlist
	if (epoll_ctl(context->loop->epollfd, op, fd, &event)) {
		return -1;
	}

	// save the stack
	context->state = WAITING;
	/*
	 *  need to do this first!
	 *  otherwise this will happen twice, this was a nasty little bug
	 */
	MOVE(context, &context->loop->current, &context->loop->waiting, tmp);
	getcontext(&context->ucp);

#if DEBUG
	printf("yielding %p\n", context);
#endif

	//we return "twice" like fork
	//immediately and when we get scheduled again
	if (context->state == WAITING) {
		if (!co_sched(context->loop)) {
			return -1;
		}
	} else if (context->state == PENDING) {
		// if we get here we've been resumed!
		context->state = RUNNING;
		context->cycles += 1;
#if DEBUG
		printf("scheduling %p (run #%d)\n", context, context->cycles);
#endif
	} else {
		fprintf(stderr, "unexpected state in co_yield_fd\n");
		abort();
	}
	return 0;
}

/*
 * Start the event loop
 */
int co_run(struct co_loop *loop)
{
	if (loop == NULL)
		return -1;

	return co_sched(loop);
}

/* vim: ts=4: */

