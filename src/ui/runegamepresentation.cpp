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

#include "settings.h"
#include "runewars.h"
#include "gametheme.h"
#include "dialogs.h"
#include "runegamewidgets.h"
#include "actions.h"
#include "crashreport.h"
#include "mahjongpart.h"
#include "matchsession.h"

namespace
{
class ScopedTickPause
{
    Window & window;

public:
    explicit ScopedTickPause(Window & win) : window(win)
    {
        window.disableTickEvent(true);
    }

    ~ScopedTickPause()
    {
        window.disableTickEvent(false);
    }

    ScopedTickPause(const ScopedTickPause &) = delete;
    ScopedTickPause & operator=(const ScopedTickPause &) = delete;
};

class ScopedFlag
{
    bool & flag;

public:
    explicit ScopedFlag(bool & value) : flag(value)
    {
        flag = true;
    }

    ~ScopedFlag()
    {
        flag = false;
    }

    ScopedFlag(const ScopedFlag &) = delete;
    ScopedFlag & operator=(const ScopedFlag &) = delete;
};
}

void MahjongPartScreen::tickEvent(u32 ms)
{
    if(Multiplayer::session().active() && Multiplayer::session().paused())
    {
        turnTimeoutPending = false;
        animationTurn.setPause(true);
    }
    else if(Multiplayer::session().active()) animationTurn.setPause(false);
    // Modal dialogs run a nested event loop. Keep the Mahjong client from polling
    // and handling the same server prompt again while a Luck choice is open.
    if(resolvingLuckChoice) return;

    if(animationGame.isEnabled())
    {
	if(animationGame.next(ms))
	    renderWindow();

	// need wait end animation
	return;
    }
    else
    if(animationKong.isEnabled())
    {
	if(animationKong.next(ms))
	    renderWindow();

	// need wait end animation
	return;
    }
    else
    if(animationPung.isEnabled())
    {
	if(animationPung.next(ms))
	    renderWindow();

	// need wait end animation
	return;
    }
    else
    if(animationChao.isEnabled())
    {
	if(animationChao.next(ms))
	    renderWindow();

	// need wait end animation
	return;
    }

    if(animationTurn.isEnabled())
    {
	if(animationTurn.next(ms))
	    renderWindow();
    }

    // Consume expiry in the same presentation tick, before polling or draining
    // authoritative actions.  A queued SDL event could otherwise arrive after
    // AI turns have advanced and press a control belonging to a later choice.
    if(turnTimeoutPending)
    {
	turnTimeoutPending = false;
	actionOutOfTime();
	return;
    }

    if(playerReady && tt.check(ms, Settings::presentationDelay(100)))
    {
	// Presentation actions can deliberately remain queued while an animation runs.
	// Consume them before asking the authoritative state for another action.
	if(actions.empty())
	{
	    const bool switched = selectLocalAvatar();
	    Multiplayer::runeEvents(myAvatar, actions);
	    if(Multiplayer::session().active() && networkRevision != Multiplayer::session().revision())
	        syncNetworkState();
	    if(switched && actions.empty()) renderWindow();
	}
	bool redraw = false;

	while(actions.size())
	{
	    auto action = actions.front();
	    actions.pop_front();
	    fastLogText.color = fastLogNormalColor;
	    CrashReport::breadcrumb(std::string("Mahjong action type=")
	        .append(String::number(action.type())).append(" payload=").append(action.toString()));

	    switch(action.type())
	    {
		case Action::MahjongBegin:
		    redraw = actionMahjongBegin(action);
		    break;

		case Action::MahjongEnd:
		    redraw = actionMahjongEnd(action);
		    break;

	        case Action::MahjongTurn:
		    redraw = actionMahjongTurn(action);
		    break;

	        case Action::MahjongLuckChoice:
		    redraw = actionMahjongLuckChoice(action);
		    break;

	        case Action::MahjongPass:
		    redraw = actionMahjongPass(action);
		    break;

	        case Action::MahjongGame:
		    actionMahjongGame(action);
		    // for start animationGame
		    return;

	        case Action::MahjongKong1:
		    actionMahjongKong1(action);
		    // for start animationKong
		    return;

	        case Action::MahjongKong2:
		    actionMahjongKong2(action);
		    // for start animationKong
		    return;

	        case Action::MahjongPung:
		    actionMahjongPung(action);
		    // for start animationPung
		    return;

	        case Action::MahjongChao:
		    actionMahjongChao(action);
		    // for start animationChao
		    return;

	        case Action::MahjongDrop:
		    redraw = actionMahjongDrop(action);
		    break;

	        case Action::MahjongSummon:
		    redraw = actionMahjongSummon(action);
		    break;

	        case Action::MahjongCast:
		    redraw = actionMahjongCast(action);
		    break;

	        case Action::MahjongInfo:
		    redraw = actionMahjongInfo(action);
		    break;

	        case Action::MahjongData:
		    redraw = actionMahjongLoadData();
		    break;

		default:
		    ERROR("unknown action: " << action.type());
		    break;
	    }

	    if(action.type() == Action::MahjongBegin ||
	       action.type() == Action::MahjongData ||
	       action.type() == Action::MahjongEnd)
	    {
		const std::string reason = std::string("mahjong-action-") + String::number(action.type());
		if(!GameData::saveRecovery(toJsonObject(), reason))
		    ERROR("recovery checkpoint failed: " << reason);
	    }
	}

	if(redraw) renderWindow();
    }
}

