#include <cassert>
#include <iostream>
#include "ui/Widgets.h"
#include "ui/LibraryView.h"
#include "ui/ReaderView.h"
#include "core/Paths.h"

using namespace gm;
namespace gm::w {
struct TextInputTestAccess {
    inline static std::wstring clipboard;
    inline static bool available=true;
    static bool Read(HWND,std::wstring& text) {if(!available)return false;text=clipboard;return true;}
    static bool Write(HWND,const std::wstring& text) {if(!available)return false;clipboard=text;return true;}
    static bool Key(TextInput& input,UINT key,bool ctrl=false,bool shift=false) {return input.HandleKey(key,ctrl,shift,Read,Write);}
    static void Owner(TextInput& input,HWND owner) {input.m_owner=owner;}
    static float Scroll(const TextInput& input) {return input.m_scrollX;}
};
}
namespace gm {
struct LibraryViewTestAccess {
    static w::TextInput& Editor(LibraryView& view,int index) {
        view.m_selectedRowIndex=-1;view.m_chDropdownOpen=false;view.m_search.focused=false;
        view.m_tab=LibraryView::kSeries;
        if(index==1){view.m_tab=LibraryView::kLocal;return view.m_localSearch;}
        if(index==2){view.m_tab=LibraryView::kSearch;view.EnsureSearchTab();return view.m_activeSearch->input;}
        if(index==3){view.m_selectedRowIndex=0;view.m_cardMode=1;return view.m_chapterSearch;}
        if(index==4){view.m_chDropdownOpen=true;return view.m_chDropdownSearch;}
        return view.m_search;
    }
};
struct ReaderViewTestAccess {
    static w::TextInput& Editor(ReaderView& view) {view.m_sidebarChDropdownOpen=true;return view.m_sidebarChSearch;}
};
}
using Edit=w::TextInputTestAccess;

static void TestEditing() {
    w::TextInput input;input.focused=true;input.SetText(L"One Piece — Şans 日本語 😀");
    const auto original=input.text;
    assert(Edit::Key(input,'A',true)&&input.SelectedText()==original);
    assert(Edit::Key(input,'C',true)&&Edit::clipboard==original&&input.text==original);
    assert(Edit::Key(input,'X',true)&&input.text.empty()&&input.cursor==0);
    assert(Edit::Key(input,'V',true)&&input.text==original&&!input.HasSelection());
    input.SelectAll();Edit::clipboard=L"Yeni manga";
    assert(Edit::Key(input,'V',true)&&input.text==L"Yeni manga");
    input.SelectAll();assert(input.OnChar(L'Ş')&&input.text==L"Ş");
    input.SetText(L"One Piece");Edit::Key(input,VK_HOME);Edit::Key(input,VK_RIGHT,false,true);Edit::Key(input,VK_RIGHT,false,true);Edit::Key(input,VK_RIGHT,false,true);
    assert(input.SelectedText()==L"One");Edit::Key(input,'C',true);assert(Edit::clipboard==L"One");
    input.Paste(L"Two");assert(input.text==L"Two Piece");
    Edit::Key(input,VK_END);Edit::Key(input,VK_LEFT,true,true);assert(input.SelectedText()==L"Piece");
    Edit::Key(input,'X',true);assert(input.text==L"Two "&&Edit::clipboard==L"Piece");
    Edit::Key(input,VK_INSERT,false,true);assert(input.text==L"Two Piece");
    Edit::Key(input,VK_BACK,true);assert(input.text==L"Two ");
    input.SelectAll();Edit::Key(input,VK_BACK);assert(input.text.empty());
    input.SetText(L"abc");input.SelectAll();Edit::available=false;
    Edit::Key(input,'X',true);Edit::Key(input,'V',true);assert(input.text==L"abc"&&input.SelectedText()==L"abc");
    Edit::available=true;input.SelectAll();input.Paste(L"\r\n\t");assert(input.text==L"abc");
    input.Paste(L"A\r\nB\tC");assert(input.text==L"ABC");
    input.SetText(L"A😀B");Edit::Key(input,VK_LEFT);Edit::Key(input,VK_LEFT,false,true);assert(input.SelectedText()==L"😀");
    Edit::Key(input,VK_DELETE);assert(input.text==L"AB");
    input.SetText(L"😀");Edit::Key(input,VK_BACK);assert(input.text.empty());
    input.SetText(L"hello");input.SelectAll();Edit::Key(input,VK_LEFT);assert(input.cursor==0&&!input.HasSelection());
    input.SelectAll();Edit::Key(input,VK_RIGHT);assert(input.cursor==5&&!input.HasSelection());
    for(wchar_t c:{L'\x01',L'\x03',L'\x16',L'\x18',L'\x7f'}) assert(input.OnChar(c)&&input.text==L"hello");
    input.focused=false;assert(!Edit::Key(input,'A',true)&&!input.OnChar(L'z'));
    for(int i=0;i<5;++i){
        LibraryView view;auto& field=LibraryViewTestAccess::Editor(view,i);field.SetText(L"Manga");field.focused=true;
        assert(view.HasFocusedTextInput());assert(view.OnKey('A',true));assert(field.SelectedText()==L"Manga");
        assert(view.OnKey('L'));assert(view.OnChar(L'L')&&field.text==L"L");
        assert(view.OnKey(VK_HOME,false,true)&&field.SelectedText()==L"L");
    }
    ReaderView reader;auto& field=ReaderViewTestAccess::Editor(reader);field.SetText(L"Chapter");field.focused=true;
    assert(reader.HasFocusedTextInput());assert(reader.OnKey('A',true,false,false));
    for(UINT key:{'L','F','C','A','D','B'}) assert(reader.OnKey(key,false,false,false));
    assert(reader.HasFocusedTextInput());assert(reader.OnChar(L'F')&&field.text==L"F");
    std::cout<<"Selection, Unicode, copy/cut/paste, replacement, failure preservation and all visible editor routes: PASS\n";
}

