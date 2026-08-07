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
#include <functional>
#include <unordered_map>

#include "runewars.h"
#include "settings.h"
#include "gametheme.h"
#include "crashreport.h"
#include "contentpackage.h"

struct dirNotFound
{
    bool operator() (const std::string & dir)
    {
        return ! Systems::isDirectory(dir);
    }
};

namespace GameTheme
{
    StringList                          resourceFiles;
    StringList				shareDirs;

    std::string				themeName;
    std::string				themeDescription;
    std::string				themeAuthor;
    Size				themeSize;
    JsonToolTip				themeTooltips;

    std::unordered_map<std::string, BinaryBuf>	   cacheBinaries;
    std::unordered_map<std::string, Sprite>	   cacheSprites;
    std::unordered_map<std::string, FontRenderTTF> cacheFonts;

    std::unordered_map<std::string, ImageInfo>	mapImagesInfo;
    std::unordered_map<std::string, FileInfo>	mapFilesInfo;

    Sprite                              jsonFileSprite(const JsonObject &);
    Sprite                              jsonRectSprite(const JsonObject &);
    Sprite                              jsonTextSprite(const JsonObject &);
    Sprite                              jsonCompositeSprite(const JsonObject &);

    bool loadResources(const Application &);
    bool jsonFontsLoad(const std::string &);

    template<typename T>
    bool loadJson(const char* json, std::unordered_map<std::string, T> & m)
    {
        JsonContent jc = GameTheme::jsonResource(json);
        if(jc.isArray())
        {
	    auto list = jc.toArray().toStdList<T>();
	    for(auto it = list.begin(); it != list.end(); ++it)
	    {
		const T & info = *it;
		m[info.id] = info;
	    }

            return true;
        }

        ERROR("incorrect json array: " << json);
        return false;
    }
}

void GameTheme::clear(void)
{
    cacheSprites.clear();
    mapImagesInfo.clear();
    cacheFonts.clear();
    mapFilesInfo.clear();
    cacheBinaries.clear();
}

bool GameTheme::loadResources(const Application & app)
{
    StringList shareDirs = Systems::shareDirectories(app.domain());

    if(app.program)
        shareDirs.push_front(Systems::dirname(app.program));

    for(auto & dir: shareDirs)
        VERBOSE("scan share dir: " << dir);

    std::transform(shareDirs.begin(), shareDirs.end(), shareDirs.begin(), [](auto & path){ return Systems::concatePath(path, "themes"); });
    shareDirs.remove_if(dirNotFound());

    if(shareDirs.empty())
    {
        ERROR("dir not found: " << "themes");
        return false;
    }

    std::transform(shareDirs.begin(), shareDirs.end(), shareDirs.begin(), [&](auto & path){ return Systems::concatePath(path, app.theme); });
    shareDirs.remove_if(dirNotFound());

    if(shareDirs.empty())
    {
        ERROR("dir not found: " << app.theme);
        return false;
    }

    for(auto it = shareDirs.rbegin(); it != shareDirs.rend(); ++it)
    {
        VERBOSE("find files order: " << *it);
        resourceFiles << Systems::findFiles(*it);
    }

    return 0 < resourceFiles.size();
}

