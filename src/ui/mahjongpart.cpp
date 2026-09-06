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

#include "settings.h"
#include "runewars.h"
#include "gametheme.h"
#include "dialogs.h"
#include "runegametargetdialogs.h"
#include "runegamewidgets.h"
#include "actions.h"
#include "mahjongpart.h"
#include "matchtopology.h"
#include "matchpresentation.h"

namespace
{
// Reduce transparent artwork in stages; SDL2 Texture::scale ignores its
// smoothing argument and a single large reduction loses the fine wind strokes.
Texture fitTableArtwork(const Texture & texture, const Size & bounds)
{
    if(!texture.isValid() || bounds.isEmpty()) return texture;
    const double ratio = std::min(double(bounds.w) / texture.width(), double(bounds.h) / texture.height());
    const Size target(std::max(1, int(texture.width() * ratio)), std::max(1, int(texture.height() * ratio)));
    Surface surface = Display::createSurface(texture);
    while(surface.width() > target.w * 2 && surface.height() > target.h * 2)
        surface = Surface::scale(surface, Size(surface.width() / 2, surface.height() / 2), true);
    return Display::createTexture(Surface::scale(surface, target, true));
}

void placeDuelGuardian(SpritesAnimation & animation, const Point & center, const Size & bounds)
{
    // The resting pose and every action frame share one place on the table.
    // Resize the screen's copies, leaving the theme's FFA sprites intact.
    for(Sprite & frame : animation.sprites)
    {
        frame.setTexture(fitTableArtwork(frame, bounds));
        frame.setPosition(center - frame.size() / 2);
    }
}

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

}

WindMarker::WindMarker(const Sprites & sp)
{
    if(1 < sp.size())
    {
	first = sp[0];
	second = sp[1];
    }
}

OrderTurn::OrderTurn(const JsonObject & jobject)
{
    const JsonObject* jo = jobject.getObject("windcompass:sprites");
    if(jo)
    {
	windsMarker[0] = WindMarker(GameTheme::jsonSprites(*jo, "east"));
	windsMarker[1] = WindMarker(GameTheme::jsonSprites(*jo, "south"));
	windsMarker[2] = WindMarker(GameTheme::jsonSprites(*jo, "west"));
	windsMarker[3] = WindMarker(GameTheme::jsonSprites(*jo, "north"));
        if(jobject.hasKey("windcompass:size"))
        {
            const Size bounds = GameTheme::jsonSize(jobject, "windcompass:size");
            for(auto & marker : windsMarker)
            {
                marker.first = fitTableArtwork(marker.first, bounds);
                marker.second = fitTableArtwork(marker.second, bounds);
            }
        }
    }

    windPositions = GameTheme::jsonSidesPositions(jobject, "windcompass:positions");
}

WindMarker OrderTurn::createMarker(const Wind & wind, bool local) const
{
    WindMarker res = windsMarker[wind() - 1];
    if(local) res.swap();
    return res;
}

void OrderTurn::renderCentered(Window & win, const Point & pos, const WindMarker & marker) const
{
    win.renderTexture(marker.tx(), Point(pos.x - marker.tx().width() / 2, pos.y - marker.tx().height() / 2));
}

void OrderTurn::render(Window & win, const Wind & localWind, const Wind & currentWind, const Wind & partWind) const
{
    const WindCompass winds(localWind);

    renderCentered(win, windPositions.center, windsMarker[partWind() - 1]);
    // In Duel the two seat winds belong to the opposing name plaques.
    if(MatchPresentation::duel()) return;
    if(activeMatchTopology().hasWind(winds.left()()))
        renderCentered(win, windPositions.left, createMarker(winds.left(), winds.left() == currentWind));
    if(activeMatchTopology().hasWind(winds.right()()))
        renderCentered(win, windPositions.right, createMarker(winds.right(), winds.right() == currentWind));
    renderCentered(win, windPositions.top, createMarker(winds.top(), winds.top() == currentWind));
    renderCentered(win, windPositions.bottom, createMarker(winds.bottom(), winds.bottom() == currentWind));
}

void OrderTurn::renderSeat(Window & win, const Point & center, const Wind & wind, bool active) const
{
    if(wind.isValid()) renderCentered(win, center, createMarker(wind, active));
}

Rect TurnAnimation::maxArea(void) const
{
    Rects rects; rects.reserve(sprites.size());
    for(auto & sprite : sprites)
	rects.push_back(sprite.area());
    return rects.around();
}

void TurnAnimation::renderTick(Window & win) const
{
    for(auto it = sprites.begin(); it != sprites.end(); ++it)
    {
        win.renderTexture(*it);

	if(std::distance(sprites.begin(), it) + 1 == sprites.size() - index)
	    break;
    }
}

void TurnAnimation::renderAll(Window & win) const
{
    for(auto & sprite: sprites)
        win.renderTexture(sprite);
}

bool TurnAnimation::isLastSprite(void) const
{
    return index + 1 == sprites.size();
}

