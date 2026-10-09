using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Unit.Tests;

public class MoneroUtilsTests
{
    private const string MainnetAddress = "42U9v3qs5CjZEePHBZHwuSckQXebuZu299NSmVEmQ41YJZQhKcPyujyMSzpDH4VMMVSBo3U3b54JaNvQLwAjqDhKS3rvM3L";
    private const string TestnetAddress = "9tUBnNCkC3UKGygHCwYvAB1FscpjUuq5e9MYJd2rXuiiTjjfVeSVjnbSG5VTnJgBgy9Y7GTLfxpZNMUwNZjGfdFr1z79eV1";
    private const string ShortPaymentId = "87fdf837b5e6a390";
    private const string LongPaymentId = "87fdf837b5e6a390ef35647e9842991c8434d5452ad1b0ab304e0fa65b9c9e14";

    [Fact]
    public void ValidatesAddressesForTheirNetwork()
    {
        Assert.True(IsValidAddress(MainnetAddress, 0));
        Assert.True(IsValidAddress(TestnetAddress, 1));
        Assert.False(IsValidAddress(MainnetAddress, 1));
        Assert.False(IsValidAddress("", 0));
    }

    [Theory]
    [InlineData(ShortPaymentId, true)]
    [InlineData(LongPaymentId, true)]
    [InlineData("not-a-payment-id", false)]
    [InlineData("", false)]
    public void ValidatesPaymentIds(string paymentId, bool expected)
    {
        Assert.Equal(expected, IsValidPaymentId(paymentId));
    }

    [Fact]
    public void ValidatesLongAndShortPaymentIds()
    {
        Assert.True(IsValidPaymentIdLong(LongPaymentId));
        Assert.False(IsValidPaymentIdLong(ShortPaymentId));
        Assert.True(IsValidPaymentIdShort(ShortPaymentId));
        Assert.False(IsValidPaymentIdShort(LongPaymentId));
    }

    [Fact]
    public void ParsesLongAndShortPaymentIdsIntoFixedSizeBuffers()
    {
        var longIdBytes = new byte[32];
        var shortIdBytes = new byte[8];

        Assert.True(ParsePaymentIdLong(LongPaymentId, longIdBytes));
        Assert.False(ParsePaymentIdLong(ShortPaymentId, longIdBytes));
        Assert.True(ParsePaymentIdShort(ShortPaymentId, shortIdBytes));
        Assert.False(ParsePaymentIdShort(LongPaymentId, shortIdBytes));
        Assert.Equal(Convert.FromHexString(LongPaymentId), longIdBytes);
        Assert.Equal(Convert.FromHexString(ShortPaymentId), shortIdBytes);
    }

    [Theory]
    [InlineData(1.0, 1_000_000_000_000)]
    [InlineData(0.25, 250_000_000_000)]
    public void ConvertsXmrToAtomicUnits(double amount, ulong expected)
    {
        Assert.Equal(0, XmrToAtomicUnits(amount, out ulong actual));
        Assert.Equal(expected, actual);
    }

    [Fact]
    public void ConvertsAtomicUnitsToXmr()
    {
        Assert.Equal(0.25, AtomicUnitsToXmr(250_000_000_000));
    }

    [Fact]
    public void RejectsNegativeXmrAmounts()
    {
        Assert.Equal(-1, XmrToAtomicUnits(-1.0, out _));
    }

    [Fact]
    public void ReturnsNetworkRingSize()
    {
        Assert.Equal(16, GetRingSize());
    }

    // the version of the C ABI that this test was written for. A binding checks it after loading the library
    private const uint ExpectedAbiMajor = 0;
    private const uint ExpectedAbiMinor = 1;
    private const uint ExpectedAbiPatch = 0;

    [Fact]
    public void ReturnsTheAbiVersionThisBindingWasWrittenFor()
    {
        Assert.Equal(0, GetAbiVersion(out var major, out var minor, out var patch));
        Assert.Equal(ExpectedAbiMajor, major);
        Assert.Equal(ExpectedAbiMinor, minor);
        Assert.Equal(ExpectedAbiPatch, patch);
    }

    [DllImport("monero_c", EntryPoint = "monero_utils_get_abi_version", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetAbiVersion(out uint major, out uint minor, out uint patch);

    [DllImport("monero_c", EntryPoint = "monero_utils_is_valid_address", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool IsValidAddress(string address, int networkType);

    [DllImport("monero_c", EntryPoint = "monero_utils_is_valid_payment_id", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool IsValidPaymentId(string paymentId);

    [DllImport("monero_c", EntryPoint = "monero_utils_is_valid_payment_id_long", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool IsValidPaymentIdLong(string paymentId);

    [DllImport("monero_c", EntryPoint = "monero_utils_is_valid_payment_id_short", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool IsValidPaymentIdShort(string paymentId);

    [DllImport("monero_c", EntryPoint = "monero_utils_parse_payment_id_long", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool ParsePaymentIdLong(string paymentId, [Out] byte[] output);

    [DllImport("monero_c", EntryPoint = "monero_utils_parse_payment_id_short", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool ParsePaymentIdShort(string paymentId, [Out] byte[] output);

    [DllImport("monero_c", EntryPoint = "monero_utils_xmr_to_atomic_units", CallingConvention = CallingConvention.Cdecl)]
    private static extern int XmrToAtomicUnits(double amount, out ulong output);

    [DllImport("monero_c", EntryPoint = "monero_utils_atomic_units_to_xmr", CallingConvention = CallingConvention.Cdecl)]
    private static extern double AtomicUnitsToXmr(ulong amount);

    [DllImport("monero_c", EntryPoint = "monero_utils_get_ring_size", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetRingSize();
}
