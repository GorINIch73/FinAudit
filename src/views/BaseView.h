#pragma once

#include "imgui.h"
#include "../DatabaseManager.h"
#include "../PdfReporter.h"
#include <vector>
#include <string>
#include <utility>

class UIManager; // Forward declaration

class BaseView {
public:
    virtual ~BaseView() = default;
    virtual void Render() = 0;
    virtual void SetDatabaseManager(DatabaseManager* dbManager) = 0;
    virtual void SetPdfReporter(PdfReporter* pdfReporter) = 0;
    virtual void SetUIManager(UIManager* uiManager) = 0; // New pure virtual method
    virtual std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() = 0;
    
    virtual const char* GetTitle() { return Title.c_str(); }
    virtual void SetTitle(const std::string& title) { Title = title; }

    virtual void OnDeactivate() {}
    virtual bool ForceSave() { return true; }

    bool IsVisible = false;

    protected:
    DatabaseManager* dbManager = nullptr;
    PdfReporter* pdfReporter = nullptr;
    UIManager* uiManager = nullptr; // New protected member
    std::string Title;};