MahjongPartScreen::MahjongPartScreen() : JsonWindow("screen_mahjongpart.json", nullptr),
    myAvatar(GameData::localMahjongAvatar()), orderTurn(jobject), animationTurn(jobject, "animation:turn"),
    animationChao(jobject, "animation:chao"), animationPung(jobject, "animation:pung"),
    animationKong(jobject, "animation:kong"), animationGame(jobject, "animation:game"),
    stoneSelected(-1), variantSelected(-1), playersMarker(0), animationDropStep(40),
    animationDropDelay(12), animationDealDelay(55), dealingInitialHand(false), dealtStoneCount(0),
    animatingDrawStone(false), iconAffectedSkull(this), iconAffectedSword(this), iconAffectedNumber(this),
    iconAffectedDiscard(this), iconAffectedSilence(this), iconAffectedScry(this), playerReady(false),
    resolvingLuckChoice(false), turnTimeoutPending(false)
{
    ld = GameData::toLocalData(myAvatar);
    restoreMahjongMusic();

    stonesPos.reserve(GAME_SET_COUNT);
    stoneActiveSprite = GameTheme::jsonSprite(jobject, "stone:active");
    stoneSelectedSprite = GameTheme::jsonSprite(jobject, "stone:selected");

    stoneVariantSprite = Display::createTexture(Size(stoneSelectedSprite.width(), stoneSelectedSprite.height() / 2));
    Display::renderTexture(stoneSelectedSprite, Rect(0, stoneSelectedSprite.height() / 2, stoneSelectedSprite.width(), stoneSelectedSprite.height() / 2), stoneVariantSprite, Rect(Point(0, 0), stoneVariantSprite.size()));

    defaultColor = GameTheme::jsonColor(jobject, "default:color");
    defaultFont = jobject.getString("default:font");
    namesFont = jobject.getString("names:font");

    animationDropDelay = Settings::presentationDelay(jobject.getInteger("mahjongdrop:delay", 12));
    animationDropStep = jobject.getInteger("mahjongdrop:step", 40);
    animationDealDelay = Settings::presentationDelay(jobject.getInteger("mahjongdeal:delay", 55));

    animationChao.delay = Settings::presentationDelay(animationChao.delay);
    animationPung.delay = Settings::presentationDelay(animationPung.delay);
    animationKong.delay = Settings::presentationDelay(animationKong.delay);
    animationGame.delay = Settings::presentationDelay(animationGame.delay);

    iconAffectedSkull.setJsonObject(jobject.getObject("affected:skull"));
    iconAffectedSkull.setVisible(false);
    iconAffectedSword.setJsonObject(jobject.getObject("affected:sword"));
    iconAffectedSword.setVisible(false);
    iconAffectedNumber.setJsonObject(jobject.getObject("affected:number"));
    iconAffectedNumber.setVisible(false);
    iconAffectedDiscard.setJsonObject(jobject.getObject("affected:discard"));
    iconAffectedDiscard.setVisible(false);
    iconAffectedSilence.setJsonObject(jobject.getObject("affected:silence"));
    iconAffectedSilence.setVisible(false);
    iconAffectedScry.setJsonObject(jobject.getObject("affected:scry"));
    iconAffectedScry.setVisible(false);

    remainsPos = GameTheme::jsonPoint(jobject, "offset:remains");
    localSetPos = GameTheme::jsonPoint(jobject, "offset:localset");
    dropStonePos = GameTheme::jsonPoint(jobject, "offset:dropstone");
    croupierPos = GameTheme::jsonPoint(jobject, "offset:trash");

    winSetPos = GameTheme::jsonSidesPositions(jobject, "winrules:positions");
    namesPos = GameTheme::jsonSidesPositions(jobject, "names:positions");
    fastLogText = GameTheme::jsonTextInfo(jobject, "textinfo:fastlog");
    fastLogNormalColor = fastLogText.color;

    if(MatchPresentation::duel())
    {
        if(jobject.hasKey("duel:background") && !sprites.empty())
        {
            const Sprite background = GameTheme::jsonSprite(jobject, "duel:background");
            if(background.isValid()) sprites.front().setTexture(fitTableArtwork(background, size()));
        }
        namesPos.top = Point(512, 141);
        namesPos.bottom = Point(512, 535);
        croupierPos = Point(296, 190);
        dropStonePos = Point(832, 475);
        fastLogText.position = Point(512, 577);
        placeDuelGuardian(animationChao, Point(110, 85), Size(168, 168));
        placeDuelGuardian(animationPung, Point(918, 105), Size(168, 168));
        placeDuelGuardian(animationKong, Point(948, 455), Size(132, 132));
        placeDuelGuardian(animationGame, Point(106, 566), Size(168, 168));
        // Keep claims beside the current discard, clear of the discard history.
        const std::pair<const char*, Point> claims[] = {
            {"but_chow", Point(736, 535)}, {"but_pung", Point(816, 535)},
            {"but_kong", Point(896, 535)}, {"but_game", Point(776, 579)},
            {"but_pass", Point(856, 579)}
        };
        for(const auto & claim : claims)
            if(JsonButton* button = buttons.findIds(claim.first)) button->setPosition(claim.second);
    }

    buttonPass = buttons.findIds("but_pass");
    buttonChao = buttons.findIds("but_chow");
    buttonPung = buttons.findIds("but_pung");
    buttonKong = buttons.findIds("but_kong");
    buttonGame = buttons.findIds("but_game");
    buttonCast = buttons.findIds("but_cast");

    if(!buttonPass)
    {
	ERROR("button id not found: " << "but_pass");
	Engine::except(__FUNCTION__, "exit");
    }

    if(!buttonChao)
    {
	ERROR("button id not found: " << "but_chow");
	Engine::except(__FUNCTION__, "exit");
    }

    if(!buttonPung)
    {
	ERROR("button id not found: " << "but_pung");
	Engine::except(__FUNCTION__, "exit");
    }

    if(!buttonKong)
    {
	ERROR("button id not found: " << "but_kong");
	Engine::except(__FUNCTION__, "exit");
    }

    if(!buttonGame)
    {
	ERROR("button id not found: " << "but_game");
	Engine::except(__FUNCTION__, "exit");
    }

    buttonLocalReady = *buttonPass;
    //buttonLocalReady.setParent(this);
    buttonLocalReady.setAction(Action::ButtonLocalReady);
    buttonLocalReady.setPosition(localReadyPos());

    buttonLocalKong = *buttonKong;
    //buttonLocalKong.setParent(this);
    buttonLocalKong.setAction(Action::ButtonLocalKong);
    buttonLocalKong.setPosition(localKongPos());

    buttonLocalGame = *buttonGame;
    //buttonLocalGame.setParent(this);
    buttonLocalGame.setAction(Action::ButtonLocalGame);
    buttonLocalGame.setPosition(localGamePos());

    setVisible(true);

    buttonPass->setVisible(false);
    buttonPass->setAction(Action::ButtonPass);

    buttonChao->setVisible(false);
    buttonChao->setAction(Action::ButtonChao);

    buttonPung->setVisible(false);
    buttonPung->setAction(Action::ButtonPung);

    buttonKong->setVisible(false);
    buttonKong->setAction(Action::ButtonKong);

    buttonGame->setVisible(false);
    buttonGame->setAction(Action::ButtonGame);

    buttonLocalReady.setVisible(true);
    buttonLocalKong.setVisible(false);
    buttonLocalGame.setVisible(false);

    JsonButton* button = nullptr;

    button = buttons.findIds("but_cast");
    if(button) button->setAction(Action::ButtonCast);

    button = buttons.findIds("but_logs");
    if(button) button->setAction(Action::ButtonChat);

    button = buttons.findIds("but_syst");
    if(button) button->setAction(Action::ButtonSystem);

    button = buttons.findIds("but_qest");
    if(button) button->setAction(Action::ButtonSummary);

    button = buttons.findIds("but_maps");
    if(button) button->setAction(Action::ButtonMap);

    // disable guardian rules sound
    if(! Settings::soundGuardianRules())
    {
	animationChao.sound.clear();
	animationPung.sound.clear();
	animationKong.sound.clear();
	// animationGame.sound.clear();
    }

    const JsonObject & savedState = GameData::jsonGUI();
    if(savedState.isValid()) fromJsonObject(savedState);

    // Affected-spell icons are derived from the authoritative player state.
    // Never let stale GUI data from a save briefly resurrect expired effects.
    syncAffectedSpellIndicators();

    DEBUG("start");
}

