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

#include <clocale>
#include <algorithm>
#include <exception>

#include "gameplayrng.h"
#include "contentcatalog.h"
#include "gametheme.h"
#include "settings.h"
#include "intropart.h"
#include "mainmenu.h"
#include "settingsmenu.h"
#include "encyclopedia.h"
#include "loadrecovery.h"
#include "replaybrowser.h"
#include "recovery.h"
#include "replay.h"
#include "replayfiles.h"
#include "savegames.h"
#include "selectperson.h"
#include "showplayers.h"
#include "mahjongpart.h"
#include "mahjongsummarypart.h"
#include "adventurepart.h"
#include "battlesummarypart.h"
#include "gamesummarypart.h"
#include "crashreport.h"

#include "runewars.h"

#ifndef FOUR_WINDS_VERSION
#define FOUR_WINDS_VERSION "unknown"
#endif

#ifndef FOUR_WINDS_BUILD_DATE
#define FOUR_WINDS_BUILD_DATE "unknown"
#endif

#ifndef FOUR_WINDS_RELEASE_TAG
#define FOUR_WINDS_RELEASE_TAG ""
#endif

#ifndef FOUR_WINDS_GAME_REVISION
#define FOUR_WINDS_GAME_REVISION "unknown"
#endif

class GameLoadScreen : public DisplayWindow
{
    std::string sourceFile;
    std::string primaryFile;
    bool        promoteRecovery;

protected:
    void windowCreateEvent(void) override
    {
        CrashReport::breadcrumb(std::string("Game load stage=begin source=").append(sourceFile)
            .append(" promote=").append(promoteRecovery ? "true" : "false"));
        if(GameData::loadGame(sourceFile))
        {
            if(promoteRecovery)
            {
                std::string error;
                if(!Recovery::promoteSave(sourceFile, primaryFile, &error))
                    MessageBox(_("Recovery Warning"),
                               StringFormat(_("The checkpoint was loaded, but the current autosave could not be replaced: %1"))
                                   .arg(error), *this, false).exec();
            }
	    CrashReport::breadcrumb(std::string("Game load stage=end status=ok source=").append(sourceFile));
	    setResultCode(1);
        }
        else
        {
	    CrashReport::breadcrumb(std::string("Game load stage=end status=failed source=").append(sourceFile));
	    MessageBox(_("Load Game"), _("The saved game could not be loaded. The file was left untouched."),
	               *this, false).exec();
	    setResultCode(0);
        }

	// close screen
	setVisible(false);
    }

public:
    GameLoadScreen(const std::string & source, const std::string & primary, bool promote) :
        DisplayWindow(Color::Black), sourceFile(source), primaryFile(primary), promoteRecovery(promote) {}
};

std::string Application::domain(void)
{
    return "four-winds-reborn";
}

std::string Application::name(void)
{
    return "Four Winds Reborn";
}

std::string Application::version(void)
{
    const std::string releaseTag = FOUR_WINDS_RELEASE_TAG;
    return releaseTag.empty() ? std::string("v").append(FOUR_WINDS_VERSION).append("-dev") : releaseTag;
}

std::string Application::buildDate(void)
{
    return FOUR_WINDS_BUILD_DATE;
}

std::string Application::revision(void)
{
    std::string revision = FOUR_WINDS_GAME_REVISION;
    const std::string dirtySuffix = "-dirty";
    const bool dirty = revision.size() > dirtySuffix.size() &&
        revision.compare(revision.size() - dirtySuffix.size(), dirtySuffix.size(), dirtySuffix) == 0;

    if(dirty) revision.resize(revision.size() - dirtySuffix.size());
    if(revision != "unknown" && revision.size() > 7) revision.resize(7);
    if(dirty) revision.append(dirtySuffix);
    return revision;
}

bool Application::isReleaseBuild(void)
{
    return std::string(FOUR_WINDS_RELEASE_TAG).size() > 0;
}

namespace
{
const char* menuName(int menu)
{
    switch(menu)
    {
        case Menu::GameExit: return "GameExit";
        case Menu::MainMenu: return "MainMenu";
        case Menu::SelectPerson: return "SelectPerson";
        case Menu::ShowPlayers: return "ShowPlayers";
        case Menu::MahjongPart: return "Mahjong";
        case Menu::MahjongSummaryPart: return "MahjongSummary";
        case Menu::AdventurePart: return "Adventure";
        case Menu::BattleSummaryPart: return "BattleSummary";
        case Menu::GameSummaryPart: return "GameSummary";
        case Menu::MahjongInitPart: return "MahjongInit";
        case Menu::GameLoadPart: return "GameLoad";
        case Menu::LoadRecovery: return "LoadRecovery";
        case Menu::SettingsMenu: return "Settings";
        case Menu::Encyclopedia: return "Encyclopedia";
        case Menu::ReplayLibrary: return "ReplayLibrary";
        default: return "Unknown";
    }
}

std::string normalizedThemeAlias(const std::string & value)
{
    return String::toLower(value) == "default" ? "classic" : value;
}
}

