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

#include "settings.h"
#include "gamedata.h"
#include "gametheme.h"
#include "matchtopology.h"
#include "matchpresentation.h"
#include "dialogs.h"
#include "adventureuievents.h"
#include "battlechoicedialog.h"
#include "actions.h"
#include "aibattle.h"
#include "aiprofile.h"
#include "adventurepart.h"
#include "matchsession.h"
#include "multiplayerlobby.h"
#include "crashreport.h"
#include "swe/swe_music.h"
#ifdef BUILD_DEBUG
#include "developertools.h"
#endif

LandPolygon::LandPolygon(const LandInfo & info, const JsonObject & jo, Window & win) : WindowToolTipArea(& win), landInfo(info), owner(info.clan), poly(info.points)
{
    Rect area = poly.around();

    setSize(area);
    setPosition(area);

    combatFightTexture = GameTheme::jsonSprite(jo, "sprite:combat_fight");
    combatFightStatus = false;

    const JsonToolTip & tips = GameTheme::jsonToolTipInfo();
    renderToolTip(info.name, GameTheme::fontRender(tips.font), tips.fncolor, tips.bgcolor, tips.rtcolor);

    resetState(FlagModality);
    setVisible(false);

    // add animations
    if(! info.id.isTowerWinds())
    {
	// power state
	if(info.stat.power)
	{
	    animationPower = SpritesAnimation(jo, "animation:town");
	    animationPower.setPosition(info.center - position() - animationPower.spriteSize() / 2);
	    animationPower.setEnabled(true);
	}

	// party flag
	animationFlag.loop = true;
	animationFlag.delay = 400;
	userEvent(LandPolygonFlagAnimationReInit, const_cast<LandInfo*>(& landInfo));

    }
}

bool LandPolygon::isAreaPoint(const Point & pos) const
{
    return Window::isAreaPoint(pos) ? poly & pos : false;
}

void LandPolygon::animationsDisabled(bool f)
{
    animationPower.setEnabled(f);
    animationFlag.setEnabled(f);
}

void LandPolygon::tickEvent(u32 ms)
{
    bool render = false;

    if(animationPower.isEnabled() && animationPower.next(ms)) render = true;
    if(animationFlag.isEnabled() && animationFlag.next(ms)) render = true;

    if(render) renderWindow();
}

bool LandPolygon::userEvent(int act, void* data)
{
    if(act == LandPolygonCombatStatusReset)
    {
	// reset combat status
	if(!data || data == & landInfo)
	{
	    combatFightStatus = false;
	    renderWindow();
	    return true;
	}
    }

    if(act == LandPolygonCombatStatus)
    {
	// set combat status
	if(data && data == & landInfo)
	{
	    combatFightStatus = true;
	    renderWindow();
	    return true;
	}
    }

    if(act == LandPolygonFlagAnimationReInit &&
	! landInfo.id.isTowerWinds() && (!data || data == & landInfo))
    {
	auto win = static_cast<const MapScreenBase*>(parent());
	if(! win) return false;

	const ClanInfo & clanInfo = GameData::clanInfo(landInfo.clan);
	const RemotePlayer & clanOwner = win->ld.playerOfClan(landInfo.clan);

	const Sprite & sprite1 = GameTheme::texture(clanInfo.townflag1);
	const Sprite & sprite2 = GameTheme::texture(clanInfo.townflag2);

	animationFlag.sprites.clear();
	const BattleParty* party = clanOwner.army.findPartyConst(landInfo.id);

	if(party && !party->isEmpty())
	{
	    animationFlag.sprites << sprite1 << sprite2;
	    animationFlag.sprites[0].setPosition(landInfo.center - position() - Size(sprite1.width() / 2, sprite1.height()) + Size(0, 15));
	    animationFlag.sprites[1].setPosition(landInfo.center - position() - Size(sprite2.width() / 2, sprite2.height()) + Size(0, 15));
	    animationFlag.setEnabled(true);
	}

	// target event: fixed owner
	if(data == & landInfo)
	    owner = landInfo.clan;

	renderWindow();
	return true;
    }

    return false;
}

bool LandPolygon::mousePressEvent(const ButtonEvent & be)
{
    auto win = static_cast<MapScreenBase*>(parent());

    if(be.isButtonLeft() && win && win->isAllowMoveFlag(landInfo))
    {
	// Start immediately so a quick trackpad drag cannot outrun the queued event.
	return win->userEvent(AdventureTurnMoveStart, const_cast<LandInfo*>(& landInfo));
    }

    return false;
}

void LandPolygon::mouseFocusEvent(void)
{
    pushEventAction(LandPolygonFocus, parent(), const_cast<LandInfo*>(& landInfo));
}

bool LandPolygon::mouseClickEvent(const ButtonsEvent & be)
{
    if(be.isButtonLeft())
	pushEventAction(LandPolygonClickLeft, parent(), const_cast<LandInfo*>(& landInfo));
    else
    if(be.isButtonRight())
	pushEventAction(LandPolygonClickRight, parent(), const_cast<LandInfo*>(& landInfo));

    return true;
}