Point MahjongPartScreen::localReadyPos(void) const
{
    const Rect & pos = newStonePos();
    return pos.toPoint() + Point(10, pos.w - buttonLocalReady.height());
}

Point MahjongPartScreen::localKongPos(void) const
{
    const Rect & pos = newStonePos();
    return pos.toPoint() + Point(pos.w + 10, 0);
}

Point MahjongPartScreen::localGamePos(void) const
{
    const Rect & pos = newStonePos();
    return pos.toPoint() + pos.toSize() + Point(10, -buttonLocalGame.height());
}

Rect MahjongPartScreen::newStonePos(void) const
{
    const GameStones & stones = ld.myPlayer().stones;
    Rect res;

    if(stones.size())
    {
	const StoneSprite sprite(stones[0], StoneSprite::Large);
	res = Rect(Point(localSetPos.x + (sprite.width() * stones.size()) / 2 + 10, localSetPos.y), sprite.size());
    }

    return res;
}

void MahjongPartScreen::renderWindow(void)
{
    JsonWindow::renderWindow();

    if(MatchPresentation::duel())
    {
        using namespace MatchPresentation;
        text(*this, modeName(), Point(192, 176), 166, ink(), 26);
        int labelY = 214;
        for(const std::string & line : GameTheme::fontRender("dejavus14").splitStringWidth(rulesName(), 166))
        {
            text(*this, line, Point(192, labelY), 166, ink(), 14);
            labelY += 18;
        }
    }
    else
    {
        MatchPresentation::card(*this, Rect(280, 554, 466, 26), Color(107, 99, 70));
        MatchPresentation::text(*this, MatchPresentation::modeName() + " / " + MatchPresentation::rulesName(),
                                Point(513, 557), 450, MatchPresentation::ink(), 16);
    }

    renderCroupier();
    renderWinRules();
    renderNames();
    renderScryRunes();
    renderGameStoneRemains();

    if(0 <= variantSelected)
    {
	GameStones stones = ld.myPlayer().stones;
	stones.add(ld.dropStone);
	renderLocalSet(stones);
    }
    else
	renderLocalSet(ld.myPlayer().stones);

    orderTurn.render(*this, ld.myPlayer().wind, ld.currentWind, ld.partWind);

    if(animationTurn.isEnabled())
    {
	animationTurn.renderTick(*this);

	if(animationTurn.isLastSprite())
	{
	    animationTurn.setEnabled(false);
	    // Do not put an unscoped timeout into the shared SDL event queue.
	    // Authoritative Mahjong actions may advance several turns before that
	    // event is delivered, allowing it to act on a later choice window.
	    turnTimeoutPending = true;
	}
    }
    else
	animationTurn.renderAll(*this);

    animationChao.render(*this);
    animationPung.render(*this);
    animationKong.render(*this);
    animationGame.render(*this);

    if(0 > variantSelected &&
	ld.dropStone.isValid() && 0 > stoneSelected)
    {
	const StoneSprite sprite(ld.dropStone, StoneSprite::Large);
	renderTexture(sprite, dropStonePos - sprite.size() / 2);
    }

    if(buttonCast)
	buttonCast->setInformed(checkCastInformer());

    if(fastLogText.text.size()) renderTextInfo(fastLogText);
}

