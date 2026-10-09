using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Integration.Tests;

public class NativeLibraryTests
{
    [Fact]
    public void LoadsNativePackageAndCallsMoneroUtils()
    {
        Assert.Equal(16, GetRingSize());
    }

    [DllImport("monero_c", EntryPoint = "monero_utils_get_ring_size", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetRingSize();
}