void LandPolygon::renderWindow(void)
{
    auto win = static_cast<const MapScreenBase*>(parent());

    if(win)
    {
	if(! landInfo.id.isTowerWinds())
	{

            if(MatchPresentation::duel() || MatchPresentation::teams())
            {
                const Points & outline = poly.boundaryVertices();
                const Color color = MatchPresentation::sideColor(activeMatchTopology().teamForClan(owner()));
                for(auto it = outline.begin(); it != outline.end(); ++it)
                {
                    auto next = std::next(it);
                    if(next == outline.end()) next = outline.begin();
                    renderLine(color, *it - position(), *next - position());
                }
            }
	    const ClanInfo & clanInfo = GameData::clanInfo(owner);
	    const Texture & textureTown = GameTheme::texture(clanInfo.town);
	    const Size offy(0, 15);

	    // render animation power
	    if(animationPower.isEnabled()) animationPower.render(*this);
	    // render town sprite
	    renderTexture(textureTown, landInfo.center - position() - Size(textureTown.width() / 2, textureTown.height()) + offy);
	    // render combat status
	    if(combatFightStatus)
		renderTexture(combatFightTexture, landInfo.center - position() - Size(combatFightTexture.width() / 2, combatFightTexture.height()) + offy);
	    else
	    // render animation flag
	    if(animationFlag.isEnabled()) animationFlag.render(*this);

	    const AdventureHints::DestinationCue cue = win->mapCue(landInfo);
	    if(cue != AdventureHints::DestinationCue::None)
	    {
		const Points & outline = poly.boundaryVertices();
		for(auto it = outline.begin(); it != outline.end(); ++it)
		{
		    auto next = std::next(it);
		    if(next == outline.end()) next = outline.begin();
		    renderLine(win->mapCueColor(cue), *it - position(), *next - position());
		}
	    }
	}
	else
	// TowerOf4Winds: all army flags render
	{
	    std::vector<Texture> flags;
	    int width = 0;

	    for(const auto & other : win->ld.players)
	    {
                if(!other.avatar.isValid()) continue;
		const BattleParty* party = other.army.findPartyConst(landInfo.id);

		if(party && !party->isEmpty())
		{
		    flags.push_back(GameTheme::texture(GameData::clanInfo(other.clan).flag2));
		    width += flags.back().width();
		}
	    }

	    int offx = 0;
	    int offy = 15;

	    for(auto & flg : flags)
	    {
		renderTexture(flg, landInfo.center - position() - Size(width / 2, 0) + Size(offx, offy));
		offx += flg.width();
	    }
	}
    }

    if(isFocused())
    {
	for(auto it = poly.begin(); it != poly.end(); ++it)
	    renderPoint(Color::Red, *it - position());
    }
}

PartyCreaturesBar::PartyCreaturesBar(Window & win) : Window(&win), clanIconOffset(0, 0)
{
    resetState(FlagModality);
    setVisible(false);
}

BattleParty* PartyCreaturesBar::currentParty(void) const
{
    if(!clan.isValid() || !land.isValid()) return nullptr;
    return GameData::getBattleArmy(clan).findParty(land);
}

bool PartyCreaturesBar::mouseClickEvent(const ButtonsEvent & be)
{
    const int offset = 5;
    const Texture & icon = GameTheme::texture(GameData::clanInfo(clan).button);
    BattleParty* party = currentParty();

    // click clan icon
    if(be.isClick(Rect(clanIconOffset, icon.size())))
    {
	pushEventAction(be.isButtonLeft() ? ClanIconClickLeft : ClanIconClickRight, parent(), party);
	return true;
    }

    if(party)
    {
	const BattleCreature* bcr = nullptr;
	// click bcr1 icon
	if(be.isClick(Rect(Point(icon.width() + offset, 0), icon.size())))
	{
	    bcr = party->index(0);
	}
	else
	// click bcr2 icon
	if(be.isClick(Rect(Point(2 * (icon.width() + offset), 0), icon.size())))
	{
	    bcr = party->index(1);
	}
	else
	// click bcr3 icon
	if(be.isClick(Rect(Point(2 * (icon.width() + offset), 0), icon.size())))
	{
	    bcr = party->index(2);
	}

	if(bcr && bcr->isValid())
	{
	    pushEventAction(be.isButtonLeft() ? CreatureIconClickLeft : CreatureIconClickRight, parent(), const_cast<BattleCreature*>(bcr));

	    auto win = static_cast<const MapScreenBase*>(parent());
	    if(win && win->isMyClan(clan))
		pushEventAction(AdventureTurnCreatureSelect, parent(), const_cast<BattleCreature*>(bcr));

	    return true;
	}
    }

    return false;
}

void PartyCreaturesBar::renderWindow(void)
{
    const int offset = 5;
    const BattleParty* party = currentParty();

    // clan icon
    if(clan.isValid())
    {
	const Texture & textureClan = GameTheme::texture(GameData::clanInfo(clan).button);
	renderTexture(textureClan, clanIconOffset);
    }

    if(party)
    {
	const Texture & textureClan = GameTheme::texture(GameData::clanInfo(clan).button);
	Point dst = Size(textureClan.width() + offset, 0);

	for(int it = 0; it < 3; ++it)
	{
	    const BattleCreature* bcr = party->index(it);
	    if(bcr && bcr->isValid())
	    {
		const CreatureInfo & creatureInfo = GameData::creatureInfo(*bcr);
		Texture textureCreature = GameTheme::texture(creatureInfo.image2);
		renderTexture(textureCreature, dst);

		// can selected only my party
		auto win = static_cast<const MapScreenBase*>(parent());
		if(bcr->isSelected() && win && win->isMyClan(clan))
		    renderRect(Color::Lime, Rect(dst, Size(58, 58)));

		dst.x += textureCreature.width() + offset;
	    }
	    else
	    {
		dst.x += textureClan.width() + offset;
	    }
	}
    }
}

