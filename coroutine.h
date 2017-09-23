#ifndef COROUTINE_H
#define COROUTINE_H

struct co_loop;
struct co_context;

/*
 * library initialization
 */
struct co_loop *co_init();

/*
 * Create a new coroutine
 */
struct co_context *co_create(struct co_loop *loop, void (*func)(struct co_context*, void *), void *arg);

/*
 * Exit a coroutine - this function MUST be called
 * execution will abort if a coroutine returns without calling
 * co_exit()
 */
void co_exit(struct co_context *context, int rc);

/*
 * Yield execution there's data
 * available for the fd
 */
int co_yield_fd(struct co_context *context, int fd);

/*
 * Start the event loop
 */
int co_run(struct co_loop *loop);

#endif