bool GameTheme::init(const Application & app)
{
    themeName = app.theme;

    CrashReport::breadcrumb(std::string("Theme stage=resources status=begin name=").append(app.theme));
    if(! loadResources(app))
	{
	CrashReport::breadcrumb(std::string("Theme stage=resources status=failed name=").append(app.theme));
	return false;
	}
    CrashReport::breadcrumb(std::string("Theme stage=resources status=ok files=")
        .append(String::number(resourceFiles.size())));

    JsonObject jo = jsonResource("index.json").toObject();

    if(! jo.isValid())
    {
        ERROR("index.json not found, theme: " << app.theme);
        return false;
    }

    std::string contentPackageError;
    if(!activateContentPackage(jo, &contentPackageError))
    {
        CrashReport::breadcrumb(std::string("Theme stage=content-package status=failed reason=")
            .append(contentPackageError));
        ERROR("content package: " << contentPackageError);
        return false;
    }
    const ContentPackageManifest & contentManifest = activeContentPackageManifest();
    const ContentPackageIdentity package = contentPackageIdentity(contentManifest);
    CrashReport::breadcrumb(std::string("Theme stage=content-package status=ok id=")
        .append(package.id).append(" version=").append(std::to_string(package.version))
        .append(" contract=").append(contentManifest.engineContract));

    Settings::read();
    // The package accepted by the startup validator is authoritative for this
    // process. This repairs the in-memory preference after a safe fallback
    // without rewriting unrelated settings.
    Settings::setContentTheme(app.theme);

    // load caches
    if(! loadJson<ImageInfo>("images.json", mapImagesInfo))
	return false;

    if(! loadJson<FileInfo>("musics.json", mapFilesInfo))
	return false;

    if(! loadJson<FileInfo>("sounds.json", mapFilesInfo))
	return false;

    // load fonts
    if(! jsonFontsLoad("fonts.json"))
	return false;

    // parse index json
    themeName = jo.getString("name");
    themeDescription = jo.getString("description");
    themeAuthor = jo.getString("author");
    themeSize = JsonUnpack::size(jo, "size");

    // load tooltips
    const JsonObject* jc = jo.getObject("tooltips");

    if(jc)
    {
	themeTooltips.bgcolor = JsonUnpack::color(*jc, "bgcolor", Color::Wheat);
	themeTooltips.rtcolor = JsonUnpack::color(*jc, "rtcolor", Color::MidnightBlue);
	themeTooltips.fncolor = JsonUnpack::color(*jc, "fncolor", Color::Black);
	themeTooltips.font = jc->getString("font", "tooltips");
    }
    else
    {
	themeTooltips.bgcolor = Color("wheat");
	themeTooltips.rtcolor = Color("midnightblue");
	themeTooltips.fncolor = Color("black");
	themeTooltips.font = "tooltips";
    }

    CrashReport::breadcrumb(std::string("Theme stage=gamedata status=begin file=")
        .append(contentManifest.gameData));
    JsonObject gameData = jsonResource(contentManifest.gameData).toObject();
    if(!gameData.isValid())
    {
        CrashReport::breadcrumb(std::string("Theme stage=gamedata status=failed file=")
            .append(contentManifest.gameData).append(" reason=invalid-or-missing"));
        ERROR("content package game data not found or invalid: " << contentManifest.gameData);
        return false;
    }
    if(! GameData::init(gameData))
    {
	CrashReport::breadcrumb("Theme stage=gamedata status=failed");
	ERROR("game data init: error");
        return false;
    }
    CrashReport::breadcrumb("Theme stage=gamedata status=ok");

    VERBOSE("use theme: " << themeName << ", " << "author: " << themeAuthor << ", " << themeSize.toString());

    bool displayFullScreen = app.fullscreen ? true : Settings::fullscreen();
    Size displaySize = app.geometry;
    if(displaySize.isEmpty())
    {
        int scale = Settings::windowScale();

        if(!displayFullScreen)
        {
            const Size available = Display::usableBounds().toSize();

            displaySize = Size(themeSize.w * scale / 100,
                               themeSize.h * scale / 100);
            while(75 < scale &&
                  (available.w < displaySize.w || available.h < displaySize.h))
            {
                scale -= 25;
                displaySize = Size(themeSize.w * scale / 100,
                                   themeSize.h * scale / 100);
            }

            Settings::setWindowScale(scale);
        }
        else
            displaySize = Size(themeSize.w * scale / 100,
                               themeSize.h * scale / 100);
    }

    Display::setFixedRenderSize(true);

    const std::string title = app.name().append(", version: ").append(app.version());
    CrashReport::breadcrumb(std::string("Theme stage=display status=begin window=")
        .append(displaySize.toString()).append(" render=").append(themeSize.toString())
        .append(" fullscreen=").append(displayFullScreen ? "true" : "false"));
    if(! Display::init(title, displaySize, themeSize, displayFullScreen, Settings::accel(), false))
    {
	CrashReport::breadcrumb("Theme stage=display status=failed");
	ERROR("display init: " << displaySize.toString());
        return false;
    }
    CrashReport::breadcrumb("Theme stage=display status=ok");

    return true;
}