void PartyCreaturesBar::setParty(const Clan & cl, const Land & value)
{
    clan = cl;
    land = value;
    renderWindow();
    setVisible(true);
}

AffectedSpellsIcon::AffectedSpellsIcon(const JsonObject & jo) : bcrUid(-1)
{
    leftArea = GameTheme::jsonRect(jo, "area:spell_affected1");
    rightArea = GameTheme::jsonRect(jo, "area:spell_affected2");
}

void AffectedSpellsIcon::updateSpells(const BattleCreature & bcr, Window & win)
{
    icons.clear();
    bcrUid = bcr.battleUnit();

    Point posr = rightArea.toPoint();
    Point posl = leftArea.toPoint();

    for(auto & as : bcr.affectedSpells())
    {
	const SpellInfo & si = GameData::spellInfo(as);

	Point & pos = si.target() == SpellTarget::Friendly ? posl : posr;
	const Rect & art = si.target() == SpellTarget::Friendly ? leftArea : rightArea;

	icons.emplace_back(si, pos, win);
	SpellIcon & icon = icons.back();

	// fixed centered
	pos.x = art.x + (art.w - icon.width()) / 2;
	icon.setPosition(pos);

	pos.y += icon.height() + 4;
    }
}

void AffectedSpellsIcon::setVisible(bool f)
{
    for(auto & win : icons)
	win.setVisible(f);
}

MapScreenBase::MapScreenBase(const LocalData & data, Window* win) : JsonWindow("screen_adventurepart.json", win), ld(data),
    selectedLand(Land(Land::TowerOf4Winds)), affectedSpells(jobject), bar1(*this), bar2(*this)
{
    townTowerWindsTexture = GameTheme::jsonSprite(jobject, "sprite:town_tower_winds");
    townTowerWindsPos = GameData::landInfo(Land::TowerOf4Winds).center - townTowerWindsTexture.size() / 2;

    defaultFont = jobject.getString("default:font");
    defaultColor = jobject.getString("default:color");

    statChangedUpColor = jobject.getString("color:stat_up");
    statChangedDownColor = jobject.getString("color:stat_down");
    cueMoveColor = jobject.getString("color:cue_move");
    cueAttackColor = jobject.getString("color:cue_attack");
    cueClaimColor = jobject.getString("color:cue_claim");

    Point topClanPos = GameTheme::jsonPoint(jobject, "offset:topclan");
    Point botClanPos = GameTheme::jsonPoint(jobject, "offset:botclan");
    Point clanIconOffset = GameTheme::jsonPoint(jobject, "offset:clanicon");
    landNamePos = GameTheme::jsonPoint(jobject, "offset:nameland");
    info1NamePos = GameTheme::jsonPoint(jobject, "offset:info1");
    info2NamePos = GameTheme::jsonPoint(jobject, "offset:info2");
    viewMapPos = GameTheme::jsonPoint(jobject, "offset:viewmap");
    selectedIconPos = GameTheme::jsonRect(jobject, "area:icon_portrait");

    const JsonObject* forecast = jobject.getObject("battle:forecast");
    if(forecast)
    {
        forecastTitle = GameTheme::jsonTextInfo(*forecast, "textinfo:title");
        forecastCapture = GameTheme::jsonTextInfo(*forecast, "textinfo:capture");
        forecastSurvival = GameTheme::jsonTextInfo(*forecast, "textinfo:survival");
        forecastForces = GameTheme::jsonTextInfo(*forecast, "textinfo:forces");
        forecastScope = GameTheme::jsonTextInfo(*forecast, "textinfo:scope");
    }
    cueLegend = GameTheme::jsonTextInfo(jobject, "textinfo:cue_legend");

    // const Texture & tmp = GameTheme::texture(GameData::clanInfo(Clan::Red).button);

    bar1.setPosition(topClanPos);
    bar2.setPosition(botClanPos);
    bar1.setClanIconOffset(clanIconOffset);
    bar2.setClanIconOffset(clanIconOffset);

    bar1.setSize(Size(238, 56));
    bar2.setSize(Size(238, 56));

    spriteLandStat1 = GameTheme::jsonSprite(jobject, "sprite:stat11");
    spriteLandStat2 = GameTheme::jsonSprite(jobject, "sprite:stat12");
    spriteLandStat3 = GameTheme::jsonSprite(jobject, "sprite:stat13");
    spriteLandStat4 = GameTheme::jsonSprite(jobject, "sprite:stat14");
    spriteLandStat5 = GameTheme::jsonSprite(jobject, "sprite:stat15");
    spriteLandStat6 = GameTheme::jsonSprite(jobject, "sprite:stat16");

    spriteCreatureStat1 = GameTheme::jsonSprite(jobject, "sprite:stat21");
    spriteCreatureStat2 = GameTheme::jsonSprite(jobject, "sprite:stat22");
    spriteCreatureStat3 = GameTheme::jsonSprite(jobject, "sprite:stat23");
    spriteCreatureStat4 = GameTheme::jsonSprite(jobject, "sprite:stat24");
    spriteCreatureStat5 = GameTheme::jsonSprite(jobject, "sprite:stat25");
    spriteCreatureStat6 = GameTheme::jsonSprite(jobject, "sprite:stat26");

    for(auto & id : lands_all)
    {
	const LandInfo & landInfo = GameData::landInfo(id);
	lands.push_back(new LandPolygon(landInfo, jobject, *this));
    }

    for(int ii = 1; ii < 10; ++ii)
    {
	std::string key = StringFormat("animation:maps%1").arg(ii);

	if(jobject.getObject(key))
	{
	    animationMapObjects.push_back(SpritesAnimation(jobject, key));
	    animationMapObjects.back().setEnabled(true);
	}
    }

    selectedLand.reset();
    selectedCreature.reset();
    selectedClan = GameData::myPerson().clan;
}

