Name:       ivi-dialer
Summary:    IVI Dialer
Version:    0.1.0
Release:    1
Group:      System/Libraries
License:    MIT
URL:        http://www.tizen.org
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ofono)
BuildRequires:  pkgconfig(ecore-x)
BuildRequires:  pkgconfig(appcore-efl)
BuildRequires:  pkgconfig(ui-gadget-1)
BuildRequires:  pkgconfig(capi-social-contacts)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(capi-system-power)
BuildRequires:  pkgconfig(utilX)
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  pkgconfig(notification)
BuildRequires:  libug-contacts-devel
BuildRequires:  edje-tools


%description
Provides a dialer application for an In-Vehicle Infotainment (IVI) system, where
the primary modem access is via a mobile device connected to the head unit via the
handsfree profile over bluetooth.  In addition to basic phone calls, this dialer will
also expose the contacts from the connected mobile device via the Phone Book Access
Protocol (PBAP) over bluetooth when supported.

%prep
%setup -q -n %{name}-%{version}

%build

%autogen --enable-tizen

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%files
%defattr(-,root,root,-)
%{_bindir}/answer_daemon
%{_bindir}/dialer
%{_bindir}/messages
%{_bindir}/dialer_daemon
%{_bindir}/dialer_open
%{_bindir}/message_daemon
%{_datadir}/ofono-efl/examples/contacts.csv
%{_datadir}/ofono-efl/scripts/ofono-efl-contacts-db-create.py
%{_datadir}/ofono-efl/themes/default-sd.edj
%{_datadir}/ofono-efl/themes/default.edj
%{_datadir}/dbus-1/services/org.tizen.dialer.service
%{_datadir}/ofono-efl/tizen-examples/order
/opt/share/applications/answer_daemon.desktop
/opt/share/applications/dialer.desktop
/opt/share/applications/org.tizen.call.desktop
/opt/share/applications/org.tizen.phone.desktop