bool MahjongPartScreen::checkCastInformer(void) const
{
    if(ld.myPlayer().isCasted() ||
       ld.myPlayer().isSilenced() ||
       ld.myPlayer().isAffectedSpell(Spell::ManaFog))
	return false;

    const AvatarInfo & avatarInfo = GameData::avatarInfo(ld.myPlayer().avatar);
    const Spells & spells = avatarInfo.spells;
    const Creatures & creatures = avatarInfo.creatures;

    for(auto it = spells.begin(); it != spells.end(); ++it)
    {
        const SpellInfo & spellInfo = GameData::spellInfo(*it);
        if(spellInfo.stones.size() && ld.myPlayer().points >= spellInfo.cost &&
	    ld.myPlayer().stones.allowCast(spellInfo.stones, ld.myPlayer().newStone))
	    return true;
    }

    for(auto it = creatures.begin(); it != creatures.end(); ++it)
    {
        const CreatureInfo & creatureInfo = GameData::creatureInfo(*it);
        if(creatureInfo.stones.size() && ld.myPlayer().points >= creatureInfo.cost &&
	    ld.myPlayer().stones.allowCast(creatureInfo.stones, ld.myPlayer().newStone))
	    return true;
    }

    return false;
}

void MahjongPartScreen::syncAffectedSpellIndicators(void)
{
    const LocalPlayer & player = ld.myPlayer();

    iconAffectedSkull.setVisible(player.isAffectedSpell(Spell::DrawSkull));
    iconAffectedSword.setVisible(player.isAffectedSpell(Spell::DrawSword));
    iconAffectedNumber.setVisible(player.isAffectedSpell(Spell::DrawNumber));
    iconAffectedDiscard.setVisible(player.isAffectedSpell(Spell::RandomDiscard));
    iconAffectedSilence.setVisible(player.isAffectedSpell(Spell::Silence));
    iconAffectedScry.setVisible(player.isAffectedSpell(Spell::ScryRunes));
}

void MahjongPartScreen::actionQuit(void)
{
#ifndef SWE_DISABLE_AUDIO
    if(Settings::music()) Music::reset();
#endif

    setResultCode(Menu::MahjongSummaryPart);
    setVisible(false);
}

void MahjongPartScreen::restoreMahjongMusic(void)
{
#ifndef SWE_DISABLE_AUDIO
    if(Settings::music())
    {
	const std::string clan = GameData::myPerson().clan.toString();
	const std::string clanKey = std::string("music:").append(clan);
	StringList playlist;

	if(jobject.hasKey(clanKey))
	    playlist = jobject.getStdList<std::string>(clanKey);
	else
	    playlist.push_back(clan);

	if(jobject.hasKey("music:common"))
	{
	    const StringList common = jobject.getStdList<std::string>("music:common");
	    for(const auto & track : common)
		if(std::find(playlist.begin(), playlist.end(), track) == playlist.end())
		    playlist.push_back(track);
	}

	const std::string preferred = playlist.empty() ? clan : playlist.front();
	setMusicPlaylist(playlist, preferred);
    }
#endif
}

JsonObject MahjongPartScreen::toJsonObject(void)
{
    JsonObject jo;

    jo.addString("type", "MahjongPartScreen");

    jo.addInteger("stoneSelected", stoneSelected);
    jo.addInteger("variantSelected", variantSelected);
    jo.addInteger("playersMarker", playersMarker);
    jo.addString("fastLogText", fastLogText.text);
    jo.addString("fastLogOwner", fastLogOwner.toString());

    if(buttonPass) jo.addObject("buttonPass", buttonPass->toJsonInfo());
    if(buttonChao) jo.addObject("buttonChao", buttonChao->toJsonInfo());
    if(buttonPung) jo.addObject("buttonPung", buttonPung->toJsonInfo());
    if(buttonKong) jo.addObject("buttonKong", buttonKong->toJsonInfo());
    if(buttonGame) jo.addObject("buttonGame", buttonGame->toJsonInfo());

    jo.addObject("buttonLocalReady", buttonLocalReady.toJsonInfo());
    jo.addObject("buttonLocalKong", buttonLocalKong.toJsonInfo());
    jo.addObject("buttonLocalGame", buttonLocalGame.toJsonInfo());

    jo.addBoolean("playerReady", playerReady);
    jo.addArray("gameLogs", JsonPack::stringList(gameLogs.toStringList()));

    return jo;
}

