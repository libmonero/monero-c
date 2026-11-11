# Results with context

The results come with the parents that their models have, so a transfer is in its tx and a tx in its block, as in monero-java. The shapes are in [docs/json-shapes.md](https://github.com/libmonero/monero-c/blob/main/docs/json-shapes.md), and the docs of each function in `src/wallet/monero_c_wallet.h` and `src/daemon/monero_c_daemon.h` say which one it returns.

- `monero_wallet_get_txs()`, `monero_wallet_get_transfers()` and `monero_wallet_get_outputs()` return `{"blocks":[...]}`, for a full wallet and an RPC wallet alike. A block has its `height` and its `timestamp`, and its `txs`, and a tx has the `incomingTransfers`, the `outgoingTransfer` or the `outputs` of the result. The txs that no block holds yet, in the pool or not relayed, are in one block that has no `height`.
- `monero_daemon_get_tx()`, `monero_daemon_get_txs()` and `monero_daemon_get_tx_pool()` return the same, with the txs of one block in one block. `monero_daemon_get_outputs()` does too, with each output in the `outputs` of its tx (the hash and the `isLocked` flag) and the txs in the block of their height. The outputs come in the order of the blocks, so match them by `amount` and `index`.
- `monero_wallet_create_tx()`, `monero_wallet_create_txs()`, `monero_wallet_sweep_output()`, `monero_wallet_sweep_unlocked()`, `monero_wallet_sweep_dust()`, `monero_wallet_sign_txs()` and `monero_wallet_describe_tx_set()` return `{"txSets":[...]}`. A tx set has its `txs` and the hex that signs or relays them: the `unsignedTxHex` of a view-only wallet, which a wallet that has the keys signs with `monero_wallet_sign_txs()`. What they return goes as it is into `monero_wallet_relay_tx_json()`, `monero_wallet_relay_txs_json()` and `monero_wallet_describe_tx_set()`.
- `on_output_received` and `on_output_spent` get the output in its tx and its block, as `{"blocks":[...]}`, with the hash, the unlock time, the extra, the miner flag and the locked flag of the tx and the height of the block.
- The query of the transfers or the outputs can hold the query of their txs in `txQuery`, such as `{"isIncoming":true,"txQuery":{"isConfirmed":true,"minHeight":50}}`.
- The txs come in the order of the blocks, so a query by hashes doesn't get them back in the order of its hashes: sort them by the hashes that you asked for.

```json
{"blocks":[
  {"height":2,"timestamp":1791675257,
   "txs":[{"hash":"62ded941...","fee":0,"numConfirmations":99,"unlockTime":62,"isMinerTx":true,"isLocked":false,
           "incomingTransfers":[{"amount":35184271425600,"accountIndex":0,"subaddressIndex":0}]}]},
  {"txs":[{"hash":"ffa2a44c...","inTxPool":true,"outgoingTransfer":{"amount":1000000000,"accountIndex":0}}]}
]}
```

`monero_wallet_get_accounts()`, `monero_wallet_get_subaddresses()` and `monero_wallet_get_address_book_entries()` return `{"accounts":[...]}`, `{"subaddresses":[...]}` and `{"entries":[...]}`, as the bridge of monero-java writes them. The other lists of values that have no parent, such as the key images, the seed languages, the notes and the hashes, are plain JSON arrays.
