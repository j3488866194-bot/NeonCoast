"""Fetch the two pinned, officially licensed Renderpeople character archives.

Run from any directory: python tools/fetch-scan-characters.py
Use --offline to verify/reuse local ZIPs without making any network requests.
Only missing or mismatched extracted files are replaced. Unreal is not started.
Sources stay under the gitignored artifacts/realism/renderpeople directory.
These proprietary assets are NOT CC0/MIT; see docs/licenses/RENDERPEOPLE.md.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "artifacts/realism/renderpeople"
LANDING_PAGE = "https://renderpeople.com/free-3d-people/"
LICENSE_URL = "https://renderpeople.com/general-terms-and-conditions/"
LICENSE = (
    "Renderpeople General Terms and Conditions, version 13.12.2024; "
    "free model terms per section 2.5, video-game use per 4.1(b); not CC0/MIT. "
    "Raw data redistribution restricted under 4.2(b) and 4.3(b)."
)
# Pinned to the official downloads acquired and CRC-checked on 2026-10-21.
# An upstream replacement fails verification instead of silently changing assets.
CHARACTERS = {
    "eric": {
        "source_url": "https://renderpeople.com/sample/free/rp_eric_rigged_001_FBX.zip",
        "bytes": 45_390_786,
        "sha256": "55b763ebea36cc8747a57a26cc46252fb0fa195c92ef87a12f0cf42f23264383",
    },
    "carla": {
        "source_url": "https://renderpeople.com/sample/free/rp_carla_rigged_001_FBX.zip",
        "bytes": 178_888_446,
        "sha256": "34ea79ba21470aea5e6db828cefa5ed14f98f6bb040bf0ff1193473ad5e0bf3e",
    },
}
CHUNK = 1024 * 1024
MAX_UNPACKED_BYTES = 512 * 1024 * 1024
LICENSE_NOTES = f"""Renderpeople GmbH official free 3D data: Eric Rigged 001 and Carla Rigged 001.
Source: {LANDING_PAGE}
Applicable terms: {LICENSE_URL}
Terms recorded on 2026-10-21, version shown: 13.12.2024.
Section 2.5 applies the paid-data terms to free models; section 4.1(b) permits private and commercial computer/video-game use.
Raw data redistribution and making individual model files accessible are restricted by sections 4.2(b) and 4.3(b).
This is a proprietary Renderpeople asset license, NOT CC0 or MIT.
Keep the original FBX/textures and editable imported assets private. Game distribution uses integrated cooked resources subject to those terms, not an asset library.
Per-character source JSON records the official URL, pinned SHA-256, bytes and verified ZIP entry CRCs.
See docs/licenses/RENDERPEOPLE.md for provenance and the separate Epic animation source/license.
This dated provenance summary does not replace the official terms or claim they were re-read at download time.
"""


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def official_url(url):
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or parsed.hostname != "renderpeople.com"
            or parsed.port not in (None, 443) or parsed.username is not None
            or parsed.password is not None or parsed.fragment):
        raise ValueError(f"Only HTTPS renderpeople.com sources are allowed: {url}")
    return url


class OfficialOnlyRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        # Validate before following, not merely after downloading from a redirect.
        official_url(new_url)
        return super().redirect_request(request, fp, code, message, headers, new_url)


def safe_path(base, relative):
    """Reject ZIP traversal, Windows aliases/ADS and existing escaping junctions."""
    parts = relative.rstrip("/").split("/")
    if (not relative or "\\" in relative or ":" in relative or "\x00" in relative
            or PurePosixPath(relative).is_absolute()
            or any(p in ("", ".", "..") or p.rstrip(" .") != p
                   or Path(p).is_reserved() for p in parts)):
        raise ValueError(f"Unsafe archive/output path: {relative!r}")
    resolved_base = base.resolve()
    resolved_base.relative_to(ROOT.resolve())
    target = base.joinpath(*parts)
    target.resolve().relative_to(resolved_base)
    if target.is_symlink():
        raise ValueError(f"Refusing a symbolic-link output: {target}")
    return target


def hash_file(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def pinned_archive(path, metadata):
    return (path.is_file() and path.stat().st_size == metadata["bytes"]
            and hash_file(path) == (metadata["bytes"], metadata["sha256"]))


def temporary_file(parent):
    parent.resolve().relative_to(DEST.resolve())
    return tempfile.NamedTemporaryFile(mode="wb", prefix=".scan-", suffix=".partial",
                                       dir=parent, delete=False)


def remove_temporary(path):
    path.resolve().relative_to(DEST.resolve())
    path.unlink(missing_ok=True)


def write_text(path, value):
    if path.is_file() and path.read_text(encoding="utf-8") == value:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with temporary_file(path.parent) as output:
            temporary = Path(output.name)
            output.write(value.encode("utf-8"))
        temporary.replace(path)
    finally:
        if temporary is not None:
            remove_temporary(temporary)


def archive_for(name, metadata, offline):
    filename = PurePosixPath(urllib.parse.urlsplit(metadata["source_url"]).path).name
    path = safe_path(DEST, filename)
    if pinned_archive(path, metadata):
        return path, True, None
    if offline:
        raise RuntimeError(f"Offline: missing or mismatched pinned archive: {path}")
    request = urllib.request.Request(official_url(metadata["source_url"]),
                                     headers={"User-Agent": "NeonCoastLocalPrototype/1.0"})
    opener = urllib.request.build_opener(OfficialOnlyRedirects())
    temporary = None
    print(f"DOWNLOAD {name}: {metadata['bytes']} bytes from {metadata['source_url']}", flush=True)
    try:
        with opener.open(request, timeout=120) as response, temporary_file(DEST) as output:
            temporary = Path(output.name)
            official_url(response.geturl())
            if response.status != 200:
                raise RuntimeError(f"Unexpected download HTTP status {response.status}")
            length = response.headers.get("Content-Length")
            if length is not None and int(length) != metadata["bytes"]:
                raise RuntimeError(f"Upstream archive size changed for {name}")
            received = 0
            for block in iter(lambda: response.read(CHUNK), b""):
                received += len(block)
                if received > metadata["bytes"]:
                    raise RuntimeError(f"Download exceeds pinned size for {name}")
                output.write(block)
        if not pinned_archive(temporary, metadata):
            raise RuntimeError(f"Pinned byte count/SHA-256 mismatch for {name}; original file preserved")
        temporary.replace(path)
        return path, False, utc_now()
    finally:
        if temporary is not None:
            remove_temporary(temporary)


def extracted_matches(path, info):
    if not path.is_file() or path.stat().st_size != info.file_size:
        return False
    crc = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            crc = zlib.crc32(block, crc)
    return crc & 0xFFFFFFFF == info.CRC


def extract_verified(archive, name):
    destination = safe_path(DEST, name)
    entries, reused, extracted = [], 0, 0
    with zipfile.ZipFile(archive) as source:
        infos = source.infolist()
        if len(infos) > 128 or sum(i.file_size for i in infos) > MAX_UNPACKED_BYTES:
            raise ValueError("Archive exceeds the expected character package limits")
        seen = set()
        # Preflight every member before making any extraction changes.
        for info in infos:
            target = safe_path(destination, info.filename)
            key = str(target).casefold()
            mode = info.external_attr >> 16
            if key in seen or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise ValueError(f"Duplicate or non-regular ZIP member: {info.filename}")
            if info.flag_bits & 1:
                raise ValueError(f"Encrypted ZIP member: {info.filename}")
            seen.add(key)
        bad = source.testzip()
        if bad is not None:
            raise ValueError(f"ZIP CRC failed: {bad}")
        destination.mkdir(parents=True, exist_ok=True)
        for info in infos:
            target = safe_path(destination, info.filename)
            entries.append({"name": info.filename, "bytes": info.file_size,
                            "crc32": f"{info.CRC:08x}"})
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if extracted_matches(target, info):
                reused += 1
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with source.open(info) as incoming, temporary_file(target.parent) as output:
                    temporary = Path(output.name)
                    for block in iter(lambda: incoming.read(CHUNK), b""):
                        output.write(block)
                if not extracted_matches(temporary, info):
                    raise ValueError(f"Extracted file CRC/size failed: {info.filename}")
                temporary.replace(target)
                extracted += 1
            finally:
                if temporary is not None:
                    remove_temporary(temporary)
    return destination, entries, reused, extracted


def fetch_character(name, offline):
    metadata = CHARACTERS[name]
    archive, reused_archive, retrieved_at = archive_for(name, metadata, offline)
    destination, entries, reused, extracted = extract_verified(archive, name)
    report_path = safe_path(DEST, name + "-source.json")
    previous = {}
    if report_path.is_file():
        try:
            previous = json.loads(report_path.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            pass
    if retrieved_at is None and all(previous.get(key) == metadata[key]
                                    for key in ("source_url", "bytes", "sha256")):
        retrieved_at = previous.get("retrieved_at")
    report = {"retrieved_at": retrieved_at, "verified_at": utc_now(), **metadata,
              "landing_page": LANDING_PAGE, "license_url": LICENSE_URL, "license": LICENSE,
              "license_recorded_on": "2026-10-21", "file": str(archive),
              "zip_crc_verified": True, "archive_reused": reused_archive,
              "extracted_to": str(destination), "extracted_files_reused": reused,
              "extracted_files_written": extracted, "entries": entries}
    write_text(report_path, json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"VERIFIED {name}: archive={'reused' if reused_archive else 'downloaded'}, "
          f"files reused={reused}, files written={extracted}, SHA-256/ZIP CRC/file CRC passed", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Never make network requests; require local pinned ZIPs")
    parser.add_argument("--characters", nargs="+", choices=tuple(CHARACTERS), default=list(CHARACTERS),
                        help="Characters to prepare (default: eric carla)")
    args = parser.parse_args()
    DEST.resolve().relative_to(ROOT.resolve())
    DEST.mkdir(parents=True, exist_ok=True)
    write_text(safe_path(DEST, "LICENSE-NOTES.txt"), LICENSE_NOTES)
    for name in dict.fromkeys(args.characters):
        fetch_character(name, args.offline)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
