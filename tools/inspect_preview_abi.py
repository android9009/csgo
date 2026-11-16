#!/usr/bin/env python3
"""Offline PE32 RTTI inspection. Never loads/executes a supplied DLL.
Install pefile in a virtual environment, then pass DLL paths on the command line.
Reported vtables are candidates, NOT callable runtime interfaces.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import pefile

CLASSES = ('CCustomMaterialOwner', 'CCSWeaponVisualsDataProcessor', 'CCustomMaterialManager')


def occurrences(data, pattern):
    pos = 0
    while True:
        pos = data.find(pattern, pos)
        if pos < 0:
            return
        yield pos
        pos += 1


def inspect(path):
    raw = path.read_bytes()
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.Magic != 0x10b:
        raise ValueError(f'{path.name}: expected x86 PE32')
    data = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase

    def executable(va):
        rva = va - base
        return any(s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize
                   and s.Characteristics & 0x20000000 for s in pe.sections)

    result = dict(file=path.name, sha256=hashlib.sha256(raw).hexdigest(),
                  timestamp=hex(pe.FILE_HEADER.TimeDateStamp), classes={})
    for name in CLASSES:
        candidates = []
        for name_rva in occurrences(data, ('.?AV' + name + '@@\0').encode()):
            type_rva = name_rva - 8  # TypeDescriptor: two pointers, then mangled name
            for type_ref in occurrences(data, struct.pack('<I', base + type_rva)):
                locator = type_ref - 12
                if locator < 0 or locator + 20 > len(data):
                    continue
                signature, offset, cd_offset, td, hierarchy = struct.unpack_from('<5I', data, locator)
                if signature != 0 or td != base + type_rva or not base <= hierarchy < base + len(data):
                    continue
                for locator_ref in occurrences(data, struct.pack('<I', base + locator)):
                    vtable = locator_ref + 4
                    # Stop at the next RTTI locator/data word, not an arbitrary method count.
                    methods = []
                    for i in range(32):
                        pos = vtable + i * 4
                        if pos + 4 > len(data):
                            break
                        target = struct.unpack_from('<I', data, pos)[0]
                        if not executable(target):
                            break
                        methods.append(hex(target - base))
                    if methods:
                        candidates.append(dict(type_rva=hex(type_rva), locator_rva=hex(locator),
                                               subobject_offset=hex(offset), vtable_rva=hex(vtable),
                                               method_rvas=methods))
        if candidates:
            result['classes'][name] = candidates
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path, nargs='+')
    args = parser.parse_args()
    print(json.dumps([inspect(path) for path in args.dll], indent=2))
