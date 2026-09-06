#include "rdp_log.hpp"

#if PARALLEL_RDP_LOG
#include "logging.hpp"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static FILE *g_log_file = nullptr;
static bool g_log_enabled = false;

namespace {
class RDPLogInterface : public Util::LoggingInterface
{
public:
    bool log(const char *tag, const char *fmt, va_list va) override
    {
        if (!rdp_log_is_enabled()) return false;
        char msg[4096];
        vsnprintf(msg, sizeof(msg), fmt, va);
        rdp_log("%s%s", tag ? tag : "", msg);
        return true;
    }
};

static RDPLogInterface g_granite_log_iface;
}

static void get_log_path(char *out, size_t size)
{
    HMODULE module = NULL;
    out[0] = '\0';
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(const void *)&get_log_path, &module))
        return;
    const DWORD length = GetModuleFileNameA(module, out, (DWORD)size);
    if (length == 0u || length >= size) {
        out[0] = '\0';
        return;
    }
    char *const slash = strrchr(out, '\\');
    if (!slash) {
        out[0] = '\0';
        return;
    }
    slash[1] = '\0';
    if (strlen(out) + sizeof("parallel-rdp.log") > size) {
        out[0] = '\0';
        return;
    }
    strcat(out, "parallel-rdp.log");
}

void rdp_log_init(void)
{
    g_log_enabled = true;

    if (!g_log_enabled) {
        if (g_log_file) {
            fclose(g_log_file);
            g_log_file = nullptr;
        }
        return;
    }

    Util::set_thread_logging_interface(&g_granite_log_iface);

    if (!g_log_file) {
        char path[MAX_PATH];
        get_log_path(path, sizeof(path));
        if (path[0] != '\0') {
            g_log_file = fopen(path, "w");
        }
        if (!g_log_file) {
            g_log_file = fopen("parallel-rdp.log", "w");
        }

        if (g_log_file) {
            time_t now = time(nullptr);
            fprintf(g_log_file, "=== paraLLEl-RDP Full Log Session Start: %s", ctime(&now));
            fflush(g_log_file);
        }
    }
}

void rdp_log_close(void)
{
    if (g_log_file) {
        fprintf(g_log_file, "=== paraLLEl-RDP Log End ===\n");
        fclose(g_log_file);
        g_log_file = nullptr;
    }
    g_log_enabled = false;
}

bool rdp_log_is_enabled(void)
{
    return g_log_enabled;
}

void rdp_log(const char *fmt, ...)
{
    if (!g_log_enabled || !fmt) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "[%02d:%02d:%02d.%03d]",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (g_log_file) {
        fprintf(g_log_file, "%s [paraLLEl] %s\n", time_str, buf);
        fflush(g_log_file);
    }

    OutputDebugStringA(time_str);
    OutputDebugStringA(" [paraLLEl] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}
#endif
