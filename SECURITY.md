# Security Policy

## Supported versions

Security fixes are made on `main` and included in a subsequent release. Older releases are not guaranteed to receive security fixes; use the latest release where possible.

## Reporting a vulnerability

Please report vulnerabilities privately through [GitHub's private vulnerability reporting](https://github.com/libmonero/monero-c/security/advisories/new). Do not disclose vulnerability details in public issues.

If private reporting is unavailable, contact a repository maintainer privately before sharing technical details. Include the affected version, impact, and steps to reproduce, and remove secrets or personal data from any attached logs.

## Wallet file permissions

On POSIX systems, monero-c makes local full-wallet files and their `.keys` files readable and writable by the owner only (`0600`). The calls that write them (creation, save, close with save, password change and move) run with a umask of `077`, so the files never exist with a wider mode, and they set `0600` after the call too, for the files that were already there. Calls report an error if the permissions cannot be set. Opening an existing wallet takes the permission of the group and the others away from its files, if they belong to the user that opens it, and the open does not fail when that can't be done; a wallet that doesn't open, with a wrong password, is left as it was.

The umask belongs to the process: while one of those calls runs, a file that another thread of the program makes also gets no permission for the group and the others. The umask that was there before is put back when the last call returns.

## Verifying release provenance

Release assets carry GitHub build provenance attestations. Verify an asset with GitHub CLI:

```sh
gh attestation verify <asset-path> --repo libmonero/monero-c
```
