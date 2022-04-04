#ifdef ARLIB_TEST
#define DEFLATE_TEST_IMPL
#endif
#include "deflate.h"
#include "endian.h"
#include "test.h"

static uint16_t bitreverse16(uint16_t arg)
{
#if defined(__has_builtin) && __has_builtin(__builtin_bitreverse16)
	return __builtin_bitreverse16(arg);
#else
	// this is a variant of seander's bithack, but it leaves things misaligned until the end
	// this allows deleting some shifts, making it smaller and faster (drawback is it needs a 32bit register, but I have that)
	// I'd submit it to seander, but as of may 2020, his address bounces. I think he graduated.
    uint32_t tmp = arg;
	tmp = tmp | tmp<<16;
	tmp = (tmp&(0xf0f0<<8))>>8 | (tmp&(0x0f0f<<8));
	tmp = (tmp&(0xcccc<<4))>>4 | (tmp&(0x3333<<4));
	tmp = (tmp&(0xaaaa<<2))    + (tmp&(0x5555<<2))*4; // x + 4*y = lea
	return tmp>>3;
#endif
}

#define HUFF_FAST_BITS 9
#define HUFF_SLOW_BITS 3

#define HUFF_FAST_BITS_MASK ((1<<HUFF_FAST_BITS)-1)
#define HUFF_SLOW_BITS_MASK ((1<<HUFF_SLOW_BITS)-1)

// in: length in bits of each huffman symbol, 0..15 (0 means unrepresentable)

// out:     8000 - symbol type flag
//          7800 - bits taken by symbol thus far (0..15)
// (type=0) 01FF - output value (0..285, except 256 is remapped to 384)
// (type=0) 0600 - unused
// (type=1) 07FF - branch target; low HUFF_SLOW_BITS bits are always zero

// if sum(0x10000>>in[n] where in[n]!=0) != 0x10000:
//   if sum < 0x10000, there are no symbols with size >= 2 bits, and bad_symbol != 19:
//     the remaining slots (either half of them, or all) are filled with 'bad_symbol'
//   else error

// this is quite specialized for what DEFLATE needs
static bool unpack_huffman_dfl(uint16_t * out, const uint8_t * in, size_t in_len, size_t bad_symbol)
{
	uint16_t n_len[16] = {};
	for (size_t n=0;n<in_len;n++)
		n_len[in[n]]++;
	
	size_t used_bits = 0;
	uint16_t bits_start[16];
	for (size_t n=1;n<16;n++)
	{
		bits_start[n] = used_bits;
		used_bits += (0x10000>>n) * n_len[n];
	}
	
	// if everything defined as accepted by RFC 1951 is a valid DEFLATE bytestram and everything else is forbidden,
	//  then a huffman table containing two or more zeroes and nothing else is illegal
	//  "One distance code of zero bits means that there are no distance codes used at all (the data is all literals)."
	// if everything with a reasonable and unambiguous definition in 1951 is legal, then all incomplete huffman tables are legal,
	//  if the unused bit sequences never show up
	// zlib matches neither behavior; it accepts a huffman table containing multiple zeroes and nothing else,
	//  as well as one 1-bit symbol and the rest zeroes, but anything else is rejected
	// this one matches zlib
	// https://github.com/madler/zlib/blob/cacf7f1d4e3d44d871b605da3b647f07d718623f/inftrees.c#L130-L138
	// https://tools.ietf.org/html/rfc1951#page-13
	if (UNLIKELY(used_bits != 0x10000))
	{
		if (used_bits > 0x10000) return false; // no overfull tables
		if (used_bits != (size_t)(n_len[1]<<15)) return false; // only 1-bit and unrepresentable symbols
		if (bad_symbol == 19) return false; // huffman huffman table can't be incomplete
		// (zlib implements this check differently, but our results are the same)
		assert_reached();
	}
	// used_bits can be 0, 0x8000, or 0x10000
	
	size_t out_tree = 1<<HUFF_FAST_BITS;
	memset(out, 0xFF, sizeof(uint16_t)<<HUFF_FAST_BITS);
	
	for (size_t n=0;n<in_len;n++)
	{
		int n_bits = in[n];
		if (!n_bits) continue;
		uint16_t bits = bitreverse16(bits_start[n_bits]);
		bits_start[n_bits] += 0x10000 >> n_bits;
		
		uint32_t layer_start = 0;
		int layer_bits = HUFF_FAST_BITS;
		int layer_bits_mask = (1<<layer_bits)-1;
		int bits_used = 0;
		
		while (n_bits > layer_bits)
		{
			uint16_t& link = out[layer_start | (bits&layer_bits_mask)];
			
			bits >>= layer_bits;
			n_bits -= layer_bits;
			bits_used += layer_bits;
			
			if (link == 0xFFFF)
			{
				// create a new layer in the huffman table
#ifdef ARLIB_TEST
				assert(out_tree < inflator::huff_table_size_286);
#endif
				link = 0x8000 | (bits_used << 11) | out_tree;
				layer_start = out_tree;
				memset(out+out_tree, 0xFF, sizeof(uint16_t)<<HUFF_SLOW_BITS);
				out_tree += 1<<HUFF_SLOW_BITS;
			}
			else
			{
				// follow the existing node
#ifdef ARLIB_TEST
				assert(link & 0x8000);
#endif
				layer_start = link&0x7FF;
			}
			
			layer_bits = HUFF_SLOW_BITS;
			layer_bits_mask = (1<<layer_bits)-1;
		}
		
		for (int clones=0;clones<(1<<(layer_bits-n_bits));clones++)
		{
			uint16_t& leaf = out[layer_start | (clones<<n_bits) | (bits & layer_bits_mask)];
#ifdef ARLIB_TEST
			assert(leaf == 0xFFFF);
			assert(n_bits > 0);
			assert_eq(bits_used+n_bits, in[n]);
#endif
			leaf = 0x0000 | ((bits_used+n_bits) << 11) | n;
			if (n == 256) leaf |= 128; // remap EOF, saves an instruction in the symbol decoder fast path
		}
	}
	
	if (UNLIKELY(used_bits != 0x10000))
	{
		// used_bits can be 0, 0x8000, or 0x10000, and can't be the last one in this branch
		// if 0, erase all; if 0x8000, erase only odd indices, up to out_tree
		// I can't find any solution better than the obvious one - just wipe all slots that are 0xFFFF
		for (size_t i=0;i<out_tree;i++)
		{
			if (out[i] == 0xFFFF)
				out[i] = 0x0000 | (0 << 11) | bad_symbol;
		}
	}
	
	return true;
}