const JsonToolTip & GameTheme::jsonToolTipInfo(void)
{
    return themeTooltips;
}

bool GameTheme::jsonFontsLoad(const std::string & json)
{
    JsonContent jc = jsonResource(json);
    if(jc.isArray())
    {
	for(auto & info : jc.toArray().toStdList<FontInfo>())
	    cacheFonts[info.id].load(readResource(info.file), info.size, info.blend, info.style, info.hinting);

        return true;
    }

    ERROR("incorrect json array: " << json);
    return false;
}

const BinaryBuf & GameTheme::readResource(const std::string & filename, std::string* res)
{
    auto it = cacheBinaries.find(filename);
    if(it == cacheBinaries.end())
    {
	std::string path;
	BinaryBuf & buf = cacheBinaries[filename];

	if(findResource(filename, &path))
	{
	    buf = Systems::readFile(path);

	    if(buf.empty())
		ERROR("error read file: " << filename);

	    if(res) res->assign(path);
	}
	else
	{
	    ERROR("file not found: " << filename);
	}

	return buf;
    }

    return (*it).second;
}

bool GameTheme::findResource(const std::string & filename, std::string* res)
{
    if(Systems::isFile(filename))
    {
        if(res) res->assign(filename);
        return true;
    }

    for(auto & rf : resourceFiles)
        if(String::toLower((Systems::basename(rf))) == String::toLower(filename))
    {
        if(res) res->assign(rf);
        return true;
    }

    return false;
}

JsonContent GameTheme::jsonResource(const std::string & filename)
{
    JsonContent res;
    auto & buf = readResource(filename.substr(0, 4) == "res:" ? filename.substr(4) : filename);

    if(! res.parseBinary(reinterpret_cast<const char*>(buf.data()), buf.size()))
	ERROR("parse file: " << filename);

    return res;
}

const Size & GameTheme::size(void)
{
    return themeSize;
}

const std::string & GameTheme::author(void)
{
    return themeAuthor;
}

Texture GameTheme::texture(const std::string & name)
{
    return sprite(name);
}

const FontRender & GameTheme::fontRender(const std::string & name)
{
    auto it = cacheFonts.find(name);

    if(it != cacheFonts.end())
        return (*it).second;

    return systemFont();
}

const BinaryBuf & GameTheme::sound(const std::string & name)
{
    auto it = mapFilesInfo.find(name);

    if(it == mapFilesInfo.end())
        ERROR("not found: " << name);

    if(it == mapFilesInfo.end())
        return readResource("");

    return readResource(localizedSoundFile((*it).second, Settings::language()));
}

bool GameTheme::isVoiceSound(const std::string & name)
{
    if(name == "begin" || name.rfind("creature", 0) == 0 ||
       name.rfind("intro_en_", 0) == 0 || name.rfind("intro_ru_", 0) == 0 ||
       name.rfind("round_", 0) == 0 || name.rfind("anim_", 0) == 0)
        return true;

    static const char* suffixes[] = { "_chao", "_game", "_kong", "_name", "_pung" };
    for(const char* suffix : suffixes)
    {
        const std::string ending(suffix);
        if(ending.size() <= name.size() &&
           name.compare(name.size() - ending.size(), ending.size(), ending) == 0)
            return true;
    }
    return false;
}

std::string GameTheme::localizedSoundFile(const FileInfo & info, const std::string & value)
{
    const std::string language = String::toLower(value);
    const bool russian = language == "ru" || language == "russian" ||
        language.rfind("ru_", 0) == 0 || language.rfind("ru-", 0) == 0 ||
        language.rfind("russian_", 0) == 0;
    const bool english = language == "en" || language == "english" ||
        language.rfind("en_", 0) == 0 || language.rfind("en-", 0) == 0 ||
        language.rfind("english_", 0) == 0;

    if(russian && ! info.fileRu.empty())
        return info.fileRu;
    if(english && ! info.fileEn.empty())
        return info.fileEn;
    return info.file;
}

const BinaryBuf & GameTheme::music(const std::string & name)
{
    return sound(name);
}

