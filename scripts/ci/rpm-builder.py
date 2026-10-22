#!/usr/bin/env python3
"""Capture complete builders, validate reviewed locks, and load verified archives."""
import argparse
import gzip
import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess

TARGETS = {f"{d}-{a}" for d in ("fedora43", "fedora44", "rocky10")
           for a in ("amd64", "arm64")}
MEMBERS = {"builder.tar.gz", "packages.tsv", "image.json", "tools.txt"}
MAX_ARCHIVE = 4 * 1024 ** 3


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    require(path.is_file() and not path.is_symlink(), "expected regular input file")
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def read_json(path):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= 1024 ** 2,
            "invalid or oversized JSON input")
    return json.loads(path.read_text(), object_pairs_hook=unique_object)


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write("\n")


def validate(entry, registered=True):
    require(isinstance(entry, dict), "builder entry must be an object")
    require(set(entry) == {"target", "image_id", "base_image", "capture_revision",
                           "files", "artifact_id", "run_id"}, "unexpected builder fields")
    require(isinstance(entry["target"], str) and entry["target"] in TARGETS, "unknown builder target")
    for key in ("image_id",):
        require(isinstance(entry[key], str) and re.fullmatch(r"sha256:[0-9a-f]{64}", entry[key]),
                "invalid image digest")
    require(isinstance(entry["base_image"], str) and
            re.fullmatch(r"[a-z0-9./:-]+@sha256:[0-9a-f]{64}", entry["base_image"]),
            "base image must be digest-pinned")
    require(isinstance(entry["capture_revision"], str) and
            re.fullmatch(r"[0-9a-f]{40}", entry["capture_revision"]), "invalid capture revision")
    for key in ("artifact_id", "run_id"):
        if not registered and entry[key] is None:
            continue
        require(type(entry[key]) is int and entry[key] > 0, "invalid artifact/run identity")
    require(isinstance(entry["files"], dict) and set(entry["files"]) == MEMBERS,
            "incomplete builder file inventory")
    for name, record in entry["files"].items():
        require(isinstance(record, dict) and set(record) == {"sha256", "bytes"},
                "invalid file record")
        require(isinstance(record["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", record["sha256"]),
                "invalid file digest")
        limit = MAX_ARCHIVE if name == "builder.tar.gz" else 16 * 1024 ** 2
        require(type(record["bytes"]) is int and 0 < record["bytes"] <= limit,
                "invalid file size")
    return entry


def lock_entries(path):
    lock = read_json(path)
    require(isinstance(lock, dict) and set(lock) == {"format_version", "builders"} and
            type(lock["format_version"]) is int and lock["format_version"] == 1,
            "unsupported builder lock")
    require(isinstance(lock["builders"], dict) and set(lock["builders"]) == TARGETS,
            "builder lock must contain all six targets")
    for target, entry in lock["builders"].items():
        validate(entry)
        require(entry["target"] == target, "builder target mismatch")
    return lock["builders"]


def verify(entry, directory, registered=True):
    validate(entry, registered)
    require(directory.is_dir() and not directory.is_symlink(), "invalid payload directory")
    require({p.name for p in directory.iterdir()} == MEMBERS, "unexpected builder payload")
    for name, record in entry["files"].items():
        path = directory / name
        require(path.is_file() and not path.is_symlink(), "invalid builder payload file")
        require(path.stat().st_size == record["bytes"] and digest(path) == record["sha256"],
                f"builder checksum mismatch: {name}")


def inspect(image):
    records = json.loads(subprocess.check_output(["docker", "image", "inspect", image]))
    require(len(records) == 1, "ambiguous builder image")
    return records[0]


def capture(image, target, base_image, directory):
    require(target in TARGETS, "unknown builder target")
    directory.mkdir()
    payload = directory / "payload"
    payload.mkdir()
    info = inspect(image)
    arch = target.rsplit("-", 1)[1]
    require(info["Os"] == "linux" and info["Architecture"] == arch, "builder architecture mismatch")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    labels = info.get("Config", {}).get("Labels") or {}
    require(labels.get("org.opencontainers.image.revision") == revision and
            labels.get("org.opencontainers.image.base.name") == base_image,
            "capture source/base labels do not match reviewed inputs")
    entry = {"target": target, "image_id": info["Id"], "base_image": base_image,
             "capture_revision": revision, "files": {}, "artifact_id": None, "run_id": None}
    (payload / "image.json").write_text(json.dumps(info, sort_keys=True, indent=2) + "\n")
    query = "%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\t%{LICENSE}\t%{SOURCERPM}\t%{VENDOR}\n"
    packages = subprocess.check_output([
        "docker", "run", "--rm", "--pull=never", "--network=none", "--entrypoint", "rpm",
        info["Id"], "-qa", "--qf", query,
    ], text=True)
    (payload / "packages.tsv").write_text("".join(sorted(packages.splitlines(keepends=True))))
    pg_config = "/usr/pgsql-18/bin/pg_config" if target.startswith("rocky10-") else "/usr/bin/pg_server_config"
    tools = subprocess.check_output([
        "docker", "run", "--rm", "--pull=never", "--network=none", "--entrypoint", "/bin/sh", info["Id"],
        "-eu", "-c", f"{pg_config} --version; gcc --version; ld --version; rpm --version; "
        "sha256sum /opt/rpm-dependencies/jansson.tar.gz /opt/rpm-dependencies/libjwt.tar.gz",
    ], text=True)
    (payload / "tools.txt").write_text(tools)
    # docker save includes parent layers; retaining only an image ID cannot
    # recreate a deleted builder. Docker's image/save and image/load manuals.
    archive = directory / "builder.tar"
    subprocess.run(["docker", "image", "save", "--output", str(archive), info["Id"]], check=True)
    with archive.open("rb") as source, (payload / "builder.tar.gz").open("xb") as output:
        with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0, compresslevel=1) as compressed:
            shutil.copyfileobj(source, compressed, 1024 ** 2)
    archive.unlink()
    entry["files"] = {p.name: {"sha256": digest(p), "bytes": p.stat().st_size}
                      for p in payload.iterdir()}
    validate(entry, registered=False)
    write_json(directory / "builder-lock.json", entry)


