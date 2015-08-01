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

import configparser
import os
import re
import subprocess
import sys

def run_command(cmd):
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                         shell=True, universal_newlines=True)
        return output.replace("\n", "").strip(), True
    except subprocess.CalledProcessError as e:
        return e.output, False

def submodule_clone(name, path, url, branch, depth, dest, sparserules, single_branch):
    if os.path.exists(dest):
        return None, True

    print("GIT: cloning submodule %s ..." % name, end="")
    sys.stdout.flush()

    if single_branch:
        out, status = run_command("git clone {url} {branch} {depth} --bare {dest}".format(
                url=url, branch=branch, depth=depth, dest=dest))
    else:
        # clone it all if we don't support single branch checkout
        out, status = run_command("git clone {url} --bare {dest}".format(
                url=url, dest=dest))

    if sparserules:
        submodule_config = "%s/config" % dest
        rules_dir = "%s/info" % dest

        if not os.path.exists(rules_dir):
            os.makedirs(rules_dir)

        rules_file = "%s/info/sparse-checkout" % dest

        config = configparser.ConfigParser()
        config.read(submodule_config)
        config.set("core", "sparsecheckout", 'true')
        config.set("core", "bare", 'false')
        with open(submodule_config, "w") as fd:
            config.write(fd)

        content = ""
        for ln in sparserules.split():
            content += "%s\n" % ln

        with open(rules_file, "w") as fd:
            fd.write(content)

    run_command("git submodule init " + path)

    print("[done]")
    return out, status

def submodule_checkout(name, submodule_git, path, branch, single_branch):
    git_link = "%s/.git" % path

    if os.path.exists(git_link):
        os.remove(git_link)

    ddepth = len(path.split("/"))
    with open(git_link, "w") as git:
        git.write("gitdir: %s%s" % ("../" * ddepth, submodule_git))

    if single_branch:
        run_command("cd {path} && git checkout -f".format(path=path))
    else:
        run_command("cd {path} && git checkout -f {branch}".format(path=path,branch=branch))

def git_single_branch():
    git_version = list(map(int, out.replace("git version ", "").split(".")))
    major = git_version[0]
    minor = git_version[1]
    release = git_version[2]
    return (major >= 1 and minor >= 7 and release >= 10)

if __name__ == "__main__":
    gitmodules = ".gitmodules"
    if not os.path.exists(gitmodules):
        exit(0)

    config = configparser.ConfigParser()
    config["DEFAULT"] = {"sparsecheckout": False, "sparserules": "", "branch": ""}
    config.read(gitmodules)

    out, status = run_command("git --version")
    if not status:
        print("Could not check git version")
        exit(1)

    # does git support single branch checkout?
    single_branch = git_single_branch()

    for k,v in config.items():
        if not k.startswith("submodule"):
            continue

        name = k.replace("submodule ", "").replace("\"", "")
        path = config.get(k, "path")
        url = config.get(k, "url")
        sparsecheckout = config.get(k, "sparsecheckout")
        sparserules = config.get(k, "sparserules")
        branch = config.get(k, "branch")

        if not name and not path:
            print("ERROR: Can't initialize submodule: %d, missing either name or path." % name)
            exit(1)

        submodule_git = ".git/modules/%s" % name
        depth = "--depth 1" if sparsecheckout else ""
        branch_arg = "-b %s" % branch if branch else ""
        out, status = submodule_clone(name, path, url, branch_arg, depth, submodule_git,
                                      sparserules, single_branch)

        if not status:
            print("ERROR: Failed to clone submodule: %s" % name)
            print("ERROR: %s" % out)
            exit(1)

        submodule_checkout(name, submodule_git, path, branch, single_branch)
