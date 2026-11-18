#!/usr/bin/env bash
# Rebuild the Qt 6.8.3 Shiboken Python entrypoint. The ARM64 runtime ABI stays the same.
set -euo pipefail
source_dir=$(realpath "$1")
generator=$(realpath "$2")
ndk_bin=$(realpath "$3")
python_prefix=$(realpath "$4")
native=$(realpath "$5")
output=$(realpath "$6")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

test "$(git -C "$source_dir" rev-parse HEAD)" = 3ae6c97953d563c33ff630f259716b01b81aa531
cp -r "$source_dir/sources/shiboken6/shibokenmodule" "$work/source"
# On LP64, the 6.8.3 size_t converter internally calls PyLong_AsLongLong.
# The unsigned-long-long converter uses PyLong_AsUnsignedLongLong and preserves the tag.
sed -i 's/wrapInstance(size_t,/wrapInstance(unsigned long long,/' "$work/source/typesystem_shiboken.xml"
grep -Fq 'wrapInstance(unsigned long long,' "$work/source/typesystem_shiboken.xml"
sed -e "s|@CMAKE_CURRENT_SOURCE_DIR@|$work/source|g" \
    -e "s|@CMAKE_CURRENT_BINARY_DIR@|$work|g" \
    "$work/source/shibokenmodule.txt.in" > "$work/project.txt"
"$generator/shiboken6" --project-file="$work/project.txt"
"$ndk_bin/aarch64-linux-android28-clang++" -O2 -std=c++17 -fPIC -shared -static-libstdc++ \
    -DPy_LIMITED_API=0x03090000 -I"$generator/include" \
    -I"$source_dir/sources/shiboken6/libshiboken" -I"$python_prefix/include/python3.14" \
    "$work/Shiboken/shiboken_module_wrapper.cpp" -L"$native" -L"$python_prefix/lib" \
    -l:libshiboken6.abi3.so -lpython3.14 -Wl,--no-undefined,-z,max-page-size=16384 \
    -o "$work/Shiboken.abi3.so"
"$ndk_bin/llvm-strip" --strip-unneeded "$work/Shiboken.abi3.so"
install -m 644 "$work/Shiboken.abi3.so" "$output"
