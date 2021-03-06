/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include "utils/BaseUtil.h"
#include "utils/BitManip.h"
#include "utils/Log.h"
#include "utils/LogDbg.h"
#include "utils/FileUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/Dpi.h"

#include "wingui/WinGui.h"
#include "wingui/TreeModel.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/StaticCtrl.h"
#include "wingui/ButtonCtrl.h"
#include "wingui/ListBoxCtrl.h"
#include "wingui/DropDownCtrl.h"
#include "wingui/EditCtrl.h"
#include "wingui/TrackbarCtrl.h"

#include "Annotation.h"
#include "EngineBase.h"
#include "EnginePdf.h"
#include "EngineMulti.h"
#include "EngineManager.h"

#include "Translations.h"
#include "SumatraConfig.h"
#include "SettingsStructs.h"
#include "Controller.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "Notifications.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "EditAnnotations.h"

using std::placeholders::_1;

constexpr int borderWidthMin = 0;
constexpr int borderWidthMax = 12;

// clang-format off
// TODO: more
static const char* gAnnotationTypes = "Text\0Free Text\0Stamp\0Caret\0Ink\0Square\0Circle\0Line\0Polygon\0";
static const char *gFileAttachmentUcons = "Graph\0Paperclip\0PushPin\0Tag\0";
static const char *gSoundIcons = "Speaker\0Mic\0";
static const char* gTextIcons = "Comment\0Help\0Insert\0Key\0NewParagraph\0Note\0Paragraph\0";
static const char *gStampIcons = "Approved\0AsIs\0Confidential\0Departmental\0Draft\0Experimental\0Expired\0Final\0ForComment\0ForPublicRelease\0NotApproved\0NotForPublicRelease\0Sold\0TopSecret\0";
static const char *gLineEndingStyles = "None\0Square\0Circle\0Diamond\0OpenArrow\0ClosedArrow\0Butt\0ROpenArrow\0RClosedArrow\0Slash\0";
static const char* gColors = "None\0Aqua\0Black\0Blue\0Fuchsia\0Gray\0Green\0Lime\0Maroon\0Navy\0Olive\0Orange\0Purple\0Red\0Silver\0Teal\0White\0Yellow\0";
static const char *gFontNames = "Cour\0Helv\0TiRo\0";
static const char *gFontReadableNames = "Courier\0Helvetica\0TimesRoman\0";
static const char* gQuaddingNames = "Left\0Center\0Right\0";

static COLORREF gColorsValues[] = {
    //0x00000000, /* transparent */
    ColorUnset, /* transparent */
    //0xff00ffff, /* aqua */
    0xffffff00, /* aqua */
    0xff000000, /* black */
    //0xff0000ff, /* blue */
    0xffff0000, /* blue */
    //0xffff00ff, /* fuchsia */
    0xffff00ff, /* fuchsia */
    0xff808080, /* gray */
    0xff008000, /* green */
    0xff00ff00, /* lime */
    //0xff800000, /* maroon */
    0xff000080, /* maroon */
    //0xff000080, /* navy */
    0xff800000, /* navy */
    //0xff808000, /* olive */
    0xff008080, /* olive */
    //0xffffa500, /* orange */
    0xff00a5ff, /* orange */
    0xff800080, /* purple */
    //0xffff0000, /* red */
    0xff0000ff, /* red */
    0xffc0c0c0, /* silver */
    //0xff008080, /* teal */
    0xff808000, /* teal */
    0xffffffff, /* white */
    //0xffffff00, /* yellow */
    0xff00ffff, /* yellow */
};

AnnotationType gAnnotsWithBorder[] = {
    AnnotationType::FreeText,  AnnotationType::Ink,    AnnotationType::Line,
    AnnotationType::Square,    AnnotationType::Circle, AnnotationType::Polygon,
    AnnotationType::PolyLine,
};

AnnotationType gAnnotsWithInteriorColor[] = {
    AnnotationType::Line, AnnotationType::Square, AnnotationType::Circle,
};

AnnotationType gAnnotsWithColor[] = {
    AnnotationType::Stamp,     AnnotationType::Text,   AnnotationType::FileAttachment,
    AnnotationType::Sound,     AnnotationType::Caret,     AnnotationType::FreeText,
    AnnotationType::Ink,       AnnotationType::Line,      AnnotationType::Square,
    AnnotationType::Circle,    AnnotationType::Polygon,   AnnotationType::PolyLine,
    AnnotationType::Highlight, AnnotationType::Underline, AnnotationType::StrikeOut,
    AnnotationType::Squiggly,
};
// clang-format on

// in SumatraPDF.cpp
extern void RerenderForWindowInfo(WindowInfo*);

const char* GetKnownColorName(COLORREF c) {
    if (c == ColorUnset) {
        return gColors; // first value is "None" for unset
    }
    COLORREF c2 = ColorSetAlpha(c, 0xff);
    int n = (int)dimof(gColorsValues);
    for (int i = 1; i < n; i++) {
        if (c2 == gColorsValues[i]) {
            const char* s = seqstrings::IdxToStr(gColors, i);
            return s;
        }
    }
    return nullptr;
}

