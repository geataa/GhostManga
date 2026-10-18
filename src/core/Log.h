#pragma once
//
// Dongusel gunluk: portable_data\logs\ghostmanga.log, 1 MB'ta .1'e doner.
// Is parcacigi guvenli. (FR-DIAG-001)
//
namespace gm::log {

void Init();
void Write(const wchar_t* fmt, ...);

} // namespace gm::log
