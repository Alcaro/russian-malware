#include "staticmap.h"
#include "endian.h"
#include "hash.h"

static_assert(sizeof(uint8_t) == 1);
static_assert(sizeof(uint64_t) == 8);

enum {
	// Every object starts with a u64 size+type. Size is a power of two >= 32, and type is one of the t_ values.
	t_typemask = 15,
	off_header = 0,
	// The object is then followed by
	
	t_free = 0,
	off_f_nextobj = 8, // pointer to next object of that size
	off_f_prevptr = 16, // pointer to previous next pointer; points to either another freelist object, or the root object's freelists
	// the rest of the freespace object is ignored
	
	t_data = 1,
	off_d_keysz = 8,
	off_d_valsz = 16,
	off_d_keystart = 24,
	// no off_d_valstart, it's variable
	// there may be padding after the value
	
	t_meta = 2, // either hashmap or root object
	// hashmap starts with an unused u64 at offset 8, then a sequence of
	off_h_ptr = 0, // 0 if empty, 1 for tombstone, else file offset (always a multiple of 32)
	off_h_hash = 8, // full hash
	off_h_nodesize = 16,
	// the root object contains
	off_r_sig = 8, // "arlsmap" 0x00
	sig_passive = 0x0070616d736c7261,
	sig_active  = 0x0170616d736c7261,
	off_r_sigactive = 15, // the 0x00 is instead 0x01 while the staticmap object exists in any process
	off_r_hm_ptr = 16, // pointer to hashmap (hashmap size can be read from its object header)
	off_r_hm_load = 24, // entries plus tombstones, not counting slot zero
	off_r_entries = 32, // number of proper entries (also hardcoded in size() in staticmap.h)
	// followed by pointer to first freespace of size 0x20, then 0x40, etc until size 0x80000000'00000000
	off_r_objsize = 512,
};

static uint64_t off_r_freespace(uint64_t bits) { return bits*8; }

static uint64_t sm_hash(bytesr by)
{
	const uint8_t * val = by.ptr();
	size_t n = by.size();
	
	if (END_LITTLE && sizeof(size_t) == 8)
		return hash_shuffle(hash(val, n));
	
	uint64_t hash = 5381;
	while (n >= sizeof(uint64_t))
	{
		hash = (hash^(hash>>7)^readu_le64(val)) * 2546270801;
		val += sizeof(uint64_t);
		n -= sizeof(uint64_t);
	}
	while (n)
	{
		hash = (hash ^ *val) * 31;
		val++;
		n--;
	}
	return hash_shuffle(hash);
}

bool staticmap::open(cstrnul fn, bool map_writable)
{
	if (!f.open(fn, (file2::mode)(file2::m_write | file2::m_exclusive))) return false;
	this->map_writable = map_writable;
	this->sector_size = f.sector_size();
	
	mmap = f.mmapw(map_writable);
	if (mmap.size() == 0)
		create();
	else if (rd64(off_header) == t_meta+off_r_objsize && rd64(off_r_sig) == sig_passive)
		{} // do nothing
	else if (rd64(off_header) == t_meta+off_r_objsize && rd64(off_r_sig) == sig_active)
	{
		if (mmap.size() <= 1024 || rd64(off_r_entries) == 0)
			create();
		else
			recover();
	}
	else
		f.close();
	return true;
}

void staticmap::create()
{
	mmap = nullptr;
	uint8_t buf[off_r_objsize + 512];
	
	writeu_le64(buf+off_header, off_r_objsize+t_meta);
	writeu_le64(buf+off_r_sig, sig_active);
	writeu_le64(buf+off_r_hm_ptr, 512); // hashmap pointer
	writeu_le64(buf+off_r_hm_load, 0); // number of used hashmap slots
	writeu_le64(buf+off_r_entries, 0); // number of object members
	for (int i=5;i<=9;i++) writeu_le64(buf+off_r_freespace(i), 0); // no freespaces of size 32 to 512
	for (int i=10;i<=15;i++) writeu_le64(buf+off_r_freespace(i), 1<<i); // have freespaces of size 1024 to 32768
	for (int i=16;i<=63;i++) writeu_le64(buf+off_r_freespace(i), 1); // no freespaces of size 65536 to 9223372036854775808
	
	writeu_le64(buf+512, 512+t_meta); // hashmap header
	for (int i=2;i<=63;i++) writeu_le64(buf+512+i*8, 0); // fill the hashmap
	f.pwrite(0, buf);
	
	f.resize(65536, mmap, map_writable);
	
	for (int i=10;i<=15;i++)
		wr64(1<<i, (1<<i)+t_free, 0, off_r_freespace(i)); // header, next_obj, prev_ptr
}

