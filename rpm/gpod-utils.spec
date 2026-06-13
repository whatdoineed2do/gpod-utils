Name:           gpod-utils
Version:        %{_version}
Release:        1%{?dist}
Summary:        Command line tools using libgpod to access iPod data

License:        GPL-2.0-or-later
URL:            https://github.com/whatdoineed2do/gpod-utils
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc make
BuildRequires:  autoconf automake libtool
BuildRequires:  pkgconfig
BuildRequires:  libgpod-devel
BuildRequires:  glib2-devel
BuildRequires:  ffmpeg-devel
BuildRequires:  json-c-devel
BuildRequires:  sqlite-devel

%description
Command line tools using libgpod to access iPod data.
Provides utilities for listing, copying, removing, tagging, and verifying
iPod tracks from the command line.

%prep
%setup -q

%build
autoreconf --install
%configure
%make_build

%install
%make_install

%files
%{_bindir}/gpod-cp
%{_bindir}/gpod-extract
%{_bindir}/gpod-hashsum
%{_bindir}/gpod-ls
%{_bindir}/gpod-recent-pl
%{_bindir}/gpod-rm
%{_bindir}/gpod-tag
%{_bindir}/gpod-verify

%changelog
* Sat Jun 13 2026 Ilya Kargapolov <d3vil.st@gmail.com> - 1.4.4-1
- Initial RPM packaging
