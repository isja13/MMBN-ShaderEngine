#include "main.h"

bool _tstring_view_icmp::operator()(const _tstring_view &a, const _tstring_view &b) const {
    int ret = _tcsnicmp(a.data(), b.data(), std::min(a.size(), b.size()));
    if (ret == 0) return a.size() < b.size();
    return ret < 0;
}


#include "dinput8_dll.h"
#include "overlay.h"
#include "conf.h"
#include "ini.h"
#include "log.h"
#include "../RetroArch/retroarch.h"
#include <errhandlingapi.h>

#include <dbghelp.h>

#include <psapi.h>

static void DumpModules() {
    FILE* f = fopen("C:\\modules.log", "w");
    if (!f) return;

    HMODULE mods[1024];
    DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();

    if (EnumProcessModules(proc, mods, sizeof(mods), &needed)) {
        unsigned count = needed / sizeof(HMODULE);
        for (unsigned i = 0; i < count; ++i) {
            MODULEINFO mi = {};
            char path[MAX_PATH] = {};
            GetModuleInformation(proc, mods[i], &mi, sizeof(mi));
            GetModuleFileNameA(mods[i], path, MAX_PATH);
            fprintf(f, "%p %p %s\n",
                mi.lpBaseOfDll,
                (char*)mi.lpBaseOfDll + mi.SizeOfImage,
                path);
        }
    }
    fclose(f);
}


static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Ignore MSVC thread naming exception
    if (code == 0x406D1388)
        return EXCEPTION_CONTINUE_SEARCH;

    // Optional: ignore breakpoint exceptions too if you don't want test traps logged
     if (code == EXCEPTION_BREAKPOINT)
         return EXCEPTION_CONTINUE_SEARCH;

     DumpModules();

    FILE* f = fopen("C:\\crash.log", "w");
    if (f) {
        HANDLE proc = GetCurrentProcess();
        SymInitialize(proc, nullptr, TRUE);

        fprintf(f, "Code: 0x%08lx\n", ep->ExceptionRecord->ExceptionCode);
        fprintf(f, "Addr: %p\n", ep->ExceptionRecord->ExceptionAddress);
        fprintf(f, "RIP: %p\n", (void*)ep->ContextRecord->Rip);
        fprintf(f, "RAX: %p\n", (void*)ep->ContextRecord->Rax);
        fprintf(f, "RCX: %p\n", (void*)ep->ContextRecord->Rcx);
        fprintf(f, "RDX: %p\n", (void*)ep->ContextRecord->Rdx);
        fprintf(f, "R8:  %p\n", (void*)ep->ContextRecord->R8);
        fprintf(f, "RSP: %p\n", (void*)ep->ContextRecord->Rsp);
        fprintf(f, "RBP: %p\n", (void*)ep->ContextRecord->Rbp);

        fprintf(f, "ExceptionInformationCount: %lu\n",
            (unsigned long)ep->ExceptionRecord->NumberParameters);
        for (ULONG i = 0; i < ep->ExceptionRecord->NumberParameters; ++i) {
            fprintf(f, "ExceptionInformation[%lu]: %p\n",
                (unsigned long)i,
                (void*)ep->ExceptionRecord->ExceptionInformation[i]);
        }

        // Resolve crash address to symbol + source line
        char sym_buf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = (SYMBOL_INFO*)sym_buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 256;
        DWORD64 sym_disp = 0;
        if (SymFromAddr(proc, ep->ContextRecord->Rip, &sym_disp, sym))
            fprintf(f, "Symbol: %s + 0x%llx\n", sym->Name, sym_disp);
        else
            fprintf(f, "Symbol: unresolved (0x%lx)\n", GetLastError());

        IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof(line);
        DWORD line_disp = 0;
        if (SymGetLineFromAddr64(proc, ep->ContextRecord->Rip, &line_disp, &line))
            fprintf(f, "Source: %s : %lu\n", line.FileName, line.LineNumber);

        // Stack walk
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 sf = {};
        sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;

        fprintf(f, "\nStack:\n");
        for (int i = 0; i < 24; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(),
                &sf, &ctx, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 256;
            sym_disp = 0;
            DWORD64 moduleBase = SymGetModuleBase64(proc, sf.AddrPC.Offset);
            char modPath[MAX_PATH] = {};
            if (moduleBase) {
                GetModuleFileNameA((HMODULE)moduleBase, modPath, MAX_PATH);
            }
            if (SymFromAddr(proc, sf.AddrPC.Offset, &sym_disp, sym))
                fprintf(f, "  [%02d] %s + 0x%llx  (%016llx)  [%s]\n",
                    i, sym->Name, sym_disp, sf.AddrPC.Offset,
                    modPath[0] ? modPath : "<unknown>");
            else
                fprintf(f, "  [%02d] %016llx  [%s]\n",
                    i, sf.AddrPC.Offset,
                    modPath[0] ? modPath : "<unknown>");

            line.SizeOfStruct = sizeof(line);
            if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &line_disp, &line))
                fprintf(f, "       %s : %lu\n", line.FileName, line.LineNumber);
        }

        fflush(f);
        fclose(f);
        SymCleanup(proc);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static Logger dummy_logger = {NULL};
