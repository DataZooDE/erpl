"""Pytest fixtures for the sap_read_table parallelism harness.

Loads ABAP Trial credentials and exposes a fresh DuckDB connection
to every test. The debug build statically links the ERPL extensions
so no LOAD is required.
"""
from __future__ import annotations

import json
import os
import pathlib
import subprocess

import duckdb
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DUCKDB_BINARY = REPO_ROOT / "build" / "debug" / "duckdb"
NWRFC_LIB = REPO_ROOT / "nwrfcsdk" / "linux" / "lib"
ARTIFACTS = REPO_ROOT / "artifacts"
ORACLE_DIR = REPO_ROOT / "build" / "oracle"
ADT_CREDS = REPO_ROOT / ".adt.creds"


def _load_credentials() -> dict[str, str]:
    """Load SAP creds from .adt.creds JSON (ADT port) and derive RFC env vars.

    The A4H container exposes the gateway on the host as port 3300 (sapgw00),
    which is SYSNR=00. ASHOST=localhost. The HTTP port in .adt.creds (50000)
    is for ADT, not RFC, but the user/password/client are shared.
    """
    creds = json.loads(ADT_CREDS.read_text())
    return {
        "ERPL_SAP_ASHOST": creds.get("host", "localhost"),
        "ERPL_SAP_SYSNR": "00",
        "ERPL_SAP_CLIENT": creds.get("client", "001"),
        "ERPL_SAP_USER": creds.get("user", "DEVELOPER"),
        "ERPL_SAP_PASSWORD": creds.get("password", ""),
        "ERPL_SAP_LANG": "EN",
    }


@pytest.fixture(scope="session")
def sap_env() -> dict[str, str]:
    env = _load_credentials()
    for k, v in env.items():
        os.environ.setdefault(k, v)
    # Inject the SAP shared libs so the static-linked extension can resolve them.
    nwrfc = str(NWRFC_LIB)
    current = os.environ.get("LD_LIBRARY_PATH", "")
    if nwrfc not in current.split(":"):
        os.environ["LD_LIBRARY_PATH"] = nwrfc + (":" + current if current else "")
    return env


@pytest.fixture
def con(sap_env: dict[str, str]):
    """In-process DuckDB connection with the debug-build extensions loaded."""
    # The python duckdb module loads its own DuckDB; it cannot use the
    # statically-linked debug binary. We use the CLI binary via subprocess for
    # SAP-side calls (see `cli` fixture) and the python module only for
    # local-only operations (parquet IO, hashing oracles).
    c = duckdb.connect(":memory:", config={"allow_unsigned_extensions": True})
    yield c
    c.close()


@pytest.fixture(scope="session")
def cli_run(sap_env: dict[str, str]):
    """Run a SQL script through the debug duckdb CLI and return stdout.

    The CLI is the only way to get the statically-linked ERPL extension. The
    secret is created inline so each invocation is self-contained.
    """
    secret_sql = f"""
CREATE OR REPLACE SECRET abap_trial (
    TYPE sap_rfc,
    ASHOST '{sap_env["ERPL_SAP_ASHOST"]}',
    SYSNR '{sap_env["ERPL_SAP_SYSNR"]}',
    CLIENT '{sap_env["ERPL_SAP_CLIENT"]}',
    USER '{sap_env["ERPL_SAP_USER"]}',
    PASSWD '{sap_env["ERPL_SAP_PASSWORD"]}',
    LANG '{sap_env["ERPL_SAP_LANG"]}'
);
"""

    # Suppress the ASan/LSan noise from libsapnwrfc.so static init.
    env_extra = {
        "LSAN_OPTIONS": f"suppressions={REPO_ROOT / 'scripts' / 'lsan_suppress.txt'}:print_suppressions=0",
        "ASAN_OPTIONS": "detect_odr_violation=0:detect_leaks=0",
    }

    def _run(sql: str, *, timeout: int = 600, retries: int = 2) -> str:
        full = secret_sql + sql
        last_err = ""
        for attempt in range(retries + 1):
            proc = subprocess.run(
                [str(DUCKDB_BINARY), "-csv", "-c", full],
                capture_output=True,
                text=True,
                timeout=timeout,
                env={**os.environ, **env_extra},
            )
            if proc.returncode == 0:
                return proc.stdout
            stderr = proc.stderr or ""
            transient = (
                "RFC_COMMUNICATION_FAILURE" in stderr
                or "Timeout while opening an RFC connection" in stderr
            )
            last_err = (
                f"duckdb cli failed (rc={proc.returncode}, attempt {attempt + 1}/{retries + 1})\n"
                f"--- stdout ---\n{proc.stdout}\n"
                f"--- stderr ---\n{stderr}\n"
                f"--- sql ---\n{full}"
            )
            if not transient or attempt == retries:
                break
        raise RuntimeError(last_err)

    return _run


@pytest.fixture(scope="session")
def artifacts_dir() -> pathlib.Path:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    return ARTIFACTS


@pytest.fixture(scope="session")
def oracle_dir() -> pathlib.Path:
    ORACLE_DIR.mkdir(parents=True, exist_ok=True)
    return ORACLE_DIR
