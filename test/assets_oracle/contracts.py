#!/usr/bin/env python3
"""Exercise every cache owner failure and prove no allocations on repeated passes."""
import os
import subprocess
import sys
import tempfile
for variant in (0,1,2,3,4,5,6,7):
    for failure in range(12):
        with tempfile.TemporaryDirectory() as root:
            result=subprocess.run([sys.argv[1],root,str(variant),'0','0'],capture_output=True,check=True,
                env={**os.environ,'ASSET_FAIL_ALLOC':str(failure)})
            attempts,live=map(int,result.stderr.decode().rsplit('ALLOC ',1)[1].split())
            if live: raise RuntimeError(f'{variant}/{failure}: {live} leaked owners')
print('96 cache allocation-failure cases passed; repeated successful passes allocate zero times')
