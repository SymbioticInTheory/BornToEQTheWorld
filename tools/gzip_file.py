"""gzip_file.py SRC DST: deterministic gzip (used by the CMake build)."""
import gzip
import shutil
import sys

with open(sys.argv[1], 'rb') as src, gzip.GzipFile(sys.argv[2], 'wb', 9, mtime=0) as dst:
    shutil.copyfileobj(src, dst)
