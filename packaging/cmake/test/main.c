/* A program that uses the installed library: it prints the ABI version and compares it with the first argument, if there is one. */
#include <stdio.h>
#include <string.h>

#include "monero_c.h"

int main(int argc, char** argv) {
    uint32_t major = 0, minor = 0, patch = 0;
    char version[64];

    if (monero_utils_get_abi_version(&major, &minor, &patch) != MONERO_OK) {
        fprintf(stderr, "monero_utils_get_abi_version() failed\n");
        return 1;
    }
    snprintf(version, sizeof(version), "%u.%u.%u", (unsigned)major, (unsigned)minor, (unsigned)patch);
    printf("%s\n", version);
    if (argc > 1 && strcmp(argv[1], version) != 0) {
        fprintf(stderr, "the ABI version is %s, expected %s\n", version, argv[1]);
        return 1;
    }
    return 0;
}