bool MahjongPartScreen::fromJsonObject(const JsonObject & jo)
{
    const std::string & type = jo.getString("type");

    if(type == "MahjongPartScreen")
    {
	stoneSelected = jo.getInteger("stoneSelected");
	variantSelected = jo.getInteger("variantSelected");
	playersMarker = jo.getInteger("playersMarker");
	fastLogText.text = jo.getString("fastLogText");
	fastLogOwner = Wind(jo.getString("fastLogOwner", "none"));

	if(buttonPass) buttonPass->setJsonInfo(jo.getObject("buttonPass"));
	if(buttonChao) buttonChao->setJsonInfo(jo.getObject("buttonChao"));
	if(buttonPung) buttonPung->setJsonInfo(jo.getObject("buttonPung"));
	if(buttonKong) buttonKong->setJsonInfo(jo.getObject("buttonKong"));
	if(buttonGame) buttonGame->setJsonInfo(jo.getObject("buttonGame"));

	buttonLocalReady.setJsonInfo(jo.getObject("buttonLocalReady"));
	buttonLocalKong.setJsonInfo(jo.getObject("buttonLocalKong"));
	buttonLocalGame.setJsonInfo(jo.getObject("buttonLocalGame"));

	playerReady = jo.getBoolean("playerReady", false);
	gameLogs = jo.getStdList<std::string>("gameLogs");
	return true;
    }
    else
    {
	ERROR("unknown type: " << (type.size() ? type : "null"));
    }

    return false;
}

void MahjongPartScreen::actionButtonChat(void)
{
    ShowLogsDialog(gameLogs, *this).exec();
}

bool MahjongPartScreen::actionCreateScreenshot(void)
{
    std::string file = Settings::fileSave("screenshot.png");

    if(Display::renderScreenshot(file))
    {
	VERBOSE(file);
	return true;
    }

    return false;
}

bool MahjongPartScreen::actionButtonSystem(void)
{
    ScopedTickPause pause(*this);
    const int choice = InGameMenuDialog(*this).exec();

    if(choice == InGameMenuResult::SaveGame)
    {
        SaveGameAs(toJsonObject(), *this);
        renderWindow();
        return true;
    }

    if(choice == InGameMenuResult::MainMenu)
    {
        if(!GameData::saveGame(toJsonObject()))
        {
            MessageBox(_("Save Game"), _("Unable to update the autosave."), *this, false).exec();
            renderWindow();
            return true;
        }
        setResultCode(Menu::MainMenu);
        setVisible(false);
	return true;
    }

    renderWindow();
    return true;
}

void MahjongPartScreen::actionButtonSummary(void)
{
    MapStatusDialog(ld, *this).exec();
}

void MahjongPartScreen::actionButtonMap(void)
{
    ShowMapDialog(ld, *this).exec();
    restoreMahjongMusic();
}

void MahjongPartScreen::renderDropStone(void)
{
    playSound("stone2");

    const WindCompass compass(ld.myPlayer().wind);
    const Point* pointFrom = nullptr;

    if(compass.left() == ld.currentWind)
	pointFrom = & winSetPos.left;
    else
    if(compass.right() == ld.currentWind)
	pointFrom = & winSetPos.right;
    else
    if(compass.top() == ld.currentWind)
	pointFrom = & winSetPos.top;
    else
    if(compass.bottom() == ld.currentWind)
	pointFrom = & winSetPos.bottom;

    if(pointFrom && 0 < animationDropStep)
    {
	const Point dropPosBack = dropStonePos;
	const Points points = Tools::renderLine(*pointFrom, dropStonePos, animationDropStep);

	animationTurn.setPause(true);

	for(auto & pt : points)
	{
	    dropStonePos = pt;

	    setDirty(true);
	    DisplayScene::sceneRedraw();

            Tools::delay(1 < animationDropDelay ? animationDropDelay : 1);
	}

	dropStonePos = dropPosBack;
	animationTurn.setPause(false);
	renderWindow();
    }
}

void MahjongPartScreen::renderDrawStone(void)
{
    const LocalPlayer & player = ld.myPlayer();
    if(!player.newStone.isValid() || animationDropStep <= 0) return;

    const StoneSprite sprite(player.newStone, StoneSprite::Large);
    const Point source = remainsPos - sprite.size() / 2;
    const Point destination = newStonePos().toPoint();
    const Points points = Tools::renderLine(source, destination, animationDropStep);

    animationTurn.setPause(true);
    animatingDrawStone = true;
    for(const Point & point : points)
    {
        drawStoneAnimationPos = point;
        setDirty(true);
        DisplayScene::sceneRedraw();
        Tools::delay(1 < animationDropDelay ? animationDropDelay : 1);
    }
    animatingDrawStone = false;
    animationTurn.setPause(false);
    renderWindow();
}