void inflator::bits_refill_fast()
{
	if (m_in_nbits&32) return;
	if (LIKELY(m_in_end - m_in_at >= 4))
	{
		m_in_bits_buf |= (uint64_t)readu_le32(m_in_at) << m_in_nbits;
		m_in_nbits += 32;
		m_in_at += 4;
	}
	else bits_refill_all(); // refill to max (after this, m_in is known empty)
}

void inflator::bits_refill_all()
{
	while (m_in_at < m_in_end && m_in_nbits <= 64-8)
	{
		m_in_bits_buf |= (uint64_t)*m_in_at++ << m_in_nbits;
		m_in_nbits += 8;
	}
}

uint32_t inflator::bits_extract(uint32_t n)
{
#define BITS_FAST(n, target)                               \
	{                                                      \
		uint32_t bits_out = (n);                           \
		(target) = bits_buf & (((uint64_t)1<<bits_out)-1); \
		nbits -= bits_out;                                 \
		bits_buf >>= bits_out;                             \
	}
	
	uint32_t ret;
#define bits_buf m_in_bits_buf
#define nbits m_in_nbits
	BITS_FAST(n, ret);
#undef bits_buf
#undef nbits
	return ret;
}

uint32_t inflator::bits_read_huffman(const uint16_t * huff)
{
#define HUFF_READ_FAST(huff, target)                                                       \
	{                                                                                      \
		uint16_t st = huff[bits_buf&HUFF_FAST_BITS_MASK];                                  \
		while (st & 0x8000)                                                                \
			st = huff[(st&0x7ff) | ((bits_buf>>((st&0x7800) >> 11))&HUFF_SLOW_BITS_MASK)]; \
		                                                                                   \
		size_t bits_discard = st >> 11;                                                    \
		nbits -= bits_discard;                                                             \
		bits_buf >>= bits_discard;                                                         \
		                                                                                   \
		(target) = st&0x01ff;                                                              \
	}
	
	uint32_t target;
#define bits_buf m_in_bits_buf
#define nbits m_in_nbits
	HUFF_READ_FAST(huff, target);
#undef bits_buf
#undef nbits
	return target;
}

