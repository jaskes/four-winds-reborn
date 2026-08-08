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

#include <algorithm>
#include <iterator>
#ifdef BUILD_DEBUG
#include <sstream>
#endif

#include "gamedata.h"
#include "gametheme.h"
#include "dialogs.h"
#include "adventureuievents.h"
#include "actions.h"
#include "adventurepart.h"
#ifdef BUILD_DEBUG
#include "developertools.h"
#endif

namespace
{
class ScopedAdventureTickPause
{
    Window & window;

public:
    explicit ScopedAdventureTickPause(Window & win) : window(win)
    {
        window.disableTickEvent(true);
    }

    ~ScopedAdventureTickPause()
    {
        window.disableTickEvent(false);
    }

    ScopedAdventureTickPause(const ScopedAdventureTickPause &) = delete;
    ScopedAdventureTickPause & operator=(const ScopedAdventureTickPause &) = delete;
};
}

bool AdventurePartScreen::submitHumanAction(const ClientMessage & action)
{
    ActionRejection rejection;
    if(GameData::client2Adventure(myAvatar, action, actions, &rejection)) return true;

    showActionRejection(rejection);
    return false;
}

void AdventurePartScreen::showActionRejection(const ActionRejection & rejection)
{
    MessageBox(_("Action Rejected"), GameData::actionRejectionMessage(rejection),
               *this, false).exec();
}

void AdventurePartScreen::updateCommandButtons(void)
{
    const bool hasSelected = selectedLand.isValid() &&
	0 < ld.myPlayer().army.partySelected(selectedLand).size();
    const bool canDismiss = ld.yourTurn() && selectedClan == ld.myPlayer().clan && hasSelected;
    const bool canOrder = ld.yourTurn() && selectedLand.isValid() &&
	isAllowMoveFlag(GameData::landInfo(selectedLand));

    if(buttonDismiss) buttonDismiss->setDisabled(!canDismiss);
    if(buttonOrder) buttonOrder->setDisabled(!orderSource.isValid() && !canOrder);
}

bool AdventurePartScreen::isAllowMoveFlag(const LandInfo & info) const
{
    return !orderSource.isValid() && MapScreenBase::isAllowMoveFlag(info);
}

AdventureHints::DestinationCue AdventurePartScreen::mapCue(const LandInfo & info) const
{
    const Land origin = orderSource.isValid() ? orderSource :
        (moveFlag.isVisible() ? moveFlag.fromLand() : Land());
    if(origin.isValid()) return AdventureHints::destinationCue(ld, origin, info.id);
    return MapScreenBase::mapCue(info);
}

bool AdventurePartScreen::commitPendingOrders(void)
{
    if(history.empty()) return true;

    bool accepted = true;
    for(const CreatureMoved & creatureMoved : history)
    {
        if(!submitHumanAction(ClientUnitMoved(creatureMoved.first, creatureMoved.second)))
        {
            accepted = false;
            break;
        }
    }

    history.clear();
    MapScreenBase::ld = GameData::toLocalData(myAvatar);
    selectedCreature.reset();
    affectedSpells.setVisible(false);
    bar1.reset();
    bar2.reset();
    updateCommandButtons();
    return accepted;
}

void AdventurePartScreen::cancelOrderMode(bool redraw)
{
    if(!orderSource.isValid()) return;

    orderSource.reset();
    clearBattleForecast();
    updateCommandButtons();
    if(redraw) renderWindow();
}

bool AdventurePartScreen::moveSelectedParty(const Land & fromLand, const Land & toLand)
{
    LocalPlayer & player = ld.myPlayer();
    const std::vector<int> moved = player.army.moveSelectedCreatures(fromLand, toLand);
    if(moved.empty()) return false;

    for(const int unit : moved)
	history.emplace_back(unit, toLand);

    // A split leaves the remaining source party ready for the next command.
    player.army.partySetAllSelected(fromLand);
    selectedCreature.reset();
    affectedSpells.setVisible(false);
    bar1.reset();
    bar2.reset();

    if(buttonUndo) buttonUndo->setDisabled(false);
    playSound("snddrop");

    const LandInfo & landInfo = GameData::landInfo(toLand);
    const RemotePlayer* defender = landInfo.clan.isValid() ?
        GameData::players().playerOfClan(landInfo.clan) : nullptr;
    if(defender && !GameData::allied(player, *defender))
	DisplayScene::pushEvent(nullptr, LandPolygonCombatStatus, const_cast<LandInfo*>(& landInfo));

    DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, nullptr);
    pushEventAction(MapScreenSelectLand, this, const_cast<LandInfo*>(& landInfo));
    updateCommandButtons();
    return true;
}

