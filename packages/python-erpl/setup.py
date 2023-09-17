import os
import os.path as osp
import shutil

from glob import glob
from pathlib import Path
from setuptools import setup, find_packages

platform_name = os.sys.platform
current_dir = Path(__file__).parent.absolute()
tgt_dir = "erpl"

def join_current_dir(*paths):
    return osp.abspath(osp.join(current_dir, *paths))

def clear_binaries():
    shutil.rmtree(join_current_dir(tgt_dir), ignore_errors=True)

def copy_binaries(*src_paths):
    os.makedirs(join_current_dir(tgt_dir), exist_ok=True)
    for sp in src_paths:
        for f in glob(sp):
            print("Copying {} to {}".format(f, join_current_dir(tgt_dir)))
            shutil.copy(f, join_current_dir(tgt_dir))

def copy_binaries_for_platform(platform_name):
    # Set the correct binary path
    if platform_name == "linux":
        copy_binaries(join_current_dir("../../nwrfcsdk/lib/*.so*"),
                    join_current_dir("../../build/release/extension/erpel/erpel.duckdb_extension"))
    elif platform_name == "win32":
        raise Exception("Windows is not supported yet")
    elif platform_name == "darwin":  # macOS
        raise Exception("macOS is not supported yet")
    else:
        raise Exception("Unsupported platform: {}".format(platform_name))

clear_binaries()
copy_binaries_for_platform(platform_name)


setup(
    name="python-erpl",
    version="0.1",
    packages=find_packages(),
    package_data={
        "python-erpl": [join_current_dir(tgt_dir, "*")]
    },
    include_package_data=True,
    install_requires=[ ],
)
