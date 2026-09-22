MODULE_big = pg_oauth_validator
DOCS = README.md docs/operations.md PROVIDER-COMPATIBILITY.md LICENSE \
	THIRD-PARTY-NOTICES.md
OBJS = src/pg_oauth_validator.o src/config.o src/hba_policy.o src/policy.o \
	src/cache_state.o src/cache_key.o src/shared_cache.o src/http_freshness.o \
	src/http_transport.o src/metadata.o src/jwks.o src/base64url.o \
	src/jwt_envelope.o src/signature.o src/claims.o src/identity.o \
	src/issuer_key.o src/validator.o
TEST_BIN = tests/fail_closed_test
POLICY_TEST_BIN = tests/policy_test
JWT_ENVELOPE_TEST_BIN = tests/jwt_envelope_test
JWKS_TEST_BIN = tests/jwks_test
SIGNATURE_TEST_BIN = tests/signature_test
CLAIMS_TEST_BIN = tests/claims_test
IDENTITY_TEST_BIN = tests/identity_test
METADATA_TEST_BIN = tests/metadata_test
HTTP_TRANSPORT_TEST_BIN = tests/http_transport_test
ISSUER_KEY_TEST_BIN = tests/issuer_key_test
VALIDATOR_TEST_BIN = tests/validator_test
CACHE_STATE_TEST_BIN = tests/cache_state_test
CACHE_KEY_TEST_BIN = tests/cache_key_test
HTTP_FRESHNESS_TEST_BIN = tests/http_freshness_test
OAUTH_TEST_CLIENT = tests/integration/oauth_test_client
OAUTH_TEST_CLIENT_PATH = $(CURDIR)/$(OAUTH_TEST_CLIENT)
CACHE_PROBE = tests/integration/cache_probe$(DLSUFFIX)
CACHE_PROBE_PATH = $(CURDIR)/$(CACHE_PROBE)
VALIDATOR_LIBRARY ?= $(CURDIR)/$(MODULE_big)$(DLSUFFIX)
LIBJWT_SPIKE_BIN = tests/dependency/libjwt_spike
LIBJWT_FUZZ_BIN = tests/fuzz/libjwt_inputs_fuzz
JWT_ENVELOPE_FUZZ_BIN = tests/fuzz/jwt_envelope_fuzz
JWKS_FUZZ_BIN = tests/fuzz/jwks_fuzz
CLAIMS_FUZZ_BIN = tests/fuzz/claims_fuzz
IDENTITY_FUZZ_BIN = tests/fuzz/identity_fuzz
METADATA_FUZZ_BIN = tests/fuzz/metadata_fuzz
LIBJWT_FUZZ_CORPUS = .fuzz-corpus
JWT_ENVELOPE_FUZZ_CORPUS = .jwt-envelope-fuzz-corpus
JWKS_FUZZ_CORPUS = .jwks-fuzz-corpus
CLAIMS_FUZZ_CORPUS = .claims-fuzz-corpus
IDENTITY_FUZZ_CORPUS = .identity-fuzz-corpus
METADATA_FUZZ_CORPUS = .metadata-fuzz-corpus
SOURCE_DIR := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
BUILD_ROOT ?= $(SOURCE_DIR)/build
PG18_CONFIG ?= pg_config
PG19_CONFIG ?= pg_config
# Reviewed minimum dependency versions. These are the floors this source tree
# actually requires, not the versions the release contract pins. See
# docs/dependencies.md for the API that establishes each floor.
LIBJWT_MIN_VERSION = 3.3.3
JANSSON_MIN_VERSION = 2.7
LIBCURL_MIN_VERSION = 7.63.0
OPENSSL_MIN_VERSION = 3.0.0

# How each embedded dependency is linked. The release contract is `static`:
# reviewed PIC static archives produced by scripts/ci/install-jansson.sh and
# scripts/ci/install-libjwt.sh, so the module does not depend on the deployment
# environment's JOSE and JSON libraries. Downstream distribution packagers that
# must link shared system libraries instead select `shared`, per library,
# because the two dependencies are rarely both available in a usable form. A
# distribution that ships Jansson without a static archive, for example, builds
# with JANSSON_LINK_MODE=shared and leaves LIBJWT_LINK_MODE at static.
LINK_MODE ?= static
JANSSON_LINK_MODE ?= $(LINK_MODE)
LIBJWT_LINK_MODE ?= $(LINK_MODE)

