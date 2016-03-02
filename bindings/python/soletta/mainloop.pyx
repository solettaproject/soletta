import asyncio


cdef extern from "sol-mainloop.h":
	struct sol_mainloop_implementation:
		void run()
		bint idle_add()
		void idle_del()
		pass

	bint sol_mainloop_set_implementation(const sol_mainloop_implementation *)


cdef void wrap_sol_init():
	pass

cdef void wrap_sol_shutdown():
	pass

cdef void wrap_sol_quit():
	asyncio.get_event_loop().stop()

cdef void wrap_sol_run():
	asyncio.get_event_loop().run_forever()

cdef void *wrap_sol_timeout_add(args):
	pass

cdef void wrap_sol_timeout_del(args):
	pass

cdef bint wrap_idle_add(args):
	return False

cdef void wrap_idle_del(args):
	pass

cdef void quit():
	pass

cdef void wrap_source_add(args):
	pass

cdef void wrap_source_del(args):
	pass

cdef void * wrap_source_get_data(args):
	return NULL

cdef sol_mainloop_implementation _py_asyncio_impl

_py_asyncio_impl.run = wrap_sol_run
"""_py_asyncio_impl.idle_add = wrap_idle_add
_py_asyncio_impl.idle_del = wrap_idle_del
_py_asyncio_impl.init = wrap_sol_init
_py_asyncio_impl.quit = wrap_sol_quit
_py_asyncio_impl.run = wrap_sol_run
_py_asyncio_impl.shutdown = wrap_sol_shutdown
_py_asyncio_impl.source_add = wrap_source_add
_py_asyncio_impl.source_del = wrap_source_del
_py_asyncio_impl.source_get_data = wrap_source_get_data
_py_asyncio_impl.timeout_add = wrap_sol_timeout_add
_py_asyncio_impl.timeout_del = wrap_sol_timeout_del"""

sol_mainloop_set_implementation(&_py_asyncio_impl) # to be called on module load
