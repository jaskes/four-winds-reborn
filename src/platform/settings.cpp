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

#include "aiprofile.h"
#include "gametheme.h"
#include "runewars.h"
#include "settings.h"

namespace
{
    int gameMusicVolume = 100;
    int gameEffectsVolume = 100;
    int gameVoiceVolume = 100;
    bool gameAccel = true;
    bool gameFullscreen = false;
    int gameWindowScale = 100;
    bool guardianRulesSound = true;
    std::string lang;
    std::string speed = "classic";
    std::string selectedContentTheme = "classic";
    std::string selectedRuneGameRuleset = "classic";
    AI::Difficulty defaultAiDifficulty = AI::Difficulty::Normal;

    int normalizedVolume(int value)
    {
	return std::max(0, std::min(100, value));
    }

    int normalizedWindowScale(int value)
    {
        const int clamped = std::max(75, std::min(200, value));
        return ((clamped + 12) / 25) * 25;
    }

    std::string normalizedLanguage(const std::string & value)
    {
	const std::string lower = String::toLower(value);
	if(lower == "ru" || lower == "russian" || lower.rfind("ru_", 0) == 0 ||
	   lower.rfind("ru-", 0) == 0 || lower.rfind("russian_", 0) == 0)
	    return "ru";
	return "en";
    }

    std::string normalizedGameSpeed(const std::string & value)
    {
	const std::string lower = String::toLower(value);
	if(lower == "fast") return "fast";
	if(lower == "normal") return "normal";
	return "classic";
    }

    std::string normalizedRuneGameRuleset(const std::string & value)
    {
        return String::toLower(value) == "quick" ? "quick" : "classic";
    }

    std::string normalizedContentTheme(const std::string & value,
                                       const std::string & fallback = "classic")
    {
	if(value.empty() || value == "." || value == ".." ||
	   value.find('/') != std::string::npos ||
	   value.find('\\') != std::string::npos)
	    return normalizedContentTheme(fallback, "classic");
	if(String::toLower(value) == "default") return "classic";
	return value;
    }

    std::string userSettingsFile(void)
    {
	if(const char* overrideFile = Systems::environment("FOUR_WINDS_SETTINGS_FILE"))
	    return overrideFile;
	return Settings::fileSave("settings.json");
    }
}

bool Settings::read(void)
{
    gameMusicVolume = 100;
    gameEffectsVolume = 100;
    gameVoiceVolume = 100;
    gameAccel = true;
    gameFullscreen = false;
    gameWindowScale = 100;
    guardianRulesSound = true;
    lang = normalizedLanguage(Systems::messageLocale(1));
    speed = "classic";
    defaultAiDifficulty = AI::Difficulty::Normal;
    selectedRuneGameRuleset = "classic";
    selectedContentTheme = "classic";

    JsonObject jo = GameTheme::jsonResource("config.json").toObject();

    if(jo.isValid())
    {
	gameMusicVolume = normalizedVolume(jo.getInteger("music:volume", 100));
	gameEffectsVolume = normalizedVolume(jo.getInteger("sound:volume", 100));
	gameVoiceVolume = normalizedVolume(jo.getInteger("voice:volume", 100));
	if(!jo.getBoolean("music", true)) gameMusicVolume = 0;
	if(!jo.getBoolean("sound", true)) gameEffectsVolume = gameVoiceVolume = 0;
	gameFullscreen = jo.getBoolean("display:fullscreen", false);
	gameWindowScale = normalizedWindowScale(jo.getInteger("display:window_scale", 100));
	gameAccel = jo.getBoolean("display:accel", true);

	guardianRulesSound = jo.getBoolean("sound:guardianrules", true);
	lang = normalizedLanguage(jo.getString("language", Systems::messageLocale(1)));
	speed = normalizedGameSpeed(jo.getString("game:speed", "classic"));
	defaultAiDifficulty = AI::difficultyFromString(
	    jo.getString("ai:difficulty", AI::difficultyName(defaultAiDifficulty)));
	selectedRuneGameRuleset = normalizedRuneGameRuleset(
	    jo.getString("rune_game:ruleset", selectedRuneGameRuleset));
    }

    JsonObject user = JsonContentFile(userSettingsFile()).toObject();
    if(user.isValid())
    {
	if(user.hasKey("music:volume"))
	    gameMusicVolume = normalizedVolume(user.getInteger("music:volume", gameMusicVolume));
	if(user.hasKey("sound:volume"))
	    gameEffectsVolume = normalizedVolume(user.getInteger("sound:volume", gameEffectsVolume));
	if(user.hasKey("voice:volume"))
	    gameVoiceVolume = normalizedVolume(user.getInteger("voice:volume", gameVoiceVolume));
	if(user.hasKey("music") && !user.getBoolean("music", true)) gameMusicVolume = 0;
	if(user.hasKey("sound") && !user.getBoolean("sound", true))
	    gameEffectsVolume = gameVoiceVolume = 0;
	gameFullscreen = user.getBoolean("display:fullscreen", gameFullscreen);
	gameWindowScale = normalizedWindowScale(
	    user.getInteger("display:window_scale", gameWindowScale));
	guardianRulesSound = user.getBoolean("sound:guardianrules", guardianRulesSound);
	lang = normalizedLanguage(user.getString("language", lang));
	speed = normalizedGameSpeed(user.getString("game:speed", speed));
	defaultAiDifficulty = AI::difficultyFromString(
	    user.getString("ai:difficulty", AI::difficultyName(defaultAiDifficulty)));
	selectedRuneGameRuleset = normalizedRuneGameRuleset(
	    user.getString("rune_game:ruleset", selectedRuneGameRuleset));
	selectedContentTheme = normalizedContentTheme(
	    user.getString("content:theme", selectedContentTheme));
    }

    return true;
}