# Resolve a static archive through pkg-config, yielding an empty value rather
# than a bare "/libjwt.a" when pkg-config cannot answer. Diagnosing that stays
# in check-link-dependencies so that targets which do not link, such as `clean`
# and `formatcheck`, still run without the dependencies installed.
pkg_config_libdir = $(shell pkg-config --variable=libdir $(1) 2>/dev/null)
static_archive = $(strip $(if $(call pkg_config_libdir,$(1)),\
	$(call pkg_config_libdir,$(1))/$(2)))

ifeq ($(LIBJWT_LINK_MODE),static)
LIBJWT_STATIC := $(call static_archive,libjwt,libjwt.a)
LIBJWT_LINK := $(LIBJWT_STATIC)
else ifeq ($(LIBJWT_LINK_MODE),shared)
LIBJWT_LINK := $(shell pkg-config --libs libjwt 2>/dev/null)
else
$(error LIBJWT_LINK_MODE must be static or shared (found: $(LIBJWT_LINK_MODE)))
endif

ifeq ($(JANSSON_LINK_MODE),static)
JANSSON_STATIC := $(call static_archive,jansson,libjansson.a)
JANSSON_LINK := $(JANSSON_STATIC)
else ifeq ($(JANSSON_LINK_MODE),shared)
JANSSON_LINK := $(shell pkg-config --libs jansson 2>/dev/null)
else
$(error JANSSON_LINK_MODE must be static or shared (found: $(JANSSON_LINK_MODE)))
endif

PGFILEDESC = "pg_oauth_validator - OAuth access-token validator"
EXTRA_CLEAN = $(TEST_BIN) $(POLICY_TEST_BIN) $(JWT_ENVELOPE_TEST_BIN) \
	$(JWKS_TEST_BIN) \
	$(SIGNATURE_TEST_BIN) \
	$(CLAIMS_TEST_BIN) \
	$(IDENTITY_TEST_BIN) \
	$(METADATA_TEST_BIN) \
	$(HTTP_TRANSPORT_TEST_BIN) \
	$(ISSUER_KEY_TEST_BIN) \
	$(VALIDATOR_TEST_BIN) \
	$(CACHE_STATE_TEST_BIN) \
	$(CACHE_KEY_TEST_BIN) \
	$(HTTP_FRESHNESS_TEST_BIN) \
	$(OAUTH_TEST_CLIENT) $(CACHE_PROBE) $(LIBJWT_SPIKE_BIN) $(LIBJWT_FUZZ_BIN) \
	$(JWT_ENVELOPE_FUZZ_BIN) $(JWKS_FUZZ_BIN) $(CLAIMS_FUZZ_BIN) \
	$(IDENTITY_FUZZ_BIN) \
	$(METADATA_FUZZ_BIN) \
	.pytest_cache .pycache \
	$(LIBJWT_FUZZ_CORPUS) $(JWT_ENVELOPE_FUZZ_CORPUS) \
	$(JWKS_FUZZ_CORPUS) $(CLAIMS_FUZZ_CORPUS) $(IDENTITY_FUZZ_CORPUS) \
	$(METADATA_FUZZ_CORPUS) \
	.ci-analysis \
	tests/integration/__pycache__ tests/interop/keycloak/__pycache__

# Warnings are fatal by default, and every gate in this repository keeps that
# default. WERROR=0 exists for downstream distribution packagers building a
# released tag on a newer toolchain, where a diagnostic added after the release
# would otherwise turn an unchanged, reviewed source tree into a build failure.
# Do not set it in this project's own builds.
WERROR ?= 1
ifeq ($(WERROR),1)
WERROR_FLAG = -Werror
endif

# Shared strict flags for the standalone test and fuzz binaries, which are
# compiled outside PGXS.
STRICT_CFLAGS = -std=c17 -Wall -Wextra $(WERROR_FLAG) -Wshadow

