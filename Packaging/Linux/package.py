#!/usr/bin/env python3
"""Stage and statically verify the shared native Linux installed tree."""
import email.parser
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
POLICY_FILE = HERE / "host-policy.json"
MANIFEST = "artifact-manifest.json"


def fail(message):
    raise RuntimeError(message)


def run(args, cwd=None):
    proc = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    if proc.returncode:
        fail(f"{args[0]} failed: {proc.stderr.strip()[-1200:]}")
    return proc.stdout


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def inside(path, root):
    try:
        return os.path.commonpath((str(Path(path).resolve(strict=True)),
                                   str(Path(root).resolve(strict=True)))) == str(Path(root).resolve(strict=True))
    except (OSError, ValueError):
        return False


def safe_rel(value):
    path = Path(value)
    if path.is_absolute() or not path.parts or ".." in path.parts or "\\" in value:
        fail(f"unsafe relative path: {value}")
    return path.as_posix()


def package_rel(root, path):
    try:
        return Path(path).resolve(strict=True).relative_to(root.resolve(strict=True)).as_posix()
    except (OSError, ValueError):
        fail(f"path is outside package: {path}")


def scan(root):
    found = {}
    for base, dirs, files in os.walk(root, followlinks=False):
        dirs.sort()
        files.sort()
        for name in list(dirs):
            path = Path(base) / name
            if path.is_symlink():
                found[path.relative_to(root).as_posix()] = {"link": os.readlink(path)}
                dirs.remove(name)
        for name in files:
            path = Path(base) / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink():
                found[rel] = {"link": os.readlink(path)}
            elif path.is_file():
                found[rel] = {"file": digest(path)}
            else:
                fail(f"unsupported filesystem entry: {path}")
    return dict(sorted(found.items()))


def table(path, fields, header=False, empty=False):
    lines = path.read_text().splitlines()
    if header:
        if not lines or lines[0].split("\t") != list(fields):
            fail(f"unexpected table header: {path}")
        lines = lines[1:]
    elif not lines and not empty:
        fail(f"empty receipt table: {path}")
    rows = []
    for line in lines:
        values = line.split("\t")
        if len(values) != len(fields):
            fail(f"malformed table row: {path}")
        rows.append(dict(zip(fields, values)))
    return rows


def hash_receipt(path):
    result = {}
    for line in path.read_text().splitlines():
        if len(line) < 67 or line[64:66] != "  ":
            fail(f"malformed SHA256 receipt: {path}")
        value, rel = line[:64], safe_rel(line[66:])
        if not re.fullmatch(r"[0-9a-f]{64}", value) or rel in result:
            fail(f"invalid or duplicate SHA256 receipt: {path}: {rel}")
        result[rel] = value
    if not result:
        fail(f"empty SHA256 receipt: {path}")
    return result


def compare_tree(tree, records, prefixes, label, excluded=(), additions=False):
    prefixes = tuple(prefixes)
    if any(not p.startswith(prefixes) for p in records):
        fail(f"{label} receipt contains a path outside its retained tree")
    expected = {p: h for p, h in records.items()
                if p.startswith(prefixes) and p not in excluded}
    actual = {p: row["file"] for p, row in tree.items()
              if p.startswith(prefixes) and "file" in row and p not in excluded}
    if any(actual.get(p) != h for p, h in expected.items()) or (
            not additions and actual.keys() != expected.keys()):
        fail(f"{label} receipt file set or hash differs")


def validate_build(root):
    for name in ("install", "deps", "sources", "receipts"):
        if not (root / name).is_dir():
            fail(f"builder output is missing {name}/")
    tree = scan(root)
    source_hashes = hash_receipt(root / "receipts/sources.sha256")
    compare_tree(tree, source_hashes, ("sources/",), "source")
    native_hashes = hash_receipt(root / "receipts/native-artifacts.sha256")
    compare_tree(tree, native_hashes, ("install/", "deps/"), "native artifact")
    receipt_path = "receipts/receipts.sha256"
    receipt_hashes = hash_receipt(root / receipt_path)
    compare_tree(tree, receipt_hashes, ("receipts/",), "receipt", (receipt_path,))
    links = {}
    for row in table(root / "receipts/native-symlinks.tsv", ("path", "target"), empty=True):
        rel = safe_rel(row["path"])
        if rel in links or not rel.startswith(("install/", "deps/")):
            fail(f"invalid native symlink receipt: {rel}")
        links[rel] = row["target"]
    actual_links = {p: row["link"] for p, row in tree.items()
                    if "link" in row and p.startswith(("install/", "deps/"))}
    if links != actual_links:
        fail("native symlink set differs from receipt")
    for rel, target in links.items():
        path = root / rel
        if os.readlink(path) != target or not inside(path, root / rel.split("/", 1)[0]):
            fail(f"native symlink escapes its installed tree: {rel}")
    pins = json.loads((root / "sources/recipes/sources.json").read_text())
    return tree, native_hashes, pins