void staticmap::recover()
{
	// at unclean shutdown,
	// - hashmap contents are untrustworthy
	// - freelist contents are untrustworthy
	// - hashmap size/load may be off by one
	// however, we still know that
	// - every object is valid and contains its size
	// - every data object contains proper data
	// - there are no mergeable freelist objects
	// - the hashmap pointer is valid
	//     though there may be an extra hashmap object at another address
	
	uint64_t hashmap_pos = rd64(off_r_hm_ptr);
	uint64_t hashmap_size = rd64(hashmap_pos+off_header) & ~t_typemask;
	uint64_t free_this = 0; // either a duplicate hashmap or a duplicate data node; there can only be one
	
	uint8_t new_root[off_r_objsize];
	memset(new_root, 0, sizeof(new_root));
	
	bytearray new_map_buf;
	new_map_buf.resize(hashmap_size);
	uint8_t* new_map = new_map_buf.ptr();
	wr64(new_map, hashmap_size+t_meta);
	
	uint64_t num_obj = 0;
	uint64_t pos = off_r_objsize;
	while (pos < mmap.size())
	{
		uint64_t head = rd64(pos+off_header);
		uint64_t type = head & t_typemask;
		uint64_t size = head & ~t_typemask;
		if (head == 0) // if recently expanded, create a freelist object here
			size = (pos & -pos); // this will blow up if the file size can be not power of two
		if (type == t_free)
		{
			uint64_t freelist_ptr = off_r_freespace(ilog2(size));
			uint64_t prev_obj = readu_le64(new_root+freelist_ptr);
			if (prev_obj > 1)
				wr64(prev_obj+off_f_prevptr, pos+off_f_nextobj);
			else
				prev_obj = 0;
			wr64(new_root+freelist_ptr, pos);
			wr64(pos, size+t_free, prev_obj, freelist_ptr);
		}
		else if (type == t_data)
		{
			uint64_t keylen = rd64(pos+off_d_keysz);
			bytesr key = mmap.slice(pos+off_d_keystart, keylen);
			uint64_t hash = sm_hash(key);
			
			uint64_t dst_off = hash & (new_map_buf.size()-off_h_nodesize);
			while (true)
			{
				// this reads the object header if dst_off = 0; this is fine, it's nonzero
				uint64_t node_addr = readu_le64(new_map+dst_off+off_h_ptr);
				if (node_addr == 0)
					break;
				if (readu_le64(new_map+dst_off+off_h_hash) == hash)
				{
					bytesr node_key = mmap.slice(node_addr+off_d_keystart, rd64(node_addr+off_d_keysz));
					if (key == node_key)
					{
						free_this = pos;
						goto skip;
					}
				}
				dst_off = (dst_off+off_h_nodesize) & (new_map_buf.size()-off_h_nodesize);
			}
			num_obj++;
			wr64(new_map+dst_off, pos, hash);
		skip: ;
		}
		else if (type == t_meta && pos != hashmap_pos)
		{
			free_this = pos;
		}
		// else it's the actual hashmap, so do nothing
		pos += size;
	}
	for (int i=63;readu_le64(new_root+off_r_freespace(i))==0;i--)
		writeu_le64(new_root+off_r_freespace(i), 1);
	wr64(new_root+off_r_hm_load, num_obj);
	wr64(new_root+off_r_entries, num_obj);
	f.pwrite(off_r_hm_load, bytesr(new_root).skip(off_r_hm_load));
	f.pwrite(hashmap_pos, new_map_buf);
	
	if (free_this != 0)
		free(free_this);
}

