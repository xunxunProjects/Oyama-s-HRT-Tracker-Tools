# AI contributors: commit rules

This page is for AI agents and humans using AI assistance. The repository is
a real security-sensitive CLI; a plausible-looking patch is not evidence.

## Before you change anything

- Read `CONTRIBUTING.md`, the relevant README, and this wiki page.
- Work in a branch. Never commit directly to `main`; never force-push a
  shared branch; never use an admin merge to skip required review unless the
  maintainer explicitly asked.
- Know which repository you are in: `Oyama-s-HRT-Tracker` (web/Worker) or
  `Oyama-s-HRT-Tracker-Tools` (CLI). They have different tests and release
  processes.

## Never commit

- `.dev.vars`, `.env`, vaults, tokens, passphrases, account passwords,
  snapshots, encrypted logs, or any real user data.
- Build output: `tools/hrt-tool/build/`, `rust/hrt-print/target/`, `dist/`,
  `node_modules/`, `.playwright-mcp/`, editor caches.
- Generated release binaries or archives; releases are produced by CI from a
  tagged commit.
- `DRAFT` markers, temporary notes, or TODO placeholders in published docs.

## Required checks

For `hrt-tool`:

```sh
cmake -S tools/hrt-tool -B tools/hrt-tool/build
cmake --build tools/hrt-tool/build -j"$(nproc)"
ctest --test-dir tools/hrt-tool/build --output-on-failure
```

For the Rust printer: `cargo test --offline` in
`tools/hrt-tool/rust/hrt-print`. For the Go tools: `go test ./...` in their
module directories. For cross-builds, follow
[Cross Compilation](Cross-Compilation). Run the full suite, not only the test
you touched; report the exact command and result.

For the main repo, follow its `CONTRIBUTING.md` (`npx tsc --noEmit`,
`npm run lint`, `npm run format:check`, `npm test`, `npm run build`).

## Commit and PR rules

- Use a focused branch and a conventional subject such as
  `feat(hrt-tool): ...`, `fix(worker): ...`, or `docs(wiki): ...`.
- Explain what changed, why, what you ran, and what you did not run.
- Update `CHANGELOG.md` for user-visible changes; keep the version in
  `CMakeLists.txt` as the single source for the CLI.
- Do not fabricate test results, screenshots, or audit evidence. If a command
  was not run, say so.
- Do not fix a security issue silently: reproduce it, add a regression test,
  and note the impact.
- Do not change authentication, cryptography, key handling, or token
  binding without a maintainer's explicit approval.
- Do not touch unrelated files or other people's worktrees.

## Wiki changes

The wiki source lives in `wiki/` in the repository. Edit it there, run
`scripts/publish-wiki.sh --check` and `--dry-run`, then publish with
`scripts/publish-wiki.sh`. Do not edit the GitHub wiki UI directly. Keep
internal links as `[label](slug)` and run a link check before publishing.

## Definition of done

- [ ] The change is on a branch and the diff is minimal.
- [ ] Tests pass; the command and output are recorded.
- [ ] No secrets, data, or build artifacts are staged.
- [ ] Documentation and changelog are updated.
- [ ] A PR is open, CI is green, and a human has reviewed it.
