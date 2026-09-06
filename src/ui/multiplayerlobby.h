#ifndef FOUR_WINDS_MULTIPLAYER_LOBBY_H
#define FOUR_WINDS_MULTIPLAYER_LOBBY_H
#include "gamedata.h"
#include "jsongui.h"
#include "networkaddress.h"

// Scene object keeps network IO alive during nested dialogs and summaries.
class MultiplayerScenePump : public BaseObject
{
    void tickEvent(u32) override;
public:
    bool isValidObject() const override;
};

class MultiplayerLobbyScreen : public JsonWindow
{
    std::string address;
    std::string port = "19782";
    std::string room;
    std::string playerName = "Player";
    std::string error;
    std::string notice;
    int field = 0;
    int mode = 0;
    int humans = 2;
    bool quick = true;
    Multiplayer::LocalIPv4Discovery discovery;
    std::vector<std::string> localAddresses;
    std::size_t localAddress = 0;
    bool addressesReady = false;
    bool replaceField = false;
    bool sessionWasActive = false;
    u32 noticeAt = 0;
    u32 refreshed = 0;
    std::string& inputValue();
    void pasteClipboard();
    void copyInvitation();
    void activate(int);
protected:
    bool keyPressEvent(const KeySym&) override;
    bool textInputEvent(const std::string&) override;
    bool mouseClickEvent(const ButtonsEvent&) override;
    void tickEvent(u32) override;
public:
    MultiplayerLobbyScreen();
    ~MultiplayerLobbyScreen();
    void renderWindow() override;
};

int waitForMultiplayerPhase(int previousPhase);
void takeMultiplayerHandoffEvents(ActionList&);
#endif