bool MapScreenBase::isMyClan(const Clan & clan) const
{
    return clan == ld.myPlayer().clan;
}

bool MapScreenBase::isAllowMoveFlag(const LandInfo & landInfo) const
{
    const LocalPlayer & player = ld.myPlayer();

    return (landInfo.id.isTowerWinds() || landInfo.clan == player.clan) && landInfo.id == selectedLand &&
        0 < player.army.partySelected(landInfo.id).size();
}

void MapScreenBase::animationsDisabled(bool f)
{
    for(auto & obj : animationMapObjects)
	obj.setEnabled(! f);

    for(auto & land : lands)
	land->animationsDisabled(f);
}

void MapScreenBase::tickEvent(u32 ms)
{
    for(auto & obj : animationMapObjects)
    {
	if(obj.isEnabled())
	{
	    bool res = obj.next(ms);
	    if(res) setDirty(true);
	}
    }
}

const Color & MapScreenBase::getBaseStatColor(int base, int current) const
{
    if(base < current) return statChangedUpColor;
    else
    if(base > current) return statChangedDownColor;

    return defaultColor;
}

void MapScreenBase::renderLandInfo(const Land & land)
{
    const LandInfo & landInfo = GameData::landInfo(land);
    const FontRender & frs = GameTheme::fontRender(defaultFont);

    // name
    renderText(frs, landInfo.name, defaultColor, landNamePos, AlignCenter);

    // stats
    renderTexture(spriteLandStat1);
    renderText(frs, String::number(landInfo.stat.attack), defaultColor, spriteLandStat1.position() + Size(2 + spriteLandStat1.width(), -4));

    renderTexture(spriteLandStat2);
    renderText(frs, String::number(landInfo.stat.ranger), defaultColor, spriteLandStat2.position() + Size(2 + spriteLandStat2.width(), -4));

    renderTexture(spriteLandStat3);
    renderText(frs, String::number(landInfo.stat.defense), defaultColor, spriteLandStat3.position() + Size(2 + spriteLandStat3.width(), -4));

    renderTexture(spriteLandStat4);
    renderText(frs, String::number(landInfo.stat.loyalty), defaultColor, spriteLandStat4.position() + Size(2 + spriteLandStat4.width(), -4));

    renderTexture(spriteLandStat5);
    renderText(frs, landInfo.stat.power ? "Y" : "N", defaultColor, spriteLandStat5.position() + Size(2 + spriteLandStat5.width(), -4));

    renderTexture(spriteLandStat6);
    renderText(frs, String::number(landInfo.stat.point), defaultColor, spriteLandStat6.position() + Size(2 + spriteLandStat6.width(), -4));
}

void MapScreenBase::clearBattleForecast(void)
{
    forecastTarget.reset();
    forecastPreview = AdventureHints::BattlePreview();
}

void MapScreenBase::updateBattleForecast(const Land & origin, const Land & target)
{
    clearBattleForecast();
    forecastPreview = AdventureHints::battlePreview(ld, origin, target, GameData::aiDifficulty());
    if(forecastPreview.available) forecastTarget = target;
}

void MapScreenBase::renderBattleForecast(void)
{
    if(!forecastTarget.isValid() || !forecastPreview.available) return;

    renderTextInfo(forecastTitle, forecastPreview.showPercentages ?
        _("Battle Forecast") : _("Battle Intel"));
    if(forecastPreview.showPercentages)
    {
        renderTextInfo(forecastCapture,
                       StringFormat(_("Capture: %1%")).arg(forecastPreview.captureChance));
        renderTextInfo(forecastSurvival,
                       StringFormat(_("Attackers remain: %1%")).arg(forecastPreview.attackerSurvival));
    }
    else
    {
        renderTextInfo(forecastCapture, _("Outcome hidden at this difficulty"));
    }
    renderTextInfo(forecastForces,
			   StringFormat(_("Known: %1 atk. / %2 guards"))
			       .arg(forecastPreview.attackerCount)
			       .arg(forecastPreview.visibleDefenderCount));
    renderTextInfo(forecastScope,
			   StringFormat(_("Land: %1 - visible forces only"))
			       .arg(forecastPreview.townLoyalty));
}

