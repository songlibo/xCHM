/*
  Copyright (C) 2003 - 2019  Razvan Cojocaru <rzvncj@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
  MA 02110-1301, USA.
*/

#include <chmhtmlnotebook.h>
#include <chminputstream.h>
#include <chmlistctrl.h>
#include <chmsearchpanel.h>
#include <hhcparser.h>
#include <wx/config.h>
#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/wx.h>

namespace {

inline bool IS_WHITESPACE(wxChar c)
{
    return c == wxT(' ') || c == wxT('\n') || c == wxT('\r') || c == wxT('\t');
}

} // end of anonymous namespace

CHMSearchPanel::CHMSearchPanel(wxWindow* parent, wxTreeCtrl* topics, CHMHtmlNotebook* nbhtml)
    : wxPanel(parent), _tcl(topics), _text(nullptr), _partial(nullptr), _titles(nullptr), _search(nullptr),
      _results(nullptr), _nbhtml(nbhtml)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    SetAutoLayout(true);
    SetSizer(sizer);

    _text = new wxTextCtrl(this, ID_SearchText, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

    _partial = new wxCheckBox(this, -1, _("Get partial matches"));
    _titles  = new wxCheckBox(this, -1, _("Search titles only"));
    _search  = new wxButton(this, ID_SearchButton, _("Search"));

#if wxUSE_TOOLTIPS
    _partial->SetToolTip(_("Allow partial matches."));
    _titles->SetToolTip(_("Only search in the contents' titles."));
    _search->SetToolTip(_("Search contents for occurences of the specified text."));
#endif
    _results = new CHMListCtrl(this, nbhtml, ID_Results);

    sizer->Add(_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 2);
    sizer->Add(_partial, 0, wxLEFT | wxRIGHT | wxTOP, 10);
    sizer->Add(_titles, 0, wxLEFT | wxRIGHT, 10);
    sizer->Add(_search, 0, wxALL, 10);
    sizer->Add(_results, 1, wxALL | wxEXPAND, 2);

    GetConfig();
}

CHMSearchPanel::~CHMSearchPanel()
{
    SetConfig();
}

void CHMSearchPanel::OnSearch(wxCommandEvent&)
{
    wxBusyCursor bcr;

    _results->Reset();

    auto     sr = _text->GetLineText(0);
    wxString word;

    if (sr.IsEmpty())
        return;

    auto chmf = CHMInputStream::GetCache();

    if (!chmf)
        return;

    sr.MakeLower();
    sr.Replace(wxT("+"), wxT(" "), true);
    sr.Replace(wxT("-"), wxT(" "), true);
    sr.Replace(wxT("#"), wxT(" "), true);
    sr.Replace(wxT("@"), wxT(" "), true);
    sr.Replace(wxT("^"), wxT(" "), true);
    sr.Replace(wxT("&"), wxT(" "), true);
    sr.Replace(wxT("%"), wxT(" "), true);

    wxStringTokenizer tkz(sr, wxT(" \t\r\n"));

    while (word.IsEmpty())
        if (tkz.HasMoreTokens())
            word = tkz.GetNextToken();

    CHMSearchResults h1;
    chmf->IndexSearch(word, !_partial->IsChecked(), _titles->IsChecked(), &h1);

    while (tkz.HasMoreTokens()) {
        auto token = tkz.GetNextToken();
        if (token.IsEmpty())
            continue;

        CHMSearchResults h2, tmp;
        chmf->IndexSearch(token, !_partial->IsChecked(), _titles->IsChecked(), &h2);

        if (!h2.empty()) {
            for (auto&& item : h2)
                if (h1.find(item.first) != h1.end())
                    tmp[item.first] = item.second;
            h1 = tmp;
        } else {
            h1.clear();
            break;
        }
    }

    if (_titles->IsChecked() && h1.empty()) {
        PopulateList(_tcl->GetRootItem(), sr, !_partial->IsChecked());
        _results->UpdateUI();
        return;
    }

    for (auto&& item : h1) {
        auto url = item.first.StartsWith(wxT("/")) ? item.first : (wxT("/") + item.first);
        _results->AddPairItem(item.second, url);
    }

    _results->UpdateUI();
}