def verify_retained(root, tree, manifest, native_hashes):
    compare_tree(tree, hash_receipt(root / "receipts/receipts.sha256"), ("receipts/",), "receipt", ("receipts/receipts.sha256",))
    compare_tree(tree, hash_receipt(root / "receipts/sources.sha256"),
                 ("sources/",), "source", additions=True)
    for path, checksum in native_hashes.items():
        if not path.startswith("install/"):
            continue
        rel, entry = path[len("install/"):], tree.get(path[len("install/"):])
        if not entry:
            fail(f"installed native artifact is missing: {rel}")
        if magic(root / rel):
            prov = manifest["elf"].get(rel, {}).get("provenance", {})
            if prov.get("source_key") != path or prov.get("original_sha256") != checksum:
                fail(f"installed ELF provenance differs from native receipt: {rel}")
        elif entry.get("file") != checksum:
            fail(f"installed non-ELF bytes differ from native receipt: {rel}")
    for row in table(root / "receipts/native-symlinks.tsv", ("path", "target"), empty=True):
        if row["path"].startswith("install/") and tree.get(row["path"][8:], {}).get("link") != row["target"]:
            fail(f"installed symlink differs from native receipt: {row['path']}")


def product_record(root):
    env = (root / "receipts/environment.txt").read_text()
    revision = re.search(r"(?m)^product_revision\t([0-9a-f]{40})$", env)
    archive_hash = re.search(r"(?m)^product_archive_sha256\t([0-9a-f]{64})$", env)
    candidates = [p for p in ("sources/product.tar", "sources/product.tar.gz")
                  if (root / p).is_file()]
    if not revision or not archive_hash or len(candidates) != 1:
        fail("product revision/hash receipt or archive is missing")
    rel = candidates[0]
    if digest(root / rel) != archive_hash.group(1):
        fail("product archive differs from its retained receipt")
    return {"revision": revision.group(1), "archive_path": rel,
            "archive_sha256": archive_hash.group(1)}


def compiler_record(root):
    text = (root / "receipts/environment.txt").read_text()
    result = {}
    for name in ("gcc", "g++"):
        match = re.search(rf"(?ms)^\[{re.escape(name)}\]\n([^\n]+)", text)
        if not match:
            fail(f"compiler identity is missing from environment receipt: {name}")
        result[name] = match.group(1)
    return result


def pin_inputs(root, output=None):
    pins = json.loads((root / "sources/recipes/sources.json").read_text())
    rows = table(root / "receipts/sources.tsv",
                 ("name", "url", "commit", "archive_sha256", "notices"), header=True)
    by_name = {row["name"]: row for row in rows}
    if len(by_name) != len(rows) or set(by_name) != {p["name"] for p in pins["git_sources"]}:
        fail("pinned source list is incomplete or duplicated")
    for item in pins["git_sources"]:
        row = by_name[item["name"]]
        archive_rel = f"sources/dependencies/{item['name']}-{item['commit']}.tar"
        archive = root / archive_rel
        if not archive.is_file() or (row["url"], row["commit"], row["notices"],
                                     row["archive_sha256"]) != (
                item["url"], item["commit"], "|".join(item["notices"]), digest(archive)):
            fail(f"pinned archive identity mismatch: {item['name']}")
        with tarfile.open(archive, "r:*") as source:
            for notice in item["notices"]:
                member = f"{item['name']}/{safe_rel(notice)}"
                stream = source.extractfile(member)
                if stream is None:
                    fail(f"pinned source notice is missing: {member}")
                data = stream.read()
                if not data:
                    fail(f"pinned source notice is empty: {member}")
                target_rel = f"share/licenses/pinned/{item['name']}/{notice}"
                target = (output or root) / target_rel
                if output:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                elif not target.is_file() or target.read_bytes() != data:
                    fail(f"staged pinned notice differs from source: {target_rel}")


def magic(path):
    with open(path, "rb") as stream:
        return stream.read(4) == b"\x7fELF"


def readelf(path):
    if not magic(path):
        return None
    text = run(["readelf", "-h", "-l", "-d", "--version-info", str(path)])
    cls = re.search(r"(?m)^\s*Class:\s*(.+)$", text)
    data = re.search(r"(?m)^\s*Data:\s*(.+)$", text)
    machine = re.search(r"(?m)^\s*Machine:\s*(.+)$", text)
    if not cls or not data or not machine:
        fail(f"incomplete ELF header: {path}")
    if cls.group(1).strip() != "ELF64" or "little endian" not in data.group(1):
        fail(f"unsupported ELF class or byte order: {path}")
    interp = re.search(r"Requesting program interpreter:\s*([^]]+)\]", text)
    needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", text)
    soname = re.search(r"\(SONAME\).*?\[([^]]+)\]", text)
    runpath = re.search(r"\(RUNPATH\).*?\[([^]]*)\]", text)
    rpath = re.search(r"\(RPATH\).*?\[([^]]*)\]", text)
    versions, in_needs = [], False
    for line in text.splitlines():
        if "Version needs section" in line:
            in_needs = True
        elif "Version definition section" in line or "Version symbols section" in line:
            in_needs = False
        if in_needs:
            match = re.search(r"\bName:\s*([A-Za-z0-9_.]+)", line)
            if match:
                versions.append(match.group(1))
    return {"machine": machine.group(1).strip(),
            "interpreter": interp.group(1).strip() if interp else None,
            "soname": soname.group(1) if soname else None,
            "needed": sorted(set(needed)),
            "runpath": runpath.group(1).split(":") if runpath else [],
            "rpath": rpath.group(1).split(":") if rpath else [],
            "version_needs": sorted(set(versions))}


