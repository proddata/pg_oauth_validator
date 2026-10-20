#!/bin/sh

# Run as root only inside a disposable, --network=none RPM test container.
set -eu

test "$#" -eq 1 || {
	echo "usage: $0 PREPARED_INPUT_DIR" >&2
	exit 2
}
: "${PG_CONFIG_PATH:?PG_CONFIG_PATH is required}"
test "$(id -u)" -eq 0
inputs=$(CDPATH= cd -- "$1" && pwd)
repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

# The container network namespace enforces offline building. Check it here so
# forgetting --network=none cannot silently weaken this regression gate.
test -d /sys/class/net/lo
for interface in /sys/class/net/*; do
	test -f "$interface/flags" || continue
	test "$interface" != /sys/class/net/lo || continue
	flags=$(cat "$interface/flags")
	test "$((flags & 1))" -eq 0 || {
		echo 'error: a non-loopback network interface is up; use --network=none' >&2
		exit 1
	}
done
(cd "$inputs" && sha256sum --check INPUTS.sha256)
if rpmspec --parse --define 'fedora 42' --define 'rhel 9' \
	"$inputs/SPECS/pg_oauth_validator.spec" >"$work_dir/unsupported.out" 2>&1; then
	echo 'error: spec accepted an unsupported distribution' >&2
	exit 1
fi
grep -q 'Only Fedora 43/44 and Enterprise Linux 10' "$work_dir/unsupported.out"
mkdir -p "$work_dir/rpmbuild" "$work_dir/tmp"
cp -R "$inputs/SOURCES" "$inputs/SPECS" "$work_dir/rpmbuild/"
source_epoch=$(sed -n 's/^%global source_epoch \([0-9][0-9]*\)$/\1/p' \
	"$work_dir/rpmbuild/SPECS/pg_oauth_validator.spec")
test -n "$source_epoch"
chown -R nobody "$work_dir"
chmod 0755 "$work_dir"
runuser -u nobody -- env SOURCE_DATE_EPOCH="$source_epoch" \
	rpmbuild -ba \
	--define "_topdir $work_dir/rpmbuild" \
	--define "_tmppath $work_dir/tmp" \
	--define '_buildhost pg-oauth-rpm-builder.invalid' \
	--define 'use_source_date_epoch_as_buildtime 1' \
	--define 'clamp_mtime_to_source_date_epoch 1' \
	"$work_dir/rpmbuild/SPECS/pg_oauth_validator.spec"

package_name=postgresql18-pg_oauth_validator
package=$(find "$work_dir/rpmbuild/RPMS" -type f -name "$package_name-[0-9]*.rpm")
test -n "$package" && test "$(printf '%s\n' "$package" | wc -l)" -eq 1
test "$(rpm -qp --qf '%{NAME}' "$package")" = "$package_name"
test "$(rpm -qp --qf '%{ARCH}' "$package")" = "$(rpm --eval '%{_arch}')"
test -z "$(rpm -qp --scripts "$package")"
requires=$work_dir/requires
rpm -qp --requires "$package" >"$requires"
case "$PG_CONFIG_PATH" in
	/usr/bin/pg_server_config)
		server=postgresql-server
		grep -Fx 'postgresql-server(:MODULE_COMPAT_18)' "$requires"
		;;
	/usr/pgsql-18/bin/pg_config) server=postgresql18-server ;;
	*) echo 'error: unexpected PostgreSQL selector' >&2; exit 1 ;;
esac
grep -Fx "$server$(rpm --eval '%{?_isa}') >= 18.6" "$requires"
grep -Fx "$server$(rpm --eval '%{?_isa}') < 19" "$requires"
rpm -qp --provides "$package" >"$work_dir/provides"
grep -Fx 'bundled(jansson) = 2.15.1' "$work_dir/provides"
grep -Fx 'bundled(libjwt) = 3.3.3' "$work_dir/provides"
if grep -E 'lib(jwt|jansson)' "$requires"; then
	echo 'error: embedded dependencies became runtime requirements' >&2
	exit 1
fi

# The SRPM must carry all three actual source archives, not only download URLs.
srpm=$(find "$work_dir/rpmbuild/SRPMS" -type f -name '*.src.rpm')
test -n "$srpm" && test "$(printf '%s\n' "$srpm" | wc -l)" -eq 1
mkdir "$work_dir/srpm"
rpm2cpio "$srpm" >"$work_dir/source.cpio"
(cd "$work_dir/srpm" && cpio --extract --quiet <"$work_dir/source.cpio")
(cd "$work_dir/srpm" && sha256sum --check SHA256SUMS)
cmp "$inputs/SPECS/pg_oauth_validator.spec" "$work_dir/srpm/pg_oauth_validator.spec"

# Install with normal dependency/conflict checking; never use --nodeps.
library="$($PG_CONFIG_PATH --pkglibdir)/pg_oauth_validator.so"
test ! -e "$library"
rpm --install "$package"
rpm --verify "$package_name"
test -f "$library"
test -f "/usr/share/licenses/$package_name/LICENSE"
test -f "/usr/share/licenses/$package_name/THIRD-PARTY-LICENSES/JANSSON"
test -f "/usr/share/licenses/$package_name/THIRD-PARTY-LICENSES/LIBJWT-MPL-2.0"
test -f "/usr/share/doc/$package_name/BUILD-SOURCE"
nm -D "$library" | grep '_PG_oauth_validator_module_init'
readelf --dynamic "$library" >"$work_dir/dynamic"
if grep -E '\((RPATH|RUNPATH)\)' "$work_dir/dynamic"; then
	echo 'error: packaged module has a private runtime search path' >&2
	exit 1
fi
ldd "$library" >"$work_dir/ldd"
if grep -E 'not found|lib(jwt|jansson)' "$work_dir/ldd"; then
	echo 'error: invalid runtime linkage' >&2
	exit 1
fi
if find "/usr/share/doc/$package_name" "/usr/share/licenses/$package_name" \
	"$library" -type f -perm /022 | grep -q .; then
	echo 'error: writable installed package files' >&2
	exit 1
fi

# Use the current security suite to test the RPM's library; do not overwrite it
# with `make install` or build-tree module bytes.
cp -a "$repo_root/." /source/
make -C /source clean PG_CONFIG="$PG_CONFIG_PATH" >/dev/null
make -C /source integrationcheck PG_CONFIG="$PG_CONFIG_PATH" \
	VALIDATOR_LIBRARY="$library"
rpm --verify "$package_name"
rpm --erase "$package_name"
test ! -e "$library"
test ! -e "/usr/share/doc/$package_name"
test ! -e "/usr/share/licenses/$package_name"
if rpm -q "$package_name"; then
	echo 'error: package remains installed after removal' >&2
	exit 1
fi
echo 'RPM offline build, source integrity, install, OAuth, and removal checks passed'
