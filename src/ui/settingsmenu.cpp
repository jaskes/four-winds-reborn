/***************************************************************************
 *   Four Winds Reborn settings menu                                       *
 ***************************************************************************/

#include <algorithm>

#include "swe/swe_music.h"

#include "aiprofile.h"
#include "gametheme.h"
#include "dialogs.h"
#include "runewars.h"
#include "settings.h"
#include "settingsmenu.h"

namespace
{
    const char* difficultyLabel(AI::Difficulty difficulty)
    {
        switch(difficulty)
        {
            case AI::Difficulty::Training: return _("Training");
            case AI::Difficulty::Easy: return _("Easy");
            case AI::Difficulty::Hard: return _("Hard");
            case AI::Difficulty::Unfair: return _("Unfair");
            default: return _("Normal");
        }
    }

    const char* difficultyDescription(AI::Difficulty difficulty)
    {
        switch(difficulty)
        {
            case AI::Difficulty::Training:
                return _("AI defends and heals, but does not invade or use offensive spells.");
            case AI::Difficulty::Easy:
                return _("Forgiving opponents use simpler plans and conserve fewer runes.");
            case AI::Difficulty::Hard:
                return _("Deeper planning and stronger coordination. Battle forecasts are hidden.");
            case AI::Difficulty::Unfair:
                return _("Brutal AI with explicit economy bonuses. No hidden information or rigged luck.");
            default:
                return _("The intended fair challenge with the complete AI toolset.");
        }
    }

    Texture scaledDifficultyPortrait(const char* id, const Size & size)
    {
        Texture scaled = Display::createTexture(size);
        const Texture & source = GameTheme::texture(id);
        if(source.isValid())
            Display::renderTexture(source, source.rect(), scaled, scaled.rect());
        return scaled;
    }
}

SettingsMenuScreen::SettingsMenuScreen(const std::string & program) :
    JsonWindow("screen_settings.json", nullptr), selected(0),
    language(Settings::language()), initialContentTheme(Settings::contentTheme()),
    contentPackageIndex(0), gameSpeed(Settings::gameSpeed()),
    aiDifficulty(Settings::aiDifficulty()),
    runeGameRuleset(Settings::runeGameRuleset()),
    matchMode(Settings::matchMode()),
    musicVolume(Settings::musicVolume()), effectsVolume(Settings::effectsVolume()),
    voiceVolume(Settings::voiceVolume()),
    guardianVoices(Settings::soundGuardianRules()),
    fullscreen(Settings::fullscreen()), windowScale(Settings::windowScale())
{
    leftPanel = JsonUnpack::rect(jobject, "panel:left", Rect(0, 0, 224, 768));
    rightPanel = JsonUnpack::rect(jobject, "panel:right", Rect(800, 0, 224, 768));
    menuArea = JsonUnpack::rect(jobject, "menu:area", Rect(812, 196, 200, 440));
    difficultyArea = JsonUnpack::rect(jobject, "difficulty:area", Rect(14, 88, 196, 530));
    difficultyPortraitArea = JsonUnpack::rect(
        jobject, "difficulty:portrait", Rect(34, 132, 156, 156));
    itemHeight = jobject.getInteger("menu:item_height", 54);
    itemGap = jobject.getInteger("menu:item_gap", 8);

    titleFont = jobject.getString("font:title", "dejavus40");
    menuFont = jobject.getString("font:menu", "dejavus22");
    smallFont = jobject.getString("font:small", "terminus14");

    panelColor = GameTheme::jsonColor(jobject, "color:panel");
    panelBorderColor = GameTheme::jsonColor(jobject, "color:panel_border");
    dividerColor = GameTheme::jsonColor(jobject, "color:divider");
    titleColor = GameTheme::jsonColor(jobject, "color:title");
    textColor = GameTheme::jsonColor(jobject, "color:text");
    mutedColor = GameTheme::jsonColor(jobject, "color:muted");
    buttonColor = GameTheme::jsonColor(jobject, "color:button");
    buttonSelectedColor = GameTheme::jsonColor(jobject, "color:button_selected");
    buttonBorderColor = GameTheme::jsonColor(jobject, "color:button_border");
    buttonSelectedBorderColor = GameTheme::jsonColor(jobject, "color:button_selected_border");

    for(const char* id : { "difficulty_training", "difficulty_easy", "difficulty_normal",
                          "difficulty_hard", "difficulty_unfair" })
        difficultyPortraits.emplace_back(
            scaledDifficultyPortrait(id, difficultyPortraitArea.toSize()));

    const Size available = Display::usableBounds().toSize();
    for(const int candidate : { 75, 100, 125, 150, 175, 200 })
    {
        const Size size = windowSizeForScale(candidate);
        if(size.w <= available.w && size.h <= available.h)
            windowScales.push_back(candidate);
    }
    if(windowScales.empty()) windowScales.push_back(75);
    if(std::find(windowScales.begin(), windowScales.end(), windowScale) == windowScales.end())
    {
        windowScale = windowScales.front();
        for(const int candidate : windowScales)
            if(candidate <= Settings::windowScale()) windowScale = candidate;
    }

    for(InstalledContentPackage & package :
        ContentCatalog::discover(program, Application::domain()))
    {
        if(package.compatible) contentPackages.push_back(std::move(package));
        else VERBOSE("content package ignored: " << package.theme << ": " << package.error);
    }
    for(size_t index = 0; index < contentPackages.size(); ++index)
        if(contentPackages[index].theme == initialContentTheme)
            contentPackageIndex = static_cast<int>(index);

    addEntry(AIDifficulty);
    addEntry(RuneGameRules);
    addEntry(MatchMode);
    addEntry(Language);
    addEntry(ContentPackage);
    addEntry(GameSpeed);
    addEntry(MusicVolume);
    addEntry(EffectsVolume);
    addEntry(VoiceVolume);
    addEntry(GuardianVoices);
    addEntry(DisplayMode);
    addEntry(WindowSize);
    addEntry(Apply);
    addEntry(Back);

    setMouseTrack(true);
    setKeyHandle(true);
    setVisible(true);
}

