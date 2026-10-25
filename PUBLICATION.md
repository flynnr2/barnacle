# Publication preparation

The working repository contains private operational history. Do not publish
its current Git history or assume `.gitignore` removes already tracked captures.
The original drive downloads, device key, installation evidence and live monitor
remain local and intact.

Create a fresh, allowlisted source tree with:

```sh
.venv/bin/python tools/export_public.py --destination release/cubecell-meshcore-source
```

The export contains the supported shared firmware and profiles, build/identity
tools, synthetic tests, dependency pins, user documentation and third-party
notices. It excludes all captures, images, logs, private identities, compiled
firmware, local toolchains, research reports and Git history. Legacy test
coordinates are replaced with synthetic coordinates in the export. The original
tests and saved captures in the workshop are not rewritten for that purpose.

Use the fresh tree for a new public repository. Nothing in this workflow creates
a GitHub repository, pushes commits or publishes an artifact. The exporter
refuses an existing destination rather than overwriting it and writes a manifest
plus a ZIP for review.

Original project code is MIT licensed. See
`THIRD_PARTY_NOTICES.md` for dependency terms already retained. Hardware results
must distinguish a compiled profile from a profile actually flashed and tested.
At preparation time, only the repeater-clock behavior has device commissioning
evidence; the other profiles require their own physical validation.
