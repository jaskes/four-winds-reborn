/***************************************************************************
 *   Copyright (C) 2020 by RuneWarsNA team <runewars.newage@gmail.com>     *
 *                                                                         *
 *   Part of the RuneWars: NewAge engine:                                  *
 *   https://github.com/AndreyBarmaley/runewars.newage                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <algorithm>
#include <iterator>

#include "gametheme.h"
#include "dialogs.h"
#include "runegametargetdialogs.h"
#include "mahjongpart.h"
#include "matchsession.h"
#ifdef BUILD_DEBUG
#include "developertools.h"
#endif

bool MahjongPartScreen::userEvent(int act, void* data)
{
    switch(act)
    {
        case Action::MahjongGameQuit: actionQuit(); break;
        // Countdown expiry is consumed synchronously by tickEvent.  Ignore
        // legacy queued events so they cannot act on a newer choice window.
        case Action::MahjongOutOfTime: DEBUG("stale Mahjong timeout ignored"); break;
        case Action::MahjongDropSelected: actionDropSelected(); break;

        case Action::ButtonCast: actionButtonShowCast(); break;

        case Action::ButtonLocalReady: if(buttonLocalReady.isVisible()) actionButtonLocalReady(); break;
        case Action::ButtonLocalKong: if(buttonLocalKong.isVisible()) actionButtonLocalKong(); break;
        case Action::ButtonLocalGame: if(buttonLocalGame.isVisible()) actionButtonLocalGame(); break;

        case Action::ButtonPass: if(buttonPass->isVisible()) actionButtonPass(WinRule::None); break;
        case Action::ButtonChao: if(buttonChao->isVisible()) actionButtonPass(WinRule::Chao); break;
        case Action::ButtonPung: if(buttonPung->isVisible()) actionButtonPass(WinRule::Pung); break;
        case Action::ButtonKong: if(buttonKong->isVisible()) actionButtonPass(WinRule::Kong); break;
        case Action::ButtonGame: if(buttonGame->isVisible()) actionButtonPass(WinRule::Game); break;

        case Action::ButtonChat: actionButtonChat(); break;
        case Action::ButtonSystem: actionButtonSystem(); break;
        case Action::ButtonSummary: actionButtonSummary(); break;
        case Action::ButtonMap: actionButtonMap(); break;

        default: break;
    }

    return true;
}

bool MahjongPartScreen::keyPressEvent(const KeySym & key)
{
#ifdef BUILD_DEBUG
    if(Multiplayer::session().active() &&
       (key.keycode() == Key::F5 || key.keycode() == Key::F6 || key.keycode() == Key::F9)) return true;
#endif
    switch(key.keycode())
    {
        case Key::LEFT: return actionSelectedShiftLeft();
        case Key::RIGHT: return actionSelectedShiftRight();
        case Key::SPACE: return actionPressSpace();
        case Key::F4: return actionCreateScreenshot();

        case Key::c: return userEvent(Action::ButtonChao, nullptr);
        case Key::p: return userEvent(Action::ButtonPung, nullptr);
        case Key::k: return userEvent(Action::ButtonKong, nullptr);
        case Key::g: return userEvent(Action::ButtonGame, nullptr);

#ifdef BUILD_DEBUG
        case Key::F5: return actionEventDebug1();
        case Key::F6: return actionEventDebug2();
        case Key::F9:
        {
            const DeveloperTools::Command command = DeveloperTools::showPanel(*this, myAvatar);
            if(command == DeveloperTools::Command::ToggleAutoplay)
            {
                GameData::setDeveloperAutoplay(myAvatar,
                    !GameData::developerAutoplay(myAvatar));
                return true;
            }
            if(command != DeveloperTools::Command::Cancel)
            {
                const DeveloperTools::FastForwardResult result =
                    DeveloperTools::fastForward(command, myAvatar);
                if(!result.success)
                {
                    MessageTop(_("Developer Tools"), result.error, *this);
                    return true;
                }
                setResultCode(result.menu);
                setVisible(false);
                return true;
            }
            return true;
        }
#endif

#ifdef ANDROID
        case SDLK_AC_BACK:
#endif
        case Key::ESCAPE: return actionButtonSystem();
        default: break;
    }

    return false;
}

bool MahjongPartScreen::mouseClickEvent(const ButtonsEvent & coords)
{
    if(!coords.isButtonLeft()) return false;

    if(ld.yourTurn())
    {
        if(0 <= stoneSelected)
        {
            int index = -1;
            auto it = std::find_if(stonesPos.begin(), stonesPos.end(),
                [&](const Rect & rt) { return coords.isClick(rt); });

            if(it != stonesPos.end())
                index = std::distance(stonesPos.begin(), it);
            else if(coords.isClick(newStonePos()))
                index = stonesPos.size();

            if(0 <= index)
            {
                if(stoneSelected == index)
                    pushEventAction(Action::MahjongDropSelected, this, nullptr);
                else
                {
                    playSound("select");
                    stoneSelected = index;
                    renderWindow();
                }

                return true;
            }
        }
    }

    if(0 <= variantSelected)
    {
        auto it = std::find_if(variantsPos.begin(), variantsPos.end(),
            [&](const Rect & rt) { return coords.isClick(rt); });

        int index = it != variantsPos.end() ?
            std::distance(variantsPos.begin(), it) : -1;

        if(0 <= index)
        {
            if(variantSelected == index)
                pushEventAction(Action::MahjongDropSelected, this, nullptr);
            else
            {
                playSound("select");
                variantSelected = index;
                renderWindow();
            }

            return true;
        }
    }

    if(coords.isClick(animationTurn.maxArea()))
        return actionPressSpace();

    return false;
}

void MahjongPartScreen::actionOutOfTime(void)
{
    turnTimeoutPending = false;

    const bool hasChoice =
        buttonLocalReady.isVisible() ||
        buttonGame->isVisible() ||
        buttonKong->isVisible() ||
        buttonPung->isVisible() ||
        buttonChao->isVisible() ||
        buttonPass->isVisible() ||
        0 <= variantSelected ||
        0 <= stoneSelected ||
        ld.myPlayer().newStone.isValid();

    if(!hasChoice)
    {
        DEBUG("stale Mahjong timeout ignored: no active choice");
        return;
    }

    // A timeout owns exactly one local choice.  Retire its countdown before
    // scheduling the button/drop action, whose engine callback is delayed.
    animationTurn.setEnabled(false);

    if(buttonLocalReady.isVisible())
        buttonLocalReady.setClicked();
    else if(buttonGame->isVisible())
        buttonGame->setClicked();
    else if(buttonKong->isVisible())
        buttonKong->setClicked();
    else if(buttonPung->isVisible())
        buttonPung->setClicked();
    else if(buttonChao->isVisible())
        buttonChao->setClicked();
    else if(buttonPass->isVisible())
        buttonPass->setClicked();
    else if(0 <= variantSelected)
        pushEventAction(Action::MahjongDropSelected, this, nullptr);
    else if(0 <= stoneSelected || ld.myPlayer().newStone.isValid())
        pushEventAction(Action::MahjongDropSelected, this, nullptr);
}

void MahjongPartScreen::actionButtonLocalReady(void)
{
    buttonLocalReady.setVisible(false);
    playerReady = submitHumanAction(ClientReady());
}

bool MahjongPartScreen::submitHumanAction(const ClientMessage & action)
{
    ActionRejection rejection;
    if(Multiplayer::runeCommand(myAvatar, action, actions, &rejection))
    {
        if(Multiplayer::session().active() && ld.dropStone.isValid() && !ld.yourTurn() &&
           (action.type() == Action::ClientButtonPass || action.type() == Action::ClientButtonGame ||
            action.type() == Action::ClientButtonPung || action.type() == Action::ClientButtonKong1 ||
            action.type() == Action::ClientChaoVariant))
            networkClaimSubmitted = true;
        return true;
    }

    showActionRejection(rejection);
    return false;
}

void MahjongPartScreen::retireTurnTimeout(void)
{
    turnTimeoutPending = false;
    animationTurn.setEnabled(false);
}

void MahjongPartScreen::showActionRejection(const ActionRejection & rejection)
{
    fastLogText.text = GameData::actionRejectionMessage(rejection);
    fastLogText.color = Color::Red;
    fastLogOwner = ld.myPlayer().wind;
    gameLogs << fastLogText.text;
    renderWindow();
}

void MahjongPartScreen::actionButtonLocalKong(void)
{
    buttonLocalKong.setVisible(false);
    actionButtonPass(WinRule::Kong);
}

void MahjongPartScreen::actionButtonLocalGame(void)
{
    buttonLocalGame.setVisible(false);
    actionButtonPass(WinRule::Game);
}

bool MahjongPartScreen::actionPressSpace(void)
{
    if(buttonPass->isVisible() || buttonLocalReady.isVisible() || 0 <= stoneSelected || 0 <= variantSelected)
    {
        if(buttonChao->isVisible() || buttonPung->isVisible() ||
            buttonKong->isVisible() || buttonGame->isVisible() || buttonLocalGame.isVisible())
        {
            VERBOSE("skip auto pass");
        }
        else
            actionOutOfTime();

        return true;
    }

    return false;
}

void MahjongPartScreen::actionButtonPass(int rule)
{
    playSound("button");
    buttonPass->setVisible(false);
    buttonChao->setVisible(false);
    buttonPung->setVisible(false);
    buttonKong->setVisible(false);
    buttonGame->setVisible(false);

    buttonLocalReady.setVisible(false);
    bool sendPass = false;
    bool choiceCompleted = false;

    if(fastLogText.text.size()) fastLogText.text.clear();

    if(rule == WinRule::Game)
    {
        if(Multiplayer::session().active() || submitHumanAction(ClientSayGame()))
            choiceCompleted = submitHumanAction(ClientButtonGame());
    }
    else if(rule == WinRule::Kong)
    {
        if(ld.yourTurn())
        {
            if(Multiplayer::session().active() || submitHumanAction(ClientSayKong(2)))
                choiceCompleted = submitHumanAction(ClientButtonKong2());
        }
        else
        {
            if(Multiplayer::session().active() || submitHumanAction(ClientSayKong(1)))
                choiceCompleted = submitHumanAction(ClientButtonKong1());
        }
    }
    else if(rule == WinRule::Pung)
    {
        if(Multiplayer::session().active() || submitHumanAction(ClientSayPung()))
            choiceCompleted = submitHumanAction(ClientButtonPung());
    }
    else if(rule == WinRule::Chao)
    {
        if(!Multiplayer::session().active() && !submitHumanAction(ClientSayChao())) return;
        const Stones & chaoVariants = ld.myPlayer().stones.findChaoVariants(ld.dropStone);

        if(chaoVariants.size())
        {
            // User must choose between multiple legal Chao variants.
            if(1 < chaoVariants.size())
            {
                variantSelected = 0;
                DEBUG("chao multiple variant");
            }
            else
                choiceCompleted = submitHumanAction(ClientChaoVariant(0));

            renderWindow();
        }
        else
            sendPass = true;
    }
    else
        sendPass = true;

    if(sendPass)
        choiceCompleted = submitHumanAction(ClientButtonPass());

    if(choiceCompleted)
        retireTurnTimeout();
}

void MahjongPartScreen::actionDropSelected(void)
{
    buttonLocalGame.setVisible(false);
    buttonLocalKong.setVisible(false);

    if(0 <= variantSelected)
    {
        if(submitHumanAction(ClientChaoVariant(variantSelected)))
            retireTurnTimeout();
    }
    else if(0 <= stoneSelected)
    {
        if(submitHumanAction(ClientDropIndex(stoneSelected)))
        {
            retireTurnTimeout();
            // Legacy four-hand Duel must offer the discard to the partner.
            if(!Multiplayer::session().active() && GameData::localMahjongAvatar() == myAvatar)
                buttonPass->setClicked();
            stoneSelected = -1;
        }
    }

    renderWindow();
}

bool MahjongPartScreen::actionSelectedShiftLeft(void)
{
    if(0 < variantSelected)
    {
        playSound("select");
        variantSelected--;
        renderWindow();
        return true;
    }
    else if(0 <= stoneSelected)
    {
        if(0 < stoneSelected)
            stoneSelected--;
        else
            stoneSelected = ld.myPlayer().newStone.isValid() ? stonesPos.size() : stonesPos.size() - 1;
        playSound("select");
        renderWindow();
        return true;
    }

    return false;
}

bool MahjongPartScreen::actionSelectedShiftRight(void)
{
    if(0 <= variantSelected && variantSelected < variantsPos.size() - 1)
    {
        playSound("select");
        variantSelected++;
        renderWindow();
        return true;
    }
    else if(0 <= stoneSelected)
    {
        if(stoneSelected < (ld.myPlayer().newStone.isValid() ? stonesPos.size() : stonesPos.size() - 1))
            stoneSelected++;
        else
            stoneSelected = 0;
        playSound("select");
        renderWindow();
        return true;
    }

    return false;
}

void MahjongPartScreen::actionButtonShowCast(void)
{
    const LocalPlayer & player = ld.myPlayer();

    RuneCastDialog castDialog(player, player.newStone, *this);
    if(0 < castDialog.exec())
    {
        if(ld.yourTurn())
        {
            if(castDialog.resultIsCreature())
            {
                const Creature cr = castDialog.resultCreature();
                ShowSummonCreatureDialog summonDialog(ld, cr, *this);
                if(summonDialog.exec())
                    submitHumanAction(ClientSummonCreature(cr, summonDialog.land()));
            }
            else
            {
                const Spell sp = castDialog.resultSpell();
                const SpellInfo & spellInfo = GameData::spellInfo(sp);

                if((spellInfo.target() == SpellTarget::AllPlayers) ||
                   (spellInfo.target() == SpellTarget::MyPlayer))
                {
                    submitHumanAction(ClientCastSpell(sp));
                }
                else if(spellInfo.target() == SpellTarget::OtherPlayer)
                {
                    animationTurn.setPause(true);

                    TargetPlayerDialog targetDialog(ld, *this);
                    if(targetDialog.exec())
                    {
                        auto avaid = static_cast<Avatar::avatar_t>(targetDialog.resultCode());
                        submitHumanAction(ClientCastSpell(sp, avaid));
                    }

                    animationTurn.setPause(false);
                }
                else
                {
                    ShowCastSpellDialog castDialog(ld, sp, *this);
                    int result = castDialog.exec();
                    if(0 < result)
                    {
                        // Result 1 targets a land; creature and teleport targets carry a unit id.
                        int battleUnit = 1 == result ? -1 : castDialog.unit();
                        submitHumanAction(ClientCastSpell(sp, castDialog.land(), battleUnit));
                    }
                }
            }
        }
        else
        {
            ActionRejection rejection;
            rejection.reason = ActionRejectReason::WrongTurn;
            showActionRejection(rejection);
        }
    }

    // Map-based spell and summon dialogs use the adventure screen theme.
    // Restore the local clan track before returning to the rune table.
    restoreMahjongMusic();
    renderWindow();
}
