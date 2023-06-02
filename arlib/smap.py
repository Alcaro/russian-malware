#!/usr/bin/env python3
# This program allows reading Arlib staticmaps.
# Write support, error checking and interruption recovery are not implemented. Use the C++ version if you need that.
# May screw up if the program is concurrently writing to the image cache. If so, try again, writes are quick.

import sys
if len(sys.argv) == 2:
	by = open(sys.argv[1],"rb").read()
elif len(sys.argv) == 1:
	by = sys.stdin.buffer.read()
else:
	print("bad usage")

def read_staticmap(by):
	import struct
	def rd64(pos):
		return struct.unpack_from("<Q", buffer=by, offset=pos)[0]
	
	if by[8:15] != b"arlsmap": return None
	
	ret = {}
	obj_pos = 0
	while obj_pos < len(by):
		head = rd64(obj_pos)
		if head&15 == 1:
			keylen = rd64(obj_pos+8)
			vallen = rd64(obj_pos+16)
			ret[by[obj_pos+24 : obj_pos+24+keylen]] = by[obj_pos+24+keylen : obj_pos+24+keylen+vallen]
		obj_pos += head&~15
	
	return ret

print(read_staticmap(by))