uint64_t staticmap::alloc(size_t bytes)
{
//printf("alloc %lu", bytes);
	uint64_t wanted_bucket = ilog2(bytes);
	
	uint64_t current_bucket = wanted_bucket;
	
again:
	uint64_t ptr = rd64(off_r_freespace(current_bucket));
	if (ptr > 1)
	{
		{
			// remove from freelist
			uint64_t next_free = rd64(ptr+off_f_nextobj);
			if (next_free)
				wr64(next_free+off_f_prevptr, off_r_freespace(current_bucket));
			wr64(off_r_freespace(current_bucket), next_free);
		}
		
	split:
		// split it, add new freespaces to their respective buckets
		uint64_t split = wanted_bucket;
		while (split < current_bucket)
		{
			uint64_t ptr_new_free = ptr+(1<<split);
			uint64_t prev_obj = rd64(off_r_freespace(split));
			if (prev_obj == 1)
			{
				for (uint64_t n=split;rd64(off_r_freespace(split))==1;n--)
					wr64(off_r_freespace(split), 0);
				prev_obj = 0;
			}
			if (prev_obj) // if freelist[n]
				wr64(rd64(off_r_freespace(split))+off_f_prevptr, ptr_new_free+off_f_nextobj); // set freelist[n]->prev to new object
			wr64(ptr_new_free, (1<<split)+t_free, prev_obj, off_r_freespace(split)); // create object
			wr64(off_r_freespace(split), ptr_new_free); // add to freelist
			split++;
		}
		// TODO: batch up writes to freelist
		
//printf(" -> %lu\n", ptr);
		return ptr;
	}
	else if (ptr == 0)
	{
		current_bucket++;
		goto again;
	}
	else // else equals 1, every future bucket is empty
	{
		current_bucket = ilog2(mmap.size());
		ptr = mmap.size();
		f.resize(mmap.size()*2, mmap, map_writable);
		goto split;
	}
}

void staticmap::free(uint64_t addr)
{
//printf("free %lu\n", addr);
	uint64_t size = rd64(addr+off_header) & ~t_typemask;
again:
	uint64_t other = addr ^ size;
	if (rd64(other+off_header) == size + t_free)
	{
		uint64_t next_obj = rd64(other+off_f_nextobj);
		f.pwrite(rd64(other+off_f_prevptr), mmap.slice(other+off_f_nextobj, 8));  // *(other.prev_ptr) = other.next_obj;
		if (next_obj)                                                             // if (other.next_obj)
			f.pwrite(next_obj+off_f_prevptr, mmap.slice(other+off_f_prevptr, 8)); //   other.next_obj->prev_ptr = other.prev_ptr;
		
		addr &= ~size;
		size *= 2;
		goto again;
	}
	
	uint64_t bucket_pos = off_r_freespace(ilog2(size));
	// bucket_pos can be 1 if the freed space is combined with a sibling, such that the entire right half of the file is now free
	uint64_t prev_free = rd64(bucket_pos) & ~1;
	if (prev_free > 1)
		wr64(prev_free+off_f_prevptr, addr+off_f_nextobj);
	wr64(addr, size+t_free, prev_free, bucket_pos);
	wr64(bucket_pos, addr);
}

uint64_t staticmap::hashmap_locate(uint64_t hash, bytesr key)
{
	uint64_t ret = 0;
	uint64_t hashmap_pos = rd64(off_r_hm_ptr);
	uint64_t hashmap_size = rd64(hashmap_pos+off_header) & ~t_typemask;
	
	// ignore bottom 4 bits of hash
	uint64_t off = hash & (hashmap_size-off_h_nodesize);
	while (true)
	{
		if (off == 0)
			off = off_h_nodesize;
		uint64_t node_addr = rd64(hashmap_pos+off+off_h_ptr);
		uint64_t node_hash = rd64(hashmap_pos+off+off_h_hash);
		if (node_addr <= 1)
		{
			if (ret == 0)
				ret = hashmap_pos+off;
			if (node_addr == 0)
				return ret;
		}
		if (node_addr > 1 && node_hash == hash)
		{
			bytesr node_key = mmap.slice(node_addr+off_d_keystart, rd64(node_addr+off_d_keysz));
			if (key == node_key)
				return hashmap_pos+off;
		}
		off = (off+off_h_nodesize) & (hashmap_size-off_h_nodesize);
	}
}

