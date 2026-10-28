// Controller input through libpad (PADMAN) or libpadx (XPADMAN, when built
// with -DXPAD), driven by a pad script: prints every change of the buttons
// and the left stick, and what the configuration queries report.
#include <kernel.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>

#ifdef XPAD
#define SIO2MAN "rom0:XSIO2MAN"
#define PADMAN "rom0:XPADMAN"
#else
#define SIO2MAN "rom0:SIO2MAN"
#define PADMAN "rom0:PADMAN"
#endif

static char padArea[256] __attribute__((aligned(64)));
static int vsyncSema;

static int vsyncHandler(int cause)
{
    (void)cause;
    iSignalSema(vsyncSema);
    ExitHandler();
    return 0;
}

static void waitFrame(void)
{
    WaitSema(vsyncSema);
}

static void waitRequest(void)
{
    while (padGetReqState(0, 0) != PAD_RSTAT_COMPLETE)
        waitFrame();
    while (padGetState(0, 0) != PAD_STATE_STABLE)
        waitFrame();
}

int main(void)
{
    ee_sema_t sema;
    struct padButtonStatus status;
    int i, modes, frames = 0;
    unsigned int last = 0xFFFFFFFF;

    sema.init_count = 0;
    sema.max_count = 1;
    sema.option = 0;
    vsyncSema = CreateSema(&sema);
    AddIntcHandler(INTC_VBLANK_S, vsyncHandler, 0);
    EnableIntc(INTC_VBLANK_S);

    SifInitRpc(0);
    SifLoadModule(SIO2MAN, 0, NULL);
    SifLoadModule(PADMAN, 0, NULL);
    padInit(0);
    printf("pad: init, %d ports, %d slots\n", padGetPortMax(), padGetSlotMax(0));
    if (!padPortOpen(0, 0, padArea)) {
        printf("pad: open failed\n");
        return 1;
    }
    while (padGetState(0, 0) != PAD_STATE_STABLE)
        waitFrame();
    modes = padInfoMode(0, 0, PAD_MODETABLE, -1);
    printf("pad: stable, current id %d, %d modes:", padInfoMode(0, 0, PAD_MODECURID, 0), modes);
    for (i = 0; i < modes; i++)
        printf(" %d", padInfoMode(0, 0, PAD_MODETABLE, i));
    printf("\n");

    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    waitRequest();
    printf("pad: dualshock mode, id %d, %d actuators\n", padInfoMode(0, 0, PAD_MODECURID, 0), padInfoAct(0, 0, -1, 0));
    if (padInfoPressMode(0, 0)) {
        padEnterPressMode(0, 0);
        waitRequest();
        printf("pad: pressure mode\n");
    }

    // Report changes for a few seconds of frames.
    for (frames = 0; frames < 200; frames++) {
        waitFrame();
        if (padRead(0, 0, &status) == 0)
            continue;
        unsigned int now = (0xFFFF ^ status.btns) | (status.ljoy_h << 16) | (status.ljoy_v << 24);
        if (now != last) {
            printf("pad: buttons 0x%04x lx %d ly %d cross pressure %d\n", now & 0xFFFF, status.ljoy_h, status.ljoy_v,
                   status.cross_p);
            last = now;
        }
    }
    padPortClose(0, 0);
    padEnd();
    printf("pad: done\n");
    return 0;
}
