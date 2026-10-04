#include <signal.h>
#include <stdio.h>

static void trap(int sig)
{
    puts("single-step trap!");
}

int main(void)
{
    signal(SIGTRAP, trap);

    asm volatile(
        "pushfq\n"
        "orq $0x100, (%%rsp)\n"
        "popfq\n"
        "nop\n"
        "nop\n"
        "nop\n"
        :
        :
        : "memory"
    );

    asm volatile(
        "pushfq\n"
        "andq $~0x100, (%%rsp)\n"
        "popfq\n"
        :
        :
        : "memory"
    );

    return 0;
}
