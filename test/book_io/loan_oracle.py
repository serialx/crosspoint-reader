#!/usr/bin/env python3
"""Compare the original protected-book gate source with the exported Rust gate."""
import argparse
import subprocess
import tempfile
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('archive')
p.add_argument('--capture', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root/'lib/Epub/ContentProtection.cpp').read_text()
if a.capture:
    start = source.index('  if (expiresAt != 0)')
    end = source.index('\n  auto decryptor', start)
    gate = 'void* check(int64_t expiresAt, std::string& err) {\n'+source[start:end]+'\nreturn (void*)1; }'
else:
    gate = '''extern "C" unsigned xp_book_loan(int64_t, void*, int64_t(*)(void*));
void* check(int64_t expiresAt, std::string& err) {
  auto status=xp_book_loan(expiresAt,nullptr,[](void*){return trustedtime::trustedNow();});
  if(status==1) err="loan date unverified";
  if(status==2) err="access expired";
  return status ? nullptr : (void*)1;
}'''
code = '''#include <cstdint>
#include <cstdio>
#include <string>
int64_t clockValue;
namespace trustedtime {int64_t trustedNow(){std::puts("clock");return clockValue;}}
'''+gate+'''
int main(){ for(int64_t expiry : {INT64_MIN,int64_t(-1),int64_t(0),int64_t(1),INT64_MAX})
for(int64_t now : {INT64_MIN,int64_t(-1),int64_t(0),int64_t(1),INT64_MAX}){
clockValue=now;std::string error; auto result=check(expiry,error);
std::printf("%lld %lld %d %s\\n",(long long)expiry,(long long)now,result!=nullptr,error.c_str());}}
'''
with tempfile.TemporaryDirectory() as d:
    cpp=Path(d)/'loan.cpp';cpp.write_text(code)
    exe=Path(d)/'loan'
    subprocess.run(['c++','-std=c++20',str(cpp),a.archive,'-o',str(exe)],check=True)
    actual=subprocess.check_output([str(exe)])
golden=Path(__file__).with_name('loan-golden.txt')
if a.capture: golden.write_bytes(actual)
elif actual!=golden.read_bytes(): raise SystemExit('Loan gate differs from original source')
print('Loan gate: 25 exact result, error, and clock-call traces')
