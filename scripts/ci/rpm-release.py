#!/usr/bin/env python3
"""Verify retained RPM bundles and rehearse exact source/version pairs offline."""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rpm_candidates", ROOT / "scripts/ci/check-rpm-candidates.py")
candidates = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(candidates)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_object(path, limit):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= limit,
            "invalid JSON evidence file")

    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate JSON evidence key")
            result[key] = value
        return result

    return json.loads(path.read_text(), object_pairs_hook=unique)


def checksum_text(directory):
    require(not directory.is_symlink() and directory.is_dir(), "retained bundle must be a directory")
    require({path.name for path in directory.iterdir()} <=
            {"RPMS", "SRPMS", "inputs", "BUILD-ENVIRONMENT.txt", "SHA256SUMS"},
            "unexpected retained bundle entry")
    return "".join(f"{digest}  {name}\n" for name, digest in sorted(candidates.inventory(directory).items()))


def seal(directory):
    text = checksum_text(directory)
    with (directory / "SHA256SUMS").open("x", encoding="utf-8") as stream:
        stream.write(text)


def verify(directory):
    manifest = directory / "SHA256SUMS"
    require(manifest.is_file() and not manifest.is_symlink() and manifest.stat().st_size <= 65536,
            "missing or invalid retained checksum manifest")
    require(manifest.read_text() == checksum_text(directory), "retained bundle checksum mismatch")


def identity(directory):
    verify(directory)
    spec_path = directory / "inputs/SPECS/pg_oauth_validator.spec"
    require(spec_path.stat().st_size <= 128 * 1024, "oversized retained spec")
    spec = spec_path.read_text()
    values = []
    for pattern in (r"^Version:\s+((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))$",
                    r"^%global source_revision ([0-9a-f]{40})$"):
        matches = re.findall(pattern, spec, re.MULTILINE)
        require(len(matches) == 1, "missing or ambiguous source/version identity")
        values.append(matches[0])
    return tuple(values)


def package(directory):
    files = [p for p in (directory / "RPMS").rglob("*.rpm")
             if re.fullmatch(r"postgresql18-pg_oauth_validator-[0-9].*\.rpm", p.name)]
    require(len(files) == 1, "exactly one retained validator RPM required")
    return files[0]


def compare(first, second):
    verify(first)
    verify(second)
    require(candidates.inventory(first) == candidates.inventory(second),
            "fresh-runner candidate bundles differ")


def baseline_policy(path, candidate_version):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= 4096,
            "invalid baseline policy")
    value = read_object(path, 4096)
    require(isinstance(value, dict) and set(value) == {"version", "tag", "source_revision", "classification"},
            "invalid baseline policy fields")
    for version in (value["version"], candidate_version):
        require(isinstance(version, str) and len(version) <= 32 and
                re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version),
                "MAJOR.MINOR.PATCH required")
    require(value["tag"] == "v" + value["version"] and
            isinstance(value["source_revision"], str) and
            re.fullmatch(r"[0-9a-f]{40}", value["source_revision"]), "invalid baseline source/tag")
    require(value["classification"] == "rebuilt-source-release", "unsupported baseline classification")
    require(tuple(map(int, value["version"].split("."))) < tuple(map(int, candidate_version.split("."))),
            "candidate version must follow baseline")
    return value


def baseline_suite(inputs, destination):
    """Use the reviewed release's tests for its versioned behavior contract."""
    policy = read_object(ROOT / "packaging/rpm/release-baseline.json", 4096)
    # Reuse strict policy validation without assuming the next release number.
    parts = list(map(int, policy["version"].split(".")))
    parts[2] += 1
    policy = baseline_policy(ROOT / "packaging/rpm/release-baseline.json",
                             ".".join(map(str, parts)))
    spec = (inputs / "SPECS/pg_oauth_validator.spec").read_text()
    require(re.findall(r"^%global source_revision ([0-9a-f]{40})$", spec, re.MULTILINE) ==
            [policy["source_revision"]] and
            re.findall(r"^Version:\s+(\S+)$", spec, re.MULTILINE) == [policy["version"]],
            "baseline test suite requires reviewed source/version")
    require(destination.is_dir() and not destination.is_symlink() and not any(destination.iterdir()),
            "baseline test destination must be empty")
    archive = subprocess.check_output([
        "git", "-c", f"safe.directory={ROOT}", "-C", str(ROOT), "archive", "--format=tar",
        policy["source_revision"],
    ])
    # Bytes come from the exact reviewed Git object, never an arbitrary archive.
    subprocess.run(["tar", "--extract", "--directory", str(destination)], input=archive, check=True)


