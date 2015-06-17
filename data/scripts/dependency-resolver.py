#!/usr/bin/env python3

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
    "UDEV": "libudev",
}

header_deps = {
    "KDBUS": "<systemd/sd-bus.h>",
    "PTHREAD": "<pthread.h>",
    "RIOTOS": "<riotos/cpu.h>",
}

exec_deps = {
    "VALGRIND": "valgrind",
    "LCOV": "lcov",
    "GENHTML": "genhtml",
}

python_pkg_deps = {
    "HAVE_PYTHON_JSONSCHEMA" : "jsonschema"
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
            line = "ifeq (y,$(CONFIG_USE_%s))\n" % k
            line = "%sDEP_CFLAGS += %s\n" % (line, flags)
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
