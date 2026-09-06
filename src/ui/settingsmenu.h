/***************************************************************************
 *   Four Winds Reborn settings menu                                       *
 ***************************************************************************/

#ifndef _FWR_SETTINGSMENU_
#define _FWR_SETTINGSMENU_

#include <vector>

#include "contentcatalog.h"
#include "gamedata.h"

class SettingsMenuScreen : public JsonWindow
{
    enum EntryKind
    {
        AIDifficulty,
        RuneGameRules,
        MatchMode,
        Language,
        ContentPackage,
        GameSpeed,
        MusicVolume,
        EffectsVolume,
        VoiceVolume,
        GuardianVoices,
        DisplayMode,
        WindowSize,
        Apply,
        Back
    };

    struct Entry
    {
        EntryKind kind;
        Rect      area;
    };

    std::vector<Entry> entries;
    int                selected;
    std::string        language;
    std::string        initialContentTheme;
    std::vector<InstalledContentPackage> contentPackages;
    int                contentPackageIndex;
    std::string        gameSpeed;
    AI::Difficulty     aiDifficulty;
    std::string        runeGameRuleset;
    std::string        matchMode;
    int                musicVolume;
    int                effectsVolume;
    int                voiceVolume;
    bool               guardianVoices;
    bool               fullscreen;
    std::vector<int>   windowScales;
    int                windowScale;

    Rect               leftPanel;
    Rect               rightPanel;
    Rect               menuArea;
    Rect               difficultyArea;
    Rect               difficultyPortraitArea;
    std::vector<Texture> difficultyPortraits;
    int                itemHeight;
    int                itemGap;

    std::string        titleFont;
    std::string        menuFont;
    std::string        smallFont;

    Color              panelColor;
    Color              panelBorderColor;
    Color              dividerColor;
    Color              titleColor;
    Color              textColor;
    Color              mutedColor;
    Color              buttonColor;
    Color              buttonSelectedColor;
    Color              buttonBorderColor;
    Color              buttonSelectedBorderColor;

    void               addEntry(EntryKind);
    std::string        entryLabel(EntryKind) const;
    std::string        entryValue(EntryKind) const;
    bool               isVolumeEntry(EntryKind) const;
    int                volumeValue(EntryKind) const;
    void               setVolumeValue(EntryKind, int);
    Size               windowSizeForScale(int) const;
    bool               adjustSelected(int);
    bool               activateSelected(void);
    bool               selectNext(int);

protected:
    bool               keyPressEvent(const KeySym &) override;
    bool               mouseClickEvent(const ButtonsEvent &) override;
    bool               mouseMotionEvent(const Point &, u32) override;

public:
    explicit SettingsMenuScreen(const std::string & program);

    void               renderWindow(void) override;
};

#endif
