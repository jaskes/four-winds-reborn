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
#include <chrono>
#include <string>

#include "settings.h"
#include "actions.h"
#include "gametheme.h"
#include "gamesummarypart.h"
#include "matchscore.h"
#include "matchtopology.h"
#include "matchpresentation.h"

namespace
{
    std::uint64_t monotonicMilliseconds(void)
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }
}

GameSummaryScreen::GameSummaryScreen() : JsonWindow("screen_game_summary.json", nullptr),
    buttonNext(nullptr), scores(MatchScore::current()),
    winners(MatchScore::winnerIndices(scores)), winnerPage(0), page(Page::Victory),
    mousePressedWinnerPage(0), victoryMousePressArmed(false),
    victoryAdvancePending(false), scoresOpenedAtMilliseconds(0)
{
    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:category"));
    labels.back().text = _("Category");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:score1"));
    labels.back().text = _("Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:score2"));
    labels.back().text = _("Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:score3"));
    labels.back().text = _("Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:score4"));
    labels.back().text = _("Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:rank1"));
    labels.back().text = _("Rank");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:rank2"));
    labels.back().text = _("Rank");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:rank3"));
    labels.back().text = _("Rank");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:rank4"));
    labels.back().text = _("Rank");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:territory"));
    labels.back().text = _("Territory Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:summon"));
    labels.back().text = _("Summon Circle Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:unit"));
    labels.back().text = _("Unit Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:spell"));
    labels.back().text = _("Spell Point Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:land"));
    labels.back().text = _("Land Claim Score");

    labels.push_back(GameTheme::jsonTextInfo(jobject, "textinfo:total"));
    labels.back().text = _("Total Score");

    const std::array<const char*, 4> scoreKeys = {
        "textinfo:score1", "textinfo:score2", "textinfo:score3", "textinfo:score4"
    };
    const std::array<const char*, 4> rankKeys = {
        "textinfo:rank1", "textinfo:rank2", "textinfo:rank3", "textinfo:rank4"
    };
    const std::array<int, MatchScore::CategoryCount> rowY = { 286, 320, 374, 410, 446 };
    for(std::size_t playerIndex = 0;
        playerIndex < scores.size() && playerIndex < scoreKeys.size(); ++playerIndex)
    {
        const MatchScore::PlayerResult & player = scores[playerIndex];
        const JsonTextInfo scoreTemplate = GameTheme::jsonTextInfo(jobject, scoreKeys[playerIndex]);
        const JsonTextInfo rankTemplate = GameTheme::jsonTextInfo(jobject, rankKeys[playerIndex]);

        JsonTextInfo playerName = scoreTemplate;
        playerName.position.x = (scoreTemplate.position.x + rankTemplate.position.x) / 2;
        playerName.position.y = 215;
        playerName.font = "dejavus22";
        playerName.text = player.person.name();
        if(player.finalRank == 1)
            playerName.color = GameTheme::jsonColor(jobject, "color:winner");
        values.push_back(playerName);

        for(std::size_t category = 0; category < MatchScore::CategoryCount; ++category)
        {
            JsonTextInfo scoreValue = scoreTemplate;
            scoreValue.position.y = rowY[category];
            scoreValue.font = "dejavus22";
            scoreValue.text = String::number(player.categories[category].score);
            values.push_back(scoreValue);

            JsonTextInfo rankValue = rankTemplate;
            rankValue.position.y = rowY[category];
            rankValue.font = "dejavus22";
            rankValue.text = String::number(player.categories[category].rank);
            values.push_back(rankValue);
        }

        JsonTextInfo totalValue = scoreTemplate;
        totalValue.position.y = 638;
        totalValue.font = "dejavus26";
        totalValue.text = String::number(player.totalScore);

        JsonTextInfo finalRank = rankTemplate;
        finalRank.position.y = 638;
        finalRank.font = "dejavus26";
        finalRank.text = String::number(player.finalRank);
        if(player.finalRank == 1)
        {
            totalValue.color = GameTheme::jsonColor(jobject, "color:winner");
            finalRank.color = GameTheme::jsonColor(jobject, "color:winner");
        }
        values.push_back(totalValue);
        values.push_back(finalRank);
    }

    summaryTitle = GameTheme::jsonTextInfo(jobject, "textinfo:summary_title");
    summaryTitle.text = MatchPresentation::teams() && winners.size() == 2 ? _("TEAM VICTORY") :
        (winners.size() > 1 ? _("JOINT VICTORY") : _("VICTORY"));
    summaryWinners = GameTheme::jsonTextInfo(jobject, "textinfo:summary_winners");
    summaryWinners.text = winnerNames();
    if(winners.size() > 2)
        summaryWinners.font = "dejavus20";
    summaryDetails = GameTheme::jsonTextInfo(jobject, "textinfo:summary_details");
    if(!winners.empty() && winners.front() < scores.size())
    {
        const MatchScore::PlayerResult & winner = scores[winners.front()];
        summaryDetails.text = activeMatchTopology().teamCount() < activeMatchTopology().seatCount() ?
            StringFormat(_("Team score: %1")).arg(winner.teamScore) :
            StringFormat(_("Final score: %1")).arg(winner.totalScore);
    }
    summaryPortraitArea = GameTheme::jsonRect(jobject, "area:summary_portraits");
    summaryWinnerArea = GameTheme::jsonRect(jobject, "area:summary_winner");

    victoryPanel = GameTheme::jsonRect(jobject, "area:victory_panel");
    victoryPanelColor = GameTheme::jsonColor(jobject, "color:victory_panel");
    victoryPanelColor.setA(jobject.getInteger("alpha:victory_panel", 224));
    victoryBorderColor = GameTheme::jsonColor(jobject, "color:victory_border");
    victoryTitle = GameTheme::jsonTextInfo(jobject, "textinfo:victory_title");
    victoryName = GameTheme::jsonTextInfo(jobject, "textinfo:victory_name");
    victoryDetails = GameTheme::jsonTextInfo(jobject, "textinfo:victory_details");
    victoryWinners = GameTheme::jsonTextInfo(jobject, "textinfo:victory_winners");
    victoryHint = GameTheme::jsonTextInfo(jobject, "textinfo:victory_hint");

    if(!winners.empty())
    {
        const int portraitGap = 8;
        const int portraitCount = static_cast<int>(std::min<std::size_t>(winners.size(), 4));
        const int portraitSize = std::min(summaryPortraitArea.h - 12,
            (summaryPortraitArea.w - portraitGap * (portraitCount + 1)) / portraitCount);

        for(int index = 0; index < portraitCount; ++index)
        {
            const std::size_t winnerIndex = winners[index];
            if(winnerIndex >= scores.size()) continue;
            const AvatarInfo & avatarInfo = GameData::avatarInfo(scores[winnerIndex].person.avatar);
            const Texture & portrait = GameTheme::texture(avatarInfo.portrait);
            if(portrait.isValid())
                summaryPortraits.emplace_back(Texture::scale(portrait,
                    Size(portraitSize, portraitSize), true));
        }
    }

    buttonNext = buttons.findIds("but_done");
    if(buttonNext)
        buttonNext->setAction(Action::ButtonDone);

    if(winners.empty())
        page = Page::Scores;
    else
        updateVictoryPage();

    buttons.setVisible(page == Page::Scores);

    setVisible(true);
}

