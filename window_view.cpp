﻿/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"


#include "window_view.h"
#include "level.h"
#include "options.h"
#include "renderer.h"
#include "tile.h"
#include "clock.h"
#include "creature_view.h"
#include "view_index.h"
#include "map_memory.h"
#include "progress_meter.h"
#include "version.h"
#include "map_gui.h"
#include "minimap_gui.h"
#include "player_message.h"
#include "position.h"
#include "sound_library.h"
#include "player_role.h"
#include "file_sharing.h"
#include "fx_manager.h"
#include "fx_renderer.h"
#include "fx_info.h"
#include "fx_view_manager.h"
#include "draw_line.h"
#include "tileset.h"
#include "target_type.h"

using SDL::SDL_Keysym;
using SDL::SDL_Keycode;

View* WindowView::createDefaultView(ViewParams params) {
  return new WindowView(params);
}

View* WindowView::createLoggingView(OutputArchive& of, ViewParams params) {
  return createDefaultView(params);
  //return new LoggingView(of, new WindowView(params));
}

View* WindowView::createReplayView(InputArchive& ifs, ViewParams params) {
  return createDefaultView(params);
  //return new ReplayView(ifs, new WindowView(params));
}

constexpr int rightBarWidthCollective = 330;
constexpr int rightBarWidthPlayer = 330;
constexpr int bottomBarHeightCollective = 66;
constexpr int bottomBarHeightPlayer = 66;

Rectangle WindowView::getMapGuiBounds() const {
  auto type = gameInfo.infoType;
  if (gameInfo.takingScreenshot)
    return Rectangle(renderer.getSize());
  else
    switch (type) {
      case GameInfo::InfoType::PLAYER:
        return Rectangle(Vec2(rightBarWidthPlayer, 0), renderer.getSize() - Vec2(0, bottomBarHeightPlayer));
      case GameInfo::InfoType::BAND:
        return Rectangle(Vec2(rightBarWidthCollective, 0), renderer.getSize() - Vec2(0, bottomBarHeightCollective));
      case GameInfo::InfoType::SPECTATOR: {
        Vec2 levelSize = Level::getSplashVisibleBounds().getSize();
        return Rectangle(
            (renderer.getSize() - levelSize.mult(mapLayout->getSquareSize())) / 2,
            (renderer.getSize() + levelSize.mult(mapLayout->getSquareSize())) / 2);
        }
    }
}

int WindowView::getMinimapWidth() const {
  return max(149, renderer.getSize().x / 11);
}

Vec2 WindowView::getMinimapOrigin() const {
  Vec2 offset(-20, 70);
  return Vec2(renderer.getSize().x - getMinimapWidth(), 0) + offset;
}

WindowView::WindowView(ViewParams params) : renderer(params.renderer), gui(params.gui), useTiles(params.useTiles),
    options(params.options), clock(params.clock), guiBuilder(renderer, gui, clock, params.options, {
        [this](UserInput input) { inputQueue.push(input);},
        [this](SDL_Keysym ev) { keyboardAction(ev);},
        [this]() { refreshScreen(false);},
        [this](const string& s) { presentText("", s); },
        }), zoomUI(-1),
    soundLibrary(params.soundLibrary), bugreportSharing(params.bugreportSharing), bugreportDir(params.bugreportDir),
    installId(params.installId) {}

void WindowView::initialize(unique_ptr<fx::FXRenderer> fxRenderer, unique_ptr<FXViewManager> fxViewManager) {
  renderer.setFullscreen(options->getBoolValue(OptionId::FULLSCREEN));
  renderer.setVsync(options->getBoolValue(OptionId::VSYNC));
  renderer.enableCustomCursor(!options->getBoolValue(OptionId::DISABLE_CURSOR));
//  renderer.setFullscreenMode(options->getChoiceValue(OptionId::FULLSCREEN_RESOLUTION));
  renderer.initialize();
  renderer.setZoom(options->getBoolValue(OptionId::ZOOM_UI) ? 2 : 1);
  options->addTrigger(OptionId::FULLSCREEN, [this] (int on) {
    renderer.setFullscreen(on);
    renderer.initialize();
  });
  options->addTrigger(OptionId::VSYNC, [this] (int on) {
    renderer.setVsync(on);
  });
  options->addTrigger(OptionId::DISABLE_CURSOR, [this] (int on) { renderer.enableCustomCursor(!on); });
  //options->addTrigger(OptionId::FULLSCREEN_RESOLUTION, [this] (int index) { fullScreenResolution = index; });
  options->addTrigger(OptionId::ZOOM_UI, [this] (int on) { zoomUI = on; });
  renderThreadId = currentThreadId();
  vector<ViewLayer> allLayers;
  for (auto l : ENUM_ALL(ViewLayer))
    allLayers.push_back(l);
  asciiLayouts = {
    {MapLayout(Vec2(16, 20), allLayers),
    MapLayout(Vec2(8, 10),
        {ViewLayer::FLOOR_BACKGROUND, ViewLayer::FLOOR, ViewLayer::CREATURE})}, false};
  spriteLayouts = {{
    MapLayout(Vec2(48, 48), allLayers),
//    MapLayout(Vec2(36, 36), allLayers),
    MapLayout(Vec2(24, 24), allLayers)}, true};
  if (useTiles)
    currentTileLayout = spriteLayouts;
  else
    currentTileLayout = asciiLayouts;
  this->fxRenderer = fxRenderer.get();
  mapGui.reset(new MapGui({
      bindMethod(&WindowView::mapContinuousLeftClickFun, this),
      [this] (Vec2 pos) {
          if (!guiBuilder.getActiveButton(CollectiveTab::BUILDINGS)) {
            guiBuilder.closeOverlayWindowsAndClearButton();
            inputQueue.push(UserInput(UserInputId::TILE_CLICK, pos));
          }
      },
      bindMethod(&WindowView::mapRightClickFun, this),
      [this] { refreshInput = true;},
      },
      inputQueue,
      clock,
      options,
      &gui,
      std::move(fxRenderer),
      std::move(fxViewManager)));
  minimapGui.reset(new MinimapGui([this]() { inputQueue.push(UserInput(UserInputId::DRAW_LEVEL_MAP)); }));
  rebuildMinimapGui();
  guiBuilder.setMapGui(mapGui);
}

bool WindowView::isKeyPressed(SDL::SDL_Scancode code) {
  return SDL::SDL_GetKeyboardState(nullptr)[code];
}

void WindowView::mapContinuousLeftClickFun(Vec2 pos) {
  guiBuilder.closeOverlayWindows();
  optional<int> activeLibrary = guiBuilder.getActiveButton(CollectiveTab::TECHNOLOGY);
  optional<int> activeBuilding = guiBuilder.getActiveButton(CollectiveTab::BUILDINGS);
  auto collectiveTab = guiBuilder.getCollectiveTab();
  switch (gameInfo.infoType) {
    case GameInfo::InfoType::BAND:
/*      if (collectiveTab == CollectiveTab::MINIONS)
        inputQueue.push(UserInput(UserInputId::MOVE_TO, pos));
      else*/
      if (collectiveTab == CollectiveTab::BUILDINGS) {
        if (activeBuilding && (isKeyPressed(SDL::SDL_SCANCODE_LSHIFT) || isKeyPressed(SDL::SDL_SCANCODE_RSHIFT)))
          inputQueue.push(UserInput(UserInputId::RECT_SELECTION, BuildingClickInfo{pos, *activeBuilding}));
        else if (activeBuilding && (isKeyPressed(SDL::SDL_SCANCODE_LCTRL) || isKeyPressed(SDL::SDL_SCANCODE_RCTRL)))
          inputQueue.push(UserInput(UserInputId::RECT_DESELECTION, pos));
        else if (activeBuilding)
          inputQueue.push(UserInput(UserInputId::BUILD, BuildingClickInfo{pos, *activeBuilding}));
      }
      break;
    default:
      break;
  }
}

