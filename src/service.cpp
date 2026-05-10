#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <macro.hpp>

#if OS == LINUX
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>

namespace service {
namespace {

const char *service_template = R"([Unit]
Description=DeskX Remote Control Service
After=network.target

[Service]
Type=simple
ExecStart=%s server --port=%s
Restart=on-failure
User=root

[Install]
WantedBy=multi-user.target
)";

bool
copy_to_bin(const char *src, const char *service_name) {
	char dest[PATH_MAX];
	char link[PATH_MAX];
	struct stat st;

	if (::stat(src, &st) != 0) {
		INFO(std::string(ERR) + "Source executable not found: " + src);
		return false;
	}

	::snprintf(dest, sizeof(dest), "/usr/bin/rcs");
	::snprintf(link, sizeof(link), "/etc/systemd/system/%s.service", service_name);

	FILE *in = ::fopen(src, "rb");
	if (!in) {
		INFO(ERR"Cannot open source file");
		return false;
	}

	FILE *out = ::fopen(dest, "wb");
	if (!out) {
		INFO(std::string(ERR) + "Cannot create " + dest + " (need root permission)");
		::fclose(in);
		return false;
	}

	char buf[8192];
	size_t n;
	while ((n = ::fread(buf, 1, sizeof(buf), in)) > 0) {
		if (::fwrite(buf, 1, n, out) != n) {
			INFO(std::string(ERR) + "Failed to write to " + dest);
			::fclose(in);
			::fclose(out);
			return false;
		}
	}

	::fclose(in);
	::fclose(out);

	::chmod(dest, 0755);
	INFO(std::string(NOTE) + "Copied to " + dest);

	return true;
}

bool
create_systemd_service(const char *service_name, const char *port) {
	char path[PATH_MAX];
	char content[4096];
	::snprintf(path, sizeof(path), "/etc/systemd/system/%s.service", service_name);

	::snprintf(content, sizeof(content), service_template, service_name, port);

	FILE *f = ::fopen(path, "w");
	if (!f) {
		INFO(ERR"Cannot create service file (need root permission)");
		return false;
	}

	::fputs(content, f);
	::fclose(f);

	INFO(std::string(NOTE) + "Created service: " + path);
	return true;
}

bool
start_service(const char *service_name) {
	char cmd[256];
	::snprintf(cmd, sizeof(cmd), "systemctl daemon-reload");
	if (::system(cmd) != 0) {
		INFO(WARN"Failed to reload systemd");
	}

	::snprintf(cmd, sizeof(cmd), "systemctl enable %s", service_name);
	if (::system(cmd) != 0) {
		INFO(ERR"Failed to enable service");
		return false;
	}

	::snprintf(cmd, sizeof(cmd), "systemctl start %s", service_name);
	if (::system(cmd) != 0) {
		INFO(ERR"Failed to start service");
		return false;
	}

	INFO(std::string(NOTE) + "Service started: " + service_name);
	return true;
}

}

bool
install(const char *src, const char *service_name, const char *port) {
	if (!copy_to_bin(src, service_name)) {
		return false;
	}

	if (!create_systemd_service(service_name, port)) {
		return false;
	}

	if (!start_service(service_name)) {
		return false;
	}

	INFO(NOTE"Installation complete!");
	return true;
}

bool
uninstall(const char *service_name) {
	char cmd[512];

	::snprintf(cmd, sizeof(cmd), "systemctl stop %s", service_name);
	const int stop_rc = ::system(cmd);
	(void)stop_rc;
	INFO(NOTE"Service stopped");

	::snprintf(cmd, sizeof(cmd), "systemctl disable %s", service_name);
	const int disable_rc = ::system(cmd);
	(void)disable_rc;
	INFO(NOTE"Service disabled");

	char path[PATH_MAX];
	::snprintf(path, sizeof(path), "/etc/systemd/system/%s.service", service_name);
	::unlink(path);
	INFO(NOTE"Service file removed");

	::unlink("/usr/bin/rcs");
	INFO(NOTE"Removed /usr/bin/rcs");

	return true;
}

}

