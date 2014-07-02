Name:       lemolo
Summary:    Ofono EFL Dialer
Version:    0.1.6
Release:    1
Group:      Applications/Telephony
License:    Apache-2.0
URL:        http://www.tizen.org
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ofono)
BuildRequires:  pkgconfig(appcore-efl)
BuildRequires:  pkgconfig(contacts-service2)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(capi-system-power)
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  pkgconfig(notification)
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  edje-tools
BuildRequires:  gettext-tools
Requires: ofono
Requires: automotive-message-broker

%description
Provides a dialer application for an In-Vehicle Infotainment (IVI) system, where
the primary modem access is via a mobile device connected to the head unit via the
handsfree profile over bluetooth.

%prep
%setup -q -n %{name}-%{version}

%build

%autogen \
--enable-notification \
--enable-tizen

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install
%find_lang %{name}

%files -f %{name}.lang
%defattr(-,root,root,-)
%{_bindir}/answer_daemon
%{_bindir}/dialer
%{_bindir}/messages
%{_bindir}/dialer_daemon
%{_bindir}/dialer_open
%{_bindir}/message_daemon
%{_datadir}/lemolo/examples/contacts.csv
%{_datadir}/lemolo/ringtones/default.wav
%{_datadir}/lemolo/scripts/ofono-efl-contacts-db-create.py
%{_datadir}/lemolo/themes/default.edj
%{_datadir}/lemolo/themes/night.edj
%{_datadir}/lemolo/tizen-examples/order
%{_datadir}/packages/lemolo.xml
%{_datadir}/icons/default/small/org.tizen.dialer.png
%{_datadir}/dbus-1/services/org.tizen.dialer.service
