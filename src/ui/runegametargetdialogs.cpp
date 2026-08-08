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

#include "gametheme.h"
#include "dialogs.h"
#include "adventureuievents.h"
#include "runegametargetdialogs.h"

ShowMapDialog::ShowMapDialog(const LocalData & data, Window & win) : MapScreenBase(data, & win)
{
    setVisible(true);
    buttons.setVisible(false);

    JsonButton* button = buttons.findIds("but_close");

    if(button)
    {
        button->setAction(MapScreenClose);
        button->setHotKey(Key::ESCAPE);
        button->setVisible(true);
    }
}

void ShowMapDialog::renderLabel(void)
{
    const FontRender & frs = GameTheme::fontRender(defaultFont);
    renderText(frs, _("View Map"), defaultColor, viewMapPos, AlignCenter);
}

ShowSummonCreatureDialog::ShowSummonCreatureDialog(const LocalData & data, const Creature &, Window & win)
    : MapScreenBase(data, & win)
{
    selectedLand = Land(Land::TowerOf4Winds);

    setVisible(true);
    buttons.setVisible(false);

    JsonButton* button = buttons.findIds("but_cancel");
    if(button)
    {
        button->setAction(MapScreenClose);
        button->setHotKey(Key::ESCAPE);
        button->setVisible(true);
    }

    for(auto & id : lands_all)
    {
        const LandInfo & landInfo = GameData::landInfo(id);

        if(landAllowJoin(landInfo, ld.myPlayer()))
        {
            animationMapObjects.emplace_back(jobject, "animation:place");

            auto & anim = animationMapObjects.back();
            anim.setPosition(landInfo.center - anim.spriteSize() / 2);
            anim.setEnabled(true);
        }
    }
}

bool ShowSummonCreatureDialog::landAllowJoin(const LandInfo & info, const LocalPlayer & player)
{
    if(info.stat.power)
    {
        if(info.id.isTowerWinds())
            return true;
        else if(info.clan == player.clan)
        {
            const BattleParty* party = player.army.findPartyConst(info.id);
            return party ? party->canJoin() : true;
        }
    }

    return false;
}

bool ShowSummonCreatureDialog::userEvent(int event, void* data)
{
    if(MapScreenBase::userEvent(event, data))
    {
        if(event == LandPolygonClickLeft)
        {
            auto landInfo = static_cast<LandInfo*>(data);

            if(landInfo && landAllowJoin(*landInfo, ld.myPlayer()))
            {
                setResultCode(selectedLand());
                setVisible(false);
            }
        }

        return true;
    }

    return false;
}

const Land & ShowSummonCreatureDialog::land(void) const
{
    return selectedLand;
}

void ShowSummonCreatureDialog::renderLabel(void)
{
    const FontRender & frs = GameTheme::fontRender(defaultFont);
    renderText(frs, _("Summoning"), defaultColor, viewMapPos, AlignCenter);
}

ShowCastSpellDialog::ShowCastSpellDialog(const LocalData & data, const Spell & sp, Window & win)
    : MapScreenBase(data, & win), spell(sp), targetUnit(-1)
{
    setVisible(true);
    buttons.setVisible(false);

    JsonButton* button = buttons.findIds("but_cancel");
    if(button)
    {
        button->setAction(MapScreenClose);
        button->setHotKey(Key::ESCAPE);
        button->setVisible(true);
    }
}

bool ShowCastSpellDialog::userEvent(int act, void* data)
{
    if(MapScreenBase::userEvent(act, data))
    {
        const LocalPlayer & player = ld.myPlayer();
        const SpellInfo & info = GameData::spellInfo(spell);

        if(act == LandPolygonClickLeft)
        {
            auto landInfo = static_cast<LandInfo*>(data);
            if(info.target() == SpellTarget::Land && landInfo)
            {
                std::string msg = StringFormat(_("Apply spell: %1, on land: %2?")).arg(info.name).arg(landInfo->name);
                if(MessageBox(_("Error"), msg, *this, true).exec())
                {
                    setResultCode(1);
                    setVisible(false);
                }
            }
            else if(spell() == Spell::Teleport && landInfo && 0 < targetUnit)
            {
                const BattleParty* party = player.army.findPartyConst(landInfo->id);
                const bool friendlyLand = landInfo->id.isTowerWinds() || landInfo->clan == player.clan;
                const bool canJoin = ! party || party->canJoin();

                if(friendlyLand && canJoin && landInfo->id != targetSource)
                {
                    std::string msg = StringFormat(_("Teleport selected creature to %1?")).arg(landInfo->name);
                    if(MessageBox(_("Teleport"), msg, *this, true).exec())
                    {
                        setResultCode(3);
                        setVisible(false);
                    }
                }
                else
                {
                    MessageBox(_("Error"), _("Select another friendly territory with a free party slot."), *this, false).exec();
                }
            }
        }

        if(act == CreatureIconClickLeft)
        {
            if(info.target() != SpellTarget::Land && selectedCreature.canReceiveSpell(spell) &&
               (((info.target() & SpellTarget::Friendly) &&
                 GameData::allied(player.clan, selectedCreature.clan())) ||
                ((info.target() & SpellTarget::Enemy) &&
                 !GameData::allied(player.clan, selectedCreature.clan()))))
            {
                if(spell() == Spell::Teleport)
                {
                    targetUnit = selectedCreature.battleUnit();
                    targetSource = selectedLand;
                }
                else
                {
                    setResultCode(2);
                    setVisible(false);
                }
            }
            else
            {
                std::string msg = StringFormat(_("Can'not apply spell: %1, incorrect target: %2")).arg(info.name).arg(info.target.toString());
                MessageBox(_("Error"), msg, *this, false).exec();
            }
        }

        return true;
    }

    return false;
}

const Land & ShowCastSpellDialog::land(void) const
{
    return selectedLand;
}

int ShowCastSpellDialog::unit(void) const
{
    return 0 < targetUnit ? targetUnit : selectedCreature.battleUnit();
}

void ShowCastSpellDialog::renderLabel(void)
{
    const FontRender & frs = GameTheme::fontRender(defaultFont);
    renderText(frs, GameData::spellInfo(spell).name, defaultColor, viewMapPos, AlignCenter);
}
