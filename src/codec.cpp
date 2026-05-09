
#include <string.h>
#include <codec.hpp>
#include <codec/lz4.hpp>
#include <codec/rgb.hpp>
#include <codec/axis.hpp>

#define LINE byte{1}
#define AXIS byte{0}

namespace codec {
namespace {

size_t width, height, pixnum, xmax, framemax;
const palette::cfg *pcfg = nullptr;
byte delta, skipx, skipy;
byte *prev = nullptr;
byte *next = nullptr;
byte *lz4m = nullptr;
bool start;
int lz4l;

// Adaptive scaling state
size_t target_w = 0, target_h = 0;
bool need_scale = false;

byte
is(const byte s) {
	return (s & 0x80) == 0x80 ? LINE : AXIS;
}

}

void
init(const size_t &x, const size_t &y, const byte num,
	 const byte bits, const byte lz4level) {
	DIE(x > SCR_X_MAX || y > SCR_Y_MAX);
	pixnum = x * y;
	framemax = pixnum * 3;
	DIE(!framemax);

	lz4l   = static_cast<int>(lz4level);
	start  = true;
	delta  = num;
	width  = x;
	height = y;
	xmax   = x;
	lz4m   = new byte[framemax];
	pcfg   = bits == 14 ? &palette::rgb14 : &palette::rgb12;
	DIE(!lz4m);
}

void
skip(const byte x, const byte y) {
	xmax = width / (x + 1);
	skipx = x;
	skipy = y;
}

void
allocate(void) {
	prev = new byte[framemax];
	next = new byte[framemax];
	DIE(!prev || !next);
}

void
free(void) {
	RET_IF(!prev);
	delete[] prev;
	delete[] next;
	prev = nullptr;
	next = nullptr;
	pcfg = nullptr;
}

size_t
max(void) {
	return pixnum * 4;
}

bool
get(display::pixs &pixs, byte *msg, uint64_t &size) {
	RET_IF(!pixs.ptr, false);

	size_t shift, skip = 0, x = 0;
	rgb color0(pcfg), color1(pcfg);
	axis xy;
	size = 0;

	color0.set(pixs.ptr);
	byte *pbuff = prev;
	byte *nbuff = next;
	byte *buff  = lz4m;

	auto step = [&pixs, &pbuff, &nbuff, &x](size_t &num) {
		for (byte i = 0; i <= skipx; i++) {
			pixs.next();
			num++;
			x++;
		}

		pbuff += 3;
		nbuff += 3;
		RET_IF(x < width);

		x = 0;
		for (byte i = 0; i < skipy; i++) {
			pixs.next(width);
			num += width;
		}
	};

	step(shift);
	for (size_t i = 1 + skipx; i < pixnum; step(i)) {
		::memcpy(nbuff, pixs.ptr, 3);
		if (::memcmp(pbuff, pixs.ptr, 3) == 0 && !start) {
			skip++;
			continue;
		}
		if (skip) {
			shift = color0.encode(buff);
			size += shift;
			buff += shift;
			shift = 0;

			if (skip > 0x1FFF) {
				shift = skip  / xmax;
				skip -= shift * xmax;
			}
			if (shift) {
				xy.set(shift, axis::type::Y);
				shift = xy.encode(buff);
				size += shift;
				buff += shift;
			}
			if (skip) {
				xy.set(skip, axis::type::X);
				shift = xy.encode(buff);
				size += shift;
				buff += shift;
				skip = 0;
			}

			color0.set(pixs.ptr);
			continue;
		}

		color1.set(pixs.ptr);
		if (color0.full()) {
			shift = color0.encode(buff);
			size += shift;
			buff += shift;
			color0 = color1;
			continue;
		}

		if (color0.eq(color1, delta)) {
			++color0;
			continue;
		}
		else {
			shift = color0.encode(buff);
			size += shift;
			buff += shift;
			color0 = color1;
		}
	}
	if (color0.size() > 1) {
		shift = color0.encode(buff);
		size += shift;
	}

	start = false;
	std::swap(prev, next);
	RET_IF(!size, false);

	size = lz4::compress(msg + 8, lz4m, size, lz4l);
	const uint64_t num = htonll(size);
	::memcpy(msg, &num, 8);
	size += 8;
	return true;
}

void
set(byte *win, byte *msg, uint64_t &size) {
	size = lz4::decompress(lz4m, msg, size, framemax);
	DIE(!size);

	byte *buff = lz4m;
	size_t tmp;
	for (uint64_t num = 0; num < size;) {
		if (is(*buff) == AXIS) {
			tmp = axis::decode(width, 4, &buff, &win);
			num += tmp;
			NEXT_IF(tmp);
			INFO(WARN"Shift is broken, skip frame");
			return;
		}

		tmp = rgb::decode(width, 4, *pcfg, &buff, &win);
		num += tmp;
		if (!tmp) {
			INFO(WARN"Line is broken, skip frame");
			return;
		}
	}
}

// Update viewport for adaptive scaling
void
update_viewport(const size_t &tw, const size_t &th) {
	if (tw == target_w && th == target_h) return;
	target_w = tw;
	target_h = th;
	need_scale = (tw > 0 && th > 0 && (tw != width || th != height));
	INFO(NOTE"Viewport updated to " + std::to_string(tw) + "x" + std::to_string(th)
		 + (need_scale ? " (scaling enabled)" : " (native)"));
}

// Get current viewport dimensions
void
get_viewport(size_t &tw, size_t &th) {
	tw = target_w;
	th = target_h;
}

// Scale source pixels to target dimensions using bilinear interpolation
void
scale(display::pixs &pixs, byte *msg, uint64_t &size,
	  const size_t &tw, const size_t &th) {
	RET_IF(!pixs.ptr || tw == 0 || th == 0);
	
	const size_t sw = width, sh = height;
	const float scale_x = static_cast<float>(sw) / tw;
	const float scale_y = static_cast<float>(sh) / th;
	
	// Write scaled image directly to output buffer
	byte *out = msg + 8;
	size_t out_pos = 0;
	
	for (size_t ty = 0; ty < th; ty++) {
		// Source y coordinate (center of target row)
		const size_t sy_base = static_cast<size_t>(ty * scale_y);
		
		for (size_t tx = 0; tx < tw; tx++) {
			// Source x coordinate (center of target pixel)
			const size_t sx_base = static_cast<size_t>(tx * scale_x);
			
			// Bilinear interpolation weights
			const float fx = tx * scale_x - sx_base;
			const float fy = ty * scale_y - sy_base;
			
			// Four source pixels
			const size_t sx0 = sx_base;
			const size_t sy0 = std::min(sy_base, sh - 1);
			const size_t sx1 = std::min(sx_base + 1, sw - 1);
			const size_t sy1 = std::min(sy_base + 1, sh - 1);
			
			byte *p00 = pixs.ptr + (sy0 * sw + sx0) * pixs.shift;
			byte *p10 = pixs.ptr + (sy0 * sw + sx1) * pixs.shift;
			byte *p01 = pixs.ptr + (sy1 * sw + sx0) * pixs.shift;
			byte *p11 = pixs.ptr + (sy1 * sw + sx1) * pixs.shift;
			
			// Bilinear interpolation for each channel (R, G, B)
			for (int c = 0; c < 3; c++) {
				float v00 = p00[c];
				float v10 = p10[c];
				float v01 = p01[c];
				float v11 = p11[c];
				
				float interpolated = v00 * (1 - fx) * (1 - fy)
									+ v10 * fx * (1 - fy)
									+ v01 * (1 - fx) * fy
									+ v11 * fx * fy;
				out[out_pos++] = static_cast<byte>(interpolated + 0.5f);
			}
			// Alpha channel (keep full opacity)
			out[out_pos++] = 255;
		}
	}
	
	// Use raw encoding for scaled frames
	size = out_pos;
	const uint64_t num = htonll(size);
	::memcpy(msg, &num, 8);
	size += 8;
}

}