def compare_evidence(first, second, baseline):
    version, revision = identity(first / "candidate")
    require(identity(second / "candidate") == (version, revision), "candidate identities differ")
    policy = baseline_policy(ROOT / "packaging/rpm/release-baseline.json", version)
    require(identity(baseline) == (policy["version"], policy["source_revision"]),
            "retained baseline does not match reviewed release")
    receipts = []
    expected = {
        "format_version": 1, "baseline_version": policy["version"], "candidate_version": version,
        "baseline_source_revision": policy["source_revision"], "candidate_source_revision": revision,
        "baseline_classification": "rebuilt-source-release; no previously published RPM",
        "baseline_rpm_sha256": hashlib.sha256(package(baseline).read_bytes()).hexdigest(),
        "baseline_bundle_manifest_sha256": hashlib.sha256((baseline / "SHA256SUMS").read_bytes()).hexdigest(),
        "phases": ["baseline", "candidate", "rollback"],
        "checks": ["retained_data", "restart", "authorization", "denial", "rotation",
                   "provider_outage_recovery", "invalid_configuration_recovery", "redaction"],
    }
    for root in (first, second):
        verify(root / "candidate")
        receipt = read_object(root / "rehearsal.json", 65536)
        require(isinstance(receipt, dict) and type(receipt.get("format_version")) is int and
                set(receipt) == set(expected) | {
                    "candidate_rpm_sha256", "candidate_bundle_manifest_sha256",
                    "restored_library_sha256", "restored_configuration_sha256",
                } and all(receipt.get(k) == v for k, v in expected.items()),
                "missing or inconsistent lifecycle evidence")
        require(receipt.get("candidate_rpm_sha256") ==
                hashlib.sha256(package(root / "candidate").read_bytes()).hexdigest() and
                receipt.get("candidate_bundle_manifest_sha256") ==
                hashlib.sha256((root / "candidate/SHA256SUMS").read_bytes()).hexdigest(),
                "candidate receipt digest mismatch")
        require((root / "SOURCE-REVISION").read_text().strip() == revision,
                "artifact source record mismatch")
        require((root / "builders.lock.json").read_bytes() ==
                (ROOT / "packaging/rpm/builders.lock.json").read_bytes(), "builder lock mismatch")
        require(isinstance(receipt.get("restored_library_sha256"), str) and
                re.fullmatch(r"[0-9a-f]{64}", receipt["restored_library_sha256"]),
                "missing restored module digest")
        configuration = receipt.get("restored_configuration_sha256")
        require(isinstance(configuration, dict) and set(configuration) == {
            "postgresql.conf", "postgresql.auto.conf", "pg_hba.conf", "pg_ident.conf",
            "oauth-ca.pem", "server.crt", "server.key",
        } and all(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value)
                  for value in configuration.values()), "incomplete restored configuration evidence")
        receipts.append(receipt)
    require(receipts[0]["restored_library_sha256"] == receipts[1]["restored_library_sha256"],
            "rollback library differs across runners")


def require_offline():
    require(os.geteuid() == 0 and os.environ.get("RPM_RELEASE_REHEARSAL") == "1",
            "disposable root rehearsal environment required")
    network = pathlib.Path("/sys/class/net")
    require((network / "lo").is_dir(), "Linux offline network namespace required")
    for interface in network.iterdir():
        flags = interface / "flags"
        if interface.name != "lo" and flags.is_file():
            require(int(flags.read_text(), 0) & 1 == 0,
                    "non-loopback interface is up")