void WindowView::mapRightClickFun(Vec2 pos) {
  inputQueue.push(UserInput(UserInputId::CREATURE_MAP_CLICK_EXTENDED, pos));
  switch (gameInfo.infoType) {
    case GameInfo::InfoType::SPECTATOR:
    case GameInfo::InfoType::BAND:
      guiBuilder.closeOverlayWindows();
      guiBuilder.clearActiveButton();
      break;
    default:
      break;
  }
}

void WindowView::reset() {
  mapLayout = &currentTileLayout.layouts[0];
  gameReady = false;
  wasRendered = false;
  minimapGui->clear();
  mapGui->clearCenter();
  guiBuilder.reset();
  gameInfo = GameInfo{};
  soundQueue.clear();
}

void WindowView::displayOldSplash() {
  Rectangle menuPosition = guiBuilder.getMenuPosition(MenuType::MAIN_NO_TILES, 0);
  int margin = 10;
  renderer.drawImage(renderer.getSize().x / 2 - 415, menuPosition.bottom() + margin,
      gui.get(GuiFactory::TexId::SPLASH1));
  Texture& splash2 = gui.get(GuiFactory::TexId::SPLASH2);
  renderer.drawImage((renderer.getSize().x - splash2.getSize().x) / 2,
      menuPosition.top() - splash2.getSize().y - margin, splash2);
}

void WindowView::displayMenuSplash2() {
  drawMenuBackground(0, 0);
}

void WindowView::drawMenuBackground(double barState, double mouthState) {
  Texture& menuCore = gui.get(GuiFactory::TexId::MENU_CORE);
  Texture& menuMouth = gui.get(GuiFactory::TexId::MENU_MOUTH);
  double scale = double(renderer.getSize().y) / menuCore.getSize().y;
  int width = menuCore.getSize().x * scale;
  double mouthPos1 = 184 * menuCore.getSize().x / 1920;
  double mouthPos2 = 214 * menuCore.getSize().x / 1920;
  double mouthX = (renderer.getSize().x - (menuMouth.getSize().x + 5) * scale) / 2;
  renderer.drawFilledRectangle(mouthX, mouthPos1 * scale, 1 + mouthX + barState * menuMouth.getSize().x * scale,
      (mouthPos2 + menuMouth.getSize().y) * scale, Color(60, 76, 48));
  renderer.drawImage((renderer.getSize().x - width) / 2, 0, menuCore, scale);
  renderer.drawImage(mouthX, scale * (mouthPos1 * (1 - mouthState) + mouthPos2 * mouthState), menuMouth, scale);
  renderer.drawText(Color::WHITE, Vec2(30, renderer.getSize().y - 40), "Version " BUILD_DATE " " BUILD_VERSION,
      Renderer::NONE, 16);
  renderer.drawText(Color::WHITE, Vec2(30, renderer.getSize().y - 21), "Install id: " + installId,
      Renderer::NONE, 16);
}

void WindowView::getAutosaveSplash(const ProgressMeter& meter, const string& text) {
  SGuiElem window = gui.miniWindow(gui.empty(), []{});
  Vec2 windowSize(440, 70);
  Rectangle bounds((renderer.getSize() - windowSize) / 2, (renderer.getSize() + windowSize) / 2);
  Rectangle progressBar(bounds.minusMargin(15));
  window->setBounds(bounds);
  while (!splashDone) {
    refreshScreen(false);
    window->render(renderer);
    double progress = meter.getProgress();
    Rectangle bar(progressBar.topLeft(), Vec2(1 + progressBar.left() * (1.0 - progress) +
          progressBar.right() * progress, progressBar.bottom()));
    renderer.drawFilledRectangle(bar, Color::DARK_GREEN.transparency(50));
    renderer.drawText(Color::WHITE, Vec2(bounds.middle().x, bounds.top() + 20), text, Renderer::HOR);
    renderer.drawAndClearBuffer();
    sleep_for(milliseconds(30));
    Event event;
    while (renderer.pollEvent(event)) {
      propagateEvent(event, {});
      considerResizeEvent(event);
    }
  }
}

void WindowView::getSmallSplash(const string& text, function<void()> cancelFun) {
  SGuiElem window = gui.miniWindow(gui.empty(), []{});
  Vec2 windowSize(500, 90);
  string cancelText = "[cancel]";
  Rectangle bounds((renderer.getSize() - windowSize) / 2, (renderer.getSize() + windowSize) / 2);
  Rectangle progressBar(bounds.minusMargin(15));
  window->setBounds(bounds);
  while (!splashDone) {
    refreshScreen(false);
    window->render(renderer);
    renderer.drawText(Color::WHITE, Vec2(bounds.middle().x, bounds.top() + 20), text, Renderer::HOR);
    Rectangle cancelBut(bounds.middle().x - renderer.getTextLength(cancelText) / 2, bounds.top() + 50,
        bounds.middle().x + renderer.getTextLength(cancelText) / 2, bounds.top() + 80);
    if (cancelFun)
      renderer.drawText(Color::LIGHT_BLUE, cancelBut.topLeft(), cancelText);
    renderer.drawAndClearBuffer();
    sleep_for(milliseconds(30));
    Event event;
    while (renderer.pollEvent(event)) {
      propagateEvent(event, {});
      considerResizeEvent(event);
      if (event.type == SDL::SDL_MOUSEBUTTONDOWN && cancelFun) {
        if (Vec2(event.button.x, event.button.y).inRectangle(cancelBut))
          cancelFun();
      }
    }
  }
}

void WindowView::getBigSplash(const ProgressMeter& meter, const string& text, function<void()> cancelFun) {
  auto t0 = clock->getRealMillis();
  int mouthMillis = 400;
  Texture& loadingSplash = gui.get(GuiFactory::TexId::LOADING_SPLASH);
  string cancelText = "[cancel]";
  while (!splashDone) {
    Vec2 textPos = useTiles ? Vec2(renderer.getSize().x / 2, renderer.getSize().y * 0.5)
      : Vec2(renderer.getSize().x / 2, renderer.getSize().y - 60);
    Rectangle cancelBut(textPos.x - renderer.getTextLength(cancelText) / 2, textPos.y + 30,
        textPos.x + renderer.getTextLength(cancelText) / 2, textPos.y + 60);
    if (useTiles)
      drawMenuBackground(meter.getProgress(), min(1.0, (double)(clock->getRealMillis() - t0).count() / mouthMillis));
    else
      renderer.drawImage((renderer.getSize().x - loadingSplash.getSize().x) / 2,
          (renderer.getSize().y - loadingSplash.getSize().y) / 2, loadingSplash);
    renderer.drawText(Color::WHITE, textPos, text, Renderer::HOR);
    if (cancelFun)
      renderer.drawText(Color::LIGHT_BLUE, cancelBut.topLeft(), cancelText);
    renderer.drawAndClearBuffer();
    sleep_for(milliseconds(30));
    Event event;
    while (renderer.pollEvent(event)) {
      considerResizeEvent(event);
      if (event.type == SDL::SDL_MOUSEBUTTONDOWN && cancelFun) {
        if (Vec2(event.button.x, event.button.y).inRectangle(cancelBut))
          cancelFun();
      }
    }
  }
}

void WindowView::displaySplash(const ProgressMeter* meter, const string& text, SplashType type,
    function<void()> cancelFun) {
  splashDone = false;
  renderDialog.push([=] {
    switch (type) {
      case SplashType::BIG: getBigSplash(*meter, text, cancelFun); break;
      case SplashType::AUTOSAVING: getAutosaveSplash(*meter, text); break;
      case SplashType::SMALL: getSmallSplash(text, cancelFun); break;
    }
    splashDone = false;
    renderDialog.pop();
  });
}

void WindowView::clearSplash() {
  splashDone = true;
  if (currentThreadId() != renderThreadId)
    while (splashDone) {
      Progress::checkIfInterrupted();
    }
}

void WindowView::resize(int width, int height) {
  renderer.resize(width, height);
  refreshInput = true;
}

void WindowView::close() {
}

Color getSpeedColor(int value) {
  if (value > 100)
    return Color(max(0, 255 - (value - 100) * 2), 255, max(0, 255 - (value - 100) * 2));
  else
    return Color(255, max(0, 255 + (value - 100) * 4), max(0, 255 + (value - 100) * 4));
}