void staticmap::rehash_if_needed()
{
	uint64_t hashmap_pos = rd64(off_r_hm_ptr);
	uint64_t hashmap_size = rd64(hashmap_pos+off_header) & ~t_typemask;
	
	uint64_t new_num_nodes;
	if (rd64(off_r_entries) > hashmap_size/off_h_nodesize/2)
		new_num_nodes = hashmap_size/off_h_nodesize*2;
	else if (rd64(off_r_hm_load) > hashmap_size/off_h_nodesize*3/4)
		new_num_nodes = hashmap_size/off_h_nodesize;
	else return;
	
	bytearray new_map_buf;
	new_map_buf.resize(new_num_nodes * off_h_nodesize);
	
	uint8_t* new_map = new_map_buf.ptr();
	wr64(new_map, new_num_nodes*off_h_nodesize+t_meta);
	
	for (uint64_t src_off=16;src_off<hashmap_size;src_off+=16)
	{
		uint64_t node_addr = rd64(hashmap_pos+src_off+off_h_ptr);
		uint64_t node_hash = rd64(hashmap_pos+src_off+off_h_hash);
		if (node_addr <= 1) continue;
		
		uint64_t dst_off = node_hash & (new_map_buf.size()-off_h_nodesize);
		while (readu_le64(new_map+dst_off+off_h_ptr) != 0) // this reads the object header if dst_off = 0; this is fine, it's nonzero
			dst_off = (dst_off+off_h_nodesize) & (new_map_buf.size()-off_h_nodesize);
		wr64(new_map+dst_off, node_addr, node_hash);
	}
	
	if (hashmap_size == new_map_buf.size())
	{
		f.pwrite(hashmap_pos, new_map_buf);
		wr64(off_r_hm_load, rd64(off_r_entries));
	}
	else
	{
		uint64_t new_pos = alloc(new_map_buf.size());
		f.pwrite(new_pos, new_map_buf);
		wr64(off_r_hm_ptr, new_pos, rd64(off_r_entries));
		free(hashmap_pos);
	}
}

bytesw staticmap::get_or_empty(bytesr key, bool* found)
{
//puts("GET "+tostringhex(key));
	uint64_t hash = sm_hash(key);
	uint64_t hashmap_pos = hashmap_locate(hash, key);
	
	uint64_t ptr = rd64(hashmap_pos+off_h_ptr);
	if (found)
		*found = (ptr > 1);
	if (ptr > 1)
		return mmap.slice(ptr+off_d_keystart+key.size(), rd64(ptr+off_d_valsz));
	return {};
}

bytesw staticmap::insert(bytesr key, bytesr val)
{
//puts("INSERT "+tostringhex(key)+" "+tostringhex(val));
//puts("XINSERT "+tostringhex(key)+" "+tostring(val.size())+" 55");
	desync();
	
	rehash_if_needed();
	
	uint64_t hash = sm_hash(key);
	uint64_t hashmap_pos = hashmap_locate(hash, key);
	
	uint64_t prev_ptr = rd64(hashmap_pos+off_h_ptr);
	uint64_t alloc_size = bitround(off_d_keystart+key.size()+val.size());
	if (alloc_size <= sector_size && prev_ptr > 1 && val.size() == rd64(prev_ptr+off_d_valsz))
	{
		// choose in-place update if possible
		f.pwrite(prev_ptr+off_d_keystart+key.size(), val);
		return mmap.slice(prev_ptr+off_d_keystart+key.size(), val.size());
	}
	uint64_t pos = alloc(alloc_size);
	
	uint8_t head[off_d_keystart];
	wr64(head, alloc_size+t_data, key.size(), val.size());
	
	if (alloc_size <= sector_size)
	{
		iovec iov[] = { { head, off_d_keystart }, { (void*)key.ptr(), key.size() }, { (void*)val.ptr(), val.size() } };
		f.pwritev(pos, iov);
	}
	else
	{
		iovec iov[] = {                           { (void*)key.ptr(), key.size() }, { (void*)val.ptr(), val.size() } };
		f.pwritev(pos+off_d_keystart, iov);
		f.pwrite(pos, head);
	}
	
	if (prev_ptr > 1)
		free(prev_ptr);
	else
		wr64(off_r_hm_load, rd64(off_r_hm_load)+(prev_ptr==0), rd64(off_r_entries)+1);
	wr64(hashmap_pos, pos, hash);
	
	return mmap.slice(pos+off_r_entries+key.size(), val.size());
}

void staticmap::remove(bytesr key)
{
//puts("REMOVE "+tostringhex(key));
	desync();
	
	uint64_t hash = sm_hash(key);
	uint64_t hashmap_pos = hashmap_locate(hash, key);
	
	uint64_t ptr = rd64(hashmap_pos+off_h_ptr);
	if (ptr > 1)
	{
		free(ptr);
		wr64(hashmap_pos+off_h_ptr, 1);
		wr64(off_r_entries, rd64(off_r_entries)-1);
	}
}

void staticmap::reset()
{
//puts("RESET");
	f.resize(0);
	create();
}

void staticmap::sync()
{
	if (!*synced)
	{
		f.pwrite(off_r_sigactive, synced);
		f.sync();
	}
	*synced = true;
}

void staticmap::desync()
{
	if (!f) return;
	if (*synced)
		f.pwrite(off_r_sigactive, synced);
	*synced = false;
}

