using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Unit.Tests;

public class MoneroDaemonTests
{
    [Fact]
    public void DaemonRetainsItsRpcConnectionAfterTheConnectionHandleIsFreed()
    {
        Assert.Equal(0, CreateConnection(ConnectionJson, out IntPtr connection));
        Assert.NotEqual(IntPtr.Zero, connection);

        try
        {
            Assert.Equal(0, ConnectWith(connection, out IntPtr daemon));
            Assert.NotEqual(IntPtr.Zero, daemon);

            FreeConnection(connection);
            connection = IntPtr.Zero;

            try
            {
                Assert.Equal(0, SetPollPeriod(daemon, 1000));
                Assert.Equal(0, GetRpcConnection(daemon, out IntPtr json));
                string serialized = TakeString(json);
                Assert.Contains("\"uri\":\"http://127.0.0.1:1\"", serialized);
                Assert.Contains("\"sslVerify\":false", serialized);
                Assert.Contains("\"isOnline\":false", serialized);
            }
            finally
            {
                FreeDaemon(daemon);
            }
        }
        finally
        {
            FreeConnection(connection);
        }
    }

    private const string ConnectionJson =
        "{\"uri\":\"http://127.0.0.1:1\",\"username\":\"user\",\"password\":\"pass\",\"sslVerify\":false,\"timeoutMs\":2000}";

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

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_create", CallingConvention = CallingConvention.Cdecl)]
    private static extern int CreateConnection([MarshalAs(UnmanagedType.LPUTF8Str)] string json, out IntPtr connection);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeConnection(IntPtr connection);

    [DllImport("monero_c", EntryPoint = "monero_daemon_connect_with", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ConnectWith(IntPtr connection, out IntPtr daemon);

    [DllImport("monero_c", EntryPoint = "monero_daemon_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeDaemon(IntPtr daemon);

    [DllImport("monero_c", EntryPoint = "monero_daemon_get_rpc_connection", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetRpcConnection(IntPtr daemon, out IntPtr json);

    [DllImport("monero_c", EntryPoint = "monero_daemon_set_poll_period", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SetPollPeriod(IntPtr daemon, ulong periodMs);

    [DllImport("monero_c", EntryPoint = "monero_utils_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeNative(IntPtr value);
}