bool MahjongPartScreen::actionMahjongLoadData(void)
{
    if(Multiplayer::session().active())
    {
        syncNetworkState();
        return true;
    }
    ld = GameData::toLocalData(myAvatar);
    // A completed Chao clears the authoritative discard before its data
    // refresh reaches the screen. Do not keep rendering a now-invalid Chao
    // variant against that cleared discard.
    if(!ld.dropStone.isValid()) variantSelected = -1;

    syncAffectedSpellIndicators();

    return true;
}

void MahjongPartScreen::syncNetworkState(void)
{
    const Wind previousWind = ld.currentWind;
    const Stone previousDraw = ld.myPlayer().newStone;
    const Stone previousDrop = ld.dropStone;
    ld = GameData::toLocalData(myAvatar);
    if(!ld.dropStone.isValid() || previousDrop != ld.dropStone || previousWind != ld.currentWind)
        networkClaimSubmitted = false;
    networkRevision = Multiplayer::session().revision();
    playerReady = true;
    buttonLocalReady.setVisible(false);
    const bool playable = Multiplayer::session().phase() == Menu::MahjongPart;
    const bool ownTurn = playable && ld.yourTurn() && !ld.dropStone.isValid();
    LocalPlayer& player = ld.myPlayer();
    if(!ownTurn || previousWind != ld.currentWind || previousDraw != player.newStone)
        stoneSelected = -1;
    if(!ld.dropStone.isValid()) variantSelected = -1;
    if(ownTurn && player.newStone.isValid() && stoneSelected < 0)
        stoneSelected = static_cast<int>(player.stones.size());
    // Pung/Chao have no draw, but the caller must still be able to select a
    // discard after reconnect or when the phase wait consumed the prompt.
    if(ownTurn && !player.newStone.isValid() && stoneSelected < 0 &&
       player.stones.size() % 3 == 2)
        stoneSelected = static_cast<int>(player.stones.size()) - 1;
    refreshDiscardClaimButtons();
    if(!playable)
    {
        buttonPass->setVisible(false);
        buttonChao->setVisible(false);
        buttonPung->setVisible(false);
        buttonKong->setVisible(false);
        buttonGame->setVisible(false);
    }
    WinResults result;
    buttonLocalGame.setVisible(ownTurn && player.isWinMahjong(ld.currentWind, ld.roundWind,
                                                             ld.dropStone, &result));
    buttonLocalKong.setVisible(ownTurn && player.isMahjongKong2(ld.currentWind));
    buttonLocalGame.setPosition(localGamePos());
    buttonLocalKong.setPosition(localKongPos());
    if(!ownTurn) retireTurnTimeout();
    else if(stoneSelected >= 0 && !animationTurn.isEnabled()) animationTurn.setEnabled(true);
    syncAffectedSpellIndicators();
}