void MapScreenBase::renderCreatureInfo(const BattleCreature & battle)
{
    const CreatureInfo & creatureInfo = GameData::creatureInfo(battle);
    const FontRender & frs = GameTheme::fontRender(defaultFont);

    // creature portrait
    Texture texturePortrait = GameTheme::texture(creatureInfo.image1);
    renderTexture(texturePortrait, selectedIconPos);

    // creature name
    renderText(frs, creatureInfo.name, defaultColor, info1NamePos, AlignCenter);

    // creature stats
    renderTexture(spriteCreatureStat1);
    renderText(frs, String::number(battle.attack()), getBaseStatColor(battle.baseAttack(), battle.attack()),
		    spriteCreatureStat1.position() + Size(2 + spriteCreatureStat1.width(), -4));

    renderTexture(spriteCreatureStat2);
    renderText(frs, String::number(battle.ranger()), getBaseStatColor(battle.baseRanger(), battle.ranger()),
		    spriteCreatureStat2.position() + Size(2 + spriteCreatureStat2.width(), -4));

    renderTexture(spriteCreatureStat3);
    renderText(frs, String::number(battle.defense()), getBaseStatColor(battle.baseDefense(), battle.defense()),
		    spriteCreatureStat3.position() + Size(2 + spriteCreatureStat3.width(), -4));

    renderTexture(spriteCreatureStat4);
    renderText(frs, String::number(battle.baseLoyalty()), defaultColor, spriteCreatureStat4.position() + Size(2 + spriteCreatureStat4.width(), -4));

    // cur loyalty
    renderTexture(spriteCreatureStat5);
    renderText(frs, String::number(battle.loyalty()), getBaseStatColor(battle.baseLoyalty(), battle.loyalty()), spriteCreatureStat5.position() + Size(2 + spriteCreatureStat5.width(), -4));

    // cur move
    renderTexture(spriteCreatureStat6);
    renderText(frs, String::number(battle.freeMovePoint()), defaultColor, spriteCreatureStat6.position() + Size(2 + spriteCreatureStat6.width(), -4));
}

void MapScreenBase::renderClanAvatarInfo(const RemotePlayer & player)
{
    const ClanInfo & clanInfo = GameData::clanInfo(player.clan);
    const AvatarInfo & avatarInfo = GameData::avatarInfo(player.avatar);
    const FontRender & frs = GameTheme::fontRender(defaultFont);

    // avatar portrait
    Texture texturePortrait = GameTheme::texture(avatarInfo.portrait);
    renderTexture(texturePortrait, selectedIconPos);

    // avatar name
    renderText(frs, avatarInfo.name, defaultColor, info1NamePos, AlignCenter);

    // clan name
    Rect pos = renderText(frs, clanInfo.name, defaultColor, info2NamePos, AlignCenter);
    Texture clanFlag = GameTheme::texture(clanInfo.flag1);
    renderTexture(clanFlag, pos.toPoint() - Size(clanFlag.width() + 5, 5));
    renderTexture(clanFlag, pos.toPoint() + Size(pos.w + 5, -5));
}

void MapScreenBase::renderWindow(void)
{
    JsonWindow::renderWindow();

    // render map objects animation
    for(auto & spritesAnim : animationMapObjects)
	if(spritesAnim.isEnabled()) spritesAnim.render(*this);

    renderLabel();

    // town tower winds image
    renderTexture(townTowerWindsTexture, townTowerWindsPos);

    if(selectedCreature.isValid() && !forecastTarget.isValid())
	renderCreatureInfo(selectedCreature);

    if(selectedClan.isValid() && !forecastTarget.isValid())
	renderClanAvatarInfo(ld.playerOfClan(selectedClan));

    const Land infoLand = forecastTarget.isValid() ? forecastTarget : selectedLand;
    if(infoLand.isValid()) renderLandInfo(infoLand);
    renderBattleForecast();
    if(MatchPresentation::duel() || MatchPresentation::teams())
    {
        using namespace MatchPresentation;
        card(*this, Rect(136, 6, 520, 27), Color(107, 99, 70));
        text(*this, modeName(), Point(396, 10), 135, ink(), 16);
        for(int team = 0; team < 2; ++team)
        {
            std::string name = teamName(team);
            if(duel())
                for(const auto & player : ld.players)
                    if(player.avatar.isValid() && side(player) == team) name = player.name();
            text(*this, name, Point(team == 0 ? 230 : 561, 10), 177, sideColor(team), 16);
        }
    }
}

