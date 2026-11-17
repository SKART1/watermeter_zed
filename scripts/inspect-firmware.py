#!/usr/bin/env python3
"""Read BIN/OTA metadata and validate sizes/CRC without modifying images."""
import argparse
import binascii
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    result = {"file": str(path), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    firmware = data
    if data[:4] == bytes.fromhex("1ef1ee0b"):
        magic, version, length, control, manufacturer, image_type, file_version, stack, title, total = struct.unpack_from("<I5HIH32sI", data)
        tag, payload_length = struct.unpack_from("<HI", data, length)
        if length != 56 or control != 0 or tag != 0 or total != len(data) or payload_length != len(data) - length - 6:
            raise ValueError(f"Unexpected OTA structure: {path}")
        firmware = data[length + 6:]
        result["ota"] = {"header_version": hex(version), "header_length": length, "manufacturer": hex(manufacturer), "image_type": hex(image_type), "file_version": hex(file_version), "stack_version": stack, "total_size": total, "payload_size": payload_length}
    if len(firmware) < 32:
        raise ValueError(f"Short firmware: {path}")
    raw_version = int.from_bytes(firmware[2:6], "little")
    raw_manufacturer = int.from_bytes(firmware[18:20], "little")
    raw_image_type = int.from_bytes(firmware[20:22], "little")
    declared_size = int.from_bytes(firmware[24:28], "little")
    crc_ok = (binascii.crc32(firmware[:-4]) ^ 0xFFFFFFFF) == int.from_bytes(firmware[-4:], "little")
    if firmware[6:8] != bytes.fromhex("5d02") or declared_size != len(firmware) or not crc_ok:
        raise ValueError(f"Firmware marker/size/CRC failed: {path}")
    if "ota" in result and (raw_version != file_version or raw_manufacturer != manufacturer or raw_image_type != image_type):
        raise ValueError(f"OTA and firmware metadata differ: {path}")
    result["firmware"] = {"size": len(firmware), "sha256": hashlib.sha256(firmware).hexdigest(), "first_32_bytes": firmware[:32].hex(), "file_version": hex(raw_version), "manufacturer": hex(raw_manufacturer), "image_type": hex(raw_image_type), "declared_size": declared_size, "crc_valid": crc_ok, "date_code_20250324_occurrences": firmware.count(b"20250324")}
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps([inspect(path) for path in args.images], indent=2))
