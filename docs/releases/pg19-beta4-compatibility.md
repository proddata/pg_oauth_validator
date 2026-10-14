# PostgreSQL 19 Beta 4 compatibility check

- **Review date:** 2026-10-14
- **PostgreSQL package:** `19~beta4-1.pgdg12+1`
- **Container:** `postgres:19beta4-bookworm@sha256:4c618d9a30e0b88078b9371a0499d7055da9ab7f5afe386a460758eac9857f06`
- **Debian snapshot:** `20261005T000000Z`
- **Architecture tested locally:** `aarch64`
- **Status:** preview-only compatibility

The [PostgreSQL 19 Beta 4 announcement](https://www.postgresql.org/about/news/postgresql-19-beta-4-released-3386/)
identifies this as a preview release. The earlier
[Beta 3 compatibility record](pg19-beta3-compatibility.md) remains historical
evidence for that version.

The pinned development image built with the exact PostgreSQL development
package and Debian snapshot above. `docker compose -f compose.test.yml run
--rm pg19` completed the strict C17 build, unit and component checks, and real
PostgreSQL integration suite (**23 passed**). The PostgreSQL 19 playground
image also built with the matching `libpq-oauth` package.

These checks ran against the combined identity and image changes before commit.
They establish local build and authentication-test compatibility on that source.
They do not replace the CI matrix, sanitizer,
static-analysis, packaging, interoperability, or independent review gates for
a release.
