#include "stdafx.h"
#include "main_loop.h"
#include "view.h"
#include "highscores.h"
#include "music.h"
#include "options.h"
#include "progress_meter.h"
#include "file_sharing.h"
#include "square.h"
#include "model_builder.h"
#include "parse_game.h"
#include "name_generator.h"
#include "view.h"
#include "village_control.h"
#include "campaign.h"
#include "game.h"
#include "model.h"
#include "clock.h"
#include "view_id.h"
#include "saved_game_info.h"
#include "retired_games.h"
#include "save_file_info.h"
#include "creature.h"
#include "campaign_builder.h"
#include "player_role.h"
#include "campaign_type.h"
#include "game_save_type.h"
#include "exit_info.h"
#include "tutorial.h"
#include "model.h"
#include "item_type.h"
#include "pretty_printing.h"
#include "content_factory.h"
#include "enemy_factory.h"
#include "external_enemies.h"
#include "game_config.h"
#include "avatar_menu_option.h"
#include "creature_name.h"
#include "tileset.h"
#include "content_factory.h"
#include "scroll_position.h"
#include "miniunz.h"
#include "external_enemies_type.h"
#include "mod_info.h"
#include "container_range.h"
#include "extern/iomanip.h"
#include "enemy_info.h"

#ifdef USE_STEAMWORKS
#include "steam_ugc.h"
#include "steam_client.h"
#endif

MainLoop::MainLoop(View* v, Highscores* h, FileSharing* fSharing, const DirectoryPath& freePath,
    const DirectoryPath& uPath, Options* o, Jukebox* j, SokobanInput* soko, TileSet* tileSet, bool singleThread, int sv, string modVersion)
      : view(v), dataFreePath(freePath), userPath(uPath), options(o), jukebox(j), highscores(h), fileSharing(fSharing),
        useSingleThread(singleThread), sokobanInput(soko), tileSet(tileSet), saveVersion(sv), modVersion(modVersion) {
}

vector<SaveFileInfo> MainLoop::getSaveFiles(const DirectoryPath& path, const string& suffix) {
  vector<SaveFileInfo> ret;
  for (auto file : path.getFiles()) {
    if (file.hasSuffix(suffix))
      ret.push_back({file.getFileName(), file.getModificationTime(), false});
  }
  sort(ret.begin(), ret.end(), [](const SaveFileInfo& a, const SaveFileInfo& b) {
        return a.date > b.date;
      });
  return ret;
}

static string getDateString(time_t t) {
  char buf[100];
  strftime(buf, sizeof(buf), "%c", std::localtime(&t));
  return buf;
}

bool MainLoop::isCompatible(int loadedVersion) {
  return loadedVersion > 2 && loadedVersion <= saveVersion && loadedVersion / 100 == saveVersion / 100;
}

GameConfig MainLoop::getGameConfig() const {
  string currentMod = options->getStringValue(OptionId::CURRENT_MOD2);
  return GameConfig(getModsDir(), std::move(currentMod));
}

static string getSaveSuffix(GameSaveType t) {
  switch (t) {
    case GameSaveType::KEEPER: return ".kep";
    case GameSaveType::ADVENTURER: return ".adv";
    case GameSaveType::RETIRED_SITE: return ".sit";
    case GameSaveType::RETIRED_CAMPAIGN: return ".cam";
    case GameSaveType::AUTOSAVE: return ".aut";
  }
}

template <typename T>
static optional<T> loadFromFile(const FilePath& filename, bool failSilently) {
  try {
    T obj;
    CompressedInput input(filename.getPath());
    string discard;
    SavedGameInfo discard2;
    int version;
    input.getArchive() >> version >> discard >> discard2;
    input.getArchive() >> obj;
    return std::move(obj);
  } catch (std::exception& ex) {
    if (failSilently)
      return none;
    else
      throw ex;
  }
}

static bool isNotFilename(char c) {
  return !(tolower(c) >= 'a' && tolower(c) <= 'z') && !isdigit(c) && c != '_';
}

static string stripFilename(string s) {
  s.erase(remove_if(s.begin(),s.end(), isNotFilename), s.end());
  return s;
}

void MainLoop::saveGame(PGame& game, const FilePath& path) {
  CompressedOutput out(path.getPath());
  string name = game->getGameDisplayName();
  SavedGameInfo savedInfo = game->getSavedGameInfo();
  savedInfo.spriteMods = tileSet->getSpriteMods();
  out.getArchive() << saveVersion << name << savedInfo;
  out.getArchive() << game;
}

struct RetiredModelInfo {
  PModel SERIAL(model);
  ContentFactory SERIAL(factory);
  SERIALIZE_ALL(model, factory)
};

void MainLoop::saveMainModel(PGame& game, const FilePath& path) {
  CompressedOutput out(path.getPath());
  string name = game->getGameDisplayName();
  SavedGameInfo savedInfo = game->getSavedGameInfo();
  savedInfo.spriteMods = tileSet->getSpriteMods();
  out.getArchive() << saveVersion << name << savedInfo;
  RetiredModelInfo info {
    std::move(game->getMainModel()),
    game->removeContentFactory()
  };
  out.getArchive() << info;
}

int MainLoop::getSaveVersion(const SaveFileInfo& save) {
  if (auto info = getNameAndVersion(userPath.file(save.filename)))
    return info->second;
  else
    return -1;
}

