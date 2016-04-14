#!/usr/bin/python3

from distutils.core import setup
from distutils.extension import Extension
from Cython.Build import cythonize
import os
import sys

using_soletta_build = False

src_root = "./"
install_prefix= "/usr/"
lib_dir = os.path.join(install_prefix, 'lib')
include_dir = os.path.join(install_prefix, "include", "soletta")
install_root = "/"

if "SOLETTA_SRC_ROOT" in os.environ:
    src_root = os.environ["SOLETTA_SRC_ROOT"]
    using_soletta_build = True

if "PREFIX" in os.environ:
    install_prefix = os.environ["PREFIX"]

if "LIBDIR" in os.environ:
    lib_dir = os.environ["LIBDIR"]

if "SOL_INCLUDEDIR" in os.environ:
    include_dir = os.environ["SOL_INCLUDEDIR"]
    using_soletta_build = True

if "SOLETTA_INSTALL_ROOT" in os.environ:
    install_root = os.environ["SOLETTA_INSTALL_ROOT"]

bindings_dir = ""

if using_soletta_build:
    bindings_dir = os.path.join(src_root, "bindings", "python")

soletta_prefix = ""#os.path.join(install_root, install_prefix)
asyncio_prefix =  os.path.join(bindings_dir, "soletta", "asyncio")
tests_prefix = os.path.join(bindings_dir, "soletta", "tests")
soletta_srcs = [os.path.join(asyncio_prefix, src) for src in ["_soletta_mainloop.pyx"]]
test_srcs = [os.path.join(tests_prefix, src) for src in ["_soletta_tests.pyx"]]

extensions = [
    Extension(os.path.join(soletta_prefix, "_soletta_mainloop"), soletta_srcs,
        include_dirs = ['.', include_dir],
        libraries = ["soletta"],
        library_dirs = [lib_dir]),
    Extension(os.path.join(tests_prefix, "_soletta_tests"), test_srcs,
        include_dirs = ['.', include_dir],
        libraries = ["soletta"],
        library_dirs = [lib_dir])
]

pkg_dir = {}

soletta_pkg = os.path.join("soletta", "asyncio")

if "./" in soletta_pkg:
    soletta_pkg = soletta_pkg[2:]

pkg_dir[soletta_pkg] = bindings_dir

tests_pkg = os.path.join("soletta", "tests")

if "./" in tests_pkg:
    tests_pkg = tests_pkg[2:]

pkg_dir[tests_pkg] = bindings_dir

packages = [soletta_pkg, tests_pkg]


print("""Soletta Python Build
prefix: {prefix}
src_root: {src_root}
include_dir: {include_dir}
libdir: {libdir}
bindings_dir: {bindings_dir}
soletta_build_dir: {soletta_prefix}
python modules: 
{modules}

install root: {install_root}

""".format(prefix=install_prefix, soletta_prefix=soletta_prefix, src_root=src_root, include_dir=include_dir, libdir=lib_dir, bindings_dir=bindings_dir, install_root = install_root, modules="\n".join(packages)))

setup(
    name = "soletta",
    package_dir = pkg_dir,
    packages = packages,
    ext_modules = cythonize(extensions)
)
