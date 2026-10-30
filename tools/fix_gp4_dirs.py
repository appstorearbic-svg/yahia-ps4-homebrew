#!/usr/bin/env python3
"""Ensure every parent directory of a GP4 file exists in its rootdir tree."""
import sys
import xml.etree.ElementTree as ET

path = sys.argv[1]
tree = ET.parse(path)
root = tree.getroot()
rootdir = root.find("rootdir")
if rootdir is None:
    raise SystemExit("GP4 is missing rootdir")

def find_or_add(parent, name):
    for child in parent.findall("dir"):
        if child.get("targ_name") == name:
            return child
    return ET.SubElement(parent, "dir", {"targ_name": name})

for entry in root.findall("./files/file"):
    target = entry.get("targ_path", "")
    parts = [part for part in target.split("/") if part]
    node = rootdir
    for part in parts[:-1]:
        node = find_or_add(node, part)

tree.write(path, encoding="UTF-8", xml_declaration=True)
