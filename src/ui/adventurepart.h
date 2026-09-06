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

#ifndef _RWNA_ADVENTUREPART_
#define _RWNA_ADVENTUREPART_

#include "adventurehints.h"
#include "dialogs.h"
class MapScreenBase;

class LandPolygon : public WindowToolTipArea
{
    const LandInfo &	landInfo;
    Clan		owner;
    Polygon		poly;
    SpritesAnimation    animationPower;
    SpritesAnimation    animationFlag;
    Texture		combatFightTexture;
    bool		combatFightStatus;

protected:
    bool		mousePressEvent(const ButtonEvent &) override;
    bool		mouseClickEvent(const ButtonsEvent &) override;
    void        	mouseFocusEvent(void) override;
    void		tickEvent(u32 ms) override;
    bool		userEvent(int, void*) override;

public:
    LandPolygon(const LandInfo &, const JsonObject &, Window &);

    void        	renderWindow(void) override;
    bool        	isAreaPoint(const Point &) const override;

    void		animationsDisabled(bool);
};

class AffectedSpellsIcon
{
    std::list<SpellIcon> icons;
    int			 bcrUid;
    Rect		 leftArea;
    Rect		 rightArea;

public:
    AffectedSpellsIcon(const JsonObject &);

    const int &		battleUnit(void) const { return bcrUid; }

    void		updateSpells(const BattleCreature &, Window &);
    void		setVisible(bool);
};

class PartyCreaturesBar : public Window
{
    Clan		clan;
    Land		land;
    Point		clanIconOffset;

    BattleParty*	currentParty(void) const;

protected:
    bool		mouseClickEvent(const ButtonsEvent &) override;

public:
    PartyCreaturesBar(Window &);

    void        	renderWindow(void) override;
    void		setParty(const Clan &, const Land &);
    void		setClanIconOffset(const Point & value) { clanIconOffset = value; }
    void		reset(void) { clan = Clan(); land = Land(); }

    const Clan &	currentClan(void) const { return clan; }
};

class MapScreenBase : public JsonWindow
{
protected:
    friend class	LandPolygon;

    LocalData 		ld;
    std::string		defaultFont;
    Color		defaultColor;
    Color		statChangedUpColor;
    Color		statChangedDownColor;
    Color               cueMoveColor;
    Color               cueAttackColor;
    Color               cueClaimColor;

    std::list<LandPolygon*> lands;

    Sprite		spriteLandStat1;
    Sprite		spriteLandStat2;
    Sprite		spriteLandStat3;
    Sprite		spriteLandStat4;
    Sprite		spriteLandStat5;
    Sprite		spriteLandStat6;

    Sprite		spriteCreatureStat1;
    Sprite		spriteCreatureStat2;
    Sprite		spriteCreatureStat3;
    Sprite		spriteCreatureStat4;
    Sprite		spriteCreatureStat5;
    Sprite		spriteCreatureStat6;

    Clan		selectedClan;
    Land		selectedLand;
    BattleCreature	selectedCreature;

    Land                forecastTarget;
    AdventureHints::BattlePreview forecastPreview;

    Texture 		townTowerWindsTexture;
    Point 		townTowerWindsPos;

    AffectedSpellsIcon	affectedSpells;

    std::list<SpritesAnimation>
			animationMapObjects;

    PartyCreaturesBar	bar1;
    PartyCreaturesBar	bar2;

    Point		landNamePos;
    Point		info1NamePos;
    Point		info2NamePos;
    Point		viewMapPos;
    Rect		selectedIconPos;

    JsonTextInfo        forecastTitle;
    JsonTextInfo        forecastCapture;
    JsonTextInfo        forecastSurvival;
    JsonTextInfo        forecastForces;
    JsonTextInfo        forecastScope;
    JsonTextInfo        cueLegend;

    virtual void	renderLabel(void) {}
    virtual bool	isAdventureMode(void) const { return false; }
    virtual AdventureHints::DestinationCue mapCue(const LandInfo &) const;

    const Color &	getBaseStatColor(int, int) const;

    void		animationsDisabled(bool);