std::string GameSummaryScreen::winnerNames(void) const
{
    std::string names;
    for(const std::size_t index : winners)
    {
        if(index >= scores.size()) continue;
        if(!names.empty()) names += ", ";
        names += scores[index].person.name();
    }
    return names;
}

void GameSummaryScreen::updateVictoryPage(void)
{
    if(winnerPage >= winners.size() || winners[winnerPage] >= scores.size())
    {
        page = Page::Scores;
        buttons.setVisible(true);
        return;
    }

    const MatchScore::PlayerResult & winner = scores[winners[winnerPage]];
    const int gloatIndex = winner.person.avatar.id() - Avatar::Orachi;
    if(gloatIndex < 0 || 8 < gloatIndex)
    {
        page = Page::Scores;
        buttons.setVisible(true);
        return;
    }

    victoryArt = GameTheme::texture("gloat" + std::to_string(gloatIndex));
    if(!victoryArt.isValid())
    {
        page = Page::Scores;
        buttons.setVisible(true);
        return;
    }
    victoryTitle.text = summaryTitle.text;
    victoryName.text = winner.person.name();
    victoryDetails.text = activeMatchTopology().teamCount() < activeMatchTopology().seatCount() ?
        StringFormat(_("Team score: %1")).arg(winner.teamScore) :
        StringFormat(_("Final score: %1")).arg(winner.totalScore);
    victoryWinners.text = winners.size() > 1 ?
        StringFormat(_("Winners: %1")).arg(winnerNames()) : std::string();
    victoryHint.text = winnerPage + 1 < winners.size() ?
        _("CLICK / ENTER: NEXT WINNER") : _("CLICK / ENTER: DETAILED SCORES");
}

