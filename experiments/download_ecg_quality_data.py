"""Download public QTDB signals/reference beats and NSTDB noise sources.

Only the files needed by the quality benchmark are downloaded. Every payload
is verified against the official SHA256SUMS.txt before replacing a local file.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import time
import urllib.request


def fetch(url: str) -> bytes:
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=90) as response:
                return response.read()
        except OSError:
            if attempt == 2:
                raise
            time.sleep(attempt + 1)
    raise RuntimeError("unreachable")


def download(database: str, root: Path) -> None:
    base = f"https://physionet.org/files/{database}/1.0.0/"
    target = root / database
    target.mkdir(parents=True, exist_ok=True)
    manifest = fetch(base + "SHA256SUMS.txt")
    expected = {}
    for line in manifest.decode().splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip("*")
        if name.startswith("./"):
            name = name[2:]
        if "/" not in name and "\\" not in name:
            expected[name] = digest
    if database == "qtdb":
        names = sorted(n for n in expected if n.endswith((".hea", ".dat", ".atr")) or n == "RECORDS")
    else:
        names = [f"{record}.{extension}" for record in ("bw", "ma", "em") for extension in ("hea", "dat")]

    def one(name: str) -> dict:
        path = target / name
        digest = expected[name]
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
            return {"file": name, "sha256": digest, "verified": True}
        body = fetch(base + name)
        if hashlib.sha256(body).hexdigest() != digest:
            raise ValueError(f"Checksum mismatch: {base + name}")
        staging = path.with_suffix(path.suffix + ".part")
        staging.write_bytes(body)
        staging.replace(path)
        return {"file": name, "sha256": digest, "verified": True}

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(one, names))
    (target / "SHA256SUMS.txt").write_bytes(manifest)
    (target / "download_manifest.json").write_text(json.dumps({
        "base_url": base, "checksum_manifest_sha256": hashlib.sha256(manifest).hexdigest(),
        "files": results,
    }, indent=2), encoding="utf-8")
    print(f"{database}: verified {len(results)} files", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    args = parser.parse_args()
    for database in ("qtdb", "nstdb"):
        download(database, args.data_dir)