const JsonValue & operator>> (const JsonValue & jv, ImageInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);
	st.id = jo->getString("id");
	st.file = jo->getString("file");
	st.fileEn = jo->getString("file:en");
	st.fileRu = jo->getString("file:ru");
	st.crop = JsonUnpack::rect(*jo, "crop");
	st.colorkey = jo->getString("colorkey");
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, FileInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);
	st.id = jo->getString("id");
	st.file = jo->getString("file");
	st.fileEn = jo->getString("file:en");
	st.fileRu = jo->getString("file:ru");
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, FontInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);
	st.id = jo->getString("id");
	st.file = jo->getString("file");
	st.size = jo->getInteger("size");
	std::string blend = String::toLower(jo->getString("blend"));
        if(blend == "solid")
            st.blend = RenderSolid;
        else
        if(blend == "blended")
            st.blend = RenderBlended;
        else
        if(blend == "shaded")
            st.blend = RenderShaded;

        st.style = StyleNormal;
        for(auto & val : jo->getStdList<std::string>("style"))
        {
            auto lower = String::toLower(val);
            if(lower == "bold")
                st.style |= StyleBold;
            else
            if(lower == "italic")
                st.style |= StyleItalic;
            else
            if(lower == "underline")
                st.style |= StyleUnderLine;
            else
            if(lower == "strikethrough")
                st.style |= StyleStrikeThrough;
        }

        st.hinting = HintingNormal;
        std::string hinting = String::toLower(jo->getString("hinting", "normal"));
        if(hinting == "light")
            st.hinting = HintingLight;
        else
        if(hinting == "mono")
            st.hinting = HintingMono;
        else
        if(hinting == "none")
            st.hinting = HintingNone;
    }

    return jv;
}

Sprite GameTheme::sprite(const std::string & key)
{
    if(key == "null") return cacheSprites[key];

    auto it = mapImagesInfo.find(key);
    if(it == mapImagesInfo.end())
    {
	Sprite res = cacheSprites[key];
	if(res.isValid()) return res;
        ERROR("id not found: " << key);
	return res;
    }

    ImageInfo & info = (*it).second;

    const std::string file = localizedSoundFile(info, Settings::language());
    const std::string cacheKey = info.fileEn.empty() && info.fileRu.empty() ?
        key : key + "\x1f" + file;
    Sprite res = cacheSprites[cacheKey];
    if(res.isValid()) return res;

    std::string path;

    if(! findResource(file, &path))
    {
        ERROR("file not found: " << file);
        return res;
    }

    BinaryBuf buf = Systems::readFile(path);
    if(buf.empty())
        return res;

    Surface sf(buf);
    if(! sf.isValid())
    {
        ERROR("unknown format: " << file);
        return res;
    }

    if(info.colorkey.size())
	sf.setColorKey(Color(info.colorkey));

    res.setTexture(Display::createTexture(sf));

    if(! info.crop.toSize().isEmpty())
    {
        Texture tmp = Display::createTexture(info.crop.toSize());
        Display::renderTexture(res, info.crop, tmp, tmp.rect());
        res.setTexture(tmp);
    }

    cacheSprites[cacheKey] = res;

    return res;
}

Point GameTheme::jsonPoint(const JsonObject & jo, const std::string & key)
{
    if(jo.hasKey(key))
	return JsonUnpack::point(jo, key);

    ERROR("tag not found: " << key);
    return Point();
}

Size GameTheme::jsonSize(const JsonObject & jo, const std::string & key)
{
    if(jo.hasKey(key))
	return JsonUnpack::size(jo, key);

    ERROR("tag not found: " << key);
    return Size();
}

Rect GameTheme::jsonRect(const JsonObject & jo, const std::string & key)
{
    if(jo.hasKey(key))
	return JsonUnpack::rect(jo, key);

    ERROR("tag not found: " << key);
    return Rect();
}

Color GameTheme::jsonColor(const JsonObject & jo, const std::string & key)
{
    if(jo.hasKey(key))
	return jo.getString(key);

    ERROR("tag not found: " << key);
    return Color();
}

Sprite GameTheme::jsonSprite(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return Sprite();
    }

    return jsonSprite(*jv);
}