bool MahjongPartScreen::selectLocalAvatar(void)
{
    const Avatar selected = Multiplayer::session().active() ? Multiplayer::session().localAvatar() : GameData::localMahjongAvatar();
    if(!selected.isValid() || selected == myAvatar) return false;

    myAvatar = selected;
    ld = GameData::toLocalData(myAvatar);
    stoneSelected = -1;
    variantSelected = -1;
    refreshDiscardClaimButtons();
    syncAffectedSpellIndicators();
    return true;
}

void MahjongPartScreen::refreshDiscardClaimButtons(void)
{
    const bool mayClaimDiscard = ld.dropStone.isValid() && !ld.yourTurn() &&
        (!Multiplayer::session().active() || !networkClaimSubmitted);
    LocalPlayer & player = ld.myPlayer();
    WinResults result;

    buttonPass->setVisible(Multiplayer::session().active() ? mayClaimDiscard : ld.dropStone.isValid());
    buttonGame->setVisible(mayClaimDiscard &&
        player.isWinMahjong(ld.currentWind, ld.roundWind, ld.dropStone, &result));
    buttonKong->setVisible(mayClaimDiscard &&
        player.isMahjongKong1(ld.currentWind, ld.dropStone));
    buttonPung->setVisible(mayClaimDiscard &&
        player.isMahjongPung(ld.currentWind, ld.dropStone));
    buttonChao->setVisible(mayClaimDiscard &&
        player.isMahjongChao(ld.currentWind, ld.dropStone));
}

bool MahjongPartScreen::actionMahjongLuckChoice(const ActionMessage & v)
{
    const auto action = static_cast<const MahjongLuckChoice &>(v);
    const VecStones choices = action.choices();
    ld.currentWind = action.currentWind();

    if(!ld.yourTurn() || choices.size() != 2)
    {
	ERROR("invalid Luck draw action");
	return false;
    }

    ScopedFlag resolving(resolvingLuckChoice);
    LuckDrawDialog dialog(choices, *this);
    {
	// A nested dialog loop still ticks its parent unless explicitly paused.
	ScopedTickPause pause(*this);
	dialog.exec();
    }

    // Older/re-entrant polling may already have queued copies of this prompt.
    // They can never be a new valid choice before MahjongTurn/MahjongData.
    actions.remove_if([](const ActionMessage & queued) {
	return queued.type() == Action::MahjongLuckChoice;
    });

    const int selected = dialog.resultCode();
    if(selected < 0 || 1 < selected)
    {
	ERROR("invalid Luck draw selection: " << selected);
	return false;
    }

    CrashReport::breadcrumb(std::string("Luck selection=").append(String::number(selected)));
    if(!submitHumanAction(ClientLuckChoice(selected)))
	ERROR("Luck draw selection was rejected: " << selected);
    return false;
}

bool MahjongPartScreen::actionMahjongEnd(const ActionMessage & v)
{
    auto action = static_cast<const MahjongEnd &>(v);
    ld.currentWind = action.currentWind();

    // The authoritative round end invalidates any countdown that belonged to
    // the last local turn.  Leaving it active can emit a stale timeout while
    // the end-of-round presentation is running.
    retireTurnTimeout();

#ifdef BUILD_DEBUG
    // A visible AI takeover is intentionally scoped to one complete phase.
    GameData::setDeveloperAutoplay(myAvatar, false);
#endif

#ifndef SWE_DISABLE_AUDIO
    playSoundWait();
    Music::reset();
#endif

    pushEventAction(Action::MahjongGameQuit, this, nullptr);
    DEBUG("current wind: " << ld.currentWind.toString());

    return false;
}