def abi_needs(facts, arch, policy, path):
    expected = policy["architecture"][arch]
    if facts["machine"] != expected["machine"]:
        fail(f"ELF machine mismatch: {path}: {facts['machine']}")
    if facts["interpreter"] and facts["interpreter"] != expected["interpreter"]:
        fail(f"ELF interpreter mismatch: {path}: {facts['interpreter']}")
    limit = tuple(int(n) for n in policy["glibc_max"].split("."))
    for name in facts["version_needs"]:
        if name == "GLIBC_ABI_DT_RELR":
            floor = tuple(int(n) for n in policy["glibc_abi_dt_relr"].split("."))
            if floor > limit:
                fail(f"GLIBC_ABI_DT_RELR floor exceeds policy: {path}")
        elif name.startswith("GLIBC_"):
            match = re.fullmatch(r"GLIBC_(\d+(?:\.\d+)+)", name)
            if not match or name == "GLIBC_PRIVATE":
                fail(f"unknown or private GLIBC requirement {name}: {path}")
            version = tuple(int(n) for n in match.group(1).split("."))
            width = max(len(version), len(limit))
            if version + (0,) * (width - len(version)) > limit + (0,) * (width - len(limit)):
                fail(f"GLIBC requirement exceeds {policy['glibc_max']}: {path}: {name}")


def elf_facts(root, tree, policy, arch, prefix=""):
    found, sonames = {}, {}
    for source_rel, row in tree.items():
        if not source_rel.startswith(prefix) or "file" not in row or not magic(root / source_rel):
            continue
        rel = source_rel[len(prefix):]
        facts = readelf(root / source_rel)
        if not rel.startswith(("bin/", "lib/")):
            fail(f"ELF is outside bin/ and lib/: {rel}")
        abi_needs(facts, arch, policy, rel)
        if facts["interpreter"] and not rel.startswith("bin/"):
            fail(f"unexpected ELF interpreter in library tree: {rel}")
        if facts["soname"]:
            prior = sonames.get(facts["soname"])
            if prior and prior != row["file"]:
                fail(f"conflicting staged SONAME: {facts['soname']}")
            sonames[facts["soname"]] = row["file"]
        for needed in facts["needed"]:
            if not needed or "/" in needed:
                fail(f"absolute or path-like DT_NEEDED: {rel}: {needed}")
        found[rel] = facts
    return found


def validate_links(root, tree):
    for rel, row in tree.items():
        if "link" not in row:
            continue
        path, target = root / rel, row["link"]
        if Path(target).is_absolute() or not inside(path, root):
            fail(f"package symlink is absolute or escaping: {rel}")


def verify_closure(root, facts_by_path, policy, tree):
    validate_links(root, tree)
    executable = root / "bin/vkquake"
    loader = root / "bin" / policy["openxr_loader"]
    if "bin/vkquake" not in facts_by_path or not executable.is_file() or not os.access(executable, os.X_OK):
        fail("required executable is missing or not executable: bin/vkquake")
    if not loader.is_symlink() or not loader.is_file() or not inside(loader, root):
        fail(f"required executable-side OpenXR loader alias is missing: {loader.name}")
    loader_rel = loader.resolve(strict=True).relative_to(root.resolve()).as_posix()
    if (not loader_rel.startswith("lib/") or len([f for f in facts_by_path.values() if f["soname"] == policy["openxr_loader"]]) != 1 or
            facts_by_path.get(loader_rel, {}).get("soname") != policy["openxr_loader"]):
        fail("OpenXR loader alias does not resolve to its matching ELF")
    host = set(policy["host_sonames"])
    for rel in tree:
        if rel.startswith(("bin/", "lib/")) and Path(rel).name in host:
            fail(f"bundled entry shadows a host-owned SONAME: {rel}")
    for rel, facts in facts_by_path.items():
        name = policy["openxr_loader"]
        key = "openxr" if facts["soname"] == name else (
            "bin" if rel.startswith("bin/") else "lib")
        search_paths = policy["runpath"][key].split(":")
        if facts["runpath"] != search_paths or facts["rpath"]:
            fail(f"ELF RUNPATH is not normalized: {rel}")
        if facts["soname"] in host:
            fail(f"host-owned SONAME was bundled: {rel}: {facts['soname']}")
        contexts = [rel] + ([f"bin/{name}"] if key == "openxr" else [])
        context_edges = []
        for context in contexts:
            edges = {}
            for needed in facts["needed"]:
                if needed in host:
                    edges[needed] = {"owner": "host"}
                    continue
                for entry in search_paths:
                    directory = (root / Path(context).parent /
                                 entry.replace("$ORIGIN", ".")).resolve(strict=True)
                    if not inside(directory, root):
                        fail(f"ELF RUNPATH escapes package: {context}")
                    candidate = directory / needed
                    if not candidate.exists() and not candidate.is_symlink():
                        continue
                    real = candidate.resolve(strict=True)
                    if not inside(real, root):
                        fail(f"ELF dependency escapes package: {context}: {needed}")
                    provider_rel = real.relative_to(root.resolve()).as_posix()
                    provider = facts_by_path.get(provider_rel)
                    if provider is None or (provider["soname"] and provider["soname"] != needed):
                        fail(f"DT_NEEDED does not resolve to a matching internal ELF: {context}: {needed}")
                    edges[needed] = {"owner": "package", "path": provider_rel}
                    break
                if needed not in edges:
                    fail(f"unresolved non-host ELF dependency: {context}: {needed}")
            context_edges.append(edges)
        if any(edges != context_edges[0] for edges in context_edges[1:]):
            fail(f"OpenXR load contexts resolve different providers: {rel}")
        facts["resolved_needed"] = context_edges[0]


def owner_map(receipts):
    result = {}
    for row in table(receipts / "installed-file-owners.tsv", ("path", "package")):
        result.setdefault(row["path"], set()).add(row["package"])
    return result


