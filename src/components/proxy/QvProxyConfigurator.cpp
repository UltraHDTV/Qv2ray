#include "QvProxyConfigurator.hpp"

#include "base/Qv2rayBase.hpp"
#include "common/QvHelpers.hpp"
#include "components/plugins/QvPluginHost.hpp"
#ifdef Q_OS_WIN
    #include <WinInet.h>
    #include <Windows.h>
#endif

namespace Qv2ray::components::proxy
{

#ifdef Q_OS_MACOS
    QStringList macOSgetNetworkServices()
    {
        QProcess p;
        p.setProgram("/usr/sbin/networksetup");
        p.setArguments(QStringList{ "-listallnetworkservices" });
        p.start();
        p.waitForStarted();
        p.waitForFinished();
        LOG(MODULE_PROXY, p.errorString())
        auto str = p.readAllStandardOutput();
        auto lines = SplitLines(str);
        QStringList result;

        // Start from 1 since first line is unneeded.
        for (auto i = 1; i < lines.count(); i++)
        {
            // * means disabled.
            if (!lines[i].contains("*"))
            {
                result << (lines[i].contains(" ") ? "\"" + lines[i] + "\"" : lines[i]);
            }
        }

        LOG(MODULE_PROXY, "Found " + QSTRN(result.size()) + " network services: " + result.join(";"))
        return result;
    }
#endif
#ifdef Q_OS_WIN
    #define NO_CONST(expr) const_cast<wchar_t *>(expr)
    // static auto DEFAULT_CONNECTION_NAME =
    // NO_CONST(L"DefaultConnectionSettings");
    ///
    /// INTERNAL FUNCTION
    bool __QueryProxyOptions()
    {
        INTERNET_PER_CONN_OPTION_LIST List;
        INTERNET_PER_CONN_OPTION Option[5];
        //
        unsigned long nSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
        Option[0].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
        Option[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
        Option[2].dwOption = INTERNET_PER_CONN_FLAGS;
        Option[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
        Option[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
        //
        List.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
        List.pszConnection = nullptr; // NO_CONST(DEFAULT_CONNECTION_NAME);
        List.dwOptionCount = 5;
        List.dwOptionError = 0;
        List.pOptions = Option;

        if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, &nSize))
        {
            LOG(MODULE_PROXY, "InternetQueryOption failed, GLE=" + QSTRN(GetLastError()))
        }

        LOG(MODULE_PROXY, "System default proxy info:")

        if (Option[0].Value.pszValue != nullptr)
        {
            LOG(MODULE_PROXY, QString::fromWCharArray(Option[0].Value.pszValue))
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_AUTO_PROXY_URL) == PROXY_TYPE_AUTO_PROXY_URL)
        {
            LOG(MODULE_PROXY, "PROXY_TYPE_AUTO_PROXY_URL")
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_AUTO_DETECT) == PROXY_TYPE_AUTO_DETECT)
        {
            LOG(MODULE_PROXY, "PROXY_TYPE_AUTO_DETECT")
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_DIRECT) == PROXY_TYPE_DIRECT)
        {
            LOG(MODULE_PROXY, "PROXY_TYPE_DIRECT")
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_PROXY) == PROXY_TYPE_PROXY)
        {
            LOG(MODULE_PROXY, "PROXY_TYPE_PROXY")
        }

        if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, &nSize))
        {
            LOG(MODULE_PROXY, "InternetQueryOption failed,GLE=" + QSTRN(GetLastError()))
        }

        if (Option[4].Value.pszValue != nullptr)
        {
            LOG(MODULE_PROXY, QString::fromStdWString(Option[4].Value.pszValue))
        }

        INTERNET_VERSION_INFO Version;
        nSize = sizeof(INTERNET_VERSION_INFO);
        InternetQueryOption(nullptr, INTERNET_OPTION_VERSION, &Version, &nSize);

        if (Option[0].Value.pszValue != nullptr)
        {
            GlobalFree(Option[0].Value.pszValue);
        }

        if (Option[3].Value.pszValue != nullptr)
        {
            GlobalFree(Option[3].Value.pszValue);
        }

        if (Option[4].Value.pszValue != nullptr)
        {
            GlobalFree(Option[4].Value.pszValue);
        }

        return false;
    }
    bool __SetProxyOptions(LPWSTR proxy_full_addr, bool isPAC)
    {
        INTERNET_PER_CONN_OPTION_LIST list;
        BOOL bReturn;
        DWORD dwBufSize = sizeof(list);
        // Fill the list structure.
        list.dwSize = sizeof(list);
        // NULL == LAN, otherwise connectoid name.
        list.pszConnection = nullptr;

        if (isPAC)
        {
            LOG(MODULE_PROXY, "Setting system proxy for PAC")
            //
            list.dwOptionCount = 2;
            list.pOptions = new INTERNET_PER_CONN_OPTION[2];

            // Ensure that the memory was allocated.
            if (nullptr == list.pOptions)
            {
                // Return FALSE if the memory wasn't allocated.
                return FALSE;
            }

            // Set flags.
            list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
            list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_AUTO_PROXY_URL;
            // Set proxy name.
            list.pOptions[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            list.pOptions[1].Value.pszValue = proxy_full_addr;
        }
        else
        {
            LOG(MODULE_PROXY, "Setting system proxy for Global Proxy")
            //
            list.dwOptionCount = 3;
            list.pOptions = new INTERNET_PER_CONN_OPTION[3];

            if (nullptr == list.pOptions)
            {
                return false;
            }

            // Set flags.
            list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
            list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;
            // Set proxy name.
            list.pOptions[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
            list.pOptions[1].Value.pszValue = proxy_full_addr;
            // Set proxy override.
            list.pOptions[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            auto localhost = L"localhost";
            list.pOptions[2].Value.pszValue = NO_CONST(localhost);
        }

        // Set the options on the connection.
        bReturn = InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, dwBufSize);
        delete[] list.pOptions;
        InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
        InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
        return bReturn;
    }
#endif

    void SetSystemProxy(const QString &address, int httpPort, int socksPort)
    {
        LOG(MODULE_PROXY, "Setting up System Proxy")
        bool hasHTTP = (httpPort != 0);
        bool hasSOCKS = (socksPort != 0);

        if (!(hasHTTP || hasSOCKS))
        {
            LOG(MODULE_PROXY, "Nothing?")
            return;
        }

        if (hasHTTP)
        {
            LOG(MODULE_PROXY, "Qv2ray will set system proxy to use HTTP")
        }

        if (hasSOCKS)
        {
            LOG(MODULE_PROXY, "Qv2ray will set system proxy to use SOCKS")
        }

#ifdef Q_OS_WIN
        const auto scheme = (hasHTTP ? "" : "socks5://");
        QString __a = scheme + address + ":" + QSTRN(hasHTTP ? httpPort : socksPort);

        LOG(MODULE_PROXY, "Windows proxy string: " + __a)
        auto proxyStrW = new WCHAR[__a.length() + 1];
        wcscpy(proxyStrW, __a.toStdWString().c_str());
        //
        __QueryProxyOptions();

        if (!__SetProxyOptions(proxyStrW, false))
        {
            LOG(MODULE_PROXY, "Failed to set proxy.")
        }

        __QueryProxyOptions();
#elif defined(Q_OS_LINUX)
        QStringList actions;
        actions << QString("gsettings set org.gnome.system.proxy mode '%1'").arg("manual");
        bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE";
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        // Configure HTTP Proxies for HTTP, FTP and HTTPS
        if (hasHTTP)
        {
            // iterate over protocols...
            for (const auto protocol : { "http", "ftp", "https" })
            {
                // for GNOME:
                {
                    actions << QString("gsettings set org.gnome.system.proxy.%1 host '%2'").arg(protocol, address);
                    actions << QString("gsettings set org.gnome.system.proxy.%1 port %2").arg(protocol, QSTRN(httpPort));
                }

                // for KDE:
                if (isKDE)
                {
                    actions << QString("kwriteconfig5 --file %1/kioslaverc --group 'Proxy Settings' --key %2Proxy 'http://%3 %4'")
                                   .arg(configPath, protocol, address, QSTRN(httpPort));
                }
            }
        }

        // Configure SOCKS5 Proxies
        if (hasSOCKS)
        {
            // for GNOME:
            {
                actions << QString("gsettings set org.gnome.system.proxy.socks host '%1'").arg(address);
                actions << QString("gsettings set org.gnome.system.proxy.socks port %1").arg(socksPort);
            }

            // for KDE:
            if (isKDE)
            {
                actions << QString("kwriteconfig5 --file %1/kioslaverc --group 'Proxy Settings' --key socksProxy 'socks://%2 %3'")
                               .arg(configPath, address, QSTRN(socksPort));
            }
        }

        // Setting Proxy Mode to Manual
        {
            // for GNOME:
            {
                actions << "gsettings set org.gnome.system.proxy mode 'manual'";
            }

            // for KDE:
            if (isKDE)
            {
                LOG(MODULE_PROXY, "KDE detected")
                actions << QString("kwriteconfig5 --file %1/kioslaverc --group 'Proxy Settings' --key ProxyType 1").arg(configPath);
            }
        }

        // Execute them all!
        //
        // note: do not use std::all_of / any_of / none_of,
        // because those are short-circuit and cannot guarantee atomicity.
        auto result = std::count_if(actions.cbegin(), actions.cend(), [](const QString &action) {
                          // execute and get the code
                          const auto returnCode = QProcess::execute(action);
                          // print out the commands and result codes
                          DEBUG(MODULE_PROXY, QString("[%1] %2").arg(QSTRN(returnCode), action))
                          // give the code back
                          return returnCode == QProcess::NormalExit;
                      }) == actions.size();

        if (!result)
        {
            LOG(MODULE_PROXY, "Something wrong when setting proxies.")
        }

        // TODO: Post-settings for DDE

#else

        for (auto service : macOSgetNetworkServices())
        {
            LOG(MODULE_PROXY, "Setting proxy for interface: " + service)

            if (hasHTTP)
            {
                QProcess::execute("/usr/sbin/networksetup -setwebproxystate " + service + " on");
                QProcess::execute("/usr/sbin/networksetup -setsecurewebproxystate " + service + " on");
                QProcess::execute("/usr/sbin/networksetup -setwebproxy " + service + " " + address + " " + QSTRN(httpPort));
                QProcess::execute("/usr/sbin/networksetup -setsecurewebproxy " + service + " " + address + " " + QSTRN(httpPort));
            }

            if (hasSOCKS)
            {
                QProcess::execute("/usr/sbin/networksetup -setsocksfirewallproxystate " + service + " on");
                QProcess::execute("/usr/sbin/networksetup -setsocksfirewallproxy " + service + " " + address + " " + QSTRN(socksPort));
            }
        }

#endif
        //
        // Trigger plugin events
        QMap<Events::SystemProxy::SystemProxyType, int> portSettings;
        if (hasHTTP)
            portSettings.insert(Events::SystemProxy::SystemProxyType::SystemProxy_HTTP, httpPort);
        if (hasSOCKS)
            portSettings.insert(Events::SystemProxy::SystemProxyType::SystemProxy_SOCKS, socksPort);
        PluginHost->Send_SystemProxyEvent(
            Events::SystemProxy::EventObject{ portSettings, Events::SystemProxy::SystemProxyStateType::SystemProxyState_SetProxy });
    }

    void ClearSystemProxy()
    {
        LOG(MODULE_PROXY, "Clearing System Proxy")

#ifdef Q_OS_WIN
        LOG(MODULE_PROXY, "Cleaning system proxy settings.")
        INTERNET_PER_CONN_OPTION_LIST list;
        BOOL bReturn;
        DWORD dwBufSize = sizeof(list);
        // Fill out list struct.
        list.dwSize = sizeof(list);
        // nullptr == LAN, otherwise connectoid name.
        list.pszConnection = nullptr;
        // Set three options.
        list.dwOptionCount = 1;
        list.pOptions = new INTERNET_PER_CONN_OPTION[list.dwOptionCount];

        // Make sure the memory was allocated.
        if (nullptr == list.pOptions)
        {
            // Return FALSE if the memory wasn't allocated.
            LOG(MODULE_PROXY, "Failed to allocat memory in DisableConnectionProxy()")
        }

        // Set flags.
        list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
        list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT;
        //
        // Set the options on the connection.
        bReturn = InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, dwBufSize);
        delete[] list.pOptions;
        InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
        InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
#elif defined(Q_OS_LINUX)
        QStringList actions;
        const bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE";
        const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        // Setting System Proxy Mode to: None
        {
            // for GNOME:
            {
                actions << QString("gsettings set org.gnome.system.proxy mode 'none'");
            }

            // for KDE:
            if (isKDE)
            {
                actions << QString("kwriteconfig5 --file %1/kioslaverc --group 'Proxy Settings' --key ProxyType 0").arg(configRoot);
            }
        }

        // Execute the Actions
        for (const auto &action : actions)
        {
            // execute and get the code
            const auto returnCode = QProcess::execute(action);
            // print out the commands and result codes
            DEBUG(MODULE_PROXY, QString("[%1] %2").arg(QSTRN(returnCode), action))
        }

#else
        for (auto service : macOSgetNetworkServices())
        {
            LOG(MODULE_PROXY, "Clearing proxy for interface: " + service)
            QProcess::execute("/usr/sbin/networksetup -setautoproxystate " + service + " off");
            QProcess::execute("/usr/sbin/networksetup -setwebproxystate " + service + " off");
            QProcess::execute("/usr/sbin/networksetup -setsecurewebproxystate " + service + " off");
            QProcess::execute("/usr/sbin/networksetup -setsocksfirewallproxystate " + service + " off");
        }

#endif
        //
        // Trigger plugin events
        PluginHost->Send_SystemProxyEvent(
            Events::SystemProxy::EventObject{ {}, Events::SystemProxy::SystemProxyStateType::SystemProxyState_ClearProxy });
    }
} // namespace Qv2ray::components::proxy
