# AGENTS.md

## Purpose

This file defines how humans and coding agents work in this repository. Product behavior, supported features, security policy, and deferred scope belong in [`FEATURES.md`](FEATURES.md). The current implementation is described in [`docs/architecture.md`](docs/architecture.md), and design decisions are recorded in [`docs/adr/`](docs/adr/).

Read the relevant specification, architecture, and decisions before making architectural, security-sensitive, or externally visible changes.

## Quality standard

Treat this as security-critical PostgreSQL infrastructure. Optimize for correctness, explicit trust boundaries, operational predictability, maintainability, and reviewability—not implementation speed or feature count.

Use these defaults:

- Fail closed on invalid input, incomplete configuration, unavailable required dependencies, and internal errors.
- Prefer small, cohesive changes with tests over broad rewrites.
- Make security policy explicit in configuration and typed interfaces; do not hide it in library defaults.
- Keep behavior deterministic and resource-bounded.
- Preserve backward compatibility only when it does not weaken security. Document intentional incompatibilities and migration steps.
- Do not claim production readiness from unit tests alone.

If a requested change conflicts with `FEATURES.md` or weakens a security invariant, stop and explain the conflict. Update the specification in a reviewable pull request before changing the implementation contract.

## Sources and evidence

Do not guess PostgreSQL APIs, OAuth/JOSE semantics, provider behavior, or library guarantees.

- Verify PostgreSQL integration details against the exact supported PostgreSQL source or official documentation.
- Use applicable RFCs as the primary source for protocol behavior.
- Verify provider exceptions against current official provider documentation and bind them to a named profile.
- Verify third-party library behavior with its documentation and focused boundary tests.
- Add concise source references in code comments when they explain a non-obvious constraint. Comments should explain why, not restate code.

Record important architectural and security-policy decisions in [`docs/adr/`](docs/adr/).

## Change workflow

Before editing:

1. Read the relevant specification, architecture, and decision records.
2. Inspect nearby implementation, tests, build definitions, and repository-local instructions.
3. Identify the trust boundary, failure behavior, and milestone affected.
4. Check the working tree and preserve unrelated user changes.
5. Surface assumptions that would alter public behavior or security policy; do not silently decide unresolved product questions.

While editing:

1. Implement the smallest complete vertical slice.
2. Keep PostgreSQL integration glue thin and isolate testable domain logic.
3. Add or update tests in the same change.
4. Keep refactoring separate from behavior changes when practical.
5. Handle every error path deliberately; never convert an error into authorization success.
6. Use stable, non-sensitive diagnostic codes and structured internal results.
7. Avoid dead code, speculative abstractions, and later-milestone scaffolding without a current use.

Before declaring work complete:

1. Run the narrowest relevant tests, then every configured local quality gate.
2. Confirm denial behavior and secret/token redaction on failure paths.
3. Run formatting, strict compiler warnings, static analysis, and sanitizers when configured.
4. Check documentation, configuration examples, upgrade implications, and rollback behavior.
5. Review the diff for accidental generated files, secrets, unrelated edits, and weakened tests.
6. Report exactly what ran, what passed, and what could not run. Never invent results.

## Code design guidelines

- Prefer explicit data flow and typed results over shared mutable state, implicit globals, or exceptions for expected validation failures.
- Separate parsing, cryptographic verification, claim validation, identity derivation, authorization, I/O, caching, and PostgreSQL callback adaptation.
- Treat all external bytes and configuration as untrusted until validated.
- Validate at boundaries and preserve a distinction between malformed, invalid, unavailable, and unauthorized states internally.
- Bound input size, collection size, recursion/nesting, allocation, CPU work, network time, retry count, and cache growth.
- Avoid network operations while holding PostgreSQL locks.
- Keep time and network dependencies injectable so boundary and outage behavior can be tested deterministically.
- Initialize callback outputs to denial before work begins.
- Follow PostgreSQL memory-context and error-handling contracts. Contain C++ exceptions and PostgreSQL `longjmp` behavior at reviewed boundaries.
- Use established cryptographic implementations. Never implement cryptographic primitives.
- Never rely on a dependency's default security policy without enforcing and testing this project's policy explicitly.

## Testing policy

Testing must follow a pyramid with strong negative coverage:

1. **Unit tests:** pure parsing, policy, claim, identity, configuration, and cache-state behavior.
2. **Component tests:** JOSE/crypto library integration, HTTP constraints, discovery/JWKS handling, refresh, and redaction.
3. **PostgreSQL integration tests:** load the real module in supported PostgreSQL versions and exercise real connection authentication, HBA rules, usermaps, reloads, failures, and concurrency.
4. **Interoperability tests:** versioned provider environments or sanitized fixtures, kept separate from deterministic offline tests.
5. **Fuzz and robustness tests:** token, Base64URL, JSON, JWK, metadata, scope, identity, and configuration parsers.

