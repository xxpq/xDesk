
#ifndef DESKX_ARGS_HPP
#define DESKX_ARGS_HPP

#include <map>
#include <string>
#include <vector>

class args {
public:
enum type {
	CLIENT, SERVER, INSTALL, UNINSTALL, UNKNOWN
};

	args(void) { }

	args(const int, char **);

	void
	operator=(const args &);

	std::string
	operator[](const std::string &) const;

	bool
	ok(void) const;

	type
	mode(void) const;

	void
	print(void) const;

	int
	num(const std::string &) const;

protected:
	std::map<std::string, std::string> map_;
	type type_ = UNKNOWN;
	std::string service_name_;

public:
	std::string
	service_name(void) const { return service_name_; }
};

#endif