void MainLoop::uploadFile(const FilePath& path, const string& title, const SavedGameInfo& info) {
  atomic<bool> cancelled(false);
  optional<string> error;
  optional<string> url;
  doWithSplash(SplashType::AUTOSAVING, "Uploading "_s + path.getPath() + "...", 1,
      [&] (ProgressMeter& meter) {
        error = fileSharing->uploadSite(path, title, info, meter, url);
      },
      [&] {
        cancelled = true;
        fileSharing->cancel();
      });
  if (url)
    if (view->yesOrNoPrompt("Your retired dungeon has been uploaded to Steam Workshop. "
        "Would you like to open its page in your browser now?"))
      openUrl("https://steamcommunity.com/sharedfiles/filedetails/?id=" + *url);
  if (error && !cancelled)
    view->presentText("Error uploading file", *error);
}

FilePath MainLoop::getSavePath(const PGame& game, GameSaveType gameType) {
  return userPath.file(stripFilename(game->getGameIdentifier()) + getSaveSuffix(gameType));
}

void MainLoop::saveUI(PGame& game, GameSaveType type, SplashType splashType) {
  auto path = getSavePath(game, type);
  function<void()> uploadFun = nullptr;
  if (type == GameSaveType::RETIRED_SITE) {
    int saveTime = game->getMainModel()->getSaveProgressCount();
    doWithSplash(splashType, "Retiring site...", saveTime,
        [&] (ProgressMeter& meter) {
        Square::progressMeter = &meter;
        uploadFun = [this, path, name = game->getGameDisplayName(), savedInfo = game->getSavedGameInfo()] {
          uploadFile(path, name, savedInfo);
        };
        MEASURE(saveMainModel(game, path), "saving time")});
  } else {
    int saveTime = game->getSaveProgressCount();
    doWithSplash(splashType, type == GameSaveType::AUTOSAVE ? "Autosaving" : "Saving game...", saveTime,
        [&] (ProgressMeter& meter) {
        Square::progressMeter = &meter;
        MEASURE(saveGame(game, path), "saving time")});
  }
  Square::progressMeter = nullptr;
  if (uploadFun)
    uploadFun();
}

void MainLoop::eraseSaveFile(const PGame& game, GameSaveType type) {
  remove(getSavePath(game, type).getPath());
}

void MainLoop::getSaveOptions(const vector<pair<GameSaveType, string>>& games, vector<ListElem>& options,
    vector<SaveFileInfo>& allFiles) {
  for (auto elem : games) {
    vector<SaveFileInfo> files = getSaveFiles(userPath, getSaveSuffix(elem.first));
    files = files.filter([this] (const SaveFileInfo& info) { return isCompatible(getSaveVersion(info));});
    append(allFiles, files);
    if (!files.empty()) {
      options.emplace_back(elem.second, ListElem::TITLE);
      append(options, files.transform(
          [this] (const SaveFileInfo& info) {
              auto nameAndVersion = getNameAndVersion(userPath.file(info.filename));
              return ListElem(nameAndVersion->first, getDateString(info.date));}));
    }
  }
}

optional<SaveFileInfo> MainLoop::chooseSaveFile(const vector<ListElem>& options,
    const vector<SaveFileInfo>& allFiles, string noSaveMsg, View* view) {
  if (options.empty()) {
    view->presentText("", noSaveMsg);
    return none;
  }
  auto ind = view->chooseFromList("Choose game", options, 0);
  if (ind)
    return allFiles[*ind];
  else
    return none;
}

TimeInterval MainLoop::getAutosaveFreq() {
  return 1500_visible;
}

enum class MainLoop::ExitCondition {
  ALLIES_WON,
  ENEMIES_WON,
  TIMEOUT,
  UNKNOWN
};

void MainLoop::bugReportSave(PGame& game, FilePath path) {
  int saveTime = game->getSaveProgressCount();
  doWithSplash(SplashType::AUTOSAVING, "Saving game...", saveTime,
      [&] (ProgressMeter& meter) {
      Square::progressMeter = &meter;
      MEASURE(saveGame(game, path), "saving time")});
  Square::progressMeter = nullptr;
}

MainLoop::ExitCondition MainLoop::playGame(PGame game, bool withMusic, bool noAutoSave, bool splashScreen,
    function<optional<ExitCondition>(WGame)> exitCondition, milliseconds stepTimeMilli, optional<int> maxTurns) {
  if (!splashScreen)
    registerModPlaytime(true);
  OnExit on_exit([&]() {
    if (!splashScreen)
      registerModPlaytime(false);
  });

  tileSet->setTilePaths(game->getContentFactory()->tilePaths);
  view->reset();
  if (!noAutoSave)
    view->setBugReportSaveCallback([&] (FilePath path) { bugReportSave(game, path); });
  DestructorFunction removeCallback([&] { view->setBugReportSaveCallback(nullptr); });
  game->initialize(options, highscores, view, fileSharing);
  Intervalometer meter(stepTimeMilli);
  Intervalometer pausingMeter(stepTimeMilli);
  auto lastMusicUpdate = GlobalTime(-1000);
  auto lastAutoSave = game->getGlobalTime();
  optional<GlobalTime> exitTime;
  if (maxTurns)
    exitTime = game->getGlobalTime() + TimeInterval(*maxTurns);
  while (1) {
    if (exitTime && game->getGlobalTime() >= *exitTime)
      throw GameExitException();
    double step = 1;
    if (!game->isTurnBased()) {
      double gameTimeStep = view->getGameSpeed() / stepTimeMilli.count();
      auto timeMilli = view->getTimeMilli();
      double count = meter.getCount(timeMilli);
      //INFO << "Intervalometer " << timeMilli << " " << count;
      step = min(1.0, double(count) * gameTimeStep);
      if (maxTurns)
        step = 1;
      if (view->isClockStopped()) {
        // Advance the clock a little more until the local time reaches 0.99,
        // so creature animations are paused at their actual positions.
        double localTime = game->getMainModel()->getLocalTimeDouble();
        if (localTime - trunc(localTime) < pauseAnimationRemainder) {
          step = min(1.0, double(pausingMeter.getCount(view->getTimeMilliAbsolute())) * gameTimeStep);
          step = min(step, pauseAnimationRemainder - (localTime - trunc(localTime)));
        }
      } else
        pausingMeter.clear();
    }
    INFO << "Time step " << step;
    if (auto exitInfo = game->update(step)) {
      exitInfo->visit(
          [&](ExitAndQuit) {
            eraseAllSavesExcept(game, none);
          },
          [&](GameSaveType type) {
            if (type == GameSaveType::RETIRED_SITE) {
              game->prepareSiteRetirement();
              saveUI(game, type, SplashType::AUTOSAVING);
              game->doneRetirement();
            } else
              saveUI(game, type, SplashType::AUTOSAVING);
            eraseAllSavesExcept(game, type);
          }
      );
      return ExitCondition::UNKNOWN;
    }
    if (exitCondition)
      if (auto c = exitCondition(game.get()))
        return *c;
    auto gameTime = game->getGlobalTime();
    if (lastMusicUpdate < gameTime && withMusic) {
      jukebox->setType(game->getCurrentMusic(), game->changeMusicNow());
      lastMusicUpdate = gameTime;
    }
    if (lastAutoSave < gameTime - getAutosaveFreq() && !noAutoSave) {
      if (options->getBoolValue(OptionId::AUTOSAVE)) {
        saveUI(game, GameSaveType::AUTOSAVE, SplashType::AUTOSAVING);
        eraseAllSavesExcept(game, GameSaveType::AUTOSAVE);
      }
      lastAutoSave = gameTime;
    }
    view->refreshView();
  }
}