void SettingsMenuScreen::addEntry(EntryKind kind)
{
    if(kind == AIDifficulty)
    {
        entries.push_back(Entry{ kind, difficultyArea });
        return;
    }

    int index = 0;
    for(const Entry & entry : entries)
        if(entry.kind != AIDifficulty) ++index;
    const int posy = menuArea.y + index * (itemHeight + itemGap);
    entries.push_back(Entry{ kind, Rect(menuArea.x, posy, menuArea.w, itemHeight) });
}

std::string SettingsMenuScreen::entryLabel(EntryKind kind) const
{
    switch(kind)
    {
        case AIDifficulty: return _("AI Difficulty");
        case RuneGameRules: return _("Rune Game Rules");
        case MatchMode: return _("Match Mode");
        case Language: return _("Language");
        case ContentPackage: return _("Content Package");
        case GameSpeed: return _("Game Speed");
        case MusicVolume: return _("Music");
        case EffectsVolume: return _("Sound Effects");
        case VoiceVolume: return _("Voices");
        case GuardianVoices: return _("Guardian Voices");
        case DisplayMode: return _("Display Mode");
        case WindowSize: return _("Window Size");
        case Apply: return _("Apply");
        case Back: return _("Back");
    }
    return std::string();
}

std::string SettingsMenuScreen::entryValue(EntryKind kind) const
{
    switch(kind)
    {
        case AIDifficulty: return difficultyLabel(aiDifficulty);
        case RuneGameRules: return runeGameRuleset == "quick" ? _("Quick") : _("Classic");
        case MatchMode:
            if(matchMode == "duel") return _("Duel");
            if(matchMode == "coalition") return _("Coalition");
            return _("Free for All");
        case Language: return language == "ru" ? _("Russian") : _("English");
        case ContentPackage:
            if(0 <= contentPackageIndex &&
               contentPackageIndex < static_cast<int>(contentPackages.size()))
                return _(contentPackages[contentPackageIndex].displayName().c_str());
            return _("Unavailable");
        case GameSpeed:
            if(gameSpeed == "classic") return _("Classic");
            if(gameSpeed == "fast") return _("Fast");
            return _("Normal");
        case MusicVolume:
        case EffectsVolume:
        case VoiceVolume: return std::to_string(volumeValue(kind)).append("%");
        case GuardianVoices: return guardianVoices ? _("ON") : _("OFF");
        case DisplayMode: return fullscreen ? _("Fullscreen") : _("Windowed");
        case WindowSize:
        {
            const Size size = windowSizeForScale(windowScale);
            return std::to_string(size.w).append(" x ").append(std::to_string(size.h));
        }
        default: break;
    }
    return std::string();
}

