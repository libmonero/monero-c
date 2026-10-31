/* Prints the ABI version of the library and compares it with the first argument. */
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
    return argc > 1 && strcmp(argv[1], version) != 0;
}