void MainLoop::eraseAllSavesExcept(const PGame& game, optional<GameSaveType> except) {
  for (auto erasedType : ENUM_ALL(GameSaveType))
    if (erasedType != except)
      eraseSaveFile(game, erasedType);
}

optional<RetiredGames> MainLoop::getRetiredGames(CampaignType type) {
  switch (type) {
    case CampaignType::FREE_PLAY: {
      RetiredGames ret;
      for (auto& info : getSaveFiles(userPath, getSaveSuffix(GameSaveType::RETIRED_CAMPAIGN)))
        if (isCompatible(getSaveVersion(info)))
          if (auto saved = loadSavedGameInfo(userPath.file(info.filename)))
            ret.addLocal(*saved, info, true);
      for (auto& info : getSaveFiles(userPath, getSaveSuffix(GameSaveType::RETIRED_SITE)))
        if (isCompatible(getSaveVersion(info)))
          if (auto saved = loadSavedGameInfo(userPath.file(info.filename)))
            ret.addLocal(*saved, info, false);
      optional<vector<FileSharing::SiteInfo>> onlineSites;
      doWithSplash(SplashType::SMALL, "Fetching list of retired dungeons from the server...",
          [&] { onlineSites = fileSharing->listSites(); }, [&] { fileSharing->cancel(); });
      if (onlineSites) {
        for (auto& elem : *onlineSites)
          if (isCompatible(elem.version))
            ret.addOnline(elem.gameInfo, elem.fileInfo, elem.totalGames, elem.wonGames, elem.subscribed);
      } else
        view->presentText("", "Failed to fetch list of retired dungeons from the server.");
      ret.sort();
      return ret;
    }
    default:
      return none;
  }
}

PGame MainLoop::prepareTutorial(const ContentFactory* contentFactory) {
  PGame game = loadGame(dataFreePath.file("tutorial.kep"));
  if (game) {
    USER_CHECK(contentFactory->immigrantsData.count("tutorial"));
    Tutorial::createTutorial(*game, contentFactory);
  } else
    view->presentText("Sorry", "Failed to load the tutorial :(");
  return game;
}

struct ModelTable {
  Table<PModel> models;
  vector<ContentFactory> factories;
};

TilePaths MainLoop::getTilePathsForAllMods() const {
  auto readTiles = [&] (const GameConfig* config) {
    vector<TileInfo> tileDefs;
    if (auto res = config->readObject(tileDefs, GameConfigId::TILES, nullptr))
      return optional<TilePaths>();
    return optional<TilePaths>(TilePaths(std::move(tileDefs), config->getModName()));
  };
  GameConfig currentConfig = getGameConfig();
  auto ret = readTiles(&currentConfig);
  for (auto modDir : getModsDir().getSubDirs()) {
    GameConfig config(getModsDir(), modDir);
    if (auto paths = readTiles(&config)) {
      if (ret)
        ret->merge(*paths);
      else
        ret = paths;
    }
  }
  USER_CHECK(ret) << "No available tile paths found";
  return *ret;
}