struct EditAnnotationsWindow {
    TabInfo* tab = nullptr;
    Window* mainWindow = nullptr;
    LayoutBase* mainLayout = nullptr;

    DropDownCtrl* dropDownAdd = nullptr;

    ListBoxCtrl* listBox = nullptr;
    StaticCtrl* staticRect = nullptr;
    StaticCtrl* staticAuthor = nullptr;
    StaticCtrl* staticModificationDate = nullptr;
    StaticCtrl* staticPopup = nullptr;
    StaticCtrl* staticContents = nullptr;
    EditCtrl* editContents = nullptr;
    StaticCtrl* staticTextAlignment = nullptr;
    DropDownCtrl* dropDownTextAlignment = nullptr;
    StaticCtrl* staticTextFont = nullptr;
    DropDownCtrl* dropDownTextFont = nullptr;
    StaticCtrl* staticTextSize = nullptr;
    TrackbarCtrl* trackbarTextSize = nullptr;
    StaticCtrl* staticTextColor = nullptr;
    DropDownCtrl* dropDownTextColor = nullptr;

    StaticCtrl* staticLineStart = nullptr;
    DropDownCtrl* dropDownLineStart = nullptr;
    StaticCtrl* staticLineEnd = nullptr;
    DropDownCtrl* dropDownLineEnd = nullptr;

    StaticCtrl* staticIcon = nullptr;
    DropDownCtrl* dropDownIcon = nullptr;

    StaticCtrl* staticBorder = nullptr;
    TrackbarCtrl* trackbarBorder = nullptr;

    StaticCtrl* staticColor = nullptr;
    DropDownCtrl* dropDownColor = nullptr;
    StaticCtrl* staticInteriorColor = nullptr;
    DropDownCtrl* dropDownInteriorColor = nullptr;

    StaticCtrl* staticOpacity = nullptr;
    TrackbarCtrl* trackbarOpacity = nullptr;

    ButtonCtrl* buttonSaveAttachment = nullptr;
    ButtonCtrl* buttonEmbedAttachment = nullptr;

    ButtonCtrl* buttonDelete = nullptr;

    StaticCtrl* staticSaveTip = nullptr;
    ButtonCtrl* buttonSavePDF = nullptr;

    ListBoxModel* lbModel = nullptr;

    Vec<Annotation*>* annotations = nullptr;
    // currently selected annotation
    Annotation* annot = nullptr;

    str::Str currTextColor;
    str::Str currCustomColor;
    str::Str currCustomInteriorColor;

    ~EditAnnotationsWindow();
};

static void HidePerAnnotControls(EditAnnotationsWindow* win) {
    win->staticRect->SetIsVisible(false);
    win->staticAuthor->SetIsVisible(false);
    win->staticModificationDate->SetIsVisible(false);
    win->staticPopup->SetIsVisible(false);
    win->staticContents->SetIsVisible(false);
    win->editContents->SetIsVisible(false);
    win->staticTextAlignment->SetIsVisible(false);
    win->dropDownTextAlignment->SetIsVisible(false);
    win->staticTextFont->SetIsVisible(false);
    win->dropDownTextFont->SetIsVisible(false);
    win->staticTextSize->SetIsVisible(false);
    win->trackbarTextSize->SetIsVisible(false);
    win->staticTextColor->SetIsVisible(false);
    win->dropDownTextColor->SetIsVisible(false);

    win->staticLineStart->SetIsVisible(false);
    win->dropDownLineStart->SetIsVisible(false);
    win->staticLineEnd->SetIsVisible(false);
    win->dropDownLineEnd->SetIsVisible(false);

    win->staticIcon->SetIsVisible(false);
    win->dropDownIcon->SetIsVisible(false);

    win->staticBorder->SetIsVisible(false);
    win->trackbarBorder->SetIsVisible(false);
    win->staticColor->SetIsVisible(false);
    win->dropDownColor->SetIsVisible(false);
    win->staticInteriorColor->SetIsVisible(false);
    win->dropDownInteriorColor->SetIsVisible(false);

    win->staticOpacity->SetIsVisible(false);
    win->trackbarOpacity->SetIsVisible(false);

    win->buttonSaveAttachment->SetIsVisible(false);
    win->buttonEmbedAttachment->SetIsVisible(false);

    win->buttonDelete->SetIsVisible(false);
}

static int FindStringInArray(const char* items, const char* toFind, int valIfNotFound = -1) {
    int idx = seqstrings::StrToIdx(items, toFind);
    int i = 0;
    while (*items) {
        if (str::Eq(items, toFind)) {
            return i;
        }
        i++;
        items = seqstrings::SkipStr(items);
    }
    return valIfNotFound;
}

static bool IsAnnotationTypeInArray(AnnotationType* arr, size_t arrSize, AnnotationType toFind) {
    for (size_t i = 0; i < arrSize; i++) {
        if (toFind == arr[i]) {
            return true;
        }
    }
    return false;
}
void DeleteEditAnnotationsWindow(EditAnnotationsWindow* win) {
    delete win;
}

