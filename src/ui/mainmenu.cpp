/***************************************************************************
 *   Four Winds Reborn main menu                                           *
 ***************************************************************************/

#include "gametheme.h"
#include "dialogs.h"
#include "mainmenu.h"
#include "runewars.h"

MainMenuScreen::MainMenuScreen(bool saveExists, bool saveValid, bool recoveryExists,
                               bool replayExists) :
    JsonWindow("screen_mainmenu.json", nullptr), hasSave(saveExists), selected(-1)
{
    leftPanel = JsonUnpack::rect(jobject, "panel:left", Rect(0, 0, 224, 768));
    rightPanel = JsonUnpack::rect(jobject, "panel:right", Rect(800, 0, 224, 768));
    menuArea = JsonUnpack::rect(jobject, "menu:area", Rect(812, 196, 200, 440));
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

    addEntry(_("Continue"), saveValid ? "" :
             (saveExists ? _("INVALID SAVE") : _("NO SAVED GAME")),
             Menu::GameLoadPart, saveValid);
    addEntry(_("New Game"), "", Menu::SelectPerson, true);
    const bool loadAvailable = saveExists || recoveryExists;
    addEntry(_("Load Game"), loadAvailable ? "" : _("NO SAVES"),
             Menu::LoadRecovery, loadAvailable);
    addEntry(_("Replays"), replayExists ? "" : _("NO REPLAYS"),
             Menu::ReplayLibrary, true);
    addEntry(_("Encyclopedia"), "", Menu::Encyclopedia, true);
    addEntry(_("Settings"), "", Menu::SettingsMenu, true);
    addEntry(_("Multiplayer"), "", Menu::MultiplayerLobby, true);
    addEntry(_("Quit"), "", Menu::GameExit, true);

    selected = saveValid ? 0 : 1;
    setMouseTrack(true);
    setKeyHandle(true);
    setVisible(true);
}

void MainMenuScreen::addEntry(const std::string & label, const std::string & note, int target, bool enabled)
{
    const int index = static_cast<int>(entries.size());
    const int posy = menuArea.y + index * (itemHeight + itemGap);
    entries.push_back(Entry{ label, note, Rect(menuArea.x, posy, menuArea.w, itemHeight), target, enabled });
}