bool SettingsMenuScreen::isVolumeEntry(EntryKind kind) const
{
    return kind == MusicVolume || kind == EffectsVolume || kind == VoiceVolume;
}

int SettingsMenuScreen::volumeValue(EntryKind kind) const
{
    if(kind == MusicVolume) return musicVolume;
    if(kind == EffectsVolume) return effectsVolume;
    if(kind == VoiceVolume) return voiceVolume;
    return 0;
}

void SettingsMenuScreen::setVolumeValue(EntryKind kind, int value)
{
    value = std::max(0, std::min(100, value));
    if(kind == MusicVolume) musicVolume = value;
    else if(kind == EffectsVolume) effectsVolume = value;
    else if(kind == VoiceVolume) voiceVolume = value;
}

Size SettingsMenuScreen::windowSizeForScale(int value) const
{
    const Size logical = Display::size();
    return Size(logical.w * value / 100, logical.h * value / 100);
}

void SettingsMenuScreen::renderWindow(void)
{
    JsonWindow::renderWindow();

    renderColor(panelColor, leftPanel);
    renderColor(panelColor, rightPanel);

    renderLine(panelBorderColor, Point(leftPanel.x + leftPanel.w - 1, 0),
               Point(leftPanel.x + leftPanel.w - 1, height() - 1));
    renderLine(dividerColor, Point(leftPanel.x + leftPanel.w - 3, 0),
               Point(leftPanel.x + leftPanel.w - 3, height() - 1));
    renderLine(panelBorderColor, Point(rightPanel.x, 0), Point(rightPanel.x, height() - 1));
    renderLine(dividerColor, Point(rightPanel.x + 2, 0),
               Point(rightPanel.x + 2, height() - 1));

    const FontRender & title = GameTheme::fontRender(titleFont);
    const FontRender & menu = GameTheme::fontRender(menuFont);
    const FontRender & difficultyName = GameTheme::fontRender("dejavus18");
    const FontRender & small = GameTheme::fontRender(smallFont);
    const FontRender & footer = GameTheme::fontRender("dejavus12");
    const int leftCenter = leftPanel.x + leftPanel.w / 2;
    const int rightCenter = rightPanel.x + rightPanel.w / 2;

    const bool difficultySelected = !entries.empty() &&
        entries[selected].kind == AIDifficulty;
    renderColor(difficultySelected ? buttonSelectedColor : buttonColor, difficultyArea);
    renderRect(difficultySelected ? buttonSelectedBorderColor : buttonBorderColor,
               difficultyArea);
    if(difficultySelected)
        renderLine(buttonSelectedBorderColor,
                   Point(difficultyArea.x + 3, difficultyArea.y + 3),
                   Point(difficultyArea.x + 3,
                         difficultyArea.y + difficultyArea.h - 4));

    renderText(small, _("AI DIFFICULTY"), panelBorderColor,
               Point(leftCenter, difficultyArea.y + 17), AlignCenter);
    renderLine(panelBorderColor, Point(difficultyArea.x + 18, difficultyArea.y + 43),
               Point(difficultyArea.x + difficultyArea.w - 18,
                     difficultyArea.y + 43));

    const int portraitIndex = static_cast<int>(aiDifficulty);
    renderColor(Color::Black, difficultyPortraitArea);
    renderRect(panelBorderColor, difficultyPortraitArea);
    if(0 <= portraitIndex && portraitIndex < static_cast<int>(difficultyPortraits.size()) &&
       difficultyPortraits[portraitIndex].isValid())
        renderTexture(difficultyPortraits[portraitIndex], difficultyPortraitArea.toPoint());

    const int selectorY = difficultyPortraitArea.y + difficultyPortraitArea.h + 30;
    renderText(difficultyName, "<", difficultySelected ? titleColor : textColor,
               Point(difficultyArea.x + 25, selectorY), AlignCenter, AlignCenter);
    renderText(difficultyName, difficultyLabel(aiDifficulty), titleColor,
               Point(leftCenter, selectorY), AlignCenter, AlignCenter);
    renderText(difficultyName, ">", difficultySelected ? titleColor : textColor,
               Point(difficultyArea.x + difficultyArea.w - 25, selectorY),
               AlignCenter, AlignCenter);

    int descriptionY = selectorY + 39;
    const int descriptionWidth = difficultyArea.w - 28;
    for(const std::string & line : small.splitStringWidth(
            difficultyDescription(aiDifficulty), descriptionWidth))
    {
        renderText(small, line, textColor, Point(leftCenter, descriptionY), AlignCenter);
        descriptionY += small.lineSkipHeight();
    }

    renderLine(dividerColor,
               Point(difficultyArea.x + 18, difficultyArea.y + difficultyArea.h - 82),
               Point(difficultyArea.x + difficultyArea.w - 18,
                     difficultyArea.y + difficultyArea.h - 82));
    renderText(small, _("FOR NEW GAMES"), panelBorderColor,
               Point(leftCenter, difficultyArea.y + difficultyArea.h - 66), AlignCenter);
    int saveNoteY = difficultyArea.y + difficultyArea.h - 47;
    for(const std::string & line : footer.splitStringWidth(
            _("Saved games are unchanged."), difficultyArea.w - 28))
    {
        renderText(footer, line, mutedColor, Point(leftCenter, saveNoteY), AlignCenter);
        saveNoteY += footer.lineSkipHeight();
    }

    renderText(small, _("SETTINGS"), panelBorderColor, Point(rightCenter, 139), AlignCenter);
    renderLine(panelBorderColor, Point(rightPanel.x + 28, 164),
               Point(rightPanel.x + rightPanel.w - 28, 164));

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        const Entry & entry = entries[ii];
        if(entry.kind == AIDifficulty) continue;
        const bool isSelected = static_cast<int>(ii) == selected;
        const Color fill = isSelected ? buttonSelectedColor : buttonColor;
        const Color border = isSelected ? buttonSelectedBorderColor : buttonBorderColor;
        const Color labelColor = isSelected ? titleColor : textColor;
        const std::string value = entryValue(entry.kind);

        renderColor(fill, entry.area);
        renderRect(border, entry.area);

        if(isSelected)
            renderLine(buttonSelectedBorderColor,
                       Point(entry.area.x + 3, entry.area.y + 3),
                       Point(entry.area.x + 3, entry.area.y + entry.area.h - 4));

        if(isVolumeEntry(entry.kind))
        {
            renderText(small, entryLabel(entry.kind), labelColor,
                       Point(entry.area.x + 10, entry.area.y + 4));
            renderText(small, value, isSelected ? titleColor : mutedColor,
                       Point(entry.area.x + entry.area.w - 10, entry.area.y + 4), AlignRight);

            const Rect track(entry.area.x + 12, entry.area.y + entry.area.h - 10,
                             entry.area.w - 24, 6);
            renderColor(mutedColor, track);
            const int fillWidth = track.w * volumeValue(entry.kind) / 100;
            if(0 < fillWidth)
                renderColor(isSelected ? buttonSelectedBorderColor : panelBorderColor,
                            Rect(track.x, track.y, fillWidth, track.h));
        }
        else if(value.empty())
        {
            renderText(menu, entryLabel(entry.kind), labelColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + entry.area.h / 2),
                       AlignCenter, AlignCenter);
        }
        else
        {
            renderText(menu, entryLabel(entry.kind), labelColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + 1), AlignCenter);
            renderText(small, value, isSelected ? titleColor : mutedColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + 27), AlignCenter);
        }
    }

    renderText(small, _("LEFT / RIGHT TO ADJUST"), mutedColor,
               Point(rightCenter, height() - 27), AlignCenter);
}