static void DeleteAnnotations(EditAnnotationsWindow* win) {
    int nAnnots = win->annotations->isize();
    for (int i = 0; i < nAnnots; i++) {
        Annotation* a = win->annotations->at(i);
        if (a->pdf) {
            // hacky: only annotations with pdf_annot set belong to us
            delete a;
        }
    }
    delete win->annotations;
    win->annotations = nullptr;
    win->annot = nullptr;
}

EditAnnotationsWindow::~EditAnnotationsWindow() {
    DeleteAnnotations(this);
    delete mainWindow;
    delete mainLayout;
    delete lbModel;
}

static void RebuildAnnotations(EditAnnotationsWindow* win) {
    auto model = new ListBoxModelStrings();
    int n = 0;
    if (win->annotations) {
        n = win->annotations->isize();
    }

    str::Str s;
    for (int i = 0; i < n; i++) {
        auto annot = win->annotations->at(i);
        if (annot->isDeleted) {
            continue;
        }
        s.Reset();
        s.AppendFmt("page %d, ", annot->pageNo);
        s.AppendView(AnnotationReadableName(annot->type));
        model->strings.Append(s.AsView());
    }

    win->listBox->SetModel(model);
    delete win->lbModel;
    win->lbModel = model;
}

static void WndCloseHandler(EditAnnotationsWindow* win, WindowCloseEvent* ev) {
    CrashIf(win->mainWindow != ev->w);
    win->tab->editAnnotsWindow = nullptr;
    delete win;
}

extern void ReloadDocument(WindowInfo* win, bool autorefresh);
static void SetAnnotations(EditAnnotationsWindow* win, TabInfo* tab);
static void UpdateUIForSelectedAnnotation(EditAnnotationsWindow* win, int itemNo);

static void ButtonSavePDFHandler(EditAnnotationsWindow* win) {
    OPENFILENAME ofn = {0};
    TabInfo* tab = win->tab;
    EngineBase* engine = win->tab->AsFixed()->GetEngine();
    WCHAR dstFileName[MAX_PATH + 1] = {0};
    if (IsCtrlPressed()) {
        str::WStr fileFilter(256);
        fileFilter.Append(_TR("PDF documents"));
        fileFilter.Append(L"\1*.pdf\1");
        fileFilter.Append(L"\1*.*\1");
        str::TransChars(fileFilter.Get(), L"\1", L"\0");

        // TODO: automatically construct "foo.pdf" => "foo Copy.pdf"
        const WCHAR* name = engine->FileName();
        str::BufSet(dstFileName, dimof(dstFileName), name);

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = win->mainWindow->hwnd;
        ofn.lpstrFile = dstFileName;
        ofn.nMaxFile = dimof(dstFileName);
        ofn.lpstrFilter = fileFilter.Get();
        ofn.nFilterIndex = 1;
        // ofn.lpstrTitle = _TR("Rename To");
        // ofn.lpstrInitialDir = initDir;
        ofn.lpstrDefExt = L".pdf";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

        bool ok = GetSaveFileNameW(&ofn);
        if (!ok) {
            return;
        }
        AutoFreeStr dstFilePath = strconv::WstrToUtf8(dstFileName);
        EnginePdfSaveUpdated(engine, dstFilePath.as_view());
        // TODO: show a notification if saved or error message if failed to save
        return;
    }

    bool ok = EnginePdfSaveUpdated(engine, {});
    // TODO: show a notification if saved or error message if failed to save
    if (!ok) {
        return;
    }

    // TODO: hacky: set tab->editAnnotsWindow to nullptr to
    // disable a check in ReloadDocuments. Could pass additional argument
    auto tmpWin = tab->editAnnotsWindow;
    tab->editAnnotsWindow = nullptr;
    ReloadDocument(tab->win, false);
    tab->editAnnotsWindow = tmpWin;

    DeleteAnnotations(win);
    SetAnnotations(win, tab);
    UpdateUIForSelectedAnnotation(win, -1);
}

static void EnableSaveIfAnnotationsChanged(EditAnnotationsWindow* win) {
    bool didChange = false;
    if (win->annotations) {
        for (auto& annot : *win->annotations) {
            if (annot->isChanged || annot->isDeleted) {
                didChange = true;
                break;
            }
        }
    }
    if (didChange) {
        win->staticSaveTip->SetTextColor(MkRgb(0, 0, 0));
    } else {
        win->staticSaveTip->SetTextColor(MkRgb(0xcc, 0xcc, 0xcc));
    }
    win->buttonSavePDF->SetIsEnabled(didChange);
}

static void ItemsFromSeqstrings(Vec<std::string_view>& items, const char* strings) {
    while (*strings) {
        items.Append(strings);
        strings = seqstrings::SkipStr(strings);
    }
}

