# Removing the bundled SSH tunnel from erpl

`erpl` bundles an SSH tunnel extension in `tunnel/`. It has been superseded by
[erpl-tunnel](https://github.com/DataZooDE/erpl-tunnel), a dedicated extension that does
strictly more. This removes the bundled copy and leaves behind stubs that tell callers
where it went.

## No functionality is lost

Measured against both trees, not assumed:

| | bundled `tunnel/` | `erpl-tunnel` |
|---|---|---|
| `tunnel_create`, `tunnel_close`, `tunnel_close_all`, `tunnels()` | yes | yes (`tunnel_create` kept as a deprecated alias of `tunnel_import`) |
| `ssh_tunnel` secret | 7 parameters | same type name, plus a `tunnel` alias, all 7 parameters, plus mesh parameters |
| Reverse tunnels (`tunnel_export`) | — | yes |
| `tunnel_import`, `tunnel_peers`, `tunnel_self`, `tunnel_mesh_activate` | — | yes |
| Tailscale / NetBird backends | — | yes |
| `bind_all` on create | — | yes |
| Published platforms | only inside the erpl bundle | get.erpl.io since 2026-07-26, **including `linux_arm64`**, which erpl itself excludes |
| Source | 1,973 lines | 4,785 lines |

Two behavioural differences, neither a loss:

- `tunnels()` gains `backend` and `direction` at positions 2 and 3, so the **column order
  changes**. Queries naming columns are unaffected; `SELECT *` consumed positionally is not.
- `tunnel_create` is deprecated upstream in favour of `tunnel_import`, but still works.

**`erpl_rfc` has no dependency on the tunnel in either direction** — verified by grep across
`rfc/`, `bics/` and `odp/`. The only mention is one comment. It is a plain port forwarder;
a tunnelled SAP connection just points `ashost` at a local port. So nothing in the RFC path
changes.

## The reason this is urgent, not cosmetic

The bundled copy **silently shadows the standalone extension**. Demonstrated against real
artifacts:

```sql
LOAD erpl;                                   -- trampoline extracts and loads its erpl_tunnel
INSTALL '<erpl-tunnel build>'; LOAD erpl_tunnel;   -- succeeds. No error, no warning.

SELECT function_name FROM duckdb_functions()
WHERE function_name IN ('tunnel_export','tunnel_import','tunnel_peers','tunnel_self',
                        'tunnel_create','tunnel_close');
-- tunnel_close
-- tunnel_create      <- only the old ones
```

The extension name `erpl_tunnel` is already claimed, so the second `LOAD` is a no-op and the
user keeps the weaker implementation without being told. **Anyone who has erpl installed
cannot use erpl-tunnel at all today.** Removing the bundled copy is what unblocks them.

## Design, and the two constraints that force it

Decisions taken: the functions **error immediately** with a migration message rather than
warning for a release, and users **install erpl-tunnel separately** rather than erpl
bundling it.

The stubs must live in **`erpl_rfc`**, not in a surviving `erpl_tunnel` extension. Two
findings force this, both verified experimentally rather than read off the source:

1. **The name must be freed.** As long as anything erpl ships is called `erpl_tunnel`, the
   user's `LOAD erpl_tunnel` is a no-op and the real extension never loads (above).

2. **Last registration wins, so the stub is transparent once the real one loads.** A
   duplicate pragma name does *not* throw — contrary to what `ExtensionLoader` suggests,
   where the pragma path leaves `CreateInfo::on_conflict` at its `ERROR_ON_CONFLICT`
   default. Tested by registering a `tunnel_create` stub in `erpl_rfc` alongside the real
   one: startup succeeded, and `PRAGMA tunnel_create(remote_host=…, remote_port=…)` was
   handled by the **real** implementation, registered second. The stub never fired.

The stub must **not** register the `ssh_tunnel` secret type. `RegisterSecretType` throws
`InternalException("Attempted to register an already registered secret type")`, and secret
*functions* use `ERROR_ON_CONFLICT` explicitly — so registering it would make `LOAD
erpl_tunnel` fail outright.

### The ordering hazard

"Last registration wins" cuts both ways. `LOAD erpl_tunnel` followed by `LOAD erpl` would
have the stub register *second* and shadow the real implementation — the very bug being
fixed, inverted.

So the stubs must be registered **conditionally**: look the name up in the system catalog
first and skip if a real implementation is already present. This is the one piece of the
change that needs care, and it needs a test in both load orders.

## Phases

### Phase 1 — deprecation stubs in `erpl_rfc`

- New `rfc/src/pragma_tunnel_deprecated.{hpp,cpp}`: `tunnel_create`, `tunnel_close`,
  `tunnel_close_all` as pragmas and `tunnels()` as a table function, each throwing:

  ```
  PRAGMA tunnel_create has moved out of erpl into the dedicated erpl_tunnel
  extension, which supports reverse tunnels and Tailscale/NetBird in addition
  to SSH.

    INSTALL erpl_tunnel FROM 'http://get.erpl.io';
    LOAD erpl_tunnel;

  See https://github.com/DataZooDE/erpl-tunnel
  ```

- Each stub carries the **same named parameters** as the function it replaces, so a
  realistic call binds to it and gets the message rather than a signature error.
- Registration is skipped when the name already exists in the catalog.
- No `ssh_tunnel` secret type registration.

### Phase 2 — delete the bundled extension

- Remove `tunnel/` entirely.
- `extension_config.cmake`: drop the `erpl_tunnel` load in both branches.
- `trampoline/`: drop the embed, extract and load of `erpl_tunnel.duckdb_extension` on all
  three platforms.
- `rfc/vcpkg.json`: drop `libssh2` — nothing else in erpl uses it.
- `Makefile`: remove `sql_tests_tunnel` and the docker-compose SSH mock steps.
- `.github/`: remove tunnel references.

### Phase 3 — tests

- A new `rfc/test/sql/sap_tunnel_deprecated.test`: each stub raises, and the message names
  `erpl_tunnel` and the repository.
- **Both load orders**, which is the part that can regress silently:
  - `LOAD erpl` then `LOAD erpl_tunnel` → real functions win, `tunnel_export` present.
  - `LOAD erpl_tunnel` then `LOAD erpl` → real functions still win; the stub must not
    have displaced them.
- Full RFC/BICS/ODP suites stay green (they never touched the tunnel).

### Phase 4 — documentation and release

- `API_REFERENCE.md`: replace the `erpl_tunnel — SSH Tunneling` section with a short
  pointer to the dedicated extension.
- `README.md`, `CLAUDE.md`, `TELEMETRY.md`: drop erpl_tunnel from the extension list.
- `CHANGELOG.md`: a **breaking change** entry — what moved, the two-line migration, and the
  `tunnels()` column-order difference.

## Risks

- **Breaking on upgrade.** Working tunnel setups stop on the release that ships this. The
  error states the fix in two lines — and those users cannot migrate today anyway, so the
  status quo is not actually safer.
- **The ordering hazard above** is the one real implementation risk. It is invisible unless
  tested in both directions, which is why Phase 3 tests both.
- **Artifact size.** Dropping libssh2 and ~2,000 lines shrinks the erpl bundle; the smoke
  test should confirm the trampoline still extracts and loads with one fewer extension.