# Keep project warnings strict without imposing them on PostgreSQL itself.
override PG_CFLAGS += $(STRICT_CFLAGS)
ifeq ($(WERROR),1)
ifneq (,$(findstring clang,$(notdir $(CC))))
# PostgreSQL's PGXS exports GCC-only warning/optimization flags. Keep project
# warnings fatal while allowing Clang to ignore only unsupported PGXS flags.
override PG_CFLAGS += -Wno-error=unknown-warning-option \
	-Wno-error=ignored-optimization-argument -Wno-error=ignored-attributes
endif
endif
override PG_CPPFLAGS += $(shell pkg-config --cflags jansson libcurl libjwt openssl 2>/dev/null)
SHLIB_LINK += -Wl,-Bsymbolic \
	$(LIBJWT_LINK) $(JANSSON_LINK) \
	$(shell pkg-config --libs libcurl openssl 2>/dev/null)

ifeq ($(SANITIZE),1)
DEPENDENCY_SANITIZER_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
endif

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

.PHONY: build-pg18 build-pg19 check-cache-key check-cache-state check-fail-closed check-http-freshness check-jwks check-jwt-envelope \
	check-signature \
	check-claims check-http-transport check-identity check-issuer-key check-metadata \
	check-validator \
	check-release-artifacts \
	check-dependency-installers \
	format formatcheck \
	check-source-tree \
	check-pg-version check-policy check-link-dependencies \
	check-static-link-dependencies \
	check-libjwt-spike check-libjwt-version check-symbols clean-pg18 clean-pg19 \
	dependency-spike fuzz-claims fuzz-identity fuzz-jwks fuzz-jwt-envelope \
	fuzz-metadata \
	fuzz-libjwt-spike \
	integrationcheck \
	installedcheck packagecheck \
	release-package release-source-checksums \
	interop-keycloak \
	sanitizercheck fuzz-smoke \
	dev-check-pg18 dev-check-pg19 dev-integration-pg18 dev-integration-pg19 \
	dev-integration-full-pg18 dev-integration-full-pg19 \
	dev-test-pg18 dev-test-pg19 dev-test-full-pg18 dev-test-full-pg19 \
	dev-verify-pg18 dev-verify-pg19 verify-dev \
	test-pg18 test-pg19 \
	verify verify-all

all: check-pg-version check-link-dependencies

check-source-tree:
	sh "$(srcdir)/scripts/ci/check-source-tree.sh"

format:
	sh "$(srcdir)/scripts/format-c.sh"

formatcheck:
	sh "$(srcdir)/scripts/format-c.sh" --check

check-release-artifacts:
	sh "$(srcdir)/tests/ci/test_release_artifacts.sh"

check-dependency-installers:
	sh "$(srcdir)/tests/ci/test_dependency_installers.sh"

sanitizercheck:
	$(MAKE) dependency-spike check-http-transport check-issuer-key \
		check-validator check-cache-state check-cache-key \
		check-http-freshness check-jwt-envelope check-jwks check-signature \
		check-claims check-identity check-metadata CC=clang SANITIZE=1

fuzz-smoke:
	$(MAKE) fuzz-libjwt-spike fuzz-jwt-envelope fuzz-jwks fuzz-claims \
		fuzz-identity fuzz-metadata

# Prevent compilation from starting with incompatible server headers or an
# unusable link-time dependency configuration. These are order-only so the
# checks cannot be reordered after the objects they guard.
$(OBJS): | check-pg-version check-link-dependencies

check-pg-version:
	@version="$$($(PG_CONFIG) --version)"; \
	case "$$version" in \
	  "PostgreSQL 18"*|"PostgreSQL 19"*) ;; \
	  *) echo "error: PostgreSQL 18 or 19 pg_config is required (found: $$version)" >&2; exit 1 ;; \
	esac; \
	if test -n "$(EXPECTED_PG_MAJOR)"; then \
	  case "$$version" in \
	    "PostgreSQL $(EXPECTED_PG_MAJOR)"*) ;; \
	    *) echo "error: PostgreSQL $(EXPECTED_PG_MAJOR) pg_config is required (found: $$version)" >&2; exit 1 ;; \
	  esac; \
	fi