RuneWarsClient::RuneWarsClient(int argc, char** argv) :
    Application(argv[0], false, Size(0, 0), "classic"), part(0),
    themeCommandOverride(false)
{
    CrashReport::breadcrumb("Startup stage=engine_log_init status=begin");
    LogWrapper::init(domain(), argv[0]);
    CrashReport::breadcrumb("Startup stage=engine_log_init status=ok");

    // make params theme
#ifdef RUNEWARS_THEME
    theme = RUNEWARS_THEME;
#endif
    theme = Settings::preferredContentTheme(theme);
    if(Systems::environment("RUNEWARS_THEME"))
	{
	theme = Systems::environment("RUNEWARS_THEME");
	themeCommandOverride = true;
	}

    savefile = Settings::fileSaveGame();
    parseCommandOptions(argc, argv);
    theme = normalizedThemeAlias(theme);

    // A stale user preference must never make the game unbootable. Explicit
    // command-line/environment themes remain strict diagnostic overrides.
    if(!themeCommandOverride)
    {
	std::string error;
	if(!ContentCatalog::isAvailable(program, domain(), theme, &error))
	{
	    ERROR("stored content package unavailable: " << theme << ": " << error
	          << "; falling back to classic");
	    theme = "classic";
	}
    }
}

void RuneWarsClient::parseCommandOptions(int argc, char** argv)
{
    int opt;

    while((opt = Systems::GetCommandOptions(argc, argv, "hft:g:p:s:")) != -1)
    switch(opt)
    {
	case 'f':
	    fullscreen = true;
	    break;

        case 't':
	    if(Systems::GetOptionsArgument())
	    {
		theme = Systems::GetOptionsArgument();
		themeCommandOverride = true;
	    }
            break;
#ifdef BUILD_DEBUG
        case 'p':
	    if(Systems::GetOptionsArgument())
		part = String::toInt(Systems::GetOptionsArgument());
	    break;
#endif

        case 's':
	    if(Systems::GetOptionsArgument())
		savefile = Systems::GetOptionsArgument();
	    break;

        case 'g':
	    if(Systems::GetOptionsArgument())
	    {
		StringList list = String::split(Systems::GetOptionsArgument(), 'x');

		if(2 == list.size())
		    geometry = Size(String::toInt(list.front()), String::toInt(list.back()));
		else
		    ERROR("unknown geometry format");
	    }
            break;

        case '?':
        case 'h':
	    COUT("Usage: " << argv[0] << " [OPTIONS]\n" <<
		"\t-f\tfullscreen\n" <<
		"\t-g\twindow geometry (1024x768 is default)\n" <<
		"\t-t\ttheme name\n" <<
		"\t-s\tload savefile\n" <<
		"\t-h\tprint this help and exit\n");
	    exit(0);

        default:  break;
    }
}

bool RuneWarsClient::translationInit(void)
{
    Translation::reset();
    Translation::setStripContext('|');

    const std::string language = String::toLower(Settings::language());
    const bool sourceEnglish = language.empty() || language == "c" || language == "posix" ||
        language == "en" || language == "english" || language.rfind("en_", 0) == 0 ||
        language.rfind("en-", 0) == 0 || language.rfind("english_", 0) == 0;
    if(sourceEnglish)
    {
	Translation::setLanguage("en");
        VERBOSE("using built-in English strings");
        return true;
    }

    std::string lang = StringFormat("%1.mo").arg(Settings::language());
    std::string path;

    if(Translation::bindDomain(domain(), GameTheme::readResource(lang, &path)))
    {
	Translation::setLanguage(Settings::language());
        VERBOSE("loaded from: " << path);
        Translation::setDomain(domain());
        return true;
    }

    return false;
}

Person fixedEmptyPerson(const Person & cur)
{
    // fix random clan or avatar
    if(! cur.clan.isValid() && ! cur.avatar.isValid())
    {
        Avatar avatar = Avatar::random();
        const AvatarInfo & hi = GameData::avatarInfo(avatar);
        auto clan = GameplayRng::choose(hi.clans.begin(), hi.clans.end());
        return Person(avatar, *clan, cur.wind);
    }
    else
    if(! cur.clan.isValid())
    {
        const AvatarInfo & hi = GameData::avatarInfo(cur.avatar);
        auto clan = GameplayRng::choose(hi.clans.begin(), hi.clans.end());
        return Person(cur.avatar, *clan, cur.wind);
    }
    else
    if(! cur.avatar.isValid())
    {
        Avatars avatars = GameData::avatarsOfClan(cur.clan);
        GameplayRng::shuffle(avatars.begin(), avatars.end());
        return Person(avatars.front(), cur.clan, cur.wind);
    }

    return cur;
}