bool MahjongPartScreen::actionMahjongBegin(const ActionMessage & v)
{
    // A rejected command/reconnect replays Begin plus the current prompt.
    networkClaimSubmitted = false;
    auto action = static_cast<const MahjongBegin &>(v);
    ld.currentWind = action.currentWind();
    ld.roundWind = action.roundWind();

    // A new part must never inherit the final countdown from the previous one.
    retireTurnTimeout();

    playSound("begin");
    playSoundWait();

    if(action.newRound())
    {
	playSound(std::string("round").append("_").append(ld.roundWind.toString()));
	playSoundWait();
    }

    gameLogs << StringFormat(_("Game Begins With %1")).arg(GameData::windInfo(ld.roundWind).name);
    DEBUG("new round: " << (action.newRound() ? "true" : "false") << ", " << "wind round: " << ld.roundWind.toString());

    renderInitialDeal();

    return false;
}

bool MahjongPartScreen::actionMahjongTurn(const ActionMessage & v)
{
    auto action = static_cast<const MahjongTurn &>(v);
    ld.currentWind = action.currentWind();

    // Every authoritative turn change retires the previous local countdown.
    retireTurnTimeout();

    stoneSelected = -1;
    variantSelected = -1;
    playersMarker = 0;

    if(! ld.yourTurn())
    {
	const RemotePlayer & owner = ld.playerOfWind(ld.currentWind);
	DEBUG(owner.toString());
	return false;
    }

    animationTurn.setEnabled(true);

    LocalPlayer & player = ld.myPlayer();

    player.newStone = GameStone(action.newStone(), false);
    player.setCasted(false);

    if(action.showGame())
    {
	buttonLocalGame.setPosition(localGamePos());
	buttonLocalGame.setVisible(true);
    }

    if(action.showKong())
    {
	buttonLocalKong.setPosition(localKongPos());
	buttonLocalKong.setVisible(true);
    }

    if(player.newStone.isValid())
    {
	playSound("stone2");
	stoneSelected = player.stones.size();
	renderDrawStone();

	DEBUG("current wind: " << ld.currentWind.toString() << ", " << "new stone: " << player.newStone());
	return true;
    }

    stoneSelected = player.stones.size() - 1;

    DEBUG("current wind: " << ld.currentWind.toString() << ", " << "new stone: " << "skipped");
    return true;
}

bool MahjongPartScreen::actionMahjongGame(const ActionMessage & v)
{
    auto action = static_cast<const MahjongGame &>(v);
    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    if(action.sayOnly())
    {
	buttonPass->setVisible(true);
	buttonChao->setVisible(false);
	buttonPung->setVisible(false);
	buttonKong->setVisible(false);

	playSound(owner.avatar.toString().append("_game"));
	renderWaitPlayers(ownerWind);

	fastLogText.text = StringFormat(_("%1 Call Game")).arg(owner.name());
	fastLogOwner = ownerWind;

	gameLogs << fastLogText.text;
	return true;
    }
    else
    {
	animationGame.setEnabled(true);

	DEBUG(owner.toString() << ", " << "drop stone: " << ld.dropStone());
    }

    //animationTurn.stop();

    return false;
}

u32 startAnimationBackgroundGuard(u32 ms, void* ptr)
{
    if(ptr)
    {
	SpritesAnimation* animation = reinterpret_cast<SpritesAnimation*>(ptr);
#ifndef SWE_DISABLE_AUDIO
	if(Settings::soundGuardianRules())
	    while(Sound::isPlaying()) Tools::delay(50);
#endif
	animation->setEnabled(true);
    }

    return 0;
}