def installed_map(receipts):
    result = {}
    for row in table(receipts / "installed-packages.tsv",
                     ("binary_package", "binary_version", "source_package", "source_version")):
        if row["binary_package"] in result:
            fail(f"duplicate installed package receipt: {row['binary_package']}")
        result[row["binary_package"]] = row
        result.setdefault(row["binary_package"].split(":", 1)[0], row)
    return result


def installed_row(installed, package):
    row = installed.get(package) or installed.get(package.split(":", 1)[0])
    if not row:
        fail(f"Ubuntu owner is absent from installed inventory: {package}")
    return row


def source_identity(source, binary, version):
    match = re.fullmatch(r"(\S+?)(?:\s+\(([^()]*)\))?", source.strip())
    return (match.group(1), match.group(2) or version) if match else (binary, version)


def installed_identity(row, binary, version):
    return row["source_package"] or binary, row["source_version"] or version


def dsc_record(path):
    raw = path.read_text(errors="strict")
    if raw.startswith("-----BEGIN PGP SIGNED MESSAGE-----"):
        parts = raw.split("\n\n", 1)
        if len(parts) != 2 or "\n-----BEGIN PGP SIGNATURE-----" not in parts[1]:
            fail(f"malformed signed source descriptor: {path}")
        raw = parts[1].split("\n-----BEGIN PGP SIGNATURE-----", 1)[0]
    message = email.parser.Parser().parsestr(raw)
    source, source_version = source_identity(
        message.get("Source", ""), "", message.get("Version", ""))
    version = message.get("Version", "")
    artifact_count, names = 0, set()
    for line in message.get("Checksums-Sha256", "").splitlines():
        if not line.strip():
            continue
        fields = line.split()
        if (len(fields) != 3 or not re.fullmatch(r"[0-9a-f]{64}", fields[0]) or
                not fields[1].isdigit()):
            fail(f"malformed source checksum record: {path}")
        name = safe_rel(fields[2])
        if name in names:
            fail(f"duplicate source checksum path: {path}: {name}")
        names.add(name)
        artifact = path.parent / name
        if not artifact.is_file() or artifact.stat().st_size != int(fields[1]) or digest(artifact) != fields[0]:
            fail(f"source archive checksum or size mismatch: {artifact}")
        artifact_count += 1
    if not source or not version or source_version != version or not artifact_count:
        fail(f"incomplete source descriptor: {path}")
    return source, version


def dsc_index(root):
    result = {}
    for path in sorted((root / "sources/ubuntu/source").rglob("*.dsc")):
        source, version = dsc_record(path)
        key = (source, version)
        row = (path, digest(path))
        if key in result and result[key][1] != row[1]:
            fail(f"conflicting source descriptors: {source}={version}")
        result[key] = row
    return result


def repository_for(package, version):
    selected = False
    for line in run(["apt-cache", "policy", package]).splitlines():
        match = re.match(r"\s*(?:\*\*\*\s+)?(\S+)\s+\d+", line)
        if match:
            selected = match.group(1) == version
        elif selected:
            uri = re.search(r"https?://\S+", line)
            if uri:
                return uri.group(0)
    fail(f"installed package repository/version missing: {package}={version}")


def deb_identity(path):
    fields = email.parser.Parser().parsestr(run(["dpkg-deb", "-f", str(path)]))
    package, version = fields.get("Package", ""), fields.get("Version", "")
    if not package or not version:
        fail(f"incomplete named deb control fields: {path}")
    source, source_version = source_identity(fields.get("Source", ""), package, version)
    arch = fields.get("Architecture", "")
    if not arch:
        fail(f"deb architecture field is missing: {path}")
    depends = fields.get("Depends", "") + "," + fields.get("Pre-Depends", "")
    dependencies = {name.split(":", 1)[0] for name in re.findall(
        r"(?<![\w.+-])[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?", depends)}
    return {"package": package, "version": version, "source": source,
            "source_version": source_version, "architecture": arch,
            "dependencies": dependencies}


def index_deb(path, index):
    info, checksum = deb_identity(path), digest(path)
    key = (info["package"], info["version"])
    if key in index and index[key][1] != checksum:
        fail(f"conflicting exact deb archives: {key}")
    index[key] = (path, checksum, info)


def deb_index(root):
    result = {}
    for path in sorted((root / "sources/ubuntu").rglob("*.deb")):
        index_deb(path, result)
    return result


def verify_deb_members(deb, expected):
    proc = subprocess.Popen(["dpkg-deb", "--fsys-tarfile", str(deb)], stdout=subprocess.PIPE)
    found = {}
    try:
        with tarfile.open(fileobj=proc.stdout, mode="r|*") as archive:
            for member in archive:
                name = member.name[2:] if member.name.startswith("./") else member.name
                if name in expected:
                    if not member.isfile() or name in found:
                        fail(f"deb payload member is missing, duplicate or nonregular: {name}")
                    stream = archive.extractfile(member)
                    if stream is None:
                        fail(f"deb payload member is missing, duplicate or nonregular: {name}")
                    h = hashlib.sha256()
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        h.update(block)
                    found[name] = h.hexdigest()
        code = proc.wait()
    finally:
        if proc.poll() is None:
            proc.stdout.close()
            proc.wait()
    if code or found != expected:
        fail(f"deb payload member set or hashes differ: {deb}")


