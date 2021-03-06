// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2014 - TortoiseSVN

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
#include "TortoiseProc.h"
#include "SwitchDlg.h"
#include "RepositoryBrowser.h"
#include "BrowseFolder.h"
#include "TSVNPath.h"
#include "AppUtils.h"
#include "PathUtils.h"

IMPLEMENT_DYNAMIC(CSwitchDlg, CResizableStandAloneDialog)
CSwitchDlg::CSwitchDlg(CWnd* pParent /*=NULL*/)
    : CResizableStandAloneDialog(CSwitchDlg::IDD, pParent)
    , m_URL(L"")
    , Revision(L"HEAD")
    , m_pLogDlg(NULL)
    , m_bNoExternals(CRegDWORD(L"Software\\TortoiseSVN\\noext"))
    , m_bStickyDepth(FALSE)
    , m_bIgnoreAncestry(FALSE)
    , m_bFolder(false)
    , m_height(0)
    , m_depth(svn_depth_unknown)
{
}

CSwitchDlg::~CSwitchDlg()
{
    delete m_pLogDlg;
}

void CSwitchDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableStandAloneDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_URLCOMBO, m_URLCombo);
    DDX_Text(pDX, IDC_REVISION_NUM, m_rev);
    DDX_Control(pDX, IDC_SWITCHPATH, m_SwitchPath);
    DDX_Control(pDX, IDC_DESTURL, m_DestUrl);
    DDX_Control(pDX, IDC_SRCURL, m_SrcUrl);
    DDX_Check(pDX, IDC_NOEXTERNALS, m_bNoExternals);
    DDX_Check(pDX, IDC_STICKYDEPTH, m_bStickyDepth);
    DDX_Check(pDX, IDC_IGNOREANCESTRY, m_bIgnoreAncestry);
    DDX_Control(pDX, IDC_DEPTH, m_depthCombo);
}


BEGIN_MESSAGE_MAP(CSwitchDlg, CResizableStandAloneDialog)
    ON_BN_CLICKED(IDC_BROWSE, OnBnClickedBrowse)
    ON_BN_CLICKED(IDHELP, OnBnClickedHelp)
    ON_EN_CHANGE(IDC_REVISION_NUM, &CSwitchDlg::OnEnChangeRevisionNum)
    ON_BN_CLICKED(IDC_LOG, &CSwitchDlg::OnBnClickedLog)
    ON_REGISTERED_MESSAGE(WM_REVSELECTED, &CSwitchDlg::OnRevSelected)
    ON_WM_SIZING()
    ON_CBN_EDITCHANGE(IDC_URLCOMBO, &CSwitchDlg::OnCbnEditchangeUrlcombo)
END_MESSAGE_MAP()

