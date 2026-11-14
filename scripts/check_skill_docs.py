#!/usr/bin/env python3
"""scripts/check_skill_docs.py -- keep the Claude Code plugin and the docs honest.

The erpl plugin (plugins/erpl/) teaches an AI assistant the SQL surface. A skill that names
a function, option, skill or command that does not exist sends the assistant off writing
SQL that fails, so every name is checked against what a build actually registers and what
the plugin actually ships.

Two modes:

  --names   (offline, no SAP system; runs in CI after the smoke test)
            1. Every identifier shaped like an erpl SQL name -- sap_*, tunnel_*, erpl_* -- that
               a scanned file mentions in a backtick span, in call syntax `name(`, after
               PRAGMA/SET, or anywhere in a SKILL.md/command frontmatter, must be a
               registered function, pragma or setting.
            2. Every skill (`erpl-...`) and command (`/erpl:...`) a plugin file or README.md
               names in backticks must exist under plugins/erpl/.
            3. Every CREATE SECRET of TYPE sap_rfc in a ```sql fence, and every field listed
               in the secret-field reference, must be accepted by the sap_rfc secret type
               (checked by creating the secret in :memory:, which needs no SAP).
            4. Every ```sql fence whose first line is `-- check:offline` must run.
            Exceptions -- parameter and table names, other plugins' skills, other
            extensions' functions -- are listed with a reason in
            scripts/doc_snippets/allowed_names.txt (a trailing `*` matches a prefix).

  --live    (needs SAP: the a4h trial, credentials from ERPL_SAP_* like the SQL suites)
            Executes every ```sql fence of the plugin files, each in a fresh in-memory
            database. Unless the fence creates a secret itself, a secret named `my_sap` --
            the name the docs use -- is created first from ERPL_SAP_*. Skipped: fences keyed
            in scripts/doc_snippets/synopsis.txt (placeholders, network installs, example
            hosts) and fences starting with `-- check:skip-live` (destructive or
            system-specific). A fence keyed in scripts/doc_snippets/expected_errors.txt must
            fail.

Illustrative or deliberately broken SQL belongs in a ```text fence, which is never run.
Keys in synopsis.txt / expected_errors.txt are `<file>\\t<first non-comment line>`; a key that
matches two fences of one file is an error, because the entry would silence both.

Documented CREATE PERSISTENT SECRETs never reach the real ~/.duckdb/stored_secrets: every
block runs with `secret_directory` set to a throwaway directory.

The DuckDB to use is `--duckdb CMD` (shell-split, so `arch -x86_64 /path/duckdb` works) plus
`--preamble SQL` run before every block. Default: the static debug build
(build/debug/duckdb, every extension linked in, no LOAD) with the SAP SDK on
LD_LIBRARY_PATH. Against a release artifact pass its CLI, `--preamble "LOAD erpl;"` and
`--home` pointing at where it was INSTALLed.

Writes a TSV report (status, file, line, first line, detail) to --out (default
build/skill-docs.tsv). Exit: 0 clean, 1 on FAIL or STALE, 2 when DuckDB cannot be run.
"""
import argparse
import fnmatch
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
ALLOWED_NAMES_REL = os.path.relpath(ALLOWED_NAMES_FILE, REPO_ROOT)

PLUGIN_DIR = os.path.join("plugins", "erpl")
SKILLS_DIR = os.path.join(PLUGIN_DIR, "skills")
COMMANDS_DIR = os.path.join(PLUGIN_DIR, "commands")
# Repo docs whose SQL names are checked alongside the plugin: the ones a user (and an
# assistant) reads as the reference.
NAME_SCAN_FILES = ["README.md", "API_REFERENCE.md", "TROUBLESHOOTING.md"]
# Repo docs whose skill and command references are checked (the plugin's own files always are).
REFERENCE_SCAN_FILES = ["README.md"]
# A table whose first column lists sap_rfc secret fields in backticks.
SECRET_FIELD_DOC = os.path.join(SKILLS_DIR, "erpl-setup", "references", "secret-fields.md")

