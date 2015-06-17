#!/usr/bin/env python3

from multiprocessing import Manager
from threading import Thread
import argparse
import subprocess

WARN = '\033[93m'
PASS = '\033[92m'
FAIL = '\033[31m'
ENDC = '\033[0m'

STATUS_COLOR = [FAIL, PASS]
STATUS_TAG = ["FAIL", "PASS"]

def run_test_program(cmd, test, stat, log):
    success = 1
    print("%sRUNNING: %s%s" % (WARN, ENDC, test))
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                         shell=True, universal_newlines=True)
        log.append(output)
    except subprocess.CalledProcessError as e:
        success = 0
        log.append(e.output)

    stat[test] = success

    # print out each test's result right away
    print("%s%s:%s %s" % (STATUS_COLOR[success], STATUS_TAG[success], ENDC, test))

def print_log(log_file, stat, log):
    output = ""
    status_cnt = {'FAIL': 0, 'PASS': 0}
    for k,v in sorted(stat.items()):
        curr_status = STATUS_TAG[v]
        status_cnt[curr_status] = status_cnt[curr_status] + 1

    output += "============================================================================\n"
    output += "Testsuite summary\n"
    output += "============================================================================\n"
    output += "# TOTAL: %d\n" % len(stat)
    output += "# SUCCESS: %d\n" % status_cnt["PASS"]
    output += "# FAIL: %d\n" % status_cnt["FAIL"]
    output += "============================================================================\n"
    output += "See %s\n" % log_file
    output += "============================================================================\n"

    # show the stat in the stdout
    print(output)

    log_output = ""
    f = open(log_file, mode="w+")
    for i in log:
        log_output += "%s\n" % i
    f.write(log_output)
    f.close()

def run_valgrind_test(args):
    manager = Manager()
    common_args = "--error-exitcode=1 --num-callers=30"
    valgrind_tools = {
        'memcheck': '--leak-check=full --show-reachable=no',
        'helgrind': '--history-level=approx',
        'drd': None,
        'exp-sgcheck': None,
    }

    for k,v in valgrind_tools.items():
        stat = manager.dict()
        log = manager.list()
        threads = []

        for i in args.tests.split():
            cmd = "{valgrind} {test_path} {supp} --tool={tool} {tool_args} {common}". \
                  format(valgrind=args.valgrind, test_path=i, supp=args.valgrind_supp, \
                         tool=k, tool_args=v, common=common_args)
            t = Thread(target=run_test_program, args=(cmd, i, stat, log,))
            t.start()
            threads.append(t)

        for t in threads:
            t.join()

        print_log("test-suite-%s.log" % k, stat, log)

def run_test(args):
    manager = Manager()
    stat = manager.dict()
    log = manager.list()
    threads = []

    for i in args.tests.split():
        t = Thread(target=run_test_program, args=(i, i, stat, log,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    print_log("test-suite.log", stat, log)

def run_suite(args):
    if args.valgrind:
        run_valgrind_test(args)
    else:
        run_test(args)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", help="List of tests to run", type=str)
    parser.add_argument("--valgrind", help="Path to valgrind, if provided " \
                        "the tests are run with it", type=str)
    parser.add_argument("--valgrind-supp", help="Path to valgrind's suppression file", type=str)

    args = parser.parse_args()
    if args.valgrind_supp:
        args.valgrind_supp = "--suppressions=%s" % args.valgrind_supp

    run_suite(args)