bool MapScreenBase::userEvent(int event, void* data)
{
    switch(event)
    {
	// click land polygon
	case MapScreenSelectLand:
        case LandPolygonClickLeft:
        case LandPolygonClickRight:
	    if(data)
	    {
		auto landInfo = static_cast<LandInfo*>(data);
		if(selectedLand != landInfo->id || event == MapScreenSelectLand)
		{
		    if(MapScreenSelectLand != event)
			playSound("bselect");
		    else
		    {
			if(landInfo->id != selectedLand)
			    DEBUG("selected land: " << landInfo->name);
		    }

		    selectedLand = landInfo->id;
		    selectedCreature.reset();
		    selectedClan.reset();
		    affectedSpells.setVisible(false);

		    if(landInfo->id.isTowerWinds())
		    {
			Clan clan1 = Clan(GameData::myPerson().clan);
			Clan clan2 = clan1.next();

			bar1.setParty(clan1, selectedLand);
			bar2.setParty(clan2, selectedLand);

			selectedClan = GameData::myPerson().clan;
		    }
		    else
		    {
			selectedClan = landInfo->clan;

			bar1.setParty(selectedClan, selectedLand);
			bar2.reset();
		    }
		    renderWindow();
		}
		return true;
	    }
	    break;

	// click clan icon
	case ClanIconClickLeft:
	case ClanIconClickRight:
	    clearBattleForecast();
	    if(selectedLand.isTowerWinds())
	    {
		Clan clan1, clan2;
		selectedCreature.reset();
		affectedSpells.setVisible(false);

		// click bar1
		if(bar1.area() & Display::mouseCursorPosition())
		{
		    selectedClan = bar1.currentClan();
		    clan1 = selectedClan.prev();
		    clan2 = selectedClan;
		}
		else
		// click bar2
		{
		    selectedClan = bar2.currentClan();
		    clan1 = selectedClan;
		    clan2 = selectedClan.next();
		}

		bar1.setParty(clan1, selectedLand);
		bar2.setParty(clan2, selectedLand);

		renderWindow();
		return true;
	    }
	    else
	    // other lands
	    {
		auto party = static_cast<BattleParty*>(data);
		if(party && ! selectedClan.isValid())
		    selectedClan = party->clan();

		selectedCreature.reset();
		affectedSpells.setVisible(false);

		renderWindow();
		return true;
	    }
	    break;

	// click creatures icon
	case CreatureIconClickLeft:
	case CreatureIconClickRight:
	    clearBattleForecast();
	    if(data)
	    {
		auto bcr = static_cast<BattleCreature*>(data);
		selectedCreature = *bcr;
		selectedClan.reset();
		affectedSpells.updateSpells(selectedCreature, *this);
		affectedSpells.setVisible(true);
		renderWindow();
		return true;
	    }
	    break;

	case MapScreenClose:
	    setVisible(false);
	    return true;

	case AdventureTurnPlayer:
	    clearBattleForecast();
	    if(data)
	    {
		auto player = static_cast<RemotePlayer*>(data);
		selectedClan = player->clan;
		selectedLand.reset();
		selectedCreature.reset();
		affectedSpells.setVisible(false);
		renderWindow();
		return true;
	    }
	    break;

	default: break;
    }

    return false;
}

bool MapScreenBase::isAllowLandClaim(const LandInfo & info) const
{
    return isAdventureMode() && ld.yourTurn() && AdventureHints::canClaimObserved(ld, info.id);
}

AdventureHints::DestinationCue MapScreenBase::mapCue(const LandInfo & info) const
{
    return isAllowLandClaim(info) ? AdventureHints::DestinationCue::Claim :
        AdventureHints::DestinationCue::None;
}

const Color & MapScreenBase::mapCueColor(AdventureHints::DestinationCue cue) const
{
    switch(cue)
    {
        case AdventureHints::DestinationCue::Move: return cueMoveColor;
        case AdventureHints::DestinationCue::Attack: return cueAttackColor;
        case AdventureHints::DestinationCue::Claim: return cueClaimColor;
        default: return defaultColor;
    }
}

/* MoveFlagWindow */
MoveFlagWindow::MoveFlagWindow(const Clan & clan, Window & win) : Window(& win)
{
    flagTexture = GameTheme::texture(GameData::clanInfo(clan).flag1);
    setSize(flagTexture.size());

    setState(FlagLayoutForeground);
    setState(FlagMouseTracking);
    resetState(FlagModality);
}

void MoveFlagWindow::renderWindow(void)
{
    renderTexture(flagTexture, Point(0, 0));
}

void MoveFlagWindow::mouseTrackingEvent(const Point & pos, u32 buttons)
{
    if(buttons & ButtonLeft)
    {
	setPosition(pos - flagTexture.size() / 2);
    }
}

void MoveFlagWindow::setVisible(bool f)
{
    if(f)
	setPosition(Display::mouseCursorPosition() - flagTexture.size() / 2);

    Window::setVisible(f);
}

void MoveFlagWindow::setClan(const Clan & clan)
{
    flagTexture = GameTheme::texture(GameData::clanInfo(clan).flag1);
    setSize(flagTexture.size());
}

/* AdventurePartScreen */
AdventurePartScreen::AdventurePartScreen(const Avatar & ava) : MapScreenBase(GameData::toLocalData(ava), nullptr), myAvatar(ava), allowTickEvent(true),
    moveFlag(ld.myPlayer().clan, *this), buttonOrder(nullptr), buttonDone(nullptr), buttonUndo(nullptr), buttonDismiss(nullptr)
{
    history.reserve(6);
    LocalPlayer & player = ld.myPlayer();

    player.army.setAllSelected();

    setVisible(true);
    moveFlag.setVisible(false);
    buttons.setVisible(false);

    delayCombatResult = Settings::presentationDelay(jobject.getInteger("delay:combatresult", 600));
    JsonButton* button = nullptr;

    buttonOrder = buttons.findIds("but_order");
    if(buttonOrder)
    {
	buttonOrder->setVisible(true);
	buttonOrder->setAction(Action::ButtonOrder);
	buttonOrder->setDisabled(true);
    }

    button = buttons.findIds("but_info");
    if(button)
    {
	button->setVisible(true);
	button->setAction(Action::ButtonInfo);
    }

    button = buttons.findIds("but_menu");
    if(button)
    {
	button->setVisible(true);
	button->setAction(Action::ButtonMenu);
    }

    buttonDone = buttons.findIds("but_done");
    if(buttonDone)
    {
	buttonDone->setVisible(true);
	buttonDone->setAction(Action::ButtonDone);
	buttonDone->setDisabled(true);
    }

    buttonUndo = buttons.findIds("but_undo");
    if(buttonUndo)
    {
	buttonUndo->setVisible(true);
	buttonUndo->setAction(Action::ButtonUndo);
	buttonUndo->setDisabled(true);
    }

    buttonDismiss = buttons.findIds("but_dismiss");
    if(buttonDismiss)
    {
	buttonDismiss->setVisible(true);
	buttonDismiss->setAction(Action::ButtonDismiss);
	buttonDismiss->setDisabled(true);
    }

    selectedClan.reset();

#ifdef BUILD_DEBUG
    debugLand = Land::TowerOf4Winds;
#endif
    if(Multiplayer::session().active())
    {
        takeMultiplayerHandoffEvents(actions);
        syncNetworkState();
    }
}

