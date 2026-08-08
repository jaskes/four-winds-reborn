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

#ifndef _RWNA_SETTINGS_
#define _RWNA_SETTINGS_

#include <string>

namespace AI
{
    enum class Difficulty;
}

#define FORMAT_VERSION_20200321	20200321
#define FORMAT_VERSION_CURRENT	FORMAT_VERSION_20200321
#define FORMAT_VERSION_LAST	FORMAT_VERSION_20200321

namespace Settings
{
    std::string		shareDir(void);
    std::string		fileSave(const std::string &);
    std::string		fileSaveGame(void);
    std::string		language(void);
    void                setLanguage(const std::string &);
    std::string         gameSpeed(void);
    void                setGameSpeed(const std::string &);
    std::string         contentTheme(void);
    void                setContentTheme(const std::string &);
    // Bootstrap read used before GameTheme is initialized. Theme defaults are
    // unavailable at that point, so only the per-user preference is read.
    std::string         preferredContentTheme(const std::string & fallback = "classic");
    AI::Difficulty      aiDifficulty(void);
    void                setAIDifficulty(AI::Difficulty);
    std::string         runeGameRuleset(void);
    void                setRuneGameRuleset(const std::string &);
    int                 presentationDelay(int);

    bool		read(void);
    bool                write(std::string* error = nullptr);
    bool		music(void);
    void                setMusic(bool);
    int                 musicVolume(void);
    void                setMusicVolume(int);
    bool		sound(void);
    void                setSound(bool);
    int                 effectsVolume(void);
    void                setEffectsVolume(int);
    int                 voiceVolume(void);
    void                setVoiceVolume(int);
    int                 mixerVolume(int);
    bool		soundGuardianRules(void);
    void                setSoundGuardianRules(bool);
    bool		fullscreen(void);
    void                setFullscreen(bool);
    int                 windowScale(void);
    void                setWindowScale(int);
    bool		accel(void);

    bool		storeCache(void);
}

#endif