static void DropDownFillColors(DropDownCtrl* w, COLORREF col, str::Str& customColor) {
    Vec<std::string_view> items;
    ItemsFromSeqstrings(items, gColors);
    const char* colorName = GetKnownColorName(col);
    int idx = seqstrings::StrToIdx(gColors, colorName);
    if (idx == -1) {
        customColor.Reset();
        SerializeColorRgb(col, customColor);
        items.Append(customColor.as_view());
        idx = items.isize() - 1;
    }
    w->SetItems(items);
    w->SetCurrentSelection(idx);
}

static COLORREF GetDropDownColor(std::string_view sv) {
    int idx = seqstrings::StrToIdx(gColors, sv.data());
    if (idx >= 0) {
        int nMaxColors = (int)dimof(gColorsValues);
        CrashIf(idx >= nMaxColors);
        if (idx < nMaxColors) {
            return gColorsValues[idx];
        }
        return ColorUnset;
    }
    COLORREF col = ColorUnset;
    ParseColor(&col, sv);
    return col;
}

// TODO: mupdf shows it in 1.6 but not 1.7. Why?
static bool showRect = false;

static void DoRect(EditAnnotationsWindow* win, Annotation* annot) {
    if (!showRect) {
        return;
    }
    str::Str s;
    RectD rect = annot->Rect();
    int x = (int)rect.x;
    int y = (int)rect.y;
    int dx = (int)rect.Dx();
    int dy = (int)rect.Dy();
    s.AppendFmt("Rect: %d %d %d %d", x, y, dx, dy);
    win->staticRect->SetText(s.as_view());
    win->staticRect->SetIsVisible(true);
}

static void DoAuthor(EditAnnotationsWindow* win, Annotation* annot) {
    bool isVisible = !annot->Author().empty();
    if (!isVisible) {
        return;
    }
    str::Str s;
    s.AppendFmt("Author: %s", annot->Author().data());
    win->staticAuthor->SetText(s.as_view());
    win->staticAuthor->SetIsVisible(true);
}

static void AppendPdfDate(str::Str& s, time_t secs) {
    struct tm* tm = gmtime(&secs);
    char buf[100];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M UTC", tm);
    s.Append(buf);
}

static void DoModificationDate(EditAnnotationsWindow* win, Annotation* annot) {
    bool isVisible = (annot->ModificationDate() != 0);
    if (!isVisible) {
        return;
    }
    str::Str s;
    s.Append("Date: ");
    AppendPdfDate(s, annot->ModificationDate());
    win->staticModificationDate->SetText(s.as_view());
    win->staticModificationDate->SetIsVisible(true);
}

static void DoPopup(EditAnnotationsWindow* win, Annotation* annot) {
    int popupId = annot->PopupId();
    if (popupId < 0) {
        return;
    }
    str::Str s;
    s.AppendFmt("Popup: %d 0 R", popupId);
    win->staticPopup->SetText(s.as_view());
    win->staticPopup->SetIsVisible(true);
}

static void DoContents(EditAnnotationsWindow* win, Annotation* annot) {
    str::Str s = annot->Contents();
    // TODO: don't replace if already is "\r\n"
    s.Replace("\n", "\r\n");
    win->editContents->SetText(s.as_view());
    win->staticContents->SetIsVisible(true);
    win->editContents->SetIsVisible(true);
}

static void DoTextAlignment(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::FreeText) {
        return;
    }
    int itemNo = annot->Quadding();
    const char* items = gQuaddingNames;
    win->dropDownTextAlignment->SetItemsSeqStrings(items);
    win->dropDownTextAlignment->SetCurrentSelection(itemNo);
    win->staticTextAlignment->SetIsVisible(true);
    win->dropDownTextAlignment->SetIsVisible(true);
}

static void TextAlignmentSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    int newQuadding = ev->idx;
    win->annot->SetQuadding(newQuadding);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoTextFont(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::FreeText) {
        return;
    }
    std::string_view fontName = annot->DefaultAppearanceTextFont();
    // TODO: might have other fonts, like "Symb" and "ZaDb"
    auto itemNo = seqstrings::StrToIdx(gFontNames, fontName.data());
    if (itemNo < 0) {
        return;
    }
    win->dropDownTextFont->SetItemsSeqStrings(gFontReadableNames);
    win->dropDownTextFont->SetCurrentSelection(itemNo);
    win->staticTextFont->SetIsVisible(true);
    win->dropDownTextFont->SetIsVisible(true);
}

static void TextFontSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    ev->didHandle = true;
    const char* font = seqstrings::IdxToStr(gFontNames, ev->idx);
    win->annot->SetDefaultAppearanceTextFont(font);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoTextSize(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::FreeText) {
        return;
    }
    int fontSize = annot->DefaultAppearanceTextSize();
    AutoFreeStr s = str::Format("Text Size: %d", fontSize);
    win->staticTextSize->SetText(s.as_view());
    win->annot->SetDefaultAppearanceTextSize(fontSize);
    win->trackbarTextSize->SetValue(fontSize);
    win->staticTextSize->SetIsVisible(true);
    win->trackbarTextSize->SetIsVisible(true);
}