    void		renderLandInfo(const Land &);
    void                renderBattleForecast(void);
    void		renderClanAvatarInfo(const RemotePlayer &);
    void		renderCreatureInfo(const BattleCreature &);

    void                updateBattleForecast(const Land &, const Land &);
    void                clearBattleForecast(void);

    void		actionButtonChat(void);
    void		actionButtonDismiss(void);
    void		actionButtonInfo(void);
    void		actionButtonUndo(void);
    void		actionButtonMenu(void);

protected:
    bool		userEvent(int, void*) override;
    void		tickEvent(u32) override;

public:
    MapScreenBase(const LocalData &, Window* win = nullptr);

    void		renderWindow(void) override;
    virtual bool	isAllowMoveFlag(const LandInfo &) const;
    bool                isAllowLandClaim(const LandInfo &) const;
    const Color &       mapCueColor(AdventureHints::DestinationCue) const;
    bool		isMyClan(const Clan &) const;
};

class MoveFlagWindow : public Window
{
    Texture		flagTexture;
    Land		land;

protected:
    void		mouseTrackingEvent(const Point &, u32 buttons) override;

public:
    MoveFlagWindow(const Clan &, Window &);

    void		renderWindow(void) override;
    bool                isAreaPoint(const Point &) const override { return false; }

    void		setVisible(bool);
    void                setClan(const Clan &);
    void		setLand(const Land & ln) { land = ln; }
    const Land &	fromLand(void) const { return land; }
};

#ifdef BUILD_DEBUG
class DebugConsole : public CommandConsole
{
protected:
    bool                actionCommand(const std::string &) override;

public:
    DebugConsole(const Size & gfxsz, const FontRender & frs, Window & win) : CommandConsole(gfxsz, frs, win)
    {
    }
};
#endif

class AdventurePartScreen : public MapScreenBase
{
    Avatar		myAvatar;
    bool		allowTickEvent;
    int			delayCombatResult;

    std::vector<CreatureMoved>
			history;

    MoveFlagWindow	moveFlag;
    Land                orderSource;

    JsonButton*         buttonOrder;
    JsonButton*         buttonDone;
    JsonButton*         buttonUndo;
    JsonButton*         buttonDismiss;

    ActionList          actions;
    TickTrigger		tt;
    std::uint64_t       networkRevision = 0;

#ifdef BUILD_DEBUG
    std::unique_ptr<DebugConsole> console;
    Land                debugLand;

    bool                actionDebugCommandLands(void);
    bool                actionDebugCommandLand(const std::string &);
    bool                actionDebugCommandParty(void);
#endif

    void		renderLabel(void) override;
    bool		isAdventureMode(void) const override { return true; }
    bool                submitHumanAction(const ClientMessage &);
    bool                selectLocalAvatar(void);
    void                syncNetworkState(void);
    void                showActionRejection(const ActionRejection &);
    void		updateCommandButtons(void);
    bool                commitPendingOrders(void);
    void                cancelOrderMode(bool redraw = true);
    bool                moveSelectedParty(const Land &, const Land &);

    void		actionButtonDone(void);
    void		actionButtonOrder(void);
    void		actionButtonDismiss(void);
    void		actionButtonInfo(void);
    void		actionButtonUndo(void);
    void		actionButtonMenu(void);

    bool		actionAdventureTurn(const ActionMessage &);
    bool		actionAdventureMoves(const ActionMessage &);
    bool                actionAdventureClaim(const ActionMessage &);
    bool                actionAdventureBattleChoice(const ActionMessage &);
    bool		actionAdventureCombat(const ActionMessage &);
    bool		actionAdventureEnd(const ActionMessage &);

protected:
    bool		userEvent(int, void*) override;
    void		tickEvent(u32) override;
    bool                keyPressEvent(const KeySym &) override;
    bool                mouseReleaseEvent(const ButtonEvent &) override;

public:
    AdventurePartScreen(const Avatar &);
    bool                isAllowMoveFlag(const LandInfo &) const override;
    AdventureHints::DestinationCue mapCue(const LandInfo &) const override;

#ifdef BUILD_DEBUG
    bool                actionDebugCommand(const std::string &);
#endif
};

#endif
