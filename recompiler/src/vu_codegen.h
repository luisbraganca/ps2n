// VU micro programs to C++.
//
// A game uploads micro code to a vector unit at run time; the runtime records
// the micro memory it saw and where programs started in it (VuImage), and
// this turns each image into one C++ function the runtime runs instead of
// interpreting, wherever micro memory holds a program's words: games upload
// a program wherever there is room, and the generated code adds where it
// sits to every address it reads or writes (ps2/vu_recomp.h).
//
// The generated code keeps the interpreter's pipeline exactly, but works its
// timing out here: it walks each program with the same timing the
// interpreter steps through (ps2n/vu_micro.h), and writes down where flags,
// Q and P land, and how many cycles each block takes, instead of keeping the
// results in flight at run time. A block is code from one address with one
// state of the pipeline; a loop settles into one or two. What the generator
// does not follow (a branch or E bit in a delay slot, a jump to an address
// it did not expect, too many states at one address) hands the program back
// to the interpreter, with the pipeline as it would be.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2n {

// A VU's micro memory as a game left it, and where programs started in it.
struct VuImage {
    int unit = 1;                    // 0 or 1
    std::vector<uint8_t> micro;      // the whole micro memory: 4 KB (VU0) or 16 KB (VU1)
    std::vector<uint32_t> entries;   // byte addresses where programs started
    // Where recompiled code handed programs over: mostly where a JR or JALR
    // went that the generator did not expect, which it then expects.
    std::vector<uint32_t> targets;
};

struct VuCodegenStats {
    size_t images = 0, entries = 0, blocks = 0, pairs = 0, handovers = 0;
    size_t repeats = 0;   // entries of programs another image already has
};

// C++ for the images: a function per image, entered at any of its entries,
// and the table `kVuPrograms` (with `kVuProgramCount`) in namespace `ns`,
// whose rows carry each entry's key (ps2n::vu::entryKey). A program two
// images hold (ps2n::vu::programKey) is compiled once.
std::string generateVuCode(const std::vector<VuImage>& images, const std::string& ns, VuCodegenStats* stats = nullptr);

// The images of a VU code file, as the runtime writes it (ps2n_missing.vu.toml):
//   [[image]]
//   unit = 1
//   entries = [0x0000, 0x09e0]
//   code = """
//   0000 <pair> <pair> <pair> <pair>     (hex; a pair is lower | upper << 32)
//   """
// Lines of micro memory the file leaves out are zero. Throws ps2n::Error.
std::vector<VuImage> loadVuCode(const std::filesystem::path& file);

} // namespace ps2n
