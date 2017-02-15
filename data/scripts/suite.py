#!/usr/bin/env python3

# This file is part of the Soletta (TM) Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import multiprocessing
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

def run_test_program(task_queue, task_empty_exception):
    while not task_queue.empty():
        try:
            task = task_queue.get_nowait()
        except task_empty_exception:
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

def writeOutput(task, f):
    f.write("# Running " + task.cmd + "\n")
    f.write(task.output)
    f.write("\n")

def print_log(log_file, tasks):
    success_count = 0
    fail_count = 0
    fail_header = False

    f = open(log_file, mode="w+")

    for task in tasks:
        writeOutput(task, f)
        if task.success:
            success_count += 1
        else:
            if not fail_header:
                print("""\
============================================================================
Failed tests output
============================================================================""")
                fail_header = True
            writeOutput(task, sys.stdout)
            fail_count += 1

    summary = """\
============================================================================
Testsuite summary
============================================================================
# TOTAL: %d
# SUCCESS: %d
# FAIL: %d
============================================================================
See %s for full output.
============================================================================"""
    summary = summary % (len(tasks), success_count, fail_count, log_file)

    f.write(summary)
    f.close()
    print(summary)

VALGRIND_OPTS = "--tool=memcheck --leak-check=full --error-exitcode=1 --num-callers=30"

def run_tests(args):
    log_file = "test-suite.log"
    tasks = []

    try:
        import queue
        task_queue = queue.Queue()
        task_empty_exception = queue.Empty
    except ImportError:
        import Queue
        task_queue = Queue.Queue()
        task_empty_exception = Queue.Empty

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

    for i in range(multiprocessing.cpu_count()):
        threading.Thread(target=run_test_program,
                         args=(task_queue, task_empty_exception)).start()

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