bool Settings::write(std::string* error)
{
    const std::string file = userSettingsFile();
    const std::string directory = Systems::dirname(file);
    if(!directory.empty() && !Systems::isDirectory(directory) && !Systems::makeDirectory(directory))
    {
	if(error) *error = "unable to create the settings directory";
	return false;
    }

    JsonObject jo;
    jo.addString("language", lang);
    jo.addBoolean("music", music());
    jo.addInteger("music:volume", gameMusicVolume);
    jo.addBoolean("sound", sound());
    jo.addInteger("sound:volume", gameEffectsVolume);
    jo.addInteger("voice:volume", gameVoiceVolume);
    jo.addBoolean("display:fullscreen", gameFullscreen);
    jo.addInteger("display:window_scale", gameWindowScale);
    jo.addBoolean("sound:guardianrules", guardianRulesSound);
    jo.addString("game:speed", speed);
    jo.addString("ai:difficulty", AI::difficultyName(defaultAiDifficulty));
    jo.addString("rune_game:ruleset", selectedRuneGameRuleset);
    jo.addString("content:theme", selectedContentTheme);

    if(!Systems::saveString2File(jo.toString(), file))
    {
	if(error) *error = "unable to write settings.json";
	return false;
    }

    if(error) error->clear();
    return true;
}

std::string Settings::language(void)
{
    return lang;
}

void Settings::setLanguage(const std::string & value)
{
    lang = normalizedLanguage(value);
}

std::string Settings::gameSpeed(void)
{
    return speed;
}

void Settings::setGameSpeed(const std::string & value)
{
    speed = normalizedGameSpeed(value);
}

std::string Settings::contentTheme(void)
{
    return selectedContentTheme;
}

void Settings::setContentTheme(const std::string & value)
{
    selectedContentTheme = normalizedContentTheme(value);
}

std::string Settings::preferredContentTheme(const std::string & fallback)
{
    const JsonObject user = JsonContentFile(userSettingsFile()).toObject();
    if(!user.isValid() || !user.hasKey("content:theme"))
        return normalizedContentTheme(fallback);
    return normalizedContentTheme(user.getString("content:theme"), fallback);
}

AI::Difficulty Settings::aiDifficulty(void)
{
    return defaultAiDifficulty;
}

void Settings::setAIDifficulty(AI::Difficulty value)
{
    defaultAiDifficulty = value;
}

std::string Settings::runeGameRuleset(void)
{
    return selectedRuneGameRuleset;
}

void Settings::setRuneGameRuleset(const std::string & value)
{
    selectedRuneGameRuleset = normalizedRuneGameRuleset(value);
}

int Settings::presentationDelay(int milliseconds)
{
    if(milliseconds <= 0) return milliseconds;

    // Classic deliberately restores the more deliberate 1990s presentation.
    // Normal preserves the New Age timing, while Fast is intended for repeat play.
    const int scaled = speed == "classic" ? (milliseconds * 7 + 2) / 4 :
                       speed == "fast" ? (milliseconds * 11 + 10) / 20 : milliseconds;
    return 0 < scaled ? scaled : 1;
}

bool Settings::fullscreen(void)
{
    return gameFullscreen;
}

void Settings::setFullscreen(bool value)
{
    gameFullscreen = value;
}

bool Settings::accel(void)
{
    return gameAccel;
}

bool Settings::music(void)
{
    return 0 < gameMusicVolume;
}

int Settings::windowScale(void)
{
    return gameWindowScale;
}

void Settings::setWindowScale(int value)
{
    gameWindowScale = normalizedWindowScale(value);
}

void Settings::setMusic(bool value)
{
    gameMusicVolume = value ? 100 : 0;
}

int Settings::musicVolume(void)
{
    return gameMusicVolume;
}

void Settings::setMusicVolume(int value)
{
    gameMusicVolume = normalizedVolume(value);
}

bool Settings::sound(void)
{
    return 0 < gameEffectsVolume || 0 < gameVoiceVolume;
}

void Settings::setSound(bool value)
{
    gameEffectsVolume = gameVoiceVolume = value ? 100 : 0;
}

int Settings::effectsVolume(void)
{
    return gameEffectsVolume;
}

void Settings::setEffectsVolume(int value)
{
    gameEffectsVolume = normalizedVolume(value);
}

int Settings::voiceVolume(void)
{
    return gameVoiceVolume;
}

void Settings::setVoiceVolume(int value)
{
    gameVoiceVolume = normalizedVolume(value);
}

int Settings::mixerVolume(int value)
{
    return (normalizedVolume(value) * 128 + 50) / 100;
}

bool Settings::soundGuardianRules(void)
{
    return guardianRulesSound;
}

void Settings::setSoundGuardianRules(bool value)
{
    guardianRulesSound = value;
}

//////////////////////////////////////////////////////
std::string Settings::fileSaveGame(void)
{
    return fileSave("game.sav");
}

std::string Settings::shareDir(void)
{
    return Systems::homeDirectory(Application::domain());
}

std::string Settings::fileSave(const std::string & file)
{
    return Systems::concatePath(Systems::homeDirectory(Application::domain()), file);
}

bool Settings::storeCache(void)
{
    return Systems::environment("RUNEWARS_STORE_CACHE");
}
