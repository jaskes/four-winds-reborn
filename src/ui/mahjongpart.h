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
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef _RWNA_MAHJONGPART_
#define _RWNA_MAHJONGPART_

#include "gamedata.h"

struct MahjongAction;

struct WindMarker : std::pair<Texture, Texture>
{
    WindMarker() {}
    WindMarker(const Sprites &);
    WindMarker(const Texture & tx1, const Texture & tx2) : std::pair<Texture, Texture>(tx1, tx2) {}

    const Texture &	tx(void) const { return first; }
    void		swap(void) { first.swap(second); };
};

class OrderTurn
{
    WindMarker		windsMarker[4];
    SidesPositions	windPositions;

    WindMarker		createMarker(const Wind &, bool) const;
    void		renderCentered(Window &, const Point &, const WindMarker &) const;

public:
    OrderTurn(const JsonObject &);

    void		render(Window &, const Wind &, const Wind &, const Wind &) const;
    void                renderSeat(Window &, const Point &, const Wind &, bool) const;
};

struct TurnAnimation : SpritesAnimation
{
    bool		pause;

    TurnAnimation(const JsonObject & jo, const std::string & str) : SpritesAnimation(jo, str), pause(false) {}

    Rect		maxArea(void) const;
    void		renderTick(Window &) const;
    void		renderAll(Window &) const;
    bool		isLastSprite(void) const;

    void		setPause(bool f) { pause = f; }
    bool		isEnabled(void) const override { return pause ? false : SpritesAnimation::isEnabled(); }
};

class MahjongPartScreen : public JsonWindow
{
    Avatar		myAvatar;
    LocalData		ld;
    UnicodeList		gameLogs;

    Color		defaultColor;
    std::string		defaultFont;

    Texture		stoneActiveSprite;
    Texture		stoneSelectedSprite;
    Texture		stoneVariantSprite;

    OrderTurn		orderTurn;

    TurnAnimation	animationTurn;
    SpritesAnimation	animationChao;
    SpritesAnimation	animationPung;
    SpritesAnimation	animationKong;
    SpritesAnimation	animationGame;
    Timer		timerVoiceAnimation;

    SidesPositions	namesPos;
    std::string		namesFont;

    SidesPositions	winSetPos;
    Point		localSetPos;
    Point		croupierPos;
    Point		dropStonePos;
    Point		remainsPos;

    Rects		stonesPos;
    Rects		variantsPos;

    int			stoneSelected;
    int			variantSelected;
    int			playersMarker;
    int			animationDropStep;
    int			animationDropDelay;
    int                 animationDealDelay;
    bool                dealingInitialHand;
    std::size_t         dealtStoneCount;
    bool                animatingDrawStone;
    Point               drawStoneAnimationPos;

    IconToolTip		iconAffectedSkull;
    IconToolTip		iconAffectedSword;
    IconToolTip		iconAffectedNumber;
    IconToolTip		iconAffectedDiscard;
    IconToolTip		iconAffectedSilence;
    IconToolTip		iconAffectedScry;

    JsonTextInfo        fastLogText;
    Color               fastLogNormalColor;
    Wind		fastLogOwner;

    JsonButton*		buttonPass;
    JsonButton*		buttonChao;
    JsonButton*		buttonPung;
    JsonButton*		buttonKong;
    JsonButton*		buttonGame;
    JsonButton*         buttonCast;

    JsonButton		buttonLocalReady;
    JsonButton		buttonLocalKong;
    JsonButton		buttonLocalGame;

    bool		playerReady;
    bool		resolvingLuckChoice;
    bool                turnTimeoutPending;
    ActionList		actions;
    TickTrigger		tt;
    std::uint64_t       networkRevision = 0;
    bool                networkClaimSubmitted = false;

    void		actionButtonLocalReady(void);
    bool                selectLocalAvatar(void);
    void                refreshDiscardClaimButtons(void);
    void                syncNetworkState(void);
    bool                submitHumanAction(const ClientMessage &);
    void                showActionRejection(const ActionRejection &);
    void                retireTurnTimeout(void);
    void		actionButtonLocalKong(void);
    void		actionButtonLocalGame(void);
    void		actionOutOfTime(void);
    void		actionButtonPass(int);
    void		actionButtonShowCast(void);
    bool		actionPressSpace(void);
    void		actionDropSelected(void);
    bool		actionSelectedShiftLeft(void);
    bool		actionSelectedShiftRight(void);
    void		actionQuit(void);
    void		actionButtonChat(void);
    bool		actionButtonSystem(void);
    void		actionButtonSummary(void);
    void		actionButtonMap(void);
    void                restoreMahjongMusic(void);
    bool		actionCreateScreenshot(void);
    bool		actionEventDebug1(void);
    bool		actionEventDebug2(void);

    Point		localReadyPos(void) const;
    Point		localKongPos(void) const;
    Point		localGamePos(void) const;
    Rect		newStonePos(void) const;

    void		renderNames(void);
    void		renderNamesHorizontal(const RemotePlayer &, const Point &);
    void                renderDuelName(const RemotePlayer &, const Point &);
    void		renderNamesVertical(const RemotePlayer &, const Point &);
    void		renderScryRunes(void);
    void		renderScryVertical(const Stones &, const Point &);
    void		renderWinRules(void);
    void		renderWinRulesHorizontal(const WinRules &, const Point &);
    void		renderWinRulesVertical(const WinRules &, const Point &);
    void		renderCroupier(void);
    void		renderGameStoneRemains(void);
    void		renderLocalSet(const GameStones &);
    int			renderWinRuleVertical(const WinRule &, const Point &);
    int			renderWinRuleHorizontal(const WinRule &, const Point &);
    void		renderDropStone(void);
    void                renderDrawStone(void);
    void                renderInitialDeal(void);
    void		renderWaitPlayers(const Wind &);
    std::string		playerPrettyName(const RemotePlayer &) const;

    bool		actionMahjongLoadData(void);
    bool		actionMahjongBegin(const ActionMessage &);
    bool		actionMahjongEnd(const ActionMessage &);
    bool		actionMahjongTurn(const ActionMessage &);
    bool		actionMahjongLuckChoice(const ActionMessage &);
    bool		actionMahjongGame(const ActionMessage &);
    bool		actionMahjongKong1(const ActionMessage &);
    bool		actionMahjongKong2(const ActionMessage &);
    bool		actionMahjongPung(const ActionMessage &);
    bool		actionMahjongChao(const ActionMessage &);
    bool		actionMahjongPass(const ActionMessage &);
    bool		actionMahjongDrop(const ActionMessage &);
    bool		actionMahjongSummon(const ActionMessage &);
    bool		actionMahjongCast(const ActionMessage &);
    bool		actionMahjongInfo(const ActionMessage &);

    bool		checkCastInformer(void) const;
    void                syncAffectedSpellIndicators(void);

protected:
    bool		userEvent(int, void*) override;
    bool		keyPressEvent(const KeySym &) override;
    bool		mouseClickEvent(const ButtonsEvent &) override;
    void		tickEvent(u32) override;

    JsonObject		toJsonObject(void);
    bool		fromJsonObject(const JsonObject &);

public:
    MahjongPartScreen();

    void		renderWindow(void) override;
};

#endif