// TODO: optimize out resumption support, if every caller uses static bool inflate(bytesw out, bytesr in) and not the other two
// needs LTO, .a optimization, and a good optimizer (Clang can do it, GCC gets confused (last tested on 11.1))
// a.cpp:
//   static bool need_resume = false;
//   void enable_resume_private() { need_resume = true; }
//   if (need_resume) { ... }
// b.cpp:
//   oninit() { enable_resume_private(); }
//   void enable_resume() {} // intentionally left blank
// if anything calls enable_resume anywhere, b.cpp is included, its oninit runs, and need_resume becomes true
// if nothing calls it, b.cpp is excluded, and need_resume remains false
// in both cases, need_resume is assumed constant throughout the entire program (including, questionably and fragilely, in other ctors),
//   and LTO optimizes it accordingly
// need to test how this interacts with zlib headers; even if unused, they reference enable_resume
// also looks highly fragile, need to inspect the assembly
// alternatively, if every caller is the nonresuming one, perhaps LTO inlines inflate() into it and realizes that m_state is constant,
//   in which case I don't need to do anything at all
#define RET_FAIL() do { assert_reached(); return ret_error; } while(0)
inflator::ret_t inflator::inflate()
{
	enum {
		st_blockinit,
		st_litblock_head,
		st_litblock,
		st_huff1,
		st_huff2,
		st_huff3,
		st_mainloop,
		st_litbyte,
		st_copy
	};
	
	switch (m_state)
	{
		while (true)
		{
		[[fallthrough]];
		case st_blockinit:
			bits_refill_fast();
			if (UNLIKELY(m_in_nbits < 3+7)) // empty block with default huffman tables is 3+7 bits; literal can also use 3+7
			{
				assert_reached();
				m_state = st_blockinit;
				goto do_ret_more_input;
			}
			m_block_type = bits_extract(3);
			if (m_block_type < 1*2) // literal block
			{
				m_in_bits_buf >>= m_in_nbits&7;
				m_in_nbits &= ~7;
				
			[[fallthrough]];
			case st_litblock_head:
				bits_refill_fast();
				
				if (UNLIKELY(m_in_nbits < 32))
				{
					assert_reached();
					m_state = st_litblock_head;
					goto do_ret_more_input;
				}
				
				uint32_t len;
				{
					len = m_in_bits_buf; // this truncates to 32 bits
					len = len ^ ((~len)<<16);
					if (UNLIKELY(len >= 0x10000)) RET_FAIL();
					m_in_nbits -= 32;
					m_in_bits_buf >>= 32;
				}
				
				if(0) {
			case st_litblock:
					len = m_stlitblk_len;
				}
				while (m_in_nbits && len)
				{
					if (UNLIKELY(m_out_end == m_out_at))
					{
						assert_reached();
						m_state = st_litblock;
						m_stlitblk_len = len;
						return ret_more_output;
					}
					assert_reached();
					*m_out_at++ = bits_extract(8);
					len--;
				}
				
				uint32_t len_actual = min(len, m_in_end-m_in_at, m_out_end-m_out_at);
				memcpy(m_out_at, m_in_at, len_actual);
				len -= len_actual;
				m_in_at += len_actual;
				m_out_at += len_actual;
				
				if (UNLIKELY(len))
				{
					assert_reached();
					m_state = st_litblock;
					m_stlitblk_len = len;
					if (m_in_at == m_in_end) goto do_ret_more_input;
					else return ret_more_output;
				}
				
				if (m_in_nbits) assert_reached();
			}
			else if (LIKELY(m_block_type < 3*2))
			{
				if (m_block_type & 4)
				{
				[[fallthrough]];
				case st_huff1:
					bits_refill_fast();
					if (UNLIKELY(m_in_nbits < 5+5+4))
					{
						assert_reached();
						m_state = st_huff1;
						goto do_ret_more_input;
					}
					// if we run out of bits, we'll read infinite zeroes, which will either error out or emit nonsense tables
					// in both cases, it will terminate, so it's safe to defer the underflow check to later
					
					// dynamic huffman
					uint32_t huff_sizes;
					huff_sizes = bits_extract(5+5+4);
					if(0) {
				case st_huff2:
						huff_sizes = m_sthuff2_sizes;
					}
					
					uint32_t hlit;
					uint32_t hdist;
					{
						hlit = (huff_sizes&31) + 257;
						hdist = ((huff_sizes>>5)&31) + 1;
						uint32_t hclen = (huff_sizes>>10) + 4;
						
						// DEFLATE can represent 288 symbols and 32 distance codes, but only 286 resp. 30 are valid. why
						// RFC 1951 seems to explicitly allow HDIST > 30 (though using them is forbidden), but zlib rejects it
						if (UNLIKELY(hlit > 286 || hdist > 30)) RET_FAIL();
						
						bits_refill_all();
						if (UNLIKELY(m_in_nbits < hclen*3))
						{
							assert_reached();
							m_state = st_huff2;
							m_sthuff2_sizes = huff_sizes;
							goto do_ret_more_input;
						}
						
						static const uint8_t huff_al_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
						uint8_t huff_al_len[19];
						memset(huff_al_len, 0, 16); // last 3 are guaranteed overwritten by below loop, no point clearing them
						// first is guaranteed overwritten too, but 16 optimizes better than 15
						
						for (uint32_t i=0;i<hclen;i++) huff_al_len[huff_al_order[i]] = bits_extract(3);
						
						bool ok = unpack_huffman_dfl(m_huff_symbol, huff_al_len, 19, 19);
						if (UNLIKELY(!ok)) RET_FAIL();
					}
					
					uint32_t i;
					i = 0;
					if(0) {
				case st_huff3:
						huff_sizes = m_sthuff2_sizes;
						hlit = (m_sthuff2_sizes&31) + 257;
						hdist = ((m_sthuff2_sizes>>5)&31) + 1;
						i = m_sthuff3_iter;
					}
					while (i < hlit+hdist)
					{
						bits_refill_fast();
						if (UNLIKELY(m_in_nbits < 15+7 && !m_in_last))
						{
							assert_reached();
							m_state = st_huff3;
							m_sthuff2_sizes = huff_sizes;
							m_sthuff3_iter = i;
							goto do_ret_more_input;
						}
						
						int sym = bits_read_huffman(m_huff_symbol); // takes 15 bits
						if (UNLIKELY((int32_t)m_in_nbits < 0)) RET_FAIL();
						
						if (sym <= 15)
						{
							m_symbol_lengths[i++] = sym;
						}
						else if (sym == 16) // repeat last length
						{
							uint32_t n_rep = bits_extract(2) + 3;
							if (UNLIKELY(i == 0)) RET_FAIL();
							if (UNLIKELY(i+n_rep > hlit+hdist)) RET_FAIL();
							int last = m_symbol_lengths[i-1];
							for (uint32_t j=0;j<n_rep;j++)
								m_symbol_lengths[i++] = last;
						}
						else // sym 17 or 18 (invalid symbols are impossible)
						{
							uint32_t n_rep = bits_extract((sym-17)*4+3) + (sym-17)*8+3; // takes 7 bits
							if (UNLIKELY(i+n_rep > hlit+hdist)) RET_FAIL();
							for (uint32_t j=0;j<n_rep;j++)
								m_symbol_lengths[i++] = 0;
						}
					}
					
					if (UNLIKELY(!unpack_huffman_dfl(m_huff_symbol, m_symbol_lengths, hlit, 287))) RET_FAIL();
					uint8_t tmp[32];
					memcpy(tmp, m_symbol_lengths+hlit, 32); // copy it - m_symbol_lengths is a union with m_huff_distance
					if (UNLIKELY(!unpack_huffman_dfl(m_huff_distance, tmp, hdist, 31))) RET_FAIL();
				}
				else
				{
					// static huffman
					uint8_t len[288];
					for (int i=0 ; i<=32; i++) len[i] = 5;
					unpack_huffman_dfl(m_huff_distance, len, 32, 0);
					for (int i=0 ; i<=143;i++) len[i] = 8;
					for (int i=144;i<=255;i++) len[i] = 9;
					for (int i=256;i<=279;i++) len[i] = 7;
					for (int i=280;i<=287;i++) len[i] = 8;
					unpack_huffman_dfl(m_huff_symbol, len, 288, 0);
				}
				
				m_state = st_mainloop;
			[[fallthrough]];
			case st_mainloop:
			case st_copy:
			case st_litbyte:
				// moving these to locals drops runtime to ~75% of using members directly
				const uint8_t * out_prev = m_out_prev;
				uint8_t * out_start = m_out_start;
				uint8_t * out_at = m_out_at;
				uint8_t * out_end = m_out_end;
				const uint8_t * in_at = m_in_at;
				const uint8_t * in_end = m_in_end;
				uint64_t bits_buf = m_in_bits_buf;
				uint32_t nbits = m_in_nbits;
				
				ret_t ret_state = ret_done;
				
				if (m_state != st_mainloop)
				{
					if (m_state == st_copy) goto st_copy_inner;
					else if (m_state == st_litbyte) goto st_litbyte_inner;
					else __builtin_unreachable();
				}
				
				while (true)
				{
					// max bits needed per symbol: 15 (huff_symbol) + 5 (length bits) + 15 (huff_distance) + 13 (distance bits) = 48
					
					if (LIKELY(in_end-in_at >= 8))
					{
						if (nbits <= 32)
						{
							bits_buf |= (uint64_t)readu_le32(in_at) << nbits;
							nbits += 32;
							in_at += 4;
						}
					}
					else
					{
						while (nbits <= 56 && in_at != in_end)
						{
							assert_reached();
							bits_buf |= (uint64_t)*in_at++ << nbits;
							nbits += 8;
						}
						
						if (nbits < 48 && !m_in_last)
						{
							assert_reached();
							ret_state = ret_more_input;
							goto inner_return;
						}
					}
					
					// at this point, we know that either
					// - there are at least 32 bits in the bit buffer, and at least 32 bits in the byte buffer
					//    -> a 32bit refill without bounds check is safe
					// - there are at least 48 bits in the bit buffer
					//    -> there's no need to refill
					// - we're at EOF
					//    -> comp_at == in_end
					// -> either comp_at == in_end, a 32bit refill is safe, or there's no need to refill
					// -> can do a 32bit refill if comp_at != in_end
					// TODO: investigate performance of adding a 16bit refill up there and not refilling further down
					
					int symbol;
					HUFF_READ_FAST(m_huff_symbol, symbol); // takes 15 bits
					if (UNLIKELY((int32_t)nbits < 0)) RET_FAIL();
					
					if (symbol < 256)
					{
						if (0)
						{
					st_litbyte_inner:
							symbol = m_stlitby_val;
						}
						if (UNLIKELY(out_at == out_end))
						{
							assert_reached();
							ret_state = ret_more_output;
							m_state = st_litbyte;
							m_stlitby_val = symbol;
							goto inner_return;
						}
						*out_at++ = symbol;
					}
					else if (LIKELY(symbol <= 285))
					{
						size_t len;
						size_t dist;
						
						{
							static const uint16_t sym_detail[] = {
								  3+(0<<12),  4+(0<<12),  5+(0<<12),  6+(0<<12),   7+(0<<12), 8+(0<<12), 9+(0<<12), 10+(0<<12),
								 11+(1<<12), 13+(1<<12), 15+(1<<12), 17+(1<<12),  19+(2<<12),23+(2<<12),27+(2<<12), 31+(2<<12),
								 35+(3<<12), 43+(3<<12), 51+(3<<12), 59+(3<<12),  67+(4<<12),83+(4<<12),99+(4<<12),115+(4<<12),
								131+(5<<12),163+(5<<12),195+(5<<12),227+(5<<12), 258+(0<<12)
							};
							uint16_t detail = sym_detail[symbol-257];
							
							BITS_FAST(detail>>12, len); // takes 5 bits
							len += (detail&511);
							
							// do not refill after underflow, shift by huge is UB
							if (LIKELY((uint32_t)nbits >= 15+13)) assert_reached();
							else if (LIKELY(in_at != in_end))
							{
								assert_reached();
								bits_buf |= (uint64_t)readu_le32(in_at) << nbits;
								nbits += 32;
								in_at += 4;
							}
							else assert_reached();
							
							int dist_key;
							HUFF_READ_FAST(m_huff_distance, dist_key); // takes 15 bits
							
							int dist_base = ((2+(dist_key&1)) << (dist_key>>1) >> 1) + (dist_key!=0);
							int dist_bits = (dist_key>>1) - (dist_key >= 2);
							
							BITS_FAST(dist_bits, dist); // takes 13 bits
							dist += dist_base;
							
							if (dist == 32768) assert_reached();
							if (UNLIKELY(dist > 32768)) RET_FAIL(); // dist_key >= 30 ends up here
							// no need to check earlier, underflow just yields endless zeroes
							if (UNLIKELY((int32_t)nbits < 0)) RET_FAIL();
						}
						
						if(0) {
					st_copy_inner:
							len = m_stcopy_len;
							dist = m_stcopy_dist;
						}
						
						if (dist > (size_t)(out_at-out_start))
						{
							if (!out_prev) RET_FAIL();
							assert_reached();
							
							size_t dist_prev = dist-(out_at-out_start);
							
							const uint8_t * src = out_prev-dist_prev;
							size_t len_actual = min(len, dist_prev);
							rep_movsb(out_at, src, len_actual);
							len -= len_actual;
						}
						
						const uint8_t * src = out_at-dist;
						size_t len_actual = min(len, out_end-out_at);
						rep_movsb(out_at, src, len_actual);
						
						len -= len_actual;
						if (UNLIKELY(len))
						{
							assert_reached();
							m_stcopy_len = len;
							m_stcopy_dist = dist;
							m_state = st_copy;
							ret_state = ret_more_output;
							goto inner_return;
						}
					}
					else if (LIKELY(symbol == 384))
						break;
					else
						RET_FAIL();
				}
				
			inner_return:
				// most of the variables don't change, no need to write them back
				m_in_at = in_at;
				m_in_bits_buf = bits_buf;
				m_in_nbits = nbits;
				m_out_at = out_at;
				
				if (ret_state != ret_done)
					return ret_state;
			}
			else RET_FAIL();
			
			if (m_block_type&1) return ret_done;
		}
	default: __builtin_unreachable();
	}
do_ret_more_input:
	if (m_in_last) RET_FAIL();
	else return ret_more_input;
}

