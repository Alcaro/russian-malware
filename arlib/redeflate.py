#!/usr/bin/env python3

import sys, subprocess, zlib, tempfile
def inflate(buf): return zlib.decompress(buf, wbits=-15)

def deflate_slow(decomp):
	# there's no wbits param on zlib.compress; there's one on compressobj, but truncating the zlib header is easier.
	comp = zlib.compress(decomp)[2:-4]
	
	decomp_file = tempfile.NamedTemporaryFile()
	decomp_file.write(decomp)
	
	try:
		print("Running Zopfli...")
		proc = subprocess.run(["zopfli", "--deflate", "-c", decomp_file.name], stdout=subprocess.PIPE)
		if inflate(proc.stdout) != decomp: 1/0
		if len(proc.stdout) < len(comp): comp = proc.stdout
	except:
		print("failed, not installed?")
	
	return comp

def redeflate_fn(fname):
	comp = open(fname, "rb").read()
	comp_new = redeflate(inflate(comp))
	
	if len(comp_new) < len(comp):
		print("Reduced from",len(comp),"to",len(comp_new),"bytes ({0:.1%})".format(len(comp_new)/len(comp)))
		open(fname,"wb").write(comp_new)
	else:
		print("Couldn't compress further")

if __name__ == "__main__":
	redeflate_fn(sys.argv[1])