void MahjongPartScreen::renderInitialDeal(void)
{
    const std::size_t stoneCount = ld.myPlayer().stones.size();
    if(!stoneCount) return;

    dealingInitialHand = true;
    dealtStoneCount = 0;
    renderWindow();
    playSound("stone2");

    while(dealtStoneCount < stoneCount)
    {
        ++dealtStoneCount;
        renderWindow();
        DisplayScene::sceneRedraw();
        Tools::delay(1 < animationDealDelay ? animationDealDelay : 1);
    }

    dealingInitialHand = false;
    renderWindow();
}

void MahjongPartScreen::renderNames(void)
{
    renderNamesHorizontal(ld.remoteTop(), namesPos.top);
    renderNamesHorizontal(ld.remoteBottom(), namesPos.bottom);
    renderNamesVertical(ld.remoteLeft(), namesPos.left);
    renderNamesVertical(ld.remoteRight(), namesPos.right);
}

std::string MahjongPartScreen::playerPrettyName(const RemotePlayer & player) const
{
    std::string name = String::ucFirst(player.name());

    if(MatchPresentation::duel() || MatchPresentation::teams())
        name += " / " + MatchPresentation::role(player);

    if(GameData::usesAI(player))
	name += " - AI";
    else
    if(! (playersMarker & (static_cast<int>(1) << player.wind())))
    {
	// wait marker
	name += " *";
    }

    return name;
}

void MahjongPartScreen::renderNamesHorizontal(const RemotePlayer & player, const Point & center)
{
    if(!player.avatar.isValid()) return;
    if(MatchPresentation::duel())
    {
        renderDuelName(player, center);
        return;
    }
    Color color = fastLogText.text.size() && fastLogOwner == player.wind ? fastLogText.color :
        (MatchPresentation::teams() ? MatchPresentation::sideColor(MatchPresentation::side(player)) : defaultColor);
    std::string name = playerPrettyName(player);
    const FontRender & frs = GameTheme::fontRender(namesFont);

    Rect pos = renderText(frs, name, color, center, AlignCenter, AlignCenter, true);
    Texture flag = GameTheme::texture(GameData::clanInfo(player.clan).flag1);
    renderTexture(flag, Point(pos.x - flag.width() - 5, pos.y + (pos.h - flag.height()) / 2));
    renderTexture(flag, Point(pos.x + pos.w + 5, pos.y + (pos.h - flag.height()) / 2));
}

void MahjongPartScreen::renderDuelName(const RemotePlayer & player, const Point & center)
{
    using namespace MatchPresentation;
    const bool active = player.wind == ld.currentWind;
    const Color color = active ? ink() : Color(198, 196, 174);
    if(!jobject.hasKey("duel:background"))
        card(*this, Rect(center.x - 222, center.y - 28, 444, 56), Color(107, 99, 70));
    text(*this, String::ucFirst(player.name()), center + Point(0, -22), 310, color, 26);
    text(*this, role(player), center + Point(0, 9), 310, Color(169, 184, 178), 14);
    const Texture flag = GameTheme::texture(GameData::clanInfo(player.clan).flag1);
    renderTexture(flag, center + Point(-182 - flag.width() / 2, -flag.height() / 2));
    orderTurn.renderSeat(*this, center + Point(182, 0), player.wind, active);
}

void MahjongPartScreen::renderNamesVertical(const RemotePlayer & player, const Point & center)
{
    if(!player.avatar.isValid()) return;
    Color color = fastLogText.text.size() && fastLogOwner == player.wind ? fastLogText.color :
        (MatchPresentation::teams() ? MatchPresentation::sideColor(MatchPresentation::side(player)) : defaultColor);
    std::string name = playerPrettyName(player);
    const FontRender & frs = GameTheme::fontRender(namesFont);

    Rect pos = renderText(frs, name, color, center, AlignCenter, AlignCenter, false);
    Texture flag = GameTheme::texture(GameData::clanInfo(player.clan).flag1);
    renderTexture(flag, Point(pos.x + (pos.w - flag.width()) / 2, pos.y - flag.height() - 5));
    renderTexture(flag, Point(pos.x + (pos.w - flag.width()) / 2, pos.y + pos.h + 5));
}

int MahjongPartScreen::renderWinRuleHorizontal(const WinRule & rule, const Point & pos)
{
    int res = 0;
    switch(rule.rule())
    {
	case WinRule::Chao:
	    if(! rule.stone().isSpecial())
	    {
		StoneSprite sprite1(rule.stone(), StoneSprite::Medium);
		StoneSprite sprite2(rule.stone().next(), StoneSprite::Medium);
		StoneSprite sprite3(rule.stone().next().next(), StoneSprite::Medium);
		renderTexture(sprite1, pos);
		res += sprite1.width();
		renderTexture(sprite2, pos + Point(res, 0));
		res += sprite2.width();
		renderTexture(sprite3, pos + Point(res, 0));
		res += sprite3.width();
	    }
	    break;
	case WinRule::Pung:
	    for(int ii = 1; ii <= 3; ++ii)
	    {
		StoneSprite sprite(rule.stone(), StoneSprite::Medium);
		renderTexture(sprite, pos + Point(res, 0));
		res += sprite.width();
	    }
	    break;
	case WinRule::Kong:
	    for(int ii = 1; ii <= 4; ++ii)
	    {
		StoneSprite sprite(rule.stone(), StoneSprite::Medium);
		renderTexture(sprite, pos + Point(res, 0));
		res += sprite.width();
	    }
	    break;
	default: break;
    }
    return res;
}