bytearray inflator::inflate(bytesr in)
{
	inflator inf;
	inf.set_input(in, true);
	bytearray ret;
	ret.resize(max(4096, in.size()*8));
	inf.set_output_first(ret);
again:
	inflator::ret_t err = inf.inflate();
	if (err == inflator::ret_more_output)
	{
		ret.resize(ret.size()*2);
		inf.set_output_grow(ret);
		goto again;
	}
	else if (err != inflator::ret_done || inf.unused_input() != 0) ret.reset();
	else ret.resize(inf.output_in_last());
	
	return ret;
}

bool inflator::inflate(bytesw out, bytesr in)
{
	inflator inf;
	inf.set_input(in, true);
	inf.set_output_first(out);
	return (inf.inflate() == inflator::ret_done && inf.output_in_last() == out.size() && inf.unused_input() == 0);
}


#include "test.h"

#ifdef ARLIB_TEST
test("bitreverse16", "", "")
{
	for (int n=0;n<65536;n++)
	{
		uint32_t v = n;
		v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
		v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
		v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
		v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
		
		assert_eq(bitreverse16(n), v);
	}
}

static size_t huff_max_size(size_t num_syms)
{
	uint8_t dist_to_next_layer[16];
	int bits = 0;
	int to_next = HUFF_FAST_BITS;
	while (bits < 15)
	{
		dist_to_next_layer[bits++] = to_next--;
		if (to_next<0) to_next = HUFF_SLOW_BITS;
	}
	do {
		// can't split a size 15 node, and splitting size 14 is only useful if that directly spawns a new block
		dist_to_next_layer[bits--] = 100;
	} while (dist_to_next_layer[bits] != 1);
	
	uint16_t syms_for_bits[16] = { 1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 };
	
	size_t num_unused_syms = num_syms-1;
	
	for (int len=0;len<=15;len++)
	{
		if (syms_for_bits[len] && dist_to_next_layer[len] < 100)
		{
			syms_for_bits[len]--;
			syms_for_bits[len+1] += 2;
			num_unused_syms--;
		}
	}
	
	while (num_unused_syms)
	{
		int best_split = 15;
		for (int i=0;i<15;i++)
		{
			if (!syms_for_bits[i]) continue;
			if (dist_to_next_layer[i] < dist_to_next_layer[best_split])
				best_split = i;
		}
		if (best_split == 15) break;
		
		syms_for_bits[best_split]--;
		syms_for_bits[best_split+1] += 2;
		num_unused_syms--;
	}
	
	size_t i = 0;
	uint8_t sym_lengths[512];
	for (int len=0;len<=15;len++)
	{
		for (int j=0;j<syms_for_bits[len];j++)
			sym_lengths[i++] = len;
	}
	assert_eq(i, num_syms);
	
	uint16_t huff_tbl[2048];
	memset(huff_tbl, 0xFF, sizeof(huff_tbl));
	assert(unpack_huffman_dfl(huff_tbl, sym_lengths, i, 0));
	
	for (i=0;huff_tbl[i]!=0xFFFF;i++) {}
	return i;
}