bool AdventurePartScreen::userEvent(int act, void* data)
{
    if(act == LandPolygonClickLeft || act == LandPolygonClickRight)
        clearBattleForecast();

    if(orderSource.isValid() && (act == LandPolygonClickLeft || act == LandPolygonClickRight))
    {
	if(act == LandPolygonClickRight || !data)
	{
	    cancelOrderMode();
	    return true;
	}

	const Land target = static_cast<LandInfo*>(data)->id;
	if(target == orderSource)
	{
	    cancelOrderMode();
	    return true;
	}

	const Land source = orderSource;
	if(moveSelectedParty(source, target))
	    cancelOrderMode();
	else
	{
	    updateBattleForecast(source, target);
	    MessageBox(_("Order"), _("Selected creatures cannot move to this territory."), *this, false).exec();
	    renderWindow();
	}
	return true;
    }

    bool requestClaim = false;
    if(act == LandPolygonClickLeft && data && ld.yourTurn())
    {
	auto landInfo = static_cast<LandInfo*>(data);
	// The first click selects the territory; a second click claims its deed.
	requestClaim = selectedLand == landInfo->id && AdventureHints::canClaimObserved(ld, landInfo->id);
    }

    if(MapScreenBase::userEvent(act, data))
    {
	if(requestClaim)
	{
	    auto landInfo = static_cast<LandInfo*>(data);
	    submitHumanAction(ClientLandClaim(landInfo->id));
	}
	updateCommandButtons();
	return true;
    }

    switch(act)
    {
	case Action::ButtonDone:	actionButtonDone(); return true;
	case Action::ButtonOrder:	actionButtonOrder(); return true;
	case Action::ButtonDismiss:	actionButtonDismiss(); return true;
	case Action::ButtonInfo:	actionButtonInfo(); return true;
	case Action::ButtonUndo:	actionButtonUndo(); return true;
	case Action::ButtonMenu:	actionButtonMenu(); return true;
        default: break;
    }

    if(ld.yourTurn())
    {
#ifdef BUILD_DEBUG
	if(act == AdventureTurnShowConsole)
	{
            if(! console)
                console.reset(new DebugConsole(Size(width(), 200), GameTheme::fontRender("console"), *this));
            console->setVisible(true);
            return true;
	}
        else
#endif
	if(act == AdventureTurnCreatureSelect)
	{
	    clearBattleForecast();
	    auto bcr = data ? static_cast<BattleCreature*>(data) : nullptr;
	    if(bcr)
	    {
		bcr->switchSelected();
		updateCommandButtons();
	    }
	    return true;
	}
	else
	if(act == AdventureTurnMoveStart)
	{
	    cancelOrderMode(false);
	    clearBattleForecast();
	    moveFlag.setLand(selectedLand);
	    moveFlag.setVisible(true);
	    playSound("sndtake");
	    return true;
	}
	else
	if(act == AdventureTurnMoveStop)
	{
	    clearBattleForecast();
	    moveFlag.setVisible(false);

	    moveSelectedParty(moveFlag.fromLand(), selectedLand);
	    return true;
	}
	else
	if(act == LandPolygonFocus)
	{
	    if(data)
	    {
		auto landInfo = static_cast<LandInfo*>(data);
		const Land origin = moveFlag.isVisible() ? moveFlag.fromLand() :
		    (orderSource.isValid() ? orderSource : selectedLand);
		updateBattleForecast(origin, landInfo->id);
		renderWindow();

		if(moveFlag.isVisible())
		    pushEventAction(MapScreenSelectLand, this, data);
	    }

	    return true;
	}
    }

    return false;
}

