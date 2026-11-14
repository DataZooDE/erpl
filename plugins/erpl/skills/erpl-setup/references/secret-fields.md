# `sap_rfc` secret fields

All values are VARCHAR. The connection fields are handed to the SAP NetWeaver RFC SDK's
`RfcOpenConnection` unchanged, so SAP's documentation for that call applies.

| Field | Meaning | Direct | Load-balanced |
|---|---|:---:|:---:|
| `ashost` | Application server host | required | — |
| `sysnr` | System number (`00`–`99`; gateway port 33NN) | required | — |
| `mshost` | Message server host | — | required |
| `msserv` | Message server service/port | — | optional |
| `sysid` | System ID, e.g. `PRD` | — | required |
| `group` | Logon group, e.g. `PUBLIC` | — | required |
| `client` | Client, e.g. `100` | required | required |
| `user` | SAP user | required | required |
| `passwd` | Password (redacted) | required¹ | required¹ |
| `lang` | Logon language, e.g. `EN` | optional | optional |
| `snc_mode` | `1` turns SNC on | optional | optional |
| `snc_sso` | `1` logs on with the SNC identity | optional | optional |
| `snc_qop` | SNC quality of protection (`1`–`9`) | optional | optional |
| `snc_myname` | Own SNC name | optional | optional |
| `snc_partnername` | SAP server's SNC name | optional | optional |
| `snc_lib` | Path to the SNC/GSS library | optional | optional |
| `mysapsso2` | SSO2 logon ticket (redacted) | optional | optional |
| `x509cert` | X.509 certificate for logon (redacted) | optional | optional |
| `saprouter` | SAProuter string, e.g. `/H/router/S/3299` | optional | optional |
| `gwhost`, `gwserv` | Explicit gateway host/service | optional | optional |
| `codepage` | Logon codepage, e.g. `4103` | optional | optional |
| `trace` | SDK trace level `0`–`3` (writes `dev_rfc*.trc`, contains data) | optional | optional |
| `dest` | Destination name in `sapnwrfc.ini`; the file supplies the rest | optional | optional |
| `read_table_function` | Function module behind table reads for this secret, e.g. `/SAPDS/RFC_READ_TABLE2` | optional | optional |
| `read_table_delimiter` | One printable ASCII delimiter for table reads, e.g. `~` | optional | optional |

¹ Not needed when SNC (`snc_sso`) or an SSO2 ticket logs on.

## WebSocket RFC (wsRFC)

S/4HANA Cloud has no RFC gateway; RFC goes over a TLS WebSocket instead. On-premise systems offer it on the
ICM's wss port. `wshost` (or `use_tls 'true'` with `ashost`) selects it.

| Field | Meaning | Backend |
|---|---|---|
| `wshost` | WebSocket RFC host | both |
| `wsport` | TLS port (443 on a tenant) | both |
| `use_tls` | `'true'`: wsRFC against `ashost` | both |
| `tls_client_pse` | Client PSE with certificate and key | `nwrfc` only |
| `tls_trust_all` | `'true'` skips server certificate checks — never in production | `nwrfc` only |
| `alias_user` | Communication-user alias the certificate maps to | `nwrfc` only |
| `ws_client_cert` | Client certificate file (PEM/DER) | `proto` only |
| `ws_client_key` | Matching PKCS#8 key file (PEM/DER) | `proto` only |

A field the active backend cannot honour is refused, not ignored. Over wsRFC the credential precedence is
ticket, then password, then certificate; a certificate given together with a password secures only the TLS
layer.
