// Loads rpctest.irx through LOADFILE and calls its RPC server.
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>

#define RPCTEST_SID 0x0BADCAFE

static SifRpcClientData_t cd;
static u32 buf[64] __attribute__((aligned(64)));

int main(void)
{
    static const char args[] = "alpha\0beta";
    int ret;

    SifInitRpc(0);
    ret = SifLoadModule("host0:rpctest.irx", sizeof(args), args);
    printf("ee: load %s\n", ret >= 0 ? "ok" : "failed");
    for (;;) {
        if (SifBindRpc(&cd, RPCTEST_SID, 0) < 0) {
            printf("ee: bind failed\n");
            return 1;
        }
        if (cd.server)
            break;
        nopdelay();
    }
    printf("ee: bound\n");

    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 4;
    SifCallRpc(&cd, 1, 0, buf, 16, buf, 4, NULL, NULL);
    printf("ee: sum = %u\n", buf[0]);

    SifCallRpc(&cd, 2, 0, buf, 4, buf, 16, NULL, NULL);
    printf("ee: worker ticks %u, flag bits 0x%x, message %u, alarms %u\n", buf[0], buf[1], buf[2], buf[3]);

    buf[0] = 20; buf[1] = 22;
    SifCallRpc(&cd, 3, 0, buf, 8, buf, 64, NULL, NULL);
    printf("ee: %s\n", (char *)buf);

    SifCallRpc(&cd, 4, 0, buf, 4, buf, 12, NULL, NULL);
    printf("ee: memory %u/%u, vblank %u\n", buf[0], buf[1], buf[2]);

    buf[0] = 5; buf[1] = 6; buf[2] = 7; buf[3] = 8;
    SifCallRpc(&cd, 1, SIF_RPC_M_NOWAIT, buf, 16, buf, 4, NULL, NULL);
    while (SifCheckStatRpc(&cd))
        ;
    printf("ee: nowait sum = %u\n", buf[0]);
    return 0;
}