def stage_ubuntu(package, installed, owners, debs, dscs, root, owner_set):
    row = installed_row(installed, package)
    binary, version = row["binary_package"], row["binary_version"]
    base = binary.split(":", 1)[0]
    source, source_version = installed_identity(row, base, version)
    deb_row = debs.get((base, version))
    if not deb_row:
        cache = root / "sources/ubuntu/closure/packages"
        cache.mkdir(parents=True, exist_ok=True)
        existing = set(cache.glob("*.deb"))
        run(["apt-get", "download", f"{base}={version}"], cwd=cache)
        for path in cache.glob("*.deb"):
            if path not in existing:
                index_deb(path, debs)
        deb_row = debs.get((base, version))
    if not deb_row:
        fail(f"exact installed deb is unavailable: {binary}={version}")
    deb, deb_hash, _ = deb_row
    key = (source, source_version)
    if key not in dscs:
        safe = re.sub(r"[^A-Za-z0-9.+~-]", "_", f"{source}:{source_version}")
        source_dir = root / "sources/ubuntu/closure/source" / safe
        source_dir.mkdir(parents=True, exist_ok=True)
        run(["apt-get", "source", "--download-only", f"{source}={source_version}"], cwd=source_dir)
        for path in sorted(source_dir.glob("*.dsc")):
            if dsc_record(path) == key:
                dscs[key] = (path, digest(path))
    if key not in dscs:
        fail(f"matching exact source descriptor is unavailable: {source}={source_version}")
    dsc, _ = dscs[key]
    requested = Path(f"/usr/share/doc/{base}/copyright")
    if binary not in (owners.get(str(requested), set()) |
                      owners.get(str(requested.parent), set())):
        fail(f"requested Ubuntu copyright path is not owned by {binary}")
    try:
        notice_source = requested.resolve(strict=True)
    except (OSError, RuntimeError):
        fail(f"installed Ubuntu copyright notice is missing: {binary}")
    if (not str(notice_source).startswith("/usr/share/doc/") or not notice_source.is_file() or
            len(owners.get(str(notice_source), set())) != 1):
        fail(f"installed Ubuntu copyright provider is missing or ambiguous: {binary}")
    notice_package = next(iter(owners[str(notice_source)]))
    if notice_package != binary:
        owner_set.add(notice_package)
    notice_rel = f"share/licenses/ubuntu/{base}/copyright"
    notice = root / notice_rel
    notice.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(notice_source, notice)
    notice_hash = digest(notice)
    deb_rel = package_rel(root, deb)
    dsc_rel = package_rel(root, dsc)
    member = notice_source.as_posix().lstrip("/")
    return {"binary_package": binary, "binary_version": version,
            "source_package": source, "source_version": source_version,
            "repository": repository_for(base, version),
            "deb": {"path": deb_rel, "sha256": deb_hash},
            "dsc_path": dsc_rel,
            "copyright": {"path": notice_rel, "sha256": notice_hash,
                          "requested_path": str(requested),
                          "installed_path": str(notice_source),
                          "provider_package": notice_package, "deb_member": member}}


def distro_payload(record, root, arch, installed, owners):
    deb = root / safe_rel(record["deb"]["path"])
    info = deb_identity(deb)
    if digest(deb) != record["deb"]["sha256"]:
        fail(f"exact deb hash differs from contributor receipt: {record['binary_package']}")
    identity = (info["package"], info["version"], info["source"], info["source_version"])
    expected = (record["binary_package"].split(":", 1)[0], record["binary_version"],
                record["source_package"], record["source_version"])
    if identity != expected:
        fail(f"deb control identity differs from owner record: {record['binary_package']}")
    if info["architecture"] not in ("all", {"x86_64": "amd64", "aarch64": "arm64"}[arch]):
        fail(f"deb architecture differs from artifact: {record['binary_package']}")
    source, version = dsc_record(root / safe_rel(record["dsc_path"]))
    if (source, version) != (record["source_package"], record["source_version"]):
        fail(f"source descriptor identity or checksum set differs: {record['binary_package']}")
    row = installed_row(installed, record["binary_package"])
    installed_source, installed_version = installed_identity(
        row, record["binary_package"].split(":", 1)[0], row["binary_version"])
    if (row["binary_version"], installed_source, installed_version) != (
            record["binary_version"], record["source_package"], record["source_version"]):
        fail(f"contributor identity differs from installed receipt: {record['binary_package']}")
    note = record["copyright"]
    if digest(root / safe_rel(note["path"])) != note["sha256"]:
        fail(f"Ubuntu notice changed: {record['binary_package']}")
    package = record["binary_package"]
    if (note["requested_path"], note["deb_member"]) != (f"/usr/share/doc/{package.split(':', 1)[0]}/copyright", note["installed_path"].lstrip("/")) or \
            not note["installed_path"].startswith("/usr/share/doc/") or \
            package not in (owners.get(note["requested_path"], set()) |
                            owners.get(str(Path(note["requested_path"]).parent), set())) or owners.get(note["installed_path"], set()) != {note["provider_package"]}:
        fail(f"Ubuntu notice ownership differs from installed receipt: {package}")
    provider = note["provider_package"]
    provider_row = installed_row(installed, provider)
    provider_source, provider_version = installed_identity(
        provider_row, provider.split(":", 1)[0], provider_row["binary_version"])
    if ((provider_source, provider_version) != (source, version) or
            (provider != package and provider.split(":", 1)[0] not in info["dependencies"])):
        fail(f"Ubuntu notice provider identity or dependency differs: {package}")


def add_deb_member(payloads, path, member, checksum, kind):
    path, member = safe_rel(path), safe_rel(member)
    members = payloads.setdefault(path, {})
    if member in members and members[member] != checksum:
        fail(f"conflicting exact {kind} member receipts: {path}: {member}")
    members[member] = checksum