# Resolves pkg-config, enforces the reviewed minimum versions, and probes each
# selected static archive for PIC linkability. Runs from `all` so a build can
# never begin against an unusable or too-old dependency.
check-link-dependencies:
	sh "$(srcdir)/scripts/ci/check-link-dependencies.sh" "$(CC)" \
		libjwt "$(LIBJWT_LINK_MODE)" "$(LIBJWT_MIN_VERSION)" \
			"$(LIBJWT_STATIC)" \
		jansson "$(JANSSON_LINK_MODE)" "$(JANSSON_MIN_VERSION)" \
			"$(JANSSON_STATIC)" \
		libcurl shared "$(LIBCURL_MIN_VERSION)" "" \
		openssl shared "$(OPENSSL_MIN_VERSION)" ""

# Retained so existing invocations of the former target name keep working.
check-static-link-dependencies: check-link-dependencies

check-symbols: all
	@nm $(MODULE_big)$(DLSUFFIX) | grep -q '_PG_oauth_validator_module_init' || \
	  { echo "error: validator initialization symbol is not exported" >&2; exit 1; }

check-libjwt-version:
	@pkg-config --atleast-version=$(LIBJWT_MIN_VERSION) libjwt || \
	  { echo "error: libjwt $(LIBJWT_MIN_VERSION) or later is required for the dependency spike" >&2; exit 1; }

$(LIBJWT_SPIKE_BIN): tests/dependency/libjwt_spike.c check-libjwt-version
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) $$(pkg-config --cflags libjwt) \
		-o $@ $< $$(pkg-config --libs libjwt)

check-libjwt-spike: $(LIBJWT_SPIKE_BIN)
	./$(LIBJWT_SPIKE_BIN)

dependency-spike: check-libjwt-version check-libjwt-spike

$(LIBJWT_FUZZ_BIN): tests/fuzz/libjwt_inputs_fuzz.c check-libjwt-version
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		$$(pkg-config --cflags libjwt) -o $@ $< $$(pkg-config --libs libjwt)

fuzz-libjwt-spike: $(LIBJWT_FUZZ_BIN)
	@mkdir -p "$(LIBJWT_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(LIBJWT_FUZZ_CORPUS)/"
	./$(LIBJWT_FUZZ_BIN) -runs=2000 -max_len=16384 \
		"$(LIBJWT_FUZZ_CORPUS)"

$(TEST_BIN): tests/fail_closed_test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PG_CPPFLAGS) -I$(includedir_server) \
		-Wl,--export-dynamic -o $@ $< -ldl

check-fail-closed: $(MODULE_big)$(DLSUFFIX) $(TEST_BIN)
	./$(TEST_BIN) ./$(MODULE_big)$(DLSUFFIX)

$(POLICY_TEST_BIN): tests/policy_test.c src/policy.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PG_CPPFLAGS) -I$(includedir_server) \
		-I$(srcdir)/src -o $@ $^

check-policy: $(POLICY_TEST_BIN)
	./$(POLICY_TEST_BIN)

$(CACHE_STATE_TEST_BIN): tests/cache_state_test.c src/cache_state.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src -o $@ $^

check-cache-state: $(CACHE_STATE_TEST_BIN)
	./$(CACHE_STATE_TEST_BIN)

$(CACHE_KEY_TEST_BIN): tests/cache_key_test.c src/cache_key.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src -o $@ $^

check-cache-key: $(CACHE_KEY_TEST_BIN)
	./$(CACHE_KEY_TEST_BIN)

$(HTTP_FRESHNESS_TEST_BIN): tests/http_freshness_test.c src/http_freshness.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags libcurl) -o $@ $^ \
		$$(pkg-config --libs libcurl)

check-http-freshness: $(HTTP_FRESHNESS_TEST_BIN)
	./$(HTTP_FRESHNESS_TEST_BIN)

$(JWT_ENVELOPE_TEST_BIN): tests/jwt_envelope_test.c src/jwt_envelope.c \
	src/base64url.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson)

check-jwt-envelope: $(JWT_ENVELOPE_TEST_BIN)
	./$(JWT_ENVELOPE_TEST_BIN)

$(JWT_ENVELOPE_FUZZ_BIN): tests/fuzz/jwt_envelope_fuzz.c src/jwt_envelope.c \
	src/base64url.c
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-I$(srcdir)/src \
		$$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson)

