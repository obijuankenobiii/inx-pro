#pragma once

/**
 * @file Settings.h
 * @brief Settings page using the shared Page/navigation shell.
 */

#include "Page.h"

#include <functional>
#include <memory>

class CategorySettingsActivity;
class ReaderPresetsActivity;

enum class SettingsPanel : uint8_t { System, Reader, Presets };

/**
 * The settings page owns the settings panels directly.  The old top-level
 * SettingsActivity wrapper is intentionally not used here; CategorySettingsActivity
 * and ReaderPresetsActivity are embedded content panels inside this Page.
 */
class Settings final : public Page {
 public:
  Settings(GfxRenderer& renderer, MappedInputManager& mappedInput);

  const char* name() const override { return "Settings"; }
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  void content() override;
  void menu() override;
  bool back() override;

 private:
  enum class Pending { None, SwapPanel };

  void openPanel();
  void closePanel();
  void requestPanelSwap();
  void selectPanel(SettingsPanel panel);
  void processPending();
  bool panelTabsInput();
  void panelTabs();
  bool panelDetailOpen() const;
  bool runExternalNavigation();
  void deferExternalNavigation(const std::function<void()>& action);

  std::unique_ptr<Activity> panel;
  CategorySettingsActivity* categoryPanel = nullptr;
  ReaderPresetsActivity* readerPanel = nullptr;
  ReaderPresetsActivity* presetsPanel = nullptr;
  SettingsPanel currentPanel = SettingsPanel::System;
  SettingsPanel nextPanel = SettingsPanel::System;
  Pending pending = Pending::None;
  std::function<void()> externalNavigation;

};
