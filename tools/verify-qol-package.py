#!/usr/bin/env python3
"""Read-only verification of the QoL build and matching committed-source ZIPs.

No archives are extracted, no executable is run, and no game folder is touched.
Usage: python tools/verify-qol-package.py BUILD.zip SOURCE.zip --commit HEAD
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import zipfile

DLSS_SHA256 = "be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e"
OXANIUM_SHA256 = "2ce01d946e1e1ffc8d7eecfffbda8623bedd63eaf811a20488c4b69af45babb0"
TITLES = ("halo3", "odst", "reach", "halo4", "ce", "halo2")
ACTIONS = ("fire", "grenade", "jump", "melee", "reload", "interact", "switch_weapon",
           "switch_grenade", "equipment", "crouch", "zoom", "flashlight", "sprint")
REQUIRED_PAYLOAD = {
    "HaloMCCVR.dll", "HaloMCCVRLauncher.exe", "halomccvr.cfg", "INSTALL-MANIFEST.sha256",
    "BUILD-IDENTITY.txt", "LICENSE", "MANUAL-README.txt", "THIRD-PARTY-LICENSES.txt",
    "RELEASE-NOTES.md", "IMPLEMENTATION-STATUS.md", "nvngx_dlss.dll",
    "licenses/NVIDIA-DLSS/LICENSE.txt", "licenses/NVIDIA-DLSS/NOTICE.txt",
    "assets/fonts/Oxanium.ttf", "assets/fonts/OFL-Oxanium.txt", "assets/fonts/SOURCE.txt",
}
ROOT_FILES = {"HaloMCCVRLauncher.exe", "README.txt", "RELEASE-NOTES.md",
              "IMPLEMENTATION-STATUS.md", "CANDIDATE-MANIFEST.json"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def safe_name(name: str, directory: bool = False) -> str:
    require("\\" not in name and ":" not in name and not name.startswith("/"),
            f"Unsafe ZIP path: {name!r}")
    if directory:
        name = name.removesuffix("/")
    require(bool(name) and all(part and part not in (".", "..") and
            part[-1] not in (" ", ".") and not re.fullmatch(
                r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
            for part in name.split("/")), f"Unsafe ZIP path component: {name!r}")
    return name


def zip_files(archive: zipfile.ZipFile) -> dict[str, zipfile.ZipInfo]:
    result = {}
    names = set()
    require(len(archive.infolist()) <= 50000, "Unreasonable ZIP entry count")
    require(sum(item.file_size for item in archive.infolist()) <= 2 * 1024**3,
            "Unreasonable expanded archive size")
    for item in archive.infolist():
        name = safe_name(item.filename, item.is_dir())
        require(name.casefold() not in names, f"Duplicate Windows ZIP name: {name}")
        names.add(name.casefold())
        require((item.external_attr >> 16) & 0o170000 != 0o120000,
                f"ZIP symlink is forbidden: {name}")
        require(not item.flag_bits & 1, f"Encrypted ZIP member: {name}")
        if not item.is_dir():
            result[name] = item
    return result


def unique_json(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate metadata key: {key}")
        result[key] = value
    return result


def parse_manifest(data: bytes) -> dict[str, str]:
    result = {}
    folded = set()
    for line in data.decode("utf-8").splitlines():
        require(bool(re.fullmatch(r"[0-9a-fA-F]{64}  [\x20-\x7e]+", line)),
                "Malformed payload SHA-256 manifest line")
        digest, name = line.split("  ", 1)
        safe_name(name)
        require(name != "INSTALL-MANIFEST.sha256" and name.casefold() not in folded,
                f"Self-reference or duplicate manifest path: {name}")
        folded.add(name.casefold())
        result[name] = digest.lower()
    return result


def config_values(data: bytes) -> dict[str, str]:
    result = {}
    for line in data.decode("utf-8-sig").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        require(key not in result or result[key] == value, f"Conflicting packaged config key: {key}")
        result[key] = value
    return result


def check_config(data: bytes) -> int:
    values = config_values(data)
    for title in TITLES:
        for action in ACTIONS:
            key = f"vr_bind_{title}_{action}"
            require(key in values and values[key].isdigit() and 0 <= int(values[key]) < 16,
                    f"Missing or invalid per-game binding: {key}")
        key = f"hide_muzzle_flash_{title}"
        require(values.get(key) in ("0", "1"), f"Missing per-game muzzle-flash setting: {key}")
        key = f"bloom_enabled_{title}"
        require(values.get(key) in ("0", "1"), f"Missing per-game bloom setting: {key}")
    for key in ("virtual_stock", "virtual_stock_proximity_release",
                "vr_action_mapping", "flashlight_suppress_on_two_hand", "physical_crouch"):
        require(values.get(key) in ("0", "1"), f"Missing boolean config setting: {key}")
    require(values["physical_crouch"] == "0", "Optional physical crouch must default off")
    require(values["virtual_stock"] == "0", "Optional virtual stock must default off")
    for key, low, high in (
        ("virtual_stock_strength", 0, 1), ("physical_crouch_depth_m", .08, .65),
        ("virtual_stock_rear_reference", 0, 3), ("weapon_pouch_location", 0, 1),
        ("weapon_pouch_offset_x_m", -.4, .4), ("weapon_pouch_offset_y_m", -.4, .4),
        ("weapon_pouch_offset_z_m", -.4, .4), ("upscaler", 0, 1), ("dlss_mode", 0, 5),
    ):
        require(key in values, f"Missing QoL config setting: {key}")
        value = float(values[key])
        require(math.isfinite(value) and low <= value <= high, f"Invalid QoL config setting: {key}")
    return len(values)


def check_build(path: Path, commit: str) -> dict:
    with zipfile.ZipFile(path) as archive:
        files = zip_files(archive)
        require(ROOT_FILES <= files.keys(), "Build ZIP lacks root launcher/readme/status/manifest")
        require(all(name in ROOT_FILES or name.startswith("ModFiles/") for name in files),
                "Unexpected file outside the manual payload in build ZIP")
        payload = {name.removeprefix("ModFiles/") for name in files if name.startswith("ModFiles/")}
        require(REQUIRED_PAYLOAD <= payload, f"Missing payload files: {sorted(REQUIRED_PAYLOAD - payload)}")
        listed = parse_manifest(archive.read("ModFiles/INSTALL-MANIFEST.sha256"))
        require(set(listed) == payload - {"INSTALL-MANIFEST.sha256"},
                "INSTALL-MANIFEST must cover exactly every payload file except itself")
        metadata = json.loads(archive.read("CANDIDATE-MANIFEST.json"), object_pairs_hook=unique_json)
        require(metadata.get("schema_version") == 55 and metadata.get("payload_directory") == "ModFiles",
                "Unexpected candidate manifest schema or layout")
        require(metadata.get("source_commit") == commit and metadata.get("accepted") is False and
                metadata.get("status") == "UNTESTED_LOCAL_CANDIDATE", "Candidate source/status mismatch")
        require(set(metadata.get("files", {})) == set(listed), "Candidate file table and install manifest disagree")
        for name, expected in listed.items():
            data = archive.read("ModFiles/" + name)
            digest = sha256(data)
            require(digest == expected, f"Payload SHA-256 mismatch: {name}")
            entry = metadata["files"][name]
            require(str(entry.get("sha256", "")).lower() == digest and entry.get("bytes") == len(data),
                    f"Candidate file table mismatch: {name}")
        require(archive.read("HaloMCCVRLauncher.exe") == archive.read("ModFiles/HaloMCCVRLauncher.exe"),
                "Root and manual payload launchers differ")
        dll = archive.read("ModFiles/HaloMCCVR.dll")
        require(commit.encode("ascii") + b"\0" in dll and commit.encode("ascii") + b"-dirty" not in dll,
                "Packaged DLL does not embed the exact clean source commit")
        for name in ("RELEASE-NOTES.md", "IMPLEMENTATION-STATUS.md"):
            require(archive.read(name) == archive.read("ModFiles/" + name), f"Root/payload notes differ: {name}")
        identity = config_values(archive.read("ModFiles/BUILD-IDENTITY.txt"))
        require(identity.get("source_commit") == commit and identity.get("release_tag") == "" and
                identity.get("build_kind") == "UNTESTED_LOCAL_CANDIDATE", "Launcher build identity mismatch")
        require(listed["nvngx_dlss.dll"] == DLSS_SHA256, "NVIDIA runtime is not the pinned release DLL")
        require(listed["assets/fonts/Oxanium.ttf"] == OXANIUM_SHA256, "Bundled Oxanium font differs from licensed source")
        require(b"SIL OPEN FONT LICENSE" in archive.read("ModFiles/assets/fonts/OFL-Oxanium.txt"),
                "Missing Oxanium OFL license text")
        require(b"NVIDIA" in archive.read("ModFiles/licenses/NVIDIA-DLSS/LICENSE.txt"),
                "Missing NVIDIA distribution license text")
        config_count = check_config(archive.read("ModFiles/halomccvr.cfg"))
        require(archive.read("README.txt") == archive.read("ModFiles/MANUAL-README.txt"), "Root/manual readme differ")
        return {"payload_files": len(listed), "config_keys": config_count,
                "dll_sha256": listed["HaloMCCVR.dll"], "launcher_sha256": listed["HaloMCCVRLauncher.exe"]}


def git(repo: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True).stdout


def check_source(path: Path, repo: Path, commit: str) -> int:
    tree = {}
    for entry in git(repo, "ls-tree", "-rz", commit).split(b"\0"):
        if not entry:
            continue
        header, name = entry.split(b"\t", 1)
        mode, kind, digest = header.decode("ascii").split()
        require(kind == "blob" and mode != "120000", "Source archive cannot silently omit submodules/symlinks")
        decoded = name.decode("utf-8")
        require(decoded.split("/", 1)[0].casefold() not in {"out", ".git", ".codex", "build"},
                f"Private/build directory tracked in source: {decoded}")
        require(PurePosixPath(decoded).suffix.lower() not in {".exe", ".dll", ".pdb", ".obj", ".lib", ".log", ".zip", ".7z", ".dmp"},
                f"Binary/build/log artifact in source archive: {decoded}")
        tree["Halo-MCC-VR/" + decoded] = digest
    with zipfile.ZipFile(path) as archive:
        files = zip_files(archive)
        require(set(files) == set(tree), "Source ZIP does not contain exactly the committed tracked files")
        for name, expected in tree.items():
            data = archive.read(name)
            blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
            require(blob == expected, f"Source ZIP differs from committed Git blob: {name}")
    return len(tree)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_zip", type=Path)
    parser.add_argument("source_zip", type=Path)
    parser.add_argument("--commit", default="HEAD")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        commit = git(args.repo, "rev-parse", "--verify", args.commit + "^{commit}").decode("ascii").strip()
        require(bool(re.fullmatch("[0-9a-f]{40}", commit)), "Expected a full SHA-1 Git commit")
        report = check_build(args.build_zip, commit)
        report.update(source_files=check_source(args.source_zip, args.repo, commit), source_commit=commit,
                      build_zip_sha256=file_sha256(args.build_zip), source_zip_sha256=file_sha256(args.source_zip))
        print(json.dumps({"verified": True, **report}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"Package verification FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