bool SettingsMenuScreen::adjustSelected(int direction)
{
    if(selected < 0 || static_cast<size_t>(selected) >= entries.size()) return false;
    const EntryKind kind = entries[selected].kind;

    if(kind == AIDifficulty)
        aiDifficulty = direction < 0 ? AI::previousDifficulty(aiDifficulty) :
                                      AI::nextDifficulty(aiDifficulty);
    else if(kind == RuneGameRules)
        runeGameRuleset = runeGameRuleset == "quick" ? "classic" : "quick";
    else if(kind == MatchMode)
    {
        static const std::vector<std::string> modes = { "classic", "duel", "coalition" };
        auto it = std::find(modes.begin(), modes.end(), matchMode);
        int index = it == modes.end() ? 0 : static_cast<int>(std::distance(modes.begin(), it));
        index = (index + (direction < 0 ? -1 : 1) + static_cast<int>(modes.size())) %
                static_cast<int>(modes.size());
        matchMode = modes[index];
    }
    else if(isVolumeEntry(kind))
        setVolumeValue(kind, volumeValue(kind) + (direction < 0 ? -10 : 10));
    else if(kind == Language)
        language = language == "ru" ? "en" : "ru";
    else if(kind == ContentPackage)
    {
        if(contentPackages.empty()) return false;
        contentPackageIndex = (contentPackageIndex + (direction < 0 ? -1 : 1) +
            static_cast<int>(contentPackages.size())) %
            static_cast<int>(contentPackages.size());
    }
    else if(kind == GameSpeed)
    {
        static const std::vector<std::string> speeds = { "classic", "normal", "fast" };
        auto it = std::find(speeds.begin(), speeds.end(), gameSpeed);
        int index = it == speeds.end() ? 0 : static_cast<int>(std::distance(speeds.begin(), it));
        index = (index + (direction < 0 ? -1 : 1) + static_cast<int>(speeds.size())) %
                static_cast<int>(speeds.size());
        gameSpeed = speeds[index];
    }
    else if(kind == GuardianVoices)
        guardianVoices = !guardianVoices;
    else if(kind == DisplayMode)
        fullscreen = !fullscreen;
    else if(kind == WindowSize)
    {
        auto it = std::find(windowScales.begin(), windowScales.end(), windowScale);
        int index = it == windowScales.end() ? 0 :
            static_cast<int>(std::distance(windowScales.begin(), it));
        index = (index + (direction < 0 ? -1 : 1) +
                 static_cast<int>(windowScales.size())) %
                static_cast<int>(windowScales.size());
        windowScale = windowScales[index];
    }
    else
        return false;

    playSound("button");
    renderWindow();
    return true;
}

