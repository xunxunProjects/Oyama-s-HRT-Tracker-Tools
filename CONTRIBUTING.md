# Contributing

This is an independent, experimental project. Small, focused changes with tests
are easiest to review. Security reports follow [`SECURITY.md`](SECURITY.md), not
the public issue tracker.

## Rules

- Use synthetic data only. Never commit real health records, private keys,
  passphrases, tokens, device passes, or production backups.
- Do not add custom cryptography. Use the selected OpenPGP library and keep it
  behind the native/core boundary.
- Do not claim production readiness, stable `.hrtb`, an audit, or "no
  vulnerabilities".
- Keep the website's no-keys boundary intact: no browser key handling is added
  to this project.
- Do not force-push or rewrite shared branches.

## Go CLI

```sh
cd tools/hrt-cold
gofmt -l .
go vet ./...
go test -count=1 ./...
go test -race -count=1 ./...
go build -trimpath ./...
```

The release script also runs the tests:

```sh
tools/hrt-cold/scripts/build-release.sh 0.1.0 /tmp/hrt-cold-release
```

## Native scaffold

```sh
./configure --build-dir=build --enable-werror
cmake --build build
(cd build && ctest --output-on-failure)
```

Linux CI also runs a sanitizer build. The native core is docs-first; do not add
a crypto backend without updating [`docs/native/CRYPTO.md`](docs/native/CRYPTO.md)
and recording licence/notices changes.

## Wiki

The GitHub wiki is generated from the `wiki/` directory in this repository.
Edit the pages here and publish them with:

```sh
scripts/publish-wiki.sh
```

Do not edit the wiki UI directly; changes made there are overwritten on the
next publish. The first wiki page must be created once in the web UI before
the git repository exists.

## Community

Participation follows [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md). Report
conduct issues privately through the repository's
[advisory form](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/security/advisories/new)
with a title starting with `Conduct:`; security reports follow
[`SECURITY.md`](SECURITY.md).

## Pull requests

Include:

- what changed and why;
- the exact commands run and their results;
- whether failure paths were tested;
- any platform not tested;
- confirmation that no real data, keys, or secrets are included.

Keep documentation in English, matching the existing files. User-facing release
notes may include a short Chinese summary.
