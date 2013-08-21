Name:       lemolo
Summary:    Ofono EFL Dialer
Version:    0.1.3
Release:    1
Group:      System/Libraries
License:    Apache 2.0
URL:        http://www.tizen.org
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ofono)
BuildRequires:  pkgconfig(ecore-x)
BuildRequires:  pkgconfig(appcore-efl)
BuildRequires:  pkgconfig(contacts-service2)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(capi-system-power)
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  pkgconfig(notification)
BuildRequires:  edje-tools
Requires: ofono

%description
Provides a dialer application for an In-Vehicle Infotainment (IVI) system, where
the primary modem access is via a mobile device connected to the head unit via the
handsfree profile over bluetooth.

%prep
%setup -q -n %{name}-%{version}

%build

%autogen --enable-tizen \
         --bindir=/usr/apps/org.tizen.dialer/bin/ \
         --datadir=/usr/apps/org.tizen.dialer/data/ 

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%files
%defattr(-,root,root,-)
/usr/apps/org.tizen.dialer/bin/answer_daemon
/usr/apps/org.tizen.dialer/bin/dialer
/usr/apps/org.tizen.dialer/bin/messages
/usr/apps/org.tizen.dialer/bin/dialer_daemon
/usr/apps/org.tizen.dialer/bin/dialer_open
/usr/apps/org.tizen.dialer/bin/message_daemon
/usr/apps/org.tizen.dialer/data/lemolo/examples/contacts.csv
/usr/apps/org.tizen.dialer/data/lemolo/scripts/ofono-efl-contacts-db-create.py
/usr/apps/org.tizen.dialer/data/lemolo/themes/default.edj
/usr/apps/org.tizen.dialer/data/lemolo/themes/night.edj
/usr/apps/org.tizen.dialer/data/lemolo/tizen-examples/order
/usr/share/packages/org.tizen.dialer.xml
%{_datadir}/icons/default/small/org.tizen.dialer.png
%{_datadir}/dbus-1/services/org.tizen.dialer.service
