#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Patching/NativeHookManager.h"
#include "PlannerRows.generated.h"
#include <memory>
#include <type_traits>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSFPackRURuntime, Log, All);

namespace
{
using FTranslateFunction = FString(*)(const FString&);
using FSelectFunction = FString(*)(const TCHAR*, const TCHAR*);
std::unique_ptr<SFPackRU::PlannerTranslation> Translations;
void* TranslateTrampoline = nullptr;
void* SelectTrampoline = nullptr;

bool IsRussian()
{
    return FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("ru");
}

FString TranslateRussian(const FString& Source)
{
    if (!IsRussian()) return reinterpret_cast<FTranslateFunction>(TranslateTrampoline)(Source);
    const auto Result = Translations->TranslateGerman(std::wstring_view(*Source, Source.Len()));
    return FString(Result.c_str());
}

FString SelectRussian(const TCHAR* German, const TCHAR* English)
{
    if (!IsRussian()) return reinterpret_cast<FSelectFunction>(SelectTrampoline)(German, English);
    const auto Result = Translations->Select(German ? German : TEXT(""), English ? English : TEXT(""));
    return FString(Result.c_str());
}
}

class FSFPackRURuntimeModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
#if PLATFORM_WINDOWS && !WITH_EDITOR
        static_assert(std::is_same_v<TCHAR, wchar_t> && sizeof(TCHAR) == 2);
        if (IsRunningDedicatedServer()) return;
        Translations = std::make_unique<SFPackRU::PlannerTranslation>(SFPackRU::PlannerRows);
        ModuleChangeHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FSFPackRURuntimeModule::OnModuleChanged);
        TryInstall();
#endif
    }

    void ShutdownModule() override
    {
#if PLATFORM_WINDOWS && !WITH_EDITOR
        FModuleManager::Get().OnModulesChanged().Remove(ModuleChangeHandle);
        // This module cannot hot-reload; normal shutdown happens after Slate stops.
        if (SelectAddress) FNativeHookManagerInternal::UnregisterHookFunction(TEXT("SFPackRU.Planner.Select"), SelectAddress);
        if (TranslateAddress) FNativeHookManagerInternal::UnregisterHookFunction(TEXT("SFPackRU.Planner.Translate"), TranslateAddress);
        SelectAddress = TranslateAddress = nullptr;
        Translations.reset();
        if (PlannerHandle) { ::FreeLibrary(PlannerHandle); PlannerHandle = nullptr; }
#endif
    }

    bool SupportsDynamicReloading() override { return false; }

private:
    FDelegateHandle ModuleChangeHandle;
    void* TranslateAddress = nullptr;
    void* SelectAddress = nullptr;
    bool Attempted = false;
#if PLATFORM_WINDOWS
    HMODULE PlannerHandle = nullptr;
#endif

    void OnModuleChanged(FName ModuleName, EModuleChangeReason Reason)
    {
        if (ModuleName == TEXT("SFPFactoryPlanner") && Reason == EModuleChangeReason::ModuleLoaded) TryInstall();
    }

    void TryInstall()
    {
#if PLATFORM_WINDOWS && !WITH_EDITOR
        if (Attempted) return;
        // Acquire only an already-loaded DLL. Hold a reference until hooks are removed.
        if (!::GetModuleHandleExW(0, L"FactoryGameSteam-SFPFactoryPlanner-Win64-Shipping.dll", &PlannerHandle)) return;
        Attempted = true;
        TArray<TCHAR> PathBuffer;
        PathBuffer.SetNumUninitialized(32768);
        const DWORD PathLength = ::GetModuleFileNameW(PlannerHandle, PathBuffer.GetData(), PathBuffer.Num());
        TArray<uint8> FileBytes;
        FSHAHash Hash;
        if (PathLength == 0 || PathLength >= static_cast<DWORD>(PathBuffer.Num()) ||
            !FFileHelper::LoadFileToArray(FileBytes, PathBuffer.GetData()))
        {
            Disable(TEXT("Could not verify the loaded Planner DLL."));
            return;
        }
        FSHA1::HashBuffer(FileBytes.GetData(), FileBytes.Num(), Hash.Hash);
        if (!Hash.ToString().Equals(SFPackRU::PlannerSha1, ESearchCase::IgnoreCase))
        {
            Disable(TEXT("Planner DLL differs from the supported 1.7.1 build; no hooks installed."));
            return;
        }
        auto* Base = reinterpret_cast<uint8*>(PlannerHandle);
        auto* TranslateTarget = Base + SFPackRU::TranslateRva;
        auto* SelectTarget = Base + SFPackRU::SelectRva;
        if (FMemory::Memcmp(TranslateTarget, SFPackRU::TranslatePrefix, sizeof(SFPackRU::TranslatePrefix)) != 0 ||
            FMemory::Memcmp(SelectTarget, SFPackRU::SelectPrefix, sizeof(SFPackRU::SelectPrefix)) != 0)
        {
            Disable(TEXT("Planner language functions were modified in memory; no hooks installed."));
            return;
        }
        TranslateAddress = FNativeHookManagerInternal::RegisterHookFunction(TEXT("SFPackRU.Planner.Translate"),
            FMemberFunctionPointer{TranslateTarget, 0, 0}, nullptr, reinterpret_cast<void*>(&TranslateRussian), &TranslateTrampoline);
        SelectAddress = FNativeHookManagerInternal::RegisterHookFunction(TEXT("SFPackRU.Planner.Select"),
            FMemberFunctionPointer{SelectTarget, 0, 0}, nullptr, reinterpret_cast<void*>(&SelectRussian), &SelectTrampoline);
        UE_LOG(LogSFPackRURuntime, Display, TEXT("Installed Russian language hooks for Planner 1.7.1: 798 reviewed sources, 820 entries."));
#endif
    }

    void Disable(const TCHAR* Reason)
    {
        UE_LOG(LogSFPackRURuntime, Warning, TEXT("%s"), Reason);
#if PLATFORM_WINDOWS
        if (PlannerHandle) { ::FreeLibrary(PlannerHandle); PlannerHandle = nullptr; }
#endif
    }
};

IMPLEMENT_MODULE(FSFPackRURuntimeModule, SFPackRURuntime)