int MahjongPartScreen::renderWinRuleVertical(const WinRule & rule, const Point & pos)
{
    int res = 0;
    switch(rule.rule())
    {
	case WinRule::Chao:
	    if(! rule.stone().isSpecial())
	    {
		StoneSprite sprite1(rule.stone(), StoneSprite::Medium);
		StoneSprite sprite2(rule.stone().next(), StoneSprite::Medium);
		StoneSprite sprite3(rule.stone().next().next(), StoneSprite::Medium);
		renderTexture(sprite1, pos);
		res += sprite1.height();
		renderTexture(sprite2, pos + Point(0, res));
		res += sprite2.height();
		renderTexture(sprite3, pos + Point(0, res));
		res += sprite3.height();
	    }
	    break;
	case WinRule::Pung:
	    for(int ii = 1; ii <= 3; ++ii)
	    {
		StoneSprite sprite(rule.stone(), StoneSprite::Medium);
		renderTexture(sprite, pos + Point(0, res));
		res += sprite.height();
	    }
	    break;
	case WinRule::Kong:
	    for(int ii = 1; ii <= 4; ++ii)
	    {
		StoneSprite sprite(rule.stone(), StoneSprite::Medium);
		renderTexture(sprite, pos + Point(0, res));
		res += sprite.height();
	    }
	    break;
	default: break;
    }
    return res;
}

void MahjongPartScreen::renderWinRules(void)
{
    renderWinRulesHorizontal(ld.remoteTop().rules, winSetPos.top);
    renderWinRulesHorizontal(ld.remoteBottom().rules, winSetPos.bottom);
    renderWinRulesVertical(ld.remoteLeft().rules, winSetPos.left);
    renderWinRulesVertical(ld.remoteRight().rules, winSetPos.right);
}

void MahjongPartScreen::renderScryRunes(void)
{
    if(ld.remoteTop().isAffectedSpell(Spell::ScryRunes))
    {
	Point pos;
	for(auto & stone : ld.remoteTop().stones)
	{
	    const StoneInfo & info = GameData::stoneInfo(stone);
	    const Texture & tx = GameTheme::texture(info.small);

	    if(pos.isNull())
	    {
		pos.x = namesPos.top.x - (ld.remoteTop().stones.size() * tx.width()) / 2;
		pos.y = MatchPresentation::duel() ? 72 : namesPos.top.y - tx.height() / 2;
	    }

	    renderTexture(tx, pos);
	    pos.x += tx.width();
	}
    }

    if(ld.remoteLeft().isAffectedSpell(Spell::ScryRunes))
	renderScryVertical(ld.remoteLeft().stones, namesPos.left);

    if(ld.remoteRight().isAffectedSpell(Spell::ScryRunes))
	renderScryVertical(ld.remoteRight().stones, namesPos.right);
}

void MahjongPartScreen::renderScryVertical(const Stones & stones, const Point & center)
{
    Point pos;

    for(auto & stone : stones)
    {
	const StoneInfo & info = GameData::stoneInfo(stone);
	const Texture & tx = GameTheme::texture(info.small);

	if(pos.isNull())
	{
	    pos.x = center.x - tx.width() / 2;
	    pos.y = center.y - (stones.size() * tx.height()) / 2;
	}

	renderTexture(tx, pos);
	pos.y += tx.height();
    }
}

void MahjongPartScreen::renderWinRulesHorizontal(const WinRules & rules, const Point & center)
{
    const int countStones = rules.size() * 3 +
	    std::count_if(rules.begin(), rules.end(), [](const WinRule & rule){ return rule.isKong(); });

    if(countStones)
    {
	const StoneSprite sprite(rules[0].stone(), StoneSprite::Medium);
	Point pos = Point(center.x - (sprite.width() * countStones) / 2, center.y);

	for(auto & rule : rules)
	{
	    int offx = renderWinRuleHorizontal(rule, pos);
	    pos.x += offx + 1;
	}
    }
}

void MahjongPartScreen::renderWinRulesVertical(const WinRules & rules, const Point & center)
{
    const int countStones = rules.size() * 3 +
	    std::count_if(rules.begin(), rules.end(), [](const WinRule & rule){ return rule.isKong(); });

    if(countStones)
    {
	const StoneSprite sprite(rules[0].stone(), StoneSprite::Medium);
	Point pos = Point(center.x, center.y - (sprite.height() * countStones) / 2);

	for(auto & rule : rules)
	{
	    int offy = renderWinRuleVertical(rule, pos);
	    pos.y += offy + 1;
	}
    }
}