bool RuneWarsClient::exec(void)
{
    CrashReport::breadcrumb("Startup stage=engine_init status=begin");
    if(! Engine::init())
    {
        CrashReport::breadcrumb("Startup stage=engine_init status=failed");
        return false;
    }
    CrashReport::breadcrumb("Startup stage=engine_init status=ok");

    if(Systems::isEmbeded())
	Display::hardwareCursorHide();

    CrashReport::breadcrumb(std::string("Startup stage=theme_init status=begin theme=").append(theme));
    if(! GameTheme::init(*this))
    {
	CrashReport::breadcrumb(std::string("Startup stage=theme_init status=failed theme=").append(theme));
	Size wsz(640, 480);

        if(Display::init("Error", wsz, wsz, Settings::fullscreen(), Settings::accel(), false))
        {
            DisplayWindow win1(Color::DarkSlateGray);
            TermGUI::MessageBox(_("Error"), _("load theme error, see log for details"),
                                TermGUI::ButtonOk, systemFont(), & win1).exec();
        }
        return false;
    }
    CrashReport::breadcrumb(std::string("Startup stage=theme_init status=ok theme=").append(theme));

    // Player sessions retain their complete deterministic journal. Headless
    // simulations and tests keep the bounded default unless they explicitly
    // opt into full replay capture.
    Replay::setActionJournalLimit(0);

    CrashReport::breadcrumb("Startup stage=translation_init status=begin");
    if(! translationInit())
	{
	CrashReport::breadcrumb("Startup stage=translation_init status=failed");
	ERROR("translation not loaded");
	}
    else
	{
	GameData::retranslateThemeData();
	CrashReport::breadcrumb("Startup stage=translation_init status=ok");
	}

    bool showIntro = true;
#ifdef BUILD_DEBUG
    // Direct part launches are diagnostic shortcuts and should stay immediate.
    showIntro = part == 0;
#endif
    if(showIntro && IntroScreen::supportsLanguage(Settings::language()))
    {
	CrashReport::breadcrumb(std::string("Startup stage=intro status=begin language=")
	    .append(Settings::language()));
	IntroScreen().exec();
	CrashReport::breadcrumb("Startup stage=intro status=end");
    }

    int menu = Menu::MainMenu;
    std::string pendingLoadFile = savefile;
    bool promoteRecovery = false;
    Person selectedPerson;

#ifdef BUILD_DEBUG
    if(part)
    {
	if(! Systems::isFile(savefile))
	{
	    ERROR("savefile not found: " << savefile);
	}
	else
	if(GameData::loadGame(savefile))
	{
            selectedPerson = GameData::myPerson();

	    switch(part)
	    {
		case 1:  GameData::setGamePart(Menu::MahjongPart); break;
		case 2:  GameData::setGamePart(Menu::MahjongSummaryPart); break;
		case 3:  GameData::setGamePart(Menu::AdventurePart); break;
		case 4:  GameData::setGamePart(Menu::BattleSummaryPart); break;
		case 5:  GameData::setGamePart(Menu::GameSummaryPart); break;
		default: break;
	    }

	    switch(part)
	    {
		case 1:  menu = MahjongPartScreen().exec(); break;
		case 2:  menu = MahjongSummaryPartScreen().exec(); break;
		case 3:  menu = AdventurePartScreen(selectedPerson.avatar).exec(); break;
		case 4:  menu = BattleSummaryScreen().exec(); break;
		case 5:  menu = GameSummaryScreen().exec(); break;
		default: break;
	    }
	}
    }
#endif

    while(menu != Menu::GameExit)
    {
	const int activeMenu = menu;
	CrashReport::breadcrumb(std::string("Screen stage=enter name=").append(menuName(activeMenu))
	    .append(" id=").append(String::number(activeMenu)));
	switch(menu)
	{
	    case Menu::MainMenu:
	    {
		pendingLoadFile = savefile;
		promoteRecovery = false;
		const bool recoveryExists = !Recovery::inspectCheckpoints(Recovery::defaultDirectory()).empty();
		const bool manualSaveExists = SaveGames::hasManualSaves();
		const bool saveExists = Systems::isFile(savefile);
		const bool saveValid = saveExists && Recovery::validateSaveFile(savefile);
		menu = MainMenuScreen(saveExists, saveValid,
		                            recoveryExists || manualSaveExists,
		                            ReplayFiles::hasReplays()).exec();
	    }
	    break;

	    case Menu::LoadRecovery:
	    {
		LoadRecoveryScreen screen(savefile);
		menu = screen.exec();
		if(menu == Menu::GameLoadPart)
		{
		    pendingLoadFile = screen.loadFile();
		    promoteRecovery = screen.shouldPromoteRecovery();
		}
	    }
	    break;

	    case Menu::SettingsMenu:
	    {
		const std::string previousLanguage = Settings::language();
		menu = SettingsMenuScreen(std::string(program ? program : "")).exec();
		if(previousLanguage != Settings::language())
		{
		    if(!translationInit())
		    {
			ERROR("translation not loaded after settings change");
		    }
		    else
		    {
			GameData::retranslateThemeData();
		    }
		}
	    }
	    break;

	    case Menu::Encyclopedia:
		menu = EncyclopediaScreen().exec();
	    break;

	    case Menu::ReplayLibrary:
		menu = ReplayBrowserScreen().exec();
	    break;

	    case Menu::SelectPerson:
	    {
		SelectPersonScreen scr;
		menu = scr.exec();
		GameplayRng::seedFromEntropy();
		CrashReport::breadcrumb(std::string("Gameplay RNG stage=new_game algorithm=")
		    .append(GameplayRng::Algorithm)
		    .append(" state=").append(std::to_string(GameplayRng::state())));
		selectedPerson = fixedEmptyPerson(scr.selectedPerson());
	    }
	    break;

	    case Menu::ShowPlayers:
		// Settings stores the default for future games. Once initialization
		// completes, the selected value lives in the save and is not changed
		// by later edits to settings.json.
		GameData::setAIDifficulty(Settings::aiDifficulty());
		GameData::initPersons(selectedPerson);
		menu = ShowPlayersScreen().exec();
	    break;

	    case Menu::MahjongInitPart:
		menu = GameData::initMahjong() ? Menu::MahjongPart : Menu::GameSummaryPart;
	    break;

	    case Menu::MahjongPart:
		menu = MahjongPartScreen().exec();
	    break;

	    case Menu::MahjongSummaryPart:
		//GameData::saveGame();
		menu = MahjongSummaryPartScreen().exec();
	    break;

	    case Menu::AdventurePart:
		menu = GameData::initAdventure() ?
			AdventurePartScreen(selectedPerson.avatar).exec() : Menu::GameSummaryPart;
	    break;

	    case Menu::BattleSummaryPart:
//		GameData::saveGame();
		menu = BattleSummaryScreen().exec();
	    break;

	    case Menu::GameSummaryPart:
		menu = GameSummaryScreen().exec();
	    break;

	    case Menu::GameLoadPart:
		if(GameLoadScreen(pendingLoadFile, savefile, promoteRecovery).exec())
		{
		    selectedPerson = GameData::myPerson();
		    menu = GameData::loadedGamePart();
		}
		else
		{
		    menu = Menu::MainMenu;
		}
		pendingLoadFile = savefile;
		promoteRecovery = false;
	    break;

	    default:
		menu = Menu::GameExit;
	    break;
	}

	CrashReport::breadcrumb(std::string("Screen stage=exit name=").append(menuName(activeMenu))
	    .append(" next=").append(menuName(menu))
	    .append(" next_id=").append(String::number(menu)));

	Tools::delay(100);
    }

    CrashReport::breadcrumb("Startup stage=engine_quit status=begin");
    GameTheme::clear();
    Engine::quit();
    CrashReport::breadcrumb("Startup stage=engine_quit status=ok");

    return true;
}