FENCE_RE = re.compile(r"^```[ \t]*([A-Za-z0-9_+-]*)[ \t]*$")
NAME = r"(?:sap|tunnel|erpl)_[a-z0-9_]*[a-z0-9]"
BACKTICK_RE = re.compile(r"`([^`\n]*)`")
NAME_IN_SPAN_RE = re.compile(r"\b(" + NAME + r")\b", re.IGNORECASE)
# A name followed by "(" is a call -- unless it is being declared: `ATTACH '' AS sap_x (`,
# `CREATE SECRET sap_x (`, `CREATE TABLE sap_x (`.
CALL_RE = re.compile(r"(?:\b(AS|SECRET|TABLE|VIEW|MACRO|EXISTS)\s+)?\b(" + NAME + r")\s*\(", re.IGNORECASE)
PRAGMA_SET_RE = re.compile(r"\b(?:PRAGMA|SET|RESET)\s+(?:GLOBAL\s+|SESSION\s+)?(" + NAME + r")\b", re.IGNORECASE)
SKILL_REF_RE = re.compile(r"^erpl-[a-z0-9]+(?:-[a-z0-9]+)*$")
COMMAND_REF_RE = re.compile(r"^/erpl:([a-z0-9]+(?:-[a-z0-9]+)*)")
# Words shaped like erpl SQL names that are not part of the SQL surface.
NOT_SURFACE = {
    "sap_rfc",  # the secret type and the ATTACH TYPE
    "erpl_rfc", "erpl_bics", "erpl_odp", "erpl_ape", "erpl_tunnel",  # extension names
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
    for base, dirs, files in os.walk(os.path.join(REPO_ROOT, PLUGIN_DIR)):
        dirs.sort()
        out += [os.path.relpath(os.path.join(base, fn), REPO_ROOT) for fn in sorted(files) if fn.endswith(".md")]
    return out


def shipped_skills():
    root = os.path.join(REPO_ROOT, SKILLS_DIR)
    if not os.path.isdir(root):
        return set()
    return {d for d in os.listdir(root) if os.path.exists(os.path.join(root, d, "SKILL.md"))}


def shipped_commands():
    root = os.path.join(REPO_ROOT, COMMANDS_DIR)
    if not os.path.isdir(root):
        return set()
    return {fn[:-3] for fn in os.listdir(root) if fn.endswith(".md")}


def load_keys(path):
    if not os.path.exists(path):
        return set()
    with open(path, encoding="utf-8") as f:
        return {l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")}


def load_allowed(path):
    """name<TAB>reason per line; a trailing * makes the name a prefix pattern."""
    names = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.strip() and not line.startswith("#"):
                    name, _, reason = line.rstrip("\n").partition("\t")
                    names[name.strip().lower()] = reason.strip()
    return names


def is_allowed(name, allowed):
    return any(name == a or (a.endswith("*") and fnmatch.fnmatchcase(name, a)) for a in allowed)


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


def calls(text):
    return [name for declared, name in CALL_RE.findall(text) if not declared]


def scan(text):
    """(sql_names, references): (name, line) pairs for erpl-shaped SQL identifiers, and
    (kind, name, line) for backticked skill and command references outside fences."""
    lines = text.splitlines()
    fm_end = frontmatter_end(lines)
    in_fence, fence_lang = False, ""
    names, refs = [], []
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
            names += [(n, i + 1) for n in NAME_IN_SPAN_RE.findall(line)]
            continue
        if in_fence:
            # Inside SQL every call and PRAGMA/SET counts; ```text and others are prose.
            if fence_lang == "sql":
                code = line.split("--", 1)[0]
                names += [(n, i + 1) for n in calls(code) + PRAGMA_SET_RE.findall(code)]
            continue
        for span in BACKTICK_RE.findall(line):
            names += [(n, i + 1) for n in NAME_IN_SPAN_RE.findall(span)]
            token = span.strip()
            if SKILL_REF_RE.match(token):
                refs.append(("skill", token, i + 1))
            cm = COMMAND_REF_RE.match(token)
            if cm:
                refs.append(("command", cm.group(1), i + 1))
        names += [(n, i + 1) for n in calls(line)]
    names = [(n.lower(), l) for n, l in names]
    return [(n, l) for n, l in names if n not in NOT_SURFACE], refs


def secret_fields(text):
    """Backticked names in the first column of the field tables."""
    fields = []
    for i, line in enumerate(text.splitlines()):
        if line.startswith("|") and not line.startswith("|---"):
            cell = line.split("|")[1]
            fields += [(f, i + 1) for f in BACKTICK_RE.findall(cell) if re.fullmatch(r"[a-z0-9_]+", f)]
    return fields


class DuckDB:
    def __init__(self, cmd, preamble, env_extra, home):
        self.cmd = cmd
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
            env = dict(self.env, HOME=self.home) if self.home else self.env
            try:
                r = subprocess.run(self.cmd + ["-unsigned", "-bail", "-csv", ":memory:"], input=full,
                                   cwd=tmp, capture_output=True, text=True, timeout=timeout, env=env)
                return r.returncode, r.stdout, r.stderr
            except subprocess.TimeoutExpired:
                return 124, "", f"TIMEOUT after {timeout}s"
            except OSError as e:
                print(f"ERROR: cannot run {' '.join(self.cmd)}: {e}", file=sys.stderr)
                sys.exit(2)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def surface(self):
        rc, out, err = self.run(
            "SELECT DISTINCT function_name FROM duckdb_functions() "
            "UNION SELECT name FROM duckdb_settings();")
        if rc != 0:
            print(f"ERROR: cannot query the registered surface: {(err or out).strip()[:500]}", file=sys.stderr)
            sys.exit(2)
        return {l.strip().strip('"').lower() for l in out.splitlines()[1:] if l.strip()}


def error_line(out, err):
    for stream in (err, out):
        for line in stream.splitlines():
            if line.strip() and "Suppressions used" not in line and not line.startswith("---"):
                return line.strip()[:200]
    return ""


DEBUG_DUCKDB = os.path.join(REPO_ROOT, "build", "debug", "duckdb")


def debug_env():
    """What scripts/start-duckdb-debug.sh sets for the ASAN debug binary."""
    sdk = os.path.join(REPO_ROOT, "nwrfcsdk", "linux", "lib")
    env = {"ASAN_OPTIONS": "detect_odr_violation=0"}
    if os.path.isdir(sdk):
        env["LD_LIBRARY_PATH"] = ":".join(p for p in (os.environ.get("LD_LIBRARY_PATH", ""), sdk) if p)
    lsan = os.path.join(SCRIPT_DIR, "lsan_suppress.txt")
    if os.path.exists(lsan):
        env["LSAN_OPTIONS"] = f"suppressions={lsan}"
    return env


def resolve_cmd(cmd):
    """Shell-split the command and make a path-like executable absolute: blocks run with a
    temp dir as cwd, so a relative ./build/... would not be found there."""
    parts = shlex.split(cmd)
    for i, p in enumerate(parts):
        if os.sep in p and not p.startswith("-"):
            parts[i] = os.path.abspath(p)
    return parts


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
    mode.add_argument("--names", action="store_true", help="offline name, reference, secret-field and offline-fence check")
    mode.add_argument("--live", action="store_true", help="execute the plugin's SQL fences against SAP")
    ap.add_argument("--duckdb", default=DEBUG_DUCKDB, help="DuckDB command (default: build/debug/duckdb)")
    ap.add_argument("--preamble", default="", help="SQL run before every block, e.g. 'LOAD erpl;'")
    ap.add_argument("--home", default=None,
                    help="HOME for DuckDB, i.e. where an INSTALLed erpl lives (default: unchanged)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "build", "skill-docs.tsv"))
    ap.add_argument("files", nargs="*", help="restrict the scan to these files (repo-relative)")
    args = ap.parse_args()

    cmd = resolve_cmd(args.duckdb) if args.duckdb else []
    exe = next((p for p in cmd if os.path.isabs(p)), cmd[0] if cmd else "")
    if not exe or (os.path.isabs(exe) and not os.path.exists(exe)):
        print(f"ERROR: duckdb not found at {args.duckdb} -- build it (GEN=ninja make debug) or pass --duckdb",
              file=sys.stderr)
        return 2
    is_debug = os.path.realpath(exe) == os.path.realpath(DEBUG_DUCKDB)
    db = DuckDB(cmd, args.preamble, debug_env() if is_debug else {}, args.home)
    out_path = os.path.abspath(args.out)

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
    allowed = load_allowed(ALLOWED_NAMES_FILE)
    skills, commands = shipped_skills(), shipped_commands()
    rows, counts = [], {}
    seen_keys, mentioned = set(), set()
    names_checked = 0

    def add(status, path, line, head, detail=""):
        rows.append((status, path, str(line), head[:100], detail))
        counts[status] = counts.get(status, 0) + 1

    surface = db.surface() if args.names else set()
    secret = live_secret() if args.live else ""

    for path in files:
        text = read(path)
        if args.names:
            names, refs = scan(text)
            reported = set()
            for name, line in names:
                names_checked += 1
                mentioned.add(name)
                if name in surface or is_allowed(name, allowed) or name in reported:
                    continue
                reported.add(name)
                add("FAIL", path, line, "", f"unknown name {name}")
            if path.startswith(PLUGIN_DIR + os.sep) or path in REFERENCE_SCAN_FILES:
                for kind, name, line in refs:
                    names_checked += 1
                    mentioned.add(name)
                    exists = name in (skills if kind == "skill" else commands)
                    if exists or is_allowed(name, allowed) or name in reported:
                        continue
                    reported.add(name)
                    where = SKILLS_DIR if kind == "skill" else COMMANDS_DIR
                    add("FAIL", path, line, "", f"{kind} {name} does not exist under {where}")
            if path == SECRET_FIELD_DOC:
                fields = secret_fields(text)
                if fields:
                    stmt = "CREATE SECRET field_check (TYPE sap_rfc, " + ", ".join(
                        f"{f} 'x'" for f in dict.fromkeys(f for f, _ in fields)) + ");"
                    rc, out, err = db.run(stmt)
                    add("PASS" if rc == 0 else "FAIL", path, fields[0][1], "secret field table",
                        f"{len(fields)} fields accepted" if rc == 0 else error_line(out, err))

        file_keys = set()
        for lang, body, line in fences(text):
            if lang != "sql":
                continue
            key = f"{path}\t{first_line(body)}"
            if key in file_keys and key in (synopsis | expected):
                add("FAIL", path, line, first_line(body),
                    "another sql fence of this file starts with the same line; its support-file key covers both")
            file_keys.add(key)
            seen_keys.add(key)
            head = body.lstrip().splitlines()[0].strip() if body.strip() else ""
            if args.names:
                for stmt in SECRET_RE.findall(body):
                    if not re.search(r"\bTYPE\s+sap_rfc\b", stmt, re.IGNORECASE):
                        continue
                    rc, out, err = db.run(stmt)
                    add("PASS" if rc == 0 else "FAIL", path, line, first_line(stmt),
                        "secret fields accepted" if rc == 0 else error_line(out, err))
                if head.startswith(OFFLINE_TAG):
                    rc, out, err = db.run(body)
                    add("PASS" if rc == 0 else "FAIL", path, line, first_line(body),
                        "" if rc == 0 else error_line(out, err))
                continue
            # --live
            if key in synopsis:
                add("SYNOPSIS", path, line, first_line(body))
                continue
            if head.startswith(SKIP_LIVE_TAG):
                add("SKIP", path, line, first_line(body), "check:skip-live")
                continue
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
            for name in sorted(allowed):
                if name in surface or name in skills or name in commands:
                    add("STALE", ALLOWED_NAMES_REL, "", "", f"{name} exists now; drop the allowance")
                elif not any(is_allowed(m, {name: ""}) for m in mentioned):
                    add("STALE", ALLOWED_NAMES_REL, "", "", f"{name} is no longer mentioned")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")
    for r in rows:
        if r[0] in ("FAIL", "STALE"):
            print("\t".join(r))
    parts = [f"{v} {k}" for k, v in sorted(counts.items())]
    if args.names:
        parts.insert(0, f"{names_checked} name mentions")
    print(f"check_skill_docs ({'names' if args.names else 'live'}): {', '.join(parts) or 'nothing checked'}"
          f"  [report: {out_path}]")
    return 1 if counts.get("FAIL") or counts.get("STALE") else 0


if __name__ == "__main__":
    sys.exit(main())
