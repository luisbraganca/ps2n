// An IOP module using libsd as a sound driver does: registers, voice
// transfers to sound memory and back with a completion handler, a looping
// block transfer (streamed PCM) raising one interrupt per half buffer, and
// batched commands.
#include "irx_imports.h"

IRX_ID("sdtest", 1, 0);

static int sema;
static volatile int interrupts[2];
static u8 samples[1024] __attribute__((aligned(64)));
static u8 back[1024] __attribute__((aligned(64)));
static u8 stream[2048] __attribute__((aligned(64)));

static int transfer_done(int core, void *arg)
{
    (void)arg;
    interrupts[core & 1]++;
    iSignalSema(sema);
    return 1;
}

int _start(int argc, char *argv[])
{
    iop_sema_t s;
    sceSdBatch batch[3];
    u32 rets[3];
    int i, half1, half2;

    (void)argc;
    (void)argv;
    s.attr = 0;
    s.option = 0;
    s.initial = 0;
    s.max = 16;
    sema = CreateSema(&s);

    printf("sdtest: init %d\n", sceSdInit(0));
    sceSdSetParam(0 | SD_PARAM_MVOLL, 0x3FFF);
    printf("sdtest: master volume %04x\n", sceSdGetParam(0 | SD_PARAM_MVOLL));
    sceSdSetAddr(SD_VOICE(0, 3) | SD_VADDR_SSA, 0x5000);
    printf("sdtest: start address %05x\n", sceSdGetAddr(SD_VOICE(0, 3) | SD_VADDR_SSA));
    printf("sdtest: voices ended %06x, envelope %04x\n", sceSdGetSwitch(0 | SD_SWITCH_ENDX),
           sceSdGetParam(SD_VOICE(0, 3) | SD_VPARAM_ENVX));
    printf("sdtest: an octave up: pitch %04x\n", sceSdNote2Pitch(60, 0, 72, 0));

    sceSdSetTransIntrHandler(0, transfer_done, NULL);
    sceSdSetTransIntrHandler(1, transfer_done, NULL);
    for (i = 0; i < (int)sizeof samples; i++)
        samples[i] = i * 7;
    printf("sdtest: voice transfer %d\n", sceSdVoiceTrans(0, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, samples, (u32 *)0x5000, sizeof samples));
    WaitSema(sema);
    printf("sdtest: status %d, interrupts %d\n", sceSdVoiceTransStatus(0, 1), interrupts[0]);
    sceSdVoiceTrans(0, SD_TRANS_READ | SD_TRANS_MODE_DMA, back, (u32 *)0x5000, sizeof back);
    WaitSema(sema);
    printf("sdtest: read back %s\n", memcmp(samples, back, sizeof back) == 0 ? "ok" : "wrong");

    printf("sdtest: block transfer %d\n", sceSdBlockTrans(1, SD_TRANS_WRITE | SD_TRANS_LOOP, stream, sizeof stream));
    WaitSema(sema);
    half1 = sceSdBlockTransStatus(1, 0) >> 24;
    WaitSema(sema);
    half2 = sceSdBlockTransStatus(1, 0) >> 24;
    sceSdBlockTrans(1, SD_TRANS_STOP, NULL, 0);
    printf("sdtest: playing halves %d %d, interrupts %d\n", half1, half2, interrupts[1]);

    batch[0].func = SD_BATCH_SETPARAM;
    batch[0].entry = 1 | SD_PARAM_MVOLR;
    batch[0].value = 0x1234;
    batch[1].func = SD_BATCH_GETPARAM;
    batch[1].entry = 1 | SD_PARAM_MVOLR;
    batch[1].value = 0;
    batch[2].func = SD_BATCH_GETSWITCH;
    batch[2].entry = 0 | SD_SWITCH_ENDX;
    batch[2].value = 0;
    i = sceSdProcBatch(batch, rets, 3);
    printf("sdtest: batch %d: %x %x\n", i, rets[1], rets[2]);
    printf("sdtest: quit %d\n", sceSdQuit());
    return MODULE_NO_RESIDENT_END;
}
