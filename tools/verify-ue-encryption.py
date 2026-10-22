"""Fail closed on unencrypted UE 5.8 release containers; never read key files.

The default report is local/ignored. --unrealpak optionally proves that an
unkeyed official UnrealPak cannot list each encrypted Pak's index. Run it only
after packaging has finished. Game startup/asset loading is a separate check.

Layout sources in the installed UE 5.8.4 source:
  Runtime/Core/Internal/IO/IoStore.h: FIoStoreTocHeader
  Runtime/Core/Public/IO/IoDispatcher.h: EIoContainerFlags
  Runtime/PakFile/Public/IPlatformFilePak.h: FPakInfo::Serialize
No encryption key value or key configuration is loaded or recorded by this tool.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import uuid


ROOT = Path(__file__).resolve().parents[1]
TOC_MAGIC = b"-==--==--==--==-"
PAK_MAGIC = 0x5A6F12E1


class AuditError(ValueError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect_utoc(path: Path) -> dict:
    with path.open("rb") as stream:
        header = stream.read(144)
    if len(header) != 144 or header[:16] != TOC_MAGIC:
        raise AuditError(f"{path.name}: invalid or truncated IoStore TOC header")
    version = header[16]
    header_size = struct.unpack_from("<I", header, 20)[0]
    if version != 8 or header_size != 144:
        raise AuditError(f"{path.name}: unsupported TOC layout {version}/{header_size}; recheck UE source")
    entries = struct.unpack_from("<I", header, 24)[0]
    directory_index_size = struct.unpack_from("<I", header, 48)[0]
    partitions = struct.unpack_from("<I", header, 52)[0]
    flags = header[80]
    if flags & ~0x1F:
        raise AuditError(f"{path.name}: unknown IoStore container flags {flags}")
    global_metadata = path.name.casefold() == "global.utoc"
    encrypted, indexed = bool(flags & 2), bool(flags & 8)
    if not global_metadata and (not encrypted or not indexed or not directory_index_size):
        raise AuditError(
            f"{path.name}: game container requires encrypted data and directory index "
            f"(flags={flags}, directory_index_bytes={directory_index_size})"
        )
    if not entries or not partitions:
        raise AuditError(f"{path.name}: empty IoStore container")
    for partition in range(partitions):
        suffix = "" if partition == 0 else f"_s{partition}"
        payload = path.with_name(path.stem + suffix + ".ucas")
        if not payload.is_file() or payload.stat().st_size == 0:
            raise AuditError(f"{path.name}: missing/empty payload {payload.name}")
    return {
        "name": path.name, "toc_version": version, "header_bytes": header_size,
        "flags": flags, "encrypted": encrypted, "indexed": indexed,
        "directory_index_bytes": directory_index_size, "entries": entries,
        "partitions": partitions, "global_metadata_exemption": global_metadata,
        "guid_note": "The embedded default AES key legitimately uses an all-zero GUID.",
    }


def inspect_pak(path: Path) -> dict:
    size = path.stat().st_size
    # v8 and v10..12 are 221 bytes; only the obsolete frozen-index v9 adds a byte.
    # Do not search arbitrarily for a magic value inside encrypted payload data.
    for footer_size in (221, 222):
        if size < footer_size:
            continue
        with path.open("rb") as stream:
            stream.seek(-footer_size, 2)
            footer = stream.read(footer_size)
        magic, version = struct.unpack_from("<Ii", footer, 17)
        if magic != PAK_MAGIC:
            continue
        if version not in (8, 9, 10, 11, 12) or footer_size != (222 if version == 9 else 221):
            raise AuditError(f"{path.name}: unsupported/inconsistent Pak footer version {version}")
        index_encrypted = footer[16]
        index_offset, index_size = struct.unpack_from("<qq", footer, 25)
        if index_offset < 0 or index_size <= 0 or index_offset + index_size > size - footer_size:
            raise AuditError(f"{path.name}: invalid Pak index bounds")
        if index_encrypted != 1 or index_size % 16:
            raise AuditError(f"{path.name}: Pak index is not valid AES-encrypted data")
        return {
            "name": path.name, "pak_version": version, "footer_bytes": footer_size,
            "index_encrypted": True, "index_offset": index_offset,
            "index_bytes": index_size,
        }
    raise AuditError(f"{path.name}: missing supported Pak footer")


def unkeyed_list(unrealpak: Path, pak: Path) -> dict:
    # No project, AES parameter, -cryptokeys, keychain or config argument is passed.
    # Run outside the game project so it cannot discover project key config.
    command = [str(unrealpak), str(pak), "-List", "-unattended", "-NoCrashDialog", "-stdout"]
    completed = subprocess.run(
        command, cwd=unrealpak.parent, capture_output=True, timeout=45,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    output = (completed.stdout + completed.stderr).decode("utf-8", errors="replace")
    missing_key = bool(re.search(
        r"Missing encryption key|Failed to find requested encryption key|"
        r"Unable to find (?:the )?encryption key", output, flags=re.IGNORECASE,
    ))
    # A nonzero return code alone could mean a missing DLL or unrelated crash.
    if completed.returncode == 0 or not missing_key:
        raise AuditError(
            f"{pak.name}: unkeyed UnrealPak did not demonstrate missing-key refusal "
            f"(exit={completed.returncode}, missing_key_diagnostic={missing_key})"
        )
    return {
        "name": pak.name, "exit_code": completed.returncode,
        "missing_key_diagnostic": True, "unkeyed_index_listing_refused": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", type=Path,
                        default=ROOT / "builds/NeonCoast-UE-Windows/Windows")
    parser.add_argument("--report", type=Path,
                        default=ROOT / "artifacts/ue-encryption-verification.json")
    parser.add_argument("--unrealpak", type=Path,
                        help="Optional official UnrealPak.exe for an unkeyed index-list refusal check")
    args = parser.parse_args()
    report = {
        "status": "in_progress", "run_id": uuid.uuid4().hex,
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "release": str(args.release.resolve()),
        "scope": "IoStore data/index encryption flags and Pak index encryption, plus optional unkeyed Pak listing.",
        "limits": "Does not establish immunity to reverse engineering, supplier approval, or successful game startup.",
        "utoc": [], "pak": [], "unkeyed_pak_checks": [], "content_files": [],
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    try:
        release = args.release.resolve(strict=True)
        paks_dir = release / "NeonCoast/Content/Paks"
        utocs, paks = sorted(paks_dir.glob("*.utoc")), sorted(paks_dir.glob("*.pak"))
        if not paks or not any(p.name.casefold() != "global.utoc" for p in utocs):
            raise AuditError("Release must contain at least one game IoStore TOC and one Pak")
        # Only inspect filenames here, never the contents of possible key files.
        exposed = [str(p.relative_to(release)) for p in release.rglob("*") if p.is_file()
                   and (p.name.casefold() in {"crypto.json", "defaultcrypto.ini", "defaultencryption.ini"}
                        or re.fullmatch(r"cryptokeys.*\.json", p.name, flags=re.IGNORECASE))]
        if exposed:
            raise AuditError("Release contains possible key configuration/metadata: " + ", ".join(exposed))
        for path in utocs:
            report["utoc"].append(inspect_utoc(path))
        for path in paks:
            report["pak"].append(inspect_pak(path))
        if args.unrealpak:
            unrealpak = args.unrealpak.resolve(strict=True)
            for path in paks:
                report["unkeyed_pak_checks"].append(unkeyed_list(unrealpak, path))
        for path in sorted(paks_dir.iterdir()):
            if path.is_file() and path.suffix.casefold() in {".pak", ".utoc", ".ucas", ".sig"}:
                report["content_files"].append({
                    "name": path.name, "bytes": path.stat().st_size, "sha256": sha256(path),
                })
        report["status"] = "passed"
    except (AuditError, OSError, subprocess.SubprocessError) as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "report": str(args.report.resolve()),
                      "error": report.get("error")}, ensure_ascii=False))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
