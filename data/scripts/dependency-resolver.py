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
import json
import os
import subprocess
import sys
import tempfile
from multiprocessing.managers import BaseManager
from shutil import which
from threading import Thread

class DepContext:
    def __init__(self):
        self.cflags = {}
        self.ldflags = {}
        self.kconfig = {}
        self.makefile_vars = {}

    def add_cflags(self, k, v):
        self.cflags[k] = v

    def get_cflags(self):
        return self.cflags

    def add_ldflags(self, k, v):
        self.ldflags[k] = v

    def get_ldflags(self):
        return self.ldflags

    def add_kconfig(self, k, v):
        self.kconfig[k] = v

    def get_kconfig(self):
        return self.kconfig

    def add_makefile(self, k, v):
        self.makefile_vars[k] = v

    def get_makefile(self):
        return self.makefile_vars

class DepContextManager(BaseManager): pass
DepContextManager.register('DepContext', DepContext)

def run_command(cmd):
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                         shell=True, universal_newlines=True)
        return output.replace("\n", "").strip(), True
    except subprocess.CalledProcessError as e:
        return e.output, False


def handle_pkgconfig_check(args, conf, context):
    dep = conf["dependency"].upper()
    pkg = conf["pkgname"]
    atleast_ver = conf.get("atleast-version")
    max_ver = conf.get("max-version")
    exact_ver = conf.get("exact-version")
    ver_match = True

    if exact_ver:
        cmd = "pkg-config --exact-version=%s %s" % (exact_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False
    elif atleast_ver:
        cmd = "pkg-config --atleast-version=%s %s" % (atleast_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False
    elif max_ver:
        cmd = "pkg-config --max-version=%s %s" % (max_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False

    cflags_stat = None
    ldflags_stat = None
    if ver_match:
        cflags_cmd = "pkg-config --cflags %s" % pkg
        ldflags_cmd = "pkg-config --libs %s" % pkg

        cflags, cflags_stat = run_command(cflags_cmd)
        ldflags, ldflags_stat = run_command(ldflags_cmd)

        if cflags_stat:
            context.add_cflags("%s_CFLAGS" % dep, cflags)

        if ldflags_stat:
            context.add_ldflags("%s_LDFLAGS" % dep, ldflags)

    have_var = "y" if ((cflags_stat or ldflags_stat) and ver_match) else "n"
    context.add_kconfig("HAVE_%s" % dep, have_var)

def compile_test(source, compiler, cflags):
    f = tempfile.NamedTemporaryFile(suffix=".c",delete=False)
    f.write(bytes(source, 'UTF-8'))
    f.close()
    output = "%s-bin" % f.name
    cmd = "{compiler} {cflags} {src} -o {out}".format(compiler=compiler, cflags=cflags,
                                                      src=f.name, out=output)
    out, status = run_command(cmd)
    if os.path.exists(output):
        os.unlink(output)
    os.unlink(f.name)

    return status

def handle_ccode_check(args, conf, context):
    dep = conf["dependency"].upper()
    source = ""

    for i in conf.get("headers"):
        source += "#include %s\n" % i

    fragment = conf.get("fragment") or ""
    cstub = "{headers}\nint main(int argc, char **argv){{\n {fragment} return 0;\n}}"
    source = cstub.format(headers=source, fragment=fragment)
    success = compile_test(source, args.compiler, args.cflags)

    if success:
        context.add_cflags("%s_CFLAGS" % dep, conf.get("cflags") or "")
        context.add_ldflags("%s_LDFLAGS" % dep, conf.get("ldflags") or "")
        context.add_kconfig("HAVE_%s" % dep, "y")
    else:
        context.add_kconfig("HAVE_%s" % dep, "n")

def handle_exec_check(args, conf, context):
    dep = conf["dependency"].upper()
    path = which(conf["exec"]) or None

    context.add_makefile(dep, path)

def handle_python_check(args, conf, context):
    dep = conf["dependency"].upper()

    if conf.get("pkgname"):
        source = "import %s" % conf.get("pkgname")

    f = tempfile.NamedTemporaryFile(suffix=".py",delete=False)
    f.write(bytes(source, 'UTF-8'))
    f.close()

    cmd = "%s %s" % (sys.executable, f.name)
    output, status = run_command(cmd)
    context.add_makefile("HAVE_PYTHON_%s" % dep, "y" if status else "n")

type_handlers = {
    "pkg-config": handle_pkgconfig_check,
    "ccode": handle_ccode_check,
    "exec": handle_exec_check,
    "python": handle_python_check,
}

def var_str(items):
    output = ""
    for k,v in items:
        if not v: continue
        output += "%s ?= %s\n" % (k, v)
    return output

def makefile_gen(args, context):
    output = ""
    output += var_str(context.get_makefile().items())
    output += var_str(context.get_cflags().items())
    output += var_str(context.get_ldflags().items())
    f = open(args.makefile_output, "w+")
    f.write(output)
    f.close()

def kconfig_gen(args, context):
    output = ""
    for k,v in context.get_kconfig().items():
        output += "config {config}\n{indent}bool\n{indent}default {enabled}\n". \
                  format(config=k, indent="       ", enabled=v)
    f = open(args.kconfig_output, "w+")
    f.write(output)
    f.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="The gcc compiler[for headers based tests]",
                        type=str, default="gcc")
    parser.add_argument("--cflags", help="Additional cflags[for headers based tests]",
                        type=str, default="")
    parser.add_argument("--kconfig-output", help="The kconfig fragment output file",
                        type=str, default="Kconfig.gen")
    parser.add_argument("--makefile-output", help="The makefile fragment output file",
                        type=str, default="Makefile.gen")
    parser.add_argument("--dep-config", help="The dependencies config file",
                        type=argparse.FileType("r"), default="data/jsons/dependencies.json")

    args = parser.parse_args()
    conf = json.loads(args.dep_config.read())
    dep_checks = conf["dependencies"]

    with DepContextManager() as manager:
        threads = []
        context = manager.DepContext()

        for i in dep_checks:
            handler = type_handlers.get(i["type"])
            if not handler:
                print("Could not handle type: %s" % i["type"])
                continue
            t = Thread(target=handler, args=(args, i, context,))
            t.start()
            threads.append(t)

        for t in threads:
            t.join()

        makefile_gen(args, context)
        kconfig_gen(args, context)
