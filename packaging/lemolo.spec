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
%{_datadir}/lemolo/examples/contacts.csv
%{_datadir}/lemolo/scripts/ofono-efl-contacts-db-create.py
%{_datadir}/lemolo/themes/default-sd.edj
%{_datadir}/lemolo/themes/default.edj
%{_datadir}/dbus-1/services/org.tizen.dialer.service
%{_datadir}/lemolo/tizen-examples/order
/opt/share/applications/answer_daemon.desktop
/opt/share/applications/dialer.desktop
/opt/share/applications/org.tizen.call.desktop
/opt/share/applications/org.tizen.phone.desktop