PGame MainLoop::prepareCampaign(RandomGen& random) {
  while (1) {
    auto contentFactory = createContentFactory(false);
    if (tileSet)
      tileSet->setTilePaths(contentFactory.tilePaths);
    auto avatarChoice = getAvatarInfo(view, &contentFactory.playerCreatures, options, &contentFactory.getCreatures());
    if (auto avatar = avatarChoice.getReferenceMaybe<AvatarInfo>()) {
      CampaignBuilder builder(view, random, options, contentFactory.villains, contentFactory.gameIntros, *avatar);
      tileSet->setTilePaths(getTilePathsForAllMods());
      if (auto setup = builder.prepareCampaign(bindMethod(&MainLoop::getRetiredGames, this), CampaignType::FREE_PLAY,
          contentFactory.getCreatures().getNameGenerator()->getNext(NameGeneratorId("WORLD")))) {
        auto name = options->getStringValue(OptionId::PLAYER_NAME);
        if (!name.empty())
          avatar->playerCreature->getName().setFirst(name);
        avatar->playerCreature->getName().useFullTitle();
        auto models = prepareCampaignModels(*setup, *avatar, random, &contentFactory);
        for (auto& f : models.factories)
          contentFactory.merge(std::move(f));
        return Game::campaignGame(std::move(models.models), *setup, std::move(*avatar), std::move(contentFactory));
      } else
        continue;
    } else {
      auto option = *avatarChoice.getValueMaybe<AvatarMenuOption>();
      switch (option) {
        case AvatarMenuOption::GO_BACK:
          return nullptr;
        case AvatarMenuOption::CHANGE_MOD:
          showMods();
          continue;
        case AvatarMenuOption::TUTORIAL: {
          auto contentFactory = createContentFactory(true);
          if (auto ret = prepareTutorial(&contentFactory))
            return ret;
          else
            continue;
        }
        case AvatarMenuOption::LOAD_GAME:
          if (auto ret = loadPrevious())
            return ret;
          else
            continue;
      }
    }
  }
}

void MainLoop::splashScreen() {
  ProgressMeter meter(1);
  jukebox->setType(MusicType::INTRO, true);
  GameConfig gameConfig(getModsDir(), "vanilla");
  auto contentFactory = createContentFactory(true);
  if (tileSet)
    tileSet->setTilePaths(contentFactory.tilePaths);
  EnemyFactory enemyFactory(Random, contentFactory.getCreatures().getNameGenerator(), contentFactory.enemies,
      contentFactory.buildingInfo, contentFactory.externalEnemies);
  auto model = ModelBuilder(&meter, Random, options, sokobanInput, &contentFactory, std::move(enemyFactory))
      .splashModel(dataFreePath.file("splash.txt"));
  playGame(Game::splashScreen(std::move(model), CampaignBuilder::getEmptyCampaign(), std::move(contentFactory)),
      false, true, true);
}

void MainLoop::showCredits(const FilePath& path) {
  ifstream in(path.getPath());
  CHECK(!!in);
  vector<ListElem> lines;
  while (1) {
    char buf[100];
    in.getline(buf, 100);
    if (!in)
      break;
    string s(buf);
    if (s.back() == ':')
      lines.emplace_back(s, ListElem::TITLE);
    else
      lines.emplace_back(s, ListElem::NORMAL);
  }
  view->presentList("Credits", lines, false);
}

DirectoryPath MainLoop::getModsDir() const {
  return dataFreePath.subdirectory(gameConfigSubdir);
}

const auto modVersionFilename = "version_info";

optional<ModVersionInfo> MainLoop::getLocalModVersionInfo(const string& mod) {
  if (mod == "vanilla") {
    return ModVersionInfo{0, 0, modVersion};
  }
  ifstream in(getModsDir().subdirectory(mod).file(modVersionFilename).getPath());
  ModVersionInfo info {};
  in >> info.steamId >> info.version >> info.compatibilityTag;
  if (info.compatibilityTag == modVersion) // this also handles the check if the file existed and had sane contents
    return info;
  else
    return none;
}

void MainLoop::updateLocalModVersion(const string& mod, const ModVersionInfo& info) {
  ofstream out(getModsDir().subdirectory(mod).file(modVersionFilename).getPath());
  if (!!out) {
    out << info.steamId << "\n" << info.version << "\n" << info.compatibilityTag << "\n";
  }
}

const auto modDetailsFilename = "details.txt";

optional<ModDetails> MainLoop::getLocalModDetails(const string& mod) {
  ifstream in(getModsDir().subdirectory(mod).file(modDetailsFilename).getPath());
  ModDetails ret;
  in >> std::quoted(ret.author) >> std::quoted(ret.description);
  return ret;
}

void MainLoop::updateLocalModDetails(const string& mod, const ModDetails& info) {
  ofstream out(getModsDir().subdirectory(mod).file(modDetailsFilename).getPath());
  if (!!out) {
    out << std::quoted(info.author) << "\n" << std::quoted(info.description) << "\n";
  }
}

void MainLoop::removeMod(const string& name) {
  // TODO: how to make it safer?
  auto modDir = getModsDir().subdirectory(name);
  modDir.removeRecursively();
}

// When mod changes name, we have to remove old directory
void MainLoop::removeOldSteamMod(SteamId steamId, const string& newName) {
  auto modDir = getModsDir();
  auto modList = modDir.getSubDirs();
  for (auto& modName : modList)
    if (modName != newName)
      if (auto ver = getLocalModVersionInfo(modName))
        if (ver->steamId == steamId)
          removeMod(modName);
}

vector<ModInfo> MainLoop::getAllMods(const vector<ModInfo>& onlineMods) {
  auto modList = getModsDir().getSubDirs();
  USER_CHECK(!modList.empty()) << "No game config data found, please make sure all game data is in place";
  string currentMod = options->getStringValue(OptionId::CURRENT_MOD2);
  // check if the currentMod exists and has current version
  if ([&]{for (auto& mod : modList) if (mod == currentMod && !!getLocalModVersionInfo(mod)) return false; return true;}())
    currentMod = "vanilla";
  vector<ModInfo> allMods;
  set<SteamId> alreadyDownloaded;
  for (auto& mod : modList)
    if (auto version = getLocalModVersionInfo(mod)) {
      ModInfo modInfo;
      modInfo.versionInfo = *version;
      modInfo.name = mod;
      modInfo.canUpload = (mod != "vanilla");
      if (auto details = getLocalModDetails(mod))
        modInfo.details = *details;
      for (auto& onlineMod : onlineMods)
        if (onlineMod.versionInfo.steamId == version->steamId) {
          modInfo = onlineMod;
          if (!modInfo.canUpload && modInfo.versionInfo.version > version->version)
            modInfo.actions.push_back("Update");
          alreadyDownloaded.insert(onlineMod.versionInfo.steamId);
          break;
        }
      if (currentMod == mod)
        modInfo.isActive = true;
      else
        modInfo.actions.push_back("Activate");
      if (modInfo.canUpload)
        modInfo.actions.push_back("Upload");
      modInfo.isLocal = true;
      allMods.push_back(std::move(modInfo));
    }
  for (auto& mod : onlineMods)
    if (!alreadyDownloaded.count(mod.versionInfo.steamId)) {
      allMods.push_back(mod);
      allMods.back().actions.push_back("Download");
    }
  return allMods;
}