def verify_deb_payloads(root, contributors, provenance, arch, installed, file_owners):
    payloads, evidence = {}, set()
    for package, record in contributors.items():
        if record["binary_package"] != package:
            fail(f"Ubuntu contributor key mismatch: {package}")
        distro_payload(record, root, arch, installed, file_owners)
        note = record["copyright"]
        provider = note["provider_package"]
        evidence.add(provider)
        add_deb_member(payloads, contributors[provider]["deb"]["path"],
                       note["deb_member"], note["sha256"], "deb")
    for rel, prov in provenance.items():
        if prov["origin"] != "ubuntu":
            continue
        owner = prov["owner"]
        if owner not in contributors:
            fail(f"Ubuntu ELF contributor is missing: {rel}: {owner}")
        source, resolved = prov["source_path"], prov["resolved_path"]
        if (not Path(source).is_absolute() or not Path(resolved).is_absolute() or
                (file_owners.get(source, set()) | file_owners.get(resolved, set())) != {owner} or
                safe_rel(prov["deb_member"]) != resolved.lstrip("/")):
            fail(f"Ubuntu ELF ownership provenance differs from retained receipts: {rel}")
        evidence.add(owner)
        add_deb_member(payloads, contributors[owner]["deb"]["path"],
                       prov["deb_member"], prov["original_sha256"], "ELF")
    for path, members in payloads.items():
        verify_deb_members(root / path, members)
    return evidence


def selected_contributors(pins, installed, owners):
    selected = set()
    for package in pins["apt_packages"]:
        selected.add(installed_row(installed, package)["binary_package"])
    for row in installed.values():
        name = row["binary_package"].split(":", 1)[0]
        if name.endswith(("-dev", "-static")):
            selected.add(row["binary_package"])
    for path, packages in owners.items():
        if "/usr/include/" in path or path.endswith(".a"):
            for package in packages:
                selected.add(installed_row(installed, package)["binary_package"])
    return selected


def copy_install(build, root, tree):
    source_root = build / "install"
    for rel, row in tree.items():
        if not rel.startswith("install/"):
            continue
        dest_rel = rel[len("install/"):]
        src, dest = build / rel, root / dest_rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if "link" in row:
            resolved = src.resolve(strict=True)
            if not inside(resolved, source_root):
                fail(f"installed symlink escapes its tree: {rel}")
            os.symlink(row["link"], dest)
        else:
            shutil.copy2(src, dest)


def provider_source(name, build, root, native_hashes, owners, installed,
                    policy, arch, multiarch, elf_by_path, provenance, owner_set):
    if not name or "/" in name or name in (".", ".."):
        fail(f"invalid DT_NEEDED name: {name}")
    local = root / "lib" / name
    if local.exists() or local.is_symlink():
        real = local.resolve(strict=True)
        rel = real.relative_to(root.resolve()).as_posix()
        if rel not in elf_by_path:
            facts = readelf(real)
            if facts is None:
                fail(f"local dependency alias is not ELF: {name}")
            abi_needs(facts, arch, policy, rel)
            elf_by_path[rel] = facts
        return rel, elf_by_path[rel]
    prefix = build / "deps/lib"
    system = [Path(f"/lib/{multiarch}"), Path(f"/usr/lib/{multiarch}"),
              Path("/lib"), Path("/usr/lib")]
    tiers = [[prefix], system]
    chosen = None
    for tier in tiers:
        candidates = {}
        for directory in tier:
            path = directory / name
            if not path.exists() and not path.is_symlink():
                continue
            real = path.resolve(strict=True)
            if not real.is_file():
                fail(f"dependency provider is not a regular file: {path}")
            if tier is tiers[0]:
                if not inside(real, prefix):
                    fail(f"dependency prefix symlink escapes: {path}")
                key = package_rel(build, real)
                if key not in native_hashes:
                    fail(f"unreceipted dependency-prefix provider: {key}")
                checksum = native_hashes[key]
            else:
                if not any(inside(real, r) for r in (Path("/lib"), Path("/usr/lib"))):
                    fail(f"provider escapes selected native directories: {path}")
                checksum = digest(real)
            facts = readelf(real)
            if facts is None:
                fail(f"dependency provider is not ELF: {path}")
            abi_needs(facts, arch, policy, path)
            candidates.setdefault(checksum, (path, real, facts, checksum, key if tier is tiers[0] else None))
        if len(candidates) > 1:
            fail(f"conflicting providers in one search tier: {name}")
        if candidates:
            chosen = next(iter(candidates.values()))
            break
    if not chosen:
        fail(f"unresolved non-host ELF dependency: {name}")
    lexical, real, facts, checksum, build_key = chosen
    pinned = build_key is not None
    matched_owners = set() if pinned else (owners.get(str(lexical), set()) | owners.get(str(real), set()))
    if not pinned and len(matched_owners) != 1:
        fail(f"Ubuntu provider ownership is missing or ambiguous: {lexical}")
    owner = None
    if not pinned:
        owner = installed_row(installed, next(iter(matched_owners)))["binary_package"]
        owner_set.add(owner)
    dest = root / "lib" / real.name
    dest.parent.mkdir(parents=True, exist_ok=True)
    rel = dest.relative_to(root).as_posix()
    if dest.exists() or dest.is_symlink():
        if not dest.is_file() or digest(dest) != checksum:
            fail(f"distinct providers collide at staged path: {dest.name}")
    else:
        shutil.copy2(real, dest)
    elf_by_path[rel] = facts
    origin = "pinned" if pinned else "ubuntu"
    provenance[rel] = {"origin": origin, "owner": owner, "original_sha256": checksum}
    if pinned:
        provenance[rel]["source_key"] = build_key
    else:
        provenance[rel].update({"source_path": str(lexical), "resolved_path": str(real),
                                "deb_member": real.as_posix().lstrip("/")})
    aliases = {name}
    if facts["soname"]:
        aliases.add(facts["soname"])
    for alias in aliases:
        if not alias or "/" in alias:
            fail(f"invalid provider SONAME alias: {alias}")
        link = dest.parent / alias
        if alias == dest.name:
            continue
        if link.exists() or link.is_symlink():
            if not link.is_symlink() or os.readlink(link) != dest.name:
                fail(f"staged SONAME alias collision: {link}")
        else:
            os.symlink(dest.name, link)
    return rel, facts


