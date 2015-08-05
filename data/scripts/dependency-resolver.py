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
import pickle
import subprocess
import re
import sys
import tempfile
from shutil import which

cstub = "{headers}\nint main(int argc, char **argv){{\n {fragment} return 0;\n}}"

class DepContext:
    def __init__(self):
        self.kconfig = {}
        self.makefile_vars = {}

    def add_kconfig(self, k, t, v):
        self.kconfig[k] = {"value": v, "type": t}

    def get_kconfig(self):
        return self.kconfig

    def add_makefile_var(self, k, v, attrib, overwrite):
        curr = self.makefile_vars.get(k)
        curr_val = v

        if curr and curr["attrib"] == attrib:
            if overwrite:
                curr_val = v
            else:
                curr_val = "%s %s" % (curr["value"], v)

        self.makefile_vars[k] = {"value": curr_val, "attrib": attrib}

    def add_append_makefile_var(self, k, v, overwrite=False):
        self.add_makefile_var(k, v, "+=", overwrite)

    def add_cond_makefile_var(self, k, v, overwrite=False):
        self.add_makefile_var(k, v, "?=", overwrite)

    def get_makefile_vars(self):
        return self.makefile_vars

    def find_makefile_var(self, v):
        var = self.makefile_vars.get(v)
        if var:
            return var["value"]
        return ""

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
        cmd = "%s --exact-version=%s %s" % (args.pkg_config, exact_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False
    elif atleast_ver:
        cmd = "%s --atleast-version=%s %s" % (args.pkg_config, atleast_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False
    elif max_ver:
        cmd = "%s --max-version=%s %s" % (args.pkg_config, max_ver, pkg)
        result, status = run_command(cmd)
        if not status:
            ver_match = False

    cflags_stat = None
    ldflags_stat = None
    if ver_match:
        cflags_cmd = "%s --cflags %s" % (args.pkg_config, pkg)
        ldflags_cmd = "%s --libs %s" % (args.pkg_config, pkg)

        cflags, cflags_stat = run_command(cflags_cmd)
        ldflags, ldflags_stat = run_command(ldflags_cmd)

        if cflags_stat:
            context.add_cond_makefile_var("%s_CFLAGS" % dep, cflags)

        if ldflags_stat:
            context.add_cond_makefile_var("%s_LDFLAGS" % dep, ldflags)

    success = (cflags_stat or ldflags_stat) and ver_match
    have_var = "y" if success else "n"
    context.add_kconfig("HAVE_%s" % dep, "bool", have_var)

    return success

def compile_test(source, compiler, cflags, ldflags):
    f = tempfile.NamedTemporaryFile(suffix=".c",delete=False)
    f.write(bytes(source, 'UTF-8'))
    f.close()
    output = "%s-bin" % f.name
    cmd = "{compiler} {cflags} {src} -o {out} {ldflags}".format(compiler=compiler,
            cflags=cflags, ldflags=ldflags or "", src=f.name, out=output)
    out, status = run_command(cmd)
    if os.path.exists(output):
        os.unlink(output)
    os.unlink(f.name)

    return status

def set_makefile_compflags(flags, prefix, suffix):
    append_to = flags.get("append_to")
    flag_value = flags.get("value")

    if not flag_value:
        return

    if append_to:
        context.add_append_makefile_var(append_to, flag_value)
    else:
        context.add_cond_makefile_var("%s_%s" % (prefix, suffix),
                                      flag_value)

def handle_ccode_check(args, conf, context):
    dep = conf["dependency"].upper()
    source = ""

    cflags = conf.get("cflags", {})
    ldflags = conf.get("ldflags", {})

    defines = conf.get("defines", [])
    headers = conf.get("headers", [])

    for define in defines:
        source += "#define %s\n" % define

    for header in headers:
        source += "#include %s\n" % header

    common_cflags = context.find_makefile_var(args.common_cflags_var)
    common_ldflags = context.find_makefile_var(args.common_ldflags_var)

    test_cflags = (cflags.get("value", ""), args.cflags, common_cflags)
    test_ldflags = (ldflags.get("value", ""), args.ldflags, common_ldflags)

    fragment = conf.get("fragment") or ""
    source = cstub.format(headers=source, fragment=fragment)
    success = compile_test(source, args.compiler, (" ").join(test_cflags),
                           (" ").join(test_ldflags))

    if success:
        context.add_kconfig("HAVE_%s" % dep, "bool", "y")
        if cflags:
            set_makefile_compflags(cflags, dep, "CFLAGS")
        if ldflags:
            set_makefile_compflags(ldflags, dep, "LDFLAGS")
    else:
        context.add_kconfig("HAVE_%s" % dep, "bool", "n")

    return success


def handle_exec_check(args, conf, context):
    dep = conf.get("dependency")
    dep_sym = dep.upper()
    exe = conf.get("exec")

    if not exe:
        print("Could not parse dependency: %s, no exec was specified." % dep)
        exit(1)

    path = which(exe)
    required = conf.get("required")

    success = bool(path)

    if required and not success:
        req_label = context.find_makefile_var("NOT_FOUND")
        req_label += "executable: %s\\n" % exe
        context.add_append_makefile_var("NOT_FOUND", req_label, True)

    context.add_cond_makefile_var(dep_sym, path)
    context.add_cond_makefile_var("HAVE_%s" % dep_sym, "y" if path else "n")

    return success

def handle_python_check(args, conf, context):
    dep = conf.get("dependency")
    required = conf.get("required", False)
    pkgname = conf.get("pkgname")

    if not pkgname:
        print("Could not parse dependency: %s, no pkgname specified." % dep)
        exit(1)

    source = "import %s" % pkgname

    f = tempfile.NamedTemporaryFile(suffix=".py",delete=False)
    f.write(bytes(source, 'UTF-8'))
    f.close()

    cmd = "%s %s" % (sys.executable, f.name)
    output, status = run_command(cmd)

    success = bool(status)

    if required and not success:
        req_label = context.find_makefile_var("NOT_FOUND")
        req_label += "python module: %s\\n" % pkgname
        context.add_append_makefile_var("NOT_FOUND", req_label, True)

    context.add_cond_makefile_var("HAVE_PYTHON_%s" % dep.upper(), "y" if success else "n")

    return success

def handle_flags_check(args, conf, context, cflags, ldflags):
    append_to = conf.get("append_to")
    source = cstub.format(headers="", fragment="")
    test_cflags = ""
    test_ldflags = ""

    if cflags:
        test_cflags = " ".join(cflags)
        flags = test_cflags
    elif ldflags:
        test_ldflags = " ".join(ldflags)
        flags = test_ldflags
    else:
        print("Neither cflags nor ldflags provided to flags_check.")
        exit(1)

    success = compile_test(source, args.compiler, "-Werror %s" % test_cflags, test_ldflags)
    if success:
        context.add_append_makefile_var(append_to, flags)
        return True

    supported = []
    for i in flags:
        # must acumulate the tested one so we handle dependent flags like -Wformat*
        flags = "%s %s" % (" ".join(supported), i)
        if cflags:
            test_cflags = flags
            test_ldflags = ""
        else:
            test_ldflags = flags
            test_cflags = ""

        success = compile_test(source, args.compiler, "-Werror %s" % test_cflags, test_ldflags)
        if success:
            supported.append(i)

    if supported:
        context.add_append_makefile_var(append_to, " ".join(supported))

    return bool(supported)

def handle_cflags_check(args, conf, context):
    return handle_flags_check(args, conf, context, conf.get("cflags"), None)

def handle_ldflags_check(args, conf, context):
    return handle_flags_check(args, conf, context, None, conf.get("ldflags"))

type_handlers = {
    "pkg-config": handle_pkgconfig_check,
    "ccode": handle_ccode_check,
    "exec": handle_exec_check,
    "python": handle_python_check,
    "cflags": handle_cflags_check,
    "ldflags": handle_ldflags_check,
}

def format_makefile_var(items):
    output = ""
    for k,v in items:
        if not v or not v["value"]: continue
        output += "%s %s %s\n" % (k, v["attrib"], v["value"])
    return output

def makefile_gen(args, context):
    output = format_makefile_var(context.get_makefile_vars().items())
    f = open(args.makefile_output, "w+")
    f.write(output)
    f.close()

def kconfig_gen(args, context):
    output = ""
    for k,v in context.get_kconfig().items():
        output += "config {config}\n{indent}{ktype}\n{indent}default {enabled}\n". \
                  format(config=k, indent="       ", ktype=v["type"], enabled=v["value"])
    f = open(args.kconfig_output, "w+")
    f.write(output)
    f.close()

def is_verbose():
    flag = os.environ.get("V")
    if not flag:
        return False
    try:
        flag = int(flag) != 0
    except ValueError:
        flag = flag.lower() in ("true", "yes", "on", "enabled")
    finally:
        return flag

def run(args, dep_checks, context):
    verbose = is_verbose()

    for dep in dep_checks:
        if verbose:
            s = "Checking for %s%s... " % (dep["dependency"],
                " (optional)" if not dep.get("required") else "")
            print(s, end="")
            sys.stdout.flush()

        handler = type_handlers.get(dep["type"])
        if not handler:
            print("Parsing %s." % args.dep_config.name)
            print("Invalid type: %s at: %s\n" % (dep["type"], dep))
            exit(1)

        result = handler(args, dep, context)
        if verbose:
            print("found." if result else "not found.")

def vars_expand(origin, dest, maxrec):
    remaining = {}

    if not maxrec:
        return

    for k,v in origin.items():
        if not isinstance(v, str):
            dest[k] = v
            continue

        try:
            dest[k] = re.sub("//*", "/", (v.format(**dest)))
        except KeyError:
            remaining[k] = v

    if remaining:
        vars_expand(remaining, dest, maxrec - 1)

def handle_definitions(args, conf, context):
    definitions = conf.get("definitions")

    variables = {"PREFIX": args.prefix}
    vars_expand(definitions, variables, len(definitions))

    header = ""
    for k,v in variables.items():
        if k == "PREFIX":
            continue

        if isinstance(v, str):
            value = "\"%s\"" % v
        else:
            value = "%d" % v
        context.add_cond_makefile_var(k, v, True)
        header += "#define %s %s\n" % (k, value)

    dirname = os.path.dirname(args.definitions_header)
    if not os.path.exists(dirname):
        os.makedirs(dirname)

    f = open(args.definitions_header, "w")
    f.write(header)
    f.close()

def cache_persist(args, context):
    cache = open(args.cache, "wb")
    pickle.dump(context, cache, pickle.HIGHEST_PROTOCOL)
    cache.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="The gcc compiler[for headers based tests]",
                        type=str, default="gcc")
    parser.add_argument("--cflags", help="Additional cflags[for headers based tests]",
                        type=str, default="")
    parser.add_argument("--ldflags", help="Additional/environment ldflags",
                        type=str, default="")
    parser.add_argument("--pkg-config", help="What to use for pkg-config",
                        type=str, default="pkg-config")
    parser.add_argument("--kconfig-output", help="The kconfig fragment output file",
                        type=str, default="Kconfig.gen")
    parser.add_argument("--makefile-output", help="The makefile fragment output file",
                        type=str, default="Makefile.gen")
    parser.add_argument("--dep-config", help="The dependencies config file",
                        type=argparse.FileType("r"), default="data/jsons/dependencies.json")
    parser.add_argument("--common-cflags-var", help=("The makefile variable to "
                                                     "group common cflags"),
                        type=str, default="COMMON_CFLAGS")
    parser.add_argument("--common-ldflags-var", help=("The makefile variable to "
                                                      "group common ldflags"),
                        type=str, default="COMMON_LDFLAGS")
    parser.add_argument("--cache", help="The configuration cache.", type=str,
                        default=".config-cache")
    parser.add_argument("--prefix", help="The installation prefix",
                        type=str, default="/usr")
    parser.add_argument("--definitions-header",
                        help=("File containing definitions propagated to source code"),
                        type=str,
                        default="include/generated/sol_definitions.h")
    parser.add_argument("--makefile-gen", help="Should generate Makefile.gen?",
                        action="store_true")
    parser.add_argument("--kconfig-gen", help="Should generate Kconfig.gen?",
                        action="store_true")

    args = parser.parse_args()

    context = None
    conf = json.loads(args.dep_config.read())
    if os.path.isfile(args.cache):
        cache = open(args.cache, "rb")
        context = pickle.load(cache)
        cache.close()
    else:
        dep_checks = conf.get("dependencies")
        pre_checks = conf.get("pre-dependencies")

        context = DepContext()

        run(args, pre_checks, context)
        run(args, dep_checks, context)
        cache_persist(args, context)

    if args.makefile_gen:
        handle_definitions(args, conf, context)
        makefile_gen(args, context)
        cache_persist(args, context)

    if args.kconfig_gen:
        kconfig_gen(args, context)
