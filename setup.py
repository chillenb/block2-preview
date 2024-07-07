#!/usr/bin/env python3

import os
import shutil
import sys
import platform
from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext

class PackAlreadyBuilt(build_ext):
    def build_extensions(self):
        print("Python3: ", sys.executable)
        print("Build Dir: ", self.build_temp)

        build_dir = os.path.abspath(os.environ['BLOCK2_BUILD_DIR'])

        for ext in self.extensions:
            extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

            if not os.path.exists(extdir):
                os.makedirs(extdir)


            srcfile = os.path.join(build_dir, self.get_ext_filename(ext.name))
            shutil.copy(srcfile, extdir)
            print(f"copied {srcfile} to {extdir}")


setup(
    name="block2",
    version="0.1.10",
    packages=find_packages(),
    ext_modules=[Extension("block2", [])],
    cmdclass={"build_ext": PackAlreadyBuilt},
    license="LICENSE",
    description="""An efficient MPO implementation of DMRG for quantum chemistry.""",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    author="Huanchen Zhai, Henrik R. Larsson, Seunghoon Lee, and Zhi-Hao Cui",
    author_email="hczhai.ok@gmail.com",
    url="https://github.com/block-hczhai/block2-preview",
    install_requires=[
        "mkl==2021.4",
        "mkl-include",
        "intel-openmp",
        "numpy",
        "cmake>=3.19",
        "scipy",
        "psutil",
        "pybind11<=2.10.1",
    ],
    scripts=[
        "pyblock2/driver/block2main",
        "pyblock2/driver/gaopt",
        "pyblock2/driver/readwfn.py",
        "pyblock2/driver/writewfn.py",
    ],
)

