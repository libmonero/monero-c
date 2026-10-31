// Prints the ABI version of the library and compares it with the first argument.
import Darwin
import MoneroC

var major: UInt32 = 0
var minor: UInt32 = 0
var patch: UInt32 = 0

guard monero_utils_get_abi_version(&major, &minor, &patch) == MONERO_OK else {
    print("monero_utils_get_abi_version() failed")
    exit(1)
}
let version = "\(major).\(minor).\(patch)"
print(version)
if CommandLine.arguments.count > 1 && CommandLine.arguments[1] != version {
    exit(1)
}