static void TestDrawing() {
    const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);assert(SUCCEEDED(apartment));
    HWND window=CreateWindowExW(0,L"STATIC",L"Input test",WS_OVERLAPPEDWINDOW,0,0,480,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    Renderer renderer;std::wstring error;assert(renderer.Init(window,96,&error));renderer.Resize(480,160);
    Ui ui;w::TextInput input;input.SetText(L"abcdef");const auto box=Rc(10,10,240,50);
    auto frame=[&](const Input& mouse){ui.Begin(mouse,0);assert(renderer.Begin());input.Draw(renderer,ui,Id("test.editor"),box,L"Search");renderer.End();ui.End();};
    frame(Input{24,30,true,true,false});
    const float end=24+renderer.MeasureText(L"abc",13);
    frame(Input{end,30,true,false,false});frame(Input{end,30,false,false,true});
    assert(input.SelectedText()==L"abc");input.OnChar(L'X');assert(input.text==L"Xdef");
    input.SetText(std::wstring(300,L'W'));input.focused=true;frame(Input{});assert(Edit::Scroll(input)>0);
    input.OnKey(VK_HOME,false);frame(Input{});assert(Edit::Scroll(input)==0);
    input.SelectAll();frame(Input{});assert(input.HasSelection());
    input.Clear();frame(Input{});assert(!input.HasSelection()&&Edit::Scroll(input)==0);
    renderer.Shutdown();DestroyWindow(window);CoUninitialize();
    std::cout<<"Mouse drag selection, selection rendering and long-text caret scrolling: PASS\n";
}

static void TestNativeClipboard() {
    // Each window station has its own clipboard. Never overwrite the user's
    // interactive clipboard while checking the actual Win32 transfer/ownership.
    HWINSTA original=GetProcessWindowStation();HDESK originalDesktop=GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);assert(station);
    assert(SetProcessWindowStation(station));
    HDESK desktop=CreateDesktopW(L"InputTest",nullptr,nullptr,0,GENERIC_ALL,nullptr);assert(desktop);
    assert(SetThreadDesktop(desktop));
    HWND owner=CreateWindowExW(0,L"STATIC",L"Clipboard test",0,0,0,1,1,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);assert(owner);
    w::TextInput input;Edit::Owner(input,owner);input.focused=true;input.SetText(L"Türkçe 日本語 😀");const auto text=input.text;
    assert(input.OnKey('A',true)&&input.OnKey('C',true));input.Clear();
    assert(input.OnKey('V',true)&&input.text==text);
    input.SelectAll();assert(input.OnKey('X',true)&&input.text.empty());
    assert(input.OnKey('V',true)&&input.text==text);
    assert(OpenClipboard(owner));assert(EmptyClipboard());assert(CloseClipboard());
    input.SelectAll();assert(input.OnKey('V',true)&&input.text==text);
    DestroyWindow(owner);assert(SetThreadDesktop(originalDesktop));assert(SetProcessWindowStation(original));
    CloseDesktop(desktop);CloseWindowStation(station);
    std::cout<<"Actual Win32 Unicode clipboard copy/cut/paste in an isolated window station: PASS\n";
}
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="clipboard"){TestNativeClipboard();return 0;}
    std::wstring error;assert(paths::Init(&error));TestEditing();TestDrawing();
}