fuzz-jwt-envelope: $(JWT_ENVELOPE_FUZZ_BIN)
	@mkdir -p "$(JWT_ENVELOPE_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(JWT_ENVELOPE_FUZZ_CORPUS)/"
	./$(JWT_ENVELOPE_FUZZ_BIN) -runs=2000 -max_len=16384 \
		"$(JWT_ENVELOPE_FUZZ_CORPUS)"

$(JWKS_TEST_BIN): tests/jwks_test.c src/jwks.c src/base64url.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson openssl)

check-jwks: $(JWKS_TEST_BIN)
	./$(JWKS_TEST_BIN)

$(SIGNATURE_TEST_BIN): tests/signature_test.c src/signature.c src/jwks.c \
	src/jwt_envelope.c src/base64url.c src/claims.c src/identity.c \
	check-libjwt-version
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson libjwt openssl) -o $@ \
		$(filter %.c,$^) $$(pkg-config --libs jansson libjwt openssl)

check-signature: $(SIGNATURE_TEST_BIN)
	./$(SIGNATURE_TEST_BIN)

$(CLAIMS_TEST_BIN): tests/claims_test.c src/claims.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson)

check-claims: $(CLAIMS_TEST_BIN)
	./$(CLAIMS_TEST_BIN)

$(CLAIMS_FUZZ_BIN): tests/fuzz/claims_fuzz.c src/claims.c
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-I$(srcdir)/src $$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson)

fuzz-claims: $(CLAIMS_FUZZ_BIN)
	@mkdir -p "$(CLAIMS_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(CLAIMS_FUZZ_CORPUS)/"
	./$(CLAIMS_FUZZ_BIN) -runs=2000 -max_len=16384 "$(CLAIMS_FUZZ_CORPUS)"

$(IDENTITY_TEST_BIN): tests/identity_test.c src/identity.c src/base64url.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src -o $@ $^

check-identity: $(IDENTITY_TEST_BIN)
	./$(IDENTITY_TEST_BIN)

$(IDENTITY_FUZZ_BIN): tests/fuzz/identity_fuzz.c src/identity.c src/base64url.c
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-I$(srcdir)/src -o $@ $^

fuzz-identity: $(IDENTITY_FUZZ_BIN)
	@mkdir -p "$(IDENTITY_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(IDENTITY_FUZZ_CORPUS)/"
	./$(IDENTITY_FUZZ_BIN) -runs=2000 -max_len=3072 "$(IDENTITY_FUZZ_CORPUS)"

$(METADATA_TEST_BIN): tests/metadata_test.c src/metadata.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson libcurl) -o $@ $^ \
		$$(pkg-config --libs jansson libcurl)

check-metadata: $(METADATA_TEST_BIN)
	./$(METADATA_TEST_BIN)

$(HTTP_TRANSPORT_TEST_BIN): tests/http_transport_test.c src/http_transport.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags libcurl) -o $@ $^ \
		$$(pkg-config --libs libcurl)

check-http-transport: $(HTTP_TRANSPORT_TEST_BIN)
	python3 "$(srcdir)/tests/http_transport_driver.py" \
		"$(CURDIR)/$(HTTP_TRANSPORT_TEST_BIN)"

$(ISSUER_KEY_TEST_BIN): tests/issuer_key_test.c src/issuer_key.c \
		src/http_transport.c src/http_freshness.c src/cache_key.c \
		src/cache_state.c src/metadata.c src/jwks.c src/base64url.c
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson libcurl) -o $@ $^ \
		$$(pkg-config --libs jansson libcurl openssl)

check-issuer-key: $(ISSUER_KEY_TEST_BIN)
	python3 "$(srcdir)/tests/issuer_key_driver.py" \
		"$(CURDIR)/$(ISSUER_KEY_TEST_BIN)"

