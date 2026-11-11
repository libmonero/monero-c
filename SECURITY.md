# Security Policy

## Supported versions

Security fixes are made on `main` and included in a subsequent release. Older releases are not guaranteed to receive security fixes; use the latest release where possible.

## Reporting a vulnerability

Please report vulnerabilities privately through [GitHub's private vulnerability reporting](https://github.com/libmonero/monero-c/security/advisories/new). Do not disclose vulnerability details in public issues.

If private reporting is unavailable, contact a repository maintainer privately before sharing technical details. Include the affected version, impact, and steps to reproduce, and remove secrets or personal data from any attached logs.

## Verifying release provenance

Release assets carry GitHub build provenance attestations. Verify an asset with GitHub CLI:

```sh
gh attestation verify <asset-path> --repo libmonero/monero-c
```