void WindowView::rebuildMinimapGui() {
  int width = getMinimapWidth();
  auto icons = guiBuilder.drawMinimapIcons(gameInfo);
  auto iconsHeight = *icons->getPreferredHeight();
  minimapDecoration = gui.margin(std::move(icons),
      gui.stack(gui.rectangle(Color::BLACK), gui.miniWindow(),
      gui.margins(gui.renderInBounds(SGuiElem(minimapGui)), 6)), iconsHeight, GuiFactory::MarginType::BOTTOM);
  auto origin = getMinimapOrigin();
  minimapDecoration->setBounds(Rectangle(origin, origin + Vec2(width, width + iconsHeight)));

}

void WindowView::rebuildGui() {
  INFO << "Rebuilding UI";
  rebuildMinimapGui();
  mapGui->setBounds(getMapGuiBounds());
  SGuiElem bottom, right;
  vector<GuiBuilder::OverlayInfo> overlays;
  int rightBarWidth = 0;
  int bottomBarHeight = 0;
  optional<int> topBarHeight;
  int rightBottomMargin = 30;
  tempGuiElems.clear();
  if (!options->getIntValue(OptionId::DISABLE_MOUSE_WHEEL)) {
    tempGuiElems.push_back(gui.mouseWheel([this](bool up) { zoom(up ? -1 : 1); }));
    tempGuiElems.back()->setBounds(getMapGuiBounds());
  }
  tempGuiElems.push_back(gui.keyHandler(bindMethod(&WindowView::keyboardAction, this)));
  tempGuiElems.back()->setBounds(getMapGuiBounds());
  if (gameInfo.takingScreenshot) {
    right = gui.empty();
    bottom = gui.empty();
    rightBarWidth = 0;
    bottomBarHeight = 0;
  } else
    switch (gameInfo.infoType) {
      case GameInfo::InfoType::SPECTATOR:
          right = gui.empty();
          bottom = gui.empty();
          rightBarWidth = 0;
          bottomBarHeight = 0;
          if (getMapGuiBounds().left() > 0) {
            tempGuiElems.push_back(gui.rectangle(Color::BLACK));
            tempGuiElems.back()->setBounds(Rectangle(0, 0, getMapGuiBounds().left(), renderer.getSize().y));
            tempGuiElems.push_back(gui.rectangle(Color::BLACK));
            tempGuiElems.back()->setBounds(Rectangle(Vec2(getMapGuiBounds().right(), 0), renderer.getSize()));
          }
          if (getMapGuiBounds().top() > 0) {
            tempGuiElems.push_back(gui.rectangle(Color::BLACK));
            tempGuiElems.back()->setBounds(Rectangle(0, 0, renderer.getSize().x, getMapGuiBounds().top()));
            tempGuiElems.push_back(gui.rectangle(Color::BLACK));
            tempGuiElems.back()->setBounds(Rectangle(Vec2(0, getMapGuiBounds().bottom()), renderer.getSize()));
          }
          break;
      case GameInfo::InfoType::PLAYER:
          right = guiBuilder.drawRightPlayerInfo(*gameInfo.playerInfo.getReferenceMaybe<PlayerInfo>());
          bottom = guiBuilder.drawBottomPlayerInfo(gameInfo);
          rightBarWidth = rightBarWidthPlayer;
          bottomBarHeight = bottomBarHeightPlayer;
          break;
      case GameInfo::InfoType::BAND:
          right = guiBuilder.drawRightBandInfo(gameInfo);
          bottom = guiBuilder.drawBottomBandInfo(gameInfo);
          rightBarWidth = rightBarWidthCollective;
          bottomBarHeight = bottomBarHeightCollective;
          topBarHeight = 85;
          break;
    }
  guiBuilder.drawOverlays(overlays, gameInfo);
  if (gameInfo.infoType != GameInfo::InfoType::SPECTATOR) {
    overlays.push_back({guiBuilder.drawMessages(gameInfo.messageBuffer, renderer.getSize().x - rightBarWidth),
                       GuiBuilder::OverlayInfo::MESSAGES});
    for (auto& overlay : overlays)
      if (overlay.alignment != GuiBuilder::OverlayInfo::GAME_SPEED) {
        Vec2 pos;
        if (auto width = overlay.elem->getPreferredWidth())
          if (auto height = overlay.elem->getPreferredHeight()) {
            pos = getOverlayPosition(overlay.alignment, *height, *width, rightBarWidth, bottomBarHeight);
            tempGuiElems.push_back(std::move(overlay.elem));
            *height = min(*height, renderer.getSize().y - pos.y - bottomBarHeight);
            tempGuiElems.back()->setBounds(Rectangle(pos, pos + Vec2(*width, *height)));
          }
      }
    if (!gameInfo.takingScreenshot) {
      tempGuiElems.push_back(gui.mainDecoration(rightBarWidth, bottomBarHeight, topBarHeight));
      tempGuiElems.back()->setBounds(Rectangle(renderer.getSize()));
    }
    tempGuiElems.push_back(gui.margins(std::move(right), 20, 20, 10, 0));
    tempGuiElems.back()->setBounds(Rectangle(Vec2(0, 0),
          Vec2(rightBarWidth, renderer.getSize().y - rightBottomMargin)));
    tempGuiElems.push_back(gui.margins(std::move(bottom), 105, 10, 105, 0));
    tempGuiElems.back()->setBounds(Rectangle(
          Vec2(rightBarWidth, renderer.getSize().y - bottomBarHeight), renderer.getSize()));
    for (auto& overlay : overlays)
      if (overlay.alignment == GuiBuilder::OverlayInfo::GAME_SPEED) {
        Vec2 pos;
        if (auto width = overlay.elem->getPreferredWidth())
          if (auto height = overlay.elem->getPreferredHeight()) {
            pos = getOverlayPosition(overlay.alignment, *height, *width, rightBarWidth, bottomBarHeight);
            tempGuiElems.push_back(std::move(overlay.elem));
            tempGuiElems.back()->setBounds(Rectangle(pos, pos + Vec2(*width, *height)));
          }
      }
  }
  propagateMousePosition(getClickableGuiElems());
}

Vec2 WindowView::getOverlayPosition(GuiBuilder::OverlayInfo::Alignment alignment, int height, int width,
    int rightBarWidth, int bottomBarHeight) {
  int bottomOffset = 35;
  int sideOffset = 0;
  int rightWindowHeight = 80;
  switch (alignment) {
    case GuiBuilder::OverlayInfo::MINIONS:
      return Vec2(rightBarWidth - 20, rightWindowHeight - 6);
    case GuiBuilder::OverlayInfo::TOP_LEFT:
      return Vec2(rightBarWidth + sideOffset, rightWindowHeight);
    case GuiBuilder::OverlayInfo::IMMIGRATION:
      return Vec2(rightBarWidth, renderer.getSize().y - bottomBarHeight - 21 - height);
    case GuiBuilder::OverlayInfo::VILLAINS:
      return Vec2(rightBarWidth, renderer.getSize().y - bottomBarHeight - height);
    case GuiBuilder::OverlayInfo::BOTTOM_LEFT:
      return Vec2(rightBarWidth + guiBuilder.getImmigrationBarWidth(),
                 max(70, renderer.getSize().y - bottomBarHeight - 27 - height));
    case GuiBuilder::OverlayInfo::LEFT:
      return Vec2(sideOffset,
          renderer.getSize().y - bottomBarHeight - bottomOffset - height);
    case GuiBuilder::OverlayInfo::MESSAGES:
      return Vec2(rightBarWidth, 0);
    case GuiBuilder::OverlayInfo::GAME_SPEED:
      return Vec2(26, renderer.getSize().y - height - 50);
    case GuiBuilder::OverlayInfo::TUTORIAL:
      return Vec2(rightBarWidth + guiBuilder.getImmigrationBarWidth(),
          renderer.getSize().y - bottomBarHeight - height);
    case GuiBuilder::OverlayInfo::MAP_HINT:
      return Vec2(renderer.getSize().x - width, renderer.getSize().y - bottomBarHeight - height);
    case GuiBuilder::OverlayInfo::CENTER:
      return (renderer.getSize() - Vec2(width, height)) / 2;
  }
}

