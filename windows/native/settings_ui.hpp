#pragma once
#include <string>
#include "ui_host.hpp"

namespace ui {

enum class ToastKind { Ok, Info, Warn, Error, Muted };

// Opens the settings window on `page`, or brings the open one to the front.
void openSettings(UiHost& host, int page);
// The open settings window (for IsDialogMessage in the message loop) or null.
HWND settingsWindow();
// Shows a toast inside the settings window when it is open; no-op otherwise.
void settingsToast(const std::wstring& text, ToastKind kind);
// The Pi status, game or log changed: repaint the settings window if it is open.
void settingsRefresh();
// Fonts, GDI+ and the thumbnail worker are started by the caller before, stopped after.

}  // namespace ui