void GameSummaryScreen::advanceVictoryPage(void)
{
    if(page != Page::Victory) return;

    const std::uint64_t now = monotonicMilliseconds();
    if(winnerPage + 1 < winners.size())
    {
        ++winnerPage;
        updateVictoryPage();
    }
    else
    {
        page = Page::Scores;
        scoresOpenedAtMilliseconds = now;
        buttons.setVisible(true);
    }

    renderWindow();
}

void GameSummaryScreen::tickEvent(u32)
{
    if(!victoryAdvancePending) return;

    // SDL drains every queued input event before the scene tick.  Delaying the
    // transition until here collapses both halves of a rapid double click into
    // one page advance, so every joint winner requires a distinct later click.
    victoryAdvancePending = false;
    advanceVictoryPage();
}

void GameSummaryScreen::renderWindow(void)
{
    if(page == Page::Victory)
    {
        renderTexture(victoryArt);
        renderColor(victoryPanelColor, victoryPanel);
        renderRect(victoryBorderColor, victoryPanel);
        renderTextInfo(victoryTitle);
        renderTextInfo(victoryName);
        renderTextInfo(victoryDetails);
        if(!victoryWinners.text.empty()) renderTextInfo(victoryWinners);
        renderTextInfo(victoryHint);
        return;
    }

    JsonWindow::renderWindow();

    if(MatchPresentation::duel() || MatchPresentation::teams())
    {
        using namespace MatchPresentation;
        card(*this, Rect(12, 12, 1000, 691), Color(107, 99, 70));
        text(*this, modeName() + " / " + _("Final results"), Point(512, 30), 950, ink(), 26);
        text(*this, summaryTitle.text + ": " + winnerNames(), Point(512, 78), 950);
        const std::array<const char*, MatchScore::CategoryCount> categories = {
            _("Territory Score"), _("Summon Circle Score"), _("Unit Score"), _("Spell Point Score"), _("Land Claim Score")
        };
        for(int team = 0; team < 2; ++team)
        {
            const int x = 30 + team * 488;
            card(*this, Rect(x, 128, 476, 552), sideColor(team));
            std::array<int, MatchScore::CategoryCount> raw{}, points{};
            int total = 0, rank = 0, member = 0;
            std::string title = teamName(team);
            for(const auto & player : scores)
            {
                if(player.teamId != team) continue;
                if(duel()) title = player.person.name() + " / " + role(player.person);
                total = player.teamScore;
                rank = player.teamRank;
                const Texture portrait = Texture::scale(GameTheme::texture(GameData::avatarInfo(player.person.avatar).portrait), Size(72, 72), true);
                const int portraitX = x + 26 + member++ * 220;
                renderTexture(portrait, Point(portraitX, 188));
                text(*this, player.person.name(), Point(portraitX + 80, 216), 104, ink(), 16, AlignLeft);
                for(std::size_t category = 0; category < raw.size(); ++category)
                {
                    raw[category] += player.categories[category].score;
                    points[category] += player.categories[category].standingPoints;
                }
            }
            text(*this, title, Point(x + 238, 146), 450, sideColor(team), 22);
            text(*this, StringFormat(_("Rank: %1")).arg(rank), Point(x + 238, 276), 440, sideColor(team), 22);
            text(*this, _("Score"), Point(x + 341, 325), 82, ink(), 16);
            text(*this, _("Points"), Point(x + 423, 325), 82, ink(), 16);
            for(std::size_t category = 0; category < raw.size(); ++category)
            {
                const int y = 360 + 42 * static_cast<int>(category);
                text(*this, categories[category], Point(x + 16, y), 280, ink(), 18, AlignLeft);
                text(*this, String::number(raw[category]), Point(x + 341, y), 82);
                text(*this, String::number(points[category]), Point(x + 423, y), 82);
            }
            renderLine(sideColor(team), Point(x + 16, 580), Point(x + 460, 580));
            text(*this, StringFormat(teams() ? _("Team score: %1") : _("Final score: %1")).arg(total),
                 Point(x + 238, 609), 440, sideColor(team), 26);
        }
        return;
    }

    if(!winners.empty())
    {
        renderColor(victoryPanelColor, summaryPortraitArea);
        renderRect(victoryBorderColor, summaryPortraitArea);
        renderColor(victoryPanelColor, summaryWinnerArea);
        renderRect(victoryBorderColor, summaryWinnerArea);

        const int portraitGap = 8;
        int portraitsWidth = 0;
        for(const Texture & portrait : summaryPortraits)
            portraitsWidth += portrait.width();
        if(1 < summaryPortraits.size())
            portraitsWidth += portraitGap * (static_cast<int>(summaryPortraits.size()) - 1);

        int portraitX = summaryPortraitArea.x + (summaryPortraitArea.w - portraitsWidth) / 2;
        for(const Texture & portrait : summaryPortraits)
        {
            const int portraitY = summaryPortraitArea.y + (summaryPortraitArea.h - portrait.height()) / 2;
            renderTexture(portrait, Point(portraitX, portraitY));
            portraitX += portrait.width() + portraitGap;
        }

        renderTextInfo(summaryTitle);
        renderTextInfo(summaryWinners);
        renderTextInfo(summaryDetails);
    }

    for(auto & label : labels)
	renderTextInfo(label);

    for(auto & value : values)
        renderTextInfo(value);
}

