#pragma once
#include "array.h"
#include "file.h"

class zip : nocopy {
	struct locfhead;
	struct centdirrec;
	struct endofcdr;
	
	endofcdr* getendofcdr(arrayview<uint8_t> data);
	centdirrec* getcdr(arrayview<uint8_t> data, endofcdr* end);
	centdirrec* nextcdr(arrayview<uint8_t> data, centdirrec* cdr);
	locfhead* geth(arrayview<uint8_t> data, centdirrec* cdr);
	arrayview<uint8_t> fh_fname(arrayview<uint8_t> data, locfhead* fh);
	arrayview<uint8_t> fh_data(arrayview<uint8_t> data, locfhead* fh, centdirrec* cdr);
	
	array<string> filenames;
	struct file {
		//would've put filenames here too, but then I'd need funky tricks in files()
		uint32_t decomplen;
		uint16_t method;
		array<uint8_t> data;
		uint32_t crc32;
		uint32_t dosdate;
	};
	array<file> filedat;
	
	bool corrupt = false;
	
public:
	zip() {}
	zip(arrayview<uint8_t> data) { init(data); }
	//Whether the ZIP was corrupt prior to unpacking.
	//If yes, it's recommended to call pack() and overwrite it.
	bool repaired() const { return corrupt; }
	
	bool init(arrayview<uint8_t> data);
	
	//Invalidated whenever the file list changes.
	arrayview<string> files() const
	{
		return filenames;
	}
	
private:
	size_t find_file(cstring name) const;
	static array<uint8_t> unpackfiledat(file& f);
public:
	
	//Index -1 will safely say 'file not found'. Other nonexistent indices are undefined behavior.
	bool read_idx(size_t id, array<uint8_t>& out, bool permissive, string* error, time_t * time = NULL) const;
	bool read_idx(size_t id, array<uint8_t>& out, string* error, time_t * time = NULL) const
	{
		return read_idx(id, out, false, error, time);
	}
	bool read_idx(size_t id, array<uint8_t>& out, time_t * time = NULL) const
	{
		return read_idx(id, out, NULL, time);
	}
	array<uint8_t> read_idx(size_t id, time_t * time = NULL) const
	{
		array<uint8_t> ret;
		read_idx(id, ret, time);
		return ret;
	}
	
	bool read(cstring name, array<uint8_t>& out, bool permissive, string* error, time_t * time = NULL) const
	{
		return read_idx(find_file(name), out, permissive, error, time);
	}
	bool read(cstring name, array<uint8_t>& out, string* error, time_t * time = NULL) const
	{
		return read(name, out, false, error, time);
	}
	bool read(cstring name, array<uint8_t>& out, time_t * time = NULL) const
	{
		return read(name, out, NULL, time);
	}
	array<uint8_t> read(cstring name, time_t * time = NULL) const
	{
		array<uint8_t> ret;
		read(name, ret, time);
		return ret;
	}
	
	//Writing a blank array deletes the file.
	//If date is 0 or absent, keeps the date unchanged on the file if it existed, otherwise uses current time.
	//Deleting an index is not recommended, since that renumbers all other files.
	//Nonexistent indices, including -1, are undefined behavior.
	void replace_idx(size_t id, arrayview<uint8_t> data, time_t date = 0);
	void write(cstring name, arrayview<uint8_t> data, time_t date = 0);
	void replace_idx(size_t id, cstring data, time_t date = 0) { replace_idx(id, data.bytes(), date); }
	void write(cstring name, cstring data, time_t date = 0) { write(name, data.bytes(), date); }
	
	void sort(); // Sorts the internal files alphabetically.
	void repack(); // Decompresses all files, then recompresses them. Keeps whichever version is smaller.
	
	//Deletes __MACOSX folders and other useless nonsense. Returns whether it did anything.
	bool clean();
	
private:
	static int fileminver(const file& f);
	static bool strascii(cstring s);
public:
	array<uint8_t> pack() const;
};
