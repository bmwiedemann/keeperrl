#pragma once

#include "view.h"
#include "clock.h"
#include "avatar_menu_option.h"
#include "user_input.h"

class DummyView : public View {
  public:
  DummyView(Clock* c) : clock(c) {}
  Clock* clock;
  virtual ~DummyView() override {}
  virtual void initialize(unique_ptr<fx::FXRenderer>, unique_ptr<FXViewManager>) override {}
  virtual void reset() override {}
  virtual void displaySplash(const ProgressMeter*, const string&, SplashType, function<void()> = nullptr) override {}
  virtual void clearSplash() override {}
  virtual void close() override {}
  virtual void refreshView() override {}
  virtual double getGameSpeed() override { return 20; }
  virtual void updateView(CreatureView*, bool noRefresh) override {}
  virtual void drawLevelMap(const CreatureView*) override {}
  virtual void setScrollPos(Position) override {}
  virtual void resetCenter() override {}
  virtual UserInput getAction() override { return UserInputId::IDLE; }
  virtual bool travelInterrupt() override { return false; }
  virtual optional<int> chooseFromList(const string&, const vector<ListElem>&, int = 0,
      MenuType = MenuType::NORMAL, ScrollPosition* = nullptr, optional<UserInputId> = none) override {
    return none;
  }
  virtual optional<Vec2> chooseDirection(Vec2 playerPos, const string& message) override {
    return none;
  }
  virtual optional<Vec2> chooseTarget(Vec2 playerPos, TargetType, Table<PassableInfo>, const string& message) override {
    return none;
  }
  virtual bool yesOrNoPrompt(const string& message, bool defaultNo = false) override {
    return false;
  }
  virtual bool yesOrNoPromptBelow(const string& message, bool defaultNo = false) override {
    return false;
  }
  virtual void presentText(const string&, const string&) override {}
  virtual void presentTextBelow(const string&, const string&) override {}
  virtual void presentList(const string&, const vector<ListElem>&, bool = false,
      MenuType = MenuType::NORMAL) override {}
  virtual optional<int> getNumber(const string& title, Range range, int initial, int increments = 1) override {
    return none;
  }
  virtual optional<string> getText(const string& title, const string& value, int maxLength,
      const string& hint = "") override {
    return none;
  }
  virtual optional<UniqueEntity<Item>::Id> chooseTradeItem(const string& title, pair<ViewId, int> budget,
      const vector<ItemInfo>&, ScrollPosition* scrollPos) override {
    return none;
  }
  virtual optional<int> choosePillageItem(const string& title, const vector<ItemInfo>&, ScrollPosition* scrollPos) override {
    return none;
  }
  virtual optional<int> chooseItem(const vector<ItemInfo>& items, ScrollPosition* scrollpos) override {
    return none;
  }
  virtual void presentHighscores(const vector<HighscoreList>&) override {}
  virtual CampaignAction prepareCampaign(CampaignOptions, CampaignMenuState&) override {
    return CampaignActionId::CANCEL;
  }
  virtual optional<UniqueEntity<Creature>::Id> chooseCreature(const string&, const vector<CreatureInfo>&,
      const string& cancelText) override {
    return none;
  }
  virtual bool creatureInfo(const string& title, bool prompt, const vector<CreatureInfo>&) override {
    return false;
  }
  virtual optional<Vec2> chooseSite(const string& message, const Campaign&, optional<Vec2> current = none) override {
    return none;
  }
  virtual optional<int> chooseAtMouse(const vector<string>& elems) override {
    return none;
  }
  virtual optional<ExperienceType> getCreatureUpgrade(const CreatureExperienceInfo&) override {
    return none;
  }
  virtual optional<ModAction> getModAction(int highlighted, const vector<ModInfo>&) override {
    return none;
  }
  virtual void presentWorldmap(const Campaign&) override {}
  virtual void animateObject(Vec2 begin, Vec2 end, optional<ViewId>, optional<FXInfo>) override {}
  virtual void animation(Vec2 pos, AnimationId, Dir) override {}
  virtual void animation(const FXSpawnInfo&) override{};
  virtual milliseconds getTimeMilli() override { return clock->getMillis();}
  virtual milliseconds getTimeMilliAbsolute() override { return clock->getRealMillis();}
  virtual void stopClock() override {}
  virtual void continueClock() override {}
  virtual bool isClockStopped() override { return false; }
  virtual void addSound(const Sound&) override {}
  virtual void logMessage(const string&) override {}
  virtual void setBugReportSaveCallback(BugReportSaveCallback) override {}
  virtual variant<AvatarChoice, AvatarMenuOption> chooseAvatar(const vector<AvatarData>&) override {
    return AvatarMenuOption::GO_BACK;
  }
  virtual void dungeonScreenshot(Vec2 size) override {
  }
};