bool SettingsMenuScreen::activateSelected(void)
{
    if(selected < 0 || static_cast<size_t>(selected) >= entries.size()) return false;

    switch(entries[selected].kind)
    {
        case Language:
        case ContentPackage:
        case AIDifficulty:
        case RuneGameRules:
        case MatchMode:
        case GameSpeed:
        case MusicVolume:
        case EffectsVolume:
        case VoiceVolume:
        case GuardianVoices:
        case DisplayMode:
        case WindowSize: return adjustSelected(1);
        case Apply:
        {
            const bool previousFullscreen = Display::isFullscreenWindow();
            const Size previousWindowSize = Display::device();

            if(Display::isFullscreenWindow() != fullscreen &&
               !Display::setFullscreenMode(fullscreen))
            {
                fullscreen = Display::isFullscreenWindow();
                MessageBox(_("Settings"), _("Could not switch display mode."),
                           *this, false).exec();
                renderWindow();
                return true;
            }

            if(!fullscreen && !Display::resize(windowSizeForScale(windowScale)))
            {
                if(Display::isFullscreenWindow() != previousFullscreen)
                    Display::setFullscreenMode(previousFullscreen);
                if(!previousFullscreen) Display::resize(previousWindowSize);
                fullscreen = Display::isFullscreenWindow();
                MessageBox(_("Settings"), _("Could not resize the game window."),
                           *this, false).exec();
                renderWindow();
                return true;
            }

            Settings::setLanguage(language);
            if(0 <= contentPackageIndex &&
               contentPackageIndex < static_cast<int>(contentPackages.size()))
                Settings::setContentTheme(contentPackages[contentPackageIndex].theme);
            Settings::setGameSpeed(gameSpeed);
            Settings::setAIDifficulty(aiDifficulty);
            Settings::setRuneGameRuleset(runeGameRuleset);
            Settings::setMatchMode(matchMode);
            Settings::setMusicVolume(musicVolume);
            Settings::setEffectsVolume(effectsVolume);
            Settings::setVoiceVolume(voiceVolume);
            Settings::setSoundGuardianRules(guardianVoices);
            Settings::setFullscreen(fullscreen);
            Settings::setWindowScale(windowScale);

            std::string error;
            if(!Settings::write(&error))
            {
                MessageBox(_("Settings"),
                           StringFormat(_("Could not save settings: %1")).arg(error),
                           *this, false).exec();
                renderWindow();
                return true;
            }

            if(Settings::contentTheme() != initialContentTheme)
            {
                MessageBox(_("Content Package"),
                           _("Restart the game to activate the selected content package."),
                           *this, false).exec();
            }

#ifndef SWE_DISABLE_AUDIO
            if(Settings::music())
                Music::volume(Settings::mixerVolume(Settings::musicVolume()));
            else
                Music::reset();
            Sound::stop(-1);
#endif
            setResultCode(Menu::MainMenu);
            setVisible(false);
            return true;
        }
        case Back:
            setResultCode(Menu::MainMenu);
            setVisible(false);
            return true;
    }

    playSound("button");
    renderWindow();
    return true;
}