void WindowView::propagateMousePosition(const vector<SGuiElem>& elems) {
  Event ev;
  ev.type = SDL::SDL_MOUSEMOTION;
  ev.motion.x = renderer.getMousePos().x;
  ev.motion.y = renderer.getMousePos().y;
  gui.propagateEvent(ev, elems);
}

vector<SGuiElem> WindowView::getAllGuiElems() {
  if (!gameReady)
    return blockingElems;
  CHECK(currentThreadId() == renderThreadId);
  if (gameInfo.infoType == GameInfo::InfoType::SPECTATOR)
    return concat({mapGui}, tempGuiElems);
  vector<SGuiElem> ret = concat(tempGuiElems, blockingElems);
  if (gameReady)
    ret = concat({mapGui, minimapDecoration}, ret);
  return ret;
}

vector<SGuiElem> WindowView::getClickableGuiElems() {
  CHECK(currentThreadId() == renderThreadId);
  if (gameInfo.infoType == GameInfo::InfoType::SPECTATOR)
    return {mapGui};
  vector<SGuiElem> ret = concat(tempGuiElems, blockingElems);
  std::reverse(ret.begin(), ret.end());
  if (gameReady) {
    ret.push_back(minimapDecoration);
    ret.push_back(mapGui);
  }
  return ret;
}

void WindowView::setScrollPos(Position pos) {
  mapGui->setCenter(pos.getCoord(), pos.getLevel());
}

void WindowView::resetCenter() {
  mapGui->resetScrolling();
}

void WindowView::drawLevelMap(const CreatureView* creature) {
  Semaphore sem;
  auto gui = guiBuilder.drawLevelMap(sem, creature);
  auto minimapOrigin = getMinimapOrigin();
  Vec2 origin(minimapOrigin.x + getMinimapWidth() - *gui->getPreferredWidth(), minimapOrigin.y);
  return getBlockingGui(sem, std::move(gui), origin);
}

void WindowView::updateMinimap(const CreatureView* creature) {
  Vec2 rad(40, 40);
  Vec2 playerPos = mapGui->getScreenPos().div(mapLayout->getSquareSize());
  Rectangle bounds(playerPos - rad, playerPos + rad);
  minimapGui->update(bounds, creature, renderer);
}

void WindowView::updateView(CreatureView* view, bool noRefresh) {
  ScopeTimer timer("UpdateView timer");
  if (!wasRendered && currentThreadId() != renderThreadId)
    return;
  gameInfo = {};
  view->refreshGameInfo(gameInfo);
  if (gameInfo.infoType != GameInfo::InfoType::BAND)
    guiBuilder.clearActiveButton();
  wasRendered = false;
  guiBuilder.addUpsCounterTick();
  gameReady = true;
  if (!noRefresh)
    uiLock = false;
  switchTiles();
  rebuildGui();
  mapGui->setSpriteMode(currentTileLayout.sprites);
  bool spectator = gameInfo.infoType == GameInfo::InfoType::SPECTATOR;
  mapGui->updateObjects(view, renderer, mapLayout, true, !spectator, gameInfo.tutorial);
  updateMinimap(view);
  if (gameInfo.infoType == GameInfo::InfoType::SPECTATOR)
    guiBuilder.setGameSpeed(GuiBuilder::GameSpeed::NORMAL);
  if (soundLibrary)
    playSounds(view);
}

void WindowView::playSounds(const CreatureView* view) {
  Rectangle area = mapLayout->getAllTiles(getMapGuiBounds(), Level::getMaxBounds(), mapGui->getScreenPos());
  auto curTime = clock->getRealMillis();
  const milliseconds soundCooldown {70};
  for (auto& sound : soundQueue) {
    auto lastTime = lastPlayed[sound.getId()];
    if ((!lastTime || curTime > *lastTime + soundCooldown) && (!sound.getPosition() ||
        (sound.getPosition()->isSameLevel(view->getCreatureViewLevel()) &&
         sound.getPosition()->getCoord().inRectangle(area)))) {
      soundLibrary->playSound(sound);
      lastPlayed[sound.getId()] = curTime;
    }
  }
  soundQueue.clear();
}

void WindowView::animateObject(Vec2 begin, Vec2 end, optional<ViewId> object, optional<FXInfo> fx) {
  if (fx && mapGui->fxesAvailable())
    mapGui->addAnimation(FXSpawnInfo(*fx, begin, end - begin));
  else if (object && begin != end)
    mapGui->addAnimation(
        Animation::thrownObject(
          (end - begin).mult(mapLayout->getSquareSize()),
          *object,
          currentTileLayout.sprites),
        begin);
}

void WindowView::animation(Vec2 pos, AnimationId id, Dir orientation) {
  if (currentTileLayout.sprites)
    mapGui->addAnimation(Animation::fromId(id, orientation), pos);
}

void WindowView::animation(const FXSpawnInfo& spawnInfo) {
  mapGui->addAnimation(spawnInfo);
}

void WindowView::refreshView() {
/*    if (!wasRendered && gameReady)
      rebuildGui();*/
  wasRendered = true;
  CHECK(currentThreadId() == renderThreadId);
  if (gameReady || !blockingElems.empty())
    processEvents();
  if (!renderDialog.empty())
    renderDialog.top()();
  if (uiLock && renderDialog.empty() && blockingElems.empty())
    return;
  if ((renderDialog.empty() && gameReady) || !blockingElems.empty())
    refreshScreen(true);
}

void WindowView::drawMap() {
  for (auto gui : getAllGuiElems())
    gui->render(renderer);
  Vec2 mousePos = renderer.getMousePos();
  if (GuiElem* dragged = gui.getDragContainer().getGui())
    if (gui.getDragContainer().getOrigin().dist8(mousePos) > 30) {
      dragged->setBounds(Rectangle(mousePos + Vec2(15, 15), mousePos + Vec2(35, 35)));
      dragged->render(renderer);
    }
  guiBuilder.addFpsCounterTick();
}

static Rectangle getBugReportPos(Renderer& renderer) {
  return Rectangle(renderer.getSize() - Vec2(100, 23), renderer.getSize());
}

void WindowView::refreshScreen(bool flipBuffer) {
  {
    if (zoomUI > -1) {
      renderer.setZoom(zoomUI ? 2 : 1);
      zoomUI = -1;
    }
    if (!gameReady) {
      if (useTiles)
        displayMenuSplash2();
      else
        displayOldSplash();
    }
    drawMap();
  }
  auto bugReportPos = getBugReportPos(renderer);
  renderer.drawFilledRectangle(bugReportPos, Color::TRANSPARENT, Color::RED);
  renderer.drawText(Color::RED, bugReportPos.middle() - Vec2(0, 2), "report bug", Renderer::CenterType::HOR_VER);
  if (flipBuffer)
    renderer.drawAndClearBuffer();
}

int indexHeight(const vector<ListElem>& options, int index) {
  CHECK(index < options.size() && index >= 0);
  int tmp = 0;
  for (int i : All(options))
    if (options[i].getMod() == ListElem::NORMAL && tmp++ == index)
      return i;
  FATAL << "Bad index " << int(options.size()) << " " << index;
  return -1;
}

optional<int> reverseIndexHeight(const vector<ListElem>& options, int height) {
  if (height < 0 || height >= options.size() || options[height].getMod() != ListElem::NORMAL)
    return none;
  int sub = 0;
  for (int i : Range(height))
    if (options[i].getMod() != ListElem::NORMAL)
      ++sub;
  return height - sub;
}