$(VALIDATOR_TEST_BIN): tests/validator_test.c src/validator.c src/issuer_key.c \
		src/http_transport.c src/http_freshness.c src/cache_key.c \
		src/cache_state.c src/metadata.c src/signature.c src/jwks.c \
		src/jwt_envelope.c src/claims.c src/identity.c src/base64url.c \
		check-libjwt-version
	$(CC) $(STRICT_CFLAGS) \
		$(DEPENDENCY_SANITIZER_FLAGS) -I$(srcdir)/src \
		$$(pkg-config --cflags jansson libcurl libjwt openssl) -o $@ \
		$(filter %.c,$^) $$(pkg-config --libs jansson libcurl libjwt openssl)

check-validator: $(VALIDATOR_TEST_BIN)
	python3 "$(srcdir)/tests/validator_driver.py" \
		"$(CURDIR)/$(VALIDATOR_TEST_BIN)"

$(METADATA_FUZZ_BIN): tests/fuzz/metadata_fuzz.c src/metadata.c
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-I$(srcdir)/src $$(pkg-config --cflags jansson libcurl) -o $@ $^ \
		$$(pkg-config --libs jansson libcurl)

fuzz-metadata: $(METADATA_FUZZ_BIN)
	@mkdir -p "$(METADATA_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(METADATA_FUZZ_CORPUS)/"
	./$(METADATA_FUZZ_BIN) -runs=2000 -max_len=65536 \
		"$(METADATA_FUZZ_CORPUS)"

$(JWKS_FUZZ_BIN): tests/fuzz/jwks_fuzz.c src/jwks.c src/base64url.c
	clang $(STRICT_CFLAGS) \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-I$(srcdir)/src $$(pkg-config --cflags jansson) -o $@ $^ \
		$$(pkg-config --libs jansson openssl)

