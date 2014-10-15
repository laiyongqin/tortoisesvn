// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2014 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "SyncCommand.h"
#include "registry.h"
#include "TSVNPath.h"
#include "SimpleIni.h"
#include "SmartHandle.h"
#include "StringUtils.h"
#include "UnicodeUtils.h"
#include "PathUtils.h"
#include "AppUtils.h"
#include "PasswordDlg.h"

#define TSVN_SYNC_VERSION       1
#define TSVN_SYNC_VERSION_STR   L"1"

// registry entries
std::vector<CString> regUseArray = {
    L"TortoiseSVN\\*",
    L"TortoiseSVN\\RevisionGraph\\*",
    L"TortoiseSVN\\Colors\\*",
    L"TortoiseSVN\\History\\**",
    L"TortoiseSVN\\History\\repoURLS\\**",
    L"TortoiseSVN\\History\\repoPaths\\**",
    L"TortoiseSVN\\LogCache\\*",
    L"TortoiseSVN\\Servers\\global\\**",
    L"TortoiseSVN\\StatusColumns\\*",
    L"TortoiseMerge\\*",
};

std::vector<CString> regBlockArray = {
    L"*debug*",
    L"syncpath",
    L"syncpw",
    L"synccounter",
    L"synclast",
    L"checknewerweek",
    L"currentversion",
    L"diff",
    L"hooks",
    L"merge",
    L"tblamepos",
    L"tblamesize",
    L"tblamestate",
    L"*windowpos",
    L"nocontextpaths",
    L"configdir",
    L"defaultcheckoutpath",
    L"lastcheckoutpath",
    L"erroroccurred",
    L"newversion",
    L"newversiontext",
    L"newversionlink",
    L"historyhintshown",
    L"mergewcurl",
    L"scintilladirect2d",
    L"udiffpagesetup*",
    L"monitorfirststart",
};