optional<Vec2> WindowView::chooseDirection(Vec2 playerPos, const string& message) {
  TempClockPause pause(clock);
  gameInfo.messageBuffer = makeVec(PlayerMessage(message));
  SyncQueue<optional<Vec2>> returnQueue;
  addReturnDialog<optional<Vec2>>(returnQueue, [=] ()-> optional<Vec2> {
  rebuildGui();
  refreshScreen();
  do {
    auto pos = mapGui->projectOnMap(renderer.getMousePos());
    Event event;
    if (renderer.pollEvent(event)) {
      considerResizeEvent(event);
      if (event.type == SDL::SDL_KEYDOWN)
        switch (event.key.keysym.sym) {
          case SDL::SDLK_UP:
          case SDL::SDLK_KP_8: refreshScreen(); return Vec2(0, -1);
          case SDL::SDLK_KP_9: refreshScreen(); return Vec2(1, -1);
          case SDL::SDLK_RIGHT:
          case SDL::SDLK_KP_6: refreshScreen(); return Vec2(1, 0);
          case SDL::SDLK_KP_3: refreshScreen(); return Vec2(1, 1);
          case SDL::SDLK_DOWN:
          case SDL::SDLK_KP_2: refreshScreen(); return Vec2(0, 1);
          case SDL::SDLK_KP_1: refreshScreen(); return Vec2(-1, 1);
          case SDL::SDLK_LEFT:
          case SDL::SDLK_KP_4: refreshScreen(); return Vec2(-1, 0);
          case SDL::SDLK_KP_7: refreshScreen(); return Vec2(-1, -1);
          case SDL::SDLK_ESCAPE: refreshScreen(); return none;
          default: break;
        }
      if (pos && event.type == SDL::SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT)
          return (*pos - playerPos).getBearing();
        else
          return none;
      }
    }
    refreshScreen(false);
    if (pos && pos != playerPos) {
      Vec2 dir = (*pos - playerPos).getBearing();
      Vec2 wpos = mapLayout->projectOnScreen(getMapGuiBounds(), mapGui->getScreenPos(),
          playerPos.x + dir.x, playerPos.y + dir.y);
      if (currentTileLayout.sprites) {
        static vector<TileCoord> coords;
        if (coords.empty())
          for (int i = 0; i < 8; ++i)
            coords.push_back(renderer.getTileSet().getTileCoord("arrow" + toString(i)).getOnlyElement());
        renderer.drawTile(wpos, {coords[int(dir.getCardinalDir())]}, mapLayout->getSquareSize());
      } else {
        int numArrow = int(dir.getCardinalDir());
        static string arrows[] = { u8"⇑", u8"⇓", u8"⇒", u8"⇐", u8"⇗", u8"⇖", u8"⇘", u8"⇙"};
        renderer.drawText(Renderer::SYMBOL_FONT, mapLayout->getSquareSize().y, Color::WHITE,
            wpos + Vec2(mapLayout->getSquareSize().x / 2, 0), arrows[numArrow], Renderer::HOR);
      }
    }
    /*if (auto *inst = fx::FXManager::getInstance())
      inst->simulateStableTime(double(clock->getRealMillis().count()) * 0.001);*/
    renderer.drawAndClearBuffer();
    renderer.flushEvents(SDL::SDL_MOUSEMOTION);
  } while (1);
  });
  return returnQueue.pop();
}

optional<Vec2> WindowView::chooseTarget(Vec2 playerPos, TargetType targetType, Table<PassableInfo> passable, const string& message) {
  TempClockPause pause(clock);
  gameInfo.messageBuffer = makeVec(PlayerMessage(message));
  SyncQueue<optional<Vec2>> returnQueue;
  addReturnDialog<optional<Vec2>>(returnQueue, [=] ()-> optional<Vec2> {
  rebuildGui();
  refreshScreen();
  guiBuilder.disableClickActions = true;
  do {
    auto pos = mapGui->projectOnMap(renderer.getMousePos());
    Event event;
    if (renderer.pollEvent(event)) {
      considerResizeEvent(event);
      if (event.type == SDL::SDL_KEYDOWN && event.key.keysym.sym == SDL::SDLK_ESCAPE) {
        refreshScreen();
        return none;
      }
      if (pos && event.type == SDL::SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT)
          return *pos;
        else
          return none;
      } else
        gui.propagateEvent(event, {mapGui});
    }
    rebuildGui();
    refreshScreen(false);
    if (pos) {
      auto drawPoint = [&] (Vec2 wpos, Color color) {
        if (currentTileLayout.sprites) {
          renderer.drawViewObject(wpos, ViewId("dig_mark"), true, mapLayout->getSquareSize(), color);
        } else {
          renderer.drawText(Renderer::SYMBOL_FONT, mapLayout->getSquareSize().y, color,
              wpos + Vec2(mapLayout->getSquareSize().x / 2, 0), "0", Renderer::HOR);
        }
      };
      switch (targetType) {
        case TargetType::SHOW_ALL:
          for (auto v : passable.getBounds()) {
            auto color = [&] {
              switch (passable[v]) {
                case PassableInfo::UNKNOWN:
                  return Color::TRANSPARENT;
                case PassableInfo::PASSABLE:
                  return Color::WHITE.transparency(100);
                case PassableInfo::STOPS_HERE:
                case PassableInfo::NON_PASSABLE:
                  return Color::GREEN;
              }
            }();
            drawPoint(mapLayout->projectOnScreen(getMapGuiBounds(), mapGui->getScreenPos(), v.x, v.y), color);
          }
          break;
        case TargetType::POSITION: {
          auto color = pos->inRectangle(passable.getBounds()) && passable[*pos] == PassableInfo::PASSABLE ? Color::GREEN : Color::RED;
          drawPoint(mapLayout->projectOnScreen(getMapGuiBounds(), mapGui->getScreenPos(), pos->x, pos->y), color);
          break;
        }
        case TargetType::TRAJECTORY: {
          bool wasObstructed = false;
          auto line = drawLine(playerPos, *pos);
          for (auto& pw : line)
            if (pw != playerPos || line.size() == 1) {
              bool obstructed = wasObstructed || !pw.inRectangle(passable.getBounds()) ||
                  passable[pw] == PassableInfo::NON_PASSABLE;
              auto color = obstructed ? Color::RED : Color::GREEN;
              if (!wasObstructed && pw.inRectangle(passable.getBounds()) && passable[pw] == PassableInfo::UNKNOWN)
                color = Color::ORANGE;
              Vec2 wpos = mapLayout->projectOnScreen(getMapGuiBounds(), mapGui->getScreenPos(), pw.x, pw.y);
              wasObstructed = obstructed || passable[pw] == PassableInfo::STOPS_HERE;
              drawPoint(wpos, color);
            }
          break;
        }
      }
    }
    renderer.drawAndClearBuffer();
    renderer.flushEvents(SDL::SDL_MOUSEMOTION);
  } while (1);
  });
  guiBuilder.disableClickActions = false;
  return returnQueue.pop();
}

bool WindowView::yesOrNoPrompt(const string& message, bool defaultNo) {
  int index = defaultNo ? 1 : 0;
  return chooseFromListInternal("", {ListElem(capitalFirst(message), ListElem::TITLE), "Yes", "No"}, index,
      MenuType::YES_NO, nullptr) == 0;
}

bool WindowView::yesOrNoPromptBelow(const string &message, bool defaultNo) {
  int index = defaultNo ? 1 : 0;
  return chooseFromListInternal("", {ListElem(capitalFirst(message), ListElem::TITLE), "Yes", "No"}, index,
      MenuType::YES_NO_BELOW, nullptr) == 0;
}

optional<int> WindowView::getNumber(const string& title, Range range, int initial, int increments) {
  SyncQueue<optional<int>> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawChooseNumberMenu(returnQueue, title, range, initial, increments));
}

optional<int> WindowView::chooseFromList(const string& title, const vector<ListElem>& options, int index,
    MenuType type, ScrollPosition* scrollPos, optional<UserInputId> exitAction) {
  return chooseFromListInternal(title, options, index, type, scrollPos);
}

optional<string> WindowView::getText(const string& title, const string& value, int maxLength, const string& hint) {
  TempClockPause pause(clock);
  SyncQueue<optional<string>> returnQueue;
  addReturnDialog<optional<string>>(returnQueue, [=] ()-> optional<string> {
    return guiBuilder.getTextInput(title, value, maxLength, hint);
  });
  return returnQueue.pop();
}

