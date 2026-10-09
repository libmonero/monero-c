using System.Runtime.InteropServices;
using Xunit;

namespace Monero.Native.Unit.Tests;

public class MoneroRpcConnectionTests
{
    private const string ConnectionJson = """
        {"uri":"http://127.0.0.1:1","username":"user","password":"pass","sslVerify":false,"timeoutMs":2000,"priority":2}
        """;

    [Fact]
    public void CreatesSerializesAndUpdatesCredentials()
    {
        IntPtr connection = CreateConnection(ConnectionJson);
        try
        {
            string serialized = Serialize(connection);
            Assert.Contains("\"uri\":\"http://127.0.0.1:1\"", serialized);
            Assert.Contains("\"username\":\"user\"", serialized);
            Assert.Contains("\"sslVerify\":false", serialized);
            Assert.Contains("\"timeoutMs\":2000", serialized);
            Assert.Contains("\"priority\":2", serialized);

            Assert.Equal(0, SetCredentials(connection, "other", "secret"));
            Assert.Contains("\"username\":\"other\"", Serialize(connection));
        }
        finally
        {
            FreeConnection(connection);
        }
    }

    [Fact]
    public void StoresAndRetrievesConnectionAttributes()
    {
        IntPtr connection = CreateConnection(ConnectionJson);
        try
        {
            Assert.Equal("", GetAttribute(connection, "label"));
            Assert.Equal(0, SetAttribute(connection, "label", "local node"));
            Assert.Equal("local node", GetAttribute(connection, "label"));
        }
        finally
        {
            FreeConnection(connection);
        }
    }

    [Theory]
    [InlineData("http://abcdefghij.onion:18081", true, false)]
    [InlineData("http://abcdefghij.b32.i2p:18081", false, true)]
    [InlineData("http://127.0.0.1:18081", false, false)]
    public void DetectsOnionAndI2pAddresses(string uri, bool expectedOnion, bool expectedI2p)
    {
        IntPtr connection = CreateConnection($"{{\"uri\":\"{uri}\"}}");
        try
        {
            Assert.Equal(expectedOnion, IsOnion(connection));
            Assert.Equal(expectedI2p, IsI2p(connection));
        }
        finally
        {
            FreeConnection(connection);
        }
    }

    [Fact]
    public void ConnectionStatusStartsUnset()
    {
        IntPtr connection = CreateConnection(ConnectionJson);
        try
        {
            Assert.Equal(-1, GetOnlineStatus(connection));
            Assert.Equal(-1, GetAuthenticatedStatus(connection));
            Assert.Equal(-1, GetConnectedStatus(connection));
        }
        finally
        {
            FreeConnection(connection);
        }
    }

    private static IntPtr CreateConnection(string json)
    {
        Assert.Equal(0, CreateConnectionNative(json, out IntPtr connection));
        Assert.NotEqual(IntPtr.Zero, connection);
        return connection;
    }

    private static string Serialize(IntPtr connection)
    {
        Assert.Equal(0, SerializeNative(connection, out IntPtr value));
        return TakeString(value);
    }

    private static string GetAttribute(IntPtr connection, string key)
    {
        Assert.Equal(0, GetAttributeNative(connection, key, out IntPtr value));
        return TakeString(value);
    }

    private static string TakeString(IntPtr value)
    {
        Assert.NotEqual(IntPtr.Zero, value);
        try
        {
            return Marshal.PtrToStringAnsi(value) ?? throw new InvalidOperationException("Native string was null.");
        }
        finally
        {
            FreeNative(value);
        }
    }

    private static bool IsOnion(IntPtr connection)
    {
        Assert.Equal(0, IsOnionNative(connection, out byte value));
        return value != 0;
    }

    private static bool IsI2p(IntPtr connection)
    {
        Assert.Equal(0, IsI2pNative(connection, out byte value));
        return value != 0;
    }

    private static int GetOnlineStatus(IntPtr connection)
    {
        Assert.Equal(0, IsOnlineNative(connection, out int status));
        return status;
    }

    private static int GetAuthenticatedStatus(IntPtr connection)
    {
        Assert.Equal(0, IsAuthenticatedNative(connection, out int status));
        return status;
    }

    private static int GetConnectedStatus(IntPtr connection)
    {
        Assert.Equal(0, IsConnectedNative(connection, out int status));
        return status;
    }

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_create", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int CreateConnectionNative(string json, out IntPtr connection);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeConnection(IntPtr connection);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_serialize", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SerializeNative(IntPtr connection, out IntPtr json);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_set_credentials", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int SetCredentials(IntPtr connection, string username, string password);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_set_attribute", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int SetAttribute(IntPtr connection, string key, string value);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_get_attribute", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int GetAttributeNative(IntPtr connection, string key, out IntPtr value);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_is_onion", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsOnionNative(IntPtr connection, out byte value);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_is_i2p", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsI2pNative(IntPtr connection, out byte value);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_is_online", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsOnlineNative(IntPtr connection, out int status);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_is_authenticated", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsAuthenticatedNative(IntPtr connection, out int status);

    [DllImport("monero_c", EntryPoint = "monero_rpc_connection_is_connected", CallingConvention = CallingConvention.Cdecl)]
    private static extern int IsConnectedNative(IntPtr connection, out int status);

    [DllImport("monero_c", EntryPoint = "monero_utils_free", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FreeNative(IntPtr value);
}
