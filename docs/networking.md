# Limits, TLS and proxy

## Limits

A JSON argument can't nest deeper than 64 levels, and a mnemonic can't be longer than 4096 bytes. `monero_daemon_get_blocks_by_range()` takes at most 100000 blocks per call, and `monero_daemon_get_blocks_by_range_chunked()` has no limit. A call over a limit fails with `MONERO_ERROR` before any parser or request runs.

## TLS

A connection to an `https` server checks the certificate against the CA certificates of the system, unless its `sslVerify` is false. The library carries its own OpenSSL, which looks for the certificates in the directory of the machine that built it, so on Linux and macOS the library sets `SSL_CERT_FILE` and `SSL_CERT_DIR` when it is loaded, to the bundle and the directory of the system (the Debian, Fedora, openSUSE, Alpine and macOS ones, and Android's directory). It leaves the environment alone if you set one of the two, or if OpenSSL finds the certificates by itself, as it does on Debian and Ubuntu. Windows reads the root store of the system, and nothing is set there.

## Proxy and Tor

A connection can go through a SOCKS proxy, such as the one of a Tor node, which has to run already and listens on `127.0.0.1:9050` by default. The proxy is the `proxy_uri` argument of `monero_daemon_connect()` and `monero_wallet_set_daemon_connection()`, and the `proxyUri` of a connection JSON (`monero_rpc_connection_create()`, `monero_wallet_rpc_set_daemon_connection()`) and of the `server` in the config of `monero_wallet_create()`. Its form is `[scheme://]ip:port`, where the scheme is `socks4a` (the default), `socks4` or `socks5`, and the host is an IP address, not a name such as `localhost`. Use the default or `socks5`, so that the proxy resolves the host name of an `.onion` address: `socks4` resolves it on your machine and the connection fails. The proxy is not checked when it is set, so a mistake shows as a network error at the first call. `monero_rpc_connection_is_onion()` tells if the URI of a connection is an onion address.

```c
// a daemon on Tor, through the proxy of the local node. Tor is slower, so wait up to a minute
monero_daemon* onion_daemon = NULL;
monero_daemon_connect("http://kyaklhqp4yyza4fmtbvs6z4lrzr5cljxwa7o2d42sfelfhczsmbwzfad.onion:18081", "superuser", "abctesting123",
                      "127.0.0.1:9050", "", 60000, &onion_daemon);

// a full wallet through the same proxy. NULL would trust only a daemon on this machine, so say that this one is trusted
bool trusted = true;
monero_wallet_set_daemon_connection(wallet_full, "http://kyaklhqp4yyza4fmtbvs6z4lrzr5cljxwa7o2d42sfelfhczsmbwzfad.onion:18081",
                                    "superuser", "abctesting123", "127.0.0.1:9050", &trusted, true);

// monero-wallet-rpc reaches its daemon through the proxy of the connection
monero_wallet_rpc_set_daemon_connection(wallet_rpc,
    "{\"uri\":\"http://kyaklhqp4yyza4fmtbvs6z4lrzr5cljxwa7o2d42sfelfhczsmbwzfad.onion:18081\",\"username\":\"superuser\",\"password\":\"abctesting123\",\"proxyUri\":\"127.0.0.1:9050\"}",
    true, NULL);
```