Rectangle WindowView::getEquipmentMenuPosition(int height) {
  int width = 440;
  Vec2 origin = Vec2(rightBarWidthCollective, 200);
  origin.x = min(origin.x, renderer.getSize().x - width);
  return Rectangle(origin, origin + Vec2(width, height)).intersection(Rectangle(Vec2(0, 0), renderer.getSize()));
}

optional<int> WindowView::chooseItem(const vector<ItemInfo>& items, ScrollPosition* scrollPos1) {
  uiLock = true;
  TempClockPause pause(clock);
  SyncQueue<optional<int>> returnQueue;
  addReturnDialog<optional<int>>(returnQueue, [=] ()-> optional<int> {
    ScrollPosition* scrollPos = scrollPos1;
    ScrollPosition localScrollPos;
    if (!scrollPos)
      scrollPos = &localScrollPos;
    optional<optional<int>> retVal;
    vector<SGuiElem> lines;
    lines.push_back(gui.getListBuilder()
        .addElemAuto(gui.label("Available items:"))
        .addBackElem(gui.label("Owners:"), guiBuilder.getItemLineOwnerMargin()).buildHorizontalList());
    lines.append(guiBuilder.drawItemMenu(items,
      [&retVal] (Rectangle butBounds, optional<int> a) { retVal = a;}, true));
    int menuHeight = lines.size() * guiBuilder.getStandardLineHeight() + 30;
    SGuiElem menu = gui.miniWindow(gui.margins(
            gui.scrollable(gui.verticalList(std::move(lines), guiBuilder.getStandardLineHeight()), scrollPos),
        15), [&retVal] { retVal = optional<int>(none); });
    SGuiElem bg2 = gui.darken();
    bg2->setBounds(Rectangle(renderer.getSize()));
    while (1) {
      refreshScreen(false);
      menu->setBounds(getEquipmentMenuPosition(menuHeight));
      bg2->render(renderer);
      menu->render(renderer);
      renderer.drawAndClearBuffer();
      Event event;
      while (renderer.pollEvent(event)) {
        considerResizeEvent(event);
        propagateEvent(event, {menu});
        if (retVal)
          return *retVal;
        if (considerResizeEvent(event))
          continue;
      }
    }
  });
  return returnQueue.pop();
}

optional<UniqueEntity<Item>::Id> WindowView::chooseTradeItem(const string& title, pair<ViewId, int> budget,
    const vector<ItemInfo>& items, ScrollPosition* scrollPos) {
  SyncQueue<optional<UniqueEntity<Item>::Id>> returnQueue;
  auto menu = guiBuilder.drawTradeItemMenu(returnQueue, title, budget, items, scrollPos);
  int width = *menu->getPreferredWidth();
  int height = *menu->getPreferredHeight();
  return getBlockingGui(returnQueue, std::move(menu),
      getOverlayPosition(GuiBuilder::OverlayInfo::BOTTOM_LEFT, height, width, rightBarWidthCollective, bottomBarHeightCollective));
}

optional<int> WindowView::choosePillageItem(const string& title, const vector<ItemInfo>& items, ScrollPosition* scrollPos) {
  SyncQueue<optional<int>> returnQueue;
  auto menu = guiBuilder.drawPillageItemMenu(returnQueue, title, items, scrollPos);
  int width = *menu->getPreferredWidth();
  int height = *menu->getPreferredHeight();
  return getBlockingGui(returnQueue, std::move(menu),
                        getOverlayPosition(GuiBuilder::OverlayInfo::BOTTOM_LEFT, height, width, rightBarWidthCollective, bottomBarHeightCollective));
}

optional<ExperienceType> WindowView::getCreatureUpgrade(const CreatureExperienceInfo& info) {
  SyncQueue<optional<ExperienceType>> returnQueue;
  if (auto menu = guiBuilder.drawCreatureUpgradeMenu(returnQueue, info))
    return getBlockingGui(returnQueue, std::move(menu));
  else
    return none;
}

optional<Vec2> WindowView::chooseSite(const string& message, const Campaign& campaign, optional<Vec2> current) {
  SyncQueue<optional<Vec2>> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawChooseSiteMenu(returnQueue, message, campaign, current));
}

void WindowView::presentWorldmap(const Campaign& campaign) {
  Semaphore sem;
  return getBlockingGui(sem, guiBuilder.drawWorldmap(sem, campaign));
}

variant<View::AvatarChoice, AvatarMenuOption> WindowView::chooseAvatar(const vector<AvatarData>& avatars) {
  SyncQueue<variant<AvatarChoice, AvatarMenuOption>> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.getMainMenuLinks(guiBuilder.drawAvatarMenu(returnQueue, avatars)), none, false);
}

CampaignAction WindowView::prepareCampaign(CampaignOptions campaign, CampaignMenuState& state) {
  SyncQueue<CampaignAction> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawCampaignMenu(returnQueue, campaign, state));
}

optional<UniqueEntity<Creature>::Id> WindowView::chooseCreature(const string& title,
    const vector<CreatureInfo>& creatures, const string& cancelText) {
  SyncQueue<optional<UniqueEntity<Creature>::Id>> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawChooseCreatureMenu(returnQueue, title, creatures, cancelText));
}

bool WindowView::creatureInfo(const string& title, bool prompt, const vector<CreatureInfo>& creatures) {
  SyncQueue<bool> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawCreatureInfo(returnQueue, title, prompt, creatures));
}

optional<ModAction> WindowView::getModAction(int highlighted, const vector<ModInfo>& mods) {
  SyncQueue<optional<ModAction>> returnQueue;
  return getBlockingGui(returnQueue, guiBuilder.drawModMenu(returnQueue, highlighted, mods));
}

void WindowView::logMessage(const std::string& message) {
  RecursiveLock lock(logMutex);
  messageLog.push_back(message);
  if (messageLog.size() > 10000)
    messageLog.pop_front();
}

void WindowView::getBlockingGui(Semaphore& sem, SGuiElem elem, optional<Vec2> origin) {
  TempClockPause pause(clock);
  if (!origin)
    origin = (renderer.getSize() - Vec2(*elem->getPreferredWidth(), *elem->getPreferredHeight())) / 2;
  origin->y = max(0, origin->y);
  if (blockingElems.empty()) {
    blockingElems.push_back(gui.darken());
    blockingElems.back()->setBounds(Rectangle(renderer.getSize()));
  }
  Vec2 size(*elem->getPreferredWidth(), min(renderer.getSize().y - origin->y, *elem->getPreferredHeight()));
  elem->setBounds(Rectangle(*origin, *origin + size));
  propagateMousePosition({elem});
  blockingElems.push_back(std::move(elem));
  if (currentThreadId() == renderThreadId)
    while (!sem.get())
      refreshView();
  else
    sem.p();
  blockingElems.clear();
}

void WindowView::presentHighscores(const vector<HighscoreList>& list) {
  Semaphore sem;
  int tabNum = 0;
  bool online = false;
  vector<ScrollPosition> scrollPos(list.size());
  getBlockingGui(sem, guiBuilder.drawHighscores(list, sem, tabNum, scrollPos, online),
      guiBuilder.getMenuPosition(MenuType::NORMAL, 0).topLeft());
}

