#include "cli.h"
#include "term.h"

#include <iostream>

int main(int argc, char **argv) {
    term_platform_init();
    return run_cli(argc, argv, std::cin, std::cout, std::cerr);
}
