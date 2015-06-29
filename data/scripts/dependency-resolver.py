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
import subprocess
import sys
import tempfile
from shutil import which

pkg_config_deps = {
    "CHECK": "check",
    "GLIB": "glib-2.0",
    "GTK": "gtk+-3.0",
    "SYSTEMD": "libsystemd",
    "SYSTEMD_JOURNAL": "libsystemd-journal",
    "UDEV": "libudev",
}

header_deps = {
    "KDBUS": "<systemd/sd-bus.h>",
    "PTHREAD": "<pthread.h>",
    "RIOTOS": "<riotos/cpu.h>",
    "DLFCN_H": "<dlfcn.h>",
    "INTTYPES_H": "<inttypes.h>",
    "LINUX_NETLINK_H": "<linux/netlink.h>",
    "LINUX_RTNETLINK_H": "<linux/rtnetlink.h>",
    "LINUX_WATCHDOG_H": "<linux/watchdog.h>",
    "MEMORY_H": "<memory.h>",
    "PTHREAD_H": "<pthread.h>",
    "STDINT_H": "<stdint.h>",
    "STDLIB_H": "<stdlib.h>",
    "STRINGS_H": "<strings.h>",
    "STRING_H": "<string.h>",
    "SYS_STAT_H": "<sys/stat.h>",
    "SYS_TYPES_H": "<sys/types.h>",
    "UNISTD_H": "<unistd.h>",
}

exec_deps = {
    "VALGRIND": "valgrind",
    "LCOV": "lcov",
    "GENHTML": "genhtml",
}

cfragment_deps = {
    "ACCEPT4": {"fragment": "accept4(0, 0, 0, 0);", "headers": ["<sys/socket.h>"]},
    "ISATTY": {"fragment": "isatty(0);", "headers": ["<unistd.h>"]},
    "PPOLL": {"fragment": "ppoll(0, 0, 0, 0);", "headers": ["<poll.h>"]},
    "DECL_STRNDUPA": {"fragment": "strndupa(0, 0);", "headers": ["<string.h>"]},
    "DECL_IFLA_INET6_ADDR_GEN_MODE": {"fragment": "int v = IFLA_INET6_ADDR_GEN_MODE;", \
                                      "headers": ["<netinet/in.h>", "<netinet/ether.h>", \
                                                  "<linux/rtnetlink.h>", "<net/if.h>" \
                                                  "<linux/if_link.h>", "<linux/if_addr.h>"]},
}

python_pkg_deps = {
    "PYTHON_JSONSCHEMA" : "jsonschema"
}

makefile_gen = []
kconfig_gen = []

compiler = "gcc"
cflags = ""

def kconfig_add(key, enabled):
    item = "config {config}\n{indent}bool\n{indent}default {enabled}". \
           format(config="HAVE_%s" % key, indent="       ", enabled=enabled)
    kconfig_gen.append(item)

def pkg_config_discover():
    for k,v in pkg_config_deps.items():
        cmd = "pkg-config --libs %s --cflags" % v
        enabled = "n"
        flags = ""
        try:
            flags = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                            shell=True, universal_newlines=True)
            flags = flags.replace("\n", "")
            enabled = "y"
        except subprocess.CalledProcessError as e:
            flags = None

        if flags is not None:
            line = "ifeq (y,$(USE_%s))\n" % k
            line = "%s%s_CFLAGS += %s\n" % (line, k, flags)
            line = "%sendif\n" % line
            makefile_gen.append(line)

        kconfig_add(k, enabled)

def python_dep_test():
    for k,v in python_pkg_deps.items():
        found = "y"
        source = "import jsonschema"
        f = tempfile.NamedTemporaryFile(suffix=".py",delete=False)
        f.write(bytes(source, 'UTF-8'))
        f.close()

        try:
            cmd = "%s %s" % (sys.executable, f.name)
            subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                    shell=True, universal_newlines=True)
        except subprocess.CalledProcessError as e:
            found = "n"

        line = "HAVE_%s := %s\n" % (k, found)
        makefile_gen.append(line)
        os.unlink(f.name)

def fragment_dep_test():
    for k,v in cfragment_deps.items():
        enabled = "y"

        headers = ""
        for h in v["headers"]:
            headers = "%s#include %s\n" %(headers, h)

        source = "%s\nint main(int argc, char **argv){\n %s return 0;\n}" % (headers, v["fragment"])
        f = tempfile.NamedTemporaryFile(suffix=".c",delete=False)
        f.write(bytes(source, 'UTF-8'))
        f.close()

        try:
            output = "%s-bin" % f.name
            cmd = "{compiler} {cflags} {src} -o {out}".format(compiler=compiler, cflags=cflags,
                                                              src=f.name, out=output)
            subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                    shell=True, universal_newlines=True)
            os.unlink(output)
        except subprocess.CalledProcessError as e:
            enabled = "n"

        os.unlink(f.name)
        kconfig_add(k, enabled)

def header_dep_test():
    for k,v in header_deps.items():
        enabled = "y"
        source = "#include %s\nint main(int argc, char **argv){\n    return 0;\n}" % v
        f = tempfile.NamedTemporaryFile(suffix=".c",delete=False)
        f.write(bytes(source, 'UTF-8'))
        f.close()

        try:
            output = "%s-bin" % f.name
            cmd = "{compiler} {cflags} {src} -o {out}".format(compiler=compiler, cflags=cflags,
                                                              src=f.name, out=output)
            subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                    shell=True, universal_newlines=True)
            os.unlink(output)
        except subprocess.CalledProcessError as e:
            enabled = "n"

        os.unlink(f.name)
        kconfig_add(k, enabled)

def exec_discover():
    for k,v in exec_deps.items():
        path = which(v) or ""
        
        line = "%s ?= %s\n" % (k, path)
        makefile_gen.append(line)

if __name__ == "__main__":
    kconfig = "Kconfig.gen"
    makefile = "Makefile.gen"

    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="The gcc compiler[for headers based tests]", type=str)
    parser.add_argument("--cflags", help="Additional cflags[for headers based tests]", type=str)
    parser.add_argument("--kconfig-output", help="The kconfig fragment output file", type=str)
    parser.add_argument("--makefile-output", help="The makefile fragment output file", type=str)

    args = parser.parse_args()

    if (args.compiler):
        compiler = args.compiler

    if (args.cflags):
        cflags = args.cflags

    if (args.kconfig_output):
        kconfig = args.kconfig_output

    pkg_config_discover()
    header_dep_test()
    fragment_dep_test()
    exec_discover()
    python_dep_test()

    fragment = ""
    for i in kconfig_gen:
        fragment += "%s\n" % i

    f = open(kconfig, mode="w+")
    f.write(fragment)
    f.close()

    fragment = ""
    for i in makefile_gen:
        fragment += "%s\n" % i

    f = open(makefile, mode="w+")
    f.write(fragment)
    f.close()