void MainLoop::downloadMod(ModInfo& mod, const DirectoryPath& modDir) {
  atomic<bool> cancelled(false);
  optional<string> error;
  doWithSplash(SplashType::SMALL, "Downloading mod \"" + mod.name + "\"...", 1,
      [&] (ProgressMeter& meter) {
        error = fileSharing->downloadMod(mod.name, mod.versionInfo.steamId, modDir, meter);
        if (!error) {
          updateLocalModVersion(mod.name, mod.versionInfo);
          updateLocalModDetails(mod.name, mod.details);
          removeOldSteamMod(mod.versionInfo.steamId, mod.name);
        }
      },
      [&] {
        cancelled = true;
        fileSharing->cancel();
      });
  if (error && !cancelled)
    view->presentText("Error downloading file", *error);
}

void MainLoop::uploadMod(ModInfo& mod, const DirectoryPath& modDir) {
  GameConfig config(modDir, mod.name);
  ContentFactory f;
  if (auto err = f.readData(&config)) {
    view->presentText("Mod \"" + mod.name + "\" has errors: ", *err);
    return;
  }
  atomic<bool> cancelled(false);
  optional<string> error;
  doWithSplash(SplashType::SMALL, "Uploading mod \"" + mod.name + "\"...", 1,
      [&] (ProgressMeter& meter) {
        error = fileSharing->uploadMod(mod, modDir, meter);
        updateLocalModVersion(mod.name, mod.versionInfo);
      },
      [&] {
        cancelled = true;
        fileSharing->cancel();
      });
  if (error && !cancelled)
    view->presentText("Error uploading mod:", *error);
}

void MainLoop::createNewMod() {
  auto modsDir = getModsDir();
  if (auto name = view->getText("Enter name of new mod", "", 15)) {
    if (modsDir.getSubDirs().contains(*name)) {
      view->presentText("Error", "Mod \"" + *name + "\" is alread installed");
      return;
    }
    auto targetPath = modsDir.subdirectory(*name);
    doWithSplash(SplashType::SMALL, "Copying files...", 1,
       [&] (ProgressMeter& meter) {
         DirectoryPath::copyFiles(modsDir.subdirectory("vanilla"), targetPath, true);
       });
    updateLocalModVersion(*name, ModVersionInfo{0, 0, modVersion});
    updateLocalModDetails(*name, ModDetails{"", ""});
  }
}

vector<ModInfo> MainLoop::getOnlineMods() {
  vector<ModInfo> ret;
  doWithSplash(SplashType::SMALL, "Downloading list of online mods...", 1,
      [&] (ProgressMeter& meter) {
        ret = fileSharing->getOnlineMods().value_or(vector<ModInfo>());
        sort(ret.begin(), ret.end(), [](const ModInfo& m1, const ModInfo& m2) { return m1.upvotes > m2.upvotes; });
      });
  return ret;
}

void MainLoop::showMods() {
  auto modDir = getModsDir();
  int highlighted = 0;
  vector<ModInfo> onlineMods = getOnlineMods();
  while (1) {
    vector<ModInfo> allMods = getAllMods(onlineMods);
    auto choice = view->getModAction(highlighted, allMods);
    if (!choice)
      break;
    highlighted = choice->index;
    if (highlighted == -1) {
      createNewMod();
      return showMods();
    }
    auto& chosenMod = allMods[highlighted];
    auto action = chosenMod.actions[choice->actionId];
    if (action == "Activate")
      options->setValue(OptionId::CURRENT_MOD2, allMods[highlighted].name);
    else if (action == "Download" || action == "Update")
      downloadMod(chosenMod, modDir);
    else if (action == "Upload") {
      uploadMod(chosenMod, modDir);
      onlineMods = getOnlineMods();
    }
  }
}

void MainLoop::playMenuMusic() {
  jukebox->setType(MusicType::MAIN, true);
}

void MainLoop::considerGameEventsPrompt() {
  if (options->getIntValue(OptionId::GAME_EVENTS) == 1) {
    if (view->yesOrNoPrompt("The imps would like to gather statistics while you're playing the game and send them anonymously to the developer. This would be very helpful in designing the game. Do you agree?"))
      options->setValue(OptionId::GAME_EVENTS, 2);
    else
      options->setValue(OptionId::GAME_EVENTS, 0);
  }
}

void MainLoop::considerFreeVersionText(bool tilesPresent) {
  if (!tilesPresent)
    view->presentText("", "You are playing a version of KeeperRL without graphical tiles. "
        "Besides lack of graphics and music, this "
        "is the same exact game as the full version. If you'd like to buy the full version, "
        "please visit keeperrl.com.\n \nYou can also get it by donating to any wildlife charity. "
        "More information on the website.");
}