bool SyncCommand::Execute()
{
    bool bRet = false;
    CRegString rSyncPath(L"Software\\TortoiseSVN\\SyncPath");
    CTSVNPath syncPath = CTSVNPath(CString(rSyncPath));
    CRegDWORD regCount(L"Software\\TortoiseSVN\\SyncCounter");
    if (syncPath.IsEmpty() && !parser.HasKey(L"askforpath"))
    {
        return false;
    }
    syncPath.AppendPathString(L"tsvnsync.ini");

    if (parser.HasKey(L"askforpath"))
    {
        // ask for the path first, then for the password
        // this is used for a manual import/export
        CString path;
        bool bGotPath = !!CAppUtils::FileOpenSave(path, NULL, IDS_SYNC_SETTINGSFILE, IDS_COMMONFILEFILTER,
                                                  !!parser.HasKey(L"load"), L"", NULL);
        if (bGotPath)
            syncPath = CTSVNPath(path);
        else
            return false;
    }


    CSimpleIni iniFile;
    iniFile.SetMultiLine(true);

    CAutoRegKey hMainKey;
    RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\TortoiseSVN", 0, KEY_READ, hMainKey.GetPointer());
    FILETIME filetime = { 0 };
    RegQueryInfoKey(hMainKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &filetime);

    bool bCloudIsNewer = false;
    if (!parser.HasKey(L"save"))
    {
        // open the file in read mode
        CAutoFile hFile = CreateFile(syncPath.GetWinPathString(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile.IsValid())
        {
            // load the file
            LARGE_INTEGER fsize = { 0 };
            if (GetFileSizeEx(hFile, &fsize))
            {
                auto filebuf = std::make_unique<char[]>(fsize.QuadPart);
                DWORD bytesread = 0;
                if (ReadFile(hFile, filebuf.get(), DWORD(fsize.QuadPart), &bytesread, NULL))
                {
                    // decrypt the file contents
                    std::string encrypted = std::string(filebuf.get(), bytesread);
                    CRegString regPW(L"Software\\TortoiseSVN\\SyncPW");
                    CString password;
                    if (parser.HasKey(L"askforpath") && parser.HasKey(L"load"))
                    {
                        INT_PTR dlgret = 0;
                        bool bPasswordMatches = true;
                        do
                        {
                            bPasswordMatches = true;
                            CPasswordDlg passDlg;
                            passDlg.m_bForSave = !!parser.HasKey(L"save");
                            dlgret = passDlg.DoModal();
                            password = passDlg.m_sPW1;
                            if ((dlgret == IDOK) && (parser.HasKey(L"load")))
                            {
                                std::string passworda = CUnicodeUtils::StdGetUTF8((LPCWSTR)password);
                                std::string decrypted = CStringUtils::Decrypt(encrypted, passworda);
                                if ((decrypted.size() < 3) || (decrypted.substr(0, 3) != "***"))
                                {
                                    bPasswordMatches = false;
                                }
                            }
                        } while ((dlgret == IDOK) && !bPasswordMatches);
                        if (dlgret != IDOK)
                            return false;
                    }
                    else
                    {
                        auto passwordbuf = CStringUtils::Decrypt(CString(regPW));
                        if (passwordbuf.get())
                        {
                            password = passwordbuf.get();
                        }
                        else
                        {
                            // password does not match or it couldn't be read from
                            // the registry!
                            // 
                            TaskDialog(GetExplorerHWND(), AfxGetResourceHandle(), MAKEINTRESOURCE(IDS_APPNAME), MAKEINTRESOURCE(IDS_ERR_ERROROCCURED), MAKEINTRESOURCE(IDS_SYNC_WRONGPASSWORD), TDCBF_OK_BUTTON, TD_ERROR_ICON, NULL);
                            CString sCmd = L" /command:settings /page:20";
                            CAppUtils::RunTortoiseProc(sCmd);
                            return false;
                        }
                    }
                    std::string passworda = CUnicodeUtils::StdGetUTF8((LPCWSTR)password);
                    std::string decrypted = CStringUtils::Decrypt(encrypted, passworda);
                    if (decrypted.size() >= 3)
                    {
                        if (decrypted.substr(0, 3) == "***")
                        {
                            decrypted = decrypted.substr(3);
                            // pass the decrypted data to the ini file
                            iniFile.LoadFile(decrypted.c_str(), decrypted.size());
                            int inicount = _wtoi(iniFile.GetValue(L"sync", L"synccounter", L""));
                            if (inicount != 0)
                            {
                                if (int(DWORD(regCount)) < inicount)
                                {
                                    bCloudIsNewer = true;
                                    regCount = inicount;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (parser.HasKey(L"load"))
        bCloudIsNewer = true;
    if (parser.HasKey(L"save"))
        bCloudIsNewer = false;

    bool bHaveChanges = false;
    CString sValue;

    // go through all registry values and update either the registry
    // or the ini file, depending on which is newer
    for (const auto& regname : regUseArray)
    {
        CAutoRegKey hKey;
        CAutoRegKey hKeyKey;
        DWORD regtype = 0;
        DWORD regsize = 0;
        CString sKeyPath = L"Software";
        CString sValuePath = regname;
        CString sIniKeyName = regname;
        CString sRegname = regname;
        if (regname.Find('\\') >= 0)
        {
            // handle values in sub-keys
            sKeyPath = L"Software\\" + regname.Left(regname.ReverseFind('\\'));
            sValuePath = regname.Mid(regname.ReverseFind('\\') + 1);
        }
        if (RegOpenKeyEx(HKEY_CURRENT_USER, sKeyPath, 0, KEY_READ, hKey.GetPointer()) == ERROR_SUCCESS)
        {
            bool bEnum = false;
            bool bEnumKeys = false;
            int index = 0;
            int keyindex = 0;
            // an asterisk means: use all values inside the specified key
            if (sValuePath == L"*")
                bEnum = true;
            if (sValuePath == L"**")
            {
                bEnumKeys = true;
                bEnum = true;
                RegOpenKeyEx(HKEY_CURRENT_USER, sKeyPath, 0, KEY_READ, hKeyKey.GetPointer());
            }
            do
            {
                if (bEnumKeys)
                {
                    bEnum = true;
                    index = 0;
                    wchar_t cKeyName[MAX_PATH] = { 0 };
                    DWORD cLen = _countof(cKeyName);
                    if (RegEnumKeyEx(hKeyKey, keyindex, cKeyName, &cLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    {
                        bEnumKeys = false;
                        break;
                    }
                    ++keyindex;
                    sKeyPath = L"Software\\" + regname.Left(regname.ReverseFind('\\')) + L"\\" + cKeyName + L"\\";
                    sRegname = regname.Left(regname.ReverseFind('\\')) + L"\\" + cKeyName + L"\\";
                    hKey.CloseHandle();
                    if (RegOpenKeyEx(HKEY_CURRENT_USER, sKeyPath, 0, KEY_READ, hKey.GetPointer()) != ERROR_SUCCESS)
                    {
                        bEnumKeys = false;
                        break;
                    }
                }
                do
                {
                    if (bEnum)
                    {
                        // start enumerating all values
                        wchar_t cValueName[MAX_PATH] = { 0 };
                        DWORD cLen = _countof(cValueName);
                        if (RegEnumValue(hKey, index, cValueName, &cLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                        {
                            bEnum = false;
                            break;
                        }
                        ++index;
                        sValuePath = cValueName;
                        CString sValueLower = sValuePath;
                        sValueLower.MakeLower();
                        bool bIgnore = false;
                        for (const auto& ignore : regBlockArray)
                        {
                            if (wcswildcmp(ignore, sValueLower))
                            {
                                bIgnore = true;
                                break;
                            }
                        }
                        if (bIgnore)
                            continue;
                        sIniKeyName = sRegname.Left(sRegname.ReverseFind('\\'));
                        if (sIniKeyName.IsEmpty())
                            sIniKeyName = sValuePath;
                        else
                            sIniKeyName += L"\\" + sValuePath;
                    }
                    if (RegQueryValueEx(hKey, sValuePath, NULL, &regtype, NULL, &regsize) == ERROR_SUCCESS)
                    {
                        if (regtype != 0)
                        {
                            auto regbuf = std::make_unique<BYTE[]>(regsize);
                            if (RegQueryValueEx(hKey, sValuePath, NULL, &regtype, regbuf.get(), &regsize) == ERROR_SUCCESS)
                            {
                                switch (regtype)
                                {
                                    case REG_DWORD:
                                    {
                                        DWORD value = *(DWORD*)regbuf.get();
                                        sValue = iniFile.GetValue(L"registry_dword", sIniKeyName);
                                        DWORD nValue = DWORD(_wtol(sValue));
                                        if (nValue != value)
                                        {
                                            if (bCloudIsNewer)
                                            {
                                                RegSetValueEx(hKey, sValuePath, NULL, regtype, (BYTE *)&nValue, sizeof(nValue));
                                            }
                                            else
                                            {
                                                bHaveChanges = true;
                                                sValue.Format(L"%ld", value);
                                                iniFile.SetValue(L"registry_dword", sIniKeyName, sValue);
                                            }
                                        }
                                        if (bCloudIsNewer)
                                            iniFile.Delete(L"registry_dword", sIniKeyName);
                                    }
                                        break;
                                    case REG_QWORD:
                                    {
                                        QWORD value = *(QWORD*)regbuf.get();
                                        sValue = iniFile.GetValue(L"registry_qword", sIniKeyName);
                                        QWORD nValue = QWORD(_wtoi64(sValue));
                                        if (nValue != value)
                                        {
                                            if (bCloudIsNewer)
                                            {
                                                RegSetValueEx(hKey, sValuePath, NULL, regtype, (BYTE *)&nValue, sizeof(nValue));
                                            }
                                            else
                                            {
                                                bHaveChanges = true;
                                                sValue.Format(L"%I64d", value);
                                                iniFile.SetValue(L"registry_qword", sIniKeyName, sValue);
                                            }
                                        }
                                        if (bCloudIsNewer)
                                            iniFile.Delete(L"registry_qword", sIniKeyName);
                                    }
                                        break;
                                    case REG_EXPAND_SZ:
                                    case REG_MULTI_SZ:
                                    case REG_SZ:
                                    {
                                        sValue = (LPCWSTR)regbuf.get();
                                        CString iniValue = iniFile.GetValue(L"registry_string", sIniKeyName);
                                        if (iniValue != sValue)
                                        {
                                            if (bCloudIsNewer)
                                            {
                                                RegSetValueEx(hKey, sValuePath, NULL, regtype, (BYTE *)(LPCWSTR)iniValue, (iniValue.GetLength() + 1)*sizeof(WCHAR));
                                            }
                                            else
                                            {
                                                bHaveChanges = true;
                                                iniFile.SetValue(L"registry_string", sIniKeyName, sValue);
                                            }
                                        }
                                        if (bCloudIsNewer)
                                            iniFile.Delete(L"registry_string", sIniKeyName);
                                    }
                                        break;
                                }
                            }
                        }
                    }
                } while (bEnum);
            } while (bEnumKeys);
        }
    }

    if (bCloudIsNewer)
    {
        CString regpath = L"Software\\";

        CSimpleIni::TNamesDepend keys;
        iniFile.GetAllKeys(L"registry_dword", keys);
        for (const auto& k : keys)
        {
            CRegDWORD reg(regpath + k);
            reg = _wtol(iniFile.GetValue(L"registry_dword", k, L""));
        }

        keys.clear();
        iniFile.GetAllKeys(L"registry_qword", keys);
        for (const auto& k : keys)
        {
            CRegQWORD reg(regpath + k);
            reg = _wtoi64(iniFile.GetValue(L"registry_qword", k, L""));
        }

        keys.clear();
        iniFile.GetAllKeys(L"registry_string", keys);
        for (const auto& k : keys)
        {
            CRegString reg(regpath + k);
            reg = CString(iniFile.GetValue(L"registry_string", k, L""));
        }
    }
    {
        // sync project monitor settings
        CString sDataFilePath = CPathUtils::GetAppDataDirectory();
        sDataFilePath += L"\\MonitoringData.ini";
        CSimpleIni monitorIni;
        if (bCloudIsNewer)
        {
            CSimpleIni origMonitorIni;
            origMonitorIni.LoadFile(sDataFilePath);

            CSimpleIni::TNamesDepend keys;
            iniFile.GetAllKeys(L"ini_monitor", keys);
            for (const auto& k : keys)
            {
                CString sKey = k;
                CString sSection = sKey.Left(sKey.Find('.'));
                sKey = sKey.Mid(sKey.Find('.') + 1);
                if (sKey.CompareNoCase(L"name") == 0)
                {
                    // make sure the non-synced values are still used
                    monitorIni.SetValue(sSection, L"lastchecked", origMonitorIni.GetValue(sSection, L"lastchecked", L"0"));
                    monitorIni.SetValue(sSection, L"lastcheckedrobots", origMonitorIni.GetValue(sSection, L"lastcheckedrobots", L"0"));
                    monitorIni.SetValue(sSection, L"lastHEAD", origMonitorIni.GetValue(sSection, L"lastHEAD", L"0"));
                    monitorIni.SetValue(sSection, L"UnreadItems", origMonitorIni.GetValue(sSection, L"UnreadItems", L"0"));
                    monitorIni.SetValue(sSection, L"unreadFirst", origMonitorIni.GetValue(sSection, L"unreadFirst", L"0"));
                }
                CString sValue = CString(iniFile.GetValue(L"ini_monitor", k, L""));
                if ((sKey.Compare(L"username") == 0) || (sKey.Compare(L"password") == 0))
                {
                    sValue = CStringUtils::Encrypt(sValue);
                }
                monitorIni.SetValue(sSection, sKey, sValue);
            }
            FILE * pFile = NULL;
            _tfopen_s(&pFile, sDataFilePath, L"wb");
            monitorIni.SaveFile(pFile);
            fclose(pFile);

            // now send a message to a possible running monitor to force it
            // to reload the ini file. Otherwise it would overwrite the ini
            // file without using the synced data!
            HWND hWnd = FindWindow(NULL, CString(MAKEINTRESOURCE(IDS_MONITOR_DLGTITLE)));
            if (hWnd)
            {
                UINT TSVN_COMMITMONITOR_RELOADINI = RegisterWindowMessage(L"TSVNCommitMonitor_ReloadIni");
                PostMessage(hWnd, TSVN_COMMITMONITOR_RELOADINI, 0, 0);
            }
        }
        else
        {
            monitorIni.LoadFile(sDataFilePath);
            CSimpleIni::TNamesDepend mitems;
            monitorIni.GetAllSections(mitems);
            for (const auto& mitem : mitems)
            {
                CString sSection = mitem;
                CString Name = monitorIni.GetValue(mitem, L"Name", L"");
                if (!Name.IsEmpty())
                {
                    iniFile.SetValue(L"ini_monitor", sSection + L".Name", Name);
                    CString newval = monitorIni.GetValue(mitem, L"WCPathOrUrl", L"");
                    CString oldval = iniFile.GetValue(L"ini_monitor", sSection + L".WCPathOrUrl", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".WCPathOrUrl", newval);

                    newval = monitorIni.GetValue(mitem, L"interval", L"5");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".interval", L"0");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".interval", newval);

                    newval = monitorIni.GetValue(mitem, L"minminutesinterval", L"0");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".minminutesinterval", L"0");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".minminutesinterval", newval);

                    newval = CStringUtils::Decrypt(monitorIni.GetValue(mitem, L"username", L"")).get();
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".username", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".username", newval);

                    newval = CStringUtils::Decrypt(monitorIni.GetValue(mitem, L"password", L"")).get();
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".password", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".password", newval);

                    newval = monitorIni.GetValue(mitem, L"MsgRegex", L"");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".MsgRegex", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".MsgRegex", newval);

                    newval = monitorIni.GetValue(mitem, L"ignoreauthors", L"");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".ignoreauthors", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".ignoreauthors", newval);

                    newval = monitorIni.GetValue(mitem, L"parentTreePath", L"");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".parentTreePath", L"");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".parentTreePath", newval);
                }
                else if (sSection.CompareNoCase(L"global") == 0)
                {
                    CString newval = monitorIni.GetValue(mitem, L"PlaySound", L"1");
                    CString oldval = iniFile.GetValue(L"ini_monitor", sSection + L".PlaySound", L"1");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".PlaySound", newval);

                    newval = monitorIni.GetValue(mitem, L"ShowNotifications", L"1");
                    oldval = iniFile.GetValue(L"ini_monitor", sSection + L".ShowNotifications", L"1");
                    bHaveChanges |= newval != oldval;
                    iniFile.SetValue(L"ini_monitor", sSection + L".ShowNotifications", newval);
                }
            }
        }
    }

    {
        // sync TortoiseMerge regex filters
        CString sDataFilePath = CPathUtils::GetAppDataDirectory();
        sDataFilePath += L"\\regexfilters.ini";
        CSimpleIni regexIni;
        if (bCloudIsNewer)
        {
            CSimpleIni origRegexIni;
            origRegexIni.LoadFile(sDataFilePath);

            CSimpleIni::TNamesDepend keys;
            iniFile.GetAllKeys(L"ini_tmergeregex", keys);
            for (const auto& k : keys)
            {
                CString sKey = k;
                CString sSection = sKey.Left(sKey.Find('.'));
                sKey = sKey.Mid(sKey.Find('.') + 1);
                CString sValue = CString(iniFile.GetValue(L"ini_tmergeregex", k, L""));
                regexIni.SetValue(sSection, sKey, sValue);
            }
            FILE * pFile = NULL;
            _tfopen_s(&pFile, sDataFilePath, L"wb");
            regexIni.SaveFile(pFile);
            fclose(pFile);
        }
        else
        {
            regexIni.LoadFile(sDataFilePath);
            CSimpleIni::TNamesDepend mitems;
            regexIni.GetAllSections(mitems);
            for (const auto& mitem : mitems)
            {
                CString sSection = mitem;

                CString newval = regexIni.GetValue(mitem, L"regex", L"");
                CString oldval = iniFile.GetValue(L"ini_tmergeregex", sSection + L".regex", L"");
                bHaveChanges |= newval != oldval;
                iniFile.SetValue(L"ini_tmergeregex", sSection + L".regex", newval);

                newval = regexIni.GetValue(mitem, L"replace", L"5");
                oldval = iniFile.GetValue(L"ini_tmergeregex", sSection + L".replace", L"0");
                bHaveChanges |= newval != oldval;
                iniFile.SetValue(L"ini_tmergeregex", sSection + L".replace", newval);
            }
        }
    }


    if (bHaveChanges)
    {
        iniFile.SetValue(L"sync", L"version", TSVN_SYNC_VERSION_STR);
        DWORD count = regCount;
        ++count;
        regCount = count;
        CString tmp;
        tmp.Format(L"%d", count);
        iniFile.SetValue(L"sync", L"synccounter", tmp);

        // save the ini file
        std::string iniData;
        iniFile.SaveString(iniData);
        iniData = "***" + iniData;
        // encrypt the string

        CString password;
        if (parser.HasKey(L"askforpath"))
        {
            CPasswordDlg passDlg;
            passDlg.m_bForSave = true;
            if (passDlg.DoModal() != IDOK)
                return false;
            password = passDlg.m_sPW1;
        }
        else
        {
            CRegString regPW(L"Software\\TortoiseSVN\\SyncPW");
            auto passwordbuf = CStringUtils::Decrypt(CString(regPW));
            if (passwordbuf.get())
            {
                password = passwordbuf.get();
            }
        }
        std::string passworda = CUnicodeUtils::StdGetUTF8((LPCWSTR)password);

        std::string encrypted = CStringUtils::Encrypt(iniData, passworda);
        CPathUtils::MakeSureDirectoryPathExists(syncPath.GetContainingDirectory().GetWinPathString());
        CAutoFile hFile = CreateFile(syncPath.GetWinPathString(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile.IsValid())
        {
            DWORD written = 0;
            WriteFile(hFile, encrypted.c_str(), DWORD(encrypted.size()), &written, NULL);
        }

        bRet = true;
    }


    return bRet;
}
