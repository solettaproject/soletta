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

import argparse
import os
import queue
import subprocess
import sys
import threading

PASS_COLOR = '\033[92m'
FAIL_COLOR = '\033[31m'
ENDC_COLOR = '\033[0m'

class Task:
    def __init__(self, name, cmd):
        self.name = name
        self.cmd = cmd
        self.success = False
        self.output = ""

def run_test_program(task_queue):
    while not task_queue.empty():
        try:
            task = task_queue.get_nowait()
        except queue.Empty:
            break

        try:
            output = subprocess.check_output(task.cmd, stderr=subprocess.STDOUT,
                                             shell=True, universal_newlines=True)
            task.success = True
            task.output = output
            tag = "%sPASS:%s" % (PASS_COLOR, ENDC_COLOR)
        except subprocess.CalledProcessError as e:
            task.success = False
            task.output = e.output
            tag = "%sFAIL:%s" % (FAIL_COLOR, ENDC_COLOR)
        print("%s %s" % (tag, task.name))

        task_queue.task_done()

def print_log(log_file, tasks):
    success_count = 0
    fail_count = 0

    f = open(log_file, mode="w+")

    for task in tasks:
        f.write("# Running " + task.cmd + "\n")
        f.write(task.output)
        f.write("\n")
        if task.success:
            success_count += 1
        else:
            fail_count += 1

    summary = """\
============================================================================
Testsuite summary
============================================================================
# TOTAL: %d
# SUCCESS: %d
# FAIL: %d
============================================================================
See %s
============================================================================"""
    summary = summary % (len(tasks), success_count, fail_count, log_file)

    f.write(summary)
    f.close()
    print(summary)

VALGRIND_OPTS = "--tool=memcheck --leak-check=full --error-exitcode=1 --num-callers=30"

def run_tests(args):
    log_file = "test-suite.log"
    tasks = []
    task_queue = queue.Queue()

    prefix = ""
    if args.valgrind:
        prefix = " ".join([args.valgrind, args.valgrind_supp, VALGRIND_OPTS])
        log_file = "test-suite-memcheck.log"

    for test in args.tests.split():
        cmd = test
        if prefix:
            cmd = prefix + " " + cmd
        task = Task(test, cmd)
        tasks.append(task)
        task_queue.put(task)

    for i in range(os.cpu_count()):
        threading.Thread(target=run_test_program, args=(task_queue,)).start()

    task_queue.join()
    print_log(log_file, tasks)

    for task in tasks:
        if not task.success:
            sys.exit(1)

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
        PASS_COLOR = ''
        FAIL_COLOR = ''
        ENDC_COLOR = ''

    run_tests(args)