test("huffman table size", "", "deflate")
{
	assert_eq(inflator::huff_table_size_286, huff_max_size(286));
	assert_eq(inflator::huff_table_size_30, huff_max_size(30));
	assert_eq(inflator::huff_table_size_19, huff_max_size(19));
}

static void test1(bytesr comp, bytesr decomp)
{
	{
		bytearray decomp_actual;
		decomp_actual.resize(decomp.size());
		inflator inf;
		inf.set_input(comp, true);
		inf.set_output_first(decomp_actual);
		inflator::ret_t ret = inf.inflate();
		assert_eq(ret, inflator::ret_done);
		assert_eq(inf.output_in_last(), decomp.size());
		assert_eq(decomp_actual, decomp);
	}
	
	bool do_anything = false;
	{
		bytearray decomp_actual;
		decomp_actual.resize(0);
		inflator inf;
		inf.set_output_first(NULL);
		inf.set_input(NULL, false);
		
		uint8_t * tmp = xmalloc(1); // malloc(1) lets valgrind catch overflows
		size_t input_at = 0;
		while (true)
		{
			inflator::ret_t ret = inf.inflate();
			assert_ne(ret, inflator::ret_error);
			if (ret == inflator::ret_done) break;
			
			if (!do_anything)
			{
				do_anything = true;
				continue;
			}
			do_anything = false;
			
			if (ret == inflator::ret_more_input)
			{
				if (input_at >= comp.size())
				{
					assert(input_at != (size_t)-1);
					inf.set_input(NULL, true);
					input_at = -1;
				}
				else
				{
					tmp[0] = comp[input_at++];
					inf.set_input(bytesr(tmp, 1), false);
				}
			}
			else if (ret == inflator::ret_more_output)
			{
				assert_lt(decomp_actual.size(), decomp.size());
				decomp_actual.resize(decomp_actual.size()+1);
				inf.set_output_grow(decomp_actual);
			}
			else assert_unreachable();
		}
		free(tmp);
		
		assert_eq(decomp_actual, decomp);
		assert_eq(inf.output_in_last(), decomp.size());
	}
}