#ifndef RUNEWARS_NO_MAIN
int main(int argc, char **argv)
{
    Systems::setLocale(LC_ALL, "");
    Systems::setLocale(LC_NUMERIC, "C");
#if defined(ANDROID)
    // Android packages themes as virtual APK assets. Load the generated
    // manifest before content discovery so the normal catalog and theme
    // loading paths can treat those assets like a read-only filesystem.
    Systems::assetsInit();
#endif
    CrashReport::install(Application::domain());
    CrashReport::breadcrumb("Startup stage=process status=begin");

    int result = EXIT_SUCCESS;

    try
    {
	RuneWarsClient client(argc, argv);

	if(! client.exec())
	    result = EXIT_FAILURE;
    }
    catch(const Engine::exception & exception)
    {
	if(exception.message() != "SDL_QUIT")
	{
	    CrashReport::reportException(std::string("SWE exception in ")
	        .append(exception.function()).append(": ").append(exception.message()));
	    result = EXIT_FAILURE;
	}
    }
    catch(const std::exception & exception)
    {
	CrashReport::reportException(std::string("C++ exception: ").append(exception.what()));
	result = EXIT_FAILURE;
    }
    catch(...)
    {
	CrashReport::reportException("unknown C++ exception");
	result = EXIT_FAILURE;
    }

    CrashReport::shutdown();

    return result;
}
#endif