void AdventurePartScreen::actionButtonOrder(void)
{
    if(orderSource.isValid())
    {
	cancelOrderMode();
	return;
    }

    if(!ld.yourTurn() || !selectedLand.isValid()) return;
    const LandInfo & info = GameData::landInfo(selectedLand);
    if(!isAllowMoveFlag(info)) return;

    orderSource = selectedLand;
    clearBattleForecast();
    updateCommandButtons();
    playSound("sndtake");
    renderWindow();
}

void AdventurePartScreen::actionButtonMenu(void)
{
    ScopedAdventureTickPause pause(*this);
    const int choice = InGameMenuDialog(*this).exec();
    if(choice == InGameMenuResult::Cancel)
    {
        renderWindow();
        return;
    }

    JsonObject gui;
    gui.addString("type", "AdventurePart");
    if(choice == InGameMenuResult::SaveGame)
    {
        SaveGameAs(gui, *this, [this]()
        {
            if(commitPendingOrders()) return true;
            return false;
        });
        renderWindow();
        return;
    }

    if(!commitPendingOrders())
    {
        renderWindow();
        return;
    }

    if(!GameData::saveGame(gui))
    {
        MessageBox(_("Error"), _("Unable to save game."), *this, false).exec();
        renderWindow();
        return;
    }

    setResultCode(Menu::MainMenu);
    setVisible(false);
}

bool AdventurePartScreen::mouseReleaseEvent(const ButtonEvent & be)
{
    if(be.isButtonLeft() && moveFlag.isVisible())
        return userEvent(AdventureTurnMoveStop, nullptr);

    return false;
}

void AdventurePartScreen::actionButtonDismiss(void)
{
    LocalPlayer & player = ld.myPlayer();
    BattleCreatures bcrs = player.army.partySelected(selectedLand);

    if(bcrs.size())
    {
        for(auto & bcr : bcrs)
	{
	    DEBUG("remove creature: " << (*bcr).toString());
	    history.emplace_back((*bcr).battleUnit(), Land::None);
            player.army.remove(*bcr);
	}

	player.army.partySetAllSelected(selectedLand);

	cancelOrderMode(false);
	updateCommandButtons();
	if(buttonUndo) buttonUndo->setDisabled(false);

        selectedCreature.reset();
	affectedSpells.setVisible(false);
	bar1.reset();
	bar2.reset();

	// broadscast event
	DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, nullptr);
    }
}

void AdventurePartScreen::actionButtonInfo(void)
{
    MapStatusDialog(ld, *this).exec();
}

void AdventurePartScreen::actionButtonUndo(void)
{
    cancelOrderMode(false);
    if(!submitHumanAction(ClientAdventureUndo())) return;
    MapScreenBase::ld = GameData::toLocalData(myAvatar);
    const LandInfo & landInfo = GameData::landInfo(selectedLand);

    selectedLand.reset();
    selectedCreature.reset();
    selectedClan = ld.myPlayer().clan;
    affectedSpells.setVisible(false);

    bar1.reset();
    bar2.reset();

    DEBUG("all changes revert");
    history.clear();

    updateCommandButtons();
    if(buttonUndo) buttonUndo->setDisabled(true);

    // broadscast event
    DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, nullptr);
    DisplayScene::pushEvent(nullptr, LandPolygonCombatStatusReset, nullptr);
    // set selected land event
    pushEventAction(MapScreenSelectLand, this, const_cast<LandInfo*>(& landInfo));
}

void AdventurePartScreen::actionButtonDone(void)
{
    cancelOrderMode(false);
    if(buttonDone) buttonDone->setDisabled(true);

    if(!commitPendingOrders())
    {
        if(buttonDone) buttonDone->setDisabled(false);
        renderWindow();
        return;
    }

    if(!submitHumanAction(ClientBattleReady()) && buttonDone)
        buttonDone->setDisabled(false);
}

bool AdventurePartScreen::keyPressEvent(const KeySym & ks)
{
    if(ks.keycode() == SWE::Key::ESCAPE)
    {
	if(orderSource.isValid()) cancelOrderMode();
	else actionButtonMenu();
	return true;
    }

#ifdef BUILD_DEBUG
    if(ks.keycode() == SWE::Key::F9)
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

    if(ks.keycode() == SWE::Key::BACKQUOTE)
    {
        pushEventAction(AdventureTurnShowConsole, this, nullptr);
        return true;
    }
#endif

    return MapScreenBase::keyPressEvent(ks);
}

