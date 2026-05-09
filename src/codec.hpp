
#ifndef DESKX_CODEC_HPP
#define DESKX_CODEC_HPP

#include <cstddef>
#include <display.hpp>

namespace codec {

void
init(const size_t &, const size_t &, const byte, const byte, const byte l = 1);

void
skip(const byte, const byte);

void
allocate(void);

void
free(void);

size_t
max(void);

bool
get(display::pixs &, byte *, uint64_t &);

void
set(byte *win, byte *buff, uint64_t &);

// Adaptive scaling: rescale server buffer to target viewport
void
scale(display::pixs &, byte *, uint64_t &, const size_t &target_w, const size_t &target_h);

void
update_viewport(const size_t &target_w, const size_t &target_h);

// Get current viewport dimensions for adaptive scaling
void
get_viewport(size_t &tw, size_t &th);

}

#endif