// Memory cards: libmc against the XMCSERV service over a host directory. The
// first GetInfo reports a new card, then directories, files written and read
// back through unaligned buffers, seeking, listings with wildcards, the
// current directory, renaming and deleting. Port 2 has no card.
#include <kernel.h>
#include <libmc.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

// mcOpen takes the IOP's open flags, not the C library's.
#define MC_RDONLY 0x0001
#define MC_WRONLY 0x0002
#define MC_CREAT 0x0200

static sceMcTblGetDir table[8];
static char buffer[1024] __attribute__((aligned(64)));
static char path[1024] __attribute__((aligned(64)));

static int result(void)
{
    int cmd, r;
    mcSync(0, &cmd, &r);
    return r;
}

static void info(int port)
{
    int type = -1, free = -1, format = -1, r;
    mcGetInfo(port, 0, &type, &free, &format);
    r = result();
    printf("getinfo port %d: %d type %d free %d format %d\n", port + 1, r, type, r >= -1 ? free : -1, format);
}

static void list(const char *pattern)
{
    int i, n;
    mcGetDir(0, 0, pattern, 0, 8, table);
    n = result();
    printf("list %s: %d\n", pattern, n);
    for (i = 0; i < n; i++)
        printf("  %-12s attr %04x size %u\n", table[i].EntryName, table[i].AttrFile, table[i].FileSizeByte);
}

int main(void)
{
    int fd, i;

    SifInitRpc(0);
    printf("modules %d %d %d\n", SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0, SifLoadModule("rom0:XMCMAN", 0, NULL) >= 0,
           SifLoadModule("rom0:XMCSERV", 0, NULL) >= 0);
    printf("init %d\n", mcInit(MC_TYPE_XMC));
    info(0);
    info(0);
    info(1);

    // Whatever an interrupted earlier run left behind.
    mcDelete(0, 0, "/PS2UCTEST/DATA.BIN");
    result();
    mcDelete(0, 0, "/PS2UCTEST/RENAMED.BIN");
    result();
    mcDelete(0, 0, "/PS2UCTEST");
    result();

    mcMkDir(0, 0, "/PS2UCTEST");
    printf("mkdir %d", result());
    mcMkDir(0, 0, "/PS2UCTEST");
    printf(", again %d\n", result());

    for (i = 0; i < 100; i++)
        buffer[3 + i] = 'A' + i % 26;
    mcOpen(0, 0, "/PS2UCTEST/DATA.BIN", MC_CREAT | MC_WRONLY);
    fd = result();
    printf("create %s\n", fd >= 0 ? "ok" : "failed");
    mcWrite(fd, buffer + 3, 100);
    printf("write %d\n", result());
    mcClose(fd);
    printf("close %d\n", result());
    info(0);

    memset(buffer, 0, sizeof buffer);
    mcOpen(0, 0, "/PS2UCTEST/DATA.BIN", MC_RDONLY);
    fd = result();
    mcRead(fd, buffer + 5, 100);
    printf("read %d: %.10s...%.4s\n", result(), buffer + 5, buffer + 101);
    mcSeek(fd, 30, SEEK_SET);
    printf("seek %d", result());
    mcRead(fd, buffer, 4);
    printf(", read %d: %.4s\n", result(), buffer);
    mcWrite(fd, buffer, 4);
    printf("write to a read-only file %d\n", result());
    mcClose(fd);
    result();

    list("/*");
    list("/PS2UCTEST/*");
    mcChdir(0, 0, "/PS2UCTEST", path);
    printf("chdir %d: %s\n", result(), path);
    mcOpen(0, 0, "DATA.BIN", MC_RDONLY);
    fd = result();
    printf("open relative %s\n", fd >= 0 ? "ok" : "failed");
    mcClose(fd);
    result();
    memset(path, 0, sizeof path);   // ps2sdk's libmc copies the new directory without its terminator
    mcChdir(0, 0, "..", path);
    printf("chdir .. %d: %s\n", result(), path);

    mcRename(0, 0, "/PS2UCTEST/DATA.BIN", "RENAMED.BIN");
    printf("rename %d\n", result());
    list("/PS2UCTEST/R*");
    mcDelete(0, 0, "/PS2UCTEST");
    printf("delete a full directory %d\n", result());
    mcDelete(0, 0, "/PS2UCTEST/RENAMED.BIN");
    printf("delete %d", result());
    mcDelete(0, 0, "/PS2UCTEST");
    printf(", %d\n", result());
    mcOpen(0, 0, "/PS2UCTEST/DATA.BIN", MC_RDONLY);
    printf("open deleted %d\n", result());
    info(0);
    return 0;
}
