#!/usr/bin/env python3

# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of Intel Corporation nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from multiprocessing import Manager
from threading import Thread
import argparse
import subprocess
import sys

PASS = '\033[92m'
FAIL = '\033[31m'
ENDC = '\033[0m'

STATUS_COLOR = [FAIL, PASS]
STATUS_TAG = ["FAIL", "PASS"]

def run_test_program(cmd, test, stat, log):
    success = 1
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

VALGRIND_OPTS = "--tool=memcheck --leak-check=full --error-exitcode=1 --num-callers=30"

def run_tests(args):
    manager = Manager()
    stat = manager.dict()
    log = manager.list()
    threads = []
    log_file = "test-suite.log"

    prefix = ""
    if args.valgrind:
        prefix = " ".join([args.valgrind, args.valgrind_supp, VALGRIND_OPTS])
        log_file = "test-suite-memcheck.log"

    for i in args.tests.split():
        cmd = i
        if prefix:
            cmd = prefix + " " + cmd
        t = Thread(target=run_test_program, args=(cmd, i, stat, log,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    print_log("test-suite.log", stat, log)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", help="List of tests to run", type=str)
    parser.add_argument("--valgrind", help="Path to valgrind, if provided " \
                        "the tests are run with it", type=str)
    parser.add_argument("--valgrind-supp", help="Path to valgrind's suppression file", type=str)
    parser.add_argument("--color", metavar="WHEN", type=str,
                        help="Use colors. WHEN can be always, auto and never.")
    parser.set_defaults(color="auto")

    args = parser.parse_args()
    if args.valgrind_supp:
        args.valgrind_supp = "--suppressions=%s" % args.valgrind_supp
    if args.color == "auto":
        if sys.stdout.isatty():
            args.color = "always"
        else:
            args.color = "never"

    if args.color == "never":
        PASS = ''
        FAIL = ''
        ENDC = ''
        STATUS_COLOR = ['', '']

    run_tests(args)
