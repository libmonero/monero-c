using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Unit.Tests;

// the shapes of the C API that a binding has to marshal: buffers, arrays of strings, optional
// pointers, callbacks, UTF-8 paths and the error of each thread. None needs a daemon
public class MoneroMarshallingTests
{
    private const string Seed = "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex";
    private const string Address = "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2";
    private const string Unreachable = "http://127.0.0.1:1";

    [Fact]
    public void ConvertsJsonToBinaryAndBack()
    {
        Assert.Equal(0, JsonToBinaryNative("{\"heights\":[1,2,3]}", out IntPtr data, out UIntPtr length));
        try
        {
            Assert.NotEqual(IntPtr.Zero, data);
            Assert.NotEqual(UIntPtr.Zero, length);
            Assert.Equal(0, BinaryToJsonNative(data, length, out IntPtr json));
            Assert.Contains("\"heights\"", TakeString(json));
        }
        finally
        {
            FreeNative(data);
        }
    }

    [Fact]
    public void PassesArraysOfUtf8Strings()
    {
        string directory = NewDirectory();
        try
        {
            Assert.Equal(0, CreateFromSeedNative(Path.Combine(directory, "wallet"), "password", 0, Seed, null, 0, null, out IntPtr wallet));
            try
            {
                string[] hashes = { new string('1', 64), new string('2', 64) };
                string[] notes = { "café ☕", "\U0001F980 日本" };
                IntPtr[] hashPointers = hashes.Select(Marshal.StringToCoTaskMemUTF8).ToArray();
                IntPtr[] notePointers = notes.Select(Marshal.StringToCoTaskMemUTF8).ToArray();
                try
                {
                    Assert.Equal(0, SetTxNotesNative(wallet, hashPointers, notePointers, (UIntPtr) 2));
                    Assert.Equal(0, GetTxNotesNative(wallet, hashPointers, (UIntPtr) 2, out IntPtr json));
                    Assert.Equal("[\"café ☕\",\"\U0001F980 日本\"]", TakeString(json));

                    // a zero count is valid with a NULL array, and a nonzero one names the argument
                    Assert.Equal(0, GetTxNotesNative(wallet, null, UIntPtr.Zero, out json));
                    Assert.Equal("[]", TakeString(json));
                    Assert.Equal(-1, GetTxNotesNative(wallet, null, (UIntPtr) 1, out _));
                    Assert.Equal("tx_hashes must not be null", LastError());
                }
                finally
                {
                    foreach (IntPtr pointer in hashPointers.Concat(notePointers)) Marshal.FreeCoTaskMem(pointer);
                }
            }
            finally
            {
                FreeWallet(wallet);
            }
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // the files of a wallet take the UTF-8 path of the caller
    [Fact]
    public void CreatesAndReopensAWalletAtANonAsciiPath()
    {
        // not checked on Windows yet, see the item about the paths in TODO.md
        if (OperatingSystem.IsWindows()) return;

        string directory = NewDirectory();
        try
        {
            string path = Path.Combine(directory, "wället-é-日本-\U0001F980");
            Assert.Equal(0, CreateFromSeedNative(path, "päss", 0, Seed, null, 0, null, out IntPtr wallet));
            try
            {
                Assert.True(File.Exists(path));
                Assert.True(File.Exists(path + ".keys"));
                Assert.Equal(0, GetPathNative(wallet, out IntPtr pathPointer));
                Assert.Equal(Path.GetFileName(path), Path.GetFileName(TakeString(pathPointer)));
                Assert.Equal(0, CloseWalletNative(wallet, true));
            }
            finally
            {
                FreeWallet(wallet);
            }

            Assert.Equal(0, OpenNative(path, "päss", 0, out IntPtr reopened));
            try
            {
                Assert.Equal(0, GetPrimaryAddressNative(reopened, out IntPtr address));
                Assert.Equal(Address, TakeString(address));
            }
            finally
            {
                FreeWallet(reopened);
            }
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // a listener is a struct of function pointers, and the delegates behind them have to stay referenced
    [Fact]
    public void KeepsTheDelegatesOfAListenerAlive()
    {
        Assert.Equal(0, ConnectNative(Unreachable, null, null, null, 500, out IntPtr daemon));
        try
        {
            OnBlockHeader callback = (userData, headerJson) => { };
            var callbacks = new DaemonListenerCallbacks { OnBlockHeader = Marshal.GetFunctionPointerForDelegate(callback) };
            Assert.Equal(0, ListenerCreateNative(ref callbacks, out IntPtr listener));
            try
            {
                Assert.Equal(0, AddListenerNative(daemon, listener));
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();

                // an array and its count come back together
                Assert.Equal(0, GetListenersNative(daemon, out IntPtr listeners, out UIntPtr count));
                Assert.Equal((UIntPtr) 1, count);
                Assert.Equal(listener, Marshal.ReadIntPtr(listeners));
                FreeNative(listeners);

                Assert.Equal(0, RemoveListenerNative(daemon, listener));
            }
            finally
            {
                ListenerFree(listener);
            }

            GC.KeepAlive(callback);
        }
        finally
        {
            FreeDaemon(daemon);
        }
    }

    // a null pointer is none: a ref parameter takes Unsafe.NullRef
    [Fact]
    public void PassesNullForAnOptionalPointer()
    {
        Assert.Equal(0, ConnectNative(Unreachable, null, null, null, 500, out IntPtr daemon));
        try
        {
            ulong end = 5;
            Assert.Equal(-1, GetBlocksByRangeNative(daemon, ref Unsafe.NullRef<ulong>(), ref end, out IntPtr json));
            Assert.Equal(IntPtr.Zero, json);
            Assert.DoesNotContain("range is longer", LastError());
        }
        finally
        {
            FreeDaemon(daemon);
        }
    }

    [Fact]
    public void ResetsTheOutputsOfAFailedCall()
    {
        IntPtr text = new IntPtr(1);
        Assert.Equal(-1, GetPrimaryAddressRef(IntPtr.Zero, ref text));
        Assert.Equal(IntPtr.Zero, text);

        IntPtr listeners = new IntPtr(1);
        UIntPtr count = (UIntPtr) 7;
        Assert.Equal(-1, GetListenersRef(IntPtr.Zero, ref listeners, ref count));
        Assert.Equal(IntPtr.Zero, listeners);
        Assert.Equal(UIntPtr.Zero, count);
    }

    [Fact]
    public void ReportsTheLimitsOfTheInputs()
    {
        string tooDeep = new string('[', 65) + "1" + new string(']', 65);
        Assert.Equal(-1, GetPaymentUriNative(tooDeep, 0, out _));
        Assert.Equal("JSON is nested deeper than 64 levels", LastError());

        Assert.Equal(-1, ValidateMnemonicNative(new string('a', 5000), null));
        Assert.Equal("mnemonic is longer than 4096 bytes", LastError());

        Assert.Equal(0, ConnectNative(Unreachable, null, null, null, 500, out IntPtr daemon));
        try
        {
            ulong start = 0;
            ulong end = ulong.MaxValue;
            Assert.Equal(-1, GetBlocksByRangeNative(daemon, ref start, ref end, out _));
            Assert.StartsWith("range is longer than 100000 blocks", LastError());
        }
        finally
        {
            FreeDaemon(daemon);
        }
    }

    // the error belongs to the thread that made the failing call
    [Fact]
    public void KeepsTheLastErrorPerThread()
    {
        Assert.Equal(-1, GetPrimaryAddressNative(IntPtr.Zero, out _));
        Assert.Equal("wallet must not be null", LastError());

        string? other = null;
        var thread = new Thread(() => other = LastError());
        thread.Start();
        thread.Join();

        Assert.Equal("", other);
        Assert.Equal("wallet must not be null", LastError());
    }

    [Fact]
    public void CreatesAndFreesHandlesInALoop()
    {
        for (int i = 0; i < 100; i++)
        {
            Assert.Equal(0, KeysCreateRandomNative(0, null, out IntPtr wallet));
            FreeWallet(wallet);
        }

        for (int i = 0; i < 500; i++)
        {
            Assert.Equal(0, ConnectNative(Unreachable, null, null, null, 500, out IntPtr daemon));
            FreeDaemon(daemon);
        }
    }

    private static string NewDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), "monero-native-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        return directory;
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

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void OnBlockHeader(IntPtr userData, IntPtr headerJson);

    [StructLayout(LayoutKind.Sequential)]
    private record struct DaemonListenerCallbacks
    {
        public IntPtr UserData;
        public IntPtr OnBlockHeader;
    }

    [DllImport("monero_c", EntryPoint = "monero_last_error", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr LastErrorNative();

    [DllImport("monero_c", EntryPoint = "monero_utils_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeNative(IntPtr value);

    [DllImport("monero_c", EntryPoint = "monero_utils_json_to_binary", CallingConvention = CallingConvention.Cdecl)]
    private static extern int JsonToBinaryNative([MarshalAs(UnmanagedType.LPUTF8Str)] string json, out IntPtr data, out UIntPtr length);

    [DllImport("monero_c", EntryPoint = "monero_utils_binary_to_json", CallingConvention = CallingConvention.Cdecl)]
    private static extern int BinaryToJsonNative(IntPtr data, UIntPtr length, out IntPtr json);

    [DllImport("monero_c", EntryPoint = "monero_utils_get_payment_uri", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPaymentUriNative([MarshalAs(UnmanagedType.LPUTF8Str)] string txConfigJson, int networkType, out IntPtr uri);

    [DllImport("monero_c", EntryPoint = "monero_utils_validate_mnemonic", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ValidateMnemonicNative([MarshalAs(UnmanagedType.LPUTF8Str)] string mnemonic, [MarshalAs(UnmanagedType.LPUTF8Str)] string? language);

    [DllImport("monero_c", EntryPoint = "monero_wallet_create_from_seed", CallingConvention = CallingConvention.Cdecl)]
    private static extern int CreateFromSeedNative(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? password,
        int networkType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string seed,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? seedOffset,
        ulong restoreHeight,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? language,
        out IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_open", CallingConvention = CallingConvention.Cdecl)]
    private static extern int OpenNative(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? password,
        int networkType,
        out IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_keys_create_random", CallingConvention = CallingConvention.Cdecl)]
    private static extern int KeysCreateRandomNative(int networkType, [MarshalAs(UnmanagedType.LPUTF8Str)] string? language, out IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_close", CallingConvention = CallingConvention.Cdecl)]
    private static extern int CloseWalletNative(IntPtr wallet, [MarshalAs(UnmanagedType.U1)] bool save);

    [DllImport("monero_c", EntryPoint = "monero_wallet_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeWallet(IntPtr wallet);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_path", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPathNative(IntPtr wallet, out IntPtr path);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_primary_address", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPrimaryAddressNative(IntPtr wallet, out IntPtr address);

    // the same function with a ref parameter, to start from a value that the call has to reset
    [DllImport("monero_c", EntryPoint = "monero_wallet_get_primary_address", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetPrimaryAddressRef(IntPtr wallet, ref IntPtr address);

    [DllImport("monero_c", EntryPoint = "monero_wallet_set_tx_notes", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SetTxNotesNative(IntPtr wallet, IntPtr[] txHashes, IntPtr[] notes, UIntPtr count);

    [DllImport("monero_c", EntryPoint = "monero_wallet_get_tx_notes", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetTxNotesNative(IntPtr wallet, IntPtr[]? txHashes, UIntPtr count, out IntPtr json);

    [DllImport("monero_c", EntryPoint = "monero_daemon_connect", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ConnectNative(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string uri,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? username,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? password,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? proxyUri,
        uint timeoutMs,
        out IntPtr daemon);

    [DllImport("monero_c", EntryPoint = "monero_daemon_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeDaemon(IntPtr daemon);

    [DllImport("monero_c", EntryPoint = "monero_daemon_listener_create", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ListenerCreateNative(ref DaemonListenerCallbacks callbacks, out IntPtr listener);

    [DllImport("monero_c", EntryPoint = "monero_daemon_listener_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ListenerFree(IntPtr listener);

    [DllImport("monero_c", EntryPoint = "monero_daemon_add_listener", CallingConvention = CallingConvention.Cdecl)]
    private static extern int AddListenerNative(IntPtr daemon, IntPtr listener);

    [DllImport("monero_c", EntryPoint = "monero_daemon_remove_listener", CallingConvention = CallingConvention.Cdecl)]
    private static extern int RemoveListenerNative(IntPtr daemon, IntPtr listener);

    [DllImport("monero_c", EntryPoint = "monero_daemon_get_listeners", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetListenersNative(IntPtr daemon, out IntPtr listeners, out UIntPtr count);

    [DllImport("monero_c", EntryPoint = "monero_daemon_get_listeners", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetListenersRef(IntPtr daemon, ref IntPtr listeners, ref UIntPtr count);

    [DllImport("monero_c", EntryPoint = "monero_daemon_get_blocks_by_range", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetBlocksByRangeNative(IntPtr daemon, ref ulong startHeight, ref ulong endHeight, out IntPtr json);
}