def verify_direct_packages(root, installed, ubuntu):
    rows = table(root / "receipts/packages.tsv",
                 ("binary_package", "binary_version", "source_package", "source_version",
                  "repository", "deb_sha256"), header=True)
    for row in rows:
        owner = installed_row(installed, row["binary_package"])["binary_package"]
        record = ubuntu.get(owner)
        if not record or (row["binary_version"], row["source_package"], row["source_version"],
                          row["repository"], row["deb_sha256"]) != (
                record["binary_version"], record["source_package"], record["source_version"],
                record["repository"], record["deb"]["sha256"]):
            fail(f"direct binary receipt differs from selected package: {owner}")


def stage(build_arg, package_arg):
    build, root = Path(build_arg).resolve(strict=True), Path(package_arg).resolve(strict=True)
    if not build.is_dir() or not root.is_dir() or any(root.iterdir()):
        fail("BUILD_OUTPUT must be a directory and EMPTY_PACKAGE_DIR must exist and be empty")
    if inside(root, build) or inside(build, root):
        fail("build and package directories must be separate")
    build_tree, native_hashes, pins = validate_build(build)
    policy = json.loads(POLICY_FILE.read_text())
    if (any(not isinstance(n, str) or not n or n.strip() != n for n in policy["host_sonames"]) or
            len(set(policy["host_sonames"])) != len(policy["host_sonames"])):
        fail("host SONAME policy entries must be exact, nonempty, and unique")
    distro = Path("/etc/os-release").read_text()
    if 'ID=ubuntu' not in distro or 'VERSION_ID="24.04"' not in distro:
        fail("staging requires the native Ubuntu 24.04 builder image")
    machine = run(["uname", "-m"]).strip()
    arch = {"x86_64": "x86_64", "aarch64": "aarch64"}.get(machine)
    if not arch or not (build / "receipts/environment.txt").is_file():
        fail(f"unsupported or unreceipted builder architecture: {machine}")
    env_arch = re.search(r"(?m)^architecture\t(\S+)$", (build / "receipts/environment.txt").read_text())
    if not env_arch or env_arch.group(1) != arch:
        fail("builder receipt and native staging architecture differ")
    multiarch = run(["dpkg-architecture", "-qDEB_HOST_MULTIARCH"]).strip()
    if multiarch != {"x86_64": "x86_64-linux-gnu", "aarch64": "aarch64-linux-gnu"}[arch]:
        fail("native dpkg library architecture differs from builder target")
    product = product_record(build)
    shutil.copytree(build / "sources", root / "sources", symlinks=True)
    shutil.copytree(build / "receipts", root / "receipts", symlinks=True)
    for item in sorted(HERE.iterdir()):
        if item.is_file():
            target = root / "sources/recipes" / item.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)
    copy_install(build, root, build_tree)
    for directory in ("bin", "lib", "share"):
        (root / directory).mkdir(exist_ok=True)
    pin_inputs(root, root)
    owners = owner_map(root / "receipts")
    installed = installed_map(root / "receipts")
    debs, dscs = deb_index(root), dsc_index(root)
    source_owners = selected_contributors(pins, installed, owners)
    initial = elf_facts(build, build_tree, policy, arch, "install/")
    elf_by_path = dict(initial)
    provenance = {}
    for rel, facts in initial.items():
        key = "install/" + rel
        checksum = build_tree[key]["file"]
        if key not in native_hashes or native_hashes[key] != checksum:
            fail(f"installed ELF differs from native artifact receipt: {rel}")
        provenance[rel] = {"origin": "product", "source_key": key,
                           "original_sha256": checksum}
    openxr = policy["openxr_loader"]
    loader_rel, _ = provider_source(openxr, build, root, native_hashes,
                                    owners, installed, policy, arch, multiarch,
                                    elf_by_path, provenance, source_owners)
    loader_target = root / "bin" / openxr
    actual_loader = root / loader_rel
    if loader_target.exists() or loader_target.is_symlink():
        fail(f"OpenXR loader root collides with installed file: {openxr}")
    os.symlink(os.path.relpath(actual_loader, loader_target.parent), loader_target)
    pending = list(initial) + [loader_rel]
    visited = set()
    while pending:
        rel = pending.pop()
        if rel in visited:
            continue
        visited.add(rel)
        facts = elf_by_path[rel]
        for needed in facts["needed"]:
            if needed in policy["host_sonames"]:
                continue
            provider, _ = provider_source(
                needed, build, root, native_hashes, owners, installed,
                policy, arch, multiarch, elf_by_path, provenance, source_owners)
            if provider not in visited:
                pending.append(provider)
    ubuntu = {}
    pending = set(source_owners)
    while pending:
        package = min(pending)
        pending.remove(package)
        if package in ubuntu:
            continue
        ubuntu[package] = stage_ubuntu(
            package, installed, owners, debs, dscs, root, source_owners)
        pending.update(source_owners - ubuntu.keys())
    evidence = verify_deb_payloads(root, ubuntu, provenance, arch, installed, owners)
    expected = selected_contributors(pins, installed, owners) | evidence
    if expected != set(ubuntu):
        fail("staged Ubuntu contributor closure differs from installed ownership")
    verify_direct_packages(root, installed, ubuntu)
    for rel, facts in sorted(elf_by_path.items()):
        if rel not in provenance:
            fail(f"ELF provenance is missing before mutation: {rel}")
        if digest(root / rel) != provenance[rel]["original_sha256"]:
            fail(f"original ELF hash changed before patchelf: {rel}")
        runpath = policy["runpath"]["openxr"] if facts["soname"] == openxr else policy["runpath"][
            "bin" if rel.startswith("bin/") else "lib"]
        run(["patchelf", "--set-rpath", runpath, str(root / rel)])
    after = scan(root)
    final_facts = elf_facts(root, after, policy, arch)
    verify_closure(root, final_facts, policy, after)
    records = {rel: {"facts": facts, "provenance": provenance[rel]}
               for rel, facts in final_facts.items()}
    compiler = compiler_record(root)
    manifest = {"format": 1, "architecture": arch, "product": product,
                "compiler": compiler, "host_policy_sha256": digest(POLICY_FILE),
                "elf": records, "inventory": after, "ubuntu_owners": ubuntu}
    (root / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"staged {arch}: {len(records)} ELF files, {len(ubuntu)} Ubuntu contributors")


