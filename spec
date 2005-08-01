Name: @PACKAGE@
Summary: Backup ARCHiver
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://untroubled.org/@PACKAGE@/@PACKAGE@-@VERSION@.tar.gz
BuildRoot: %{_tmppath}/@PACKAGE@-buildroot
#BuildArch: noarch
URL: http://untroubled.org/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>
BuildRequires: bglibs >= 1.024

%description
barch is an archiver, similar to tar, but designed specifically for
backup and restore oprations and to improve on some of the deficiencies
that tar has.

%prep
%setup
echo gcc "%{optflags}" >conf-cc
echo gcc -s >conf-ld

%build
make

%install
rm -fr %{buildroot}
#make install_prefix=%{buildroot} bindir=%{_bindir} mandir=%{_mandir} install

mkdir -p %{buildroot}%{_bindir}
install barch %{buildroot}%{_bindir}/barch

mkdir -p %{buildroot}%{_mandir}/man1
install -m 644 barch.1 %{buildroot}%{_mandir}/man1/barch.1

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc ANNOUNCEMENT COPYING NEWS README *.html
%{_bindir}/*
%{_mandir}/man*/*
