#ifndef FOUR_WINDS_MATCH_PRESENTATION_H
#define FOUR_WINDS_MATCH_PRESENTATION_H

#include "gametheme.h"
#include "matchtopology.h"
#include "runegameruleset.h"

// One vocabulary and colour key for the roster, table, island and results.
namespace MatchPresentation
{
    inline bool duel() { return activeMatchTopology().seatCount() == 2; }
    inline bool teams() { return activeMatchTopology().teamCount() < activeMatchTopology().seatCount(); }
    inline Color ink() { return Color(239, 228, 174); }
    inline Color panel() { return Color(17, 24, 28); }
    inline Color sideColor(int side) { return side == 0 ? Color(236, 181, 98) : Color(113, 213, 230); }
    inline int side(const Person & person) { return activeMatchTopology().teamForClan(person.clan()); }
    inline std::string teamName(int side) { return side == 0 ? _("Team 1") : _("Team 2"); }
    inline std::string modeName()
    {
        if(duel()) return _("Duel");
        if(activeMatchTopology().id() == DuelTopologyId) return _("Duel (two hands)");
        return teams() ? _("Coalition") : _("Free for All");
    }
    inline std::string rulesName()
    {
        return activeRuneGameRuleset().id() == QuickRuneGameRulesetId ? _("Quick: East round") : _("Classic: four rounds");
    }
    inline std::string role(const Person & person)
    {
        if(GameData::isLocallyControlled(person)) return _("You");
        if(person.avatar == GameData::myPerson().avatar) return "AI";
        return activeMatchTopology().alliedByClan(GameData::myPerson().clan(), person.clan()) ? _("Ally") : _("Opponent");
    }
    inline void text(Window & window, const std::string & value, Point point,
                     int width = 1000, Color color = ink(), int size = 18, AlignType align = AlignCenter)
    {
        const FontRender* font = &GameTheme::fontRender("dejavus" + std::to_string(size));
        for(int smaller : {18, 16, 14})
            if(font->stringSize(value).w > width && smaller < size)
                font = &GameTheme::fontRender("dejavus" + std::to_string(smaller));
        window.renderText(*font, value, color, point, align);
    }
    inline void card(Window & window, const Rect & area, Color color)
    {
        window.renderColor(panel(), area);
        window.renderRect(color, area);
    }
}

#endif
