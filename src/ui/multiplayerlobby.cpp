#include "multiplayerlobby.h"
#include "matchsession.h"
#include "gametheme.h"
#include "networkstatus.h"

#include <algorithm>

namespace
{
ActionList handoffEvents;
const Color panel(15, 23, 25), border(149, 123, 68), ink(229, 220, 183);
const std::array<Rect, 4> inputs{Rect(310, 190, 400, 48), Rect(794, 190, 88, 48),
    Rect(310, 252, 572, 48), Rect(310, 314, 572, 48)};
const std::array<Rect, 7> lobbyButtons{Rect(146, 408, 230, 52), Rect(396, 408, 230, 52),
    Rect(646, 408, 230, 52), Rect(164, 484, 330, 54), Rect(530, 484, 330, 54),
    Rect(362, 688, 300, 48), Rect(310, 552, 572, 44)};

void eraseCodepoint(std::string& value)
{
    if(value.empty()) return;
    auto end = value.size() - 1;
    while(end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    value.erase(end);
}

void fittedText(JsonWindow& window, std::string value, const Rect& area, Color color = ink,
                AlignType align = AlignLeft)
{
    const FontRender* font = &GameTheme::fontRender("dejavus18");
    for(int size : {16, 14, 12})
        if(font->stringSize(value).w > area.w || font->stringSize(value).h > area.h)
            font = &GameTheme::fontRender("dejavus" + std::to_string(size));
    if(font->stringSize(value).w > area.w)
    {
        while(!value.empty() && font->stringSize(value + "...").w > area.w) eraseCodepoint(value);
        value += "...";
    }
    window.renderText(*font, value, color, Point(align == AlignCenter ? area.x + area.w / 2 : area.x,
        area.y + area.h / 2), align, AlignCenter);
}

void drawPanel(JsonWindow& window, const Rect& area)
{
    window.renderColor(panel, area);
    window.renderRect(border, area);
    window.renderRect(Color(65, 61, 42), Rect(area.x + 4, area.y + 4, area.w - 8, area.h - 8));
    for(const Point corner : {Point(area.x + 4, area.y + 4), Point(area.x + area.w - 5, area.y + 4),
         Point(area.x + 4, area.y + area.h - 5), Point(area.x + area.w - 5, area.y + area.h - 5)})
    {
        window.renderLine(border, corner + Point(0, -4), corner + Point(4, 0));
        window.renderLine(border, corner + Point(4, 0), corner + Point(0, 4));
        window.renderLine(border, corner + Point(0, 4), corner + Point(-4, 0));
        window.renderLine(border, corner + Point(-4, 0), corner + Point(0, -4));
    }
}

void drawButton(JsonWindow& window, const Rect& area, const std::string& label, bool enabled = true)
{
    window.renderColor(panel, area);
    window.renderRect(border, area);
    fittedText(window, label, Rect(area.x + 12, area.y, area.w - 24, area.h),
        enabled ? ink : Color::Gray, AlignCenter);
}

class PhaseWaitScreen : public JsonWindow
{
    int previous;
    u32 sentAt = 0;
    void tickEvent(u32 ms) override
    {
        auto& match = Multiplayer::session();
        ActionList ignored;
        match.takeEvents(ignored);
        if(match.phase() != previous)
        {
            handoffEvents.splice(handoffEvents.end(), ignored);
            setResultCode(match.phase());
            setVisible(false);
            return;
        }
        if(ms - sentAt > 500)
        {
            match.ready();
            sentAt = ms;
            renderWindow();
        }
    }
    bool keyPressEvent(const KeySym& key) override
    {
        if(key.keycode() != Key::ESCAPE) return false;
        setResultCode(Menu::MainMenu);
        setVisible(false);
        return true;
    }
    bool mouseClickEvent(const ButtonsEvent& event) override
    {
        if(event.isButtonLeft() && event.isClick(lobbyButtons[5]))
        {
            setResultCode(Menu::MainMenu);
            setVisible(false);
        }
        return true;
    }
public:
    explicit PhaseWaitScreen(int phase) : JsonWindow("screen_mainmenu.json", nullptr), previous(phase)
    { setKeyHandle(true); setVisible(true); }
    void renderWindow() override
    {
        JsonWindow::renderWindow();
        drawPanel(*this, Rect(114, 128, 796, 500));
        renderText(GameTheme::fontRender("dejavus26"), _("Waiting for players"), ink, Point(512, 250), AlignCenter);
        renderText(GameTheme::fontRender("dejavus18"), networkStatusText(), ink, Point(512, 335), AlignCenter);
        drawButton(*this, lobbyButtons[5], _("Leave room"));
    }
};
}

void MultiplayerScenePump::tickEvent(u32) { Multiplayer::session().poll(); }
bool MultiplayerScenePump::isValidObject() const { return Multiplayer::session().active(); }

MultiplayerLobbyScreen::MultiplayerLobbyScreen() : JsonWindow("screen_mainmenu.json", nullptr)
{
    handoffEvents.clear();
    setKeyHandle(true);
    setVisible(true);
    sessionWasActive = Multiplayer::session().active();
    if(!sessionWasActive) SDL_StartTextInput();
}
MultiplayerLobbyScreen::~MultiplayerLobbyScreen() { SDL_StopTextInput(); }

std::string& MultiplayerLobbyScreen::inputValue()
{
    return field == 0 ? address : field == 1 ? port : field == 2 ? room : playerName;
}

void MultiplayerLobbyScreen::pasteClipboard()
{
    if(Multiplayer::session().active()) return;
    char* clipboard = SDL_GetClipboardText();
    if(!clipboard) { error = _("Unable to read clipboard"); renderWindow(); return; }
    const std::string text(clipboard);
    SDL_free(clipboard);
    Multiplayer::InviteAddress invitation;
    if(Multiplayer::parseInvite(text, invitation))
    {
        address = invitation.address;
        port = std::to_string(invitation.port);
        room = invitation.room;
        field = 3;
        replaceField = true;
        error.clear();
    }
    else if(text.find("://") != std::string::npos)
        error = _("Invalid invitation");
    else
    {
        replaceField = true;
        textInputEvent(text);
    }
    renderWindow();
}

void MultiplayerLobbyScreen::copyInvitation()
{
    auto& match = Multiplayer::session();
    if(!match.active() || !match.isHost()) return;
    const std::string hostAddress = localAddresses.empty() ? std::string() : localAddresses[localAddress];
    const auto invitation = Multiplayer::formatInvite({hostAddress, match.port(), match.roomCode()});
    if(invitation.empty()) error = _("No LAN address found");
    else if(SDL_SetClipboardText(invitation.c_str()) == 0)
    { error.clear(); notice = _("Invitation copied"); noticeAt = SDL_GetTicks(); }
    else error = _("Unable to copy invitation");
    renderWindow();
}

void MultiplayerLobbyScreen::activate(int button)
{
    auto& match = Multiplayer::session();
    if(button == 5)
    {
        match.leave();
        setResultCode(Menu::MainMenu);
        setVisible(false);
        return;
    }
    if(button == 6)
    {
        if(match.active()) copyInvitation();
        else pasteClipboard();
        return;
    }
    if(!match.active())
    {
        if(button == 0) { mode = (mode + 1) % 3; if(mode == 0) humans = 2; }
        if(button == 1) quick = !quick;
        if(button == 2 && mode != 0) humans = humans == 4 ? 2 : humans + 1;
        if(button == 3)
        {
            Multiplayer::HostOptions options;
            if(!Multiplayer::parseNetworkPort(port, options.port))
            { error = _("Port must be between 1 and 65535"); renderWindow(); return; }
            options.mode = mode == 0 ? "duel" : mode == 1 ? "classic-ffa" : "coalition";
            options.ruleset = quick ? "quick" : "classic";
            options.humanSeats = humans;
            options.name = playerName;
            if(match.host(options, error)) room = match.roomCode();
        }
        if(button == 4)
        {
            std::uint16_t selectedPort = 0;
            if(!Multiplayer::isNumericIPv4(address)) error = _("Enter a numeric IPv4 host address");
            else if(!Multiplayer::parseNetworkPort(port, selectedPort)) error = _("Port must be between 1 and 65535");
            else if(Multiplayer::formatInvite({address, selectedPort, room}).empty()) error = _("Invalid room code");
            else match.join(address, selectedPort, room, playerName, error);
        }
    }
    else if(button == 3 && match.canStart()) match.start(error);
    sessionWasActive = match.active();
    if(sessionWasActive) SDL_StopTextInput();
    renderWindow();
}

bool MultiplayerLobbyScreen::keyPressEvent(const KeySym& key)
{
    if(key.keycode() == Key::ESCAPE) { activate(5); return true; }
    if(key.keycode() == Key::TAB)
    { field = (field + (key.keymod().isShift() ? 3 : 1)) % 4; replaceField = true; renderWindow(); return true; }
    const bool modifier = key.keymod().isCtrl() || key.keymod().isGui();
    if(modifier && key.keycode() == Key::c && Multiplayer::session().isHost())
    { copyInvitation(); return true; }
    if(Multiplayer::session().active()) return false;
    auto& value = inputValue();
    if(modifier && key.keycode() == Key::a) { replaceField = true; return true; }
    if(key.keycode() == Key::BACKSPACE)
    {
        if(replaceField) value.clear();
        else eraseCodepoint(value);
        replaceField = false;
        error.clear();
        renderWindow();
        return true;
    }
    if((key.keycode() == Key::INSERT && key.keymod().isShift()) || (modifier && key.keycode() == Key::v))
    {
        pasteClipboard();
        return true;
    }
    return false;
}

bool MultiplayerLobbyScreen::textInputEvent(const std::string& text)
{
    if(Multiplayer::session().active()) return true;
    // Mobile keyboards may insert a complete clipboard invitation directly.
    Multiplayer::InviteAddress invitation;
    if(Multiplayer::parseInvite(text, invitation))
    {
        address = invitation.address;
        port = std::to_string(invitation.port);
        room = invitation.room;
        field = 3;
        replaceField = true;
        error.clear();
        renderWindow();
        return true;
    }
    auto& value = inputValue();
    const auto limit = field == 0 ? 15U : field == 1 ? 5U : field == 2 ? 64U : 48U;
    for(unsigned char ch : text) if(ch < 32 || ch == 127) return true;
    if(field != 3)
        for(unsigned char ch : text)
            if(!((ch >= '0' && ch <= '9') || (field == 0 && ch == '.') ||
                 (field == 2 && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))))) return true;
    if((replaceField ? 0 : value.size()) + text.size() <= limit)
    {
        if(replaceField) value.clear();
        value += text;
        if(field == 2) std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
            return ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') : ch;
        });
        replaceField = false;
        error.clear();
    }
    renderWindow();
    return true;
}

