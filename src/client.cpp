
#include <thread>
#include <cmath>
#include <macro.hpp>
#include <net.hpp>
#include <codec.hpp>
#include <client.hpp>

namespace client {
namespace {

bool alive, refresh = false;
byte *window;
size_t client_w = 0, client_h = 0;  // Client window dimensions

// Frame types for adaptive scaling protocol
enum frame_type : byte {
	FRAME_COMPRESSED = 0,  // Original LZ4 compressed frame
	FRAME_RAW_RGB    = 1   // Raw RGB frame (scaled)
};

void
screen(void) {
	byte *buff = new byte[codec::max()];
	byte *raw_buff = new byte[client_w * client_h * 4];
	net::status status;
	uint64_t size;
	DIE(!buff);

	while (alive) {
		// Read frame type (1 byte)
		byte ftype;
		status = net::recv(&ftype, 1);
		BREAK_IF(status == net::status::FAIL);
		NEXT_DELAY(status == net::status::EMPTY);

		// Read frame size (8 bytes)
		status = net::recv(reinterpret_cast<byte *>(&size), 8);
		BREAK_IF(status == net::status::FAIL);

		size = ntohll(size);
		NEXT_DELAY(status == net::status::EMPTY || size < 2);

		// Handle different frame types
		if (ftype == FRAME_RAW_RGB) {
			// Raw RGB frame (from scaled transmission)
			status = net::recv(raw_buff, size);
			if (status != net::status::OK) {
				INFO(ERR"Bad raw frame");
				alive = false;
				break;
			}
			// Direct copy to window buffer (no decoding needed)
			::memcpy(window, raw_buff, std::min(static_cast<size_t>(size), 
												 client_w * client_h * 4));
		} else {
			// Compressed frame (original codec)
			status = net::recv(buff, size);
			if (status != net::status::OK) {
				INFO(ERR"Bad scr package");
				alive = false;
				break;
			}
			codec::set(window, buff, size);
		}
		refresh = true;
#ifdef TEST
	::exit(0);
#endif	
	}
#ifdef TEST
	::exit(1);
#endif
	delete[] buff;
	delete[] raw_buff;
}

}

int
start(const args &args) {
	const int port = args.num("port");
	if (port < 1) {
		INFO(ERR"Incorrect port number");
		return 3;
	}

	const std::string ip = args["ip"];
	if (ip.empty()) {
		INFO(ERR"Incorrect ip address");
		return 4;
	}

	INFO(NOTE"Trying to connect to " + ip); 
	if (!net::start(ip, port, args.mode())) {
		INFO(ERR"Can't connect to the server");
		net::close();
		return 5;
	}

	const int num = args.num("color-distance");
	const int lz4 = args.num("lz4level");
	const int fps = args.num("fps");
	const int rgb = args.num("rgb");
	net::hello msg = {
		static_cast<byte>(num == -1 ?  2 : std::min(254, num)),
		static_cast<byte>(fps  <  1 ? 50 : std::min(255, fps)),
		static_cast<byte>(lz4 == -1 ?  3 : std::min( 12, lz4)),
		static_cast<byte>(rgb == -1 ? 12 : rgb == 14 ? 14 : 12),
	};
	if (!net::send(msg)) {
		INFO(ERR"Can't send 'hello' message");
		net::close();
		return 6;
	}

	if (!gui::init()) {
		INFO(ERR"Can't init GUI module");
		net::close();
		return 9;
	}

	// Parse window-size parameter: --window-size=1280x720
	size_t client_w = 0, client_h = 0;
	const std::string &winsize = args["window-size"];
	if (!winsize.empty()) {
		size_t sep = winsize.find('x');
		if (sep != std::string::npos) {
			client_w = std::stoul(winsize.substr(0, sep));
			client_h = std::stoul(winsize.substr(sep + 1));
			INFO(NOTE"Using specified window size: " 
				 + std::to_string(client_w) + "x" + std::to_string(client_h));
		} else {
			INFO(WARN"Invalid window-size format, use WIDTHxHEIGHT (e.g., 1280x720)");
		}
	}

	// Default to display resolution if not specified
	if (client_w == 0 || client_h == 0) {
		client_w = gui::width();
		client_h = gui::height();
	}

	// Send client viewport to server for adaptive scaling
	net::client_view view;
	view.width  = static_cast<uint16_t>(std::min(client_w, static_cast<size_t>(SCR_X_MAX)));
	view.height = static_cast<uint16_t>(std::min(client_h, static_cast<size_t>(SCR_Y_MAX)));
	
	if (!net::send(view)) {
		INFO(ERR"Can't send viewport");
		net::close();
		return 6;
	}

	// Server responds with actual transmitted resolution
	net::screen srv;
	if (!net::recv(srv)) {
		INFO(ERR"Can't receive screen config");
		net::close();
		return 7;
	}
	if (srv.width < SCR_X_MIN || srv.height < SCR_Y_MIN) {
		INFO(ERR"Screen resolution is too small");
		net::close();
		return 8;
	}

	INFO(NOTE"Creating window with resolution " + std::to_string(srv.width) + "x" 
		 + std::to_string(srv.height));
	window = gui::window(srv.width, srv.height);
	if (!window) {
		INFO(ERR"Can't create new window");
		net::close();
		return 10;
	}

	codec::init(srv.width, srv.height, msg.delta, msg.rgb);
	
	// Store window dimensions for frame decoding
	client_w = srv.width;
	client_h = srv.height;
	
	alive = true;
	std::thread thr(screen);
	if (!thr.joinable()) {
		INFO(ERR"Can't start screen thread");
		gui::close();
		net::close();
		return 11;
	}

	byte *buff = new byte[display::emsg];
	std::pair<bool, bool> flags = { false, false};
	display::events elist;
	net::status status;
	DIE(!buff);
	
	INFO(NOTE"Ready to use");
	while (alive) {
		if (refresh && !gui::refresh()) {
			INFO(ERR"Can't refresh surface");
			alive = false;
			break;
		}

		refresh = false;
		flags = gui::events(elist);
		BREAK_IF(flags.second);
		NEXT_IF(!flags.first);

		elist.pack(buff);
		status = net::send(buff, display::emsg);
		BREAK_IF(status != net::status::OK);
	}

	INFO(NOTE"Session is dropped, waiting for"
			 " work to complete");
	alive = false;
	thr.join();
	delete[] buff;
	gui::close();
	net::close();
	INFO(NOTE"Quit");
	return 0;
}

}