BOOL CSwitchDlg::OnInitDialog()
{
    CResizableStandAloneDialog::OnInitDialog();
    CAppUtils::MarkWindowAsUnpinnable(m_hWnd);

    ExtendFrameIntoClientArea(IDC_REVGROUP);
    m_aeroControls.SubclassControl(this, IDC_IGNOREANCESTRY);
    m_aeroControls.SubclassOkCancelHelp(this);

    CTSVNPath svnPath(m_path);
    SetDlgItemText(IDC_SWITCHPATH, m_path);
    m_bFolder = svnPath.IsDirectory();
    SVN svn;
    CString sUUID;
    m_repoRoot = svn.GetRepositoryRootAndUUID(svnPath, true, sUUID);
    m_repoRoot.TrimRight('/');
    CString url = svn.GetURLFromPath(svnPath);
    CString destUrl = url;
    if (!m_URL.IsEmpty())
    {
        destUrl = m_URL;
    }
    m_URLCombo.LoadHistory(L"Software\\TortoiseSVN\\History\\repoPaths\\"+sUUID, L"url");
    m_URLCombo.SetCurSel(0);
    if (!url.IsEmpty())
    {
        CString relPath = destUrl.Mid(m_repoRoot.GetLength());
        relPath = CPathUtils::PathUnescape(relPath);
        relPath.Replace('\\', '/');
        m_URLCombo.AddString(relPath, 0);
        m_URLCombo.SelectString(-1, relPath);
        m_URL = destUrl;

        SetDlgItemText(IDC_SRCURL, CTSVNPath(url).GetUIPathString());
        SetDlgItemText(IDC_DESTURL, CTSVNPath(CPathUtils::CombineUrls(m_repoRoot, relPath)).GetUIPathString());
    }

    GetWindowText(m_sTitle);
    CAppUtils::SetWindowTitle(m_hWnd, m_path, m_sTitle);

    // set head revision as default revision
    SetRevision(SVNRev::REV_HEAD);

    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_WORKING)));
    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_INFINITE)));
    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_IMMEDIATE)));
    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_FILES)));
    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_EMPTY)));
    m_depthCombo.AddString(CString(MAKEINTRESOURCE(IDS_SVN_DEPTH_EXCLUDE)));
    m_depthCombo.SetCurSel(0);

    m_tooltips.Create(this);
    m_tooltips.AddTool(IDC_STICKYDEPTH, IDS_SWITCH_STICKYDEPTH_TT);

    UpdateData(FALSE);

    RECT rect;
    GetWindowRect(&rect);
    m_height = rect.bottom - rect.top;

    AddAnchor(IDC_SWITCHLABEL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_SWITCHPATH, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_URLLABEL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_URLCOMBO, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_BROWSE, TOP_RIGHT);
    AddAnchor(IDC_SRCLABEL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_SRCURL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_DESTLABEL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_DESTURL, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_REVGROUP, TOP_LEFT);
    AddAnchor(IDC_REVISION_HEAD, TOP_LEFT);
    AddAnchor(IDC_REVISION_N, TOP_LEFT);
    AddAnchor(IDC_REVISION_NUM, TOP_LEFT);
    AddAnchor(IDC_LOG, TOP_LEFT);
    AddAnchor(IDC_GROUPMIDDLE, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_DEPTH, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_STICKYDEPTH, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_NOEXTERNALS, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_IGNOREANCESTRY, BOTTOM_LEFT, BOTTOM_RIGHT);
    AddAnchor(IDOK, BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);
    AddAnchor(IDHELP, BOTTOM_RIGHT);

    if ((m_pParentWnd==NULL)&&(GetExplorerHWND()))
        CenterWindow(CWnd::FromHandle(GetExplorerHWND()));
    EnableSaveRestore(L"SwitchDlg");
    return TRUE;
}

void CSwitchDlg::OnBnClickedBrowse()
{
    UpdateData();
    SVNRev rev;
    if (GetCheckedRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N) == IDC_REVISION_HEAD)
    {
        rev = SVNRev::REV_HEAD;
    }
    else
        rev = SVNRev(m_rev);
    if (!rev.IsValid())
        rev = SVNRev::REV_HEAD;
    CAppUtils::BrowseRepository(m_repoRoot, m_URLCombo, this, rev);
    SetRevision(rev);
}

void CSwitchDlg::OnCancel()
{
    if (::IsWindow(m_pLogDlg->GetSafeHwnd())&&(m_pLogDlg->IsWindowVisible()))
    {
        m_pLogDlg->SendMessage(WM_CLOSE);
        return;
    }

    CResizableStandAloneDialog::OnCancel();
}

void CSwitchDlg::OnOK()
{
    if (!UpdateData(TRUE))
        return; // don't dismiss dialog (error message already shown by MFC framework)

    if (::IsWindow(m_pLogDlg->GetSafeHwnd())&&(m_pLogDlg->IsWindowVisible()))
    {
        m_pLogDlg->SendMessage(WM_CLOSE);
        return;
    }

    // if head revision, set revision as HEAD
    if (GetCheckedRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N) == IDC_REVISION_HEAD)
    {
        m_rev = L"HEAD";
    }
    Revision = SVNRev(m_rev);
    if (!Revision.IsValid())
    {
        ShowEditBalloon(IDC_REVISION_NUM, IDS_ERR_INVALIDREV, IDS_ERR_ERROR, TTI_ERROR);
        return;
    }

    m_URLCombo.SaveHistory();
    m_URL = CPathUtils::CombineUrls(m_repoRoot, m_URLCombo.GetString());

    switch (m_depthCombo.GetCurSel())
    {
    case 0:
        m_depth = svn_depth_unknown;
        break;
    case 1:
        m_depth = svn_depth_infinity;
        break;
    case 2:
        m_depth = svn_depth_immediates;
        break;
    case 3:
        m_depth = svn_depth_files;
        break;
    case 4:
        m_depth = svn_depth_empty;
        break;
    case 5:
        m_depth = svn_depth_exclude;
        break;
    default:
        m_depth = svn_depth_empty;
        break;
    }

    UpdateData(FALSE);

    CRegDWORD regNoExt(L"Software\\TortoiseSVN\\noext");
    regNoExt = m_bNoExternals;

    CResizableStandAloneDialog::OnOK();
}