bool MultiplayerLobbyScreen::mouseClickEvent(const ButtonsEvent& event)
{
    if(!event.isButtonLeft()) return false;
    for(std::size_t i = 0; i < inputs.size(); ++i)
        if(event.isClick(inputs[i]))
        {
            if(Multiplayer::session().active())
            {
                if(i == 0 && Multiplayer::session().isHost() && localAddresses.size() > 1)
                    localAddress = (localAddress + 1) % localAddresses.size();
            }
            else { field = static_cast<int>(i); replaceField = true; SDL_StartTextInput(); }
            renderWindow();
            return true;
        }
    for(std::size_t i = 0; i < lobbyButtons.size(); ++i)
        if(event.isClick(lobbyButtons[i])) { activate(static_cast<int>(i)); return true; }
    return false;
}

void MultiplayerLobbyScreen::tickEvent(u32 ms)
{
    auto& match = Multiplayer::session();
    if(sessionWasActive && !match.active()) SDL_StartTextInput();
    else if(!sessionWasActive && match.active()) SDL_StopTextInput();
    sessionWasActive = match.active();
    if(!notice.empty() && ms - noticeAt > 2500) notice.clear();
    if(!addressesReady) addressesReady = discovery.poll(localAddresses);
    ActionList ignored;
    if(match.active()) match.takeEvents(ignored);
    if(match.started())
    {
        setResultCode(match.phase());
        setVisible(false);
        return;
    }
    if(ms - refreshed > 250) { refreshed = ms; renderWindow(); }
}