static void test1f(bytesr comp)
{
	bytearray decomp_actual;
	decomp_actual.resize(1024);
	inflator inf;
	inf.set_input(comp, true);
	inf.set_output_first(decomp_actual);
	assert_eq(inf.inflate(), inflator::ret_error);
}

// Input format: Sequence of
// '<' followed by bits, little endian. Can also use + to zero pad to next byte boundary, or / to specify that this is a byte boundary.
// '>' followed by hex digits, or "#3 1234" (same as 123412341234). If immediately followed by '<', will rewind to the start of the buffer.
// '!' to specify that it should signal an error
// '.' to specify that it should signal completion (NUL does that too, but an extra < after . is legal)
// The last '<' is marked as the final block. It is allowed to leave it empty.
// Space is allowed at any point in input, and between (but not inside) output digit pairs.
// Throws errors if the decompressor asks for something other than what the string expects.
// May give false positives if the decompressor is refactored to buffer more or less input before processing it.
static void test1x(const char * compdesc)
{
	size_t ninputs = 0;
	for (const char * i = compdesc; *i; i++)
	{
		if (*i == '>') i+=2; // don't count >< as <
		ninputs += (*i == '<');
	}
	
	uint8_t in[256];
	
	uint8_t out[100000];
	size_t out_pos = 0;
	
	inflator inf;
	inf.set_input(NULL, false);
	inf.set_output_first(NULL);
	
	const char * iter = compdesc;
	inflator::ret_t ret = inf.inflate();
	while (true)
	{
		switch (*iter++)
		{
		case '<':
		{
			assert_eq(ret, inflator::ret_more_input);
			int nbytes = 0;
			int nbits = 0;
			int bitbuf = 0;
			while (true)
			{
				if (*iter == ' ') {}
				else if (*iter == '0' || *iter == '1')
				{
					bitbuf |= (*iter-'0') << (nbits++);
					if (nbits == 8)
					{
						in[nbytes++] = bitbuf;
						bitbuf = 0;
						nbits = 0;
					}
				}
				else if (*iter == '/')
				{
					assert_eq(nbits, 0);
				}
				else if (*iter == '+')
				{
					if (nbits)
					{
						in[nbytes++] = bitbuf;
						bitbuf = 0;
						nbits = 0;
					}
				}
				else break;
				iter++;
			}
			assert_eq(nbits, 0);
			
			inf.set_input(bytesr(in,  nbytes), (--ninputs == 0));
			ret = inf.inflate();
			
			break;
		}
		case '>':
		{
			bool rewind = (*iter == '<');
			iter += rewind;
			
			assert_eq(ret, inflator::ret_more_output);
			int nrepeat = -1;
			bytearray out_exp;
			while (true)
			{
				if (*iter == '#')
				{
					assert_eq(nrepeat, -1);
					
					iter++;
					size_t n = 0;
					while (isdigit(iter[n])) n++;
					assert(fromstring(cstring(bytesr((uint8_t*)iter, n)), nrepeat));
					iter += n;
				}
				else if (isxdigit(*iter))
				{
					bytearray by;
					size_t n = 0;
					while (isdigit(iter[n])) n++;
					assert(fromstringhex(cstring(bytesr((uint8_t*)iter, n)), by));
					iter += n;
					
					if (nrepeat == -1) nrepeat = 1;
					for (int i=0;i<nrepeat;i++)
						out_exp += by;
					nrepeat = -1;
				}
				else if (*iter == ' ') iter++;
				else break;
			}
			assert_eq(nrepeat, -1);
			
			if (rewind)
			{
				// the public API doesn't permit mixing next/grow like this,
				// but tests are written against the implementation, not the specification
				assert_gte(out_pos, 32768);
				out_pos = 0;
				inf.set_output_next(bytesw(out, out_exp.size()));
			}
			else
			{
				inf.set_output_grow(bytesw(out, out_pos+out_exp.size()));
			}
			
			ret = inf.inflate();
			if (ret != inflator::ret_error)
			{
				assert_eq(inf.output_in_last(), out_pos+out_exp.size());
				assert_eq(bytesr(out+out_pos, out_exp.size()), out_exp);
			}
			out_pos += out_exp.size();
			break;
		}
		case '!':
			assert_eq(ret, inflator::ret_error);
			return;
		case '\0':
		case '.':
			assert_eq(ret, inflator::ret_done);
			return;
		case ' ': continue;
		default: assert_unreachable();
		}
	}
}

