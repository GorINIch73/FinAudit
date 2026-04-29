#pragma once

#include "BaseView.h"
#include "../Settings.h"

class UIManager;

class SettingsView : public BaseView {
public:
    SettingsView();
    void Render() override;

    // Implementing pure virtual functions from BaseView
    void SetDatabaseManager(DatabaseManager* manager) override { dbManager = manager; }
    void SetPdfReporter(PdfReporter* reporter) override { /* Not used in this view */ }
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override { return {}; }
    void OnDeactivate() override;
    void ForceSave() override;
    void SetUIManager(UIManager* manager) override; // Declare override here

private:
    void LoadSettings();
    void SaveChanges();

    Settings currentSettings;
    Settings originalSettings;
    bool isDirty = false;
};
