#include <iostream>
#include <fstream>
#include "pchtxt/pchtxt.hpp"

int main(int argc, char **argv) {
    /* Check arguments */
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pchtxt file>" << std::endl;
        return 1;
    }

    /* Open stream. */
    auto pchtxt = std::fstream(argv[1]);
    if (!pchtxt.is_open()) {
        std::cerr << "Could not open file " << argv[1] << std::endl;
        return 1;
    }

    /* Parse pchtxt. */
    auto out = pchtxt::parsePchtxt(pchtxt, std::cout);

    /* Create ips file. */
    auto file = std::ofstream(out.collections.front().buildId + ".ips");

    /* Write ips file. */
    pchtxt::PatchCollection pc;
    pc.buildId = out.collections.front().buildId;
    pc.targetType = pchtxt::TargetType::NSO;
    pc.patches = out.collections.front().patches;
    pchtxt::writeIps(pc, file);

    return 0;
}