void AdventurePartScreen::renderLabel(void)
{
    const FontRender & frs = GameTheme::fontRender(defaultFont);
    const bool choosingDestination = orderSource.isValid() || moveFlag.isVisible();
    renderText(frs, choosingDestination ? _("Select destination") : _("Movement Phase"),
	       defaultColor, viewMapPos, AlignCenter);
    renderTextInfo(cueLegend, choosingDestination ?
        _("Blue: move  Orange: attack") : _("Yellow: claimable"));
}


void AdventurePartScreen::tickEvent(u32 ms)
{
    if(allowTickEvent && tt.check(ms, 100))
    {
        const bool switched = actions.empty() && selectLocalAvatar();
        if(actions.empty())
        {
            Multiplayer::adventureEvents(myAvatar, actions);
            if(Multiplayer::session().active() && networkRevision != Multiplayer::session().revision() && history.empty())
                syncNetworkState();
        }
        bool redraw = switched;
        bool processedAction = false;
        int lastActionType = Action::None;

        while(actions.size())
        {
            auto action = actions.front();
            actions.pop_front();
            processedAction = true;
            lastActionType = action.type();
            CrashReport::breadcrumb(std::string("Adventure action type=")
                .append(String::number(action.type())).append(" payload=").append(action.toString()));

            switch(action.type())
            {
		case Action::AdventureTurn:
		    redraw = actionAdventureTurn(action);
		    break;

		case Action::AdventureMoves:
		    redraw = actionAdventureMoves(action);
		    break;

		case Action::AdventureClaim:
		    redraw = actionAdventureClaim(action);
		    break;

		case Action::AdventureBattleChoice:
		    redraw = actionAdventureBattleChoice(action);
		    break;

		case Action::AdventureCombat:
		    redraw = actionAdventureCombat(action);
		    break;

		case Action::AdventureEnd:
		    redraw = actionAdventureEnd(action);
		    break;

                default:
                    ERROR("unknown action: " << action.type());
                    break;
            }
        }

        if(processedAction)
        {
            JsonObject gui;
            gui.addString("type", "AdventureRecovery");
            const std::string reason = std::string("adventure-action-") + String::number(lastActionType);
            if(!GameData::saveRecovery(gui, reason))
                ERROR("recovery checkpoint failed: " << reason);
        }

        if(redraw) renderWindow();
    }
}

bool AdventurePartScreen::selectLocalAvatar(void)
{
    const Avatar selected = Multiplayer::session().active() ? Multiplayer::session().localAvatar() : GameData::localAdventureAvatar();
    if(!selected.isValid() || selected == myAvatar) return false;

    cancelOrderMode(false);
    myAvatar = selected;
    ld = GameData::toLocalData(myAvatar);
    moveFlag.setClan(ld.myPlayer().clan);
    ld.myPlayer().army.setAllSelected();
    updateCommandButtons();
    return true;
}

bool AdventurePartScreen::actionAdventureTurn(const ActionMessage & v)
{
    auto action = static_cast<const AdventureTurn &>(v);

    if(Multiplayer::session().active()) syncNetworkState();

    ld.currentWind = action.currentWind();
    const RemotePlayer & player = ld.playerOfWind(ld.currentWind);

    if(!ld.yourTurn()) cancelOrderMode(false);
    updateCommandButtons();

    if(ld.yourTurn() && buttonDone && !ld.myPlayer().adventurePartDone())
    {
	buttonDone->setDisabled(false);
    }

    pushEventAction(AdventureTurnPlayer, this, const_cast<RemotePlayer*>(& player));
    DEBUG("current wind: " << ld.currentWind.toString() << ", person: " << player.name());

    return true;
}

bool AdventurePartScreen::actionAdventureMoves(const ActionMessage & v)
{
    auto action = static_cast<const AdventureMoves &>(v);
    ld.currentWind = action.currentWind();

    if(Multiplayer::session().active()) syncNetworkState();
    else if(! ld.yourTurn())
    {
	// read raw info
	MapScreenBase::ld = GameData::toLocalData(myAvatar);
    }

    DEBUG("current wind: " << ld.currentWind.toString() << ", uid: " << action.unit() << ", to land: " << action.land().toString());
    return true;
}

void AdventurePartScreen::syncNetworkState(void)
{
    std::set<int> selected;
    for(const BattleCreature* creature : ld.myPlayer().army.toBattleCreatures())
        if(creature->isSelected()) selected.insert(creature->battleUnit());
    const Wind previousWind = ld.currentWind;
    ld = GameData::toLocalData(myAvatar);
    networkRevision = Multiplayer::session().revision();
    for(BattleCreature* creature : ld.myPlayer().army.toBattleCreatures())
        creature->setSelected(previousWind != ld.currentWind || selected.count(creature->battleUnit()));
    selectedCreature.reset();
    affectedSpells.setVisible(false);
    bar1.reset();
    bar2.reset();
    const bool mayMove = Multiplayer::session().phase() == Menu::AdventurePart &&
        ld.yourTurn() && !ld.myPlayer().adventurePartDone();
    if(buttonDone) buttonDone->setDisabled(!mayMove);
    if(!mayMove) cancelOrderMode(false);
    updateCommandButtons();
    if(selectedLand.isValid())
        pushEventAction(MapScreenSelectLand, this, const_cast<LandInfo*>(&GameData::landInfo(selectedLand)));
    DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, nullptr);
}