optional<int> WindowView::chooseFromListInternal(const string& title, const vector<ListElem>& options,
    optional<int> index1, MenuType menuType, ScrollPosition* scrollPos1) {
  CHECK(!index1 || *index1 >= 0);
  if (!useTiles && menuType == MenuType::MAIN)
    menuType = MenuType::MAIN_NO_TILES;
  if (options.size() == 0)
    return none;
  uiLock = true;
  inputQueue.push(UserInputId::REFRESH);
  TempClockPause pause(clock);
  SyncQueue<optional<int>> returnQueue;
  addReturnDialog<optional<int>>(returnQueue, [=] ()-> optional<int> {
  renderer.flushEvents(SDL::SDL_KEYDOWN);
  int choice = -1;
  int count = 0;
  ScrollPosition* scrollPos = scrollPos1;
  optional<int> index = index1;
  vector<int> indexes(options.size());
  vector<int> optionIndexes;
  int elemCount = 0;
  for (int i : All(options)) {
    if (options[i].getMod() == ListElem::NORMAL) {
      indexes[count] = elemCount;
      optionIndexes.push_back(i);
      ++count;
    }
    if (options[i].getMod() != ListElem::TITLE && options[i].getMod() != ListElem::TEXT && options[i].getMod() != ListElem::HELP_TEXT)
      ++elemCount;
  }
  if (optionIndexes.empty())
    optionIndexes.push_back(0);
  vector<int> positions;
  SGuiElem stuff = gui.leftMargin(25, guiBuilder.drawListGui(capitalFirst(title), options, menuType, &index, &choice, &positions));
  if (title.empty())
    stuff = gui.topMargin(guiBuilder.getStandardLineHeight() / 2, std::move(stuff));
  auto getScrollPos = [&](int index) {
    if (index >= 0 && index < positions.size())
      return positions[index];
    else
      return 0;
  };
  ScrollPosition localScrollPos(index ? getScrollPos(optionIndexes[*index]) : 0);
  if (scrollPos == nullptr)
    scrollPos = &localScrollPos;
  SGuiElem dismissBut = gui.margins(gui.stack(makeVec(
        gui.button([&](){ choice = -100; }),
        gui.mouseHighlight2(gui.mainMenuHighlight()),
        gui.centeredLabel(Renderer::HOR, "Dismiss"))), 0, 5, 0, 0);
  switch (menuType) {
    case MenuType::MAIN: break;
    case MenuType::YES_NO_BELOW:
    case MenuType::YES_NO:
      stuff = gui.window(std::move(stuff), [&choice] { choice = -100;}); break;
    default:
      stuff = gui.scrollable(std::move(stuff), scrollPos);
      stuff = gui.margins(std::move(stuff), 0, 15, 0, 0);
      stuff = gui.margin(gui.centerHoriz(std::move(dismissBut), renderer.getTextLength("Dismiss") + 100),
          std::move(stuff), 30, gui.BOTTOM);
      stuff = gui.window(std::move(stuff), [&choice] { choice = -100;});
      break;
  }
  optional<optional<int>> callbackRet;
  auto scrollIndex = [&](int dir) {
    if (count > 0) {
      if (index)
        index = (*index + dir + count) % count;
      else
        index = 0;
      scrollPos->set(getScrollPos(optionIndexes[*index]), clock->getRealMillis());
    } else
      scrollPos->add(dir * 100, clock->getRealMillis());
  };
  stuff = gui.stack(
      std::move(stuff),
      gui.keyHandler([&] (SDL_Keysym key) {
        switch (key.sym) {
          case SDL::SDLK_KP_8:
          case SDL::SDLK_UP:
            scrollIndex(-1);
            break;
          case SDL::SDLK_KP_2:
          case SDL::SDLK_DOWN:
            scrollIndex(1);
            break;
          case SDL::SDLK_KP_5:
          case SDL::SDLK_KP_ENTER:
          case SDL::SDLK_RETURN:
            if (count > 0 && index) {
              CHECK(*index < indexes.size()) <<
                  *index << " " << indexes.size() << " " << count << " " << options.size();
              callbackRet = optional<int>(indexes[*index]);
              break;
            }
            FALLTHROUGH;
          case SDL::SDLK_ESCAPE:
            callbackRet = optional<int>(none);
            break;
          default:
            break;
        }
      }, true));
  if (menuType == MenuType::MAIN || menuType == MenuType::MAIN_NO_TILES)
    stuff = guiBuilder.getMainMenuLinks(std::move(stuff));
  while (1) {
    refreshScreen(false);
    stuff->setBounds(guiBuilder.getMenuPosition(menuType, options.size()));
    stuff->render(renderer);
    renderer.drawAndClearBuffer();
    Event event;
    while (renderer.pollEvent(event)) {
      propagateEvent(event, concat({stuff}, getClickableGuiElems()));
      if (choice > -1) {
        CHECK(choice < indexes.size()) << choice;
        return indexes[choice];
      }
      if (choice == -100)
        return none;
      if (callbackRet)
        return *callbackRet;
      if (considerResizeEvent(event))
        continue;
    }
  }
  });
  return returnQueue.pop();
}

void WindowView::presentText(const string& title, const string& text) {
  TempClockPause pause(clock);
  presentList(title, ListElem::convert({text}), false);
}

void WindowView::presentTextBelow(const string& title, const string& text) {
  TempClockPause pause(clock);
  presentList(title, ListElem::convert({text}), false, MenuType::NORMAL_BELOW);
}

void WindowView::presentList(const string& title, const vector<ListElem>& options, bool scrollDown, MenuType menu) {
  vector<ListElem> conv(options);
  for (ListElem& e : conv)
    if (e.getMod() == ListElem::NORMAL)
      e.setMod(ListElem::TEXT);
  ScrollPosition scrollPos(scrollDown ? 10000000 : 0);
  chooseFromListInternal(title, conv, none, menu, &scrollPos);
}

void WindowView::zoom(int dir) {
  refreshInput = true;
  auto& layouts = currentTileLayout.layouts;
  int index = *layouts.findAddress(mapLayout);
  if (dir != 0 )
    index += dir;
  else {
    CHECK(index == 0 || index == 1) << index;
    index = 1 - index;
  }
  index = max(0, min<int>(layouts.size() - 1, index));
  mapLayout = &currentTileLayout.layouts[index];
}

void WindowView::switchTiles() {
  int index = *currentTileLayout.layouts.findAddress(mapLayout);
  if (options->getBoolValue(OptionId::ASCII) || !useTiles)
    currentTileLayout = asciiLayouts;
  else
    currentTileLayout = spriteLayouts;
  if (currentTileLayout.layouts.size() <= index)
    index = 0;
  while (gameInfo.infoType == GameInfo::InfoType::SPECTATOR && useTiles &&
      renderer.getSize().x < Level::getSplashVisibleBounds().width() *
      currentTileLayout.layouts[index].getSquareSize().x && index < currentTileLayout.layouts.size() - 1)
    ++index;
  mapLayout = &currentTileLayout.layouts[index];
}

bool WindowView::travelInterrupt() {
  return lockKeyboard;
}

milliseconds WindowView::getTimeMilli() {
  return clock->getMillis();
}

milliseconds WindowView::getTimeMilliAbsolute() {
  return clock->getRealMillis();
}

void WindowView::stopClock() {
  clock->pause();
}

void WindowView::continueClock() {
  clock->cont();
}

bool WindowView::isClockStopped() {
  return clock->isPaused();
}

bool WindowView::considerResizeEvent(Event& event, bool withBugReportEvent) {
  if (withBugReportEvent && considerBugReportEvent(event))
    return true;
  if (event.type == SDL::SDL_QUIT)
    throw GameExitException();
  if (event.type == SDL::SDL_WINDOWEVENT && event.window.event == SDL::SDL_WINDOWEVENT_RESIZED) {
    resize(event.window.data1, event.window.data2);
    return true;
  }
  return false;
}

void WindowView::setBugReportSaveCallback(BugReportSaveCallback f) {
  bugReportSaveCallback = f;
}

void WindowView::dungeonScreenshot(Vec2 size) {
  mapGui->render(renderer);
  renderer.drawAndClearBuffer();
  renderer.makeScreenshot(DirectoryPath::current().file(retiredScreenshotFilename),
      Rectangle((renderer.getSize() - size) / 2, (renderer.getSize() + size) / 2));
}