Sprite GameTheme::jsonSprite(const JsonValue & jv)
{
    if(jv.isString())
    {
	return sprite(jv.getString());
    }

    Sprite res;
    const JsonObject* jo = jv.isObject() ? static_cast<const JsonObject*>(& jv) : nullptr;

    if(! jo)
    {
        ERROR("not object");
        return res;
    }

    std::string key = jo->getString("id");

    if(! key.empty())
    {
        res = cacheSprites[key];
        if(res.isValid()) return res;
    }

    if(! jo->hasKey("type"))
    {
        ERROR("tag not found: " << "type");
        return res;
    }

    std::string type = jo->getString("type");

    if(type == "composite")
        res = jsonCompositeSprite(*jo);
    else
    if(type == "file")
        res = jsonFileSprite(*jo);
    else
    if(type == "rect")
        res = jsonRectSprite(*jo);
    else
    if(type == "text")
        res = jsonTextSprite(*jo);
    else
    {
        ERROR("unknown type: " << type);
        return res;
    }

    if(jo->hasKey("crop"))
    {
        Rect crop = JsonUnpack::rect(*jo, "crop");
        Texture tmp = Display::createTexture(crop.toSize());
        Display::renderTexture(res, crop, tmp, tmp.rect());
        res.setTexture(tmp);
    }

    if(jo->hasKey("offset"))
    {
        Point offset = JsonUnpack::point(*jo, "offset");

        AlignValue halign(jo->getString("halign"), AlignLeft);
        AlignValue valign(jo->getString("valign"), AlignTop);

        if(halign.isCenter())
            offset.x -= res.width() / 2;
        else
        if(halign.isRight())
            offset.x -= res.width();

        if(valign.isCenter())
            offset.y -= res.height() / 2;
        else
        if(valign.isBottom())
            offset.y -= res.height();

        res.setPosition(offset);
    }

    if(jo->hasKey("alpha"))
        res.setAlphaMod(jo->getInteger("alpha"));

    if(! key.empty())
        cacheSprites[key] = res;

    return res;
}

Sprite GameTheme::jsonRectSprite(const JsonObject & jo)
{
    Sprite res;

    if(! jo.hasKey("size"))
    {
        ERROR("tag not found: " << "size");
        return res;
    }

    if(jo.hasKey("color"))
    {
        Color fill = JsonUnpack::color(jo, "fill", Color::Transparent);
        int depth = jo.hasKey("depth") ? jo.getInteger("depth") : 1;
        res.setTexture(Display::renderRect(jo.getString("color"), fill, JsonUnpack::size(jo, "size"), depth));
    }
    else
    if(jo.hasKey("fill"))
    {
        Color col = jo.getString("fill");

        if(jo.hasKey("alpha"))
            col.setA(jo.getInteger("alpha"));

        res.setTexture(Display::createTexture(JsonUnpack::size(jo, "size")));
        Display::renderClear(col, res);
    }

    return res;
}

Sprite GameTheme::jsonFileSprite(const JsonObject & jo)
{
    Sprite res;

    if(! jo.hasKey("file"))
    {
        ERROR("tag not found: " << "file");
        return res;
    }

    std::string path;
    std::string file = jo.getString("file");

    if(! findResource(file, &path))
    {
        ERROR("file not found: " << file);
        return res;
    }

    BinaryBuf buf = Systems::readFile(path);

    if(buf.empty())
        return res;

    Surface sf(buf);

    if(! sf.isValid())
    {
        ERROR("unknown format: " << file);
        return res;
    }

    if(jo.hasKey("colorkey"))
        sf.setColorKey(jo.getString("colorkey"));

    res.setTexture(Display::createTexture(sf));
    return res;
}

Sprite GameTheme::jsonTextSprite(const JsonObject & jo)
{
    Sprite res;

    if(! jo.hasKey("font"))
    {
        ERROR("tag not found: " << "font");
        return res;
    }

    JsonTextColor textInfo = jsonTextColor(jo);

    const FontRender & frs = fontRender(textInfo.font);
    res.setTexture(Display::renderText(frs, _(textInfo.text), textInfo.color));

    return res;
}