bool MahjongPartScreen::actionMahjongKong1(const ActionMessage & v)
{
    auto action = static_cast<const MahjongKong1 &>(v);
    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    if(action.sayOnly())
    {
	buttonChao->setVisible(false);

	playSound(owner.avatar.toString().append("_kong"));
	timerVoiceAnimation = Timer::create(100, startAnimationBackgroundGuard, & animationKong);
	renderWaitPlayers(ownerWind);

	fastLogText.text = StringFormat(_("%1 Kongs The Last Discard")).arg(owner.name());
	fastLogOwner = ownerWind;

	gameLogs << fastLogText.text;
	return true;
    }
    else
    {
	ld.dropStone = action.dropStone();
	DEBUG(owner.toString() << ", " << "drop stone: " << ld.dropStone());
    }

    return false;
}

bool MahjongPartScreen::actionMahjongKong2(const ActionMessage & v)
{
    auto action = static_cast<const MahjongKong2 &>(v);
    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    DEBUG(owner.toString());

    playSound(owner.avatar.toString().append("_kong"));
    timerVoiceAnimation = Timer::create(100, startAnimationBackgroundGuard, & animationKong);

    fastLogText.text = StringFormat(_("%1 Kongs The Last Drawn Rule")).arg(owner.name());
    fastLogOwner = ld.currentWind;

    gameLogs << fastLogText.text;
    return true;
}

bool MahjongPartScreen::actionMahjongPung(const ActionMessage & v)
{
    auto action = static_cast<const MahjongPung &>(v);
    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    if(action.sayOnly())
    {
	buttonChao->setVisible(false);

	playSound(owner.avatar.toString().append("_pung"));
	timerVoiceAnimation = Timer::create(100, startAnimationBackgroundGuard, & animationPung);
	renderWaitPlayers(ownerWind);

	fastLogText.text = StringFormat(_("%1 Pungs The Last Discard")).arg(owner.name());
	fastLogOwner = ownerWind;
	gameLogs << fastLogText.text;
	return true;
    }
    else
    {
	ld.dropStone = action.dropStone();
	DEBUG(owner.toString() << ", " << "drop stone: " << ld.dropStone());
    }

    return false;
}

bool MahjongPartScreen::actionMahjongChao(const ActionMessage & v)
{
    auto action = static_cast<const MahjongChao &>(v);
    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    if(action.sayOnly())
    {
	playSound(owner.avatar.toString().append("_chao"));
	timerVoiceAnimation = Timer::create(100, startAnimationBackgroundGuard, & animationChao);
	renderWaitPlayers(ownerWind);

	fastLogText.text = StringFormat(_("%1 Chows The Last Discard")).arg(owner.name());
	fastLogOwner = ownerWind;
	gameLogs << fastLogText.text;

	return true;
    }
    else
    {
	ld.dropStone = action.dropStone();
	DEBUG(owner.toString() << ", " << "drop stone: " << ld.dropStone());
    }

    return false;
}

bool MahjongPartScreen::actionMahjongPass(const ActionMessage & v)
{
#ifdef BUILD_DEBUG
    auto action = static_cast<const MahjongPass &>(v);

    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    DEBUG(owner.toString());
#endif
    return false;
}

bool MahjongPartScreen::actionMahjongDrop(const ActionMessage & v)
{
    auto action = static_cast<const MahjongDrop &>(v);

    ld.currentWind = action.currentWind();
    ld.dropStone = action.dropStone();

    LocalPlayer & player = ld.myPlayer();
    refreshDiscardClaimButtons();

    player.newStone.reset();
    stoneSelected = -1;

    renderWaitPlayers(ld.currentWind);
    renderDropStone();

    DEBUG("current wind: " << ld.currentWind.toString() << ", " << "drop stone: " << ld.dropStone());

    return true;
}

