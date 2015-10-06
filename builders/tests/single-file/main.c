#include <stdio.h>

#include "sol-log.h"
#include "sol-mainloop.h"

bool print_func(void *data) {
	SOL_WRN("test");
	return true;
}

bool quit_func(void *data) {
	sol_quit();
	return false;
}

void startup(void) {
	SOL_WRN("hello soletta");
	sol_timeout_add(100, print_func, NULL);
	sol_timeout_add(1500, quit_func, NULL);
}

void shutdown(void) {
	SOL_WRN("bye soletta");
}

SOL_MAIN_DEFAULT(startup, shutdown);