void MultiplayerLobbyScreen::renderWindow()
{
    JsonWindow::renderWindow();
    drawPanel(*this, Rect(96, 64, 832, 608));
    const auto& heading = GameTheme::fontRender("dejavus26");
    renderText(heading, _("Multiplayer"), ink, Point(512, 88), AlignCenter);
    fittedText(*this, _("Connect to the host on the same network"), Rect(128, 131, 768, 34), ink, AlignCenter);
    auto& match = Multiplayer::session();
    const bool host = match.active() && match.isHost();
    const std::string hostAddress = localAddresses.empty() ? std::string() : localAddresses[localAddress];
    const std::array<std::string, 4> labels{_("Host address"), _("Port"), _("Room code"), _("Your name")};
    const std::array<std::string, 4> values{host ? hostAddress : address,
        host ? std::to_string(match.port()) : port, host ? match.roomCode() : room, playerName};
    for(std::size_t i = 0; i < inputs.size(); ++i)
    {
        const auto& area = inputs[i];
        fittedText(*this, labels[i], Rect(i == 1 ? 726 : 132, area.y, i == 1 ? 64 : 164, area.h));
        renderColor(Color(10, 16, 18), area);
        renderRect(!match.active() && static_cast<int>(i) == field ? ink : border, area);
        const std::string shown = values[i].empty() && i == 0 ? (host ? _("No LAN address found") : "192.168.1.10") : values[i];
        fittedText(*this, shown, Rect(area.x + 12, area.y, area.w - 24, area.h), values[i].empty() ? Color::Gray : ink);
    }
    if(host && localAddresses.size() > 1)
        fittedText(*this, std::string(_("Tap the host address to choose another network")) +
            "  (" + std::to_string(localAddress + 1) + "/" + std::to_string(localAddresses.size()) + ")",
            Rect(132, 602, 750, 22), ink, AlignCenter);
    if(match.active())
    {
        const auto names = match.playerNames();
        const int count = static_cast<int>(names.size());
        for(int index = 0; index < count; ++index)
        {
            const int cellWidth = 750 / count;
            fittedText(*this, std::to_string(index + 1) + ". " + (names[index].empty() ? "..." : names[index]),
                Rect(132 + index * cellWidth + 4, 371, cellWidth - 8, 26),
                names[index].empty() ? Color::Gray : ink, AlignCenter);
        }
    }
    const std::string selectedMode = match.active() ? match.mode() : mode == 0 ? "duel" : mode == 1 ? "classic-ffa" : "coalition";
    const bool selectedQuick = match.active() ? match.ruleset() == "quick" : quick;
    const bool awaitingHost = match.active() && !host && !match.connected();
    drawButton(*this, lobbyButtons[0], awaitingHost ? "..." : selectedMode == "duel" ? _("Duel") :
        selectedMode == "classic-ffa" ? _("Free for all") : _("Coalition"), !match.active());
    drawButton(*this, lobbyButtons[1], awaitingHost ? "..." : selectedQuick ? _("Quick Rune Game") : _("Classic Rune Game"), !match.active());
    drawButton(*this, lobbyButtons[2], awaitingHost ? "..." :
        std::to_string(match.active() ? match.requiredSeats() : humans) + " " + _("human players"), !match.active());
    drawButton(*this, lobbyButtons[3], match.active() ? _("Start match") : _("Create room"), !match.active() || match.canStart());
    drawButton(*this, lobbyButtons[4], _("Join room"), !match.active());
    drawButton(*this, lobbyButtons[5], _("Back"));
    drawButton(*this, lobbyButtons[6], match.active() ? _("Copy invitation") : _("Paste invitation"),
        !match.active() || (host && !hostAddress.empty()));
    fittedText(*this, error.empty() ? notice.empty() ? networkStatusText() : notice : error,
        Rect(132, 632, 750, 24), error.empty() ? ink : Color::Red, AlignCenter);
}

int waitForMultiplayerPhase(int previousPhase) { return PhaseWaitScreen(previousPhase).exec(); }
void takeMultiplayerHandoffEvents(ActionList& output) { output.splice(output.end(), handoffEvents); }
