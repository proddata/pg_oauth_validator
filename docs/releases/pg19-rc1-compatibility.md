# PostgreSQL 19 RC1 compatibility check

- **Review date:** 2026-10-18
- **PostgreSQL package:** `19~rc1-1.pgdg12+1`
- **Container:** `postgres:19rc1-bookworm@sha256:228dc3e5a9bd18343efeecc7ae3f52aee9b6cf3a9dca5159ece1eef254a80f06`
- **Debian snapshot:** `20261005T000000Z`
- **Architecture tested locally:** `aarch64`
- **Status:** preview-only compatibility

The [PostgreSQL 19 RC1 announcement](https://www.postgresql.org/about/news/postgresql-19-rc-1-released-3409/)
identifies this as a release candidate. Both the RC1 and beta 4 official source
archives passed their published SHA-256 checks. The OAuth validator callback
header, OAuth authentication implementation, HBA implementation and header,
OAuth common header, and libpq OAuth client source files are byte-for-byte
unchanged between beta 4 and RC1.

The pinned development image built with the exact PostgreSQL development
package and Debian snapshot above. `docker compose -f compose.test.yml run
--rm pg19 dev-test-pg19` completed the strict C17 build, unit and component
checks, and real PostgreSQL integration suite (**24 passed**).

The release artifact validation checks, AddressSanitizer and
UndefinedBehaviorSanitizer component checks, fuzz smoke suite, and Clang static
analysis also passed locally; the analyzer reported no bugs. This is local
compatibility evidence. It does not replace the CI matrix, packaging,
interoperability, or independent review gates for a release.
PostgreSQL 19 remains preview-only until the final-release gates in
[`../release-readiness.md`](../release-readiness.md) pass.
