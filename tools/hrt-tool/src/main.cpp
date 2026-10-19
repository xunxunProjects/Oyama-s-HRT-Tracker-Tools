#include "version.h"

#include <iostream>
#include <string>

namespace {

int run_cli(int argc, char **argv, std::istream &in, std::ostream &out, std::ostream &err) {
    (void)in;
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "version")) {
        out << format_version_line();
        return 0;
    }
    err << "hrt-tool: unknown command; try 'hrt-tool version'\n";
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    return run_cli(argc, argv, std::cin, std::cout, std::cerr);
}
