#!/usr/bin/env python3
"""Check legacy Osiris inventory signatures offline; never load the supplied DLL.
Requires pefile. Unique matches alone do NOT validate layouts/calling conventions.
Reference: danielkrupinski/Osiris a20c93a2a913f90464d8cfb49716e3a5e6d8c7ab,
Source/BytePatterns/WindowsClientPatternFinder.h (MIT).
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import pefile

PATTERNS = {
    "inventory_manager_operand_at_5": "8D 44 24 28 B9 ? ? ? ? 50",
    "econ_item_factory_pointer_at_20": "55 8B EC 83 EC 1C 8D 45 E4 C7 45",
    "create_base_type_cache_rel32_at_1": "E8 ? ? ? ? 8D 4D 0F",
    "set_dynamic_attribute": "55 8B EC 83 E4 F8 83 EC 3C 53 8B 5D 08 56 57 6A",
    "set_custom_name_rel32_at_1": "E8 ? ? ? ? 8B 46 78 C1 E8 0A A8 01 74 13 8B 46 34",
}


def inspect(path):
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    result = {"sha256": hashlib.sha256(data).hexdigest(),
              "timestamp": hex(pe.FILE_HEADER.TimeDateStamp), "matches": {}}
    for name, signature in PATTERNS.items():
        expression = b"".join(b"." if t == "?" else re.escape(bytes([int(t, 16)]))
                              for t in signature.split())
        pattern = re.compile(b"(?=(" + expression + b"))", re.DOTALL)
        hits = []
        for section in pe.sections:
            if section.Characteristics & 0x20000000:
                hits.extend(hex(section.VirtualAddress + match.start())
                            for match in pattern.finditer(section.get_data()))
        result["matches"][name] = hits
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.client), indent=2))
