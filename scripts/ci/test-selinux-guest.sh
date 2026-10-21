#!/bin/sh
# Root-only entry point for a disposable Fedora 44 VM, never a production host.
set -eu
test "$(id -u)" -eq 0
test "$(getenforce)" = Enforcing
. /etc/os-release
test "$ID" = fedora && test "$VERSION_ID" = 44
cd /opt/pg-oauth-validator
sh scripts/ci/install-rpm-dependencies.sh fedora44
dnf install -y rpm-build cpio elfutils policycoreutils-python-utils audit iproute
systemctl start auditd
export PG_CONFIG_PATH=/usr/bin/pg_server_config
export PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig
export LD_LIBRARY_PATH=/usr/local/lib
python3 scripts/ci/prepare-rpm-sources.py fetch /opt/rpm-dependencies
JANSSON_SOURCE_ARCHIVE=/opt/rpm-dependencies/jansson.tar.gz sh scripts/ci/install-jansson.sh
LIBJWT_SOURCE_ARCHIVE=/opt/rpm-dependencies/libjwt.tar.gz sh scripts/ci/install-libjwt.sh
python3 scripts/ci/prepare-rpm-sources.py prepare 0.0.0 "$(git -c safe.directory=/opt/pg-oauth-validator rev-parse HEAD)" /opt/rpm-dependencies /opt/rpm-inputs
mkdir /source /opt/tested-rpm
export RPM_TEST_EXPORT_DIR=/opt/tested-rpm
unshare --net /bin/sh -c 'ip link set lo up; exec sh scripts/ci/test-rpm-package.sh /opt/rpm-inputs'
rpm --install /opt/tested-rpm/validator.rpm
library="$($PG_CONFIG_PATH --pkglibdir)/pg_oauth_validator.so"
restorecon -v "$library"
# The shared-cache probe is test instrumentation, distinct from the RPM module.
install -m 0755 /source/tests/integration/cache_probe.so "$($PG_CONFIG_PATH --pkglibdir)/oauth_test_cache_probe.so"
restorecon -v "$($PG_CONFIG_PATH --pkglibdir)/oauth_test_cache_probe.so"
export PG_CONFIG="$PG_CONFIG_PATH" VALIDATOR_LIBRARY="$library"
export OAUTH_TEST_CLIENT=/source/tests/integration/oauth_test_client
export CACHE_PROBE="$($PG_CONFIG_PATH --pkglibdir)/oauth_test_cache_probe.so"
export OAUTH_TEST_SELINUX=1 PYTHONPYCACHEPREFIX=/source/.pycache
# Keep policy evidence even when the suite fails. Never upload provider keys,
# token files, raw PostgreSQL logs, or pytest locals.
trap 'getenforce; semanage permissive -l; ausearch -m AVC,USER_AVC -ts boot -i || true' EXIT
python3 -m pytest -q -p no:cacheprovider tests/integration
rpm --verify postgresql18-pg_oauth_validator
test "$(getenforce)" = Enforcing
