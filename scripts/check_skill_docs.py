#!/usr/bin/env python3
"""scripts/check_skill_docs.py -- keep the Claude Code plugin and the docs honest.

The erpl plugin (plugins/erpl/) teaches an AI assistant the SQL surface. A skill that names
a function or option the extension does not register sends the assistant off writing SQL
that fails, so every name is checked against what a build actually registers.

Two modes:

  --names   (offline, no SAP system; runs in CI after the smoke test)
            1. Every identifier shaped like an erpl name -- sap_*, tunnel_*, erpl_* -- that a
               scanned file mentions in a backtick span, in call syntax `name(`, after
               PRAGMA/SET, or anywhere in a SKILL.md/command frontmatter, must be a
               registered function, pragma or setting, or be listed with a reason in
               scripts/doc_snippets/allowed_names.txt (parameter and table names,
               functions of other extensions, removed functions a changelog still names).
            2. Every CREATE SECRET of TYPE sap_rfc in a ```sql fence may only use fields the
               secret type accepts (checked by creating it in :memory:, which needs no SAP).
            3. Every ```sql fence whose first line is `-- check:offline` is executed and
               must succeed.

  --live    (needs SAP: the a4h trial, credentials from ERPL_SAP_* like the SQL suites)
            Executes every ```sql fence of the plugin files, each in a fresh in-memory
            database with a `CREATE SECRET` built from ERPL_SAP_* prepended, except
            fences that are SYNOPSIS (scripts/doc_snippets/synopsis.txt) or start with
            `-- check:skip-live` (destructive or system-specific). A fence keyed in
            scripts/doc_snippets/expected_errors.txt must fail.

Illustrative or deliberately broken SQL belongs in a ```text fence, which is never run.
Keys in synopsis.txt / expected_errors.txt are `<file>\\t<first non-comment line>`.

The DuckDB to use is `--duckdb CMD` (shell-split, so `arch -x86_64 /path/duckdb` works) plus
`--preamble SQL` run before every block. Defaults: the static debug build
(build/debug/duckdb, all extensions linked in, no LOAD) with the SAP SDK on
LD_LIBRARY_PATH. Against a release artifact, pass the CLI and
`--preamble "LOAD erpl;"` with HOME pointing at where it was INSTALLed.

Writes a TSV report (status, file, line, first line, detail) to --out (default
build/skill-docs.tsv). Exit: 0 clean, 1 on FAIL or STALE, 2 when DuckDB cannot be run.
"""
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
SUPPORT_DIR = os.path.join(SCRIPT_DIR, "doc_snippets")
SYNOPSIS_FILE = os.path.join(SUPPORT_DIR, "synopsis.txt")
EXPECTED_ERRORS_FILE = os.path.join(SUPPORT_DIR, "expected_errors.txt")
ALLOWED_NAMES_FILE = os.path.join(SUPPORT_DIR, "allowed_names.txt")

PLUGIN_DIR = os.path.join("plugins", "erpl")
# Files whose names are checked. The plugin is checked whole; of the repo docs, the two a
# user (and an assistant) reads as the reference.
NAME_SCAN_FILES = ["README.md", "API_REFERENCE.md", "TROUBLESHOOTING.md"]

FENCE_RE = re.compile(r"^```[ \t]*([A-Za-z0-9_+-]*)[ \t]*$")
NAME = r"(?:sap|tunnel|erpl)_[a-z0-9_]*[a-z0-9]"
BACKTICK_RE = re.compile(r"`[^`\n]*`")
NAME_IN_SPAN_RE = re.compile(r"\b(" + NAME + r")\b")
CALL_RE = re.compile(r"\b(" + NAME + r")\s*\(")
PRAGMA_SET_RE = re.compile(r"\b(?:PRAGMA|SET|RESET)\s+(?:GLOBAL\s+|SESSION\s+)?(" + NAME + r")\b", re.IGNORECASE)
# Words that look like erpl names but are not part of the SQL surface.
NOT_SURFACE = {
    "sap_rfc",  # the secret type and the ATTACH TYPE
    "erpl_rfc", "erpl_bics", "erpl_odp", "erpl_ape", "erpl_tunnel",  # extension names
    "sap_rfc_secret",
}
SECRET_RE = re.compile(r"CREATE\s+(?:OR\s+REPLACE\s+)?(?:PERSISTENT\s+|TEMPORARY\s+)?SECRET\b.*?\);",
                       re.IGNORECASE | re.DOTALL)