static void TextFontSizeChanging(EditAnnotationsWindow* win, TrackbarPosChangingEvent* ev) {
    ev->didHandle = true;
    int fontSize = ev->pos;
    win->annot->SetDefaultAppearanceTextSize(fontSize);
    AutoFreeStr s = str::Format("Text Size: %d", fontSize);
    win->staticTextSize->SetText(s.as_view());
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoTextColor(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::FreeText) {
        return;
    }
    COLORREF col = annot->DefaultAppearanceTextColor();
    DropDownFillColors(win->dropDownTextColor, col, win->currTextColor);
    win->staticTextColor->SetIsVisible(true);
    win->dropDownTextColor->SetIsVisible(true);
}

static void TextColorSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    auto col = GetDropDownColor(ev->item);
    win->annot->SetDefaultAppearanceTextColor(col);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoBorder(EditAnnotationsWindow* win, Annotation* annot) {
    size_t n = dimof(gAnnotsWithBorder);
    bool isVisible = IsAnnotationTypeInArray(gAnnotsWithBorder, n, annot->Type());
    if (!isVisible) {
        return;
    }
    int borderWidth = annot->BorderWidth();
    borderWidth = std::clamp(borderWidth, borderWidthMin, borderWidthMax);
    AutoFreeStr s = str::Format("Border: %d", borderWidth);
    win->staticBorder->SetText(s.as_view());
    win->trackbarBorder->SetValue(borderWidth);
    win->staticBorder->SetIsVisible(true);
    win->trackbarBorder->SetIsVisible(true);
}

static void BorderWidthChanging(EditAnnotationsWindow* win, TrackbarPosChangingEvent* ev) {
    ev->didHandle = true;
    int borderWidth = ev->pos;
    win->annot->SetBorderWidth(borderWidth);
    AutoFreeStr s = str::Format("Border: %d", borderWidth);
    win->staticBorder->SetText(s.as_view());
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoLineStartEnd(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::Line) {
        return;
    }
    int start = 0;
    int end = 0;
    annot->GetLineEndingStyles(&start, &end);
    win->dropDownLineStart->SetItemsSeqStrings(gLineEndingStyles);
    win->dropDownLineStart->SetCurrentSelection(start);
    win->dropDownLineEnd->SetItemsSeqStrings(gLineEndingStyles);
    win->dropDownLineEnd->SetCurrentSelection(end);
    win->staticLineStart->SetIsVisible(true);
    win->dropDownLineStart->SetIsVisible(true);
    win->staticLineEnd->SetIsVisible(true);
    win->dropDownLineEnd->SetIsVisible(true);
}

static void LineStartEndSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    int start = 0;
    int end = 0;
    win->annot->GetLineEndingStyles(&start, &end);
    int newVal = ev->idx;
    if (ev->dropDown == win->dropDownLineStart) {
        start = newVal;
    } else {
        CrashIf(ev->dropDown != win->dropDownLineEnd);
        end = newVal;
    }
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoIcon(EditAnnotationsWindow* win, Annotation* annot) {
    std::string_view itemName = annot->IconName();
    const char* items = nullptr;
    switch (annot->Type()) {
        case AnnotationType::Text:
            items = gTextIcons;
            break;
        case AnnotationType::FileAttachment:
            items = gFileAttachmentUcons;
            break;
        case AnnotationType::Sound:
            items = gSoundIcons;
            break;
        case AnnotationType::Stamp:
            items = gStampIcons;
            break;
    }
    if (!items || itemName.empty()) {
        return;
    }
    win->dropDownIcon->SetItemsSeqStrings(items);
    int idx = FindStringInArray(items, itemName.data(), 0);
    win->dropDownIcon->SetCurrentSelection(idx);
    win->staticIcon->SetIsVisible(true);
    win->dropDownIcon->SetIsVisible(true);
}

static void IconSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    win->annot->SetIconName(ev->item);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoColor(EditAnnotationsWindow* win, Annotation* annot) {
    size_t n = dimof(gAnnotsWithColor);
    bool isVisible = IsAnnotationTypeInArray(gAnnotsWithColor, n, annot->Type());
    if (!isVisible) {
        return;
    }
    COLORREF col = annot->Color();
    DropDownFillColors(win->dropDownColor, col, win->currCustomColor);
    win->staticColor->SetIsVisible(true);
    win->dropDownColor->SetIsVisible(true);
}

static void ColorSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    auto col = GetDropDownColor(ev->item);
    win->annot->SetColor(col);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoInteriorColor(EditAnnotationsWindow* win, Annotation* annot) {
    size_t n = dimof(gAnnotsWithInteriorColor);
    bool isVisible = IsAnnotationTypeInArray(gAnnotsWithInteriorColor, n, annot->Type());
    if (!isVisible) {
        return;
    }
    COLORREF col = annot->InteriorColor();
    DropDownFillColors(win->dropDownInteriorColor, col, win->currCustomInteriorColor);
    win->staticInteriorColor->SetIsVisible(true);
    win->dropDownInteriorColor->SetIsVisible(true);
}

