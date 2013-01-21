Name:       lemolo
Summary:    Ofono EFL Dialer
Version:    0.1.0
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
handsfree profile over bluetooth.

%prep
%setup -q -n %{name}-%{version}

%build

%autogen --enable-tizen \
         --bindir=/opt/apps/org.tizen.dialer/bin/ \
         --datadir=/opt/apps/org.tizen.dialer/data/ 

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%files
%defattr(-,root,root,-)
/opt/apps/org.tizen.dialer/bin/answer_daemon
/opt/apps/org.tizen.dialer/bin/dialer
/opt/apps/org.tizen.dialer/bin/messages
/opt/apps/org.tizen.dialer/bin/dialer_daemon
/opt/apps/org.tizen.dialer/bin/dialer_open
/opt/apps/org.tizen.dialer/bin/message_daemon
/opt/apps/org.tizen.dialer/data/lemolo/examples/contacts.csv
/opt/apps/org.tizen.dialer/data/lemolo/scripts/ofono-efl-contacts-db-create.py
/opt/apps/org.tizen.dialer/data/lemolo/themes/default-sd.edj
/opt/apps/org.tizen.dialer/data/lemolo/themes/default.edj
/opt/apps/org.tizen.dialer/data/lemolo/tizen-examples/order
#/opt/share/applications/answer_daemon.desktop
#/opt/share/applications/dialer.desktop
#/opt/share/applications/org.tizen.call.desktop
/opt/share/applications/org.tizen.dialer.desktop
%{_datadir}/icons/default/small/org.tizen.dialer.png
%{_datadir}/dbus-1/services/org.tizen.dialer.service