void MainMenuScreen::renderWindow(void)
{
    JsonWindow::renderWindow();

    renderColor(panelColor, leftPanel);
    renderColor(panelColor, rightPanel);

    renderLine(panelBorderColor, Point(leftPanel.x + leftPanel.w - 1, 0),
               Point(leftPanel.x + leftPanel.w - 1, height() - 1));
    renderLine(dividerColor, Point(leftPanel.x + leftPanel.w - 3, 0),
               Point(leftPanel.x + leftPanel.w - 3, height() - 1));
    renderLine(panelBorderColor, Point(rightPanel.x, 0), Point(rightPanel.x, height() - 1));
    renderLine(dividerColor, Point(rightPanel.x + 2, 0), Point(rightPanel.x + 2, height() - 1));

    const FontRender & title = GameTheme::fontRender(titleFont);
    const FontRender & menu = GameTheme::fontRender(menuFont);
    const FontRender & small = GameTheme::fontRender(smallFont);
    const FontRender & footer = GameTheme::fontRender("dejavus12");
    const int leftCenter = leftPanel.x + leftPanel.w / 2;
    const int rightCenter = rightPanel.x + rightPanel.w / 2;

    renderText(small, _("THE RUNE WAR"), dividerColor, Point(leftCenter, 42), AlignCenter);
    renderText(title, _("FOUR"), titleColor, Point(leftCenter, 78), AlignCenter);
    renderText(title, _("WINDS"), titleColor, Point(leftCenter, 124), AlignCenter);
    renderText(title, _("REBORN"), titleColor, Point(leftCenter, 170), AlignCenter);
    renderLine(panelBorderColor, Point(34, 229), Point(190, 229));
    renderText(small, _("OLD RUNES,"), textColor, Point(leftCenter, 239), AlignCenter);
    renderText(small, _("A NEW WAR"), textColor, Point(leftCenter, 258), AlignCenter);

    renderText(footer, _("COMMUNITY RESTORATION"), mutedColor,
               Point(leftCenter, height() - 55), AlignCenter);
    renderText(footer, _("OF THE ORIGINAL GAME"), mutedColor,
               Point(leftCenter, height() - 36), AlignCenter);

    renderText(small, _("MAIN MENU"), panelBorderColor, Point(rightCenter, 139), AlignCenter);
    renderLine(panelBorderColor, Point(rightPanel.x + 28, 164),
               Point(rightPanel.x + rightPanel.w - 28, 164));

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        const Entry & entry = entries[ii];
        const bool isSelected = static_cast<int>(ii) == selected;
        const Color fill = isSelected && entry.enabled ? buttonSelectedColor : buttonColor;
        const Color border = isSelected && entry.enabled ? buttonSelectedBorderColor : buttonBorderColor;
        const Color labelColor = entry.enabled ? (isSelected ? titleColor : textColor) : mutedColor;

        renderColor(fill, entry.area);
        renderRect(border, entry.area);

        if(isSelected && entry.enabled)
        {
            renderLine(buttonSelectedBorderColor,
                       Point(entry.area.x + 3, entry.area.y + 3),
                       Point(entry.area.x + 3, entry.area.y + entry.area.h - 4));
        }

        if(entry.note.empty())
        {
            renderText(menu, entry.label, labelColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + entry.area.h / 2),
                       AlignCenter, AlignCenter);
        }
        else
        {
            renderText(menu, entry.label, labelColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + 4), AlignCenter);
            renderText(small, entry.note, mutedColor,
                       Point(entry.area.x + entry.area.w / 2, entry.area.y + 34), AlignCenter);
        }
    }

    if(Application::isReleaseBuild())
    {
        renderText(small, StringFormat("%1 %2").arg(_("RELEASE")).arg(Application::version()),
                   mutedColor, Point(rightCenter, height() - 28), AlignCenter);
    }
    else
    {
        renderText(small, StringFormat("%1 %2").arg(_("DEV BUILD")).arg(Application::buildDate()),
                   mutedColor, Point(rightCenter, height() - 45), AlignCenter);

        const std::string revision = Application::revision();
        if(! revision.empty() && revision != "unknown")
            renderText(small, revision, mutedColor,
                       Point(rightCenter, height() - 26), AlignCenter);
    }
}

bool MainMenuScreen::activateSelected(void)
{
    if(selected < 0 || static_cast<size_t>(selected) >= entries.size()) return false;

    const Entry & entry = entries[selected];
    if(! entry.enabled) return true;

    if(entry.target == Menu::SelectPerson && hasSave)
    {
        if(! MessageBox(_("New Game"),
            _("Start a new game? The current autosave will be replaced when new progress is saved."),
            *this).exec())
        {
            renderWindow();
            return true;
        }
    }

    playSound("button");
    setResultCode(entry.target);
    setVisible(false);
    return true;
}

bool MainMenuScreen::selectNext(int direction)
{
    if(entries.empty()) return false;

    int next = selected;
    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        next = (next + direction + static_cast<int>(entries.size())) % static_cast<int>(entries.size());
        if(entries[next].enabled)
        {
            if(next != selected)
            {
                selected = next;
                renderWindow();
            }
            return true;
        }
    }

    return false;
}

bool MainMenuScreen::keyPressEvent(const KeySym & key)
{
    switch(key.keycode())
    {
        case Key::UP:       return selectNext(-1);
        case Key::DOWN:     return selectNext(1);
        case Key::RETURN:
        case Key::SPACE:    return activateSelected();
        case Key::ESCAPE:
            selected = static_cast<int>(entries.size()) - 1;
            return activateSelected();
        default: break;
    }

    return false;
}

bool MainMenuScreen::mouseClickEvent(const ButtonsEvent & coords)
{
    if(! coords.isButtonLeft()) return false;

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        if(coords.isClick(entries[ii].area))
        {
            selected = static_cast<int>(ii);
            return activateSelected();
        }
    }

    return false;
}

bool MainMenuScreen::mouseMotionEvent(const Point & pos, u32 buttons)
{
    (void) buttons;

    for(size_t ii = 0; ii < entries.size(); ++ii)
    {
        if(entries[ii].area & pos)
        {
            if(entries[ii].enabled && selected != static_cast<int>(ii))
            {
                selected = static_cast<int>(ii);
                renderWindow();
            }
            return true;
        }
    }

    return false;
}
