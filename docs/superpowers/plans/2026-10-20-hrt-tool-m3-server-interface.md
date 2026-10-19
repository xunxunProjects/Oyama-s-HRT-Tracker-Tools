# hrt-tool M3 Server Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the versioned `/api/tool/v1/*` namespace to the Oyama HRT Tracker Cloudflare Worker without changing the existing website API, so the CLI can log in, check its token, read and write encrypted snapshots, and log out.

**Architecture:** Route the new namespace inside the existing worker request pipeline. Public routes (`version`, `login`, `compute`) sit beside the current public routes; protected routes (`me`, `logout`, snapshot reads/writes) sit inside the existing JWT/session-validation block, so revocation, idle timeout, admin handling, and rate limits are shared rather than reimplemented. Content SQL moves into a small `contentStore.ts` used by both `/api/content` and `/api/tool/v1/snapshot`, keeping the website responses byte-compatible.

**Tech Stack:** TypeScript on Cloudflare Workers, D1 (SQLite), `jose` JWT, Vitest with `node:sqlite` and the migration directory.

**Spec:** `docs/superpowers/specs/2026-10-19-hrt-tool-design.md` §9 (server interface), §10 (transport/trust), §11 (data contracts).

## Global Constraints

- The main repository enters `main` only through a pull request; all CI checks must be green; merging is the final step.
- Existing `/api/login` and `/api/content` behaviour and response bodies stay byte-compatible.
- The worker never decrypts the snapshot, never derives the cloud key, and never computes concentrations in v1.
- `POST /api/tool/v1/compute` returns `501 NOT_IMPLEMENTED`; server compute is reserved.
- Snapshot bodies are capped at `MAX_CLOUD_BACKUP_BYTES` (2 MiB) and pruned with `selectBackupsToKeep` (5 revisions).
- Writes use the existing `content-write:<userId>` rate limit (20).
- `PUT /snapshot` accepts `If-Match` or `X-HRT-Base-Revision`; a mismatch returns `409` with the newest revision metadata; without it, last-writer-wins.
- No new dependency; Node 22, TypeScript, Prettier, ESLint, and Vitest as already configured.
- The new branch is `Tool-Interface/data`, cut from `upstream/main`; the PR targets `xunxunProjects/Oyama-s-HRT-Tracker:main`.

## Review Focus

1. **Website compatibility:** `/api/login` and `/api/content` keep the same status codes, JSON keys, and headers after the extraction.
2. **Auth reuse:** a tool login is subject to the same IP/account rate limits, lockout, TOTP/backup-code, and device-pass checks; a tool token is subject to session revocation and the idle timeout.
3. **Snapshot isolation and conflicts:** one user can never read or write another user's content row; `If-Match` conflicts return 409 with usable newest metadata; retention and the 2 MiB cap still apply.
4. **Opaque payloads:** arbitrary JSON `data` round-trips unchanged; malformed bodies, missing `data`, and oversized bodies fail with typed errors, never a 500.
5. **Logout:** deletes only the caller's own session, is idempotent, and leaves the token unusable for protected routes.

## Interfaces

`toolApi.ts`:

```ts
export const TOOL_API_VERSION = 1;
export const TOOL_MIN_VERSION = '0.1.0';
export const TOOL_TRANSPORTS = ['tls'] as const;

export interface ToolAuthContext {
    userId: string;
    sessionId: string | null;
    username: string;
    role: string;
}

export const toolServerTime = (): string => new Date().toISOString();
export const toolVersionBody = () => ({
    apiVersion: TOOL_API_VERSION,
    minToolVersion: TOOL_MIN_VERSION,
    transport: [...TOOL_TRANSPORTS],
    serverTime: toolServerTime(),
});

export function addToolLoginFields(body: Record<string, unknown>): Record<string, unknown>;

export async function handleToolProtected(
    request: Request,
    env: Env,
    auth: ToolAuthContext,
    json: (body: unknown, status?: number, headers?: Record<string, string>) => Response,
    jsonError: (code: string, message: string, status: number, headers?: Record<string, string>) => Response,
): Promise<Response | null>;
```

`contentStore.ts`:

```ts
export interface ContentRow { id: string; created_at: number; data: string | null; }
export async function listContent(env: Env, userId: string): Promise<Array<{ id: string; created_at: number; data_size: number }>>;
export async function latestContent(env: Env, userId: string): Promise<ContentRow | null>;
export async function getContent(env: Env, userId: string, id: string): Promise<ContentRow | null>;
export async function storeContent(env: Env, userId: string, data: unknown): Promise<string>;
```

## Task 1: Content store extraction

**Files:**
- Create: `contentStore.ts`
- Modify: `worker.ts` (`/api/content` block)
- Test: `tests/toolInterface.test.ts` (new file; content compatibility assertions)

**Interfaces:**
- Produces: `listContent`, `latestContent`, `getContent`, `storeContent`.

- [ ] **Step 1: Write the failing compatibility test**

In `tests/toolInterface.test.ts`, build the SQLite-backed `env` used by `loginLockout.test.ts`, register a user, log in, and assert `POST /api/content` still returns `201 {message, id}` and `GET /api/content` returns the metadata array with `data_size`.

- [ ] **Step 2: Run it against the current worker**

Run: `npx vitest run tests/toolInterface.test.ts`
Expected: PASS for the old route (this test pins the behaviour the extraction must preserve).