For every validation rule, include:

- an accepted case;
- missing and wrong-type cases where applicable;
- boundary values;
- relevant adversarial cases;
- assertions about the authorization result and safe diagnostics.

For a security defect, add a regression test that fails before the fix whenever practical. Never delete or weaken a negative test merely to accommodate a behavior change. A deliberate policy change requires a specification update and migration impact review.

Tests must be deterministic and independent by default. Do not require public network access, wall-clock timing luck, test ordering, or shared mutable provider state. Use controlled clocks, local fixtures, and bounded local test servers. Tests involving live providers must be explicitly opt-in and must not expose credentials or tokens.

Measure coverage to find untested security paths, not as a substitute for thoughtful cases. Once thresholds are introduced, do not reduce them without a documented reason.

## Build and dependency discipline

- Builds and tests must be reproducible from a clean checkout.
- Treat compiler warnings as errors for project code in CI.
- Use checked-in formatting and static-analysis configuration.
- Minimize dependencies, especially in the authentication path.
- Before adding a dependency, assess maintenance, license, release cadence, vulnerability history, transitive footprint, PostgreSQL compatibility, parser limits, thread/process behavior, and cryptographic capabilities.
- Pin or constrain dependencies with a reviewable update mechanism and enable automated dependency-update PRs after the build system is established.
- Track generated files only when necessary. Document and CI-check the reproducible generation command.
- Do not commit credentials, bearer tokens, private keys, production metadata, or unsanitized provider responses. Secret-scanning findings must block merges.

## GitHub CI and merge gates

Establish GitHub Actions with least-privilege permissions, pinned third-party actions, concurrency cancellation, dependency caching keyed by lock/build inputs, and explicit timeouts. Pull-request CI must not receive production secrets or run untrusted code with privileged tokens.

The initial CI should include these required jobs once the build scaffold exists:

- formatting and generated-file consistency;
- clean build with strict warnings;
- unit and component tests;
- PostgreSQL integration tests against PostgreSQL 18 and the current PostgreSQL 19 prerelease;
- static analysis;
- dependency and license audit;
- secret scanning;
- sanitizer builds (at least AddressSanitizer and UndefinedBehaviorSanitizer where supported).

Add, as the project matures:

- a compatibility matrix for every supported PostgreSQL major version;
- ThreadSanitizer or dedicated concurrency testing for shared-cache code where feasible;
- scheduled fuzzing and longer-running robustness tests;
- CodeQL or equivalent semantic analysis;
- a scheduled provider interoperability suite with isolated credentials;
- release builds, artifact provenance, SBOM generation, and signed checksums.

CI should use a minimal `permissions` block, pin external actions by full commit SHA, set job timeouts, and avoid `pull_request_target` for building contributor code. Changes to workflows receive the same review as authentication code under the version-specific policy below.

Do not merge when a required check is failing, skipped unexpectedly, or flaky. Fix or quarantine flakes with an owner and tracked rationale; do not normalize rerunning until green.

## Review expectations

Every pull request should state:

- why the change is needed and its user-visible or security effect;
- tests run and any required checks that could not run;
- rollout and rollback notes when the change affects operations.

For `0.x` prereleases while there is one maintainer, the maintainer may review
and merge their own pull request after all required checks pass. For
security-sensitive code, dependencies, workflows, or release inputs, record a
brief self-review of the final diff, denial behavior, test gaps, and secret
exposure in the pull request. External review is welcome but is not a merge or
publication requirement before `v1.0.0`.

Starting with `v1.0.0`, security-sensitive changes and release approval require
review by someone other than the author. Do not claim that self-review is
independent review. Prefer reviewable commits and keep unrelated dependency
upgrades, formatting sweeps, and security behavior changes separate.

## Documentation

Update documentation in the same change as behavior. Use precise OAuth and PostgreSQL terminology. Clearly distinguish access tokens, issuers, audiences, scopes, subjects, authenticated identities, requested PostgreSQL roles, and role mappings.

Operator-facing behavior must document secure defaults, configuration validation, logging/redaction, cache and outage behavior, reload semantics, compatibility limits, and upgrade/rollback steps.

## Canonical commands and documentation ownership

Use [docs/development.md](docs/development.md) for prerequisites, canonical build
and test commands, and CI parity. PostgreSQL 18/19 builds use separate trees;
other majors are rejected. [docs/release-readiness.md](docs/release-readiness.md)
defines additional publication gates.

Maintain each topic in its designated reference linked from the README. Keep the
README tutorial runnable and concise; link to detailed settings and procedures
instead of repeating them. ADRs record decisions and rationale, not incremental
implementation reports. Dated release/security evidence records the reviewed
inputs and limits at that time; do not keep old results synchronized with the
current tree. Git history retains superseded development narratives.
