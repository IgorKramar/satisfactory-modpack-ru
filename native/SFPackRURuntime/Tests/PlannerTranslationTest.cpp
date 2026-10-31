#include "PlannerRows.generated.h"
#include <iostream>
#include <thread>
#include <atomic>

int main()
{
    const SFPackRU::PlannerTranslation Translation(SFPackRU::PlannerRows);
    std::atomic<int> Errors = 0;
    auto Check = [&](bool Condition, const char* Message)
    {
        if (!Condition) { std::cerr << Message << '\n'; ++Errors; }
    };
    for (const auto& Row : SFPackRU::PlannerRows)
    {
        Check(Translation.TranslateGerman(Row.German) == Row.Russian, "German literal mismatch");
        Check(Translation.Select(Row.German, Row.English) == Row.Russian, "Bilingual selection mismatch");
        Check(Translation.TranslateGerman(Row.Russian) == Row.Russian, "Already translated literal changed");
    }
    Check(Translation.TranslateGerman(L"").empty(), "Empty source changed");
    Check(Translation.TranslateGerman(L"PlayerFactory_123") == L"PlayerFactory_123", "Unknown source changed");
    Check(Translation.Select(L"UNKNOWN_GERMAN", L"Unknown English") == L"Unknown English", "Unknown selection lost fallback");
    Check(Translation.TranslateGerman(L"Reserve: 25% / 120.5 MW") == L"\u0420\u0435\u0437\u0435\u0440\u0432: 25% / 120.5 MW", "Numbers or units changed");
    // Construct dynamic strings with user-supplied values around every fragment.
    // This catches overlap and replacement-cascade defects missed by exact lookup.
    for (const auto& Row : SFPackRU::PlannerRows)
    {
        if (!Row.IsFragment || Row.German.empty()) continue;
        std::wstring Source = L"[12.5]|"; Source += Row.German; Source += L"|UserFactory_987";
        std::wstring Expected = L"[12.5]|"; Expected += Row.Russian; Expected += L"|UserFactory_987";
        const auto Actual = Translation.TranslateGerman(Source);
        if (Actual != Expected) std::cerr << "Dynamic row index: " << (&Row - SFPackRU::PlannerRows) << '\n';
        Check(Actual == Expected, "Dynamic fragment mismatch");
        Check(Translation.TranslateGerman(Actual) == Actual, "Dynamic Russian text changed twice");
    }
    std::vector<std::thread> Threads;
    for (int Thread = 0; Thread < 4; ++Thread)
        Threads.emplace_back([&]
        {
            for (int Round = 0; Round < 10; ++Round)
                for (const auto& Row : SFPackRU::PlannerRows)
                    if (Translation.TranslateGerman(Row.German) != Row.Russian) ++Errors;
        });
    for (auto& Thread : Threads) Thread.join();
    std::cout << "820 literal rows, 457 dynamic fragments, 4 concurrent readers; errors=" << Errors.load() << '\n';
    return Errors.load() ? 1 : 0;
}
