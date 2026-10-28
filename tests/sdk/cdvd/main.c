// CD/DVD: libcdvd against the cdvdfsv service, with the disc built from
// tests/sdk/disc. Disc type and status, the tray check, file search, sector
// reads into aligned and unaligned buffers, streaming, the clock, and the
// same file through cdrom0: and the C library.
#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

static u8 buffer[4 * 2048] __attribute__((aligned(64)));

// DATA\PATTERN.TXT holds 5000 letters: 'a' + offset % 26.
static int pattern_ok(const u8 *p, int offset, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != 'a' + (offset + i) % 26)
            return 0;
    return 1;
}

int main(void)
{
    sceCdlFILE file;
    sceCdRMode mode = {0, SCECdSpinNom, SCECdSecS2048, 0};
    sceCdCLOCK clock;
    u32 changed, error;
    void *ring;
    FILE *fp;
    int r;

    SifInitRpc(0);
    printf("init %d\n", sceCdInit(SCECdINIT));
    printf("mmode %d\n", sceCdMmode(SCECdMmodeDvd));
    printf("disk ready %d\n", sceCdDiskReady(0));
    printf("disk type 0x%02x\n", sceCdGetDiskType());
    sceCdTrayReq(SCECdTrayCheck, &changed);
    printf("tray check: changed %u", changed);
    sceCdTrayReq(SCECdTrayCheck, &changed);
    printf(", then %u\n", changed);

    r = sceCdSearchFile(&file, "\\SYSTEM.CNF;1");
    printf("search SYSTEM.CNF: %d size %u name %s\n", r, file.size, file.name);
    r = sceCdSearchFile(&file, "\\MISSING.BIN;1");
    printf("search MISSING.BIN: %d\n", r);
    r = sceCdSearchFile(&file, "\\DATA\\PATTERN.TXT;1");
    printf("search DATA\\PATTERN.TXT: %d size %u name %s\n", r, file.size, file.name);

    // Three sectors into an aligned buffer, then one into an unaligned one:
    // the bytes around it must stay as they were.
    memset(buffer, 0xEE, sizeof buffer);
    r = sceCdRead(file.lsn, 3, buffer, &mode);
    sceCdSync(0);
    printf("read 3 sectors: %d, data %s, error %d\n", r, pattern_ok(buffer, 0, 5000) ? "ok" : "wrong", sceCdGetError());
    memset(buffer, 0xEE, sizeof buffer);
    r = sceCdRead(file.lsn + 1, 1, buffer + 3, &mode);
    sceCdSync(0);
    printf("read 1 sector to buffer+3: %d, data %s, around %02x %02x\n", r, pattern_ok(buffer + 3, 2048, 2048) ? "ok" : "wrong",
           buffer[2], buffer[3 + 2048]);
    printf("status 0x%02x\n", sceCdStatus());

    // Streaming: a ring in IOP memory, read back two sectors at a time.
    ring = SifAllocIopHeap(16 * 2048);
    printf("stream init %d\n", sceCdStInit(16, 4, ring));
    printf("stream start %d\n", sceCdStStart(file.lsn, &mode));
    r = sceCdStRead(2, (u32 *)buffer, STMBLK, &error);
    printf("stream read %d (error %u), data %s\n", r, error, pattern_ok(buffer, 0, 4096) ? "ok" : "wrong");
    r = sceCdStRead(1, (u32 *)buffer, STMBLK, &error);
    printf("stream read %d (error %u), data %s\n", r, error, pattern_ok(buffer, 4096, 904) ? "ok" : "wrong");
    printf("stream stop %d\n", sceCdStStop());
    SifFreeIopHeap(ring);

    // The clock runs on Japan time; --virtual-clock starts it on 2001-07-04.
    r = sceCdReadClock(&clock);
    printf("clock %d: 20%02x-%02x-%02x\n", r, clock.year, clock.month, clock.day);

    fp = fopen("cdrom0:\\DATA\\PATTERN.TXT;1", "rb");
    if (fp) {
        fseek(fp, 1000, SEEK_SET);
        r = fread(buffer, 1, 26, fp);
        printf("cdrom0: read %d at 1000, data %s\n", r, pattern_ok(buffer, 1000, 26) ? "ok" : "wrong");
        fclose(fp);
    } else {
        printf("cdrom0: open failed\n");
    }
    return 0;
}