Sprite GameTheme::jsonCompositeSprite(const JsonObject & jo)
{
    Sprite res;

    if(jo.hasKey("size"))
        res.setTexture(Display::createTexture(JsonUnpack::size(jo, "size")));

    const JsonValue* jb = jo.getValue("background");

    if(jb)
    {
        if(jb->isString())
        {
            Color back(jb->getString());
            if(res.isValid()) Display::renderClear(back, res);
        }
        else
        if(jb->isObject())
            *jb >> res;
    }

    const JsonValue* jt = jo.getValue("tiles");

    if(jt && jt->isArray())
    {
        for(auto & sp : jsonSprites(*jt))
            Display::renderTexture(sp, Rect(Point(0, 0), sp.size()), res, Rect(sp.position(), sp.size()));

        if(jo.hasKey("save"))
            res.save(jo.getString("save"));
    }

    return res;
}

Sprites GameTheme::jsonSprites(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return Sprites();
    }

    return jsonSprites(*jv);
}

Sprites GameTheme::jsonSprites(const JsonValue & jv)
{
    if(jv.isString())
        return jsonResource(jv.getString()).toArray().toStdVector<Sprite>();
    else
    if(jv.isArray())
    {
        const JsonArray* ja = static_cast<const JsonArray*>(& jv);
        return ja->toStdVector<Sprite>();
    }

    ERROR("not array");
    return Sprites();
}

JsonButton GameTheme::jsonButton(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return JsonButton();
    }

    return jsonButton(*jv);
}

JsonButton GameTheme::jsonButton(const JsonValue & jv)
{
    if(jv.isString())
        return jsonButton(jsonResource(jv.getString()).toObject());
    else
    if(jv.isObject())
    {
        const JsonObject* jo = static_cast<const JsonObject*>(& jv);

        ButtonInfo info;
        info.ids = jo->getString("id");
        info.action2 = jo->getString("action");

        if(jo->hasKey("cursors"))
        {
            StringList cursors = jo->getStdList<std::string>("cursors");
            if(0 < cursors.size()) info.cursorFocusTheme = cursors.front();
            if(1 < cursors.size()) info.cursorPressTheme = cursors.back();
        }

        const JsonValue* js = jo->getValue("sprites");
        if(js)
        {
            Sprites sprites = jsonSprites(*js);

            if(0 < sprites.size() && sprites[0].isValid()) info.sprite1 = sprites[0];
            if(1 < sprites.size() && sprites[1].isValid()) info.sprite2 = sprites[1];
            if(2 < sprites.size() && sprites[2].isValid()) info.sprite3 = sprites[2];
            if(3 < sprites.size() && sprites[3].isValid()) info.sprite4 = sprites[3];
        }

        JsonButton res(info);

        if(jo->hasKey("size"))
            res.setSize(JsonUnpack::size(*jo, "size"));

        if(jo->hasKey("position"))
            res.setPosition(JsonUnpack::point(*jo, "position"));

        if(jo->hasKey("hotkey"))
            res.setHotKey(Key::toKey(jo->getString("hotkey")));

        if(jo->hasKey("visible"))
	    res.setVisible(jo->getBoolean("visible"));

        if(jo->hasKey("tooltip"))
	    res.setToolTip(jo->getString("tooltip"));

        return res;
    }

    ERROR("not object");
    return JsonButton();
}

JsonButtons GameTheme::jsonButtons(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return JsonButtons();
    }

    return jsonButtons(*jv);
}

JsonButtons GameTheme::jsonButtons(const JsonValue & jv)
{
    if(jv.isString())
        return jsonResource(jv.getString()).toArray().toStdList<JsonButton>();
    else
    if(jv.isArray())
    {
        const JsonArray* ja = static_cast<const JsonArray*>(& jv);
        return ja->toStdList<JsonButton>();
    }

    ERROR("not array");
    return JsonButtons();
}

JsonTextInfo GameTheme::jsonTextInfo(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return JsonTextInfo();
    }

    return jsonTextInfo(*jv);
}

JsonTextInfo GameTheme::jsonTextInfo(const JsonValue & jv)
{
    if(jv.isString())
        return jsonTextInfo(jsonResource(jv.getString()).toObject());
    else
    if(jv.isObject())
    {
        const JsonObject* jo = static_cast<const JsonObject*>(& jv);
	return JsonTextInfo(jsonTextColor(*jo), jsonAlignPosition(*jo));
    }

    ERROR("not object");
    return JsonTextInfo();
}