BOOL WINAPI DllMain(
    HINSTANCE hinstDLL, // handle to DLL module
    DWORD fdwReason,    // reason for calling function
    LPVOID lpReserved   // reserved
) {
    // Perform actions based on the reason for calling.
    switch( fdwReason )
    {
        case DLL_PROCESS_ATTACH:
        // Initialize once for each new process.
        // Return FALSE to fail DLL load.
        {
            AddVectoredExceptionHandler(1, CrashHandler);
            SetUnhandledExceptionFilter(CrashHandler);
            atexit([]() {
                FILE* f = fopen("C:\\exit.log", "w");
                if (f) {
                    fprintf(f, "Process exiting normally via atexit\n");
                    fclose(f);
                }
                });
            DisableThreadLibraryCalls(hinstDLL);
            my_config_init();

            default_overlay = new Overlay();
            default_overlay(MOD_NAME " loaded");

            default_config = new Config();

            default_ini = new Ini(INI_FILE_NAME);
            default_ini->set_overlay(default_overlay);
            default_ini->set_config(default_config);

            default_logger = new Logger(LOG_FILE_NAME);
            default_logger->set_overlay(default_overlay);
            default_logger->set_config(default_config);

            base_dll_init(hinstDLL);
            //DumpModules();
            break;
        }

        case DLL_THREAD_ATTACH:
        // Do thread-specific initialization.
            break;

        case DLL_THREAD_DETACH:
        // Do thread-specific cleanup.
            break;

        case DLL_PROCESS_DETACH:
        // Perform any necessary cleanup.
        {
            // May need to add synchronizations
            // in this block.
            base_dll_shutdown();

            delete default_logger;
            default_logger = &dummy_logger;
            delete default_ini;
            delete default_config;
            delete default_overlay;

            my_config_free();
            break;
        }
    }
    return TRUE; // Successful DLL_PROCESS_ATTACH.
}

class cs_wrapper::Impl {
    CRITICAL_SECTION cs;
    friend class cs_wrapper;
};

cs_wrapper::cs_wrapper() : impl(new Impl()) {
    InitializeCriticalSection(&impl->cs);
}
cs_wrapper::~cs_wrapper() {
    DeleteCriticalSection(&impl->cs);
    delete impl;
}

void cs_wrapper::begin_cs() {
    EnterCriticalSection(&impl->cs);
}

bool cs_wrapper::try_begin_cs() {
    return TryEnterCriticalSection(&impl->cs);
}

void cs_wrapper::end_cs() {
    LeaveCriticalSection(&impl->cs);
}
