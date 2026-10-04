#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <sys/ptrace.h>
#include <dlfcn.h>
#include <sys/types.h>

long (*orig_ptrace)(enum __ptrace_request, pid_t, void *, void *) = NULL;

// Match the exact variadic signature of the system's ptrace
long ptrace(enum __ptrace_request request, ...) {
    va_list ap;
    va_start(ap, request);
    
    // Extract the standard arguments from the argument pool
    pid_t pid = va_arg(ap, pid_t);
    void *addr = va_arg(ap, void *);
    void *data = va_arg(ap, void *);
    
    va_end(ap);
    if (request == PTRACE_TRACEME) {
        fprintf(stderr, "[LD_PRELOAD] Intercepted ptrace(PTRACE_TRACEME). Returning 0.\n");
        return 0; 
    }

    if (!orig_ptrace) orig_ptrace = dlsym(RTLD_NEXT, "ptrace");
    
    return orig_ptrace(request, pid, addr, data);
}
