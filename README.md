# osci_licensing

GPLv3 JUCE module for osci-render licensing and update client code.

This repository is intended to be consumed as a Git submodule at
`modules/osci_licensing` in the main osci-render repository.

## Integration

The module verifies Ed25519 license tokens and release manifests with
Monocypher 4.0.2, vendored under `third_party/monocypher`.

Applications must provide base64-encoded 32-byte public keys through these
preprocessor definitions:

- `OSCI_LICENSING_BACKEND_PUBLIC_KEY_B64`
- `OSCI_LICENSING_RELEASE_PUBLIC_KEY_B64`

The matching private keys must stay outside source control and should live only
in deployment or release-signing secrets.

## License

This module is licensed under GPLv3. The canonical license text is in
`COPYING` instead of `LICENSE` because the module also has a `license/`
source directory, which conflicts with `LICENSE` on case-insensitive
filesystems.