bool SettingsMenuScreen::selectNext(int direction)
{
    if(entries.empty()) return false;
    selected = (selected + direction + static_cast<int>(entries.size())) %
               static_cast<int>(entries.size());
    renderWindow();
    return true;
}

bool SettingsMenuScreen::keyPressEvent(const KeySym & key)
{
    switch(key.keycode())
    {
        case Key::UP: return selectNext(-1);
        case Key::DOWN: return selectNext(1);
        case Key::LEFT: return adjustSelected(-1);
        case Key::RIGHT: return adjustSelected(1);
        case Key::RETURN:
        case Key::SPACE: return activateSelected();
        case Key::ESCAPE:
            selected = static_cast<int>(entries.size()) - 1;
            return activateSelected();
        default: break;
    }
    return false;
}

bool SettingsMenuScreen::mouseClickEvent(const ButtonsEvent & coords)
{
    if(!coords.isButtonLeft() && !coords.isButtonRight()) return false;

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        if(coords.isClick(entries[ii].area))
        {
            selected = static_cast<int>(ii);
            if(entries[ii].kind == AIDifficulty)
            {
                const int localX = coords.release().position().x - entries[ii].area.x;
                return adjustSelected(coords.isButtonRight() ||
                                      localX < entries[ii].area.w / 2 ? -1 : 1);
            }
            if(isVolumeEntry(entries[ii].kind))
            {
                if(!coords.isButtonLeft()) return true;

                const Rect & area = entries[ii].area;
                const int left = area.x + 12;
                const int width = area.w - 24;
                const int mouseX = coords.release().position().x;
                int volume = width <= 0 ? 0 : (mouseX - left) * 100 / width;
                volume = std::max(0, std::min(100, volume));
                volume = ((volume + 2) / 5) * 5;
                setVolumeValue(entries[ii].kind, volume);
                playSound("button");
                renderWindow();
                return true;
            }

            if(coords.isButtonRight())
            {
                switch(entries[ii].kind)
                {
                    case Language:
                    case ContentPackage:
                    case AIDifficulty:
                    case RuneGameRules:
                    case GameSpeed:
                    case GuardianVoices:
                    case DisplayMode:
                    case WindowSize: return adjustSelected(-1);
                    default: return false;
                }
            }
            return activateSelected();
        }
    }
    return false;
}

bool SettingsMenuScreen::mouseMotionEvent(const Point & pos, u32 buttons)
{
    (void) buttons;

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        if(entries[ii].area & pos)
        {
            if(selected != static_cast<int>(ii))
            {
                selected = static_cast<int>(ii);
                renderWindow();
            }
            return true;
        }
    }
    return false;
}