uint8_t* staticmap::iterator::next(uint8_t* it, uint8_t* end)
{
	while (it != end && (readu_le64(it) & t_typemask) != t_data)
		it += readu_le64(it) & ~t_typemask;
	return it;
}

staticmap::node staticmap::iterator::operator*()
{
	it = next(it, nullptr);
	uint64_t keylen = readu_le64(it+off_d_keysz);
	uint64_t vallen = readu_le64(it+off_d_valsz);
	return { bytesr(it+off_d_keystart, keylen), bytesw(it+off_d_keystart+keylen, vallen) };
}

staticmap::iterator& staticmap::iterator::operator++()
{
	it = next(it, nullptr);
	it += readu_le64(it) & ~t_typemask;
	return *this;
}

bool staticmap::iterator::operator!=(const iterator& other)
{
	uint8_t* a = it;
	uint8_t* b = other.it;
	if (it < other.it) a = next(a, b);
	else b = next(b, a);
	return (a != b);
}

#include "set.h"
void staticmap::fsck()
{
	array<uint64_t> objects_file;
	array<uint64_t> objects_reachable;
	uint64_t pos = 0;
	while (pos != mmap.size())
	{
		objects_file.append(pos);
		uint64_t head = rd64(pos);
		uint64_t size = head & ~t_typemask;
		if (size & (size-1)) abort(); // size must be power of two
		if ((size-1) & pos) abort(); // every object must be correctly aligned
		if (size < 32) abort(); // size must be at least 32
		pos += size;
	}
	
	objects_reachable.append(0);
	if (rd64(0) != off_r_objsize + t_meta) abort();
	if (rd64(off_r_sig) != sig_passive && rd64(off_r_sig) != sig_active) abort();
	
	uint64_t hashmap_pos = rd64(off_r_hm_ptr);
	uint64_t hashmap_head = rd64(hashmap_pos + off_header);
	objects_reachable.append(hashmap_pos);
	if ((hashmap_head & t_typemask) != t_meta) abort();
	uint64_t hashmap_size = hashmap_head & ~t_typemask;
	if (hashmap_size < 512) abort();
	
	set<bytesr> keys_seen;
	uint64_t hashmap_load = 0;
	uint64_t hashmap_items = 0;
	uint64_t hashmap_iter = 16;
	while (hashmap_iter < hashmap_size)
	{
		uint64_t ptr = rd64(hashmap_pos + hashmap_iter + off_h_ptr);
		uint64_t hash = rd64(hashmap_pos + hashmap_iter + off_h_hash);
		hashmap_iter += 16;
		
		if (ptr >= 1) hashmap_load++;
		if (ptr <= 1) continue;
		hashmap_items++;
		
		objects_reachable.append(ptr);
		uint64_t obj_head = rd64(ptr + off_header);
		if ((obj_head & t_typemask) != t_data) abort();
		uint64_t obj_size = obj_head & ~t_typemask;
		uint64_t key_size = rd64(ptr+off_d_keysz);
		uint64_t val_size = rd64(ptr+off_d_valsz);
		if (off_d_keystart+key_size+val_size > obj_size) abort();
		if (off_d_keystart+key_size+val_size <= obj_size/2) abort();
		
		bytesr by = mmap.slice(ptr+off_d_keystart, key_size);
		if (keys_seen.contains(by)) abort();
		keys_seen.add(by);
		if (hash != sm_hash(by)) abort();
	}
	
	if (hashmap_load != rd64(off_r_hm_load)) abort();
	if (hashmap_items != rd64(off_r_entries)) abort();
	
	bool last = false;
	for (int i=5;i<=63;i++)
	{
		uint64_t prev = off_r_freespace(i);
		uint64_t ptr = rd64(prev);
		if (ptr == 1) last = true;
		if (ptr == 0 && last) abort();
		if (ptr > 1)
		{
		again:
			objects_reachable.append(ptr);
			uint64_t size = uint64_t(1)<<i;
			uint64_t sibling = ptr ^ size;
			if (rd64(ptr + off_header) != size+t_free) abort();
			if (rd64(ptr + off_header) == rd64(sibling + off_header)) abort(); // should've been merged
			if (rd64(ptr + off_f_prevptr) != prev) abort();
			uint64_t next = rd64(ptr + off_f_nextobj);
			prev = ptr + off_f_nextobj;
			
			if (next != 0)
			{
				ptr = next;
				goto again;
			}
		}
	}
	
	objects_file.sort();
	objects_reachable.sort();
	if (objects_file != objects_reachable) abort();
}