fuzz-jwks: $(JWKS_FUZZ_BIN)
	@mkdir -p "$(JWKS_FUZZ_CORPUS)"
	@cp "$(srcdir)"/tests/fuzz/corpus/* "$(JWKS_FUZZ_CORPUS)/"
	./$(JWKS_FUZZ_BIN) -runs=2000 -max_len=65536 "$(JWKS_FUZZ_CORPUS)"

$(OAUTH_TEST_CLIENT) $(OAUTH_TEST_CLIENT_PATH): tests/integration/oauth_test_client.c
	$(CC) -std=c17 -Wall -Wextra $(WERROR_FLAG) \
		-I$(shell $(PG_CONFIG) --includedir) \
		-I$(shell $(PG_CONFIG) --includedir-server) \
		-L$(shell $(PG_CONFIG) --libdir) \
		-Wl,-rpath,$(shell $(PG_CONFIG) --libdir) -o $@ $< -lpq

$(CACHE_PROBE) $(CACHE_PROBE_PATH): tests/integration/cache_probe.c src/shared_cache.c \
		src/cache_state.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PG_CPPFLAGS) -I$(includedir_server) \
		-I$(srcdir)/src -fPIC -shared -o $@ $^

integrationcheck: all $(OAUTH_TEST_CLIENT_PATH) $(CACHE_PROBE_PATH)
	PG_CONFIG="$(PG_CONFIG)" \
	VALIDATOR_LIBRARY="$(VALIDATOR_LIBRARY)" \
	OAUTH_TEST_CLIENT="$(OAUTH_TEST_CLIENT_PATH)" \
	CACHE_PROBE="$(CACHE_PROBE_PATH)" \
	PYTHONPYCACHEPREFIX="$(CURDIR)/.pycache" \
	python3 -m pytest -q -o cache_dir="$(CURDIR)/.pytest_cache" \
		$(INTEGRATION_TEST_ARGS) \
		"$(srcdir)/tests/integration"

packagecheck: all
	@stage="$$(mktemp -d)"; \
	trap 'rm -rf "$$stage"' EXIT INT TERM; \
	$(MAKE) install DESTDIR="$$stage" >/dev/null; \
	sh "$(srcdir)/scripts/ci/check-staged-install.sh" "$$stage" "$(PG_CONFIG)"

# RELEASE_VERSION and SOURCE_DATE_EPOCH are mandatory so release artifacts are
# explicitly versioned and reproducible. RELEASE_OUTPUT defaults outside build
# trees to keep packaging output separate from compiler products.
release-package:
	@test -n "$(RELEASE_VERSION)" || \
		{ echo "error: RELEASE_VERSION is required" >&2; exit 2; }
	@test -n "$(SOURCE_DATE_EPOCH)" || \
		{ echo "error: SOURCE_DATE_EPOCH is required" >&2; exit 2; }
	SOURCE_DATE_EPOCH="$(SOURCE_DATE_EPOCH)" \
		sh "$(srcdir)/scripts/ci/build-release-package.sh" \
		"$(RELEASE_VERSION)" "$(PG_CONFIG)" \
		"$(or $(RELEASE_OUTPUT),$(CURDIR)/dist)"

# Records the reviewed checksums of a published tag's source tarball for
# downstream packagers. Run only after the signed tag is pushed.
release-source-checksums:
	@test -n "$(RELEASE_TAG)" || \
		{ echo "error: RELEASE_TAG is required" >&2; exit 2; }
	sh "$(srcdir)/scripts/ci/release-source-checksums.sh" "$(RELEASE_TAG)" \
		$(RELEASE_SOURCE_CHECKSUMS)

# Installs into pg_config's real directories; run only in a disposable,
# suitably privileged environment such as the pinned CI containers.
installedcheck: packagecheck
	$(MAKE) install
	$(MAKE) integrationcheck \
		VALIDATOR_LIBRARY="$(shell $(PG_CONFIG) --pkglibdir)/$(MODULE_big)$(DLSUFFIX)"

interop-keycloak:
	sh "$(srcdir)/scripts/interop/keycloak.sh"

verify: check-release-artifacts check-dependency-installers check-symbols check-fail-closed check-policy check-cache-state check-cache-key check-http-freshness check-jwt-envelope \
	check-jwks check-signature check-claims check-identity check-metadata \
	check-http-transport check-issuer-key check-validator

# Fast local gate. CI and release preparation continue to use `verify`, which
# additionally checks reproducible release artifacts.
verify-dev: check-dependency-installers check-symbols check-fail-closed check-policy check-cache-state check-cache-key check-http-freshness check-jwt-envelope \
	check-jwks check-signature check-claims check-identity check-metadata \
	check-http-transport check-issuer-key check-validator

build-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 all

test-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 \
		verify integrationcheck

dev-verify-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 \
		verify-dev

dev-check-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 \
		$(or $(DEV_CHECK_TARGETS),check-policy check-claims check-identity)

dev-integration-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 \
		INTEGRATION_TEST_ARGS='-m "not slow" -n 2 --dist loadgroup $(INTEGRATION_TEST_ARGS)' integrationcheck

dev-test-pg18: dev-verify-pg18 dev-integration-pg18

dev-integration-full-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 \
		INTEGRATION_TEST_ARGS='-n 2 --dist loadgroup $(INTEGRATION_TEST_ARGS)' integrationcheck

dev-test-full-pg18: dev-verify-pg18 dev-integration-full-pg18

clean-pg18:
	@mkdir -p "$(BUILD_ROOT)/pg18/src" "$(BUILD_ROOT)/pg18/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg18" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG18_CONFIG)" EXPECTED_PG_MAJOR=18 clean

build-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 all

test-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 \
		verify integrationcheck

dev-verify-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 \
		verify-dev

dev-check-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 \
		$(or $(DEV_CHECK_TARGETS),check-policy check-claims check-identity)

dev-integration-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 \
		INTEGRATION_TEST_ARGS='-m "not slow" -n 2 --dist loadgroup $(INTEGRATION_TEST_ARGS)' integrationcheck

dev-test-pg19: dev-verify-pg19 dev-integration-pg19

dev-integration-full-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 \
		INTEGRATION_TEST_ARGS='-n 2 --dist loadgroup $(INTEGRATION_TEST_ARGS)' integrationcheck

dev-test-full-pg19: dev-verify-pg19 dev-integration-full-pg19

clean-pg19:
	@mkdir -p "$(BUILD_ROOT)/pg19/src" "$(BUILD_ROOT)/pg19/tests/integration"
	$(MAKE) -C "$(BUILD_ROOT)/pg19" -f "$(SOURCE_DIR)/Makefile" \
		VPATH="$(SOURCE_DIR)" PG_CONFIG="$(PG19_CONFIG)" EXPECTED_PG_MAJOR=19 clean

verify-all: test-pg18 test-pg19
