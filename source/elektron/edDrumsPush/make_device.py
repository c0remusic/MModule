"""Writes Lockstep Push, the Push companion (ticket 22 of .scratch/elektron-drums-v2): a Max for Live MIDI effect that
goes in front of Lockstep on its track. Its patch passes the track's MIDI through, and wires lockstep_push.js (beside
this script, which the device needs beside it) to udpsend and udpreceive on 127.0.0.1, to the track's MIDI output for
the hello's notes, to the global send by which one companion armed disarms the others, and to its Push key.

The .amxd container is the one Live 12's own "Max MIDI Effect.amxd" uses (as md-drums-seq writes its devices):
"ampf", u32 4, "mmmm" (MIDI effect), "meta", u32 4, 4 zero bytes, "ptch", u32 length, then the patcher JSON ending in
a NUL.

Run: python -I make_device.py <folder>   (writes "Lockstep Push.amxd" and lockstep_push.js there; drag the .amxd into
Live from that folder, or copy both into the User Library)
"""
import json
import pathlib
import shutil
import struct
import sys

HIDDEN = 2		# a parameter neither automated nor stored with the set: Push is off as the device loads


def box(box_id, maxclass, rect, inlets, outlets, text=None, outlettype=None, present=None, **extra):
	b = {"id": box_id, "maxclass": maxclass, "numinlets": inlets, "numoutlets": outlets, "patching_rect": rect}
	if text is not None:
		b["text"] = text
	if outlettype is not None:
		b["outlettype"] = outlettype
	if present is not None:
		b["presentation"] = 1
		b["presentation_rect"] = present
	b.update(extra)
	return {"box": b}


def line(src, outlet, dst, inlet):
	return {"patchline": {"source": [src, outlet], "destination": [dst, inlet]}}


def patcher(title, boxes, lines, width):
	return {"patcher": {
		"fileversion": 1,
		"appversion": {"major": 9, "minor": 0, "revision": 7, "architecture": "x64", "modernui": 1},
		"classnamespace": "box",
		"rect": [100.0, 100.0, 760.0, 480.0],
		"openrect": [0.0, 0.0, width, 169.0],
		"openinpresentation": 1,
		"default_fontsize": 10.0,
		"default_fontface": 0,
		"default_fontname": "Arial Bold",
		"gridonopen": 1,
		"gridsize": [8.0, 8.0],
		"devicewidth": width,
		"description": "",
		"digest": "",
		"tags": "",
		"style": "",
		"title": title,
		"boxes": boxes,
		"lines": lines,
		"dependency_cache": [],
		"latency": 0,
		"project": {
			"version": 1, "creationdate": 3590052786, "modificationdate": 3590052786,
			"viewrect": [0.0, 0.0, 300.0, 500.0], "autoorganize": 1, "hideprojectwindow": 1, "showdependencies": 1,
			"autolocalize": 0, "contents": {"patchers": {}}, "layout": {}, "searchpath": {}, "detailsvisible": 0,
			"amxdtype": 1835887981, "readonly": 0, "devpathtype": 0, "devpath": ".", "sortmode": 0, "viewmode": 0,
		},
		"autosave": 0,
	}}


def write_amxd(path, patch):
	body = json.dumps(patch, indent="\t").encode("utf-8") + b"\n\x00"
	data = b"ampf" + struct.pack("<I", 4) + b"mmmm" + b"meta" + struct.pack("<I", 4) + b"\x00\x00\x00\x00"
	data += b"ptch" + struct.pack("<I", len(body)) + body
	path.write_bytes(data)


def device():
	push_key = box("obj-6", "live.text", [400, 20, 60, 20], 1, 2, "PUSH", ["", ""], [8, 8, 60, 20],
		parameter_enable=1, varname="Push", mode=1, texton="PUSH",
		saved_attribute_attributes={"valueof": {"parameter_longname": "Push", "parameter_shortname": "Push",
			"parameter_type": 2, "parameter_invisible": HIDDEN, "parameter_enum": ["off", "on"], "parameter_mmax": 1,
			"parameter_initial_enable": 1, "parameter_initial": [0]}})
	boxes = [
		box("obj-1", "newobj", [20, 20, 40, 20], 1, 1, "midiin", ["int"]),
		box("obj-2", "newobj", [20, 420, 47, 20], 1, 0, "midiout"),
		box("obj-3", "newobj", [120, 20, 100, 20], 1, 3, "live.thisdevice", ["bang", "int", "int"]),
		box("obj-4", "message", [120, 60, 40, 20], 2, 1, "start", [""]),
		box("obj-5", "newobj", [120, 220, 160, 20], 1, 6, "js lockstep_push.js", [""] * 6),
		push_key,
		box("obj-7", "newobj", [400, 60, 70, 20], 1, 1, "prepend arm", [""]),
		box("obj-8", "newobj", [120, 300, 140, 20], 1, 0, "udpsend 127.0.0.1 9"),
		box("obj-9", "newobj", [120, 140, 110, 20], 1, 1, "udpreceive 50000", [""]),
		box("obj-10", "newobj", [300, 300, 40, 20], 1, 2, "iter", ["", ""]),
		box("obj-11", "newobj", [400, 300, 140, 20], 1, 0, "s lockstep_push_arm"),
		box("obj-12", "newobj", [400, 140, 140, 20], 1, 1, "r lockstep_push_arm", [""]),
		box("obj-13", "live.comment", [560, 300, 200, 18], 1, 0, "waiting for Lockstep", present=[76, 10, 200, 18]),
	]
	lines = [
		line("obj-1", 0, "obj-2", 0),		# the track's MIDI, through
		line("obj-3", 0, "obj-4", 0),
		line("obj-4", 0, "obj-5", 0),
		line("obj-6", 0, "obj-7", 0),
		line("obj-7", 0, "obj-5", 0),
		line("obj-9", 0, "obj-5", 0),		# OSC from Lockstep
		line("obj-12", 0, "obj-5", 0),		# another companion armed
		line("obj-5", 0, "obj-8", 0),
		line("obj-5", 1, "obj-9", 0),
		line("obj-5", 2, "obj-10", 0),
		line("obj-10", 0, "obj-2", 0),		# the hello's notes
		line("obj-5", 3, "obj-11", 0),
		line("obj-5", 4, "obj-6", 0),
		line("obj-5", 5, "obj-13", 0),
	]
	return patcher("Lockstep Push", boxes, lines, 290.0)


if __name__ == "__main__":
	if len(sys.argv) != 2:
		sys.exit("usage: make_device.py <folder>")
	out = pathlib.Path(sys.argv[1])
	out.mkdir(parents=True, exist_ok=True)
	here = pathlib.Path(__file__).resolve().parent
	write_amxd(out / "Lockstep Push.amxd", device())
	shutil.copyfile(here / "lockstep_push.js", out / "lockstep_push.js")
	print("written:", out / "Lockstep Push.amxd")