bool GameSummaryScreen::keyPressEvent(const KeySym & key)
{
    switch(key.keycode())
    {
        case Key::RETURN:
        case Key::SPACE:
            if(page == Page::Victory)
            {
                victoryAdvancePending = true;
                return true;
            }
            pushEventAction(Action::ButtonDone, this, nullptr);
            return true;

        default: break;
    }

    return false;
}

bool GameSummaryScreen::mouseClickEvent(const ButtonsEvent & event)
{
    if(page != Page::Victory || !event.isButtonLeft()) return false;

    const bool accepted = GameSummaryInput::acceptsVictoryPageClick(
        victoryMousePressArmed, mousePressedWinnerPage, winnerPage);
    victoryMousePressArmed = false;
    if(!accepted) return true;

    victoryAdvancePending = true;
    return true;
}

bool GameSummaryScreen::mousePressEvent(const ButtonEvent & event)
{
    if(page != Page::Victory || !event.isButtonLeft())
    {
        victoryMousePressArmed = false;
        return false;
    }

    victoryMousePressArmed = true;
    mousePressedWinnerPage = winnerPage;
    return true;
}

bool GameSummaryScreen::userEvent(int act, void* data)
{
    switch(act)
    {
        case Action::ButtonDone:
            switch(GameSummaryInput::doneDisposition(page == Page::Victory,
                       scoresOpenedAtMilliseconds, monotonicMilliseconds()))
            {
                case GameSummaryInput::DoneDisposition::AdvanceVictory:
                    // The legacy Done button remains visible over the victory
                    // art.  Treat it as Next until every joint winner has been
                    // shown instead of closing the entire summary early.
                    victoryAdvancePending = true;
                    break;

                case GameSummaryInput::DoneDisposition::Ignore:
                    // A mouse-up that reveals the score page may also reach its
                    // Done button during the same event dispatch.
                    break;

                case GameSummaryInput::DoneDisposition::Close:
                    setResultCode(Menu::MainMenu);
                    setVisible(false);
                    break;
            }
            break;

        default: break;
    }

    return true;
}
