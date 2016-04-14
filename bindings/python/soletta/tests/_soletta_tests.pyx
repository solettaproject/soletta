import soletta.asyncio
from libc.stdint cimport uint32_t
from cython.operator cimport dereference as dref

cdef extern from "sol-mainloop.h":
	struct sol_timeout:
		pass

	sol_timeout* sol_timeout_add(uint32_t, bint (*)(void*), void*)
	bint sol_timeout_del(void*)


tests = {}

def add_test(func):
	tests[func] = False

cdef bint cb(void* data):
	cdef bint success = dref(<bint*>data)
	success = True
	return False

cdef bint cb2(void* data):
	cdef int* d = <int*>data
	cdef int count = dref(d)
	count+=1

	return True

def test_timeout():
	success = False
	t = sol_timeout_add(100, cb, <void*>&success)

	def post():
		return success
	

def test_timeout_del():
	cdef sol_timeout* t

	cdef int callbackcounts = 0

	t = sol_timeout_add(100, cb2, <void*>&callbackcounts)

	def test_timeout_del_post():
		return callbackcounts >= 1
	
	return test_timeout_del_post

def run_tests():

	import asyncio
	
	add_test(test_timeout)

	post_test_checks = []

	for test in tests:
		try:
			post = test()
			if post:
				post_test_checks.append(post)
			print("{}: pass".format(test.__name__))
		except:
			print("{}: FAIL".format(test.__name__))

	asyncio.get_event_loop().run_forever()

	for post in post_test_checks:
		try:
			assert(post())
			print("{}: pass".format(post.__name__))
		except:
			print("{}: FAIL ".format(post.__name__))


