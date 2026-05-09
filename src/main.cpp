#include <macro.hpp>
#include <args.hpp>
#include <client.hpp>
#include <server.hpp>
#include <service.hpp>

std::string
usage(const int num) {
	const std::string sopts = "Server's options:\n\t--bind-ip\t\tIP address to listen "
							  "on (default: All)\n\t--port\t\t\tConnection port\n";
	const std::string copts = "Client's options:\n\t--ip\t\t\tIP address of the server\n"
							  "\t--port\t\t\tPort of the server\n\t--color-distance\t"
							  "Compression range (1-255) (default:  2)\n\t--fps\t\t\t"
							  "Frame limit               (default: 50)\n\t--lz4level"
							  "\t\tCompression level  (1-12) (default:  3)\n\t--rgb\t"
							  "\t\tBit depth, 12 or 14       (default: 12)\n\t--window-size\t\t"
							  "Window size (WxH)          (e.g., 1280x720)\n";
	const std::string iopts = "Install options:\n\t-install <name>\t\tInstall as system service\n\t\t\t\t\t  Copies binary to system path and registers service\n\t\t\t\t\t  Usage: ./deskx -install <service_name> --port=1742\n\t-uninstall\t\tUninstall system service\n\t\t\t\t\t  Stops, removes service and deletes binary\n\t\t\t\t\t  Usage: ./deskx -uninstall <service_name>\n";
	switch (num) {
	case 1:	 return "Usage: ./deskx client [options]\n" + copts;
	case 2:	 return "Usage: ./deskx server [options]\n" + sopts;
	case 3:	 return "Usage: ./deskx -install <service_name> --port=<port>\n" + iopts;
	case 4:  return "Usage: ./deskx -uninstall <service_name>\n" + iopts;
	default: return "Usage: ./deskx [mode] [options]\nModes:\n\tclient\t\t\tMode for controlling a remote computer\n\tserver\t\t\tMode for the computer to be controlled\n\t-install <name>\tInstall as system service\n\t-uninstall\t\tUninstall system service\n\n" + copts + "\n" + sopts + "\n" + iopts + "\nExample:\n\t./deskx client --ip=192.168.0.1 --port=1742 --color-distance=2\n\t./deskx -install rcs --port=1742\n";
	}
}

int
main(int argc, char *argv[]) {
	const args args(argc, argv);

	switch (args.mode()) {
	case args::type::INSTALL: {
		const char *self = argv[0];
		const char *name = args.service_name().c_str();
		const char *port = args["port"].c_str();
		if (!args["port"].size()) {
			INFO(ERR"--port is required for installation");
			INFO(usage(3));
			return 1;
		}
		return service::install(self, name, port) ? 0 : 1;
	}

	case args::type::UNINSTALL: {
		if (argc < 3) {
			INFO(ERR"Service name required");
			INFO(usage(4));
			return 1;
		}
		return service::uninstall(argv[2]) ? 0 : 1;
	}

	case args::type::CLIENT:
		if (args.ok()) {
			return client::start(args);
		}
		INFO(usage(1));
		return 2;

	case args::type::SERVER:
		if (args.ok()) {
			return server::start(args);
		}
		INFO(usage(2));
		return 2;

	default:
		INFO(usage(0));
		return 2;
	}
}
