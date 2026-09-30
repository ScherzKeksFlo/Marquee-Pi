#pragma once
#include <string>
#include <vector>
#include "ui_host.hpp"
#include "ui_kit.hpp"

// Small windows in the arcade look: dropdown list, modal dialogs and the tray flyout.
namespace ui {

// Posted to the owner when a dropdown closes with a choice: wParam = id, lParam = item index.
constexpr UINT WM_UI_DROPDOWN = WM_APP + 40;

// Opens a list below `anchor` (screen pixels). Closes on outside click or Esc.
void showDropdown(HWND owner, RECT anchor, const std::vector<std::wstring>& items, int selected, int id);

// Modal confirm dialog; returns true for the OK button. `destructive` colors it red.
bool confirmDialog(HWND owner, const std::wstring& title, const std::wstring& text,
                   const std::wstring& okLabel, bool destructive);

// Modal "create access token" help with the commands to run on the Pi.
void tokenHelpDialog(HWND owner);

// Tray flyout anchored near `anchor` (screen pixels, usually the cursor). Commands go to the host.
void showFlyout(UiHost& host, POINT anchor);

}  // namespace ui
