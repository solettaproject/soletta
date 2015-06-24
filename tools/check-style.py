#!/bin/env python3

from shutil import which
import argparse
import os
import subprocess
import sys

DIFF_INC_COLLOR = "\033[92m"
DIFF_REM_COLOR = "\033[31m"
DIFF_REF_COLOR = "\033[36m"

UNCRUSTIFY_VERSION = "0.60"

def run_command(cmd, msg_prefix="ERROR: "):
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                         shell=True, universal_newlines=True)
        return output
    except subprocess.CalledProcessError as e:
        try:
            print("%s%s" % (msg_prefix, e.output))
        except BrokenPipeError as e:
            return None
        return None

def check_diff():
    diff = which("diff")
    if not diff:
        print("Diff tool is not present, can't check code format.")
        return None

    return diff

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

def run_check(args, uncrustify, diff):
    diff_list = check_dirty(args)
    if not diff_list:
        diff_list = check_commits(args)

    if not diff_list:
        print("No source files (*.[ch]) changed for: %s" % args.target_refspec)
        return

    cmd = "%s -c data/schemas/uncrustify.schema -l C %s" % \
          (uncrustify, diff_list.replace("\n", " "))
    output = run_command(cmd)
    if not output:
        return

    for f in diff_list.split():
        unc_file = "%s.uncrustify" % f

        if not os.path.exists(unc_file):
            print("WARNING: Expected %s not found, skipping this file." % unc_file)
            continue

        color_cmd = ""
        if sys.stdout.isatty() and not args.no_colors:
            color_cmd = " | sed 's/^-/{rem_color}-/;s/^+/{inc_color}+/;s/^@/{ref_color}@/;s/$/\x1b[0m/'". \
                        format(rem_color=DIFF_REM_COLOR, inc_color=DIFF_INC_COLLOR, ref_color=DIFF_REF_COLOR)

        diff_output = run_command("%s -u %s %s %s" % (diff, f, unc_file, color_cmd),
                                  "File not properly formatted: %s\n" % f)
        if diff_output:
            print(diff_output)
        os.remove(unc_file)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-b", help="The base commit", dest="base_commit",
                       type=str, metavar="<base-commit>")
    group.add_argument("-r", help="Use a refspec instead of a base commit",
                       dest="refspec", type=str, metavar="<refspec>")
    parser.add_argument("--no-colors", help="Don't display colorized diffs",
                        action="store_true")

    args = parser.parse_args()
    if args.base_commit:
        args.target_refspec = "%s..HEAD" % args.base_commit
    elif args.refspec:
        args.target_refspec = args.refspec
    else:
        args.target_refspec = "HEAD~1"

    uncrustify = check_uncrustify()
    if not uncrustify:
        exit(1)

    diff = check_diff()
    if not diff:
        exit(1)

    run_check(args, uncrustify, diff)