def rehearse(baseline, candidate, output):
    require_offline()
    a, b = identity(baseline), identity(candidate)
    require(a[1] != b[1], "release rehearsal requires distinct source commits")
    require(tuple(map(int, a[0].split("."))) < tuple(map(int, b[0].split("."))),
            "candidate version must follow baseline")
    require(not output.exists(), "never overwrite rehearsal evidence")
    # Import only inside the disposable server environment; sealing/comparison
    # on the CI host must not need PostgreSQL or a postgres OS account.
    sys.path.insert(0, str(ROOT / "tests/integration"))
    import rpm_upgrade
    from integration_support import command
    pg_config = os.environ["PG_CONFIG_PATH"]
    library = pathlib.Path(command(pg_config, "--pkglibdir").stdout.strip()) / "pg_oauth_validator.so"
    require(not library.exists(), "validator library already installed")
    old, new = package(baseline), package(candidate)
    require(rpm_upgrade.package_metadata(old)[1] == a[0] and
            rpm_upgrade.package_metadata(new)[1] == b[0], "RPM/spec version mismatch")
    for artifact in (old, new):
        require(not command("rpm", "-qp", "--scripts", artifact).stdout,
                "retained package contains scriptlets")
    installed = False
    try:
        command("rpm", "--install", old)
        installed = True
        result = rpm_upgrade.rehearse(
            old, new, pg_config, library, pathlib.Path("/source/tests/integration/oauth_test_client"),
            {old: a[1], new: b[1]},
        )
        result.update(format_version=1, baseline_source_revision=a[1], candidate_source_revision=b[1],
                      baseline_classification="rebuilt-source-release; no previously published RPM",
                      baseline_bundle_manifest_sha256=hashlib.sha256((baseline / "SHA256SUMS").read_bytes()).hexdigest(),
                      candidate_bundle_manifest_sha256=hashlib.sha256((candidate / "SHA256SUMS").read_bytes()).hexdigest())
    finally:
        if installed:
            command("rpm", "--erase", rpm_upgrade.PACKAGE)
            require(not library.exists(), "validator remained after rehearsal removal")
    with output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, sort_keys=True, indent=2)
        stream.write("\n")
    print("Retained source-release baseline -> candidate -> baseline passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("seal", "verify", "identity"):
        command = commands.add_parser(name)
        command.add_argument("directory", type=pathlib.Path)
    command = commands.add_parser("baseline-suite")
    command.add_argument("inputs", type=pathlib.Path)
    command.add_argument("destination", type=pathlib.Path)
    command = commands.add_parser("compare")
    command.add_argument("first", type=pathlib.Path)
    command.add_argument("second", type=pathlib.Path)
    command = commands.add_parser("rehearse")
    command.add_argument("baseline", type=pathlib.Path)
    command.add_argument("candidate", type=pathlib.Path)
    command.add_argument("output", type=pathlib.Path)
    command = commands.add_parser("policy")
    command.add_argument("path", type=pathlib.Path)
    command.add_argument("candidate_version")
    command = commands.add_parser("compare-evidence")
    command.add_argument("first", type=pathlib.Path)
    command.add_argument("second", type=pathlib.Path)
    command.add_argument("baseline", type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.command == "seal":
            seal(args.directory)
        elif args.command == "baseline-suite":
            baseline_suite(args.inputs, args.destination)
        elif args.command == "verify":
            verify(args.directory)
        elif args.command == "identity":
            print(json.dumps(identity(args.directory)))
        elif args.command == "compare":
            compare(args.first, args.second)
            print("Candidate RPM/SRPM bundles match across fresh runners")
        elif args.command == "policy":
            policy = baseline_policy(args.path, args.candidate_version)
            for key in ("version", "tag", "source_revision"):
                print("baseline_" + key + "=" + policy[key])
        elif args.command == "compare-evidence":
            compare_evidence(args.first, args.second, args.baseline)
            print("Fresh-runner lifecycle receipts match retained source/version/artifact identities")
        else:
            rehearse(args.baseline, args.candidate, args.output)
    except (ValueError, OSError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
