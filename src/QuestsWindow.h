#pragma once
#include "ChatUIGlobals.h"

namespace SentientSands {
namespace UI {

extern MyGUI::Window *g_questsWindow;
extern MyGUI::ListBox *g_questsList;
extern MyGUI::ListBox *g_questsText;
extern std::vector<std::string> g_questsStorageIds;

void CreateQuestsUI();
void CloseQuestsUI();
void PopulateQuestsUI(const std::string &data);
void SetQuestsText(const std::string &data);

void OnQuestsSelect(MyGUI::ListBox *sender, size_t index);
void OnQuestsWindowClose(MyGUI::Window *sender, const std::string &name);
DWORD WINAPI QuestsResponseThread(LPVOID lpParam);
DWORD WINAPI QuestsContentThread(LPVOID lpParam);

} // namespace UI
} // namespace SentientSands
