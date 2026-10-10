#!/usr/bin/env python3
"""Compile exact production Modlist and JSON sources under ASan/UBSan.

Only filesystem/VFS/platform boundaries are supplied by the fixture. No engine
build or installed assets are needed. Run: python3 tests/modlist_metadata_fixture.py
"""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    offset = text.index(start)
    return text[offset:text.index(end, offset)]


def mapdb(directory, title):
    return json.dumps({"episodes": [{"dir": directory, "name": title}]})


host = (ROOT / "Quake/host_cmd.c").read_text()
common = (ROOT / "Quake/common.c").read_text()
types = between((ROOT / "Quake/quakedef.h").read_text(),
                "typedef struct filelist_item_s", "extern filelist_item_t *modlist;")
types += between((ROOT / "Quake/common.h").read_text(),
                 "typedef struct searchpath_s", "extern searchpath_t *com_searchpaths;")
production = between(common, "size_t UTF8_WriteCodePoint (", "// clang-format off")
production += '#include "json.h"\n'
production += (ROOT / "Quake/json.c").read_text().replace('#include "quakedef.h"', "", 1)
production += between(host, "static filelist_item_t *FileList_AddEx (", "/*\n==================\nFileList_Add\n")
production += between(host, "static void FileList_Clear (", "static void FileList_Init (")
production += between(host, "filelist_item_t *modlist;", "//==============================================================================\n// ericw -- demo list")
source = (ROOT / "tests/modlist_metadata_fixture.c").read_text()
source = source.replace("/* TYPES */", types).replace("/* PRODUCTION */", production)

with tempfile.TemporaryDirectory(prefix="qsvr-modlist-metadata-") as temp:
    work = Path(temp)
    roots = [work / "base", work / "overlay"]
    directories = ["id1", "hipnotic", "rogue", "active", "dependency", "declared",
                   "unrelated", "copper", "derived", "description", "loose",
                   "catalog", "overlay", "localized"]
    for root in roots:
        for directory in directories:
            (root / directory / "maps").mkdir(parents=True)
    base, overlay = roots
    (base / "description/descript.ion").write_text("\n  Description title  \nIgnored line\n")
    (base / "description/mapdb.json").write_text(mapdb("description", "Losing mapdb title"))
    (base / "loose/mapdb.json").write_text(mapdb("renamed-loose", "Loose title"))
    (base / "derived/mapdb.json").write_text(mapdb("copper", "Must not label derived"))
    (base / "localized/mapdb.json").write_text(mapdb("localized", "$fixture_name"))
    (overlay / "overlay/descript.ion").write_text("Overlay title\n")
    for root, title in [(base, "Catalog title"), (overlay, "Later catalog title")]:
        (root / "addons.json").write_text(json.dumps({"addons": [
            {"gamedir": directory.upper(), "name": title}
            for directory in ["description", "loose", "catalog", "overlay"]
        ]}))
    fixture = work / "fixture.c"
    fixture.write_text(source)
    executable = work / "fixture"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(ROOT / "Quake"), str(fixture), "-o", str(executable)], check=True)
    subprocess.run([str(executable), *map(str, roots)], check=True)
