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

#include "gametheme.h"
#include "actions.h"
#include "showplayers.h"

bool sortPersonsByWind(const Person & p1, const Person & p2)
{
    return p1.wind < p2.wind;
}

ShowPlayersScreen::ShowPlayersScreen() : JsonWindow("screen_showplayers.json", nullptr)
{
    const Person & selectedPerson = GameData::myPerson();
    persons = GameData::toLocalData(selectedPerson.avatar).toPersons();
    std::sort(persons.begin(), persons.end(), sortPersonsByWind);

    buttonStart = buttons.findIds("but_start");
    buttonCancel = buttons.findIds("but_cancel");

    if(buttonStart) buttonStart->setAction(Action::ButtonStart);
    if(buttonCancel) buttonCancel->setAction(Action::ButtonCancel);

    font = jobject.getString("default:font");
    otherColor = GameTheme::jsonColor(jobject, "color:other");
    selectedColor = GameTheme::jsonColor(jobject, "color:selected");

    setVisible(true);
}

bool ShowPlayersScreen::userEvent(int act, void* data)
{
    switch(act)
    {
        case Action::ButtonStart:	return actionButtonStart();
        case Action::ButtonCancel:	return actionButtonCancel();

	default: break;
    }

    return false;
}

bool ShowPlayersScreen::actionButtonStart(void)
{
    playSound("button");
    setResultCode(Menu::MahjongInitPart);
    setVisible(false);
    return true;
}

bool ShowPlayersScreen::actionButtonCancel(void)
{
    playSound("button");
    setResultCode(Menu::SelectPerson);
    setVisible(false);

    return true;
}

Size ShowPlayersScreen::renderPerson(const Person & user, bool selected, const Point & center)
{
    const AvatarInfo & avatarInfo = GameData::avatarInfo(user.avatar);
    const ClanInfo & clanInfo = GameData::clanInfo(user.clan);
    const WindInfo & windInfo = GameData::windInfo(user.wind);

    Color color = selected ? selectedColor : otherColor;

    Texture sfAvatar = GameTheme::texture(avatarInfo.portrait);
    Texture sfClan = GameTheme::texture(clanInfo.image);

    int posx1 = center.x - sfAvatar.width() - 20;
    int posx2 = center.x + 20;

    renderTexture(sfAvatar, Point(posx1, center.y));
    renderTexture(sfClan, Point(posx2, center.y));

    Texture sfWind = GameTheme::texture(windInfo.image);

    posx1 -= sfWind.width() + 20;
    posx2 += sfClan.width() + 20;

    renderTexture(sfWind, Point(posx1, center.y + 30));
    renderTexture(sfWind, Point(posx2, center.y + 30));

    posx1 -= 20;
    posx2 += sfWind.width() + 20;

    const FontRender & frs = GameTheme::fontRender(font);

    renderText(frs, avatarInfo.name, color, Point(posx1, center.y + 10), AlignRight);
    renderText(frs, (GameData::usesAI(user) ? "AI" : "Human"), color,
               Point(posx1, center.y + 70), AlignRight);
    renderText(frs, clanInfo.name, color, Point(posx2, center.y + 10));
    renderText(frs, windInfo.name, color, Point(posx2, center.y + 70));

    return sfAvatar.size();
}

void ShowPlayersScreen::renderWindow(void)
{
    JsonWindow::renderWindow();

    const Person & selectedPerson = GameData::myPerson();
    Point center = Point(width() / 2, 40);

    for(auto & user : persons)
    {
	auto sz = renderPerson(user, GameData::isLocallyControlled(user), center);
	center.y += sz.h + 20;
    }
}