OFFLINE_TAG = "-- check:offline"
SKIP_LIVE_TAG = "-- check:skip-live"


def read(path):
    with open(os.path.join(REPO_ROOT, path), encoding="utf-8") as f:
        return f.read()


def plugin_files():
    out = []
    root = os.path.join(REPO_ROOT, PLUGIN_DIR)
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for fn in sorted(files):
            if fn.endswith(".md"):
                out.append(os.path.relpath(os.path.join(base, fn), REPO_ROOT))
    return out


def load_keys(path):
    if not os.path.exists(path):
        return set()
    with open(path, encoding="utf-8") as f:
        return {l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")}


def load_removed(path):
    names = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.strip() and not line.startswith("#"):
                    name, _, reason = line.rstrip("\n").partition("\t")
                    names[name.strip()] = reason.strip()
    return names


def fences(text):
    """(language, body, 1-based line of the opening fence) for each fenced block."""
    lines = text.splitlines()
    i, out = 0, []
    while i < len(lines):
        m = FENCE_RE.match(lines[i].strip())
        if m:
            lang, start, body = m.group(1).lower(), i, []
            i += 1
            while i < len(lines) and lines[i].strip() != "```":
                body.append(lines[i])
                i += 1
            out.append((lang, "\n".join(body), start + 1))
        i += 1
    return out


def first_line(body):
    for line in body.splitlines():
        s = line.strip()
        if s and not s.startswith("--"):
            return s
    return body.strip().splitlines()[0].strip() if body.strip() else ""


def frontmatter_end(lines):
    if lines and lines[0].strip() == "---":
        for i in range(1, len(lines)):
            if lines[i].strip() == "---":
                return i
    return 0


def name_mentions(path, text):
    """(name, line) for each erpl-shaped identifier the file uses as a SQL name."""
    lines = text.splitlines()
    fm_end = frontmatter_end(lines)
    in_fence, fence_lang = False, ""
    found = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        m = FENCE_RE.match(stripped)
        if m and not in_fence:
            in_fence, fence_lang = True, m.group(1).lower()
            continue
        if in_fence and stripped == "```":
            in_fence = False
            continue
        if 0 < i < fm_end:
            found += [(n, i + 1) for n in NAME_IN_SPAN_RE.findall(line)]
            continue
        if in_fence:
            # Inside SQL every call and PRAGMA/SET counts; ```text and others are prose.
            if fence_lang == "sql":
                code = line.split("--", 1)[0]
                found += [(n, i + 1) for n in CALL_RE.findall(code) + PRAGMA_SET_RE.findall(code)]
            continue
        for span in BACKTICK_RE.findall(line):
            found += [(n, i + 1) for n in NAME_IN_SPAN_RE.findall(span)]
        found += [(n, i + 1) for n in CALL_RE.findall(line)]
    return [(n, l) for n, l in found if n not in NOT_SURFACE]


class DuckDB:
    def __init__(self, cmd, preamble, env_extra, home):
        self.cmd = shlex.split(cmd)
        self.preamble = preamble
        self.home = home
        self.env = dict(os.environ, DATAZOO_DISABLE_TELEMETRY="1", **env_extra)

    def run(self, sql, timeout=300):
        tmp = tempfile.mkdtemp(prefix="skill_docs_")
        try:
            # A documented CREATE PERSISTENT SECRET must never land in the real
            # ~/.duckdb/stored_secrets, so secrets go to the throwaway directory. HOME itself
            # stays: erpl_bics autoloads json from the extension directory while starting up.
            secret_dir = tmp.replace("'", "''")
            full = (f"SET autoinstall_known_extensions = false;\nSET secret_directory = '{secret_dir}';\n"
                    + self.preamble + "\n" + sql)
            try:
                env = dict(self.env, HOME=self.home) if self.home else self.env
                r = subprocess.run(self.cmd + ["-unsigned", "-bail", "-csv", ":memory:"], input=full,
                                   cwd=tmp, capture_output=True, text=True, timeout=timeout, env=env)
                return r.returncode, r.stdout, r.stderr
            except subprocess.TimeoutExpired:
                return 124, "", f"TIMEOUT after {timeout}s"
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def surface(self):
        rc, out, err = self.run(
            "SELECT DISTINCT function_name FROM duckdb_functions() "
            "UNION SELECT name FROM duckdb_settings();")
        if rc != 0:
            print(f"ERROR: cannot query the registered surface: {(err or out).strip()[:500]}", file=sys.stderr)
            sys.exit(2)
        return {l.strip().strip('"') for l in out.splitlines()[1:] if l.strip()}


def error_line(out, err):
    for stream in (err, out):
        for line in stream.splitlines():
            if line.strip() and "Suppressions used" not in line and not line.startswith("---"):
                return line.strip()[:200]
    return ""


def default_duckdb():
    debug = os.path.join(REPO_ROOT, "build", "debug", "duckdb")
    return debug


def default_env():
    sdk = os.path.join(REPO_ROOT, "nwrfcsdk", "linux", "lib")
    env = {}
    if os.path.isdir(sdk):
        env["LD_LIBRARY_PATH"] = ":".join(p for p in (os.environ.get("LD_LIBRARY_PATH", ""), sdk) if p)
    lsan = os.path.join(SCRIPT_DIR, "lsan_suppress.txt")
    if os.path.exists(lsan):
        env["LSAN_OPTIONS"] = f"suppressions={lsan}"
    env["ASAN_OPTIONS"] = "detect_odr_violation=0"
    return env


def live_secret():
    keys = ["ASHOST", "SYSNR", "CLIENT", "USER", "PASSWORD", "LANG"]
    missing = [k for k in keys if not os.environ.get("ERPL_SAP_" + k)]
    if missing:
        print("ERROR: --live needs ERPL_SAP_" + ", ERPL_SAP_".join(missing) + " (see SAP_COMMON_VARS in the Makefile)",
              file=sys.stderr)
        sys.exit(2)
    e = {k: os.environ["ERPL_SAP_" + k].replace("'", "''") for k in keys}
    return (f"CREATE SECRET my_sap (TYPE sap_rfc, ASHOST '{e['ASHOST']}', SYSNR '{e['SYSNR']}', "
            f"CLIENT '{e['CLIENT']}', USER '{e['USER']}', PASSWD '{e['PASSWORD']}', LANG '{e['LANG']}');")


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--names", action="store_true", help="offline name, secret-field and offline-fence check")
    mode.add_argument("--live", action="store_true", help="execute the plugin's SQL fences against SAP")
    ap.add_argument("--duckdb", default=default_duckdb(), help="DuckDB command (default: build/debug/duckdb)")
    ap.add_argument("--preamble", default="", help="SQL run before every block, e.g. 'LOAD erpl;'")
    ap.add_argument("--home", default=None,
                    help="HOME for DuckDB, i.e. where an INSTALLed erpl lives (default: unchanged)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "build", "skill-docs.tsv"))
    ap.add_argument("files", nargs="*", help="restrict the scan to these files")
    args = ap.parse_args()

    first = shlex.split(args.duckdb)[0] if args.duckdb else ""
    if not first or (os.sep in first and not os.path.exists(first)):
        print(f"ERROR: duckdb not found at {args.duckdb} -- build it (GEN=ninja make debug) or pass --duckdb",
              file=sys.stderr)
        return 2
    env_extra = default_env() if args.duckdb == default_duckdb() else {}
    db = DuckDB(args.duckdb, args.preamble, env_extra, args.home)

    full_scan = not args.files
    plugin = plugin_files()
    if args.files:
        files = args.files
    elif args.names:
        files = plugin + [f for f in NAME_SCAN_FILES if os.path.exists(os.path.join(REPO_ROOT, f))]
    else:
        files = plugin

    synopsis = load_keys(SYNOPSIS_FILE)
    expected = load_keys(EXPECTED_ERRORS_FILE)
    removed = load_removed(ALLOWED_NAMES_FILE)
    rows, counts = [], {}
    seen_keys, mentioned = set(), set()

    def add(status, path, line, first, detail=""):
        rows.append((status, path, str(line), first[:100], detail))
        counts[status] = counts.get(status, 0) + 1

    surface = db.surface() if args.names else set()
    secret = live_secret() if args.live else ""

    for path in files:
        text = read(path)
        if args.names:
            reported = set()
            for name, line in name_mentions(path, text):
                mentioned.add(name)
                if name in surface or name in removed or name in reported:
                    continue
                reported.add(name)
                add("FAIL", path, line, "", f"unknown name {name}")

        for lang, body, line in fences(text):
            if lang != "sql":
                continue
            key = f"{path}\t{first_line(body)}"
            seen_keys.add(key)
            head = body.lstrip().splitlines()[0].strip() if body.strip() else ""
            if args.names:
                for stmt in SECRET_RE.findall(body):
                    if not re.search(r"\bTYPE\s+sap_rfc\b", stmt, re.IGNORECASE):
                        continue
                    rc, out, err = db.run(stmt)
                    if rc == 0:
                        add("PASS", path, line, first_line(stmt), "secret fields accepted")
                    else:
                        add("FAIL", path, line, first_line(stmt), error_line(out, err))
                if head == OFFLINE_TAG:
                    rc, out, err = db.run(body)
                    add("PASS" if rc == 0 else "FAIL", path, line, first_line(body),
                        "" if rc == 0 else error_line(out, err))
                continue
            # --live
            if key in synopsis:
                add("SYNOPSIS", path, line, first_line(body))
                continue
            if head == SKIP_LIVE_TAG:
                add("SKIP", path, line, first_line(body), "check:skip-live")
                continue
            # Each fence brings its own secret only when it does not create one itself.
            prefix = "" if SECRET_RE.search(body) else secret + "\n"
            rc, out, err = db.run(prefix + body)
            if key in expected:
                add("EXPECTED-ERROR" if rc != 0 else "FAIL", path, line, first_line(body),
                    "" if rc != 0 else "expected an error but it passed")
            else:
                add("PASS" if rc == 0 else "FAIL", path, line, first_line(body),
                    "" if rc == 0 else error_line(out, err))

    if full_scan:
        for key in sorted((synopsis | expected) - seen_keys):
            add("STALE", key.split("\t", 1)[0], "", "", f"support-file key matches no fence: {key}")
        if args.names:
            for name in sorted(removed):
                if name in surface:
                    add("STALE", "scripts/doc_snippets/allowed_names.txt", "", "", f"{name} is registered now; drop the allowance")
                elif name not in mentioned:
                    add("STALE", "scripts/doc_snippets/allowed_names.txt", "", "", f"{name} is no longer mentioned")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    for r in rows:
        if r[0] in ("FAIL", "STALE"):
            print("\t".join(r))
    summary = ", ".join(f"{v} {k}" for k, v in sorted(counts.items())) or "nothing checked"
    print(f"check_skill_docs ({'names' if args.names else 'live'}): {summary}  [report: {args.out}]")
    return 1 if counts.get("FAIL") or counts.get("STALE") else 0


if __name__ == "__main__":
    sys.exit(main())
