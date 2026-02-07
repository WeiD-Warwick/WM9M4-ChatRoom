#include "Client.h"
#include "ThirdParty/imgui.h"
#include "ThirdParty/imgui_impl_win32.h"
#include "ThirdParty/imgui_impl_dx12.h"
#include "ThirdParty/imgui_stdlib.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <tchar.h>

void drawOnlineList(Client& client, ChatModel& model);
void drawMessageList(std::vector<ChatMsg>& messages, float height);
void drawInputArea(Client& client, ChatModel& model, float sendBtnW);
void drawChatView(Client& client, ChatModel& model);
void drawPrivateChatWindow(Client& client, ChatModel& model);
void drawPrivateInputArea(Client& client, PrivateChatWindow& window, float sendBtnW);

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

    if (!model.isConnected || !client.isRunning()) return;

    if (!model.openMainChat) return;

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
    bool updated = false;
    for (auto& chat : model.privateChats) {
        if (!chat.open) {
            continue;
        }
        ImGui::PushID(chat.user.chatterID.c_str());
        std::string title = "Private Chat - " + chat.user.chatterName;
        ImGui::SetNextWindowSize(ImVec2(650, 700), ImGuiCond_FirstUseEver);
        ImGui::Begin(title.c_str(), &chat.open, ImGuiWindowFlags_NoCollapse);

        const float sendBtnW = 80.0f;
        const float inputH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 4.0f;
        const float listH = ImGui::GetContentRegionAvail().y - inputH - 5;

        drawMessageList(chat.messages, listH);
        drawPrivateInputArea(client, chat, sendBtnW);

        ImGui::End();
        ImGui::PopID();
        if (!chat.open) {
            updated = true;
        }
    }

    if (updated) {
        model.removeClosedPrivateChats();
    }
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
                const auto users = client.getCurrentOnlineUser();
                for (const auto& user : users) {
                    if (user.chatterID == model.uid) continue;
                    std::string label = user.chatterName;
                    ImGui::TableNextColumn();
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
                    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                        model.openPrivateChatFor(user);
                    }
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


inline void drawMessageList(std::vector<ChatMsg>& messages, float height) {
    ImGui::BeginChild("MessageList", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

    for (const auto& msg : messages) {
        std::string info = msg.sender.chatterName + ": " + msg.text;
        if (msg.fromMe) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
            ImGui::TextUnformatted(info.c_str());
            ImGui::PopStyleColor();
        }
        else {
            ImGui::TextUnformatted(info.c_str());
        }
    }

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
            client.sendGroupMessage(model.inputBuffer);
            model.mainChatMessages.push_back({ true, model.inputBuffer });
            model.inputBuffer.clear();
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

inline void drawPrivateInputArea(Client& client, PrivateChatWindow& window, float sendBtnW) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 10.0f));
    ImGui::BeginChild("PrivateInputArea", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float inputW = ImGui::GetContentRegionAvail().x - sendBtnW - spacing;
    if (inputW < 50.0f) inputW = 50.0f;

    ImGui::SetNextItemWidth(inputW);

    bool send = false;
    if (ImGui::InputText("##private_chat_input", &window.inputBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Send", ImVec2(sendBtnW, 0))) {
        send = true;
    }

    if (send) {
        if (!window.inputBuffer.empty()) {
            client.sendPrivateMessage(window.user.chatterID, window.inputBuffer);
            window.messages.push_back({ true, window.inputBuffer });
            window.inputBuffer.clear();
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

    drawMessageList(model.mainChatMessages, listH);
    drawInputArea(client, model, sendBtnW);

    ImGui::EndChild();
}