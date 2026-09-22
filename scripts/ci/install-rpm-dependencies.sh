#!/bin/sh

set -eu

usage()
{
	echo "usage: $0 fedora44|el9" >&2
	exit 2
}

test "$#" -eq 1 || usage
platform=$1

test "$(id -u)" -eq 0 || {
	echo "error: dependency installation must run as root" >&2
	exit 1
}

common_packages='ca-certificates clang llvm cmake curl-minimal diffutils findutils gcc git gzip make
openssl openssl-devel libcurl-devel pkgconf-pkg-config python3 tar binutils
krb5-devel redhat-rpm-config'

case "$platform" in
	fedora44)
		# Fedora packages the server-side PGXS selector separately from libpq's
		# pg_config. The project must build against pg_server_config.
		dnf install --assumeyes $common_packages \
			postgresql postgresql-server postgresql-server-devel
		pg_config=/usr/bin/pg_server_config
		;;
	el9)
		# The repository RPM is noarch, but PGDG publishes it from a
		# per-architecture directory and the two copies are not byte-identical,
		# so the reviewed digest is selected together with the URL.
		case "$(uname -m)" in
			x86_64)
				pgdg_repo_arch=x86_64
				pgdg_repo_sha256=416ce4d364e620c660dc6974b7c52ac6628ccb8375418f37d7dc00a50235b13c
				;;
			aarch64)
				pgdg_repo_arch=aarch64
				pgdg_repo_sha256=ac07549ce04d89f9d90315e61c74efb2138afdd6239af7ce6e7881694115a995
				;;
			*)
				echo "error: no reviewed PGDG repository package for" \
					"$(uname -m)" >&2
				exit 1
				;;
		esac
		pgdg_repo_url=https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-$pgdg_repo_arch/pgdg-redhat-repo-42.0-66.rhel9PGDG.noarch.rpm
		work_dir=$(mktemp -d)
		trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
		pgdg_repo=$work_dir/pgdg-redhat-repo.noarch.rpm
		dnf install --assumeyes ca-certificates curl-minimal
		curl --fail --location --proto '=https' --tlsv1.2 \
			--output "$pgdg_repo" "$pgdg_repo_url"
		printf '%s  %s\n' "$pgdg_repo_sha256" "$pgdg_repo" |
			sha256sum --check --status
		dnf install --assumeyes "$pgdg_repo"
		dnf -qy module disable postgresql
		dnf install --assumeyes dnf-plugins-core
		dnf config-manager --set-enabled crb
		dnf install --assumeyes $common_packages \
			postgresql18 postgresql18-server postgresql18-devel
		pg_config=/usr/pgsql-18/bin/pg_config
		;;
	*)
		usage
		;;
esac

test -x "$pg_config" || {
	echo "error: expected PostgreSQL configuration program at $pg_config" >&2
	exit 1
}
case "$($pg_config --version)" in
	"PostgreSQL 18"*) ;;
	*)
		echo "error: PostgreSQL 18 development files are required" >&2
		exit 1
		;;
esac

{
	echo "rpm-platform: $platform"
	echo "architecture: $(uname -m)"
	echo "postgresql-version: $($pg_config --version)"
	echo "rpm-packages:"
	rpm -q --qf '  %{NAME} %{VERSION}-%{RELEASE}.%{ARCH}\n' \
		gcc cmake openssl-devel libcurl-devel |
		LC_ALL=C sort
} >/etc/pg-oauth-build-inputs