static void InteriorColorSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    auto col = GetDropDownColor(ev->item);
    win->annot->SetInteriorColor(col);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void DoOpacity(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::Highlight) {
        return;
    }
    int opacity = win->annot->Opacity();
    AutoFreeStr s = str::Format("Opacity: %d", opacity);
    win->staticOpacity->SetText(s.as_view());
    win->staticOpacity->SetIsVisible(true);
    win->trackbarOpacity->SetIsVisible(true);
    win->trackbarOpacity->SetValue(opacity);
}

static void DoSaveEmbed(EditAnnotationsWindow* win, Annotation* annot) {
    if (annot->Type() != AnnotationType::FileAttachment) {
        return;
    }
    win->buttonSaveAttachment->SetIsVisible(true);
    win->buttonEmbedAttachment->SetIsVisible(true);
}

static void OpacityChanging(EditAnnotationsWindow* win, TrackbarPosChangingEvent* ev) {
    ev->didHandle = true;
    int opacity = ev->pos;
    win->annot->SetOpacity(opacity);
    AutoFreeStr s = str::Format("Opacity: %d", opacity);
    win->staticOpacity->SetText(s.as_view());
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void UpdateUIForSelectedAnnotation(EditAnnotationsWindow* win, int itemNo) {
    int annotPageNo = -1;
    win->annot = nullptr;

    // get annotation at index itemNo, skipping deleted annotations
    int idx = 0;
    int nAnnots = win->annotations->isize();
    for (int i = 0; itemNo >= 0 && i < nAnnots; i++) {
        auto annot = win->annotations->at(i);
        if (annot->isDeleted) {
            continue;
        }
        if (idx < itemNo) {
            ++idx;
            continue;
        }
        win->annot = annot;
        annotPageNo = annot->PageNo();
        break;
    }

    HidePerAnnotControls(win);
    if (win->annot) {
        DoRect(win, win->annot);
        DoAuthor(win, win->annot);
        DoModificationDate(win, win->annot);
        DoPopup(win, win->annot);
        DoContents(win, win->annot);

        DoTextAlignment(win, win->annot);
        DoTextFont(win, win->annot);
        DoTextSize(win, win->annot);
        DoTextColor(win, win->annot);

        DoLineStartEnd(win, win->annot);

        DoIcon(win, win->annot);

        DoBorder(win, win->annot);
        DoColor(win, win->annot);
        DoInteriorColor(win, win->annot);

        DoOpacity(win, win->annot);
        DoSaveEmbed(win, win->annot);

        win->buttonDelete->SetIsVisible(true);
    }

    // TODO: get from client size
    auto currBounds = win->mainLayout->lastBounds;
    int dx = currBounds.Dx();
    int dy = currBounds.Dy();
    LayoutAndSizeToContent(win->mainLayout, dx, dy, win->mainWindow->hwnd);
    if (annotPageNo > 0) {
        win->tab->AsFixed()->GoToPage(annotPageNo, false);
    }
}

static void ButtonSaveAttachment(EditAnnotationsWindow* win) {
    CrashIf(!win->annot);
    // TODO: implement me
    MessageBoxNYI(win->mainWindow->hwnd);
}

static void ButtonEmbedAttachment(EditAnnotationsWindow* win) {
    CrashIf(!win->annot);
    // TODO: implement me
    MessageBoxNYI(win->mainWindow->hwnd);
}

static void ButtonDeleteHandler(EditAnnotationsWindow* win) {
    CrashIf(!win->annot);
    win->annot->Delete();
    RebuildAnnotations(win);
    UpdateUIForSelectedAnnotation(win, -1);
}

static void ListBoxSelectionChanged(EditAnnotationsWindow* win, ListBoxSelectionChangedEvent* ev) {
    // TODO: finish me
    int itemNo = ev->idx;
    UpdateUIForSelectedAnnotation(win, itemNo);
}

static void DropDownAddSelectionChanged(EditAnnotationsWindow* win, DropDownSelectionChangedEvent* ev) {
    UNUSED(ev);
    // TODO: implement me
    MessageBoxNYI(win->mainWindow->hwnd);
}

// TODO: text changes are not immediately reflected in tooltip
// TODO: there seems to be a leak
static void ContentsChanged(EditAnnotationsWindow* win, EditTextChangedEvent* ev) {
    ev->didHandle = true;
    win->annot->SetContents(ev->text);
    EnableSaveIfAnnotationsChanged(win);
    RerenderForWindowInfo(win->tab->win);
}

static void WndSizeHandler(EditAnnotationsWindow* win, SizeEvent* ev) {
    int dx = ev->dx;
    int dy = ev->dy;
    HWND hwnd = ev->hwnd;
    if (dx == 0 || dy == 0) {
        return;
    }
    ev->didHandle = true;
    InvalidateRect(hwnd, nullptr, false);
    if (false && win->mainLayout->lastBounds.EqSize(dx, dy)) {
        // avoid un-necessary layout
        return;
    }
    LayoutToSize(win->mainLayout, {dx, dy});
}

static void WndKeyHandler(EditAnnotationsWindow* win, KeyEvent* ev) {
    // dbglogf("key: %d\n", ev->keyVirtCode);

    // only interested in Ctrl
    if (ev->keyVirtCode != VK_CONTROL) {
        return;
    }
    if (!win->buttonSavePDF->IsEnabled()) {
        return;
    }
    if (ev->isDown) {
        win->buttonSavePDF->SetText("Save as new PDF");
    } else {
        win->buttonSavePDF->SetText("Save changes to PDF");
    }
}

static StaticCtrl* CreateStatic(HWND parent, std::string_view sv = {}) {
    auto w = new StaticCtrl(parent);
    bool ok = w->Create();
    CrashIf(!ok);
    w->SetText(sv);
    return w;
}

static void CreateMainLayout(EditAnnotationsWindow* win) {
    HWND parent = win->mainWindow->hwnd;
    auto vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    {
        auto w = new DropDownCtrl(parent);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gAnnotationTypes);
        w->SetCueBanner("Add annotation...");
        w->onSelectionChanged = std::bind(DropDownAddSelectionChanged, win, _1);
        win->dropDownAdd = w;
        vbox->AddChild(w);
    }

    {
        auto w = new ListBoxCtrl(parent);
        w->idealSizeLines = 5;
        w->SetInsetsPt(4, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        win->lbModel = new ListBoxModelStrings();
        w->SetModel(win->lbModel);
        w->onSelectionChanged = std::bind(ListBoxSelectionChanged, win, _1);
        win->listBox = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent);
        win->staticRect = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent);
        // WindowBaseLayout* l2 = (WindowBaseLayout*)l;
        // l2->SetInsetsPt(20, 0, 0, 0);
        win->staticAuthor = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent);
        win->staticModificationDate = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent);
        win->staticPopup = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Contents:");
        win->staticContents = w;
        w->SetInsetsPt(4, 0, 0, 0);
        vbox->AddChild(w);
    }

    {
        auto w = new EditCtrl(parent);
        w->isMultiLine = true;
        w->idealSizeLines = 5;
        bool ok = w->Create();
        CrashIf(!ok);
        w->maxDx = 150;
        w->onTextChanged = std::bind(ContentsChanged, win, _1);
        win->editContents = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Text Alignment:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticTextAlignment = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gQuaddingNames);
        w->onSelectionChanged = std::bind(TextAlignmentSelectionChanged, win, _1);
        win->dropDownTextAlignment = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Text Font:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticTextFont = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gQuaddingNames);
        w->onSelectionChanged = std::bind(TextFontSelectionChanged, win, _1);
        win->dropDownTextFont = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Text Size:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticTextSize = w;
        vbox->AddChild(w);
    }

    {
        auto w = new TrackbarCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        w->rangeMin = 8;
        w->rangeMax = 36;
        bool ok = w->Create();
        CrashIf(!ok);
        w->onPosChanging = std::bind(TextFontSizeChanging, win, _1);
        win->trackbarTextSize = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Text Color:");
        win->staticTextColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gColors);
        w->onSelectionChanged = std::bind(TextColorSelectionChanged, win, _1);
        win->dropDownTextColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Line Start:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticLineStart = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->onSelectionChanged = std::bind(LineStartEndSelectionChanged, win, _1);
        win->dropDownLineStart = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Line End:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticLineEnd = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->onSelectionChanged = std::bind(LineStartEndSelectionChanged, win, _1);
        win->dropDownLineEnd = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Icon:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticIcon = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->onSelectionChanged = std::bind(IconSelectionChanged, win, _1);
        win->dropDownIcon = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Border:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticBorder = w;
        vbox->AddChild(w);
    }

    {
        auto w = new TrackbarCtrl(parent);
        w->rangeMin = borderWidthMin;
        w->rangeMax = borderWidthMax;
        bool ok = w->Create();
        CrashIf(!ok);
        w->onPosChanging = std::bind(BorderWidthChanging, win, _1);
        win->trackbarBorder = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Color:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gColors);
        w->onSelectionChanged = std::bind(ColorSelectionChanged, win, _1);
        win->dropDownColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Interior Color:");
        w->SetInsetsPt(8, 0, 0, 0);
        win->staticInteriorColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = new DropDownCtrl(parent);
        w->SetInsetsPt(4, 0, 0, 0);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetItemsSeqStrings(gColors);
        w->onSelectionChanged = std::bind(InteriorColorSelectionChanged, win, _1);
        win->dropDownInteriorColor = w;
        vbox->AddChild(w);
    }

    {
        auto w = CreateStatic(parent, "Opacity:");
        win->staticOpacity = w;
        vbox->AddChild(w);
    }

    {
        auto w = new TrackbarCtrl(parent);
        w->rangeMin = 0;
        w->rangeMax = 255;
        bool ok = w->Create();
        CrashIf(!ok);
        w->onPosChanging = std::bind(OpacityChanging, win, _1);
        win->trackbarOpacity = w;
        vbox->AddChild(w);
    }

    {
        auto w = new ButtonCtrl(parent);
        w->SetInsetsPt(8, 0, 0, 0);
        w->SetText("Save...");
        bool ok = w->Create();
        CrashIf(!ok);
        w->onClicked = std::bind(&ButtonSaveAttachment, win);
        win->buttonSaveAttachment = w;
        vbox->AddChild(w);
    }

    {
        auto w = new ButtonCtrl(parent);
        w->SetInsetsPt(8, 0, 0, 0);
        w->SetText("Embed...");
        bool ok = w->Create();
        CrashIf(!ok);
        w->onClicked = std::bind(&ButtonEmbedAttachment, win);
        win->buttonEmbedAttachment = w;
        vbox->AddChild(w);
    }

    {
        auto w = new ButtonCtrl(parent);
        w->SetInsetsPt(11, 0, 0, 0);
        w->SetText("Delete annotation");
        // TODO: doesn't work
        w->SetTextColor(MkRgb(0xff, 0, 0));
        bool ok = w->Create();
        CrashIf(!ok);
        w->onClicked = std::bind(&ButtonDeleteHandler, win);
        win->buttonDelete = w;
        vbox->AddChild(w);
    }

    {
        // used to take all available space between the what's above and below
        auto l = new Spacer(0, 0);
        vbox->AddChild(l, 1);
    }

    {
        auto w = CreateStatic(parent, "Tip: use Ctrl to save as a new PDF");
        w->SetTextColor(MkRgb(0xcc, 0xcc, 0xcc));
        w->SetInsetsPt(0, 0, 2, 0);
        // TODO: make invisible until buttonSavePDF is enabled
        win->staticSaveTip = w;
        vbox->AddChild(w);
    }

    {
        auto w = new ButtonCtrl(parent);
        // TODO: maybe  file name e.g. "Save changes to foo.pdf"
        w->SetText("Save changes to PDF");
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetIsEnabled(false); // only enabled if there are changes
        w->onClicked = std::bind(&ButtonSavePDFHandler, win);
        win->buttonSavePDF = w;
        vbox->AddChild(w);
    }

    auto padding = new Padding(vbox, DpiScaledInsets(parent, 4, 8));
    win->mainLayout = padding;
    HidePerAnnotControls(win);
}

