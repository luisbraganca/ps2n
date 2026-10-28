// An IOP module exercising the kernel services an RPC server typically uses.
#include "irx_imports.h"

IRX_ID("rpctest", 1, 2);

#define RPCTEST_SID 0x0BADCAFE

static SifRpcDataQueue_t qd;
static SifRpcServerData_t sd;
static u32 rpcbuf[64];
static int sema, evf, mbx;
static volatile int worker_ticks, alarm_hits;

static unsigned int alarm_cb(void *arg)
{
    (void)arg;
    alarm_hits++;
    return alarm_hits < 3 ? 36864 : 0;   // again in 1 ms, three times in all
}

static void worker(void *arg)
{
    static iop_message_t msg;
    int i;
    for (i = 0; i < 3; i++) {
        DelayThread(2000);
        worker_ticks++;
    }
    SetEventFlag(evf, 0x2);
    SendMbx(mbx, &msg);
    printf("rpctest: worker done, arg %d\n", (int)arg);
    SignalSema(sema);
}

static void *rpc_handler(int fno, void *buf, int size)
{
    u32 *d = buf;
    switch (fno) {
    case 1: {
        u32 sum = 0;
        int i;
        for (i = 0; i < size / 4; i++)
            sum += d[i];
        d[0] = sum;
        break;
    }
    case 2: {   // blocks inside the server until the worker has finished
        u32 bits = 0;
        iop_message_t *m = NULL;
        WaitSema(sema);
        WaitEventFlag(evf, 0x2, WEF_AND | WEF_CLEAR, &bits);
        ReceiveMbx((void **)&m, mbx);
        d[0] = worker_ticks;
        d[1] = bits;
        d[2] = m != NULL;
        d[3] = alarm_hits;
        break;
    }
    case 3: {
        int a = d[0], b = d[1];
        sprintf((char *)buf, "iop says %d+%d=%d", a, b, a + b);
        break;
    }
    case 4: {
        void *p = AllocSysMemory(ALLOC_FIRST, 4096, NULL);
        d[0] = p != NULL;
        d[1] = FreeSysMemory(p) == 0;
        WaitVblankStart();
        WaitVblankStart();
        d[2] = 1;
        break;
    }
    }
    return buf;
}

static void server_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc(&sd, RPCTEST_SID, rpc_handler, rpcbuf, NULL, NULL, &qd);
    sceSifRpcLoop(&qd);
}

int _start(int argc, char *argv[])
{
    iop_thread_t t;
    iop_sema_t s;
    iop_event_t e;
    iop_mbx_t mb;
    iop_sys_clock_t clk;
    int i;

    printf("rpctest: start, argc %d:", argc);
    for (i = 1; i < argc; i++)
        printf(" %s", argv[i]);
    printf("\n");

    s.attr = 0;
    s.option = 0;
    s.initial = 0;
    s.max = 1;
    sema = CreateSema(&s);
    e.attr = 0;
    e.option = 0;
    e.bits = 0;
    evf = CreateEventFlag(&e);
    mb.attr = 0;
    mb.option = 0;
    mbx = CreateMbx(&mb);

    USec2SysClock(1000, &clk);
    SetAlarm(&clk, alarm_cb, NULL);

    t.attr = TH_C;
    t.option = 0;
    t.thread = server_thread;
    t.stacksize = 0x800;
    t.priority = 0x20;
    StartThread(CreateThread(&t), NULL);
    t.thread = worker;
    t.priority = 0x30;
    StartThread(CreateThread(&t), (void *)42);
    return MODULE_RESIDENT_END;
}