ContentFactory MainLoop::createContentFactory(bool vanillaOnly) const {
  ContentFactory ret;
  auto tryConfig = [this, &ret](const string& modName) {
    GameConfig config(getModsDir(), modName);
    return ret.readData(&config);
  };
  if (vanillaOnly) {
#ifdef RELEASE
    if (auto err = tryConfig("vanilla"))
      USER_FATAL << "Error loading vanilla game data: " << *err;
#else
    while (true) {
      if (auto err = tryConfig("vanilla"))
        USER_INFO << "Error loading vanilla game data: " << *err;
      else
        break;
    }
#endif
  } else {
    auto chosenMod = options->getStringValue(OptionId::CURRENT_MOD2);
    if (auto err = tryConfig(chosenMod)) {
      USER_INFO << "Error loading mod \"" << chosenMod << "\": " << *err << "\n\nUsing vanilla game data";
      if (auto err = tryConfig("vanilla"))
        USER_FATAL << "Error loading vanilla game data: " << *err;
    }
  }
  return ret;
}

void MainLoop::launchQuickGame(optional<int> maxTurns) {
  vector<ListElem> optionsUnused;
  vector<SaveFileInfo> files;
  getSaveOptions({
      {GameSaveType::AUTOSAVE, "Recovered games:"},
      {GameSaveType::KEEPER, "Keeper games:"}, {GameSaveType::ADVENTURER, "Adventurer games:"}}, optionsUnused, files);
  auto toLoad = std::min_element(files.begin(), files.end(),
      [](const auto& f1, const auto& f2) { return f1.date > f2.date; });
  PGame game;
  if (toLoad != files.end())
    game = loadGame(userPath.file((*toLoad).filename));
  auto contentFactory = createContentFactory(false);
  if (!game) {
    AvatarInfo avatar = getQuickGameAvatar(view, &contentFactory.playerCreatures, &contentFactory.getCreatures());
    CampaignBuilder builder(view, Random, options, contentFactory.villains, contentFactory.gameIntros, avatar);
    auto result = builder.prepareCampaign(bindMethod(&MainLoop::getRetiredGames, this), CampaignType::QUICK_MAP, "[world]");
    auto models = prepareCampaignModels(*result, std::move(avatar), Random, &contentFactory);
    game = Game::campaignGame(std::move(models.models), *result, std::move(avatar), std::move(contentFactory));
  }
  playGame(std::move(game), true, false, false, nullptr, milliseconds{3}, maxTurns);
}

void MainLoop::start(bool tilesPresent) {
  splashScreen();
  view->reset();
  considerFreeVersionText(tilesPresent);
  considerGameEventsPrompt();
  int lastIndex = 0;
  while (1) {
    playMenuMusic();
    optional<int> choice;
    choice = view->chooseFromList("", {
        "Play", "Settings", "High scores", "Credits", "Quit"}, lastIndex, MenuType::MAIN);
    if (!choice)
      continue;
    lastIndex = *choice;
    switch (*choice) {
      case 0: {
        if (PGame game = prepareCampaign(Random))
          playGame(std::move(game), true, false, false);
        view->reset();
        break;
      }
      case 1: options->handle(view, OptionSet::GENERAL); break;
      case 2: highscores->present(view); break;
      case 3: showCredits(dataFreePath.file("credits.txt")); break;
      case 4: return;
    }
  }
}

void MainLoop::doWithSplash(SplashType type, const string& text, int totalProgress,
    function<void(ProgressMeter&)> fun, function<void()> cancelFun) {
  ProgressMeter meter(1.0 / totalProgress);
  if (useSingleThread)
    fun(meter);
  else
    view->doWithSplash(type, text, totalProgress, std::move(fun), std::move(cancelFun));
}

void MainLoop::doWithSplash(SplashType type, const string& text, function<void()> fun, function<void()> cancelFun) {
  if (useSingleThread)
    fun();
  else {
    view->displaySplash(nullptr, text, type, cancelFun);
    thread t = makeThread([fun, this] { fun(); view->clearSplash(); });
    view->refreshView();
    t.join();
  }
}

void MainLoop::modelGenTest(int numTries, const vector<string>& types, RandomGen& random, Options* options) {
  ProgressMeter meter(1);
  auto contentFactory = createContentFactory(false);
  EnemyFactory enemyFactory(Random, contentFactory.getCreatures().getNameGenerator(), contentFactory.enemies,
      contentFactory.buildingInfo, contentFactory.externalEnemies);
  ModelBuilder(&meter, random, options, sokobanInput, &contentFactory, std::move(enemyFactory))
      .measureSiteGen(numTries, types);
}

static CreatureList readAlly(ifstream& input) {
  string ally;
  input >> ally;
  CreatureList ret(100, CreatureId(ally.data()));
  string levelIncreaseText;
  input >> levelIncreaseText;
  EnumMap<ExperienceType, int> levelIncrease;
  vector<ItemType> equipment;
  for (auto increase : split(levelIncreaseText, {','})) {
    int amount = fromString<int>(increase.substr(1));
    switch (increase[0]) {
      case 'M':
        levelIncrease[ExperienceType::MELEE] = amount;
        break;
      case 'S':
        levelIncrease[ExperienceType::SPELL] = amount;
        break;
      case 'A':
        levelIncrease[ExperienceType::ARCHERY] = amount;
        break;
      default:
        FATAL << "Can't parse level increase " << increase;
    }
  }
  string equipmentText;
  input >> equipmentText;
  for (auto id : split(equipmentText, {','})) {
    ItemType type;
    if (auto error = PrettyPrinting::parseObject(type, id))
      FATAL << "Can't parse item type: " << id << ": " << *error;
    else
      equipment.push_back(type);
  }
  ret.addInventory(equipment);
  ret.increaseBaseLevel(levelIncrease);
  return ret;
}

