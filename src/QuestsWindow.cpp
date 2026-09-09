#include "QuestsWindow.h"
#include "Comm.h"
#include "Globals.h"
#include "Utils.h"
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_ListBox.h>
#include <mygui/MyGUI_Window.h>

namespace SentientSands {
namespace UI {

MyGUI::Window *g_questsWindow = nullptr;
MyGUI::ListBox *g_questsList = nullptr;
MyGUI::ListBox *g_questsText = nullptr;
std::vector<std::string> g_questsStorageIds;

void CloseQuestsUI() {
  if (g_questsWindow) {
    if (MyGUI::Gui::getInstancePtr())
      MyGUI::Gui::getInstancePtr()->destroyWidget(g_questsWindow);
    g_questsWindow = nullptr;
    g_questsList = nullptr;
    g_questsText = nullptr;
    g_questsStorageIds.clear();
  }
}

void PopulateQuestsUI(const std::string &data) {
  if (!g_questsList)
    return;
  g_questsList->removeAllItems();
  g_questsStorageIds.clear();
  size_t cur = 0;
  // JSON Format: [{"id": "...", "title": "..."}, ...]
  while ((cur = data.find("\"id\":", cur)) != std::string::npos) {
    cur = data.find("\"", cur + 5); // start of id value
    if (cur == std::string::npos)
      break;
    size_t idEnd = data.find("\"", cur + 1);
    std::string id = data.substr(cur + 1, idEnd - cur - 1);

    size_t titleField = data.find("\"title\":", idEnd);
    if (titleField == std::string::npos)
      break;
    size_t titleStart = data.find("\"", titleField + 8);
    size_t titleEnd = data.find("\"", titleStart + 1);
    std::string title = data.substr(titleStart + 1, titleEnd - titleStart - 1);

    g_questsList->addItem(Utf8ToWide(UnescapeJSON(title)).c_str());
    g_questsStorageIds.push_back(id);
    cur = titleEnd;
  }
  if (g_questsList->getItemCount() == 0) {
    g_questsList->addItem(Utf8ToWide(T("No active quests.")).c_str());
    g_questsStorageIds.push_back("");
  }
}

void SetQuestsText(const std::string &data) {
  if (!g_questsText)
    return;
  size_t start = (data.length() > 0 && data[0] == ' ') ? 1 : 0;
  std::stringstream ss(data.substr(start));
  std::string line;
  g_questsText->removeAllItems();
  while (std::getline(ss, line)) {
    g_questsText->addItem(Utf8ToWide(line).c_str());
  }
}

void OnQuestsSelect(MyGUI::ListBox *sender, size_t index) {
  if (index == MyGUI::ITEM_NONE)
    return;
  if (index >= g_questsStorageIds.size())
    return;
  std::string qId = g_questsStorageIds[index];
  if (qId.empty())
    return;

  if (g_questsText) {
    g_questsText->removeAllItems();
    g_questsText->addItem(Utf8ToWide(T("Loading quest...")).c_str());
  }

  QuestTask *t = new QuestTask();
  t->id = qId;
  t->json = "{\"id\":\"" + qId + "\"}";
  CreateThread(NULL, 0, QuestsContentThread, t, 0, NULL);
}

DWORD WINAPI QuestsContentThread(LPVOID lpParam) {
  QuestTask *t = (QuestTask *)lpParam;
  Log("QUESTS_THREAD: Fetching detail for " + t->id);
  std::string response = PostToPythonWithResponse(L"/quests", t->json);
  if (!response.empty()) {
    std::string content = GetJsonValue(response, "detail");
    if (!content.empty()) {
      std::string pipeMsg = "CMD: SET_QUESTS_TEXT: " + content;
      EnterCriticalSection(&g_msgMutex);
      g_messageQueue.push_back(pipeMsg);
      LeaveCriticalSection(&g_msgMutex);
    }
  }
  delete t;
  return 0;
}

DWORD WINAPI QuestsResponseThread(LPVOID lpParam) {
  Log("QUESTS_THREAD: Fetching quests list...");
  std::string response = PostToPythonWithResponse(L"/quests", "");
  if (!response.empty()) {
    std::string questsJson = GetJsonValue(response, "quests");
    if (!questsJson.empty()) {
      std::string pipeMsg = "CMD: POPULATE_QUESTS: " + questsJson;
      EnterCriticalSection(&g_msgMutex);
      g_messageQueue.push_back(pipeMsg);
      LeaveCriticalSection(&g_msgMutex);
    }
  }
  return 0;
}

void OnQuestsWindowClose(MyGUI::Window *sender, const std::string &name) {
  CloseQuestsUI();
}

void CreateQuestsUI() {
  MyGUI::Gui *gui = MyGUI::Gui::getInstancePtr();
  if (!gui)
    return;
  if (g_questsWindow)
    CloseQuestsUI();

  g_questsWindow = gui->createWidgetReal<MyGUI::Window>(
      "Kenshi_WindowCX", 0.1f, 0.1f, 0.8f, 0.8f, MyGUI::Align::Center, "Popup",
      "SentientSands_QuestsWindow");
  g_questsWindow->setCaption(Utf8ToWide(T("Quests")).c_str());
  g_questsWindow->eventWindowButtonPressed +=
      MyGUI::newDelegate(OnQuestsWindowClose);

  MyGUI::Widget *client = g_questsWindow->getClientWidget();

  // List (Left 30%)
  g_questsList = client->createWidgetReal<MyGUI::ListBox>(
      "Kenshi_ListBox", 0.02f, 0.02f, 0.28f, 0.96f,
      MyGUI::Align::Left | MyGUI::Align::VStretch, "SentientSands_QuestsList");
  g_questsList->eventListSelectAccept += MyGUI::newDelegate(OnQuestsSelect);
  g_questsList->eventListChangePosition += MyGUI::newDelegate(OnQuestsSelect);

  // Text (Right 70%)
  g_questsText = client->createWidgetReal<MyGUI::ListBox>(
      "Kenshi_ListBox", 0.32f, 0.02f, 0.66f, 0.96f, MyGUI::Align::Default,
      "SentientSands_QuestsText");
  g_questsText->addItem(
      Utf8ToWide(T("Select a quest to view details.")).c_str());

  CreateThread(NULL, 0, QuestsResponseThread, NULL, 0, NULL);
}

} // namespace UI
} // namespace SentientSands
