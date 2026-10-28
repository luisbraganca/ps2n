// The IOP side of SIF for module code: the sifman and sifcmd libraries.
//
// RPC servers that modules register live in the modules' own structures
// (SifRpcServerData_t, SifRpcDataQueue_t), which these functions maintain the
// way ps2sdk's sifcmd does, so module code that looks into them still works.
// sceSifExecRequest and sceSifRpcLoop call back into the server function,
// which may block, so they are generated as small guest routines.
#include "iop/sif.h"

#include "iop/cpu.h"
#include "iop/hle.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "log.h"
#include "ps2/memory.h"

namespace ps2::iop::sif {

using namespace hle;

namespace {

// SifRpcServerData_t / SifRpcDataQueue_t field offsets (sifrpc-common.h).
constexpr uint32_t SD_SID = 0, SD_FUNC = 4, SD_BUF = 8, SD_SIZE = 12, SD_CFUNC = 16, SD_CBUF = 20, SD_LINK = 56, SD_NEXT = 60,
                   SD_BASE = 64;
constexpr uint32_t QD_THREAD = 0, QD_ACTIVE = 4, QD_LINK = 8, QD_START = 12, QD_END = 16, QD_NEXT = 20;

// ---- sifcmd ---------------------------------------------------------------------------------
void scGetSreg() { retU(iopSreg(arg(0))); }
void scSetSreg() {
    const uint32_t old = iopSreg(arg(0));
    setIopSreg(arg(0), arg(1));
    retU(old);
}
void scAddCmdHandler() {
    setCommandHandler(arg(0), arg(1), arg(2), g_cpu.gpr[R_GP]);
    ret(0);
}
void scRemoveCmdHandler() {
    setCommandHandler(arg(0), 0, 0, 0);
    ret(0);
}
void scSendCmd() {
    sendCommandToEe(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
    ret(1);
}
void scSendCmdIntr() {
    sendCommandToEe(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
    if (const uint32_t cb = arg(6)) kernel::callHandler(cb, arg(7), g_cpu.gpr[R_GP]);
    ret(1);
}

void scSetRpcQueue() {
    const uint32_t qd = arg(0);
    mem::write32(qd + QD_THREAD, arg(1));
    for (uint32_t off : {QD_ACTIVE, QD_LINK, QD_START, QD_END, QD_NEXT}) mem::write32(qd + off, 0);
    addRpcQueue(qd);
    ret(0);
}

void scRegisterRpc() {
    const uint32_t sd = arg(0), qd = arg(6);
    mem::write32(sd + SD_SID, arg(1));
    mem::write32(sd + SD_FUNC, arg(2));
    mem::write32(sd + SD_BUF, arg(3));
    mem::write32(sd + SD_CFUNC, arg(4));
    mem::write32(sd + SD_CBUF, arg(5));
    mem::write32(sd + SD_NEXT, 0);
    mem::write32(sd + SD_LINK, 0);
    mem::write32(sd + SD_BASE, qd);
    uint32_t prev = 0;
    for (uint32_t s = mem::read32(qd + QD_LINK); s; s = mem::read32(s + SD_LINK)) prev = s;
    mem::write32(prev ? prev + SD_LINK : qd + QD_LINK, sd);
    PS2_DEBUG("iop", "IOP RPC server sid 0x%08x registered (sd 0x%08x, func 0x%08x)", arg(1), sd, arg(2));
    ret(0);
}

void scRemoveRpc() {
    const uint32_t sd = arg(0), qd = arg(1);
    for (uint32_t at = qd + QD_LINK; mem::read32(at); at = mem::read32(at) + SD_LINK) {
        if (mem::read32(at) == sd) {
            mem::write32(at, mem::read32(sd + SD_LINK));
            return retU(sd);
        }
    }
    retU(0);
}

void scRemoveRpcQueue() {
    removeRpcQueue(arg(0));
    retU(arg(0));
}

void scGetNextRequest() {
    const uint32_t qd = arg(0);
    const uint32_t sd = mem::read32(qd + QD_START);
    mem::write32(qd + QD_ACTIVE, sd ? 1 : 0);
    mem::write32(qd + QD_START, sd ? mem::read32(sd + SD_NEXT) : 0);
    retU(sd);
}

void scExecDone() {   // (sd, value the server function returned)
    finishGuestRpc(arg(0), arg(1));
}

void scGetOtherData() {
    // (rd, EE src, IOP dest, size, mode): the data is already reachable; copy it.
    const uint32_t src = arg(1), dest = arg(2), size = arg(3);
    std::vector<uint8_t> tmp(size);
    readBytes(src, tmp.data(), size);
    mem::writeBlock(dest, tmp.data(), size);
    ret(0);
}

void scUnsupportedClient() {
    PS2_UNIMPL("iop", "IOP module calling an EE RPC server",
               "an IOP module called %s: IOP modules acting as RPC clients of EE servers are not supported yet",
               trapName(currentTrap()).c_str());
    ret(-1);
}

void zero() { ret(0); }

// ---- sifman ---------------------------------------------------------------------------------------
uint32_t g_dmaId = 1;

void smSetDma() {
    // SifDmaTransfer_t: src (IOP), dest (EE), size, attr.
    const uint32_t list = arg(0);
    const int count = sarg(1);
    for (int i = 0; i < count; ++i) {
        const uint32_t d = list + 16 * static_cast<uint32_t>(i);
        dmaToEe(mem::read32(d), mem::read32(d + 4), mem::read32(d + 8));
    }
    retU(g_dmaId++ & 0x7FFFFFFF);
}
void smSetOneDma() {   // the struct is passed by value: $a0-$a3
    dmaToEe(arg(0), arg(1), arg(2));
    retU(g_dmaId++ & 0x7FFFFFFF);
}
void smDmaStat() { ret(-1); }   // every transfer has completed
void smGetMainAddr() { retU(sifRegReadIop(0x1D000000u)); }
void smGetSubAddr() { retU(kernel::kSifCommandBuffer); }
void smGetMSFlag() { retU(sifRegReadIop(0x1D000020u)); }
void smSetMSFlag() {
    sifRegWriteIop(0x1D000020u, arg(0));
    retU(sifRegReadIop(0x1D000020u));
}
void smGetSMFlag() { retU(sifRegReadIop(0x1D000030u)); }
void smSetSMFlag() {
    sifRegWriteIop(0x1D000030u, arg(0));
    retU(sifRegReadIop(0x1D000030u));
}
void smCheckInit() { ret(1); }

} // namespace

void init() {
    g_dmaId = 1;
    defineLibrary("sifcmd", 0x0101, {
        {4, "sceSifInitCmd", &zero},
        {5, "sceSifExitCmd", &zero},
        {6, "sceSifGetSreg", &scGetSreg},
        {7, "sceSifSetSreg", &scSetSreg},
        {8, "sceSifSetCmdBuffer", &zero},
        {9, "sceSifSetSysCmdBuffer", &zero},
        {10, "sceSifAddCmdHandler", &scAddCmdHandler},
        {11, "sceSifRemoveCmdHandler", &scRemoveCmdHandler},
        {12, "sceSifSendCmd", &scSendCmd},
        {13, "isceSifSendCmd", &scSendCmd},
        {14, "sceSifInitRpc", &zero},
        {15, "sceSifBindRpc", &scUnsupportedClient},
        {16, "sceSifCallRpc", &scUnsupportedClient},
        {17, "sceSifRegisterRpc", &scRegisterRpc},
        {18, "sceSifCheckStatRpc", &zero},
        {19, "sceSifSetRpcQueue", &scSetRpcQueue},
        {20, "sceSifGetNextRequest", &scGetNextRequest},
        {21, "sceSifExecRequest", nullptr},   // guest routine below
        {22, "sceSifRpcLoop", nullptr},       // guest routine below
        {23, "sceSifGetOtherData", &scGetOtherData},
        {24, "sceSifRemoveRpc", &scRemoveRpc},
        {25, "sceSifRemoveRpcQueue", &scRemoveRpcQueue},
        {26, "sceSifSetSif1CB", &zero},
        {27, "sceSifClearSif1CB", &zero},
        {28, "sceSifSendCmdIntr", &scSendCmdIntr},
        {29, "isceSifSendCmdIntr", &scSendCmdIntr},
    });
    defineLibrary("sifman", 0x0101, {
        {4, "sceSifDma2Init", &zero},
        {5, "sceSifInit", &zero},
        {6, "sceSifSetDChain", &zero},
        {7, "sceSifSetDma", &smSetDma},
        {8, "sceSifDmaStat", &smDmaStat},
        {9, "sceSifSetOneDma", &smSetOneDma},
        {10, "sceSifSendSync", &zero},
        {11, "sceSifIsSending", &zero},
        {21, "sceSifGetMSFlag", &smGetMSFlag},
        {22, "sceSifSetMSFlag", &smSetMSFlag},
        {23, "sceSifGetSMFlag", &smGetSMFlag},
        {24, "sceSifSetSMFlag", &smSetSMFlag},
        {25, "sceSifGetMainAddr", &smGetMainAddr},
        {26, "sceSifGetSubAddr", &smGetSubAddr},
        {27, "sceSifSetSubAddr", &zero},
        {28, "sceSifIntrMain", &zero},
        {29, "sceSifCheckInit", &smCheckInit},
        {30, "sceSifSetDmaIntrHandler", &zero},
        {31, "sceSifResetDmaIntrHandler", &zero},
        {32, "sceSifSetDmaIntr", &zero},
    });

    // Guest routines, written into the kernel's thunk area.
    const uint32_t execDone = addTrap(&scExecDone, "sifcmd::sceSifExecRequest (reply)");
    CodeBuilder c(kernel::kThunkArea);

    // void sceSifExecRequest(SifRpcServerData_t *sd)
    const uint32_t exec = c.here();
    c.addiu(R_SP, R_SP, -24);
    c.sw(R_RA, 20, R_SP);
    c.sw(R_S0, 16, R_SP);
    c.move(R_S0, R_A0);
    c.lw(R_T9, SD_FUNC, R_S0);
    c.lw(R_A0, 36, R_S0);           // rpc_number
    c.lw(R_A1, SD_BUF, R_S0);
    c.jalr(R_T9);
    c.lw(R_A2, SD_SIZE, R_S0);      // delay slot: size
    c.move(R_A0, R_S0);
    c.jal(execDone);
    c.move(R_A1, R_V0);             // delay slot
    c.lw(R_RA, 20, R_SP);
    c.lw(R_S0, 16, R_SP);
    c.jr(R_RA);
    c.addiu(R_SP, R_SP, 24);

    // void sceSifRpcLoop(SifRpcDataQueue_t *qd): run requests, sleep when there are none.
    const uint32_t loopFn = c.here();
    c.addiu(R_SP, R_SP, -24);
    c.sw(R_RA, 20, R_SP);
    c.sw(R_S0, 16, R_SP);
    c.move(R_S0, R_A0);
    const auto top = c.label(), sleep = c.label();
    c.bind(top);
    c.jal(exportAddress("sifcmd", 20));   // sceSifGetNextRequest
    c.move(R_A0, R_S0);
    c.beq(R_V0, R_ZERO, sleep);
    c.nop();
    c.jal(exec);
    c.move(R_A0, R_V0);
    c.beq(R_ZERO, R_ZERO, top);
    c.nop();
    c.bind(sleep);
    c.jal(exportAddress("thbase", 24));   // SleepThread
    c.nop();
    c.beq(R_ZERO, R_ZERO, top);
    c.nop();

    // int CpuInvokeInKmode(void *function, ...): a tail call with the arguments shifted.
    const uint32_t invoke = c.here();
    c.move(R_T9, R_A0);
    c.move(R_A0, R_A1);
    c.move(R_A1, R_A2);
    c.jr(R_T9);
    c.move(R_A2, R_A3);

    if (c.here() > kernel::kThunkAreaEnd) PS2_ERROR("iop", "generated IOP routines overflow their area");
    setExport("sifcmd", 21, exec);
    setExport("sifcmd", 22, loopFn);
    setExport("intrman", 14, invoke);
}

} // namespace ps2::iop::sif