def register(directory, artifact_id, run_id):
    path = directory / "builder-lock.json"
    entry = validate(read_json(path), registered=False)
    verify(entry, directory / "payload", registered=False)
    entry.update(artifact_id=artifact_id, run_id=run_id)
    validate(entry)
    path.unlink()
    write_json(path, entry)


def collect(directory, output):
    entries = {}
    for path in sorted(directory.glob("*/builder-lock.json")):
        entry = validate(read_json(path))
        require(entry["target"] not in entries, "duplicate builder target")
        entries[entry["target"]] = entry
    require(set(entries) == TARGETS, "missing frozen builder target")
    write_json(output, {"format_version": 1, "builders": entries})


def load(entry, directory):
    verify(entry, directory)
    arch = {"x86_64": "amd64", "aarch64": "arm64", "arm64": "arm64"}.get(platform.machine())
    require(entry["target"].endswith("-" + str(arch)), "native builder architecture required")
    # Verify every archive byte BEFORE Docker parses or loads it. Never pull a
    # replacement when a retained artifact is missing or corrupt.
    subprocess.run(["docker", "image", "load", "--input", str(directory / "builder.tar.gz")], check=True)
    info = inspect(entry["image_id"])
    require(info["Id"] == entry["image_id"] and info["Os"] == "linux" and
            info["Architecture"] == arch, "loaded builder identity mismatch")
    labels = info.get("Config", {}).get("Labels") or {}
    require(labels.get("org.opencontainers.image.revision") == entry["capture_revision"] and
            labels.get("org.opencontainers.image.base.name") == entry["base_image"],
            "loaded builder source/base mismatch")


def matrix(path):
    entries = lock_entries(path)
    values = []
    for target, entry in sorted(entries.items()):
        values.append({"target": target, "runner": "ubuntu-24.04-arm" if target.endswith("-arm64")
                       else "ubuntu-24.04", **{k: entry[k] for k in ("artifact_id", "run_id", "image_id")}})
    print("matrix=" + json.dumps({"include": values}, separators=(",", ":")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("capture")
    command.add_argument("image")
    command.add_argument("target")
    command.add_argument("base_image")
    command.add_argument("directory", type=pathlib.Path)
    command = commands.add_parser("register")
    command.add_argument("directory", type=pathlib.Path)
    command.add_argument("artifact_id", type=int)
    command.add_argument("run_id", type=int)
    command = commands.add_parser("collect")
    command.add_argument("directory", type=pathlib.Path)
    command.add_argument("output", type=pathlib.Path)
    command = commands.add_parser("matrix")
    command.add_argument("lock", type=pathlib.Path)
    command = commands.add_parser("load")
    command.add_argument("lock", type=pathlib.Path)
    command.add_argument("target")
    command.add_argument("directory", type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.command == "capture":
            capture(args.image, args.target, args.base_image, args.directory)
        elif args.command == "register":
            register(args.directory, args.artifact_id, args.run_id)
        elif args.command == "collect":
            collect(args.directory, args.output)
        elif args.command == "matrix":
            matrix(args.lock)
        else:
            load(lock_entries(args.lock)[args.target], args.directory)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
