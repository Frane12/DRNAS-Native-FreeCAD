#!/usr/bin/env python3
"""Host contract test of Android service storage across two hidden-RTTI DSOs.

This exercises the patched upstream header and multiple-inheritance pointer
adjustment. It is not an Android runtime or GUI validation.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def test(source):
    compiler = shutil.which('c++')
    if not compiler:
        raise RuntimeError('Host C++ compiler required for service registry preflight')
    header = Path(source) / 'src/Base/ServiceProvider.h'
    if '#ifdef __ANDROID__\n        void* instance;' not in header.read_text():
        raise RuntimeError('Android service storage patch is missing')
    with tempfile.TemporaryDirectory(prefix='drnas-service-dso-') as directory:
        root = Path(directory)
        (root / 'FCGlobal.h').write_text('#pragma once\n#define BaseExport\n')
        (root / 'Contract.h').write_text('''#pragma once
#include "ServiceProvider.h"
struct Left { virtual ~Left()=default; long padding=123; };
struct Contract { virtual ~Contract()=default; virtual int value()=0; };
struct Other { virtual ~Other()=default; };
''')
        (root / 'producer.cpp').write_text('''#include "Contract.h"
struct Implementation : Left, Contract { int value() override { return 42; } };
extern "C" __attribute__((visibility("default"))) void install(Base::ServiceProvider* registry) {
    static Implementation impl;
    registry->registerImplementation<Contract>(&impl);
    registry->registerImplementation<Contract>(&impl);
}
''')
        (root / 'consumer.cpp').write_text('''#include "Contract.h"
extern "C" __attribute__((visibility("default"))) int verify(Base::ServiceProvider* registry) {
    auto* instance=registry->provide<Contract>();
    if (!instance || instance->value()!=42 || registry->provide<Other>()) return 1;
    auto all=registry->all<Contract>();
    if (all.size()!=2) return 2;
    for (auto* item:all) if (item->value()!=42) return 3;
    return 0;
}
''')
        (root / 'main.cpp').write_text('''#include "Contract.h"
#include <dlfcn.h>
int main(int argc, char** argv) {
    if (argc!=3) return 10;
    void* producer=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    void* consumer=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
    if (!producer || !consumer) return 11;
    auto install=reinterpret_cast<void(*)(Base::ServiceProvider*)>(dlsym(producer,"install"));
    auto verify=reinterpret_cast<int(*)(Base::ServiceProvider*)>(dlsym(consumer,"verify"));
    if (!install || !verify) return 12;
    Base::ServiceProvider registry;
    install(&registry);
    return verify(&registry);
}
''')
        common = [compiler, '-std=c++17', '-D__ANDROID__', '-fvisibility=hidden',
                  '-fvisibility-inlines-hidden', '-I' + str(root), '-I' + str(header.parent)]
        for name in ('producer', 'consumer'):
            subprocess.run(common + ['-shared', '-fPIC', str(root / (name + '.cpp')),
                                     '-o', str(root / (name + '.so'))], check=True)
        subprocess.run(common + [str(root / 'main.cpp'), '-ldl', '-o', str(root / 'verify')], check=True)
        subprocess.run([str(root / 'verify'), str(root / 'producer.so'), str(root / 'consumer.so')], check=True)
    print('PASS: Android registry contract across hidden-RTTI DSOs, adjusted interface pointers and missing-service lookup')


if __name__ == '__main__':
    test(sys.argv[1])
