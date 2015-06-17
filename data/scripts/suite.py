#!/usr/bin/env python3

import argparse
import subprocess

suite_log = []
status = {}

PASS = '\033[92m'
FAIL = '\033[31m'
ENDC = '\033[0m'

STATUS_COLOR = [FAIL, PASS]
STATUS_TAG = ["FAIL", "PASS"]
STATUS_CNT = {'FAIL': 0, 'PASS': 0}

def run_test(test, valgrind, valgrind_supp):
    success = 1
    try:
        if valgrind:
            cmd = "%s --leak-check=full --error-exitcode=1 --num-callers=30 %s %s" \
                  % (valgrind, valgrind_supp, test)
        else:
            cmd = test

        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                      shell=True, universal_newlines=True)
        suite_log.append(output)
    except subprocess.CalledProcessError as e:
        success = 0

    status[test] = success

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", help="List of tests to run", type=str)
    parser.add_argument("--valgrind", help="Path to valgrind, if provided " \
                        "the tests are run with it", type=str)
    parser.add_argument("--valgrind-supp", help="Path to valgrind's suppression file", type=str)

    args = parser.parse_args()
    if args.valgrind_supp:
        args.valgrind_supp = "--suppressions=%s" % args.valgrind_supp

    for i in args.tests.split():
        run_test(i, args.valgrind, args.valgrind_supp)

    output = ""
    success = 0
    for k,v in sorted(status.items()):
        curr_status = STATUS_TAG[v]
        output += "%s%s:%s %s\n" % (STATUS_COLOR[v], curr_status, ENDC, k)
        STATUS_CNT[curr_status] = STATUS_CNT[curr_status] + 1

    output += "============================================================================\n"
    output += "Testsuite summary\n"
    output += "============================================================================\n"
    output += "# TOTAL: %d\n" % len(status)
    output += "# SUCCESS: %d\n" % STATUS_CNT["PASS"]
    output += "# FAIL: %d\n" % STATUS_CNT["FAIL"]
    output += "============================================================================\n"
    output += "See ./test-suite.log\n"
    output += "============================================================================\n"

    print(output)
    f = open("test-suite.log", mode="w+")
    log_output = ""
    for i in suite_log:
        log_output += "%s\n" % i
    f.write(log_output)
    f.close()