#ifdef BUILD_DEBUG
#include <sstream>
SWE::StringList commandSplitSpace(std::string str)
{
    std::istringstream is(str);
    SWE::StringList res;

    std::copy(std::istream_iterator<std::string>(is),
            std::istream_iterator<std::string>(), std::back_inserter(res));

    return res;
}

bool AdventurePartScreen::actionDebugCommandLands(void)
{
    std::string line;

    for(auto id : lands_all)
    {
        auto name = Land(id).toString();
        if(line.size() + name.size() + 4 > console->cols() - 2)
        {
            line.append(", ");
            console->contentLinesAppend(line);
            line.assign(name);
        }
        else
        if(line.size())
            line.append(", ").append(name);
        else
            line.assign(name);
    }

    console->contentLinesAppend(line);
    return true;
}

bool AdventurePartScreen::actionDebugCommandLand(const std::string & argv)
{
    StringList res;

    for(auto id : lands_all)
    {
        auto name = Land(id).toString();
        if(0 == name.compare(0, argv.size(), argv))
            res.push_back(name);
    }

    if(res.empty())
    {
        console->contentLinesAppend(SWE::StringFormat("error: unknown land id: %1").arg(argv));
        return false;
    }
    else
    if(1 == res.size())
    {
        debugLand = Land(res.front());
        console->contentLinesAppend(SWE::StringFormat("land %1").arg(res.front()));
    }
    else
    {
        console->contentLinesAppend(SWE::StringFormat("possible values: %1").arg(res.join(", ")));
        return false;
    }

    return true;
}

bool AdventurePartScreen::actionDebugCommandParty(void)
{
    if(debugLand.isTowerWinds())
    {
        for(auto clan : clans_all)
        {
            BattleArmy & army = GameData::getBattleArmy(clan);
            const BattleParty* party = army.findPartyConst(debugLand);

            if(party)
            {
                auto list = console->frs()->splitStringWidth(party->toString(), console->sym2gfx(TermSize(console->cols() - 2, 1)).w);
                for(auto & str : list)
                    console->contentLinesAppend(str);
            }
        }
    }
    else
    {
        const LandInfo & land = GameData::landInfo(debugLand);
        BattleArmy & army = GameData::getBattleArmy(land.clan);
        const BattleParty* party = army.findPartyConst(debugLand);

        if(party)
        {
            auto list = console->frs()->splitStringWidth(party->toString(), console->sym2gfx(TermSize(console->cols() - 2, 1)).w);
            for(auto & str : list)
                console->contentLinesAppend(str);
        }
        else
        {
            console->contentLinesAppend("party not found");
        }
    }

    return true;
}

bool AdventurePartScreen::actionDebugCommand(const std::string & str)
{
    auto words = commandSplitSpace(str);

    if(! words.empty())
    {
        auto & cmd = words.front();

        if(cmd == "help")
        {
            console->contentLinesAppend("lands - show all lands");
            console->contentLinesAppend("land <name> - set/show current land");
            return true;
        }
        else
        if(cmd == "lands")
        {
            console->contentLinesAppend(str);
            return actionDebugCommandLands();
        }
        else
        if(cmd == "land")
        {
            if(2 == words.size())
            {
                return actionDebugCommandLand(words.back());
            }
            else
            {
                console->contentLinesAppend(SWE::StringFormat("current land: %1").arg(debugLand.toString()));
                return true;
            }
        }
        else
        if(cmd == "party")
        {
            return actionDebugCommandParty();
        }
        else
        {
            console->contentLinesAppend(SWE::StringFormat("%1: command not found").arg(cmd));
        }
    }

    return false;
}

/* DebugConsole */
bool DebugConsole::actionCommand(const std::string & cmd)
{
    auto win = dynamic_cast<AdventurePartScreen*>(parent());
    if(win) win->actionDebugCommand(cmd);

    return CommandConsole::actionCommand(cmd);
}
#endif
