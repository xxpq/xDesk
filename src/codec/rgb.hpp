
#ifndef DESKX_CODEC_RGB_HPP
#define DESKX_CODEC_RGB_HPP

#include <macro.hpp>
#include <codec/palette.hpp>

namespace codec {

class rgb {
private:
	uint16_t
	rgb16(void) const;

	uint32_t
	rgb32(void) const;

protected:
	const palette::cfg *tpl = nullptr;
	byte r, g, b;
	uint32_t size_ = 1;

public:
	rgb(const palette::cfg *);

	void
	set(const byte *);

	void
	operator++(void);

	void
	operator--(void);

	void
	operator=(const rgb &);

	bool
	operator==(const rgb &) const;

	bool
	operator!=(const rgb &) const;

	bool
	full(void) const;

	const uint32_t &
	size(void) const;

	bool
	eq(const rgb &, const byte) const;

	size_t
	encode(byte *) const;

	static size_t
	decode(const size_t &, const size_t &, const palette::cfg &, byte **, byte **);

};

}

#endif