bool AdventurePartScreen::actionAdventureClaim(const ActionMessage & v)
{
    auto action = static_cast<const AdventureClaim &>(v);
    ld = GameData::toLocalData(myAvatar);
    ld.currentWind = action.currentWind();

    const LandInfo & info = GameData::landInfo(action.land());
    DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, const_cast<LandInfo*>(& info));
    pushEventAction(MapScreenSelectLand, this, const_cast<LandInfo*>(& info));

    DEBUG("land owner changed: " << action.land().toString() << ", previous owner: " <<
          action.previousOwner().toString() << ", owner: " << action.owner().toString() <<
          ", cost: " << action.cost() << ", reverted: " << action.reverted());
    return true;
}

bool AdventurePartScreen::actionAdventureBattleChoice(const ActionMessage & v)
{
    const AdventureBattleChoice & action = static_cast<const AdventureBattleChoice &>(v);
    ld.currentWind = action.currentWind();
    const BattleLegend legend = action.legend();
    if(myAvatar != legend.attacker)
    {
	ERROR("battle choice delivered to non-attacker: " << myAvatar.toString());
	return false;
    }

    allowTickEvent = false;
    animationsDisabled(true);

#ifndef SWE_DISABLE_AUDIO
    if(Settings::music()) Music::reset();
#endif

    BattleChoiceDialog dialog(legend, action.phase(), action.strikes(),
                              action.actors(), action.targets(),
	                      action.recommendedActor(), action.recommendedTarget(),
	                      action.choiceNumber(), action.choiceCount(), *this);
    dialog.exec();

    const ClientBattleChoice choice(dialog.actor(), dialog.target(), dialog.autoResolve());
    const bool accepted = submitHumanAction(choice);
    if(!accepted)
	ERROR("interactive battle choice was rejected");

    allowTickEvent = true;
    animationsDisabled(false);
    return true;
}

bool AdventurePartScreen::actionAdventureCombat(const ActionMessage & v)
{
    auto action = static_cast<const AdventureCombat &>(v);
    ld.currentWind = action.currentWind();

    allowTickEvent = false;
    animationsDisabled(true);

    const BattleLegend legend = action.legend();
    const BattleStrikes strikes = action.strikes();
    const bool visibleBattle = myAvatar == legend.attacker || myAvatar == legend.defender;

#ifndef SWE_DISABLE_AUDIO
    if(visibleBattle && Settings::music()) Music::reset();
#endif

    DEBUG("current wind: " << ld.currentWind.toString() << ", " << "legend: " << legend.toString() << ", " << "strikes: " << strikes.toString());
    playSound("sndfight");

    // draw fight sprite
    const LandInfo & landInfo = GameData::landInfo(legend.land());

    // Land combatLand = ;
    // Clan combatClan = legend.town.previousClan();

    if(visibleBattle)
    {
	// wait fightsnd
	playSoundWait();

	CombatScreenDialog dialog(legend, strikes, *this);
	dialog.exec();
    }
    else
    {
	// redraw AI combat status
	DisplayScene::pushEvent(nullptr, LandPolygonCombatStatus, const_cast<LandInfo*>(& landInfo));
	DisplayScene::handleEvents(200);

	// wait fightsnd
	playSoundWait();
    }

    // loss: remove party
    if(! legend.wins)
    {
	// remove attacker army from land
	RemotePlayer & remote = const_cast<RemotePlayer &>(ld.playerOfAvatar(legend.attacker));
	BattleArmy & attackers = remote.army;
	BattleParty* party = attackers.findParty(legend.land());

	if(party)
	{
	    party->dismiss();
	    attackers.shrinkEmpty();
	}
    }

    DisplayScene::pushEvent(nullptr, LandPolygonCombatStatusReset, const_cast<LandInfo*>(& landInfo));
    DisplayScene::pushEvent(nullptr, LandPolygonFlagAnimationReInit, const_cast<LandInfo*>(& landInfo));

    // wait scene events
    DisplayScene::handleEvents(delayCombatResult);

    allowTickEvent = true;
    animationsDisabled(false);

    if(visibleBattle) playMusic("map");

    return true;
}

bool AdventurePartScreen::actionAdventureEnd(const ActionMessage & v)
{
    auto action = static_cast<const AdventureEnd &>(v);

    DEBUG("goto next screen");

#ifdef BUILD_DEBUG
    // A visible AI takeover is intentionally scoped to one complete phase.
    GameData::setDeveloperAutoplay(myAvatar, false);
#endif

    // AdventureEnd is the stable boundary of a complete Mahjong/adventure
    // round. Keep Continue current without exposing diagnostic checkpoints as
    // ordinary player saves.
    JsonObject gui;
    gui.addString("type", "AdventurePart");

    if(!GameData::saveGame(gui))
        ERROR("round autosave failed");

    setResultCode(Menu::BattleSummaryPart);
    setVisible(false);

    return true;
}
