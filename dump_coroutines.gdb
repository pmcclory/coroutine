# 
# gdb script for dumping the contents of the co_routine
# scheduler state
#
# example:
#
#    Breakpoint 1, co_sched (loop=0x602010) at coroutine.c:206
#    206             struct co_context *next = NULL;
#    (gdb) source ./dump_coroutines.gdb
#    (gdb) dump_coroutines_state loop
#    0x602010
#    Current: (nil)
#    Ready:
#    
#    Waiting:
#    0x602120, 0x604520,
#    
#    Done:
#

define walk_coroutine_list
	if $arg0
		printf "%p, ", $arg0
		if $arg0->next
			walk_coroutine_list $arg0->next
		end
	end
	printf "\n"
end

define dump_coroutines_state
	printf "%p\n", $arg0
	printf "Current: %p\n", $arg0->current
	printf "Ready: \n"
	walk_coroutine_list $arg0->ready
	printf "Waiting: \n"
	walk_coroutine_list $arg0->waiting
	printf "Done: \n"
	walk_coroutine_list $arg0->done
end
