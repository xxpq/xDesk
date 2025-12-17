
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

}