// Memory cards: the MCSERV / XMCSERV RPC service (sid 0x80000400) that the
// EE's libmc calls, over host directories (--memcards DIR: DIR/mc0 is the card
// in port 1, DIR/mc1 the one in port 2).
//
// The card in the first port is always inserted (its directory is created on
// the first write); the second port has a card when its directory exists.
// Cards are formatted 8 MB PS2 cards; files keep no attributes or timestamps
// of their own beyond what the host file system records.
#pragma once

#include <memory>

namespace ps2::iop {

class RpcServer;

namespace memcard {

std::unique_ptr<RpcServer> makeServer();
void reset();   // closes files; the next GetInfo reports the cards as newly inserted

} // namespace memcard
} // namespace ps2::iop
