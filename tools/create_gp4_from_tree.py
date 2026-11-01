#!/usr/bin/env python3
"""Create a PS4 GP4 project without passing every asset through argv.

create-gp4 accepts a space-separated --files argument, which hits Linux's
exec argument limit with the Orbx payload. This script writes the equivalent
GP4 XML directly and lets PkgTool.Core consume it normally.
"""
from datetime import datetime
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

if len(sys.argv) != 3:
    raise SystemExit("usage: create_gp4_from_tree.py OUTPUT.gp4 CONTENT_ID")

out = Path(sys.argv[1])
content_id = sys.argv[2]
root = Path.cwd()

required = [
    Path("eboot.bin"),
    Path("sce_sys/param.sfo"),
    Path("sce_sys/icon0.png"),
]
required += sorted(Path("sce_module").glob("*"))
for tree in (Path("payload"), Path("assets")):
    if tree.exists():
        required += sorted(p for p in tree.rglob("*") if p.is_file())

files = []
seen = set()
for path in required:
    rel = path.as_posix()
    if path.is_file() and rel not in seen:
        files.append(rel)
        seen.add(rel)

if not files:
    raise SystemExit("no project files found")

psproject = ET.Element("psproject", {"fmt": "gp4", "version": "1000"})
volume = ET.SubElement(psproject, "volume")
ET.SubElement(volume, "volume_type").text = "pkg_ps4_app"
ET.SubElement(volume, "volume_id").text = "PS4VOLUME"
ET.SubElement(volume, "volume_ts").text = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
ET.SubElement(volume, "package", {
    "content_id": content_id,
    "passcode": "00000000000000000000000000000000",
    "storage_type": "digital50",
    "app_type": "full",
})
chunk_info = ET.SubElement(volume, "chunk_info", {"chunk_count": "1", "scenario_count": "1"})
chunks = ET.SubElement(chunk_info, "chunks")
ET.SubElement(chunks, "chunk", {"id": "0", "layer_no": "0", "label": "Chunk #0"})
scenarios = ET.SubElement(chunk_info, "scenarios", {"default_id": "0"})
ET.SubElement(scenarios, "scenario", {
    "id": "0", "type": "sp", "initial_chunk_count": "1", "label": "Scenario #0"
}).text = "0"

file_node = ET.SubElement(psproject, "files", {"img_no": "0"})
for rel in files:
    ET.SubElement(file_node, "file", {"targ_path": rel, "orig_path": rel})

rootdir = ET.SubElement(psproject, "rootdir")
for rel in files:
    node = rootdir
    parts = rel.split("/")[:-1]
    for part in parts:
        child = next((x for x in node.findall("dir") if x.get("targ_name") == part), None)
        if child is None:
            child = ET.SubElement(node, "dir", {"targ_name": part})
        node = child

ET.indent(psproject, "\t")
out.write_bytes(ET.tostring(psproject, encoding="UTF-8", xml_declaration=True))
print(f"Generated {out} with {len(files)} files")
