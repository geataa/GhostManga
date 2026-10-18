#include <windows.h>
#include <iostream>
#include "WebView2.h"

int main() {
    PWSTR ver = nullptr;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver);
    if (SUCCEEDED(hr) && ver) {
        std::wcout << L"WebView2 Version: " << ver << std::endl;
        CoTaskMemFree(ver);
        return 0;
    } else {
        std::cout << "WebView2 not found or error: 0x" << std::hex << hr << std::endl;
        return 1;
    }
}