bool WindowView::considerBugReportEvent(Event& event) {
  if (event.type == SDL::SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
      Vec2(event.button.x, event.button.y).inRectangle(getBugReportPos(renderer))) {
    bool exit = false;
    optional<GuiBuilder::BugReportInfo> bugreportInfo;
    auto elem = guiBuilder.drawBugreportMenu(!!bugReportSaveCallback,
        [&exit, &bugreportInfo] (optional<GuiBuilder::BugReportInfo> info) {
      bugreportInfo = info;
      exit = true;
    });
    Vec2 size(*elem->getPreferredWidth(), *elem->getPreferredHeight());
    do {
      Event event;
      while (renderer.pollEvent(event)) {
        considerResizeEvent(event, false);
        gui.propagateEvent(event, {elem});
      }
      elem->setBounds(Rectangle((renderer.getSize() - size) / 2, (renderer.getSize() + size) / 2));
      refreshScreen(false);
      elem->render(renderer);
      renderer.drawAndClearBuffer();
    } while (!exit);
    if (bugreportInfo) {
      optional<FilePath> savefile;
      optional<FilePath> screenshot;
      if (bugreportInfo->includeSave) {
        savefile = bugreportDir.file("bugreport.sav");
        bugReportSaveCallback(*savefile);
      }
      if (bugreportInfo->includeScreenshot) {
        screenshot = bugreportDir.file("bugreport.png");
        renderer.makeScreenshot(*screenshot, Rectangle(renderer.getSize()));
      }
      if (!savefile && !screenshot && bugreportInfo->text.size() < 5)
        return true;
      ProgressMeter meter(1.0);
      optional<string> result;
      displaySplash(&meter, "Uploading bug report", SplashType::AUTOSAVING, [this]{bugreportSharing->cancel();});
      thread t([&] {
        result = bugreportSharing->uploadBugReport(bugreportInfo->text, savefile, screenshot, meter);
        clearSplash();
      });
      refreshView();
      t.join();
      if (result)
        presentText("Error", "There was an error while sending the bug report: " + *result);
      if (savefile)
        remove(savefile->getPath());
      if (screenshot)
        remove(screenshot->getPath());
    }
    return true;
  }
  return false;
}

void WindowView::processEvents() {
  Event event;
  while (renderer.pollEvent(event)) {
    considerResizeEvent(event);
    if (gameInfo.infoType == GameInfo::InfoType::SPECTATOR)
      switch (event.type) {
        case SDL::SDL_KEYDOWN:
        case SDL::SDL_MOUSEWHEEL:
        case SDL::SDL_MOUSEBUTTONDOWN:
          inputQueue.push(UserInput(UserInputId::EXIT));
          return;
        default:break;
      }
    else {
      if (event.type == SDL::SDL_KEYDOWN && renderDialog.empty() && blockingElems.empty()) {
        if (lockKeyboard)
          return;
        lockKeyboard = true;
      }
      propagateEvent(event, getClickableGuiElems());
    }
    switch (event.type) {
      case SDL::SDL_KEYDOWN:
        if (gameInfo.infoType == GameInfo::InfoType::PLAYER)
          renderer.flushEvents(SDL::SDL_KEYDOWN);
        break;
      case SDL::SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT) {
          if (auto building = guiBuilder.getActiveButton(CollectiveTab::BUILDINGS))
            inputQueue.push(UserInput(UserInputId::RECT_CONFIRM, BuildingClickInfo{Vec2(0, 0), *building}));
          else if (auto building = guiBuilder.getActiveButton(CollectiveTab::TECHNOLOGY))
            inputQueue.push(UserInput(UserInputId::RECT_CONFIRM, BuildingClickInfo{Vec2(0, 0), *building}));
          else if (gameInfo.infoType == GameInfo::InfoType::BAND)
            inputQueue.push(UserInputId::RECT_CANCEL);
        }
        break;
      default: break;
    }
  }
}

void WindowView::propagateEvent(const Event& event, vector<SGuiElem> guiElems) {
  CHECK(currentThreadId() == renderThreadId);
  if (gameReady)
    guiBuilder.clearHint();
  switch (event.type) {
    case SDL::SDL_MOUSEBUTTONUP:
      // MapGui needs this event otherwise it will sometimes lock the mouse button
      mapGui->onMouseRelease(Vec2(event.button.x, event.button.y));
      break;
    case SDL::SDL_MOUSEBUTTONDOWN:
      lockKeyboard = true;
      break;
    default:break;
  }
  gui.propagateEvent(event, guiElems);
}

UserInputId getDirActionId(const SDL_Keysym& key) {
  if (GuiFactory::isCtrl(key))
    return UserInputId::TRAVEL;
  else
    return UserInputId::MOVE;
}

void WindowView::keyboardAction(const SDL_Keysym& key) {
  switch (key.sym) {
#ifndef RELEASE
    case SDL::SDLK_F10:
      if (auto input = getText("Enter effect", "", 100, ""))
        inputQueue.push({UserInputId::APPLY_EFFECT, *input});
      break;
    case SDL::SDLK_F11:
      if (auto input = getText("Enter item type", "", 100, ""))
        inputQueue.push({UserInputId::CREATE_ITEM, *input});
      break;
    case SDL::SDLK_F12:
      if (auto input = getText("Enter creature id", "", 100, ""))
        inputQueue.push({UserInputId::SUMMON_ENEMY, *input});
      break;
    case SDL::SDLK_F9:
      inputQueue.push(UserInputId::CHEAT_ATTRIBUTES);
      break;
    case SDL::SDLK_F8:
      //renderer.startMonkey();
      renderer.loadAnimations();
      renderer.getTileSet().reload();
      fxRenderer->loadTextures();
      gui.loadImages();
      break;
    case SDL::SDLK_TAB:
      // TODO: put it under different shortcut?
      //inputQueue.push(UserInputId::CHEAT_SPELLS);
      //inputQueue.push(UserInputId::CHEAT_POTIONS);
      break;
#endif
    case SDL::SDLK_F7:
      presentList("", ListElem::convert(vector<string>(messageLog.begin(), messageLog.end())), true);
      break;
    case SDL::SDLK_z: zoom(0); break;
    case SDL::SDLK_COMMA:
      inputQueue.push(UserInputId::SCROLL_UP_STAIRS);
       break;
    case SDL::SDLK_PERIOD:
      inputQueue.push(UserInputId::SCROLL_DOWN_STAIRS);
       break;
    case SDL::SDLK_F2:
      if (!renderer.isMonkey()) {
        options->handle(this, OptionSet::GENERAL);
        refreshScreen();
      }
      break;
    case SDL::SDLK_ESCAPE:
      if (!guiBuilder.clearActiveButton() && !renderer.isMonkey())
        inputQueue.push(UserInput(UserInputId::EXIT));
      break;
    case SDL::SDLK_UP:
    case SDL::SDLK_KP_8:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(0, -1)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_KP_9:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(1, -1)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_RIGHT:
    case SDL::SDLK_KP_6:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(1, 0)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_KP_3:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(1, 1)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_DOWN:
    case SDL::SDLK_KP_2:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(0, 1)));
      mapGui->onMouseGone();  
      break;
    case SDL::SDLK_KP_1:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(-1, 1)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_LEFT:
    case SDL::SDLK_KP_4:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(-1, 0)));
      mapGui->onMouseGone();
      break;
    case SDL::SDLK_KP_7:
      inputQueue.push(UserInput(getDirActionId(key), Vec2(-1, -1)));
      mapGui->onMouseGone();
      break;
    default: break;
  }
}

UserInput WindowView::getAction() {
  lockKeyboard = false;
  if (refreshInput) {
    refreshInput = false;
    return UserInputId::REFRESH;
  }
  if (auto input = inputQueue.popAsync())
    return *input;
  else
    return UserInput(UserInputId::IDLE);
}

double WindowView::getGameSpeed() {
  switch (guiBuilder.getGameSpeed()) {
    case GuiBuilder::GameSpeed::SLOW: return 0.015;
    case GuiBuilder::GameSpeed::NORMAL: return 0.025;
    case GuiBuilder::GameSpeed::FAST: return 0.04;
    case GuiBuilder::GameSpeed::VERY_FAST: return 0.06;
  }
}

optional<int> WindowView::chooseAtMouse(const vector<string>& elems) {
  return guiBuilder.chooseAtMouse(elems);
}

void WindowView::addSound(const Sound& sound) {
  soundQueue.push_back(sound);
}