void CSwitchDlg::OnBnClickedHelp()
{
    OnHelp();
}

void CSwitchDlg::OnEnChangeRevisionNum()
{
    UpdateData();
    if (m_rev.IsEmpty())
        CheckRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N, IDC_REVISION_HEAD);
    else
        CheckRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N, IDC_REVISION_N);
}

void CSwitchDlg::SetRevision(const SVNRev& rev)
{
    if (rev.IsHead())
        CheckRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N, IDC_REVISION_HEAD);
    else
    {
        CheckRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N, IDC_REVISION_N);
        m_rev = rev.ToString();
        UpdateData(FALSE);
    }
}

void CSwitchDlg::OnBnClickedLog()
{
    UpdateData(TRUE);
    if (::IsWindow(m_pLogDlg->GetSafeHwnd())&&(m_pLogDlg->IsWindowVisible()))
        return;
    m_URL = CPathUtils::CombineUrls(m_repoRoot, m_URLCombo.GetString());
    if (!m_URL.IsEmpty())
    {
        delete m_pLogDlg;
        m_pLogDlg = new CLogDlg();
        m_pLogDlg->SetSelect(true);
        m_pLogDlg->m_pNotifyWindow = this;
        m_pLogDlg->m_wParam = 0;
        m_pLogDlg->SetParams(CTSVNPath(m_URL), SVNRev::REV_HEAD, SVNRev::REV_HEAD, 1, TRUE);
        m_pLogDlg->ContinuousSelection(true);
        m_pLogDlg->SetProjectPropertiesPath(CTSVNPath(m_path));
        m_pLogDlg->Create(IDD_LOGMESSAGE, this);
        m_pLogDlg->ShowWindow(SW_SHOW);
    }
    AfxGetApp()->DoWaitCursor(-1);
}

LPARAM CSwitchDlg::OnRevSelected(WPARAM /*wParam*/, LPARAM lParam)
{
    CString temp;
    temp.Format(L"%Id", lParam);
    SetDlgItemText(IDC_REVISION_NUM, temp);
    CheckRadioButton(IDC_REVISION_HEAD, IDC_REVISION_N, IDC_REVISION_N);
    return 0;
}

void CSwitchDlg::OnSizing(UINT fwSide, LPRECT pRect)
{
    // don't allow the dialog to be changed in height
    switch (fwSide)
    {
    case WMSZ_BOTTOM:
    case WMSZ_BOTTOMLEFT:
    case WMSZ_BOTTOMRIGHT:
        pRect->bottom = pRect->top + m_height;
        break;
    case WMSZ_TOP:
    case WMSZ_TOPLEFT:
    case WMSZ_TOPRIGHT:
        pRect->top = pRect->bottom - m_height;
        break;
    }
    CResizableStandAloneDialog::OnSizing(fwSide, pRect);
}

void CSwitchDlg::OnCbnEditchangeUrlcombo()
{
    SetDlgItemText(IDC_DESTURL, CTSVNPath(CPathUtils::CombineUrls(m_repoRoot, m_URLCombo.GetWindowString())).GetUIPathString());
}

BOOL CSwitchDlg::PreTranslateMessage(MSG* pMsg)
{
    m_tooltips.RelayEvent(pMsg);

    return CResizableStandAloneDialog::PreTranslateMessage(pMsg);
}
