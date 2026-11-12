# Security Policy

## Supported versions

Security fixes are made on `main` and included in a subsequent release. Older releases are not guaranteed to receive security fixes; use the latest release where possible.

## Reporting a vulnerability

Please report vulnerabilities privately through [GitHub's private vulnerability reporting](https://github.com/libmonero/monero-c/security/advisories/new). Do not disclose vulnerability details in public issues.

If private reporting is unavailable, contact a repository maintainer privately before sharing technical details. Include the affected version, impact, and steps to reproduce, and remove secrets or personal data from any attached logs.

## Wallet file permissions

On POSIX systems, monero-c restricts local full-wallet files and their `.keys` files to owner read/write (`0600`) after creation, save, password change, and move. Calls report an error if the permissions cannot be set. Opening an existing wallet by itself does not change its existing file permissions.

## Verifying release provenance

Release assets carry GitHub build provenance attestations. Verify an asset with GitHub CLI:

```sh
gh attestation verify <asset-path> --repo libmonero/monero-c
```
