# Third-party code

This project uses MeshCore, pinned to commit
`81dbae592ffad40b4212bc0b37e82b38da08f4bc` from
<https://github.com/meshcore-dev/MeshCore>.
MeshCore's MIT notice and its bundled ed25519 implementation's zlib-style
notice are retained under `licenses/`. The pinned dependency is fetched by
the setup tool; its checkout and original notices must be retained locally.

Arduino Crypto 0.4.0 is installed through Arduino Library Manager. Its package
and original MIT notices are also retained under `licenses/`. The public source
export does not redistribute the Arduino installation or compiled firmware.

The CubeCell board support package 1.4.0 is installed through Heltec's official
board index. Its vendor sources and binary components retain their own terms;
they are not relicensed by this project or included in the source export.

Original project code is licensed under the MIT License; see `LICENSE`.
Third-party components retain their respective copyright and license notices.
