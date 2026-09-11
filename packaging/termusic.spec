# RPM spec for termusic -- an MPD client, NOT an audio daemon.
#
# Build:
#   rpmbuild -ba packaging/termusic.spec \
#            --define "_sourcedir $PWD/build/dist" \
#            --define "version $(./build/termusic --version | awk '{print $2}')"
#
# The daemon policy is deliberate: `mpd` is a weak dependency (Recommends), and
# nothing in this package starts, stops, installs or configures it. A user with
# a remote MPD server must not be forced to install a local one.

# This package ships a PREBUILT release archive, so there is no compilation in
# %build and therefore no debuginfo/debugsource to extract. Without this the
# automatic -debugsource sub-package fails with an empty file list.
%global debug_package %{nil}

Name:           termusic
Version:        0.1.0
Release:        1%{?dist}
Summary:        Lightweight terminal music client for MPD
License:        GPL-3.0-or-later
URL:            https://github.com/termusic/termusic
Source0:        %{name}-%{version}-linux-x86_64.tar.gz
BuildArch:      x86_64

# Libraries termusic needs to launch. FTXUI is linked statically and needs no
# package; the C++ runtime is pulled in automatically.
Requires:       libmpdclient
Requires:       fftw-libs-single

# The daemon is OPTIONAL: termusic can talk to a remote MPD just as well.
Recommends:     mpd

%description
termusic is a lightweight terminal music client. It is a frontend for the Music
Player Daemon: MPD owns playback, the decoder, the output, the database and the
queue, and termusic provides the terminal interface, the Vault/Core structure,
its own playback history and configuration, and controls playback through MPD.

termusic connects automatically at startup to 127.0.0.1:6600 unless another
endpoint is configured (Core > Connection, config.toml, MPD_HOST/MPD_PORT or
--host/--port). MPD may run locally or on a remote machine. termusic never
starts, stops or configures the MPD daemon; that lifecycle belongs to the user
or the system.

%prep
# The source archive expands to termusic-<version>-linux-<arch>/, not to
# termusic-<version>/, so the build directory is named explicitly.
%setup -q -n %{name}-%{version}-linux-x86_64

%build
# The archive already contains a release build; a packager who prefers to build
# from source should use the source archive and `cmake --install` (see
# packaging/README.packaging).

%install
install -Dpm 0755 termusic %{buildroot}%{_bindir}/termusic
# Licenses live where rpm expects them (%{_licensedir}), documents where the
# distribution expects those (%{_docdir}); %license/%doc then pick them up.
install -Dpm 0644 LICENSE \
    %{buildroot}%{_licensedir}/%{name}/LICENSE
install -Dpm 0644 THIRD_PARTY_LICENSES.md \
    %{buildroot}%{_licensedir}/%{name}/THIRD_PARTY_LICENSES.md
install -Dpm 0644 README.md %{buildroot}%{_docdir}/%{name}/README.md
install -Dpm 0644 config.example.toml %{buildroot}%{_docdir}/%{name}/config.example.toml

%files
%license LICENSE THIRD_PARTY_LICENSES.md
%doc README.md config.example.toml
%{_bindir}/termusic

%changelog
* Sat Sep 12 2026 termusic maintainers - 0.1.0-1
- Initial release: MPD client with Vault/Core UI, playback history,
  visualizer and XDG-compliant configuration.
