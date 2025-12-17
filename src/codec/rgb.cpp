
#include <cmath>
#include <codec/rgb.hpp>

namespace codec {
namespace {

namespace enc {

uint16_t
big(const uint16_t &color, const uint16_t &size, byte *ptr, const palette::cfg &tpl) {
	uint32_t &val = *reinterpret_cast<uint32_t *>(ptr);
	val = 0xE0000000 | ((size & tpl.encbig.tpl) << tpl.encbig.shift) | color;
	val = htonl(val);
	return 4;
}

uint16_t
mid(const uint16_t &color, const uint16_t &size, byte *ptr, const palette::cfg &tpl) {
	uint32_t &val = *reinterpret_cast<uint32_t *>(ptr);
	val = 0xC0000000 | ((size & tpl.encmid.tpl) << tpl.encmid.shift) | (color << 8);
	val = htonl(val);
	return 3;
}

uint16_t
small(const byte color, const uint16_t &size, byte *ptr) {
	uint16_t &val = *reinterpret_cast<uint16_t *>(ptr);
	val = 0xA000 | ((size & 0x1F) << 8) | color;
	val = htons(val);
	return 2;
}

uint16_t
white(const uint16_t &size, byte *ptr) {
	*ptr = 0x90 | (size & 0x0F);
	return 1;
}

uint16_t
black(const uint16_t &size, byte *ptr) {
	*ptr = 0x80 | (size & 0x0F);
	return 1;
}

}

namespace dec {

uint16_t
big(const byte *ptr, const palette::cfg &tpl, uint32_t &color, uint16_t &size) {
	const uint32_t val = ntohl(*reinterpret_cast<const uint32_t *>(ptr));
	size = (val & tpl.decbig.tpl) >> tpl.decbig.shift;
	byte &r = *reinterpret_cast<byte *>(&color),
		 &g = *(&r + 1),
		 &b = *(&r + 2);
	r = ((val & tpl.decbig.r.tpl) >> tpl.decbig.r.shift) * tpl.denoms.r;
	g = ((val & tpl.decbig.g.tpl) >> tpl.decbig.g.shift) * tpl.denoms.g;
	b = ((val & tpl.decbig.b.tpl) >> tpl.decbig.b.shift) * tpl.denoms.b;
	return 4;
}

uint16_t
mid(const byte *ptr, const palette::cfg &tpl, uint32_t &color, uint16_t &size) {
	const uint32_t val = ntohl(*reinterpret_cast<const uint32_t *>(ptr));
	size = (val & tpl.decmid.tpl) >> tpl.decmid.shift;
	byte &r = *reinterpret_cast<byte *>(&color),
		 &g = *(&r + 1),
		 &b = *(&r + 2);
	r = ((val & tpl.decmid.r.tpl) >> tpl.decmid.r.shift) * tpl.denoms.r;
	g = ((val & tpl.decmid.g.tpl) >> tpl.decmid.g.shift) * tpl.denoms.g;
	b = ((val & tpl.decmid.b.tpl) >> tpl.decmid.b.shift) * tpl.denoms.b;
	return 3;
}

uint16_t
small(const byte *ptr, const palette::cfg &tpl, uint32_t &color, uint16_t &size) {
	const uint16_t val = ntohs(*reinterpret_cast<const uint16_t *>(ptr));
	auto it = tpl.id2c.find(val & 0xFF);
	color = it == tpl.id2c.end() ? 0 : it->second;
	size  = (val & 0x1F00) >> 8;
	return 2;
}

uint16_t
single(const byte *ptr, uint32_t &color, uint16_t &size) {
	color = (*ptr & 0x10) > 0 ? 0x00EBEBEB : 0x00101010;
	size  =  *ptr & 0x0F;
	return 1;
}

}

}

rgb::rgb(const palette::cfg *arg) {
	tpl = arg;
	DIE(!arg);
}

void
rgb::set(const byte *ptr) {
	r = static_cast<byte>((ptr[0] / 255.) * 220 + 16);
	g = static_cast<byte>((ptr[1] / 255.) * 220 + 16);
	b = static_cast<byte>((ptr[2] / 255.) * 220 + 16);
	size_ = 1;
}

void
rgb::operator++(void) {
	RET_IF(full());
	size_++;
}

void
rgb::operator--(void) {
	RET_IF(size_ == 0x01);
	size_--;
}

void
rgb::operator=(const rgb &arg) {
	r = arg.r;
	g = arg.g;
	b = arg.b;
	tpl = arg.tpl;
	size_ = arg.size_;
}

bool
rgb::operator==(const rgb &arg) const {
	return r == arg.r && g == arg.g &&
		   b == arg.b && size_ == arg.size_;
}

bool
rgb::operator!=(const rgb &arg) const {
	return !operator==(arg);
}

bool
rgb::full(void) const {
	return size_ >= tpl->encbig.tpl;
}

const uint32_t &
rgb::size(void) const {
	return size_;
}

uint32_t
rgb::rgb32(void) const {
	uint32_t ret = 0;
	ret = (r << 16) | (g << 8) | b;
	return ret;
}

uint16_t
rgb::rgb16(void) const {
	uint16_t ret = 0;
	byte rp = r / tpl->denoms.r;
	byte gp = g / tpl->denoms.g;
	byte bp = b / tpl->denoms.b;
	ret = (rp << tpl->shifts.r) | (gp << tpl->shifts.g) | bp;
	return ret;
}

bool
rgb::eq(const rgb &arg, const byte delta) const {
	return std::abs(arg.r - r) + std::abs(arg.g - g) +
		   std::abs(arg.b - b) <= delta || rgb16() == arg.rgb16();
}

size_t
rgb::encode(byte *buff) const {
	const uint32_t num = size_ - 1;
	const uint16_t color = rgb16();
	
	if (num <= 15) {
		RET_IF(color == tpl->colors.white, enc::white(num, buff));
		RET_IF(color == tpl->colors.black, enc::black(num, buff));
	}
	RET_IF(num > tpl->encmid.tpl, enc::big(color, num, buff, *tpl));
	RET_IF(num > 		    0x1F, enc::mid(color, num, buff, *tpl));
	auto it = tpl->c2id.find(color);
	return it == tpl->c2id.end() ? enc::mid(color, num, buff, *tpl)
								 : enc::small(it->second, num, buff);
}

size_t
rgb::decode(const size_t &width, const size_t &shift,
			const palette::cfg &tpl, byte **ptr, byte **scr) {
	uint16_t repeat, num;
	uint32_t color;

	const byte b = *(*ptr);
	switch (b & 0xE0) {
	case 0xE0: num = dec::big(*ptr, tpl, color, repeat);
			   break;
	case 0xC0: num = dec::mid(*ptr, tpl, color, repeat);
			   break;
	case 0xA0: num = dec::small(*ptr, tpl, color, repeat);
			   break;
	case 0x80: num = dec::single(*ptr, color, repeat);
			   break;
	default:   INFO(WARN"Incorrect RGB block, shift 0");
			   return 0;
	}

	const byte *bytes = reinterpret_cast<byte *>(&color);
	(*ptr) += num;
	for (size_t i = 0; i <= repeat; i++, (*scr) += shift) {
		(*scr)[0] = bytes[0];
		(*scr)[1] = bytes[1];
		(*scr)[2] = bytes[2];
	}

	return num;
}

}