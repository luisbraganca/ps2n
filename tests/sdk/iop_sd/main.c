// Loads sdtest.irx, which exercises libsd on the IOP and prints the results.
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>

int main(void)
{
    int ret;

    SifInitRpc(0);
    ret = SifLoadModule("host0:sdtest.irx", 0, NULL);
    printf("ee: load %s\n", ret >= 0 ? "ok" : "failed");
    return 0;
}