void CHMSearchPanel::PopulateList(wxTreeItemId root, wxString& text, bool wholeWords)
{
    static auto chmf = CHMInputStream::GetCache();

    if (!chmf)
        return;

    auto data = reinterpret_cast<URLTreeItem*>(_tcl->GetItemData(root));

    if (data && (!data->_url.IsEmpty())) {
        wxString title = _tcl->GetItemText(root);
        if (TitleSearch(title, text, false, wholeWords))
            _results->AddPairItem(title, data->_url);
    }

    wxTreeItemIdValue cookie;
    auto              child = _tcl->GetFirstChild(root, cookie);

    for (auto i = 0UL; i < _tcl->GetChildrenCount(root, false); ++i) {
        PopulateList(child, text, wholeWords);
        child = _tcl->GetNextChild(root, cookie);
    }
}

bool CHMSearchPanel::TitleSearch(const wxString& title, wxString& text, bool caseSensitive, bool wholeWords)
{
    size_t i, j;
    auto   lng   = title.Length();
    auto   found = false;
    auto   tmp   = title;

    if (!caseSensitive) {
        tmp.MakeLower();
        text.MakeLower();
    }

    wxStringTokenizer tkz(text, wxT(" \t\r\n"));

    while (tkz.HasMoreTokens()) {
        found      = false;
        auto token = tkz.GetNextToken();

        if (token.IsEmpty())
            continue;

        auto wrd  = token.Length();
        auto buf1 = tmp.c_str();
        auto buf2 = token.c_str();

        if (wholeWords) {
            for (i = 0; i < lng - wrd + 1; ++i) {
                if (IS_WHITESPACE(buf1[i]))
                    continue;

                j = 0;

                while (j < wrd && buf1[i + j] == buf2[j])
                    ++j;

                if (j == wrd && (IS_WHITESPACE(buf1[i + j]) || i + j == lng))
                    if (i == 0 || IS_WHITESPACE(buf1[i - 1])) {
                        found = true;
                        break;
                    }
            }
        } else {
            for (i = 0; i < lng - wrd + 1; ++i) {
                j = 0;

                while ((j < wrd) && (buf1[i + j] == buf2[(size_t)j]))
                    ++j;

                if (j == wrd && (i == 0 || IS_WHITESPACE(buf1[i - 1]))) {
                    found = true;
                    break;
                }
            }
        }

        if (!found)
            break;
    }

    return found;
}

void CHMSearchPanel::OnSearchSel(wxListEvent&)
{
    _results->LoadSelected();
}

void CHMSearchPanel::Reset()
{
    _text->Clear();
    _results->Reset();
}

void CHMSearchPanel::SetNewFont(const wxFont& font)
{
    _results->SetFont(font);
}

void CHMSearchPanel::SetConfig()
{
    wxConfig config(wxT("xchm"));

    config.Write(wxT("/Search/partialWords"), (long)_partial->GetValue());
    config.Write(wxT("/Search/titlesOnly"), (long)_titles->GetValue());
}

void CHMSearchPanel::GetConfig()
{
    long     partial, titles;
    wxConfig config(wxT("xchm"));

    if (config.Read(wxT("/Search/partialWords"), &partial)) {
        config.Read(wxT("/Search/titlesOnly"), &titles);
        _partial->SetValue(partial);
        _titles->SetValue(titles);
    }
}

BEGIN_EVENT_TABLE(CHMSearchPanel, wxPanel)
EVT_LIST_ITEM_SELECTED(ID_Results, CHMSearchPanel::OnSearchSel)
EVT_BUTTON(ID_SearchButton, CHMSearchPanel::OnSearch)
EVT_TEXT_ENTER(ID_SearchText, CHMSearchPanel::OnSearch)
END_EVENT_TABLE()