#elif OS == OSX

#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syslimits.h>

namespace service {
namespace {

const char *plist_template = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key>
	<string>%s</string>
	<key>ProgramArguments</key>
	<array>
		<string>%s</string>
		<string>server</string>
		<string>--port=%s</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>KeepAlive</key>
	<true/>
	<key>StandardOutPath</key>
	<string>/var/log/rcs.log</string>
	<key>StandardErrorPath</key>
	<string>/var/log/rcs.error.log</string>
</dict>
</plist>
)";

bool
copy_to_bin(const char *src, const char *service_name) {
	char dest[PATH_MAX];
	struct stat st;

	(void)service_name;

	if (::stat(src, &st) != 0) {
		INFO(std::string(ERR) + "Source executable not found: " + src);
		return false;
	}

	::snprintf(dest, sizeof(dest), "/usr/local/bin/rcs");

	FILE *in = ::fopen(src, "rb");
	if (!in) {
		INFO(ERR"Cannot open source file");
		return false;
	}

	FILE *out = ::fopen(dest, "wb");
	if (!out) {
		INFO(std::string(ERR) + "Cannot create " + dest + " (need sudo)");
		::fclose(in);
		return false;
	}

	char buf[8192];
	size_t n;
	while ((n = ::fread(buf, 1, sizeof(buf), in)) > 0) {
		if (::fwrite(buf, 1, n, out) != n) {
			INFO(std::string(ERR) + "Failed to write to " + dest);
			::fclose(in);
			::fclose(out);
			return false;
		}
	}

	::fclose(in);
	::fclose(out);

	::chmod(dest, 0755);
	INFO(std::string(NOTE) + "Copied to " + dest);

	return true;
}

bool
create_launchd_service(const char *service_name, const char *bin_path, const char *port) {
	char path[PATH_MAX];
	char plist_content[4096];

	::snprintf(path, sizeof(path), "/Library/LaunchDaemons/%s.plist", service_name);

	::snprintf(plist_content, sizeof(plist_content), plist_template, service_name, bin_path, port);

	FILE *f = ::fopen(path, "w");
	if (!f) {
		INFO(ERR"Cannot create plist (need sudo)");
		return false;
	}

	::fputs(plist_content, f);
	::fclose(f);

	::chmod(path, 0644);
	INFO(std::string(NOTE) + "Created plist: " + path);

	return true;
}

bool
start_service(const char *service_name) {
	char cmd[512];

	::snprintf(cmd, sizeof(cmd), "launchctl unload /Library/LaunchDaemons/%s.plist 2>/dev/null", service_name);
	::system(cmd);

	::snprintf(cmd, sizeof(cmd), "launchctl load /Library/LaunchDaemons/%s.plist", service_name);
	if (::system(cmd) != 0) {
		INFO(WARN"Service load returned non-zero (may already be running)");
	}

	INFO(std::string(NOTE) + "Service started: " + service_name);
	return true;
}

bool
stop_service(const char *service_name) {
	char cmd[512];
	::snprintf(cmd, sizeof(cmd), "launchctl unload /Library/LaunchDaemons/%s.plist", service_name);
	::system(cmd);
	INFO(NOTE"Service stopped");
	return true;
}

}

bool
install(const char *src, const char *service_name, const char *port) {
	if (!copy_to_bin(src, service_name)) {
		return false;
	}

	if (!create_launchd_service(service_name, "/usr/local/bin/rcs", port)) {
		return false;
	}

	if (!start_service(service_name)) {
		return false;
	}

	INFO(NOTE"Installation complete!");
	return true;
}

bool
uninstall(const char *service_name) {
	stop_service(service_name);

	char path[PATH_MAX];
	::snprintf(path, sizeof(path), "/Library/LaunchDaemons/%s.plist", service_name);
	::unlink(path);
	INFO(std::string(NOTE) + "Plist removed: " + path);

	::unlink("/usr/local/bin/rcs");
	INFO(NOTE"Removed /usr/local/bin/rcs");

	return true;
}

}  // namespace service