test("deflate decompression", "", "deflate")
{
#define test1(comp, decomp) testcall(test1(bytesr((uint8_t*)comp, sizeof(comp)-1), bytesr((uint8_t*)decomp, sizeof(decomp)-1)))
#define test1f(comp) testcall(test1f(bytesr((uint8_t*)comp, sizeof(comp)-1)))
#define test1x(comp) testcall(test1x(comp))
	test1f(""); // or just empty input
	test1f("\xff"); // invalid type 3 block
	test1f("\xff\xff"); // some longer variants, to test refilling behavior
	test1f("\xff\xff\xff");
	test1f("\xff\xff\xff\xff");
	test1f("\xff\xff\xff\xff\xff");
	test1f("\xff\xff\xff\xff\xff\xff");
	test1f("\xff\xff\xff\xff\xff\xff\xff");
	test1f("\xff\xff\xff\xff\xff\xff\xff\xff");
	test1f("\xff\xff\xff\xff\xff\xff\xff\xff\xff");
	
	// literal blocks, including various corrupt variants
	test1("\x01\x00\x00\xff\xff", "");
	test1("\x00\x00\x00\xff\xff\x01\x00\x00\xff\xff", "");
	test1("\x00\x01\x00\xfe\xff\x42\x01\x01\x00\xfe\xff\x43", "\x42\x43");
	test1("\x01\x10\x00\xef\xff\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10",
	                          "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10");
	test1f("\x01");
	test1f("\x01\x10");
	test1f("\x01\x10\x00\xef");
	test1f("\x01\x10\x00\xef\xff");
	test1f("\x01\x10\x00\xef\xff\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f");
	test1f("\x01\x10\x00\xef\xef\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10");
	test1f("\x00\x00\x00\xff\xff");
	
	// fixed huffman
	test1("\x03\x00", "");
	test1("\x4b\x4c\x44\x05\x00", "aaaaaaaaaaaaaaaa");
	// dynamic huffman
	test1("\x95\xc0\x81\x00\x00\x00\x00\x80\x20\xd6\xfc\x25\x66\x38\x9e\x00", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
	test1("\x95\xc1\x81\x0c\x00\x00\x00\x80\x30\xd6\xf2\x87\x88\xa1\x1d\x59\x02", "abababababababababababababababababababababababab");
	
	// remember that every integer must be encoded backwards
	test1x("< 110 00000/00+"); // same as \x03\x00 above, just to test this tester function
	test1x("< 110 00000 < 00+");
	
	test1x("< 010 00000000" // static huffman, empty block, to make it refill a lot
	         "000+ 10000000 00000000 01111111 11111111 10101010" // literal block of a single byte
	         "110+ 00000000 00000000 11111111 11111111" // empty literal block, padding
	       "> 55");
	
	test1x("< 001 01111 00000 0000 +!"); // overlong huffman tables
	test1x("< 001 00000 01111 0000 +!");
	
	test1x("< 001 10111 10111 1111" // oversubscribed
	         "001 001 001 001 001 001 001 001 001 001 001 001 001 001 101 101 101 101 111"
	       "+!");
	test1x("< 001 10111 10111 1111" // undersubscribed
	         "001 001 001 001 001 001 001 001 001 001 001 001 001 001 101 101 101 001 101"
	       "+!");
	test1x("< 001 10111 +!"); // too few bits
	test1x("< 001 10111 10111 1111" // too few bits again
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	       "+!");
	test1x("< 001 10111 10111 1111" // first symbol is repeat last symbol
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "11101 00" // 16 00
	       "+!");
	test1x("< 001 10111 10111 1111" // overflowing repeat
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 0011100" // sym 18 - 39 zeroes
	         "11101 00" // sym 16 - repeat the last symbol 3 times
	       "+!");
	test1x("< 001 10111 10111 1111" // overflowing zero fill
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 0111100" // sym 18 - 41 zeroes
	       "+!");
	test1x("< 001 10111 10111 1111" // empty symbol huffman table
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1011100" // sym 18 - 40 zeroes
	       "+!");
	test1x("< 001 10111 10111 1111" // oversubscribed symbol huffman table
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "0001"
	         "0001"
	         "0001"
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 0101100" // sym 18 - 37 zeroes
	       "+!");
	test1x("< 001 10111 10111 1111" // oversubscribed distance huffman table
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "0001" // sym 1 - length 1
	         "11111 0111111" // sym 18 - 137 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 0101100" // sym 18 - 37 zeroes
	         "0001"
	         "0001"
	         "0001"
	       "+!");
	test1x("< 101 10111 10111 1111" // empty distance huffman table
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 1101011" // sym 18 - 118 zeroes
	         "0001" // sym 1 - length 1
	         "11111 0000110" // sym 18 - 59 zeroes
	         "0"
	       "+");
	test1x("< 001 10111 10111 1111" // invalid huffman symbol, dynamic table
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "0001" // sym 1 - length 1
	         "11111 0111111" // sym 18 - 137 zeroes
	         "11111 1111111" // sym 18 - 138 zeroes
	         "11111 0011100" // sym 18 - 39 zeroes
	         "0001" // sym 286 - length 1
	         "1"
	       "+!");
	test1x("< 010" // invalid huffman symbol, fixed table
	         "11000110" // sym 286
	       "+!");
	test1x("< 010" // too short input
	         "00110000" // byte 00
	         "+" // not enough bits for next symbol
	       "> 00"
	       "!");
	test1x("< 110" // repeat that doesn't refill
	         "00110000" // byte 0
	         "0000001" // sym 257, repeat 3 bytes
	           "00000" // from distance 1 byte back
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00000000" // sym 256
	         "+"
	       "> 00 #3 00 #6 00");
	test1x("< 110" // repeat that refills
	         "110010000" // byte 144
	         "110010000" // byte 144
	         "110010000" // byte 144
	         "0000001" // sym 257, repeat 3 bytes
	           "00000" // from distance 1 byte back
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00110000" // byte 0
	         "00000000" // sym 256
	         "+"
	       "> #3 90 #3 90 #10 00");
	test1x("< 110" // repeat with invalid distance key
	         "0000001" // sym 257, repeat 3 bytes
	           "11111" // distance key 31, but no distance
	       "+!");
	test1x("< 110" // repeat that runs out of bits
	         "0000001" // sym 257, repeat 3 bytes
	           "10111" // distance key 29
	       "+!");
	test1x("< 010" // repeat of nonexistent byte
	         "0000001 00000" // sym 257, repeat 3 bytes from 1 byte back
	         "+" // not enough bits for next symbol
	       "!");
	test1x("< 101 10111 10111 1111" // repeat across the block boundary
	         "101 101 101 001 001 001 001 001 001 001 001 001 001 001 101 001 101 001 101"
	         // key: 0000..1100 -> 0..12
	         //      11010..11111 -> 13..18
	         "0011" // sym 0 - length 3
	         "0011" // sym 1 - length 3
	         "0011" // sym 2 - length 3 (unused)
	         "11111 0100111" // sym 3-127 - 125 zeroes
	         "11111 1010111" // sym 128-255 - 128 zeroes
	         "0011" // sym 256 - length 3
	         "11111 1000100" // sym 257-284 - 28 zeroes
	         "0001" // sym 285 - length 1
	         "0001" // dist 0 - length 1
	         "0010" // dist 1 - length 2
	         "11111 0000100" // dist 2-28 - 27 zeroes
	         "0010" // dist 29 - length 2
	         
	         
	         "100" // byte 00
	         "00" // sym 285 - repeat 258 bytes from 1 byte back
	         "000000000000000000000000000000000000000000000000000000000000000000000000000000000000" // 10836 bytes
	         "000000000000000000000000000000000000000000000000000000000000000000000000000000000000" // 10836 bytes
	         "000000000000000000000000000000000000000000000000000000000000000000000000000000000000" // 10836 bytes
	         "101" // byte 01
	         // total 32768 bytes written
	         "100" // byte 00
	         
	         "010" // sym 285 - repeat 258 bytes from 2 bytes back
	         "011 1111111111111" // sym 285 - repeat 258 bytes from 32768 bytes back
	         "111+"
	       "> #32767 00 01"
	       ">< 00 #129 0100 #258 00"
	       );
	
	assert_all_reached();
}
#endif
