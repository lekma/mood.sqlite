# -*- coding: utf-8 -*-


from setuptools import setup, find_packages, Extension
from distutils.version import LooseVersion

from ctypes.util import find_library
from ctypes import cdll, c_char_p

from codecs import open
from os.path import abspath
from sys import argv


# pkg
pkg_name = "mood.sqlite"
pkg_version = "2.0.0"
pkg_desc = "mood sqlite module"

PKG_VERSION = ("PKG_VERSION", "\"{0}\"".format(pkg_version))

err_msg = "Aborted: {0}-{1} requires {{0}} >= {{1}}".format(pkg_name, pkg_version)

def check_version(current_version, minimum_version, name):
    if (
        (not current_version) or
        ((LooseVersion(current_version) < LooseVersion(minimum_version)))
    ):
        raise SystemExit(err_msg.format(name, minimum_version))


# sqlite3
sqlite3_name = "sqlite3"
sqlite3_min_version = "3.44.1"

def sqlite3_version():
    sqlite3_dll_name = find_library(sqlite3_name)
    if sqlite3_dll_name:
        sqlite3_dll = cdll.LoadLibrary(sqlite3_dll_name)
        sqlite3_dll.sqlite3_libversion.restype = c_char_p
        return sqlite3_dll.sqlite3_libversion().decode()


# setup
if "sdist" not in argv:
    check_version(sqlite3_version(), sqlite3_min_version, "sqlite3")

setup(
    name=pkg_name,
    version=pkg_version,
    description=pkg_desc,
    long_description=open(abspath("README.txt"), encoding="utf-8").read(),
    long_description_content_type="text",

    url="https://github.com/lekma/mood.sqlite",
    download_url="https://github.com/lekma/mood.sqlite/releases",
    project_urls={
        "Bug Tracker": "https://github.com/lekma/mood.sqlite/issues"
    },
    author="Malek Hadj-Ali",
    author_email="lekmalek@gmail.com",
    license="The Unlicense (Unlicense)",
    keywords="sqlite",

    setup_requires = ["setuptools>=24.2.0"],
    python_requires="~=3.10",
    packages=find_packages(),
    namespace_packages=["mood"],
    zip_safe=False,

    ext_package="mood",
    ext_modules=[
        Extension(
            "sqlite",
            [
                "src/helpers/helpers.c",
                "src/sqlite.c",
            ],
            define_macros=[PKG_VERSION],
            libraries=[sqlite3_name]
        )
    ],

    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: The Unlicense (Unlicense)",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: Implementation :: CPython"
    ]
)
