#!/usr/bin/env python3
"""Installs the Windows MSVC Qt kit and Qt 5 Compatibility from the official Qt repository.

aqtinstall 3.3.0 looks for Windows Qt 6.12 in a repository folder that does not exist. This
script reads the kit's Updates.xml, checks each archive against its .sha1 file and extracts it
with 7-Zip. Used by .github/workflows/release.yml.

Usage: python ci/install-qt-windows.py --version 6.12.0 --output C:/Qt
Prints the kit folder (<output>/<version>/msvc2022_64) on the last line.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import xml.etree.ElementTree as ElementTree

BASE_URL = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"
ARCH = "win64_msvc2022_64"
ATTEMPTS = 5
TIMEOUT_SECONDS = 120
HEADERS = {"User-Agent": "notepadpp-qt-ci"}


def with_retries(description, action):
    for attempt in range(1, ATTEMPTS + 1):
        try:
            return action()
        except (OSError, ValueError) as error:
            if attempt == ATTEMPTS:
                sys.exit(f"{description} failed: {error}")
            print(f"{description}: attempt {attempt} failed: {error}", flush=True)
            time.sleep(attempt * 5)


def read_url(url):
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
        return response.read()


def download(url, path):
    expected = read_url(url + ".sha1").decode("ascii").split()[0].lower()
    digest = hashlib.sha1()
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response, open(path, "wb") as output:
        while chunk := response.read(1024 * 1024):
            digest.update(chunk)
            output.write(chunk)
    if digest.hexdigest() != expected:
        raise ValueError(f"SHA-1 mismatch for {url}")


def find_7zip():
    found = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
    if not os.path.exists(found):
        sys.exit("7-Zip (7z.exe) not found")
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    compact = args.version.replace(".", "")
    repository = f"{BASE_URL}/qt6_{compact}/qt6_{compact}_msvc2022_64"
    wanted = {f"qt.qt6.{compact}.{ARCH}", f"qt.qt6.{compact}.addons.qt5compat.{ARCH}"}

    updates = ElementTree.fromstring(with_retries("Updates.xml", lambda: read_url(f"{repository}/Updates.xml")))
    packages = [p for p in updates.findall("PackageUpdate") if p.findtext("Name") in wanted]
    missing = wanted - {p.findtext("Name") for p in packages}
    if missing:
        sys.exit(f"packages not found in Updates.xml: {sorted(missing)}")

    seven_zip = find_7zip()
    kit = os.path.join(args.output, args.version, "msvc2022_64")
    downloads = os.path.join(args.output, ".downloads")
    os.makedirs(downloads, exist_ok=True)
    for package in packages:
        name = package.findtext("Name")
        version = package.findtext("Version")
        for archive in filter(None, (a.strip() for a in package.findtext("DownloadableArchives").split(","))):
            print(f"{name}: {archive}", flush=True)
            path = os.path.join(downloads, archive)
            with_retries(archive, lambda: download(f"{repository}/{name}/{version}{archive}", path))
            subprocess.run([seven_zip, "x", "-y", "-bso0", "-bsp0", f"-o{kit}", path], check=True)
            os.remove(path)
    os.rmdir(downloads)

    for required in ("windeployqt.exe", "Qt6Core5Compat.dll"):
        if not os.path.exists(os.path.join(kit, "bin", required)):
            sys.exit(f"{required} not found in {kit}")
    print(kit)


if __name__ == "__main__":
    main()