- [ ] **Step 3: Extract the functions**

Move the SQL and pruning into `contentStore.ts`; make `/api/content` call them. `storeContent` inserts, lists revisions, calls `selectBackupsToKeep(revisions, id)`, deletes stale rows, and returns the new id.

- [ ] **Step 4: Re-run the compatibility test and the full suite**

Run: `npx vitest run tests/toolInterface.test.ts tests/workerHardening.test.ts`
Expected: PASS; old response bodies unchanged.

- [ ] **Step 5: Commit**

```bash
git add contentStore.ts worker.ts tests/toolInterface.test.ts
git commit -m "refactor(worker): extract shared content storage helpers"
```

## Task 2: Public tool routes

**Files:**
- Create: `toolApi.ts`
- Modify: `worker.ts` (sensitivePaths, public routes, login block)
- Test: `tests/toolInterface.test.ts`

**Interfaces:**
- Produces: `GET /api/tool/v1/version`, `POST /api/tool/v1/login`, `POST /api/tool/v1/compute`.
- Consumes: the existing login block and `jsonError`.

- [ ] **Step 1: Write failing tests**

Assert `GET /api/tool/v1/version` returns `200 {apiVersion:1,minToolVersion,transport:['tls'],serverTime}` without a DB read; `POST /api/tool/v1/login` with the registered user returns 200 with `token`, `user`, `devicePass`, plus `apiVersion`, `serverTime`, `transport`, and bad credentials return 401; `POST /api/tool/v1/compute` returns 501 `NOT_IMPLEMENTED`.

- [ ] **Step 2: Verify RED**

Run: `npx vitest run tests/toolInterface.test.ts`
Expected: 404 for the new paths.

- [ ] **Step 3: Implement**

Add `/api/tool/v1/version` and `/api/tool/v1/compute` in the public section. Add `/api/tool/v1/login` to `sensitivePaths` and to the login condition; when the path is the tool one, add the three tool fields to every successful login body. Keep 2FA and error responses unchanged.

- [ ] **Step 4: Verify GREEN**

Run: `npx vitest run tests/toolInterface.test.ts`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add toolApi.ts worker.ts tests/toolInterface.test.ts
git commit -m "feat(worker): add public /api/tool/v1 routes"
```

## Task 3: Protected tool routes

**Files:**
- Modify: `toolApi.ts`, `worker.ts` (inside the protected block)
- Test: `tests/toolInterface.test.ts`

**Interfaces:**
- Produces: `GET /me`, `POST /logout`, `GET /snapshot`, `GET /snapshots`, `GET /snapshot/:id`, `PUT /snapshot`.
- Consumes: auth context from the existing JWT block and the content-store helpers.

- [ ] **Step 1: Write failing tests**

With a real token: `GET /me` returns the user and server time; `GET /snapshot` returns 404 `NO_SNAPSHOT` initially; `PUT /snapshot` stores `{data}` and returns 201; the read endpoints return the stored bundle and metadata; `PUT` with a stale `If-Match` returns 409 `CONFLICT` with the newest `{id, createdAt}`; a second user cannot read the first user's revision; `POST /logout` removes the session and a later `/me` returns 401.

- [ ] **Step 2: Verify RED**

Run: `npx vitest run tests/toolInterface.test.ts`
Expected: 404 for the protected tool paths.

- [ ] **Step 3: Implement**

Inside the authenticated try block, before the existing route blocks, dispatch `/api/tool/v1/` to `handleToolProtected`. `logout` deletes `sessions` by `sessionId` and `userId`. Snapshot writes enforce the 2 MiB cap, `content-write` limit, `If-Match` conflict, insert, and prune.

- [ ] **Step 4: Verify GREEN**

Run: `npx vitest run tests/toolInterface.test.ts`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add toolApi.ts worker.ts tests/toolInterface.test.ts
git commit -m "feat(worker): add authenticated /api/tool/v1 snapshot routes"
```

## Task 4: Contract documentation

**Files:**
- Create: `docs/tool-interface/TOOL_API.md`
- Create (Tools repo): `docs/tool-interface/TOOL_API.md`

- [ ] **Step 1: Document every route, auth requirement, request/response shape, error codes, size/retention limits, and the 501 compute reservation.**
- [ ] **Step 2: Verify the two copies are identical.**

Run: `diff docs/tool-interface/TOOL_API.md ../Oyama-s-HRT-Tracker-Tools/docs/tool-interface/TOOL_API.md`
Expected: no output.

- [ ] **Step 3: Commit both repositories.**

## Task 5: CI and pull request

- [ ] **Step 1: Run the full local CI.**

```bash
npm run lint
npm run format:check
npx tsc --noEmit
npm test
npm run build
```

Expected: all green.

- [ ] **Step 2: Push the branch and open the PR.**

```bash
git push -u origin Tool-Interface/data
gh pr create --repo xunxunProjects/Oyama-s-HRT-Tracker --base main \
  --head fantuanmtf:Tool-Interface/data --title "feat: add hrt-tool /api/tool/v1 interface" ...
```

- [ ] **Step 3: Watch every check until green.**

Run: `gh pr checks --repo xunxunProjects/Oyama-s-HRT-Tracker --watch`
Expected: every check passes; if one fails, fix it on the branch and push again.

- [ ] **Step 4: Merge as the final step.**

Merge only after every check is green, using the repository's merge method. Do not merge while any check is pending or failing.
