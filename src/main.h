#ifndef MAIN_H
#define MAIN_H

#define ENABLE_LOGGER 1
// Filters currently do not apply to FMVs and certain menu items
#define ENABLE_SLANG_SHADER 1
// Custom display resolution seems to work now
// Custom render resolution separate from display still broken
#define ENABLE_CUSTOM_RESOLUTION 0

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif

#ifndef WINBOOL
#define WINBOOL BOOL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Ensure _WIN32_WINNT is defined correctly
#define WINVER _WIN32_WINNT

#include <windows.h>
#include <tchar.h>
#include <dxgi.h>
#include <dxgiformat.h>
#include <initguid.h>
#define D3D11_NO_HELPERS
#include <C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\d3d11.h>
#include <dinput.h>

#ifndef COM_RELEASE_DECLARED
#define COM_RELEASE_DECLARED
static inline ULONG Release(void* object) {
    if (object) return ((IUnknown*)object)->Release();
    return 0;
}
#endif
#ifndef COM_ADDREF_DECLARED
#define COM_ADDREF_DECLARED
static inline ULONG AddRef(void* object) {
    if (object) return ((IUnknown*)object)->AddRef();
    return 0;
}
#endif

#ifdef UNICODE
#define BASE_DLL_NAME L"\\dxgi.dll"
#else
#define BASE_DLL_NAME "\\dxgi.dll"
#endif

#define VK_VALUE_BEGIN 1
#define VK_VALUE_END ((BYTE)-1)

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <string>
#include <string_view>
#define _tstring std::basic_string<TCHAR>
#define _tstring_view std::basic_string_view<TCHAR>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <type_traits>
#include <limits>
#include <algorithm>
#include <codecvt>
#include <tuple>
#include <atomic>
#include <regex>

#define CONCAT_BASE(a, b) a ## b
#define CONCAT(a, b) CONCAT_BASE(a, b)
#define STRINGIFY_BASE(n) #n
#define STRINGIFY(n) STRINGIFY_BASE(n)

struct _tstring_view_icmp {
    bool operator()(const _tstring_view& a, const _tstring_view& b) const;
};
#define MAP_ENUM(t) std::map<_tstring_view, t, _tstring_view_icmp>
#define MAP_ENUM_ITEM(n) { _T(#n), n }
#define ENUM_MAP(t) std::map<t, std::string>
#define ENUM_MAP_ITEM(n) { n, #n }
#define ENUM_CLASS_MAP_ITEM(n) { ENUM_CLASS::n, #n }
#define FLAG_MAP(t) std::vector<std::pair<t, std::string>>

#define MOD_NAME "Better Filters"
#define LOG_FILE_NAME _T("better-filters.log")
#define INI_FILE_NAME _T("better-filters.ini")

class cs_wrapper {
    class Impl;
    Impl* impl;

public:
    cs_wrapper();
    ~cs_wrapper();
    void begin_cs();
    bool try_begin_cs();
    void end_cs();
};

#endif