void MainLoop::battleTest(int numTries, const FilePath& levelPath, const FilePath& battleInfoPath, string enemy,
    RandomGen& random) {
  ifstream input(battleInfoPath.getPath());
  CreatureList enemies;
  for (auto& elem : split(enemy, {','})) {
    auto enemySplit = split(elem, {':'});
    auto enemyId = enemySplit[0];
    int count = enemySplit.size() > 1 ? fromString<int>(enemySplit[1]) : 1;
    for (int i : Range(count))
      enemies.addUnique(CreatureId(enemyId.data()));
  }
  int cnt = 0;
  input >> cnt;
  auto contentFactory = createContentFactory(false);
  for (int i : Range(cnt)) {
    auto allies = readAlly(input);
    std::cout << allies.getSummary(&contentFactory.getCreatures()) << ": ";
    battleTest(numTries, levelPath, allies, enemies, random);
  }
}

void MainLoop::endlessTest(int numTries, const FilePath& levelPath, const FilePath& battleInfoPath,
    RandomGen& random, optional<int> numEnemy) {
  ifstream input(battleInfoPath.getPath());
  int cnt = 0;
  input >> cnt;
  vector<CreatureList> allies;
  for (int i : Range(cnt))
    allies.push_back(readAlly(input));
  auto contentFactory = createContentFactory(false);
  ExternalEnemies enemies(random, &contentFactory.getCreatures(), EnemyFactory(random, contentFactory.getCreatures().getNameGenerator(),
      contentFactory.enemies, contentFactory.buildingInfo, contentFactory.externalEnemies)
      .getExternalEnemies(), ExternalEnemiesType::FROM_START);
  for (int turn : Range(100000))
    if (auto wave = enemies.popNextWave(LocalTime(turn))) {
      std::cerr << "Turn " << turn << ": " << wave->enemy.name << "\n";
      int totalWins = 0;
      for (auto& allyInfo : allies) {
        std::cerr << allyInfo.getSummary(&contentFactory.getCreatures()) << ": ";
        int numWins = battleTest(numTries, levelPath, allyInfo, wave->enemy.creatures, random);
        totalWins += numWins;
      }
      std::cerr << totalWins << " wins\n";
      std::cout << "Turn " << turn << ": " << wave->enemy.name << ": " << totalWins << "\n";
    }
}

optional<string> MainLoop::verifyMod(const string& path) {
  auto modsPath = userPath.subdirectory("mods_tmp");
  OnExit ex123([modsPath] { modsPath.removeRecursively(); });
  modsPath.createIfDoesntExist();
  if (auto err = unzip(path, modsPath.getPath()))
    return err;
  for (auto mod : modsPath.getSubDirs()) {
    GameConfig config(modsPath, mod);
    ContentFactory f;
    if (auto err = f.readData(&config))
      return err;
    else {
      std::cout << mod << std::endl;
      return none;
    }
  }
  return "Failed to load any mod"_s;
}

int MainLoop::battleTest(int numTries, const FilePath& levelPath, CreatureList ally, CreatureList enemies,
    RandomGen& random) {
  ProgressMeter meter(1);
  int numAllies = 0;
  int numEnemies = 0;
  int numUnknown = 0;
  auto allyTribe = TribeId::getDarkKeeper();
  std::cout.flush();
  for (int i : Range(numTries)) {
    std::cout << "Creating level" << std::endl;
    auto contentFactory = createContentFactory(false);
    EnemyFactory enemyFactory(Random, contentFactory.getCreatures().getNameGenerator(),
        contentFactory.enemies, contentFactory.buildingInfo, contentFactory.externalEnemies);
    auto model = ModelBuilder(&meter, Random, options, sokobanInput,
        &contentFactory, std::move(enemyFactory)).battleModel(levelPath, ally, enemies);
    auto game = Game::splashScreen(std::move(model), CampaignBuilder::getEmptyCampaign(), std::move(contentFactory));
    std::cout << "Done" << std::endl;
    auto exitCondition = [&](WGame game) -> optional<ExitCondition> {
      unordered_set<TribeId, CustomHash<TribeId>> tribes;
      for (auto& m : game->getAllModels())
        for (auto c : m->getAllCreatures())
          tribes.insert(c->getTribeId());
      if (tribes.size() == 1) {
        if (*tribes.begin() == allyTribe)
          return ExitCondition::ALLIES_WON;
        else
          return ExitCondition::ENEMIES_WON;
      }
      if (game->getGlobalTime().getVisibleInt() > 200)
        return ExitCondition::TIMEOUT;
      if (tribes.empty())
        return ExitCondition::UNKNOWN;
      else
        return none;
    };
    auto result = playGame(std::move(game), false, true, false, exitCondition, milliseconds{3});
    switch (result) {
      case ExitCondition::ALLIES_WON:
        ++numAllies;
        std::cerr << "a";
        break;
      case ExitCondition::ENEMIES_WON:
        ++numEnemies;
        std::cerr << "e";
        break;
      case ExitCondition::TIMEOUT:
        ++numUnknown;
        std::cerr << "t";
        break;
      case ExitCondition::UNKNOWN:
        ++numUnknown;
        std::cerr << "u";
        break;
    }
    std::cerr.flush();
  }
  std::cerr << " " << numAllies << ":" << numEnemies;
  if (numUnknown > 0)
    std::cerr << " (" << numUnknown << ") unknown";
  std::cerr << "\n";
  return numAllies;
}