#elif OS == WIN

#include <windows.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

namespace service {
namespace {

bool
copy_to_system32(const char *src) {
	char dest[MAX_PATH];
	::GetSystemDirectoryA(dest, MAX_PATH);
	::strcat_s(dest, MAX_PATH, "\\rcs.exe");

	if (!::CopyFileA(src, dest, FALSE)) {
		INFO(ERR"Failed to copy to System32 (need admin)");
		return false;
	}

	INFO(std::string(NOTE) + "Copied to " + dest);
	return true;
}

bool
delete_from_system32(void) {
	char dest[MAX_PATH];
	::GetSystemDirectoryA(dest, MAX_PATH);
	::strcat_s(dest, MAX_PATH, "\\rcs.exe");

	if (::DeleteFileA(dest)) {
		INFO(std::string(NOTE) + "Deleted " + dest);
	} else {
		INFO(std::string(WARN) + "Could not delete " + dest + " (may not exist)");
	}
	return true;
}

bool
create_service(const char *service_name, const char *port) {
	SC_HANDLE scm = ::OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!scm) {
		INFO(ERR"Cannot open Service Control Manager (need admin)");
		return false;
	}

	char exe_path[MAX_PATH];
	::GetSystemDirectoryA(exe_path, MAX_PATH);
	::strcat_s(exe_path, MAX_PATH, "\\rcs.exe");

	char cmd[MAX_PATH + 64];
	::snprintf(cmd, sizeof(cmd), "\"%s\" server --port=%s", exe_path, port);

	SC_HANDLE svc = ::CreateServiceA(
		scm,
		service_name,
		service_name,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		cmd,
		nullptr, nullptr, nullptr, nullptr, nullptr
	);

	if (!svc) {
		DWORD err = ::GetLastError();
		if (err == ERROR_SERVICE_EXISTS) {
			INFO(NOTE"Service already exists, trying to update...");
			svc = ::OpenServiceA(scm, service_name, SERVICE_ALL_ACCESS);
			if (svc) {
				::StartServiceA(svc, 0, nullptr);
				::CloseServiceHandle(svc);
				::CloseServiceHandle(scm);
				return true;
			}
		}
		INFO(std::string(ERR) + "Failed to create service: " + std::to_string(err));
		::CloseServiceHandle(scm);
		return false;
	}

	::CloseServiceHandle(svc);
	::CloseServiceHandle(scm);
	INFO(std::string(NOTE) + "Service created: " + service_name);
	return true;
}

bool
delete_service(const char *service_name) {
	SC_HANDLE scm = ::OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!scm) {
		return false;
	}

	SC_HANDLE svc = ::OpenServiceA(scm, service_name, SERVICE_ALL_ACCESS | DELETE);
	if (!svc) {
		::CloseServiceHandle(scm);
		return false;
	}

	SERVICE_STATUS status;
	::ControlService(svc, SERVICE_CONTROL_STOP, &status);
	::Sleep(500);

	::DeleteService(svc);
	::CloseServiceHandle(svc);
	::CloseServiceHandle(scm);

	INFO(std::string(NOTE) + "Service deleted: " + service_name);
	return true;
}

}

bool
install(const char *src, const char *service_name, const char *port) {
	if (!copy_to_system32(src)) {
		return false;
	}

	if (!create_service(service_name, port)) {
		return false;
	}

	INFO(NOTE"Installation complete!");
	return true;
}

bool
uninstall(const char *service_name) {
	delete_service(service_name);
	delete_from_system32();
	return true;
}

}

#else

namespace service {
bool
install(const char *, const char *, const char *) {
	INFO(ERR"Unsupported platform for service installation");
	return false;
}

bool
uninstall(const char *) {
	INFO(ERR"Unsupported platform for service uninstallation");
	return false;
}
}

#endif