void MahjongPartScreen::renderCroupier(void)
{
    Point pos = croupierPos;
    const VecStones & trash = ld.trashSet;
    // Two hands leave more stones to draw. Use the small atlas only when the
    // history exceeds six medium rows, so every discard stays on the table.
    const bool duel = MatchPresentation::duel();
    const bool compact = duel && trash.size() > 60;
    const int columns = duel ? (compact ? 18 : 10) : 9;
    const int startX = croupierPos.x + (duel && !compact ? 16 : 0);
    pos.x = startX;

    for(int index = 0; index < trash.size(); ++index)
    {
	const StoneInfo & info = GameData::stoneInfo(trash[index]);
	const Texture & sprite = GameTheme::texture(compact ? info.small : info.medium);

	if(index && 0 == (index % columns))
	{
	    pos.x = startX;
	    pos.y += sprite.height();
	}

	renderTexture(sprite, pos);
	pos.x += sprite.width();
    }
}

void MahjongPartScreen::renderLocalSet(const GameStones & stones)
{
    stonesPos.clear();
    variantsPos.clear();

    if(stones.size())
    {
	const StoneSprite sprite(stones[0], StoneSprite::Large);
        const std::size_t visibleCount = dealingInitialHand ?
            std::min(dealtStoneCount, stones.size()) : stones.size();
	Point pos = Point(localSetPos.x - (sprite.width() * visibleCount) / 2, localSetPos.y);

	std::size_t visibleIndex = 0;
	for(auto it = stones.begin(); it != stones.end() && visibleIndex < visibleCount; ++it, ++visibleIndex)
	{
	    const StoneSprite sprite(*it, StoneSprite::Large);

	    stonesPos.push_back(Rect(pos, sprite.size()));
	    renderTexture(sprite, stonesPos.back());

	    if(! static_cast<const GameStone &>(*it).isCasted())
		renderTexture(stoneActiveSprite, stonesPos.back());

	    pos.x += sprite.width();
	}
    }

    // selected
    if(0 <= variantSelected)
    {
	for(auto & stone : ld.myPlayer().stones.findChaoVariants(ld.dropStone))
	{
	    auto it = std::find(stones.begin(), stones.end(), stone);
	    if(it != stones.end())
		variantsPos.push_back(stonesPos[std::distance(stones.begin(), it)]);
	}

	// mark variant
	for(auto & rt : variantsPos)
	    renderTexture(stoneVariantSprite, rt.toPoint() - Point(4, 6) + Point(0, stoneVariantSprite.height()));

	if(0 <= variantSelected && variantSelected < variantsPos.size())
	    renderTexture(stoneSelectedSprite, variantsPos[variantSelected].toPoint() - Point(4, 6));
    }
    else
    {
	if(0 <= stoneSelected && stoneSelected < stonesPos.size())
	    renderTexture(stoneSelectedSprite, stonesPos[stoneSelected].toPoint() - Point(4, 6));

	const LocalPlayer & player = ld.myPlayer();

	if(player.newStone.isValid())
	{
	    const StoneSprite sprite(player.newStone, StoneSprite::Large);
            const Rect stonePosition = animatingDrawStone ? Rect(drawStoneAnimationPos, sprite.size()) :
                                      newStonePos();
	    renderTexture(sprite, stonePosition);

	    if(! player.newStone.isCasted())
		renderTexture(stoneActiveSprite, stonePosition);

	    if(!animatingDrawStone && stones.size() == stoneSelected)
		renderTexture(stoneSelectedSprite, stonePosition.toPoint() - Point(4, 6));
	}
    }
}

void MahjongPartScreen::renderGameStoneRemains(void)
{
    const FontRender & frs = GameTheme::fontRender(defaultFont);
    renderText(frs, String::number(ld.stoneLastCount), defaultColor, remainsPos, AlignCenter, AlignCenter);
}

void MahjongPartScreen::renderWaitPlayers(const Wind & wind)
{
    playersMarker |= static_cast<int>(1) << wind();
}

bool MahjongPartScreen::actionEventDebug1(void)
{
    VERBOSE("test1");

    auto creature = Creature::RedDragon;
    ShowSummonCreatureDialog summonDialog(ld, creature, *this);
    if(0 < summonDialog.exec())
	GameData::client2Mahjong(myAvatar, ClientSummonCreature(creature, summonDialog.land(), true), actions);

    return true;
}

bool MahjongPartScreen::actionEventDebug2(void)
{
    VERBOSE("test2");

    auto creature = Creature::SkeletonHorde;
    ShowSummonCreatureDialog summonDialog(ld, creature, *this);
    if(0 < summonDialog.exec())
	GameData::client2Mahjong(myAvatar, ClientSummonCreature(creature, summonDialog.land(), true), actions);

/*
    int spell = Spell::Heroism;
    ShowCastSpellDialog spellDialog(ld, spell, *this);

    if(spellDialog.exec())
    {
	BattleTarget target = spellDialog.targetUnit();
	VERBOSE(target.toString());
	GameData::client2Mahjong(myAvatar, ClientCastSpell(spell, target.land, (target.bcr ? target.bcr->battleUnit() : 0), true), actions);
    }
*/

    return true;
}
