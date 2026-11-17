#!/usr/bin/env python3
"""Install pinned Linux build tools inside ignored build/, without root or apt install."""
import hashlib
import json
import platform
import subprocess
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = ROOT / "build/downloads"
TOOLS = ROOT / "build/tools"
PACKAGES = (
    {
        "file": "tc32_130_Linux.tar.xz",
        "url": "https://doc.telink-semi.cn/tools/vsc/linux/toolchains/tc32_130_Linux.tar.xz",
        # Measured official HTTPS download, not a publisher-signed checksum.
        "sha256": "2e0b68634c6eca40f660b934665506f6295b03a8fc3c0019aaca19e620de0b59",
    },
    {
        "file": "make_4.3-4.1build2_amd64.deb",
        "url": "https://archive.ubuntu.com/ubuntu/pool/main/m/make-dfsg/make_4.3-4.1build2_amd64.deb",
        # Matches Ubuntu's apt metadata for Noble amd64.
        "sha256": "1fe6a815b56c7b6e9ce4086a363f09444bbd0a0d30e230c453d0b78e44b57a99",
    },
)


def main():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise SystemExit("Run with Python 3.12+ in x86_64 Ubuntu WSL.")
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    TOOLS.mkdir(parents=True, exist_ok=True)
    for package in PACKAGES:
        target = DOWNLOADS / package["file"]
        if not target.exists():
            partial = target.with_suffix(target.suffix + ".partial")
            print("Downloading", package["url"], flush=True)
            with urllib.request.urlopen(package["url"], timeout=300) as response, partial.open("wb") as output:
                while block := response.read(1024 * 1024):
                    output.write(block)
            partial.replace(target)
        with target.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        if digest != package["sha256"]:
            raise SystemExit(f"Checksum mismatch: {target}; refusing extraction")
        print(digest, target.name, flush=True)
    with tarfile.open(DOWNLOADS / PACKAGES[0]["file"]) as archive:
        if any(member.name != "tc32" and not member.name.startswith("tc32/") for member in archive):
            raise SystemExit("Unexpected TC32 archive root")
        archive.extractall(TOOLS, filter="data")
    subprocess.run(["dpkg-deb", "-x", str(DOWNLOADS / PACKAGES[1]["file"]), str(TOOLS / "host")], check=True)
    (TOOLS / "manifest.json").write_text(json.dumps(PACKAGES, indent=2) + "\n", encoding="utf-8")
    subprocess.run([str(TOOLS / "tc32/bin/tc32-elf-gcc"), "--version"], check=True)
    subprocess.run([str(TOOLS / "host/usr/bin/make"), "--version"], check=True)
    print("ToolchainPrefix:", TOOLS / "tc32/bin/tc32")


if __name__ == "__main__":
    main()
