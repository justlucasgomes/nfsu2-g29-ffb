#pragma once
// proxy_log.h — log compartilhado entre dllmain.cpp e dinput_proxy.cpp
// dllmain.cpp define PROXY_LOG_OWNER antes de incluir, criando o arquivo.
// dinput_proxy.cpp só declara o extern e escreve.
#include <windows.h>
#include <stdio.h>

#ifdef PROXY_LOG_OWNER
FILE* g_proxy_log = nullptr;

void ProxyLogOpen() {
    if (g_proxy_log) return;
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* s = strrchr(exe, '\\'); if (s) s[1] = '\0';
    char logDir[MAX_PATH], logPath[MAX_PATH];
    wsprintfA(logDir,  "%slogs",          exe);
    wsprintfA(logPath, "%slogs\\g29_ffb.log", exe);
    CreateDirectoryA(logDir, nullptr);
    g_proxy_log = fopen(logPath, "w");   // "w" apenas aqui — uma vez só
}
#else
extern FILE* g_proxy_log;
#endif

static inline void ProxyLog(const char* msg) {
    if (!g_proxy_log) return;
    fprintf(g_proxy_log, "%s\n", msg);
    fflush(g_proxy_log);
}

static inline void ProxyLogFmt(const char* fmt, ...) {
    if (!g_proxy_log) return;
    char buf[512];
    va_list a; va_start(a, fmt);
    wvsprintfA(buf, fmt, a);
    va_end(a);
    ProxyLog(buf);
}