PModel MainLoop::getBaseModel(ModelBuilder& modelBuilder, CampaignSetup& setup, const AvatarInfo& avatarInfo) {
  auto ret = [&] {
    switch (setup.campaign.getType()) {
      case CampaignType::SINGLE_KEEPER:
        return modelBuilder.singleMapModel(avatarInfo.playerCreature->getTribeId(), avatarInfo.tribeAlignment);
      case CampaignType::QUICK_MAP:
        return modelBuilder.tutorialModel();
      default:
        return modelBuilder.campaignBaseModel(avatarInfo.playerCreature->getTribeId(),
            avatarInfo.tribeAlignment, setup.externalEnemies);
    }
  }();
  return ret;
}

ModelTable MainLoop::prepareCampaignModels(CampaignSetup& setup, const AvatarInfo& avatarInfo, RandomGen& random,
    ContentFactory* contentFactory) {
  Table<PModel> models(setup.campaign.getSites().getBounds());
  auto& sites = setup.campaign.getSites();
  for (Vec2 v : sites.getBounds())
    if (auto retired = sites[v].getRetired()) {
      if (retired->fileInfo.download)
        downloadGame(retired->fileInfo);
    }
  optional<string> failedToLoad;
  int numSites = setup.campaign.getNumNonEmpty();
  vector<ContentFactory> factories;
  doWithSplash(SplashType::AUTOSAVING, "Generating map...", numSites,
      [&] (ProgressMeter& meter) {
        EnemyFactory enemyFactory(Random, contentFactory->getCreatures().getNameGenerator(), contentFactory->enemies,
            contentFactory->buildingInfo, contentFactory->externalEnemies);
        ModelBuilder modelBuilder(nullptr, random, options, sokobanInput, contentFactory, std::move(enemyFactory));
        for (Vec2 v : sites.getBounds()) {
          if (!sites[v].isEmpty())
            meter.addProgress();
          if (sites[v].getKeeper()) {
            models[v] = getBaseModel(modelBuilder, setup, avatarInfo);
          } else if (auto villain = sites[v].getVillain())
            models[v] = modelBuilder.campaignSiteModel(villain->enemyId, villain->type, avatarInfo.tribeAlignment);
          else if (auto retired = sites[v].getRetired()) {
            if (auto info = loadFromFile<RetiredModelInfo>(userPath.file(retired->fileInfo.filename), !useSingleThread)) {
              models[v] = std::move(info->model);
              factories.push_back(std::move(info->factory));
            } else {
              failedToLoad = retired->fileInfo.filename;
              setup.campaign.clearSite(v);
            }
          }
        }
      });
  if (failedToLoad)
    view->presentText("Sorry", "Error reading " + *failedToLoad + ". Leaving blank site.");
  return ModelTable{std::move(models), std::move(factories)};
}

PGame MainLoop::loadGame(const FilePath& file) {
  optional<PGame> game;
  if (auto info = loadSavedGameInfo(file))
    doWithSplash(SplashType::AUTOSAVING, "Loading "_s + file.getPath() + "...", info->progressCount,
        [&] (ProgressMeter& meter) {
          Square::progressMeter = &meter;
          INFO << "Loading from " << file;
          MEASURE(game = loadFromFile<PGame>(file, !useSingleThread), "Loading game");
    });
  Square::progressMeter = nullptr;
  return game ? std::move(*game) : nullptr;
}

bool MainLoop::downloadGame(const SaveFileInfo& file) {
  atomic<bool> cancelled(false);
  optional<string> error;
  doWithSplash(SplashType::AUTOSAVING, "Downloading " + file.filename + "...", 1,
      [&] (ProgressMeter& meter) {
        error = fileSharing->downloadSite(file, userPath, meter);
      },
      [&] {
        cancelled = true;
        fileSharing->cancel();
      });
  if (error && !cancelled)
    view->presentText("Error downloading file", *error);
  return !error;
}

static void changeSaveType(const FilePath& file, GameSaveType newType) {
  optional<FilePath> newFile;
  for (GameSaveType oldType : ENUM_ALL(GameSaveType)) {
    string suf = getSaveSuffix(oldType);
    if (file.hasSuffix(suf)) {
      if (oldType == newType)
        return;
      newFile = file.changeSuffix(suf, getSaveSuffix(newType));
      break;
    }
  }
  CHECK(!!newFile);
  remove(newFile->getPath());
  rename(file.getPath(), newFile->getPath());
}

PGame MainLoop::loadPrevious() {
  vector<ListElem> options;
  vector<SaveFileInfo> files;
  getSaveOptions({
      {GameSaveType::AUTOSAVE, "Recovered games:"},
      {GameSaveType::KEEPER, "Keeper games:"},
      {GameSaveType::ADVENTURER, "Adventurer games:"}}, options, files);
  optional<SaveFileInfo> savedGame = chooseSaveFile(options, files, "No saved games found.", view);
  if (savedGame) {
    PGame ret = loadGame(userPath.file(savedGame->filename));
    if (ret) {
      if (eraseSave())
        changeSaveType(userPath.file(savedGame->filename), GameSaveType::AUTOSAVE);
    } else
      view->presentText("Sorry", "Failed to load the save file :(");
    return ret;
  } else
    return nullptr;
}

bool MainLoop::eraseSave() {
#ifdef RELEASE
  return !options->getBoolValue(OptionId::KEEP_SAVEFILES);
#endif
  return false;
}

void MainLoop::registerModPlaytime(bool started) {
#ifdef USE_STEAMWORKS
  if (!steam::Client::isAvailable())
    return;

  string currentMod = options->getStringValue(OptionId::CURRENT_MOD2);
  if (auto localVer = getLocalModVersionInfo(currentMod)) {
    steam::ItemId itemId(localVer->steamId);
    auto& ugc = steam::UGC::instance();
    if (started)
      ugc.startPlaytimeTracking({itemId});
    else
      ugc.stopPlaytimeTracking({itemId});
  }
#endif
}