JsonTextColor GameTheme::jsonTextColor(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return JsonTextColor();
    }

    return jsonTextColor(*jv);
}

JsonTextColor GameTheme::jsonTextColor(const JsonValue & jv)
{
    if(jv.isString())
        return jsonTextColor(jsonResource(jv.getString()).toObject());
    else
    if(jv.isObject())
    {
        const JsonObject* jo = static_cast<const JsonObject*>(& jv);

        JsonTextColor res;
        res.font = jo->getString("font");
        res.text = jo->getString("text");
        res.color = JsonUnpack::color(*jo, "color", Color::White);

        return res;
    }

    ERROR("not object");
    return JsonTextColor();
}

JsonAlignPosition GameTheme::jsonAlignPosition(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return JsonAlignPosition();
    }

    return jsonAlignPosition(*jv);
}

JsonAlignPosition GameTheme::jsonAlignPosition(const JsonValue & jv)
{
    if(jv.isString())
        return jsonAlignPosition(jsonResource(jv.getString()).toObject());
    else
    if(jv.isObject())
    {
        const JsonObject* jo = static_cast<const JsonObject*>(& jv);

        JsonAlignPosition res;
        res.position = JsonUnpack::point(*jo, "position");
        res.size = JsonUnpack::size(*jo, "size");
        res.align = AlignValue(jo->getString("align"), AlignLeft);

        return res;
    }

    ERROR("not object");
    return JsonAlignPosition();
}

SidesPositions GameTheme::jsonSidesPositions(const JsonObject & jo, const std::string & key)
{
    const JsonValue* jv = jo.getValue(key);

    if(! jv)
    {
        ERROR("tag not found: " << key);
	return SidesPositions();
    }

    return jsonSidesPositions(*jv);
}

SidesPositions GameTheme::jsonSidesPositions(const JsonValue & jv)
{
    if(jv.isString())
        return jsonSidesPositions(jsonResource(jv.getString()).toObject());
    else
    if(jv.isObject())
    {
        const JsonObject* jo = static_cast<const JsonObject*>(& jv);

        SidesPositions res;
        res.left = JsonUnpack::point(*jo, "left");
        res.right = JsonUnpack::point(*jo, "right");
        res.top = JsonUnpack::point(*jo, "top");
        res.bottom = JsonUnpack::point(*jo, "bottom");
        res.center = JsonUnpack::point(*jo, "center");

        return res;
    }

    ERROR("not object");
    return SidesPositions();
}

