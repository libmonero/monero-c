using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Unit.Tests;

// a keys-only wallet needs no files and no daemon, so it checks the wallet functions through the
// native package: the creation, the enum and bool marshalling, UTF-8 strings and the errors.
// The vectors are the public, funds-free wallet of monero-python's tests/config/config.ini
public class MoneroWalletTests
{
    private const string Seed = "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex";
    private const string Address = "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2";
    private const string ViewKey = "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600";
    private const string SpendKey = "be7a2f71097f146bdf0fb5bb8edfe2240a9767e15adee74d95af1b5a64f29a0c";

    [Fact]
    public void CreatesAKeysOnlyWalletFromASeed()
    {
        IntPtr wallet = CreateFromSeed(0);
        try
        {
            Assert.Equal(Address, GetString(GetPrimaryAddressNative, wallet));
            Assert.Equal(ViewKey, GetString(GetPrivateViewKeyNative, wallet));
            Assert.Equal(SpendKey, GetString(GetPrivateSpendKeyNative, wallet));
            Assert.Equal(0, IsViewOnlyNative(wallet, out bool viewOnly));
            Assert.False(viewOnly);
        }
        finally
        {
            FreeWallet(wallet);
        }
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void ReportsTheNetworkOfTheWallet(int networkType)
    {
        IntPtr wallet = CreateFromSeed(networkType);
        try
        {
            Assert.Equal(0, GetNetworkTypeNative(wallet, out int actual));
            Assert.Equal(networkType, actual);
        }
        finally
        {
            FreeWallet(wallet);
        }
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(4)]
    [InlineData(9)]
    public void RejectsANetworkOutsideTheEnum(int networkType)
    {
        Assert.Equal(-1, KeysCreateFromSeedNative(networkType, Seed, null, null, out IntPtr wallet));
        Assert.Equal(IntPtr.Zero, wallet);
        Assert.Equal("unknown network type", LastError());
    }

    [Fact]
    public void SignsAndVerifiesAMessageWithNonAsciiText()
    {
        const string message = "h\u00e9llo w\u00f6rld \u2713";
        IntPtr wallet = CreateFromSeed(0);
        try
        {
            Assert.Equal(0, SignMessageNative(wallet, message, 0, 0, 0, out IntPtr signaturePointer));
            string signature = TakeString(signaturePointer);

            Assert.Contains("\"isGood\":true", Verify(wallet, message, signature));
            Assert.Contains("\"isGood\":false", Verify(wallet, "hello world", signature));
        }
        finally
        {
            FreeWallet(wallet);
        }
    }

    [Fact]
    public void RejectsANullWalletHandle()
    {
        Assert.Equal(-1, GetPrimaryAddressNative(IntPtr.Zero, out IntPtr address));
        Assert.Equal(IntPtr.Zero, address);
        Assert.Equal("wallet must not be null", LastError());
    }

    private static IntPtr CreateFromSeed(int networkType)
    {
        Assert.Equal(0, KeysCreateFromSeedNative(networkType, Seed, null, null, out IntPtr wallet));
        Assert.NotEqual(IntPtr.Zero, wallet);
        return wallet;
    }

    private static string Verify(IntPtr wallet, string message, string signature)
    {
        Assert.Equal(0, VerifyMessageNative(wallet, message, Address, signature, out IntPtr json));
        return TakeString(json);
    }

    private delegate int StringGetter(IntPtr wallet, out IntPtr value);

    private static string GetString(StringGetter getter, IntPtr wallet)
    {
        Assert.Equal(0, getter(wallet, out IntPtr value));
        return TakeString(value);
    }

    // a string that a function returns belongs to monero_c, and monero_utils_free() releases it
    private static string TakeString(IntPtr value)
    {
        Assert.NotEqual(IntPtr.Zero, value);
        try
        {
            return Marshal.PtrToStringUTF8(value) ?? throw new InvalidOperationException("Native string was null.");
        }
        finally
        {
            FreeNative(value);
        }
    }

    // the message of the last error of this thread, which monero_c owns and the caller doesn't free
    private static string LastError()
    {
        return Marshal.PtrToStringUTF8(LastErrorNative()) ?? "";
    }

    [DllImport("monero_c", EntryPoint = "monero_wallet_keys_create_from_seed", CallingConvention = CallingConvention.Cdecl)]
    private static extern int KeysCreateFromSeedNative(
        int networkType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string seed,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? seedOffset,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? language,
        out IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeWallet(IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_primary_address", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPrimaryAddressNative(IntPtr wallet, out IntPtr address);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_private_view_key", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPrivateViewKeyNative(IntPtr wallet, out IntPtr key);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_private_spend_key", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPrivateSpendKeyNative(IntPtr wallet, out IntPtr key);

    // the C bool is one byte, and a bool of the marshaller is four bytes unless it is told otherwise
    [DllImport("monero_c", EntryPoint = "monero_wallet_is_view_only", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsViewOnlyNative(IntPtr wallet, [MarshalAs(UnmanagedType.U1)] out bool viewOnly);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_network_type", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetNetworkTypeNative(IntPtr wallet, out int networkType);

    [DllImport("monero_c", EntryPoint = "monero_wallet_sign_message", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SignMessageNative(
        IntPtr wallet,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message,
        int signatureType,
        uint accountIndex,
        uint subaddressIndex,
        out IntPtr signature);

    [DllImport("monero_c", EntryPoint = "monero_wallet_verify_message", CallingConvention = CallingConvention.Cdecl)]
    private static extern int VerifyMessageNative(
        IntPtr wallet,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string address,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string signature,
        out IntPtr json);

    [DllImport("monero_c", EntryPoint = "monero_last_error", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr LastErrorNative();

    [DllImport("monero_c", EntryPoint = "monero_utils_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeNative(IntPtr value);
}
