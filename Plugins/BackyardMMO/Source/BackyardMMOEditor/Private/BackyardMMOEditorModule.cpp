#include "Modules/ModuleManager.h"
#include "BYMEditorSetup.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MessageLogModule.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "BackyardMMOEditor"

class FBackyardMMOEditorModule final : public IModuleInterface
{
    FTSTicker::FDelegateHandle SetupTick;
    bool bTriedAutomaticSetup = false;

    static void SetupAll() { FBYMEditorSetup::Present(FBYMEditorSetup::SetupAll()); }
    static void Import() { FBYMEditorSetup::Present(FBYMEditorSetup::ImportAssets()); }
    static void Maps() { FBYMEditorSetup::Present(FBYMEditorSetup::GenerateMaps()); }
    static void Validate() { FBYMEditorSetup::Present(FBYMEditorSetup::Validate()); }
    static bool CanRun() { return !FBYMEditorSetup::IsBusy() && GEditor && !GEditor->PlayWorld; }

    TSharedRef<SWidget> BuildMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.BeginSection("Setup", LOCTEXT("SetupHeader", "Backyard Monsters NXT"));
        Menu.AddMenuEntry(LOCTEXT("SetupAll", "Set Up Everything"),
            LOCTEXT("SetupTip", "Import original assets, create missing maps and materials, and configure this project automatically."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
            FUIAction(FExecuteAction::CreateStatic(&SetupAll), FCanExecuteAction::CreateStatic(&CanRun)));
        Menu.AddMenuEntry(LOCTEXT("Import", "Import / Refresh Original Assets"),
            LOCTEXT("ImportTip", "Read the bundled SourceAssets folder automatically, then enforce Default/sRGB color settings on every texture, including cached imports."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"),
            FUIAction(FExecuteAction::CreateStatic(&Import), FCanExecuteAction::CreateStatic(&CanRun)));
        Menu.AddMenuEntry(LOCTEXT("Maps", "Create Missing Maps"),
            LOCTEXT("MapsTip", "Create MainMenu and OpenWorld when missing. Existing maps and material edits are preserved."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Levels"),
            FUIAction(FExecuteAction::CreateStatic(&Maps), FCanExecuteAction::CreateStatic(&CanRun)));
        Menu.AddMenuEntry(LOCTEXT("Validate", "Validate Setup"),
            LOCTEXT("ValidateTip", "Check maps, game modes, source textures, catalog references, custom depth, and cook settings."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Check"),
            FUIAction(FExecuteAction::CreateStatic(&Validate), FCanExecuteAction::CreateStatic(&CanRun)));
        Menu.EndSection();
        return Menu.MakeWidget();
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
        FToolMenuSection& Section = Toolbar->FindOrAddSection("BackyardMMO");
        Section.AddEntry(FToolMenuEntry::InitComboButton(
            "BackyardMMOTools", FUIAction(), FOnGetContent::CreateRaw(this, &FBackyardMMOEditorModule::BuildMenu),
            LOCTEXT("ToolbarLabel", "Backyard Monsters NXT"), LOCTEXT("ToolbarTip", "Build, import and validate the Backyard Monsters NXT project."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"), false));

        UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        Tools->FindOrAddSection("BackyardMMO").AddMenuEntry(
            "BackyardMMOSetup", LOCTEXT("ToolsSetup", "Backyard Monsters NXT: Set Up Everything"),
            LOCTEXT("ToolsSetupTip", "Automatically prepare original assets, maps, materials and project settings."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
            FUIAction(FExecuteAction::CreateStatic(&SetupAll), FCanExecuteAction::CreateStatic(&CanRun)));
    }

public:
    virtual void StartupModule() override
    {
        FMessageLogModule& Logs = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
        Logs.RegisterLogListing("BackyardMMO", LOCTEXT("LogTitle", "Backyard Monsters NXT Setup"));
        if (IsRunningCommandlet()) return;
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBackyardMMOEditorModule::RegisterMenus));
        // Defer until an editor world exists; never open or discard the user's current map.
        SetupTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
        {
            if (!GEditor || !GEditor->GetEditorWorldContext().World()) return true;
            if (GEditor->PlayWorld) return true;
            if (!bTriedAutomaticSetup && FBYMEditorSetup::NeedsFirstSetup())
            {
                bTriedAutomaticSetup = true;
                SetupAll();
            }
            SetupTick.Reset();
            return false;
        }), 2.0f);
    }

    virtual void ShutdownModule() override
    {
        if (SetupTick.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(SetupTick);
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
            FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing("BackyardMMO");
    }
};

IMPLEMENT_MODULE(FBackyardMMOEditorModule, BackyardMMOEditor)
#undef LOCTEXT_NAMESPACE