const JsonValue & operator>> (const JsonValue & jv, StoneInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.large = jo->getString("image1");
	st.medium = jo->getString("image2");
	st.small = jo->getString("image3");
	int type = jo->getInteger("type");
	int value = jo->getInteger("value");
	st.id = static_cast<Stone::stone_t>(type * 10 + value);
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, WindInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Wind(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.image = jo->getString("image");
    }
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, ClanInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Clan(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.image = jo->getString("image");
	st.flag1 = jo->getString("flag1");
	st.flag2 = jo->getString("flag2");
	st.town = jo->getString("town");
	st.button = jo->getString("button");
	st.townflag1 = jo->getString("townflag2");
	st.townflag2 = jo->getString("townflag1");
    }
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, BaseStat & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.attack = jo->getInteger("attack");
	st.ranger = jo->getInteger("ranger");
	st.defense = jo->getInteger("defense");
	st.loyalty = jo->getInteger("loyalty");
    }
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, CreatureStat & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.move = jo->getInteger("move");
	jv >> static_cast<BaseStat &>(st);
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, TownStat & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.point = jo->getInteger("point");
	st.power = jo->getBoolean("power");
	jv >> static_cast<BaseStat &>(st);
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, CreatureInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Creature(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.image1 = jo->getString("image1");
	st.image2 = jo->getString("image2");
	st.sound1 = jo->getString("sound1");
	st.unique = jo->getBoolean("unique");
	st.fly = jo->getBoolean("fly");
	st.cost = jo->getInteger("cost");
	st.sourceDescription = jo->getString("description");
	st.description = st.sourceDescription;
	st.specials = Specials(jo->getStdList<std::string>("specials"));

	StringList stoneList = jo->getStdList<std::string>("stones");
	std::transform(stoneList.begin(), stoneList.end(), stoneList.begin(),
					[](const std::string & str){ return String::trimmed(str); });
	st.stones.assign(stoneList.begin(), stoneList.end());

	jv >> st.stat;

	if(st.stat.ranger)
	    st.specials.set(Speciality::RangerAttack);
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, AbilityInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Ability(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.sourceDescription = jo->getString("description");
	st.description = st.sourceDescription;
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, SpecialityInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Speciality(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.sourceDescription = jo->getString("description");
	st.description = st.sourceDescription;
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, SpellInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Spell(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.target = SpellTarget(jo->getString("target"));

	auto arr = jo->getStdVector<int>("effect");
	if(3 < arr.size()) st.effect = BaseStat(arr[0], arr[1], arr[2], arr[3]);

	st.persistent = jo->getBoolean("persistent");
	st.cost = jo->getInteger("cost");
	st.extval = jo->getInteger("duration");
	st.value = jo->getInteger("value");
	st.image =jo->getString("image");
	st.sound =jo->getString("sound");
	st.sourceDescription = jo->getString("description");
	st.description = st.sourceDescription;

	StringList stoneList = jo->getStdList<std::string>("stones");
	std::transform(stoneList.begin(), stoneList.end(), stoneList.begin(),
				    [](const std::string & str){ return String::trimmed(str); });
	st.stones.assign(stoneList.begin(), stoneList.end());
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, AvatarInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Avatar(jo->getString("id"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.sourceDignity = jo->getString("dignity");
	st.dignity = st.sourceDignity;
	st.portrait = jo->getString("portrait");
	st.image = jo->getString("image");
	st.aiProfile = jo->getString("ai_profile", "balanced");
	st.sourceDescription = jo->getString("description");
	st.description = st.sourceDescription;

	if(jo->hasKey("ability")) st.ability = Ability(jo->getString("ability"));

	// clans
	StringList clanList = jo->getStdList<std::string>("clans");
	std::transform(clanList.begin(), clanList.end(), clanList.begin(),
				    [](const std::string & str){ return String::trimmed(str); });
	st.clans.assign(clanList.begin(), clanList.end());

	// spells
	StringList spellList = jo->getStdList<std::string>("spells");
	std::transform(spellList.begin(), spellList.end(), spellList.begin(),
				    [](const std::string & str){ return String::trimmed(str); });
	st.spells.assign(spellList.begin(), spellList.end());

	if(st.ability == Ability(Ability::Catasrophic))
	st.spells.push_back(Spell("hell_blast"));

	// creatures
	StringList creatureList = jo->getStdList<std::string>("creatures");
	std::transform(creatureList.begin(), creatureList.end(), creatureList.begin(),
				    [](const std::string & str){ return String::trimmed(str); });
	st.creatures.assign(creatureList.begin(), creatureList.end());
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, LandInfo & st)
{
    if(jv.isObject())
    {
	auto jo = static_cast<const JsonObject*>(& jv);

	st.id = Land(jo->getString("id"));
	st.clan = Clan(jo->getString("clan"));
	st.sourceName = jo->getString("name");
	st.name = st.sourceName;
	st.center = JsonUnpack::point(*jo, "center");
	st.area = JsonUnpack::rect(*jo, "area");
	st.iconrt = JsonUnpack::rect(*jo, "iconrt");

	auto ja = jo->getArray("points");
	if(ja) st.points = JsonUnpack::points(*ja);

	st.borders.clear();
	StringList landList = jo->getStdList<std::string>("borders");

	for(auto it = landList.begin(); it != landList.end(); ++it)
		st.borders.push_back(Land(String::trimmed(*it)));

	jv >> st.stat;
    }

    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, Sprite & st)
{
    st = GameTheme::jsonSprite(jv);
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, Sprites & st)
{
    st = GameTheme::jsonSprites(jv);
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, JsonButton & st)
{
    st = GameTheme::jsonButton(jv);
    return jv;
}

const JsonValue & operator>> (const JsonValue & jv, JsonButtons & st)
{
    st = GameTheme::jsonButtons(jv);
    return jv;
}
