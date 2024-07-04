import os
from functools import cache
import importlib

@cache
def choose_build_backend():
    name = os.environ.get("BLOCK2_BUILD_BACKEND", "")
    name = name.lower().replace("_", "-")
    if name in ("skbuild", "scikit-build-core"):
        print("Using scikit-build-core backend")
        return "scikit_build_core.build"

    print("Using setuptools backend")
    return "setuptools.build_meta"

def import_build_backend():
    return importlib.import_module(choose_build_backend())

def build_wheel(*args, **kwargs):
    return import_build_backend().build_wheel(*args, **kwargs)

def build_sdist(*args, **kwargs):
    return import_build_backend().build_sdist(*args, **kwargs)

def get_requires_for_build_wheel(*args, **kwargs):
    return import_build_backend().get_requires_for_build_wheel(*args, **kwargs)

def get_requires_for_build_sdist(*args, **kwargs):
    return import_build_backend().get_requires_for_build_sdist(*args, **kwargs)

def prepare_metadata_for_build_wheel(*args, **kwargs):
    return import_build_backend().prepare_metadata_for_build_wheel(*args, **kwargs)
