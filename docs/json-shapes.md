# JSON shapes of the results

Status: in progress. Steps 1 to 6 of [Steps](#steps) are done: `monero_wallet_get_txs()`, `monero_wallet_get_transfers()` and `monero_wallet_get_outputs()` return `{"blocks":[...]}`, the queries of transfers and outputs take a `txQuery`, the calls that make txs return `{"txSets":[...]}`, the listeners get the output in its tx and its block, and the daemon returns its txs in blocks. The rest is a proposal. Nothing has been released, so the JSON of the ABI can change before the first release. The open decisions are in [Decisions](#decisions). It is the item "keep the block and the tx in the JSON of the wallet and the daemon" of [`TODO.md`](../TODO.md).

## The problem

monero-cpp models point to their parents: a tx to its block and to its tx set, a transfer or an output to its tx. `serialize()` leaves the pointers out, so monero-c serializes each result alone and the JSON loses the context that the models have. Measured on a regtest node, with the full wallet and the daemon:

| Call | What is missing |
| --- | --- |
| `monero_wallet_get_txs()` | the height and the timestamp of the block. The wallet doesn't have the hash of the block either: wallet2 doesn't keep it |
| `monero_wallet_get_transfers()`, `monero_wallet_get_outputs()` | the hash of the tx and everything else of it (height, fee, unlock time, confirmations) |
| `on_output_received`, `on_output_spent` | the payload is `{"amount","accountIndex","subaddressIndex"}`. The JNI listener of monero-java gets the height, the hash, the version, the unlock time and the locked flag |
| `monero_daemon_get_tx()`, `monero_daemon_get_txs()`, `monero_daemon_get_tx_pool()` | the height and the timestamp. monero-cpp reads them into the block of the tx, and the C layer doesn't serialize it |
| `monero_wallet_create_tx()`, `monero_wallet_sweep_output()`, `monero_wallet_sweep_unlocked()`, `monero_wallet_sweep_dust()` | the tx set, so a view-only wallet gives no `unsignedTxHex` and an offline signer can't sign what it makes. Only `monero_wallet_create_txs()` returns the set |
| the queries | `monero_wallet_get_outputs()` ignores `txQuery` (`{"txQuery":{"isLocked":true}}` and `false` both return every output), and `monero_wallet_get_transfers()` fails with "txQuery deserialization not implemented", so the filters of the tx only work through `monero_wallet_get_txs()` |

## The protocol of monero-java

The bridge of monero-java, which monero-ts shares, serializes the results with their context:

- `getTxsJni()`, `getTransfersJni()` and `getOutputsJni()` return `{"blocks":[...]}`, built with `monero_utils::get_blocks_from_txs()`, `get_blocks_from_transfers()` and `get_blocks_from_outputs()`. A transfer or an output is inside its tx, and the tx inside its block. The txs that aren't in a block yet (the pool) are in one block that has no height.
- `createTxsJni()` returns the tx set, `sweepUnlockedJni()` returns `{"txSets":[...]}`.
- The queries come as JSON rooted at a block, `{"txs":[{...tx query...,"transferQuery":{...}}]}`, and are read with `deserialize_from_block()`, so the query of a transfer can hold the query of its tx.
- The listener gets the height, the hash and the unlock time of the tx of the output, because it reads `output.m_tx`.
- Java sorts the txs by the hashes of the query, since the blocks lose the order of the request.

The C# model `MoneroWalletData` in monero-csharp already has `blocks`, so a binding expects this.

## Proposal

### Results with a block: `{"blocks":[...]}`

`monero_wallet_get_txs()`, `monero_wallet_get_transfers()` and `monero_wallet_get_outputs()` return `{"blocks":[...]}`, for the full wallet and the RPC wallet alike. Today they return a list of txs, of transfers and of outputs.

```json
{"blocks":[
  {"height":2,"timestamp":1791675257,
   "txs":[{"hash":"62ded941...","fee":0,"numConfirmations":99,"unlockTime":62,"isMinerTx":true,"isConfirmed":true,"isLocked":false,
           "incomingTransfers":[{"amount":35184271425600,"accountIndex":0,"subaddressIndex":0,"address":"47cPmb4b..."}]}]},
  {"txs":[{"hash":"ffa2a44c...","inTxPool":true,"isConfirmed":false,"outgoingTransfer":{"amount":1000000000,"accountIndex":0}}]}
]}
```

- `get_txs()` lists the txs of each block, `get_transfers()` the transfers inside their txs (`incomingTransfers`, `outgoingTransfer`), and `get_outputs()` the outputs inside their txs (`outputs`).
- A tx that isn't in a block, in the pool or not yet relayed, is in a block without `height`, as in monero-java. There is one such block at most.
- The result of a query with no match is `{"blocks":[]}`.
- The order of the txs is the one of the blocks, and a query by hashes doesn't get them back in the order of its hashes. A binding sorts them, as monero-java does. The docs of the functions say so.
- The helpers of monero-cpp don't fit as they are: `get_blocks_from_outputs()` throws "Need to handle unconfirmed output" for an output in the pool, and `get_blocks_from_txs()` only takes the txs of a wallet, so the daemon needs the same for `monero_tx`. monero-c gets its own helpers, which handle both.

### Tx sets: `{"txSets":[...]}`

Every call that returns txs that the wallet made returns `{"txSets":[...]}`: `monero_wallet_create_tx()`, `monero_wallet_create_txs()`, `monero_wallet_sweep_output()`, `monero_wallet_sweep_unlocked()`, `monero_wallet_sweep_dust()`, `monero_wallet_sign_txs()` and `monero_wallet_describe_tx_set()`. Each set has its `txs`, and `unsignedTxHex`, `signedTxHex` or `multisigTxHex` when it has them.

```json
{"txSets":[{"unsignedTxHex":"4d6f6e65726f20756e7369676e656420747820736574...",
            "txs":[{"hash":"314b9c55...","fee":2599200000,"metadata":"011673657269616c...","outgoingTransfer":{"amount":1000000000,"accountIndex":0}}]}]}
```

- It is `txSets`, not a single set for `monero_wallet_create_txs()`, so that a binding has one parser for the calls that make txs. `monero_wallet_sweep_unlocked()` and `monero_wallet_sweep_dust()` can make several sets, and the others make one.
- The inputs keep what they took: `monero_wallet_relay_tx_json()` a tx of a set (an object with `metadata`), `monero_wallet_relay_txs_json()` a list of them, and `monero_wallet_describe_tx_set()` a set. A binding passes `txSets[i]` and `txSets[i].txs[j]` as they are. They also take what the creations return, so that a caller of C needs no JSON parser to create and relay a tx: `relay_tx_json()` finds the tx in `{"txSets":[...]}` or in one set, if it holds exactly one, `relay_txs_json()` relays all the txs in it, and `describe_tx_set()` describes the set of `{"txSets":[...]}` if there is one.

### Queries: flat in, block-rooted inside

The queries stay flat for the caller, as they are now, and monero-c builds the block-rooted JSON that `deserialize_from_block()` reads:

- `monero_wallet_get_txs()`: `{"isConfirmed":true,"transferQuery":{...},"outputQuery":{...}}`, as today. The query is wrapped as `{"txs":[query]}` before it is read.
- `monero_wallet_get_transfers()`: `{"isIncoming":true,"txQuery":{"isConfirmed":true}}`. `txQuery` becomes the tx query and the rest its `transferQuery`: `{"txs":[{"isConfirmed":true,"transferQuery":{"isIncoming":true}}]}`.
- `monero_wallet_get_outputs()`: the same with `outputQuery`.

That makes `txQuery` work for transfers and outputs. The caller doesn't have to build the block-rooted form of monero-java, and what callers send today keeps working.

### Listeners

`on_output_received` and `on_output_spent` get the output inside its tx and its block, in the shape of `monero_wallet_get_outputs()`. A received output is in the `outputs` of the tx, and a spent one in its `inputs`:

```json
{"blocks":[{"height":1,"txs":[{"version":2,"unlockTime":61,"hash":"21717baf...","isConfirmed":true,"isLocked":true,
            "outputs":[{"amount":35184338534400,"accountIndex":0,"subaddressIndex":0}]}]}]}
```

A binding then reads it with the parser of the results, and gets the same `MoneroOutputWallet` that the listener of monero-java gets, with its tx and the height. An output of the pool has a block without `height`. monero-cpp builds a tx for each notification, and the JSON has a copy of it with the hash, the version, the unlock time and the flags, and of the output with its amount and its indices. The copies point to no parent, so there is nothing to release, and the models of monero-cpp stay as they are.

### The daemon

`monero_daemon_get_tx()`, `monero_daemon_get_txs()` and `monero_daemon_get_tx_pool()` return `{"blocks":[...]}` with the height and the timestamp that monero-cpp reads, and a block without `height` for the pool. `monero_daemon_get_tx()` still gives NULL for a tx that the daemon doesn't have. The calls that return blocks (`get_block_by_hash()`, `get_blocks_by_range()` and the others) don't change, because a block is the root, and neither do the headers and the listeners of the daemon.

### What doesn't change

The lists of values that have no parent stay plain arrays: the accounts, the subaddresses, the address book, the key images, the seed languages, the notes, the hashes. The tx config, the wallet config, the connection JSON and the sync result stay as they are.

## Decisions

| | Question | Options | Recommendation |
| --- | --- | --- | --- |
| 1 | The shape of the results with a context | A: `{"blocks":[...]}` as monero-java. B: keep the flat lists and add fields (`height`, `timestamp`, `txHash`) to each element | A. B duplicates the block or the tx in each transfer and output, and a binding that has the models of monero-java would need a second path |
| 2 | The input of the queries | A: flat, and monero-c translates it. B: block-rooted, as monero-java | A. It doesn't break the callers, and it fixes `txQuery` without making them build the block form |
| 3 | The tx sets | A: `{"txSets":[...]}` for every call. B: the set itself for `create_txs()`, `create_tx()` and `sweep_output()`, `{"txSets"}` for the sweeps, as monero-java | A. One parser. B only helps who ports the Java code line by line |
| 4 | The payload of the listener | A: the output in its tx and block, as `get_outputs()`. B: a flat object, `{"height","txHash","amount","accountIndex","subaddressIndex","version","unlockTime","isLocked"}` as the JNI | A. The model is the same as the one of the results. B is easier to read by hand, but it is a second format, and it has to grow with each field |
| 5 | The other lists | A: plain arrays. B: wrap them as the bridge does (`{"accounts":[...]}`, `{"subaddresses":[...]}`, `{"entries":[...]}`), as `MoneroWalletData` of monero-csharp has them | A, because they carry no context. B costs a break in many calls to match the data class of one binding, which can adapt it |
| 6 | The daemon txs | A: blocks too. B: leave them | A, for the height. It is three calls |
| 7 | The ABI version | Nothing was released, so the version stays 0.1.0 and the JSON change goes in under it, as the `is_trusted` change of PR #16 did. After the first release a change like this bumps the minor version | stay at 0.1.0 until the first release |

## Steps

One commit each, with its tests. Each step runs the unit tests with and without the sanitizers, and the integration tests on a regtest node, also under the sanitizers.

1. Helpers for the blocks and the tx sets, with the unconfirmed placeholder and the free of the models after the JSON is built. Done for the wallet in `monero_c_wallet.cpp`, with the helpers of monero-cpp for the txs and the transfers and one of its own for the outputs, which finds them in their txs. The daemon and the tx sets are left for their steps.
2. `get_txs`, `get_transfers` and `get_outputs` return blocks. Done. The integration test checks that the height and the tx hash are there, that a tx of the pool has no height, that an empty query gives `{"blocks":[]}`, and that a full wallet and an RPC wallet give the same shape.
3. The queries of transfers and outputs are translated, `txQuery` works, and the leak of the block-rooted query models is checked (`monero_utils::free()` has to reach them). Done, with the decision 2 as recommended. The integration test splits the outputs by `{"txQuery":{"isLocked":false}}` and `true` and checks that the halves add up to all of them, and that a range of heights in the `txQuery` of `get_transfers()` gives one transfer per block.
4. The creations, the sweeps, `sign_txs` and `describe_tx_set` return tx sets. Done, with the decision 3 as recommended. The integration test makes a set with a view-only wallet, and `create_tx()` and `create_txs()` both give it its `unsignedTxHex`, which `create_tx()` didn't before. It signs the hex with an offline wallet and describes it with the full wallet.
5. The payload of the listeners. Done, with the decision 4 as recommended. The integration test syncs a wallet with the keys of the one that sends, and checks the received output of a miner tx (height, hash, unlock time, locked flag, amount and indices) and the spent output of the tx that is mined (in `inputs`, with its hash).
6. The daemon txs, with the fake server of `monero_c_rpc_connection_unit_tests` and the node. Done, with the decision 6 as recommended. monero-cpp reads the height and the timestamp into a block that each tx has for itself, so the txs of one block are put in one new block, and the txs of the pool in one block that has no height. The fake server serves three txs, two in the block 101 and one in the 102, and the test checks that there are two blocks, in the order that they appear, and that the pool has none.
7. The ABI version, the docs of every changed function in the headers, the README, the package README and `TODO.md`. Then the item that compares the full wallet with the RPC wallet can be done, since both give the same JSON.

## Things to check while implementing

- That the blocks of a query hold only the txs that matched. `get_txs_aux()` of monero-cpp erases the txs that don't meet a query, and they must not stay in `block->m_txs`. Checked: it erases them from the block too. A query on the type of the txs (`{"isOutgoing":true}`) still keeps the txs that it filters out in memory, about 190 KB per call for 100 txs, which is the item of `TODO.md` about the txs that `get_txs()` filters out, and the integration test asks for its tx by hash for that reason.
- That the RPC wallet gives the same shapes. Checked: a full wallet and an RPC wallet with the same keys give the same blocks, txs, transfers and outputs, with the same heights and timestamps, and the same values for every key. The RPC wallet lacks the list `addresses` of an outgoing transfer and the `relay` and `isDoubleSpendSeen` flags of the txs of the outputs. `test_rpc_matches_full()` of the integration test compares them.
- That the unconfirmed placeholder block and the copy of the tx in the listener are freed. LeakSanitizer finds it in the unit tests.
- The cost of the listener JSON, which is built while the sync holds the lock of the wallet.
