// The IOP: its CPU (interpreted, cpu.h), its kernel (high-level emulated,
// kernel.h), and SIF, the link to the EE.
//
// The EE talks to the IOP through SIF: DMA transfers into IOP memory carry
// command packets (RPC bind/call, ...), and replies come back as packets
// written to the EE's receive buffer followed by a SIF0 DMA interrupt. This
// module implements that wire protocol, so the game's own EE-side libraries
// (sceSifCallRpc, ps2sdk's SifCallRpc, ...) work unmodified. RPC servers are
// either host services (FILEIO, LOADFILE, pad, ...) or servers that IOP
// modules register, whose code runs on the IOP interpreter.
//
// The IOP runs on the EE's host thread, in slices: at EE safepoints and
// syscalls it catches up with the EE's clock (sync), and while every EE
// thread waits it runs ahead (runWhileEeIdle).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ps2::iop {

constexpr uint32_t kIopRamSize = 2u << 20;

void init();
// LoadExecPS2: the IOP restarts with only its resident services, as the BIOS
// reboots it before loading the next executable. The disc stays inserted.
void reboot();
// The SDK release a module on the IOP ("fileio", "loadfile") reports to the
// EE libraries' version queries: 1000 for the BIOS's, or what the replacement
// image the game rebooted the IOP with (IOPRPxxx.IMG) gives it, such as 2140.
// FILEIO's request layouts depend on it.
uint32_t moduleVersion(const std::string& module);
uint8_t* ramPtr(uint32_t offset);

// ---- time ------------------------------------------------------------------------------------
void sync();                 // run the IOP up to the EE's current time
bool busy();                 // the IOP has work it could do right now
// Runs what is ready now (an interrupt just raised, threads it wakes)
// outside the IOP's time budget. The SPU2 calls it when it raises one while
// catching up, so the driver answers before more is mixed.
void runPending();
uint64_t runWhileEeIdle();   // the EE is idle: run ready IOP work; returns EE cycles' worth of it
void onVblank(bool start);
std::string describeThreads();   // for deadlock reports

// ---- the EE side of SIF ----------------------------------------------------------------------
uint32_t sbusRead(uint32_t addr);   // 0x1000F200...
void sbusWrite(uint32_t addr, uint32_t value);
int32_t sifSetDma(uint32_t descriptors, int32_t count);
int32_t sifDmaStat(uint32_t id);
int32_t sifSetReg(uint32_t reg, uint32_t value);
uint32_t sifGetReg(uint32_t reg);
void sifSetDChain();   // the EE re-armed SIF0: deliver the next queued packet

// The IOP's view of the SIF registers (0x1D000000...), for module code.
uint32_t sifRegReadIop(uint32_t phys);
void sifRegWriteIop(uint32_t phys, uint32_t value);

// ---- IOP-side SIF, for the sifman / sifcmd libraries ---------------------------------------------
// Sends a command packet (already in IOP memory) to the EE, after copying
// `extraSize` bytes from IOP `extraSrc` to EE `extraDest`.
void sendCommandToEe(uint32_t cid, uint32_t packet, uint32_t packetSize, uint32_t extraSrc, uint32_t extraDest, uint32_t extraSize);
// IOP -> EE data transfer (SIF0).
void dmaToEe(uint32_t iopSrc, uint32_t eeDest, uint32_t size);
// An RPC request from the EE to a server module code registered was handled:
// sends the reply ($v0 of the server function is `result`).
void finishGuestRpc(uint32_t serverData, uint32_t result);
void addRpcQueue(uint32_t queue);
void removeRpcQueue(uint32_t queue);
// Guest handlers for SIF commands (sceSifAddCmdHandler).
void setCommandHandler(uint32_t cid, uint32_t handler, uint32_t arg, uint32_t gp);
uint32_t iopSreg(uint32_t index);
void setIopSreg(uint32_t index, uint32_t value);

// ---- modules ----------------------------------------------------------------------------------
// Loads and starts an IOP module; `done` gets the module id (or a negative
// error) and its start result, possibly after IOP code has run.
void loadModule(const std::string& path, const std::string& args, std::function<void(int id, int result)> done);
// The same, running the IOP until the module has started (EE-side HLE stubs).
int loadModuleSync(const std::string& path, const std::string& args);

// ---- host RPC services ------------------------------------------------------------------------
class RpcServer {
public:
    // Sends the reply data (truncated or zero-padded to the size the caller
    // asked for). A service may call it later than call() returns.
    using Reply = std::function<void(std::vector<uint8_t>)>;

    explicit RpcServer(std::string name) : name_(std::move(name)) {}
    virtual ~RpcServer() = default;
    // `data` is the send buffer, already in IOP memory.
    virtual void call(uint32_t fno, const uint8_t* data, uint32_t size, uint32_t recvSize, Reply reply) = 0;
    virtual uint32_t bufferSize() const { return 0x2000; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

void registerServer(uint32_t sid, std::unique_ptr<RpcServer> server);
bool hasServer(uint32_t sid);

// Built-in services (services.cpp).
std::unique_ptr<RpcServer> makeFileIoServer();
std::unique_ptr<RpcServer> makeLoadFileServer();
std::unique_ptr<RpcServer> makeIopHeapServer();
std::unique_ptr<RpcServer> makeNullServer(const std::string& name, uint32_t sid);
// Modules whose services the host provides (pad, memory card, CD/DVD, sound, ...).
bool isServiceModule(const std::string& moduleName);
// Enables the services a module provides. Returns false if it is not one.
// `version` is the module's IRX version (0x0301 for 3.01), 0 when unknown;
// services that report their module's version answer with it.
bool enableModuleServices(const std::string& moduleName, uint16_t version);

// Host path for a PS2 device path ("host0:foo", "cdrom0:\\DATA\\X.BIN;1", "mc0:/save").
std::string hostPathFor(const std::string& ps2Path);

} // namespace ps2::iop