def verify(package_arg):
    root = Path(package_arg).resolve(strict=True)
    if not root.is_dir():
        fail("PACKAGE_DIR must be a directory")
    manifest_path = root / MANIFEST
    if not manifest_path.is_file():
        fail(f"manifest is missing: {MANIFEST}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != 1:
        fail("unsupported artifact manifest format")
    policy = json.loads(POLICY_FILE.read_text())
    policy_copy = root / "sources/recipes/host-policy.json"
    if (not policy_copy.is_file() or digest(policy_copy) != digest(POLICY_FILE) or
            manifest.get("host_policy_sha256") != digest(policy_copy)):
        fail("artifact host policy differs from verifier policy")
    tree = scan(root)
    actual = {p: row for p, row in tree.items() if p != MANIFEST}
    if actual != manifest["inventory"]:
        fail("package file/hash or symlink inventory differs from manifest")
    tops = {p.split("/", 1)[0] for p in actual} | {MANIFEST}
    if tops != {"bin", "lib", "share", "sources", "receipts", MANIFEST}:
        fail("package has missing or additional top-level entries")
    arch = manifest.get("architecture")
    if arch not in policy["architecture"]:
        fail(f"unsupported artifact architecture: {arch}")
    product = product_record(root)
    if product != manifest["product"] or compiler_record(root) != manifest["compiler"]:
        fail("product or compiler identity differs from retained receipts")
    pin_inputs(root)
    native_hashes = hash_receipt(root / "receipts/native-artifacts.sha256")
    verify_retained(root, tree, manifest, native_hashes)
    owners, installed = owner_map(root / "receipts"), installed_map(root / "receipts")
    provenance = {rel: row["provenance"] for rel, row in manifest["elf"].items()}
    evidence = verify_deb_payloads(root, manifest["ubuntu_owners"], provenance,
                                   arch, installed, owners)
    expected = selected_contributors(json.loads(
        (root / "sources/recipes/sources.json").read_text()), installed, owners) | evidence
    if expected != set(manifest["ubuntu_owners"]):
        fail("Ubuntu contributor set differs from installed package/resource ownership")
    verify_direct_packages(root, installed, manifest["ubuntu_owners"])
    facts = elf_facts(root, tree, policy, arch)
    verify_closure(root, facts, policy, tree)
    if set(facts) != set(manifest["elf"]):
        fail("ELF file set differs from artifact manifest")
    for rel, current in facts.items():
        record = manifest["elf"][rel]
        if current != record["facts"]:
            fail(f"static ELF facts or local dependency closure differ: {rel}")
        prov = record["provenance"]
        origin = prov["origin"]
        if origin in ("product", "pinned"):
            key = safe_rel(prov["source_key"])
            if native_hashes.get(key) != prov["original_sha256"]:
                fail(f"original ELF hash differs from retained native receipt: {rel}")
            if origin == "product" and not key.startswith("install/"):
                fail(f"product ELF provenance is inconsistent: {rel}")
            if origin == "pinned" and not key.startswith("deps/"):
                fail(f"pinned ELF provenance is inconsistent: {rel}")
        elif origin != "ubuntu":
            fail(f"unknown ELF provenance origin: {rel}")
    print(f"verified {arch}: {len(facts)} ELF files, {len(manifest['inventory'])} inventoried entries")


def main(argv):
    if len(argv) == 4 and argv[1] == "stage":
        stage(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == "verify":
        verify(argv[2])
    else:
        fail("usage: package.py stage BUILD_OUTPUT EMPTY_PACKAGE_DIR | package.py verify PACKAGE_DIR")


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, KeyError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"package.py: {exc}", file=sys.stderr)
        sys.exit(1)
