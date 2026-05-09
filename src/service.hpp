#ifndef DESKX_SERVICE_HPP
#define DESKX_SERVICE_HPP

namespace service {

bool install(const char* src, const char* service_name, const char* port);
bool uninstall(const char* service_name);

} // namespace service

#endif // DESKX_SERVICE_HPP
