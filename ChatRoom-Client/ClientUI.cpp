#include "Client.h"
#include "ThirdParty/imgui.h"
#include "ThirdParty/imgui_impl_win32.h"
#include "ThirdParty/imgui_impl_dx12.h"
#include "ThirdParty/imgui_stdlib.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <tchar.h>

void drawOnlineList(Client& client, ChatModel& model);
void drawMessageList(Client& client, ChatModel& model, float height);
void drawInputArea(Client& client, ChatModel& model, float sendBtnW);
void drawChatView(Client& client, ChatModel& model);

inline void drawLoginWindow(Client& client, ChatModel& model) {

    if (model.isConnected && !client.isRunning()) {
        model.isConnected = false;
        model.state = ChatModel::LoginType::Disconnected;
    }

    if (!model.isConnected) {
        model.openLogin = true;
        ImGui::SetNextWindowSize(ImVec2(420, 200), ImGuiCond_FirstUseEver);
        ImGui::Begin("ChatRoom Login", &model.openLogin, ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Name");
        ImGui::InputText("name", &model.nameBuffer);

        if (ImGui::Button("Go!")) {
            if (model.nameBuffer.empty()) {
                model.state = ChatModel::LoginType::EmptyName;
            }
            else {
                model.isConnected = client.start(model.nameBuffer);

                model.openMainChat = true;
                model.state = model.isConnected ? ChatModel::LoginType::Connected : ChatModel::LoginType::NetError;
            }
        }

        if (model.state != ChatModel::LoginType::Default) {
            ImGui::Spacing();
            ImGui::Text(model.statusMessage().c_str());
        }

        ImGui::End();
    }
}

inline void drawChatRoomWindow(Client& client, ChatModel& model) {

    //if (!model.isConnected || !client.isRunning()) return;

    //if (!model.openMainChat) return;

    const float sendBtnW = 80.0f;
    const float inputH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const float listH = ImGui::GetContentRegionAvail().y - inputH;

    ImGui::Begin("ChatRoom", &model.openMainChat, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    drawOnlineList(client, model);

    ImGui::SameLine();

    drawChatView(client, model);

    ImGui::End();
}

inline void drawPrivateChatWindow(Client& client, ChatModel& model) {

}

inline void drawOnlineList(Client& client, ChatModel& model) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

    ImGui::BeginChild("UserListPanel", ImVec2(250, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

    {
        ImGui::TextUnformatted("Online Users");
        ImGui::Separator();
        float listHeight = ImGui::GetContentRegionAvail().y;

        {
            ImGui::BeginChild("UserListScroll", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
            if (ImGui::BeginTable("UserListTable", 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
                for (int i = 0; i < 5; i++) {
                    char buf[32];
                    sprintf_s(buf, "%03d", i);
                    ImGui::TableNextColumn();
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
                    ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
                    ImGui::PopStyleVar();
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        }

    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}


inline void drawMessageList(Client& client, ChatModel& model, float height) {
    ImGui::BeginChild("MessageList", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

    // 示例：demoMsgs 是 class 内 inline static 或 cpp 定义
    for (const auto& m : ChatModel::demoMsgs) {
        if (m.fromMe) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
            ImGui::TextUnformatted(m.text.c_str());
            ImGui::PopStyleColor();
        }
        else {
            ImGui::TextUnformatted(m.text.c_str());
        }
    }

    // 可选：始终滚到底（你后面可以改成“有新消息才滚”）
    // ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
}
inline void drawInputArea(Client& client, ChatModel& model, float sendBtnW) {

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 10.0f));
    ImGui::BeginChild("InputArea", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float inputW = ImGui::GetContentRegionAvail().x - sendBtnW - spacing;
    if (inputW < 50.0f) inputW = 50.0f;

    ImGui::SetNextItemWidth(inputW);

    bool send = false;
    if (ImGui::InputText("##chat_input", &model.inputBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Send", ImVec2(sendBtnW, 0)))
        send = true;

    if (send) {
        if (!model.inputBuffer.empty()) {
            // TODO: 接入你的网络发送
            // client.sendChat(model.inputBuffer);

            // 先演示：你可以把它 push 到 messages（不要改 demoMsgs）
            // model.messages.push_back({true, model.inputBuffer});

            model.inputBuffer.clear();
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

inline void drawChatView(Client& client, ChatModel& model) {
    ImGui::BeginChild("MainChatPanel", ImVec2(0, 0), ImGuiChildFlags_None);

    const float sendBtnW = 80.0f;
    const float inputH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 4.0f;

    const float listH = ImGui::GetContentRegionAvail().y - inputH - 5;

    drawMessageList(client, model, listH);
    drawInputArea(client, model, sendBtnW);

    ImGui::EndChild();
}