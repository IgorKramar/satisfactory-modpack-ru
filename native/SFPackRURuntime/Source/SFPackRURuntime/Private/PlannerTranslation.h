#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SFPackRU
{
struct TranslationRow
{
    std::wstring_view German;
    std::wstring_view English;
    std::wstring_view Russian;
    bool IsFragment;
};

// All views refer to generated, immutable string literals owned by this DLL.
// The tables are fully constructed before either game function is intercepted.
class PlannerTranslation
{
public:
    template<std::size_t N>
    explicit PlannerTranslation(const TranslationRow (&Rows)[N])
    {
        for (const auto& Row : Rows)
        {
            German.emplace(Row.German, Row.Russian);
            English.emplace(Row.English, Row.Russian);
            Russian.emplace(Row.Russian);
            if (Row.IsFragment && !Row.German.empty())
                Fragments[Row.German.front()].push_back(&Row);
        }
        for (auto& Bucket : Fragments)
            std::stable_sort(Bucket.second.begin(), Bucket.second.end(), [](auto* A, auto* B)
            { return A->German.size() > B->German.size(); });
    }

    std::wstring TranslateGerman(std::wstring_view Source) const
    {
        if (Source.empty() || Russian.count(Source)) return std::wstring(Source);
        if (auto It = German.find(Source); It != German.end()) return std::wstring(It->second);
        std::wstring Result;
        Result.reserve(Source.size());
        for (std::size_t Position = 0; Position < Source.size();)
        {
            const TranslationRow* Match = nullptr;
            if (auto Bucket = Fragments.find(Source[Position]); Bucket != Fragments.end())
                for (auto* Candidate : Bucket->second)
                    if (Source.substr(Position, Candidate->German.size()) == Candidate->German)
                    { Match = Candidate; break; }
            if (Match)
            {
                Result.append(Match->Russian);
                Position += Match->German.size();
            }
            else Result.push_back(Source[Position++]);
        }
        // A single pass over the original text prevents replacement cascades.
        return Result;
    }

    std::wstring Select(std::wstring_view GermanSource, std::wstring_view EnglishSource) const
    {
        if (auto It = German.find(GermanSource); It != German.end()) return std::wstring(It->second);
        if (auto It = English.find(EnglishSource); It != English.end()) return std::wstring(It->second);
        return std::wstring(EnglishSource);
    }

private:
    std::unordered_map<std::wstring_view, std::wstring_view> German, English;
    std::unordered_set<std::wstring_view> Russian;
    std::unordered_map<wchar_t, std::vector<const TranslationRow*>> Fragments;
};
}
