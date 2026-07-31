# Releasing Ambient Granulator for GameCube

The release tag must point to the exact committed source revision from which
the shipped DOL and sample bank were built. Generated artifacts belong in a
GitHub Release, not in Git.

Use the current hundredth-step versioning: the first public release is
`v0.13`, followed by `v0.14`, `v0.15`, and so on. Do not move a published tag;
release the next version instead.

## Release steps

1. Land all intended changes on `main`, update `VERSION`, `CHANGELOG.md`, and
   `RELEASE_NOTES.md`, then commit and push the clean release metadata.
2. Build and verify the committed revision:

   ```sh
   tools/release.sh v0.15
   ```

   This runs host tests, patcher tests, native PowerPC validation, and packages
   a DOL, SD2SP2 bank, patcher, licences, and checksums under `build/release/`.
3. Verify the generated checksums:

   ```sh
   cd build/release
   shasum -a 256 -c SHA256SUMS.txt
   ```

4. Create and push an annotated tag on the build commit:

   ```sh
   git tag -a v0.15 -m "Ambient Granulator for GameCube v0.15"
   git push origin v0.15
   ```

5. Create the GitHub Release from that tag and attach:
   - `gamecube-ambient-granulator-v0.15.dol`
   - `gamecube-ambient-granulator-sample-bank-v0.15.bin`
   - `gamecube-granulator-patcher-v0.15.html`
   - `SHA256SUMS.txt`
   - `LICENSE` and `SAMPLE_LICENSE.md`

Keep the release notes user-facing: describe the creative changes, compatibility
notes, and the level of testing. Until the physical test record is completed,
say that the release is host-tested, Dolphin-verified, and awaiting the
documented GameCube/SD2SP2 acceptance pass.