bool MahjongPartScreen::actionMahjongSummon(const ActionMessage & v)
{
    auto action = static_cast<const MahjongSummon &>(v);

    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    Creature creature = action.creature();
    Land land = action.land();

    const CreatureInfo & creatureInfo = GameData::creatureInfo(creature);
    const LandInfo & landInfo = GameData::landInfo(land);

    playSound(creatureInfo.sound1);

    fastLogText.text = StringFormat(_("%1 summons %2 in %3")).
			arg(GameData::avatarInfo(owner.avatar).name).
			arg(creatureInfo.name).
			arg(landInfo.name);
    fastLogOwner = ownerWind;
    gameLogs << fastLogText.text;

    DEBUG(owner.toString() << ", " << "creature: " << creature.toString() << ", " << "land: " << land.toString());
    return true;
}

bool MahjongPartScreen::actionMahjongCast(const ActionMessage & v)
{
    auto action = static_cast<const MahjongCast &>(v);

    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    Spell spell = action.spell();
    const std::vector<int> targetUnits = action.targetUnits();
    std::vector<int> resists = action.resists();

    const SpellInfo & spellInfo = GameData::spellInfo(spell);

    playSound(spellInfo.sound);

    fastLogText.text.clear();
    fastLogOwner = ownerWind;

    if(spellInfo.target() == SpellTarget::Land)
    {
	Land land = action.land();
	const LandInfo & landInfo = GameData::landInfo(land);
	fastLogText.text = StringFormat(_("%1 casts %2 over %3")).
			    arg(owner.name()).arg(spellInfo.name).arg(landInfo.name);
	gameLogs << fastLogText.text;
    }
    else
    if(spellInfo.target() == SpellTarget::MyPlayer ||
	spellInfo.target() == SpellTarget::AllPlayers)
    {
	fastLogText.text = StringFormat(_("%1 casts %2")).
			    arg(owner.name()).arg(spellInfo.name);
	gameLogs << fastLogText.text;

    }
    else
    if(spellInfo.target() == SpellTarget::OtherPlayer)
    {
	Avatar target = action.target();
	const RemotePlayer & remote = ld.playerOfAvatar(target);

	fastLogText.text = StringFormat(_("%1 casts %2 to %3")).
			    arg(owner.name()).arg(spellInfo.name).arg(remote.name());
	gameLogs << fastLogText.text;
    }

    for(int unit : targetUnits)
    {
	const BattleCreature* target = ld.findBattleUnitConst(unit);

	if(target)
	{
	    bool resist = resists.end() != std::find(resists.begin(), resists.end(), target->battleUnit());

	    DEBUG(target->toString());
	    if(resist) DEBUG("Magic Resistance!");

	    const CreatureInfo & creatureInfo = GameData::creatureInfo(*target);
	    std::string tmp;

	    if(!GameData::allied(owner.clan, target->clan()))
		tmp = StringFormat(_("%1 casts %2 at %3's %4")).
		    arg(owner.name()).arg(spellInfo.name).arg(ld.playerOfClan(target->clan()).name()).arg(creatureInfo.name);
	    else
		tmp = StringFormat(_("%1 casts %2 at friendly %3")).
		    arg(owner.name()).arg(spellInfo.name).arg(creatureInfo.name);

	    if(resist) tmp.append(", ").append(_("Magic Resistance!"));

	    if(fastLogText.text.empty()) fastLogText.text = tmp;
	    gameLogs << tmp;
	}
    }

    if(fastLogText.text.empty() && !targetUnits.empty())
    {
	fastLogText.text = StringFormat(_("%1 casts %2")).arg(owner.name()).arg(spellInfo.name);
	gameLogs << fastLogText.text;
    }

    DEBUG(owner.toString() << ", " << "spell: " << spell.toString() << ", " << "target: " << spellInfo.target.toString());
    return true;
}

bool MahjongPartScreen::actionMahjongInfo(const ActionMessage & v)
{
    auto action = static_cast<const MahjongInfo &>(v);

    Wind ownerWind = action.currentWind();
    const RemotePlayer & owner = ld.playerOfWind(ownerWind);

    const std::string info = action.info();

    fastLogText.text = info;
    fastLogOwner = ownerWind;

    gameLogs << info;
    DEBUG(owner.toString() << ", " << "info: " << info);

    return true;
}