static void SetAnnotations(EditAnnotationsWindow* win, TabInfo* tab) {
    DisplayModel* dm = tab->AsFixed();
    CrashIf(!dm);
    if (!dm) {
        return;
    }

    Vec<Annotation*>* annots = new Vec<Annotation*>();
    // those annotations are owned by us
    dm->GetEngine()->GetAnnotations(annots);

    // those annotations are owned by DisplayModel
    // TODO: for uniformity, make a copy of them
    if (dm->userAnnots) {
        for (auto a : *dm->userAnnots) {
            annots->Append(a);
        }
    }

    win->tab = tab;
    tab->editAnnotsWindow = win;
    win->annotations = annots;

    RebuildAnnotations(win);
}

void StartEditAnnotations(TabInfo* tab) {
    if (tab->editAnnotsWindow) {
        HWND hwnd = tab->editAnnotsWindow->mainWindow->hwnd;
        BringWindowToTop(hwnd);
        return;
    }
    auto win = new EditAnnotationsWindow();
    auto mainWindow = new Window();
    HMODULE h = GetModuleHandleW(nullptr);
    LPCWSTR iconName = MAKEINTRESOURCEW(GetAppIconID());
    mainWindow->hIcon = LoadIconW(h, iconName);

    mainWindow->isDialog = true;
    mainWindow->backgroundColor = MkRgb((u8)0xee, (u8)0xee, (u8)0xee);
    // PositionCloseTo(w, args->hwndRelatedTo);
    // SIZE winSize = {w->initialSize.dx, w->initialSize.Height};
    // LimitWindowSizeToScreen(args->hwndRelatedTo, winSize);
    // w->initialSize = {winSize.cx, winSize.cy};
    bool ok = mainWindow->Create();
    CrashIf(!ok);
    mainWindow->onClose = std::bind(WndCloseHandler, win, _1);
    mainWindow->onSize = std::bind(WndSizeHandler, win, _1);
    mainWindow->onKeyDownUp = std::bind(WndKeyHandler, win, _1);

    win->mainWindow = mainWindow;
    CreateMainLayout(win);

    SetAnnotations(win, tab);

    // size our editor window to be the same height as main window
    int minDy = 720;
    // TODO: this is slightly less that wanted
    HWND hwnd = tab->win->hwndCanvas;
    auto rc = ClientRect(hwnd);
    if (rc.Dy() > 0) {
        minDy = rc.Dy();
        // if it's a tall window, up the number of items in list box
        // from 5 to 14
        if (minDy > 1024) {
            win->listBox->idealSizeLines = 14;
        }
    }
    LayoutAndSizeToContent(win->mainLayout, 520, minDy, mainWindow->hwnd);
    HwndPositionToTheRightOf(mainWindow->hwnd, tab->win->hwndFrame);

    // important to call this after hooking up onSize to ensure
    // first layout is triggered
    mainWindow->SetIsVisible(true);
}
