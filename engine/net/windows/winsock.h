#pragma once

// The one way the Windows socket backend includes the Winsock headers (Phase 20 Day 278), as <windows.h> goes through
// os/windows/win32.h: only files under engine/net/windows/ include this, never a neutral header.
//
// <windows.h> comes first, through its wrapper (the build defines WIN32_LEAN_AND_MEAN, so it does not pull in the old
// <winsock.h>, which would clash with <winsock2.h>). The Winsock headers are included between the same
// unity/windows_enable.h and unity/windows_disable.h pair, so the macros they share with <windows.h> stay out of the
// files merged after this one in a unity build. <mswsock.h> has SIO_UDP_CONNRESET, <mstcpip.h> SIO_TCP_INITIAL_RTO;
// <iphlpapi.h> declares if_nametoindex (scope names), from iphlpapi.lib.

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <mstcpip.h>
#include <iphlpapi.h>
#include "unity/windows_disable.h"
