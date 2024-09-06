#!/usr/bin/env python
import sys, os, re, subprocess

if len(sys.argv) == 1:
	in_fn = "arlib-debug.log"
elif len(sys.argv) == 2:
	in_fn = sys.argv[1]
else:
	1/0

lines = []  # [ ( "no address on this line", None, None, None ),
            #   ( "./myprogram(+0xab580)[0x5611372b4580]", "6275696c642d69640000000000000000", "./myprogram", "0xab580" ) ]
addresses = {}  # { ("./myprogram", "6275696c642d69640000000000000000"): { "0xab580": "arlib/whatever-file.cpp:550" } }
                # (or { "0xab580": None } if not set yet)
for line in open(in_fn, "rt"):
	assert line.endswith("\n")
	line = line[:-1]
	if m := re.match("^(.*)\(\+(0x[0-9a-f]+)\)\[0x[0-9a-f]+\]$", line):
		lines.append( [ line, m[1], None, m[2] ] )
	elif m := re.match("^(.*)\+(0x[0-9a-fA-F]+)$", line):
		lines.append( [ line, m[1], None, m[2] ] )
	else:
		lines.append( ( line, None, None, None ) )

next_build_id = None
build_id_for = {}  # { "./myprogram": "6275696c642d69640000000000000000" }
for pack in lines:
	line, fn, _buildid, addr = pack
	if m := re.match("^Build ID ([0-9a-fA-F]{32})$", line):
		next_build_id = m[1].lower()
	if fn:
		if next_build_id:
			# the top stack frame is in debug_log_stack(), which is the same module as has the build id
			build_id_for[fn] = next_build_id
			next_build_id = None
		pack[2] = build_id_for.get(fn, None)
		addresses.setdefault((fn, pack[2]), {})[addr] = None

def build_id_for(fn):
	for line in subprocess.run(["readelf", "-n", fn], capture_output=True).stdout.decode("utf-8").splitlines():
		if m := re.match("^\s+Build ID: ([0-9a-fA-F]{32})", line):
			return m[1].lower()

def find_exe_for_buildid(fn, buildid):
	if build_id_for(fn) == buildid:
		return fn
	
	dirs = [ ".", "builds", os.path.dirname(fn), os.path.dirname(fn)+"/builds" ]
	for dr in dirs:
		try:
			exes = os.listdir(dr)
		except Exception:
			continue
		for exe in exes:
			if buildid in exe.lower():
				exe_fn = dr+"/"+exe
				if build_id_for(exe_fn) == buildid:
					return exe_fn
	
	search_fn = os.path.realpath(fn)+" (deleted)"
	for exe in os.listdir("/proc/"):
		if exe.isdigit():
			try:
				exe_fn = "/proc/"+exe+"/exe"
				exe_fn_target = os.readlink(exe_fn)
				if exe_fn_target == search_fn and build_id_for(exe_fn) == buildid:
					return exe_fn
			except Exception:
				pass
	
	return None

for (fn, buildid), addr_map in addresses.items():
	addrs = list(addr_map.keys())
	if buildid:
		new_fn = find_exe_for_buildid(fn, buildid)
		if new_fn != fn:
			if new_fn:
				print("Build ID mismatch for", fn+", using", new_fn, "instead", file=sys.stderr)
				fn = new_fn
			else:
				print("Build ID mismatch for", fn+", expect nonsense", file=sys.stderr)
	if '.exe' in fn:
		# todo: objdump -p <exe> | grep RSDS
		for line in subprocess.run(["objdump", "-p", fn], capture_output=True).stdout.decode("utf-8").strip().split("\n"):
			if line.startswith("ImageBase"):
				base_addr = int(line[len("ImageBase"):].strip(),16)
		addrs2 = [hex(base_addr+int(n,16)) for n in addrs]
	else:
		addrs2 = addrs
	symbols = subprocess.run(["addr2line", "-e", fn] + addrs2, capture_output=True).stdout.decode("utf-8").strip().split("\n")
	assert len(symbols) == len(addrs)
	for addr, sym in zip(addrs, symbols):
		if sym != "??:?" and sym != "":
			addr_map[addr] = sym

for line, fn, buildid, addr in lines:
	if fn is not None and addresses[(fn,buildid)][addr] is not None:
		print(fn, addresses[(fn,buildid)][addr])
	else:
		print(line)
