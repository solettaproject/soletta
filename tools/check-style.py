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

from difflib import unified_diff
from shutil import which
import argparse
import os
import re
import subprocess
import sys

DIFF_INC_COLOR = "\033[92m"
DIFF_REM_COLOR = "\033[31m"
DIFF_REF_COLOR = "\033[36m"
END_COLOR = '\033[0m'

UNCRUSTIFY_VERSION = "0.60"

def run_command(cmd):
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                         shell=True, universal_newlines=True)
        return output
    except subprocess.CalledProcessError as e:
        try:
            print("ERROR: %s" %  e.output)
        except BrokenPipeError as e:
            return None
        return None

def check_uncrustify():
    uncrustify = which("uncrustify")
    if not uncrustify:
        print("Uncrustify tool is not present, can't check code format.")
        return None

    version = run_command("%s --version" % uncrustify)
    if not version:
        print("Could not run uncrustify command.")
        return None

    version = version.split()[1]
    if version != UNCRUSTIFY_VERSION:
        print("Uncrustify tool must be at %s version, exactly (found %s)." % \
              (UNCRUSTIFY_VERSION, version))
        return None

    return uncrustify

def check_dirty(args):
    cmd_diff = "git diff --diff-filter=ACMR --oneline --name-only -- '*.[ch]'"
    cmd_cached = "git diff --cached --diff-filter=ACMR --oneline --name-only -- '*.[ch]'"

    diff_list = run_command(cmd_diff)
    diff_list += run_command(cmd_cached)

    if not diff_list:
        return None

    print("Changes for (%s) marked to be checked, but the git tree is dirty " \
          "-- checking these files instead" % args.target_refspec)
    return diff_list

def check_commits(args):
    cmd_check = "git diff --diff-filter=ACMR --oneline --name-only %s -- '*.[ch]'"
    print("Working directory is clean, checking commit changes for (%s)" % args.target_refspec)
    return run_command(cmd_check % args.target_refspec)

def run_check(args, uncrustify, cfg_file, replace):
    diff_list = check_dirty(args)
    if not diff_list:
        diff_list = check_commits(args)

    if not diff_list:
        print("No source files (*.[ch]) changed for: %s" % args.target_refspec)
        return True

    cmd = "%s -c %s %s -l C %s" % \
          (uncrustify, cfg_file, replace, diff_list.replace("\n", " "))
    output = run_command(cmd)
    if (not output) or replace:
        return False

    passed = True
    for f in diff_list.split():
        unc_file = "%s.uncrustify" % f

        if not os.path.exists(unc_file):
            print("WARNING: Expected %s not found, skipping this file." % unc_file)
            continue

        with open(f) as fromf, open(unc_file) as tof:
            fromlines, tolines = list(fromf), list(tof)

        try:
            gen = unified_diff(fromlines, tolines, f, unc_file)
            for ln in gen:
                passed = False
                if args.color == "always":
                    out = re.sub("^\@", "%s@" % DIFF_REF_COLOR, \
                                 re.sub("^\-", "%s-" % DIFF_REM_COLOR, \
                                 re.sub("$", END_COLOR, \
                                 re.sub("^\+", "%s+" % DIFF_INC_COLOR, ln))))
                else:
                    out = ln
                sys.stdout.write(out)
            os.remove(unc_file)
        except KeyboardInterrupt:
            """ ignore keyboard interrupt and simply return """
            return False
    return passed

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-b", help="The base commit", dest="base_commit",
                       type=str, metavar="<base-commit>")
    group.add_argument("-r", help="Use a refspec instead of a base commit",
                       dest="refspec", type=str, metavar="<refspec>")
    parser.add_argument("-c", help="Path for the config file", dest="cfg_file",
                        type=str, metavar="<config>")
    parser.add_argument("--color", metavar="WHEN", type=str, \
                        help="Use colors. WHEN can be always, auto and never.")
    parser.set_defaults(color="auto")
    parser.add_argument("--replace", action='store_true', default=False, \
                        dest='replace', \
                        help="Replace files, no backup. Use at your own risk.")

    args = parser.parse_args()
    if args.base_commit:
        args.target_refspec = "%s..HEAD" % args.base_commit
    elif args.refspec:
        args.target_refspec = args.refspec
    else:
        args.target_refspec = "HEAD~1"

    if args.color == "auto":
        if sys.stdout.isatty():
            args.color = "always"
        else:
            args.color = "never"

    uncrustify = check_uncrustify()
    if not uncrustify:
        exit(1)

    if not args.cfg_file:
        args.cfg_file = "data/schemas/uncrustify.schema"

    replace = ""
    if args.replace:
        replace = "--no-backup"

    if not run_check(args, uncrustify, args.cfg_file, replace):
        exit(1)
