
#include <thread>
#include <string.h>
#include <stdlib.h>
#include <macro.hpp>
#include <net.hpp>
#include <display.hpp>
#include <codec.hpp>
#include <server.hpp>

// Frame types for adaptive scaling protocol
enum frame_type : byte {
	FRAME_COMPRESSED = 0,  // Original LZ4 compressed frame
	FRAME_RAW_RGB    = 1   // Raw RGB frame (scaled)
};

namespace server {
namespace {

display::tpl *disp = nullptr;
bool alive;

byte
session_type(void) {
#if OS != LINUX
	return 0;
#else
	std::string env(::getenv("XDG_SESSION_TYPE"));
	for (auto &c : env) {
		c = std::tolower(c);
	}
	return env == "wayland" ? WAYLAND : (env == "x11" ? X11 : TTY);
#endif
}

void
events(void) {
	byte *buff = new byte[display::emsg];
	display::events elist;
	net::status ret;
	DIE(!buff);
	
	while (alive) {
		ret = net::recv(buff, display::emsg);
		BREAK_IF(ret == net::status::FAIL);
		NEXT_DELAY(ret == net::status::EMPTY);
		elist.set(buff);
		disp->set(elist);
	}

	alive = false;
	delete[] buff;
}

}

int
start(const args &args) {
	const int port = args.num("port");
	if (port < 1) {
		INFO(ERR"Incorrect port number");
		return 3;
	}

	if (!net::start(args["bind-ip"], port, args.mode())) {
		INFO(ERR"Can't start TCP server");
		net::close();
		return 4;
	}

	disp = display::get(session_type());
	if (!disp) {
		INFO(ERR"Unsupported screen session type");
		net::close();
		return 5;
	}
	if (!disp->init()) {
		INFO(ERR"Can't init screen session");
		disp->close();
		delete disp;
		net::close();
		return 6;
	}

	for (net::hello usr;;) {
		net::kick();
		NEXT_DELAY(!net::connection() || !net::recv(usr));

		usr.delta = std::min(byte{0xFE}, usr.delta);
		usr.fps   = std::max(byte{0x01}, usr.fps);
		usr.lz4   = std::max(byte{0x01}, usr.lz4);
		usr.lz4   = std::min(byte{0x12}, usr.lz4);
		net::screen res;
		std::tie(res.width, res.height) = disp->res();
		NEXT_IF(!net::send(res));

		// Receive client viewport for adaptive scaling
		net::client_view view;
		NEXT_IF(!net::recv(view));
		
		INFO(NOTE"Client viewport: " + std::to_string(view.width) + "x" 
			 + std::to_string(view.height));
		
		alive = true;
		std::thread keys(events);
		DIE(!keys.joinable());

		codec::init(res.width, res.height, usr.delta, usr.rgb, usr.lz4);
		codec::allocate();
		codec::update_viewport(view.width, view.height);
		
		// Determine actual transmission resolution
		size_t tx_w, tx_h;
		codec::get_viewport(tx_w, tx_h);
		net::screen tx_res;
		tx_res.width  = tx_w > 0 ? static_cast<uint16_t>(tx_w) : res.width;
		tx_res.height = tx_h > 0 ? static_cast<uint16_t>(tx_h) : res.height;
		NEXT_IF(!net::send(tx_res));
		
		// Allocate buffer for scaled output if needed
		byte *buff = new byte[codec::max() + 8];
		byte *scale_buff = nullptr;
		if (tx_w > 0 && tx_h > 0) {
			scale_buff = new byte[tx_res.width * tx_res.height * 4 + 8];
		}
		DIE(!buff);

		std::chrono::milliseconds delay(1000 / usr.fps);
		auto now = NOW_MSEC, prev = now;
		display::pixs pixs;
		display::pixs scaled_pixs;
		net::status status;
		uint64_t size;

		while (alive) {
			now = NOW_MSEC;
			if (now - prev < delay) {
				prev = delay - (now - prev);
				std::this_thread::sleep_for(prev);
			}

			prev = std::move(now);
			disp->refresh(pixs);
			
			// Use adaptive scaling if viewport differs from native resolution
			if (tx_w > 0 && tx_h > 0 && scale_buff) {
				scaled_pixs = pixs;
				scaled_pixs.shift = 4;
				codec::scale(scaled_pixs, scale_buff, size, tx_res.width, tx_res.height);
				// Send frame type identifier for raw RGB
				byte ftype = FRAME_RAW_RGB;
				net::send(&ftype, 1);
				status = net::send(scale_buff, size);
			} else {
				NEXT_IF(!codec::get(pixs, buff, size));
				// Send frame type identifier for compressed frame
				byte ftype = FRAME_COMPRESSED;
				net::send(&ftype, 1);
				status = net::send(buff, size);
			}
			BREAK_IF(status != net::status::OK);
#ifdef TEST
	::exit(0);
#endif
		}
#ifdef TEST
	::exit(1);
#endif
		alive = false;
		keys.join();
		codec::free();
		delete[] buff;
		delete[] scale_buff;
	}

	net::close();
	disp->close();
	delete disp;
	return 7;
}

}