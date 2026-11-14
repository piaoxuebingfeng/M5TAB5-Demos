// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Only the BSD socket subset required by IOTC; ESP-IDF supplies it via lwIP.
#if defined(ESP_PLATFORM)
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <unistd.h>
#include <cerrno>
#include <cstring>
namespace obn { namespace net {
using socket_t=int;
constexpr socket_t kInvalid=-1;
inline int close_socket(socket_t s){return ::close(s);}
}}
