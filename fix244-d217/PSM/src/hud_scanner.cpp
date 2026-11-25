#include "hud_scanner.h"
#include "hud_3732_profiles.h"
#include "oxygen_3732_profiles.h"
#include "native_3446.h"
#include "native_menu_reader.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace psm::hud {
namespace {

template <std::size_t N> struct Pattern {
    std::array<int, N> bytes{};
    std::size_t size = 0;
    consteval Pattern(const char (&text)[N]) {
        const auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
        for (std::size_t i = 0; i + 1 < N;) {
            if (text[i] == ' ') { ++i; continue; }
            bytes[size++] = text[i] == '?' ? -1 : hex(text[i]) * 16 + hex(text[i + 1]);
            i += 2;
        }
    }
};

// Complete visibility function: internal guards/branches are exact, external
// rel32 calls and RIP data references are checked separately below.
constexpr Pattern kVisibility{
    "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 40 C5 F8 29 74 24 30 C5 F8 28 F3 48 8B DA 48 8B F9 48 "
    "85 D2 0F 84 67 01 00 00 48 8B 42 68 4C 8B 48 20 41 80 B9 73 02 00 00 00 0F 85 51 01 00 00 4D 85 "
    "C0 74 0B 41 83 78 60 00 0F 85 41 01 00 00 F6 81 64 03 00 00 01 75 15 48 8B 81 68 03 00 00 C5 FA "
    "10 05 ?? ?? ?? ?? C5 F8 2F 00 76 14 48 8B 89 A8 01 00 00 E8 ?? ?? ?? ?? 84 C0 0F 84 0F 01 00 00 "
    "48 8B 43 68 48 8B 58 38 48 8B 43 08 48 8D 50 08 33 F6 48 85 C0 48 0F 44 D6 48 8B 12 48 8D 4C 24 "
    "20 E8 ?? ?? ?? ?? 90 48 8D 54 24 58 48 8B CB E8 ?? ?? ?? ?? 90 48 8B 4C 24 20 48 8B 01 B2 01 FF "
    "50 20 48 8D 4C 24 58 E8 ?? ?? ?? ?? 40 38 70 25 0F 85 AF 00 00 00 48 8B 87 A8 01 00 00 C5 F8 57 "
    "C0 C4 E1 FA 2A 80 30 02 00 00 C5 FA 5E 15 ?? ?? ?? ?? C5 F8 57 C0 C4 E1 FA 2A 80 38 02 00 00 C5 "
    "FA 5E 1D ?? ?? ?? ?? C5 FA 10 87 30 03 00 00 C5 F8 2E C2 7A 06 75 04 32 C0 EB 02 B0 01 C5 F8 2F "
    "DA 77 5A 84 C0 75 56 E8 ?? ?? ?? ?? 84 C0 75 55 48 8B 87 68 03 00 00 C5 FA 10 08 C5 FA 10 05 ?? "
    "?? ?? ?? C5 F8 2F C1 72 08 C5 F2 58 C6 C5 FA 11 00 C5 FA 10 8F 08 03 00 00 C5 FA 10 87 0C 03 00 "
    "00 C5 F8 2F C8 72 28 C5 FA 58 C6 C5 FA 11 87 0C 03 00 00 C5 F8 2F C8 76 16 B0 01 EB 14 C5 FA 11 "
    "97 30 03 00 00 89 B7 0C 03 00 00 B0 01 EB 02 32 C0 48 8B 5C 24 50 48 8B 74 24 60 C5 F8 28 74 24 "
    "30 48 83 C4 40 5F C3"};

// Role 0..2 path including native selector table and both actor-owner reads.
constexpr Pattern kGaugePrefix{
    "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 4C 8B FA 4C 8B F1 48 85 D2 "
    "0F 84 40 08 00 00 80 B9 7E 02 00 00 00 0F 85 6B 0B 00 00 4C 8D A1 30 02 00 00 49 8B 14 24 48 89 "
    "55 E0 48 8B 89 38 02 00 00 48 89 4D D8 49 8D B6 50 02 00 00 8B 06 89 45 D0 41 0F B6 86 7C 02 00 "
    "00 88 45 48 41 80 BE 55 02 00 00 03 0F 83 5C 01 00 00 E8 ?? ?? ?? ?? 48 8B F8 65 48 8B 04 25 58 "
    "00 00 00 48 8B 08 B8 EC 01 00 00 4C 8B 05 ?? ?? ?? ?? 80 3C 08 00 4C 0F 45 05 ?? ?? ?? ?? 41 0F "
    "B6 96 55 02 00 00 49 8B 00 48 8B 48 08 48 8B 59 10 0F B7 9C 53 A0 00 00 00 49 8B 57 08 48 8D 4D "
    "50 E8 ?? ?? ?? ?? 90 41 80 BE 7D 02 00 00 00 74 3C 49 8B 47 68 48 8B 48 20 44 0F B7 C3 48 8D 55 "
    "58 48 8B 49 18 E8 ?? ?? ?? ?? 48 8B 08 48 B8 CF F7 53 E3 A5 9B C4 20 48 F7 E9 48 8B F2 48 C1 FE "
    "07 48 8B C6 48 C1 E8 3F 48 03 F0 EB 02 33 F6 49 8B 47 68 48 8B 48 20 4C 8B CF 44 0F B7 C3 48 8D "
    "55 58 48 8B 49 18 E8 ?? ?? ?? ?? 48 8B 08 48 B8 CF F7 53 E3 A5 9B C4 20 48 F7 E9 48 C1 FA 07 48 "
    "8B C2 48 C1 E8 3F 48 03 D0 48 2B D6 48 69 C2 E8 03 00 00 49 89 04 24 49 8B 47 68 48 8B 48 20 44 "
    "0F B7 C3 48 8D 55 58 48 8B 49 18 E8 ?? ?? ?? ?? 48 8B 08 48 B8 CF F7 53 E3 A5 9B C4 20 48 F7 E9 "
    "48 C1 FA 07 48 8B C2 48 C1 E8 3F 48 03 D0 48 2B D6 48 69 C2 E8 03 00 00 49 89 86 38 02 00 00 49 "
    "8B 47 68 48 8B 48 20 4C 8B C7 0F B7 D3 48 8B 49 18 E8 ?? ?? ?? ?? 41 88 86 7C 02 00 00 48 8B 4D "
    "50 48 8B 01 B2 01 FF 50 20 E9 ED 08 00 00"};
constexpr Pattern kGaugeTail{
    "48 8B 4D D8 48 8B 55 E0 48 BF CF F7 53 E3 A5 9B C4 20 41 8B 86 50 02 00 00 44 8B 45 D0 44 3B C0 "
    "74 1C 41 80 BE 54 02 00 00 00 74 0A 41 2B C0 41 89 86 EC 01 00 00 41 C6 86 54 02 00 00 00 4D 8B "
    "96 30 02 00 00 49 3B D2 75 09 49 3B 8E 38 02 00 00 74 7B 48 8B C7 49 F7 AE 40 02 00 00 48 8B CA "
    "48 C1 F9 07 48 8B C1 48 C1 E8 3F 48 03 C8 48 8B C7 49 F7 AE 48 02 00 00 4C 8B CA 49 C1 F9 07 49 "
    "8B C1 48 C1 E8 3F 4C 03 C8 48 8B C7 49 F7 AE 38 02 00 00 4C 8B C2 49 C1 F8 07 49 8B C0 48 C1 E8 "
    "3F 4C 03 C0 48 8B C7 49 F7 EA 48 C1 FA 07 48 8B C2 48 C1 E8 3F 48 03 D0 49 8B 06 C6 44 24 28 00 "
    "48 89 4C 24 20 49 8B CE FF 90 18 05 00 00 0F B6 45 48 41 3A 86 7C 02 00 00 74 08 49 8B CE E8 ?? "
    "?? ?? ?? B0 01 E9 C3 FC FF FF"};
constexpr Pattern kCost{
    "48 8B 4C 24 58 4C 8D 04 19 48 C1 EB 3F 84 DB 74 1C 48 8B 46 28 48 39 C1 7F 09 48 39 F8 0F 84 9A "
    "00 00 00 49 39 C0 7F 25 49 89 C0 EB 20 4C 8B 4E 20 4C 03 4E 18 49 39 C9 7F 05 49 39 F9 74 7E 4D "
    "39 C1 8A 0D ?? ?? ?? ?? 7F 03 4D 89 C8 44 0F B7 4C 24 50 48 89 EA 48 89 F1 E8 ?? ?? ?? ?? 48 89 "
    "F1 E8 ?? ?? ?? ?? 48 83 78 20 00 76 41"};
constexpr Pattern kCostPrefix{
    "48 89 5C 24 08 55 56 57 48 83 EC 20 31 C0 4C 89 C3 66 89 44 24 50 48 89 D5 48 89 CE 4D 85 C0 "
    "0F 84 D3 00 00 00 48 8B 79 08 4C 8D 4C 24 50 49 89 D0 48 8D 54 24 58 E8 ?? ?? ?? ??"};
constexpr Pattern kEvaluatorPrefix{
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 4D 8B F1 49 8B F0 48 8B FA 48 8B D9"};
constexpr Pattern kBooleanPrefix{
    "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 48 89 4C 24 08 57 48 83 EC 50 33 F6 48 8D 54 24 28"};
constexpr Pattern kWriterPrefix{
    "66 44 89 4C 24 20 48 89 54 24 10 53 55 56 57 41 56 48 83 EC 40 4C 8D 71 18 48 89 CF 48 8B 49 20 "
    "4C 89 C3 49 03 0E 4C 89 F6 4C 39 C1 7D 05 48 89 CB EB 10 48 8B 47 28 48 39 C3 7D 07 48 89 C3 48 8D 77 18"};
constexpr Pattern kWriterTail{
    "31 C0 48 89 D9 48 2B 0E 48 39 1E 48 0F 4E C1 48 89 47 20 48 8B 44 24 78 48 FF 47 48 48 89 47 38 "
    "0F B7 84 24 88 00 00 00 66 89 47 50 48 89 5F 08 48 83 C4 40 41 5E 5F 5E 5D 5B C3"};
constexpr Pattern kWriterBridge{
    "8B 0D ?? ?? ?? ?? 81 E9 AF D8 4D 8B 48 39 4F 30 0F 84 ?? ?? ?? ?? 80 7F 52 00"};

// 1.0.0.2944 retains the same native entry/pre-value/sign ownership.
// A dead RIP byte load disappeared from cost normalization, shortening the
// commit path by six bytes; the writer now compares its threshold to zero
// directly. Keep these as a paired exact form, with all link checks below.
constexpr Pattern kCost2944{
    "48 8B 4C 24 58 4C 8D 04 19 48 C1 EB 3F 84 DB 74 1C 48 8B 46 28 48 39 C1 7F 09 48 39 F8 0F 84 94 "
    "00 00 00 49 39 C0 7F 1F 49 89 C0 EB 1A 4C 8B 4E 20 4C 03 4E 18 49 39 C9 7F 05 49 39 F9 74 78 4D "
    "39 C1 7F 03 4D 89 C8 44 0F B7 4C 24 50 48 89 EA 48 89 F1 E8 ?? ?? ?? ?? 48 89 "
    "F1 E8 ?? ?? ?? ?? 48 83 78 20 00 76 41"};
constexpr Pattern kCostPrefix2944{
    "48 89 5C 24 08 55 56 57 48 83 EC 20 31 C0 4C 89 C3 66 89 44 24 50 48 89 D5 48 89 CE 4D 85 C0 "
    "0F 84 CD 00 00 00 48 8B 79 08 4C 8D 4C 24 50 49 89 D0 48 8D 54 24 58 E8 ?? ?? ?? ??"};
constexpr Pattern kWriterBridge2944{
    "48 83 7F 30 00 0F 84 ?? ?? ?? ?? 80 7F 52 00"};

constexpr Pattern kAb00{
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 49 8B D9 49 8B E8 "
    "0F B7 FA 4C 8B F1 E8 ?? ?? ?? ?? 84 C0 0F 84 8E 00 00 00 0F B7 D7 49 8B CE E8 ?? ?? ?? ?? 48 8B "
    "F0 48 85 DB 74 7B 33 C0 66 89 44 24 20 38 46 53 7F 3D 48 8B 4E 28 48 39 4E 08 7F 06 48 3B 6E 40 "
    "76 2D 4C 8D 4C 24 20 4C 8B C5 48 8D 54 24 28 48 8B CE E8 ?? ?? ?? ?? 44 0F B7 4C 24 20 48 8B D5 "
    "4C 8B 44 24 28 48 8B CE E8 ?? ?? ?? ?? EB 08 48 FF 46 48 48 89 6E 38 48 01 5E 10 48 8B D5 48 8B "
    "CE E8 ?? ?? ?? ?? 4C 8B 44 24 60 0F B7 D7 48 01 9E 88 00 00 00 49 8B CE E8 ?? ?? ?? ?? B0 01 EB "
    "02 32 C0 48 8B 5C 24 40 48 8B 6C 24 48 48 8B 74 24 50 48 8B 7C 24 58 48 83 C4 30 41 5E C3"};
constexpr Pattern kModifierCallerA{
    "44 0F B6 85 D8 00 00 00 48 8D 54 24 48 49 8B CD E8 ?? ?? ?? ?? 48 8D 45 C0 48 89 44 24 20 4C 8B "
    "4C 24 48 4D 8B C7 0F B7 95 E0 00 00 00 49 8B CD E8 ?? ?? ?? ?? 0F B6 F8"};
constexpr Pattern kModifierCallerB{
    "44 0F B6 85 D8 00 00 00 48 8D 54 24 58 49 8B CD E8 ?? ?? ?? ?? 48 8D 45 C0 48 89 44 24 20 4C 8B "
    "4C 24 58 4C 8B C3 0F B7 95 E0 00 00 00 49 8B CD E8 ?? ?? ?? ?? 88 44 24 40"};

// Entire mutator: grouping, grow/copy, remove/swap, penalty propagation, and
// before/after summation. This is deliberately conservative across updates.
constexpr Pattern kModifierMutator{
    "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B B1 D8 00 00 00 "
    "31 DB 4C 89 CE 45 0F B6 E8 44 8B 89 E0 00 00 00 49 89 CF 89 DF 44 89 C8 4C 89 F5 4F 8D 24 49 49 "
    "C1 E4 04 4D 01 F4 4D 39 E6 74 46 0F 1F 44 00 00 0F B7 06 66 41 39 06 75 1B 0F B7 46 10 66 41 39 "
    "46 10 75 10 48 8D 54 24 60 4C 89 F1 E8 ?? ?? ?? ?? 48 03 38 49 83 C6 30 4D 39 E6 75 D3 4D 8B B7 "
    "D8 00 00 00 41 8B 87 E0 00 00 00 4C 89 F5 41 89 C1 45 84 ED 0F 84 37 01 00 00 41 8B 8F E4 00 00 "
    "00 39 C1 0F 87 E8 00 00 00 41 BD 01 00 00 00 44 8D B4 09 01 00 00 00 41 01 CE 41 D1 EE 45 39 EE "
    "45 0F 42 F5 44 39 F0 44 0F 47 F0 41 39 CE 0F 86 BD 00 00 00 65 48 8B 04 25 58 00 00 00 43 8D 0C "
    "76 41 BC FD 01 00 00 BA 08 00 00 00 C1 E1 04 4C 03 20 41 38 1C 24 74 07 E8 ?? ?? ?? ?? EB 05 E8 "
    "?? ?? ?? ?? 48 89 C5 48 85 C0 75 19 E8 ?? ?? ?? ?? 45 31 C9 45 31 C0 44 89 EA B9 02 00 00 A0 FF "
    "15 ?? ?? ?? ?? 89 DA 41 39 9F E0 00 00 00 76 32 89 D0 FF C2 48 8D 0C 40 49 8B 87 D8 00 00 00 48 "
    "01 C9 C5 FC 10 04 C8 C5 FC 11 44 CD 00 C5 F8 10 4C C8 20 C5 F8 11 4C CD 20 41 3B 97 E0 00 00 00 "
    "72 CE 49 8B 8F D8 00 00 00 48 85 C9 74 15 C5 F8 77 41 38 1C 24 74 07 E8 ?? ?? ?? ?? EB 05 E8 ?? "
    "?? ?? ?? 49 89 AF D8 00 00 00 45 89 B7 E4 00 00 00 41 8B 87 E0 00 00 00 C5 FC 10 06 48 8D 0C 40 "
    "48 01 C9 C5 FC 11 44 CD 00 C5 F8 10 4E 20 C5 F8 11 4C CD 20 29 2D ?? ?? ?? ?? BD 01 00 B4 74 03 "
    "2D ?? ?? ?? ?? 41 01 AF E0 00 00 00 E9 7E 00 00 00 89 D9 45 85 C9 0F 84 73 00 00 00 0F B7 16 66 "
    "90 89 C8 4C 8D 04 40 4D 01 C0 66 43 39 14 C6 75 17 48 8B 46 08 4B 39 44 C6 08 75 0C 0F B7 46 10 "
    "66 43 39 44 C6 10 74 09 FF C1 44 39 C9 72 D2 EB 3E 41 8D 41 FF 39 C1 73 2F 41 8B 87 E0 00 00 00 "
    "49 8B 97 D8 00 00 00 FF C8 48 8D 0C 40 48 01 C9 C5 FC 10 04 CA C4 A1 7C 11 04 C2 C5 F8 10 4C CA "
    "20 C4 A1 78 11 4C C2 20 41 FF 8F E0 00 00 00 B8 FF FF 00 00 66 39 46 10 74 41 49 8B 8F D8 00 00 "
    "00 41 8B 87 E0 00 00 00 48 8D 14 40 48 C1 E2 04 48 01 CA 48 39 D1 74 23 0F B7 01 66 39 06 75 12 "
    "0F B7 41 10 66 39 46 10 75 08 48 8B 46 18 48 89 41 18 48 83 C1 30 48 39 D1 75 DD 49 8B AF D8 00 "
    "00 00 41 8B 87 E0 00 00 00 4C 8D 34 40 49 C1 E6 04 49 01 EE 4C 39 F5 74 36 0F 1F 80 00 00 00 00 "
    "0F B7 06 66 39 45 00 75 1D 0F B7 46 10 66 39 45 10 75 13 48 8D 54 24 60 48 89 E9 C5 F8 77 E8 ?? "
    "?? ?? ?? 48 03 18 48 83 C5 30 4C 39 F5 75 D1 48 8B 44 24 68 48 29 FB 48 89 18 C5 F8 77 48 8B 5C "
    "24 70 48 83 C4 20 41 5F 41 5E 41 5D 41 5C 5F 5E 5D C3"};
constexpr Pattern kContribution{
    "48 89 5C 24 10 48 89 7C 24 20 55 48 89 E5 48 83 EC 50 48 89 D3 48 8B 51 18 48 85 D2 74 76 31 FF "
    "48 89 7D D0 89 7D D8 48 8D 45 E0 48 89 45 10 B8 40 42 0F 00 48 29 D0 48 89 45 E0 89 7D E8 48 8D "
    "45 F0 48 89 45 20 48 8B 41 08 48 89 45 F0 89 7D F8 4C 8D 45 E0 48 8D 55 F0 41 B9 02 00 00 00 48 "
    "8D 4D D0 E8 ?? ?? ?? ?? B8 49 F7 D6 4C 2B 05 ?? ?? ?? ?? 39 45 D8 74 16 48 89 3B 48 89 D8 48 8B "
    "5C 24 68 48 8B 7C 24 78 48 83 C4 50 5D C3 48 8B 45 D0 EB 48 48 8B 41 28 48 85 C0 74 3B 31 FF 48 "
    "89 7D D0 89 7D D8 48 8D 55 F0 48 89 55 10 48 05 40 42 0F 00 48 89 45 F0 89 7D F8 48 8D 45 E0 48 "
    "89 45 20 48 8B 41 08 48 89 45 E0 89 7D E8 4C 8D 45 F0 48 8D 55 E0 EB 81 48 8B 41 08 48 89 03 48 "
    "89 D8 48 8B 5C 24 68 48 8B 7C 24 78 48 83 C4 50 5D C3"};
// 2944 retains the exact list layout, grouping, grow/copy/remove ordering,
// penalty propagation and before/after contribution ABI. Count addition now
// uses INC directly and arithmetic status uses a direct zero comparison.
// Keep the two changed native functions paired; mixed profiles fail closed.
constexpr Pattern kModifierMutator2944{
    "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B B1 D8 00 00 00 "
    "31 DB 4C 89 CE 45 0F B6 E8 44 8B 89 E0 00 00 00 49 89 CF 89 DF 44 89 C8 4C 89 F5 4F 8D 24 49 49 "
    "C1 E4 04 4D 01 F4 4D 39 E6 74 46 0F 1F 44 00 00 0F B7 06 66 41 39 06 75 1B 0F B7 46 10 66 41 39 "
    "46 10 75 10 48 8D 54 24 60 4C 89 F1 E8 ?? ?? ?? ?? 48 03 38 49 83 C6 30 4D 39 E6 75 D3 4D 8B B7 "
    "D8 00 00 00 41 8B 87 E0 00 00 00 4C 89 F5 41 89 C1 45 84 ED 0F 84 2D 01 00 00 41 8B 8F E4 00 00 "
    "00 39 C1 0F 87 E8 00 00 00 41 BD 01 00 00 00 44 8D B4 09 01 00 00 00 41 01 CE 41 D1 EE 45 39 EE "
    "45 0F 42 F5 44 39 F0 44 0F 47 F0 41 39 CE 0F 86 BD 00 00 00 65 48 8B 04 25 58 00 00 00 43 8D 0C "
    "76 41 BC FD 01 00 00 BA 08 00 00 00 C1 E1 04 4C 03 20 41 38 1C 24 74 07 E8 ?? ?? ?? ?? EB 05 E8 "
    "?? ?? ?? ?? 48 89 C5 48 85 C0 75 19 E8 ?? ?? ?? ?? 45 31 C9 45 31 C0 44 89 EA B9 02 00 00 A0 FF "
    "15 ?? ?? ?? ?? 89 DA 41 39 9F E0 00 00 00 76 32 89 D0 FF C2 48 8D 0C 40 49 8B 87 D8 00 00 00 48 "
    "01 C9 C5 FC 10 04 C8 C5 FC 11 44 CD 00 C5 F8 10 4C C8 20 C5 F8 11 4C CD 20 41 3B 97 E0 00 00 00 "
    "72 CE 49 8B 8F D8 00 00 00 48 85 C9 74 15 C5 F8 77 41 38 1C 24 74 07 E8 ?? ?? ?? ?? EB 05 E8 ?? "
    "?? ?? ?? 49 89 AF D8 00 00 00 45 89 B7 E4 00 00 00 41 8B 87 E0 00 00 00 C5 FC 10 06 48 8D 0C 40 "
    "48 01 C9 C5 FC 11 44 CD 00 C5 F8 10 4E 20 C5 F8 11 4C CD 20 40 88 2D ?? ?? ?? ?? 41 FF 87 E0 00 "
    "00 00 E9 7E 00 00 00 89 D9 45 85 C9 0F 84 73 00 00 00 0F B7 16 66 90 89 C8 4C 8D 04 40 4D 01 C0 "
    "66 43 39 14 C6 75 17 48 8B 46 08 4B 39 44 C6 08 75 0C 0F B7 46 10 66 43 39 44 C6 10 74 09 FF C1 "
    "44 39 C9 72 D2 EB 3E 41 8D 41 FF 39 C1 73 2F 41 8B 87 E0 00 00 00 49 8B 97 D8 00 00 00 FF C8 48 "
    "8D 0C 40 48 01 C9 C5 FC 10 04 CA C4 A1 7C 11 04 C2 C5 F8 10 4C CA 20 C4 A1 78 11 4C C2 20 41 FF "
    "8F E0 00 00 00 B8 FF FF 00 00 66 39 46 10 74 41 49 8B 8F D8 00 00 00 41 8B 87 E0 00 00 00 48 8D "
    "14 40 48 C1 E2 04 48 01 CA 48 39 D1 74 23 0F B7 01 66 39 06 75 12 0F B7 41 10 66 39 46 10 75 08 "
    "48 8B 46 18 48 89 41 18 48 83 C1 30 48 39 D1 75 DD 49 8B AF D8 00 00 00 41 8B 87 E0 00 00 00 4C "
    "8D 34 40 49 C1 E6 04 49 01 EE 4C 39 F5 74 30 90 0F B7 06 66 39 45 00 75 1D 0F B7 46 10 66 39 45 "
    "10 75 13 48 8D 54 24 60 48 89 E9 C5 F8 77 E8 ?? ?? ?? ?? 48 03 18 48 83 C5 30 4C 39 F5 75 D1 48 "
    "8B 44 24 68 48 29 FB 48 89 18 C5 F8 77 48 8B 5C 24 70 48 83 C4 20 41 5F 41 5E 41 5D 41 5C 5F 5E "
    "5D C3"};
constexpr Pattern kContribution2944{
    "48 89 5C 24 10 48 89 7C 24 20 55 48 89 E5 48 83 EC 50 48 89 D3 48 8B 51 18 48 85 D2 74 6C 31 FF "
    "48 89 7D D0 89 7D D8 48 8D 45 E0 48 89 45 10 B8 40 42 0F 00 48 29 D0 48 89 45 E0 89 7D E8 48 8D "
    "45 F0 48 89 45 20 48 8B 41 08 48 89 45 F0 89 7D F8 4C 8D 45 E0 48 8D 55 F0 41 B9 02 00 00 00 48 "
    "8D 4D D0 E8 ?? ?? ?? ?? 83 7D D8 00 74 16 48 89 3B 48 89 D8 48 8B 5C 24 68 48 8B 7C 24 78 48 83 "
    "C4 50 5D C3 48 8B 45 D0 EB 48 48 8B 41 28 48 85 C0 74 3B 31 FF 48 89 7D D0 89 7D D8 48 8D 55 F0 "
    "48 89 55 10 48 05 40 42 0F 00 48 89 45 F0 89 7D F8 48 8D 45 E0 48 89 45 20 48 8B 41 08 48 89 45 "
    "E0 89 7D E8 4C 8D 45 F0 48 8D 55 E0 EB 8B 48 8B 41 08 48 89 03 48 89 D8 48 8B 5C 24 68 48 8B 7C "
    "24 78 48 83 C4 50 5D C3"};
static_assert(kModifierMutator2944.size == 0x302 && kContribution2944.size == 0xE8);

constexpr Pattern kArithmeticPrefix{
    "48 89 74 24 18 44 89 4C 24 20 55 57 41 54 41 55 41 56 48 8B EC 48 83 EC 40 4D 8B 30 45 8B D9"};

static_assert(kVisibility.size == 0x1A7 && kGaugePrefix.size == 0x1CE && kGaugeTail.size == 0xEA);
static_assert(kModifierMutator.size == 0x312 && kContribution.size == 0xF2 && kAb00.size == 0xDE);


// Mounted-resource sidecar: exact register/guard contracts; only external
// rel32 operands vary. No species IDs, resource maximums, or absolute RVAs.
struct MountedReference { std::size_t offset, field, length, read_size; };
constexpr Pattern kMountRootPrefix{
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC D0 "
    "00 00 00 C5 F8 29 78 C8 C5 F8 28 F9 48 8B F1 45 33 F6 48 8B 05 ?? ?? ?? ?? 48 8B 90 80 00 00 00 "
    "0F B6 82 5C 01 00 00 FE C8 3C 16 0F 86 15 0C 00 00 48 8D 55 FF E8 ?? ?? ?? ?? 90 45 8B FE 44 38 "
    "75 0F 4C 0F 45 7D 07 48 8D 55 DF E8 ?? ?? ?? ?? 90 45 8B E6 44 38 75 EF 4C 0F 45 65 E7 45 8B EE "
    "4D 85 FF 74 4D 49 8B 57 08 48 8D 4D 77 E8 ?? ?? ?? ?? 90 49 8B CF E8 ?? ?? ?? ?? 48 8D 9E 79 03 "
    "00 00 84 C0 74 09 0F B6 03 0F BA E8 00 88 03 49 8B CF E8 ?? ?? ?? ?? 84 C0 74 09 0F B6 03 0F BA "
    "E8 00 88 03 48 8B 4D 77 48 8B 01 B2 01 FF 50 20 EB 07 48 8D 9E 79 03 00 00 41 8B C6 0F 1F 40 00 "
    "80 3B 00 75 0C FF C0 48 FF C3 83 F8 02 72 F1 EB 43 C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 86 54 03 00 "
    "00 0F B6 86 79 03 00 00 A8 20 75 28 A8 40 75 24 A8 08 75 20 0F B6 86 7A 03 00 00 A8 02 75 15 A8 "
    "04 75 11 A8 20 75 0D A8 08 75 09 33 C0 66 89 86 79 03 00 00 C5 FA 10 8E 54 03 00 00 48 8B 8E 58 "
    "01 00 00 C5 F8 57 C0 C5 F8 2F C8 76 1F C5 F2 5C C7 C5 FA 11 86 54 03 00 00 E8 ?? ?? ?? ?? 48 8B "
    "8E 68 01 00 00 E8 ?? ?? ?? ?? EB 18 44 89 B6 54 03 00 00 E8 ?? ?? ?? ?? 48 8B 8E 68 01 00 00 E8 "
    "?? ?? ?? ?? 80 BE F9 02 00 00 00 75 30 C5 C2 58 8E FC 02 00 00 C5 FA 11 8E FC 02 00 00 C5 FA 10 "
    "05 ?? ?? ?? ?? C5 F8 2F C1 0F 87 12 01 00 00 44 89 B6 FC 02 00 00 C6 86 F9 02 00 00 01 80 BE F8 "
    "02 00 00 01 75 0F C6 86 F8 02 00 00 00 48 8B CE E8 ?? ?? ?? ?? 4D 85 FF 0F 84 ED 03 00 00 44 8B "
    "86 60 03 00 00 48 8D 55 97 48 8B 0D ?? ?? ?? ?? 48 8B 09 E8 ?? ?? ?? ?? 90 80 7D A7 00 0F 84 DA "
    "02 00 00 4C 8B 6D 9F 48 8B 8E E0 01 00 00 41 8B 45 60 89 81 D0 00 00 00 85 C0 0F 95 C2 E8 ?? ?? "
    "?? ?? 48 8B 8E E8 01 00 00 48 8B 45 9F 8B 50 60 89 91 D0 00 00 00 85 D2 0F 95 C2 E8 ?? ?? ?? ?? "};
constexpr std::array<MountedReference, 17> kMountRootPrefixRefs{{
    {0x32, 0x3, 0x7, 0x8},
    {0x55, 0x1, 0x5, 0x0},
    {0x6B, 0x1, 0x5, 0x0},
    {0x8D, 0x1, 0x5, 0x0},
    {0x96, 0x1, 0x5, 0x0},
    {0xB2, 0x1, 0x5, 0x0},
    {0xF1, 0x4, 0x8, 0x4},
    {0x159, 0x1, 0x5, 0x0},
    {0x165, 0x1, 0x5, 0x0},
    {0x173, 0x1, 0x5, 0x0},
    {0x17F, 0x1, 0x5, 0x0},
    {0x19D, 0x4, 0x8, 0x4},
    {0x1D0, 0x1, 0x5, 0x0},
    {0x1E9, 0x3, 0x7, 0x8},
    {0x1F3, 0x1, 0x5, 0x0},
    {0x21D, 0x1, 0x5, 0x0},
    {0x23B, 0x1, 0x5, 0x0},
}};
static_assert(kMountRootPrefix.size == 0x240);
constexpr Pattern kMountRootGauge{
    "48 8B 55 9F 8B 42 60 39 86 F0 02 00 00 0F 84 B7 02 00 00 89 86 F0 02 00 00 48 8B 8E B8 01 00 00 "
    "E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? "};
constexpr std::array<MountedReference, 3> kMountRootGaugeRefs{{
    {0x20, 0x1, 0x5, 0x0},
    {0x28, 0x1, 0x5, 0x0},
    {0x2D, 0x1, 0x5, 0x0},
}};
static_assert(kMountRootGauge.size == 0x32);
constexpr Pattern kMountRootHorseCall{
    "C5 F8 28 DF 4D 8B C5 49 8B D7 48 8B CE E8 ?? ?? ?? ?? 84 C0 74 38 "};
constexpr std::array<MountedReference, 1> kMountRootHorseCallRefs{{
    {0xD, 0x1, 0x5, 0x0},
}};
static_assert(kMountRootHorseCall.size == 0x16);
constexpr Pattern kMountHorsePrefix{
    "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 40 C5 F8 29 74 24 30 C5 F8 28 F3 49 8B "
    "F0 48 8B DA 48 8B F9 48 85 D2 0F 84 5C 01 00 00 48 8B 42 68 4C 8B 48 20 41 80 B9 73 02 00 00 00 "
    "0F 85 46 01 00 00 4D 85 C0 0F 84 3D 01 00 00 F6 81 64 03 00 00 02 75 16 48 8B 81 68 03 00 00 C5 "
    "FA 10 05 ?? ?? ?? ?? C5 F8 2F 40 04 76 17 48 8B D6 48 8B 89 B8 01 00 00 E8 ?? ?? ?? ?? 84 C0 0F "
    "84 07 01 00 00 "};
constexpr std::array<MountedReference, 2> kMountHorsePrefixRefs{{
    {0x5F, 0x4, 0x8, 0x4},
    {0x78, 0x1, 0x5, 0x0},
}};
static_assert(kMountHorsePrefix.size == 0x85);
constexpr Pattern kMountRide{
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F1 33 FF 89 7C 24 20 48 8D 54 24 38 E8 ?? ?? "
    "?? ?? 90 40 38 7C 24 48 74 60 48 8B 5C 24 40 48 8D 53 08 48 85 DB 48 0F 44 D7 48 8B 12 48 8D 4C "
    "24 28 E8 ?? ?? ?? ?? 90 48 89 5C 24 30 B9 02 00 00 00 89 4C 24 20 48 8B 43 68 48 8B 40 48 48 85 "
    "C0 74 0C 48 8B 40 18 48 85 C0 74 03 8B 78 10 89 BE 60 03 00 00 83 E1 FD 89 4C 24 20 48 8B 4C 24 "
    "28 48 8B 01 B2 01 FF 50 20 90 48 8D 05 ?? ?? ?? ?? 48 89 44 24 38 48 8D 4C 24 38 E8 ?? ?? ?? ?? "
    "48 8B 5C 24 70 48 8B 74 24 78 48 83 C4 60 5F C3 "};
constexpr std::array<MountedReference, 4> kMountRideRefs{{
    {0x1D, 0x1, 0x5, 0x0},
    {0x42, 0x1, 0x5, 0x0},
    {0x8A, 0x3, 0x7, 0x8},
    {0x9B, 0x1, 0x5, 0x0},
}};
static_assert(kMountRide.size == 0xB0);
constexpr Pattern kMountPlayer{
    "48 89 54 24 10 53 48 83 EC 30 48 8B DA C7 44 24 20 00 00 00 00 48 8B 0D ?? ?? ?? ?? 48 8B 49 30 "
    "E8 ?? ?? ?? ?? 90 C7 44 24 20 01 00 00 00 48 8B C3 48 83 C4 30 5B C3 CC "};
constexpr std::array<MountedReference, 2> kMountPlayerRefs{{
    {0x15, 0x3, 0x7, 0x8},
    {0x20, 0x1, 0x5, 0x0},
}};
static_assert(kMountPlayer.size == 0x38);
constexpr Pattern kMountPlayerActor{
    "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 56 41 57 48 83 EC 50 48 8B DA 4C 8B F1 45 33 FF 44 89 "
    "7C 24 20 48 8B 79 58 48 85 FF 75 39 48 8D 2D ?? ?? ?? ?? 48 89 2A 4C 89 7A 08 66 44 89 7A 10 4C "
    "89 7A 18 48 8D 35 ?? ?? ?? ?? 48 89 32 48 8D 05 ?? ?? ?? ?? 48 89 02 BE 01 00 00 00 89 74 24 20 "
    "E9 09 01 00 00 BA 04 00 00 00 41 B8 10 00 00 00 48 8B 07 48 8B CF FF 90 C0 00 00 00 48 8D 2D ?? "
    "?? ?? ?? 48 89 6C 24 28 48 89 7C 24 30 84 C0 0F 95 44 24 38 44 88 7C 24 39 4C 89 7C 24 40 48 8D "
    "35 ?? ?? ?? ?? 48 89 74 24 28 48 8D 0D ?? ?? ?? ?? 48 89 4C 24 28 84 C0 75 31 48 89 2B 4C 89 7B "
    "08 66 C7 43 10 00 00 4C 89 7B 18 48 89 33 48 8D 05 ?? ?? ?? ?? 48 89 03 BE 01 00 00 00 89 74 24 "
    "20 48 89 6C 24 28 E9 83 00 00 00 49 8B 46 58 4C 8B B0 D8 00 00 00 4D 85 F6 74 1F BA 04 00 00 00 "
    "41 B8 10 00 00 00 49 8B 06 49 8B CE FF 90 C0 00 00 00 84 C0 74 04 B0 01 EB 02 32 C0 48 89 2B 4C "
    "89 73 08 88 43 10 C6 43 11 00 4C 89 7B 18 48 89 33 48 8D 05 ?? ?? ?? ?? 48 89 03 BE 01 00 00 00 "
    "89 74 24 20 48 89 6C 24 28 48 8B 07 48 8B CF FF 50 08 84 C0 74 13 33 C0 F0 40 0F B0 77 4B 75 09 "
    "48 8B 07 48 8B CF FF 50 10 C6 44 24 38 00 48 8B C3 48 8B 9C 24 90 00 00 00 48 83 C4 50 41 5F 41 "
    "5E 5F 5E 5D C3 CC "};
constexpr std::array<MountedReference, 8> kMountPlayerActorRefs{{
    {0x2C, 0x3, 0x7, 0x8},
    {0x43, 0x3, 0x7, 0x8},
    {0x4D, 0x3, 0x7, 0x8},
    {0x7C, 0x3, 0x7, 0x8},
    {0x9E, 0x3, 0x7, 0x8},
    {0xAA, 0x3, 0x7, 0x8},
    {0xCE, 0x3, 0x7, 0x8},
    {0x131, 0x3, 0x7, 0x8},
}};
static_assert(kMountPlayerActor.size == 0x186);
constexpr Pattern kMountActorResolve{
    "48 89 5C 24 08 48 89 54 24 10 55 56 57 41 56 41 57 48 83 EC 30 41 8B F8 48 8B F2 48 8B E9 45 33 "
    "FF 44 89 7C 24 20 45 85 C0 75 30 48 8D 0D ?? ?? ?? ?? 48 89 0A 4C 89 7A 08 66 44 89 7A 10 4C 89 "
    "7A 18 48 8D 05 ?? ?? ?? ?? 48 89 02 48 8D 05 ?? ?? ?? ?? 48 89 02 E9 6D 01 00 00 8B C7 25 00 00 "
    "00 30 3D 00 00 00 10 75 30 48 8D 0D ?? ?? ?? ?? 48 89 0A 4C 89 7A 08 66 44 89 7A 10 4C 89 7A 18 "
    "48 8D 05 ?? ?? ?? ?? 48 89 02 48 8D 05 ?? ?? ?? ?? 48 89 02 E9 2F 01 00 00 48 8D 59 10 48 89 5C "
    "24 78 41 BE FF FF FF FF F7 43 0C 00 00 00 40 74 27 41 8B CF 41 8B C6 F0 44 0F B1 73 08 85 C0 74 "
    "17 81 F9 E8 03 00 00 73 0F FF C1 41 8B C6 F0 44 0F B1 73 08 85 C0 75 E9 48 8B 03 B2 01 48 8B CB "
    "FF 50 18 F7 43 0C 00 00 00 40 74 2A 41 8B C6 F0 44 0F B1 73 08 85 C0 74 1D 0F 1F 80 00 00 00 00 "
    "48 8B 03 48 8B CB FF 50 38 41 8B C6 F0 44 0F B1 73 08 85 C0 75 EA 8B D7 48 8B 4D 08 E8 ?? ?? ?? "
    "?? 48 8B F8 48 85 C0 74 68 F6 40 5E 10 74 62 BA 04 00 00 00 41 B8 10 00 00 00 48 8B 08 4C 8B 89 "
    "C0 00 00 00 48 8B C8 41 FF D1 84 C0 0F 95 C0 48 8D 0D ?? ?? ?? ?? 48 89 0E 48 89 7E 08 88 46 10 "
    "C6 46 11 00 4C 89 7E 18 48 8D 05 ?? ?? ?? ?? 48 89 06 48 8D 05 ?? ?? ?? ?? 48 89 06 C7 44 24 20 "
    "01 00 00 00 48 8B 03 B2 01 48 8B CB FF 50 20 EB 3F 48 8B 03 B2 01 48 8B CB FF 50 20 48 8D 0D ?? "
    "?? ?? ?? 48 89 0E 4C 89 7E 08 66 C7 46 10 00 00 4C 89 7E 18 48 8D 05 ?? ?? ?? ?? 48 89 06 48 8D "
    "05 ?? ?? ?? ?? 48 89 06 C7 44 24 20 01 00 00 00 48 8B C6 48 8B 5C 24 60 48 83 C4 30 41 5F 41 5E "
    "5F 5E 5D C3 CC "};
constexpr std::array<MountedReference, 13> kMountActorResolveRefs{{
    {0x2B, 0x3, 0x7, 0x8},
    {0x42, 0x3, 0x7, 0x8},
    {0x4C, 0x3, 0x7, 0x8},
    {0x69, 0x3, 0x7, 0x8},
    {0x80, 0x3, 0x7, 0x8},
    {0x8A, 0x3, 0x7, 0x8},
    {0x11C, 0x1, 0x5, 0x0},
    {0x14F, 0x3, 0x7, 0x8},
    {0x168, 0x3, 0x7, 0x8},
    {0x172, 0x3, 0x7, 0x8},
    {0x19C, 0x3, 0x7, 0x8},
    {0x1B4, 0x3, 0x7, 0x8},
    {0x1BE, 0x3, 0x7, 0x8},
}};
static_assert(kMountActorResolve.size == 0x1E5);
constexpr Pattern kMountQuery{
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 66 44 89 44 24 18 41 56 48 83 EC 30 4C 8B F1 49 8B "
    "F9 48 8D 4C 24 50 41 0F B7 D8 48 8B F2 E8 ?? ?? ?? ?? 80 78 11 00 0F 85 C1 00 00 00 48 8D 4C 24 "
    "20 66 89 5C 24 20 E8 ?? ?? ?? ?? 80 78 11 00 0F 85 FB 00 00 00 49 8B 0E 48 83 C1 30 E8 ?? ?? ?? "
    "?? 0F B7 88 B8 05 00 00 66 89 4C 24 20 48 8D 4C 24 20 E8 ?? ?? ?? ?? 83 78 30 00 0F 84 CF 00 00 "
    "00 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8D 4C 24 20 66 89 5C 24 20 48 8B F8 E8 ?? ?? ?? ?? 8B 48 14 "
    "48 8B 47 48 83 3C 88 FF 0F 84 A2 00 00 00 49 8B 0E 48 83 C1 30 E8 ?? ?? ?? ?? 0F B7 88 B8 05 00 "
    "00 66 89 4C 24 20 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8D 4C 24 20 66 89 5C 24 20 48 8B F8 E8 ?? ?? "
    "?? ?? 8B 48 14 48 8B 47 48 8B 0C 88 49 8B 46 18 48 C1 E1 05 48 8B 0C 01 48 89 0E EB 7D 65 48 8B "
    "04 25 58 00 00 00 BA EC 01 00 00 48 8B 08 48 8B 05 ?? ?? ?? ?? 80 3C 0A 00 48 0F 45 05 ?? ?? ?? "
    "?? 48 8B 00 48 8B 48 08 48 8B 41 10 66 3B 98 A0 00 00 00 75 0C 49 8B 06 80 B8 73 02 00 00 00 75 "
    "0F 0F B7 D3 49 8B CE E8 ?? ?? ?? ?? 84 C0 75 0C 48 8B 05 ?? ?? ?? ?? 48 89 06 EB 1E 0F B7 D3 49 "
    "8B CE E8 ?? ?? ?? ?? 4C 8D 4C 24 20 4C 8B C7 48 8B D6 48 8B C8 E8 ?? ?? ?? ?? 48 8B 5C 24 40 48 "
    "8B C6 48 8B 74 24 48 48 8B 7C 24 58 48 83 C4 30 41 5E C3 "};
constexpr std::array<MountedReference, 15> kMountQueryRefs{{
    {0x2D, 0x1, 0x5, 0x0},
    {0x46, 0x1, 0x5, 0x0},
    {0x5C, 0x1, 0x5, 0x0},
    {0x72, 0x1, 0x5, 0x0},
    {0x86, 0x1, 0x5, 0x0},
    {0x98, 0x1, 0x5, 0x0},
    {0xB5, 0x1, 0x5, 0x0},
    {0xCB, 0x1, 0x5, 0x0},
    {0xDD, 0x1, 0x5, 0x0},
    {0x10E, 0x3, 0x7, 0x8},
    {0x119, 0x4, 0x8, 0x8},
    {0x147, 0x1, 0x5, 0x0},
    {0x150, 0x3, 0x7, 0x8},
    {0x162, 0x1, 0x5, 0x0},
    {0x175, 0x1, 0x5, 0x0},
}};
static_assert(kMountQuery.size == 0x193);
constexpr Pattern kMountSlot{
    "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20 48 8B F1 0F B7 DA 48 8B 09 48 83 C1 30 E8 ?? ?? ?? "
    "?? 48 8D 4C 24 38 0F B7 90 B8 05 00 00 66 89 54 24 38 E8 ?? ?? ?? ?? 48 8D 4C 24 38 66 89 5C 24 "
    "38 48 8B F8 E8 ?? ?? ?? ?? 8B 48 14 48 8B 47 58 8B 14 88 83 FA FF 75 12 33 C0 48 8B 5C 24 30 48 "
    "8B 74 24 40 48 83 C4 20 5F C3 48 8B 5C 24 30 48 8D 04 D2 48 C1 E0 04 48 03 46 58 48 8B 74 24 40 "
    "48 83 C4 20 5F C3 "};
constexpr std::array<MountedReference, 3> kMountSlotRefs{{
    {0x1C, 0x1, 0x5, 0x0},
    {0x32, 0x1, 0x5, 0x0},
    {0x44, 0x1, 0x5, 0x0},
}};
static_assert(kMountSlot.size == 0x86);
constexpr Pattern kMountValidSelector{
    "48 89 5C 24 08 66 89 54 24 10 57 48 83 EC 20 48 8B F9 0F B7 DA 48 8D 4C 24 38 E8 ?? ?? ?? ?? 80 "
    "78 11 00 74 5D 48 8B 0F 48 83 C1 30 E8 ?? ?? ?? ?? 0F B7 88 B8 05 00 00 66 89 4C 24 40 48 8D 4C "
    "24 40 E8 ?? ?? ?? ?? 83 78 20 00 74 35 48 8D 4C 24 40 E8 ?? ?? ?? ?? 48 8D 4C 24 40 66 89 5C 24 "
    "40 48 8B F8 E8 ?? ?? ?? ?? 8B 48 14 48 8B 47 58 83 3C 88 FF 0F 95 C0 48 8B 5C 24 30 48 83 C4 20 "
    "5F C3 48 8B 5C 24 30 32 C0 48 83 C4 20 5F C3 "};
constexpr std::array<MountedReference, 5> kMountValidSelectorRefs{{
    {0x1A, 0x1, 0x5, 0x0},
    {0x2C, 0x1, 0x5, 0x0},
    {0x42, 0x1, 0x5, 0x0},
    {0x52, 0x1, 0x5, 0x0},
    {0x64, 0x1, 0x5, 0x0},
}};
static_assert(kMountValidSelector.size == 0x8F);
constexpr Pattern kMountBindingHead{
    "48 89 5C 24 08 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 60 FF FF FF 48 81 EC "
    "A0 01 00 00 48 8B FA 48 8B F1 45 33 E4 "};
static_assert(kMountBindingHead.size == 0x2D);
constexpr Pattern kMountBinding{
    "4C 8D 0D ?? ?? ?? ?? 4C 8D 86 B8 01 00 00 48 8D 95 F0 00 00 00 48 8B CE E8 ?? ?? ?? ?? 8B 85 F0 "
    "00 00 00 85 C0 0F 85 43 08 00 00 48 8B 86 B8 01 00 00 80 B8 64 02 00 00 09 0F 85 29 08 00 00 "};
constexpr std::array<MountedReference, 2> kMountBindingRefs{{
    {0x0, 0x3, 0x7, 0x8},
    {0x18, 0x1, 0x5, 0x0},
}};
static_assert(kMountBinding.size == 0x3F);
constexpr Pattern kMountSelectorReg{
    "41 B9 FF FF 02 00 48 8D 15 ?? ?? ?? ?? 41 B8 01 00 00 00 48 8D 0D ?? ?? ?? ?? E9 ?? ?? ?? ?? "};
constexpr std::array<MountedReference, 3> kMountSelectorRegRefs{{
    {0x6, 0x3, 0x7, 0x8},
    {0x13, 0x3, 0x7, 0x8},
    {0x1A, 0x1, 0x5, 0x0},
}};
static_assert(kMountSelectorReg.size == 0x1F);
constexpr Pattern kMountRootRouting{
    "48 8D 55 77 48 8B 4D 9F E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 8B 48 14 39 0D ?? ?? ?? ?? 0F 84 "
    "04 01 00 00 39 0D ?? ?? ?? ?? 0F 84 F8 00 00 00 39 0D ?? ?? ?? ?? 0F 84 EC 00 00 00 39 0D ?? ?? "
    "?? ?? 0F 84 E0 00 00 00 C6 86 FA 02 00 00 00 "};
constexpr std::array<MountedReference, 6> kMountRootRoutingRefs{{
    {0x8, 0x1, 0x5, 0x0},
    {0x10, 0x1, 0x5, 0x0},
    {0x18, 0x2, 0x6, 0x4},
    {0x24, 0x2, 0x6, 0x4},
    {0x30, 0x2, 0x6, 0x4},
    {0x3C, 0x2, 0x6, 0x4},
}};
static_assert(kMountRootRouting.size == 0x4F);
constexpr Pattern kMountRootPrefixAnchor{
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC D0 "
    "00 00 00 C5 F8 29 78 C8 C5 F8 28 F9 48 8B F1 45 33 F6 "};
constexpr Pattern kMountRideAnchor{
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F1 33 FF 89 7C 24 20 48 8D 54 24 38 "};
constexpr Pattern kMountBindingHeadAnchor{
    "48 89 5C 24 08 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 60 FF FF FF 48 81 EC "
    "A0 01 00 00 48 8B FA 48 8B F1 45 33 E4 "};
constexpr Pattern kMountQueryAnchor{
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 66 44 89 44 24 18 41 56 48 83 EC 30 4C 8B F1 49 8B "
    "F9 48 8D 4C 24 50 41 0F B7 D8 48 8B F2 "};

struct Span { std::size_t start = 0, size = 0; DWORD flags = 0; };
struct Function { std::size_t begin = 0, end = 0; };
struct Image {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::array<Span, 96> sections{};
    std::size_t section_count = 0;
    IMAGE_DATA_DIRECTORY exceptions{};

    bool Range(std::size_t at, std::size_t count) const {
        return at <= size && count <= size - at;
    }
    template <typename T> bool Read(std::size_t at, T& value) const {
        if (!Range(at, sizeof(value))) return false;
        std::memcpy(&value, data + at, sizeof(value));
        return true;
    }
    bool Section(std::size_t at, std::size_t count, DWORD required) const {
        if (!Range(at, count)) return false;
        for (std::size_t i = 0; i < section_count; ++i) {
            const auto& s = sections[i];
            if ((s.flags & required) == required && at >= s.start &&
                at - s.start <= s.size && count <= s.size - (at - s.start)) return true;
        }
        return false;
    }
    bool Exec(std::size_t at, std::size_t count = 1) const {
        return Section(at, count, IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ);
    }
    bool Init(const void* pointer, std::size_t length) {
        data = static_cast<const std::uint8_t*>(pointer);
        size = length;
        if (!data || length > (std::numeric_limits<std::uintptr_t>::max)() -
                reinterpret_cast<std::uintptr_t>(data)) return false;
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!Read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
            !Read(static_cast<std::size_t>(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt.OptionalHeader.SizeOfImage == 0 || nt.OptionalHeader.SizeOfImage > size ||
            nt.OptionalHeader.SizeOfHeaders > nt.OptionalHeader.SizeOfImage ||
            nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) return false;
        size = nt.OptionalHeader.SizeOfImage;
        section_count = nt.FileHeader.NumberOfSections;
        if (!section_count || section_count > sections.size()) return false;
        const auto table = static_cast<std::size_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
        if (table > nt.OptionalHeader.SizeOfHeaders ||
            section_count * sizeof(IMAGE_SECTION_HEADER) > nt.OptionalHeader.SizeOfHeaders - table) return false;
        for (std::size_t i = 0; i < section_count; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!Read(table + i * sizeof(section), section)) return false;
            const std::size_t extent = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            if (!Range(section.VirtualAddress, extent)) return false;
            sections[i] = {section.VirtualAddress, extent, section.Characteristics};
        }
        exceptions = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        return exceptions.Size && exceptions.Size % sizeof(RUNTIME_FUNCTION) == 0 &&
               Section(exceptions.VirtualAddress, exceptions.Size, IMAGE_SCN_MEM_READ);
    }
    bool FunctionAt(std::size_t at, Function& function) const {
        unsigned found = 0;
        for (std::size_t i = 0; i < exceptions.Size; i += sizeof(RUNTIME_FUNCTION)) {
            RUNTIME_FUNCTION entry{};
            if (!Read(exceptions.VirtualAddress + i, entry)) return false;
            if (at >= entry.BeginAddress && at < entry.EndAddress) {
                if (++found != 1 || !Exec(entry.BeginAddress, entry.EndAddress - entry.BeginAddress)) return false;
                function = {entry.BeginAddress, entry.EndAddress};
            }
        }
        return found == 1;
    }
    std::size_t Relative(std::size_t instruction, std::size_t field, std::size_t length) const {
        std::int32_t displacement = 0;
        if (!Exec(instruction, length) || !Read(instruction + field, displacement)) return 0;
        const auto target = static_cast<std::int64_t>(instruction) +
                            static_cast<std::int64_t>(length) + displacement;
        return target > 0 && static_cast<std::uint64_t>(target) < size ? static_cast<std::size_t>(target) : 0;
    }
    std::size_t Branch(std::size_t at, std::uint8_t opcode = 0xE8) const {
        if (!Exec(at, 5) || data[at] != opcode) return 0;
        const auto target = Relative(at, 1, 5);
        return Exec(target) ? target : 0;
    }
    std::size_t Follow(std::size_t at) const {
        std::array<std::size_t, 4> seen{};
        for (std::size_t i = 0; i < seen.size(); ++i) {
            if (!at || !Exec(at, 5)) return 0;
            for (std::size_t j = 0; j < i; ++j) if (seen[j] == at) return 0;
            seen[i] = at;
            if (data[at] != 0xE9) return at;
            at = Branch(at, 0xE9);
        }
        return 0;
    }
    template <std::size_t N> bool Match(std::size_t at, const Pattern<N>& pattern) const {
        if (!Exec(at, pattern.size)) return false;
        for (std::size_t i = 0; i < pattern.size; ++i)
            if (pattern.bytes[i] >= 0 && data[at + i] != pattern.bytes[i]) return false;
        return true;
    }
    template <std::size_t N> ScanStatus Find(const Pattern<N>& pattern, std::size_t& at) const {
        at = 0;
        unsigned count = 0;
        for (std::size_t i = 0; i < section_count; ++i) {
            const auto& s = sections[i];
            if (!Exec(s.start, s.size) || s.size < pattern.size) continue;
            std::size_t cursor = s.start;
            const auto last = s.start + s.size - pattern.size;
            while (cursor <= last) {
                const auto* p = static_cast<const std::uint8_t*>(std::memchr(
                    data + cursor, pattern.bytes[0], last - cursor + 1));
                if (!p) break;
                cursor = static_cast<std::size_t>(p - data);
                if (Match(cursor, pattern)) {
                    if (++count > 1) { at = 0; return ScanStatus::Ambiguous; }
                    at = cursor;
                }
                ++cursor;
            }
        }
        return count == 1 ? ScanStatus::Ready : ScanStatus::NotFound;
    }
};

ScanResult Fail(ScanStatus status, ScanStage stage) { ScanResult r{}; r.status = status; r.stage = stage; return r; }

bool CheckVisibilityLinks(const Image& image, std::size_t visibility, std::size_t gauge) {
    Function function{};
    if (!image.FunctionAt(visibility, function) || function.begin != visibility ||
        function.end != visibility + kVisibility.size || image.Branch(visibility + 0x73) != gauge ||
        !image.Match(image.Follow(image.Branch(visibility + 0x127)), kBooleanPrefix)) return false;
    for (const std::size_t call : {0xA1U, 0xAFU, 0xC7U}) if (!image.Branch(visibility + call)) return false;
    const auto timer_constant = image.Relative(visibility + 0x5E, 4, 8);
    const auto scale_constant = image.Relative(visibility + 0xEA, 4, 8);
    return timer_constant && scale_constant &&
        image.Section(timer_constant, 4, IMAGE_SCN_MEM_READ) &&
        image.Section(scale_constant, 4, IMAGE_SCN_MEM_READ) &&
        timer_constant == image.Relative(visibility + 0x13B, 4, 8) &&
        scale_constant == image.Relative(visibility + 0xFF, 4, 8);
}

bool CheckGaugeLinks(const Image& image, std::size_t gauge, std::size_t tail) {
    Function start{}, finish{};
    if (!image.FunctionAt(gauge, start) || !image.FunctionAt(tail, finish) ||
        start.begin != gauge || start.begin != finish.begin || start.end != finish.end ||
        tail + kGaugeTail.size != start.end || image.Branch(gauge + 0x1C9, 0xE9) != tail) return false;
    for (const std::size_t call : {0x72U, 0xC1U, 0xE5U, 0x126U, 0x16BU, 0x1B1U})
        if (!image.Branch(gauge + call)) return false;
    const auto table = image.Relative(gauge + 0x8B, 3, 7);
    const auto alternate = image.Relative(gauge + 0x96, 4, 8);
    return table && alternate == table + 8 && image.Section(table, 16, IMAGE_SCN_MEM_READ) &&
           image.Branch(tail + 0xDE) && image.Branch(tail + 0xE5, 0xE9) >= start.begin &&
           image.Branch(tail + 0xE5, 0xE9) < start.end;
}


MountedResourceScanResult MountedFail(ScanStatus status, MountedScanStage stage) {
    MountedResourceScanResult result{};
    result.status = status;
    result.stage = stage;
    return result;
}

bool MountedFunction(const Image& image, std::size_t at, std::size_t size) {
    if (!at || !size || !image.Exec(at, size)) return false;
    const auto end = at + size;
    unsigned owners = 0;
    // An interior record can overlap the hook without covering the function
    // start. Require one owner for the entire validated instruction extent.
    for (std::size_t i = 0; i < image.exceptions.Size; i += sizeof(RUNTIME_FUNCTION)) {
        RUNTIME_FUNCTION entry{};
        if (!image.Read(image.exceptions.VirtualAddress + i, entry)) return false;
        if (entry.BeginAddress < end && entry.EndAddress > at) {
            if (++owners != 1 || entry.BeginAddress != at || entry.EndAddress != end) return false;
        }
    }
    return owners == 1;
}

template <std::size_t N>
bool MountedReferences(const Image& image, std::size_t at,
                       const std::array<MountedReference, N>& references) {
    for (const auto& ref : references) {
        const auto target = image.Relative(at + ref.offset, ref.field, ref.length);
        if (!target || (ref.read_size
            ? !image.Section(target, ref.read_size, IMAGE_SCN_MEM_READ)
            : !image.Exec(target))) return false;
    }
    return true;
}

// One .pdata discovery pass. Full linked validation follows only after unique
// anchors are established, so a second candidate cannot hide behind bad links.
ScanStatus FindMountedFunctions(const Image& image, std::array<std::size_t, 4>& sites) {
    std::array<unsigned, 4> counts{};
    for (std::size_t i = 0; i < image.exceptions.Size; i += sizeof(RUNTIME_FUNCTION)) {
        RUNTIME_FUNCTION entry{};
        if (!image.Read(image.exceptions.VirtualAddress + i, entry)) return ScanStatus::InvalidImage;
        if (entry.EndAddress <= entry.BeginAddress) continue;
        const auto size = entry.EndAddress - entry.BeginAddress;
        std::size_t slot = sites.size();
        if (size == 0xC8D && image.Match(entry.BeginAddress, kMountRootPrefixAnchor)) slot = 0;
        else if (size == 0xB0 && image.Match(entry.BeginAddress, kMountRideAnchor)) slot = 1;
        else if (size == 0x1069 && image.Match(entry.BeginAddress, kMountBindingHeadAnchor)) slot = 2;
        else if (size == 0x193 && image.Match(entry.BeginAddress, kMountQueryAnchor)) slot = 3;
        if (slot == sites.size()) continue;
        if (!image.Exec(entry.BeginAddress, size)) return ScanStatus::InvalidContract;
        if (++counts[slot] != 1) return ScanStatus::Ambiguous;
        sites[slot] = entry.BeginAddress;
    }
    for (const auto count : counts) if (count != 1) return ScanStatus::NotFound;
    return ScanStatus::Ready;
}

// This small static-initializer leaf has no .pdata record. Search executable
// bytes once, qualified by the *linked* selector token, then verify its name.
ScanStatus CheckMountedSelector(const Image& image, std::size_t token) {
    constexpr char name[] = "selector-horse-sp-status-gauge";
    std::size_t registration = 0;
    for (std::size_t i = 0; i < image.section_count; ++i) {
        const auto& section = image.sections[i];
        if (!image.Exec(section.start, section.size) || section.size < kMountSelectorReg.size) continue;
        std::size_t cursor = section.start;
        const auto last = section.start + section.size - kMountSelectorReg.size;
        while (cursor <= last) {
            const auto* found = static_cast<const std::uint8_t*>(std::memchr(
                image.data + cursor, kMountSelectorReg.bytes[0], last - cursor + 1));
            if (!found) break;
            cursor = static_cast<std::size_t>(found - image.data);
            if (image.Match(cursor, kMountSelectorReg) &&
                image.Relative(cursor + 0x13, 3, 7) == token) {
                if (registration) return ScanStatus::Ambiguous;
                registration = cursor;
            }
            ++cursor;
        }
    }
    if (!registration) return ScanStatus::NotFound;
    const auto literal = image.Relative(registration + 6, 3, 7);
    const auto initializer = image.Branch(registration + 0x1A, 0xE9);
    Function function{};
    return MountedReferences(image, registration, kMountSelectorRegRefs) &&
        literal && image.Section(literal, sizeof(name), IMAGE_SCN_MEM_READ) &&
        std::memcmp(image.data + literal, name, sizeof(name)) == 0 &&
        image.FunctionAt(initializer, function) && function.begin == initializer
        ? ScanStatus::Ready : ScanStatus::InvalidContract;
}

bool CheckMountedQuery(const Image& image, std::size_t query, std::size_t gauge,
                       std::size_t& slot, std::size_t& evaluator) {
    if (!MountedFunction(image, query, kMountQuery.size) || !image.Match(query, kMountQuery) ||
        !MountedReferences(image, query, kMountQueryRefs)) return false;
    slot = image.Branch(query + 0x162);
    evaluator = image.Branch(query + 0x175);
    const auto valid = image.Branch(query + 0x147);
    if (!MountedFunction(image, slot, kMountSlot.size) || !image.Match(slot, kMountSlot) ||
        !MountedReferences(image, slot, kMountSlotRefs) ||
        !MountedFunction(image, valid, kMountValidSelector.size) ||
        !image.Match(valid, kMountValidSelector) ||
        !MountedReferences(image, valid, kMountValidSelectorRefs) ||
        !MountedFunction(image, evaluator, 0x6A) || !image.Match(evaluator, kEvaluatorPrefix)) return false;
    const auto selector = image.Branch(slot + 0x44);
    const auto actor_profile = image.Branch(slot + 0x1C);
    const auto status_profile = image.Branch(slot + 0x32);
    if (!MountedFunction(image, selector, 0x128) ||
        !MountedFunction(image, actor_profile, 0x128) ||
        !MountedFunction(image, status_profile, 0x128) ||
        selector == actor_profile || selector == status_profile || actor_profile == status_profile) return false;
    for (const std::size_t call : {0x2DU, 0x46U, 0x98U, 0xDDU})
        if (image.Branch(query + call) != selector) return false;
    for (const std::size_t call : {0x5CU, 0xB5U})
        if (image.Branch(query + call) != actor_profile) return false;
    for (const std::size_t call : {0x72U, 0x86U, 0xCBU})
        if (image.Branch(query + call) != status_profile) return false;
    return image.Branch(valid + 0x1A) == selector && image.Branch(valid + 0x64) == selector &&
        image.Branch(valid + 0x2C) == actor_profile &&
        image.Branch(valid + 0x42) == status_profile && image.Branch(valid + 0x52) == status_profile &&
        image.Branch(gauge + 0x126) == query &&
        image.Relative(query + 0x10E, 3, 7) == image.Relative(gauge + 0x8B, 3, 7) &&
        image.Relative(query + 0x119, 4, 8) == image.Relative(gauge + 0x96, 4, 8);
}

}  // namespace

ScanResult ScanContracts(const void* pointer, std::size_t length, std::uintptr_t native_cost_site) {
    v3446::Image current{};
    const bool native_image = current.Init(pointer,length);
    if (native_image && psm::v3446::HasCurrentResourceLayout(current)) {
        const auto base=reinterpret_cast<std::uintptr_t>(pointer);
        const auto visibility=current.Find(v3446::kVisibility), gauge=current.Find(v3446::kGauge);
        if(!visibility) return Fail(ScanStatus::InvalidContract,ScanStage::Visibility);
        if(!gauge || current.Branch(visibility+0x73)!=gauge) return Fail(ScanStatus::InvalidContract,ScanStage::Gauge);
        const auto cost=current.Find(v3446::kCost),writer=current.Find(v3446::kWriter);
        const auto reader=menu_reader::Find(current);
        if(native_cost_site<base||!v3446::ResourceGroup(current,reader+0x9B,native_cost_site-base,reader))
            return Fail(ScanStatus::InvalidContract,ScanStage::CostCommit);
        ScanResult r{};r.status=ScanStatus::Ready;r.stage=ScanStage::None;
        r.visibility_after_call=base+visibility+0x12C;r.gauge_before_current_load=base+gauge+0xAF9;
        r.cost_after_commit=base+cost+0x9A;r.gauge_update=base+gauge;r.commit_target=base+writer;
        r.gauge_actor_rbp=false;return r;
    }
    if (native_image && native_cost_site >= reinterpret_cast<std::uintptr_t>(pointer)) {
        const auto cost = current.Find(layout3732::kCost);
        if (cost) {
            const auto base = reinterpret_cast<std::uintptr_t>(pointer);
            const auto visibility = current.Find(layout3732::kVisibility);
            if (!visibility) return Fail(ScanStatus::NotFound, ScanStage::Visibility);
            const auto gauge = current.Find(layout3732::kGauge);
            if (!gauge || current.Branch(visibility + 0x73) != gauge)
                return Fail(ScanStatus::InvalidContract, ScanStage::Gauge);
            const auto writer = current.Find(layout3732::kWriter);
            const auto metadata = current.Find(layout3732::kMetadata);
            const auto reader = current.Find(psm::oxygen::layout3732::kReader);
            if (!writer || !metadata || !reader || native_cost_site - base != cost + 0x3B ||
                current.Branch(cost + 0x36) != reader || current.Branch(cost + 0x9F) != writer ||
                current.Branch(cost + 0xA7) != metadata || current.Branch(cost + 0xC0) != metadata ||
                current.Branch(writer + 0x95) != reader || current.Branch(writer + 0xE3) != reader)
                return Fail(ScanStatus::InvalidContract, ScanStage::CostCommit);
            ScanResult result{};
            result.status = ScanStatus::Ready;
            result.stage = ScanStage::None;
            result.visibility_after_call = base + visibility + 0x12C;
            result.gauge_before_current_load = base + gauge + 0xAF9;
            result.cost_after_commit = base + cost + 0xA4;
            result.gauge_update = base + gauge;
            result.commit_target = base + writer;
            result.gauge_actor_rbp = false;
            return result;
        }
    }
    Image image{};
    if (!image.Init(pointer, length)) return Fail(ScanStatus::InvalidImage, ScanStage::Image);
    std::size_t visibility = 0, gauge = 0, tail = 0, cost = 0;
    auto status = image.Find(kVisibility, visibility);
    if (status != ScanStatus::Ready) return Fail(status, ScanStage::Visibility);
    status = image.Find(kGaugePrefix, gauge);
    if (status != ScanStatus::Ready) return Fail(status, ScanStage::Gauge);
    status = image.Find(kGaugeTail, tail);
    if (status != ScanStatus::Ready) return Fail(status, ScanStage::Gauge);
    if (!CheckVisibilityLinks(image, visibility, gauge)) return Fail(ScanStatus::InvalidContract, ScanStage::Visibility);
    if (!CheckGaugeLinks(image, gauge, tail)) return Fail(ScanStatus::InvalidContract, ScanStage::Gauge);
    const auto base = reinterpret_cast<std::uintptr_t>(pointer);
    if (native_cost_site < base || native_cost_site - base >= image.size)
        return Fail(ScanStatus::InvalidContract, ScanStage::CostCommit);
    // Count both supported forms before checking links. A second candidate
    // cannot hide behind another version or deliberately broken call targets.
    std::size_t cost_2944 = 0;
    const auto old_status = image.Find(kCost, cost);
    const auto current_status = image.Find(kCost2944, cost_2944);
    if (old_status == ScanStatus::Ambiguous || current_status == ScanStatus::Ambiguous ||
        (old_status == ScanStatus::Ready && current_status == ScanStatus::Ready))
        return Fail(ScanStatus::Ambiguous, ScanStage::CostCommit);
    const bool current_form = current_status == ScanStatus::Ready;
    if (!current_form && old_status != ScanStatus::Ready)
        return Fail(ScanStatus::NotFound, ScanStage::CostCommit);
    if (current_form) cost = cost_2944;
    const std::size_t commit_offset = current_form ? 0x53 : 0x59;
    const std::size_t helper_offset = current_form ? 0x5B : 0x61;
    const std::size_t cost_length = current_form ? kCost2944.size : kCost.size;
    Function cost_function{};
    const auto evaluator = image.Follow(image.Branch(cost - 5));
    if (cost != native_cost_site - base || !image.FunctionAt(cost, cost_function) ||
        cost_function.begin + kCostPrefix.size != cost ||
        !(current_form ? image.Match(cost_function.begin, kCostPrefix2944)
                       : image.Match(cost_function.begin, kCostPrefix)) ||
        cost + cost_length > cost_function.end || !image.Match(evaluator, kEvaluatorPrefix) ||
        !image.Branch(cost + helper_offset) || image.Branch(evaluator + 0x21) != image.Branch(cost + helper_offset))
        return Fail(ScanStatus::InvalidContract, ScanStage::CostCommit);
    const auto writer = image.Follow(image.Branch(cost + commit_offset));
    const auto writer_tail = image.Relative(writer + (current_form ? 0x48 : 0x53), 2, 6);
    Function writer_function{}, writer_tail_function{};
    if (!writer || !image.Match(writer, kWriterPrefix) ||
        !image.FunctionAt(writer, writer_function) || writer_function.begin != writer ||
        !(current_form ? image.Match(writer + 0x43, kWriterBridge2944)
                       : image.Match(writer + 0x43, kWriterBridge)) ||
        !image.Match(writer_tail, kWriterTail) ||
        !image.FunctionAt(writer_tail, writer_tail_function) || writer_tail_function.begin != writer_tail ||
        writer_tail_function.end != writer_tail + kWriterTail.size)
        return Fail(ScanStatus::InvalidContract, ScanStage::CostCommit);
    ScanResult result{};
    result.status = ScanStatus::Ready;
    result.stage = ScanStage::None;
    result.visibility_after_call = base + visibility + 0x12C;
    result.gauge_before_current_load = base + tail + 0x3E;
    result.cost_after_commit = base + cost + commit_offset + 5;
    result.gauge_update = base + gauge;
    result.commit_target = base + writer;
    return result;
}

bool ValidateHudModifierListContract(const void* pointer, std::size_t length) {
    v3446::Image current{};
    const bool native_image = current.Init(pointer,length);
    if(native_image && psm::v3446::HasCurrentResourceLayout(current)){
        const auto m=current.Find(v3446::kMutator),c=current.Find(v3446::kContribution);
        const auto a=current.Find(v3446::kAb00),callers=current.Find(v3446::kCallers);
        return m&&c&&a&&callers&&current.Follow(current.Branch(m+0x6C))==c&&
            current.Follow(current.Branch(m+0x2DE))==c&&
            current.Branch(callers+0x283)==m&&current.Branch(callers+0x2A3)==a&&
            current.Branch(callers+0x3DC)==m&&current.Branch(callers+0x400)==a;
    }
    if (native_image) {
        const auto mutator = current.Find(layout3732::kMutator);
        if (mutator) {
            const auto contribution = current.Find(layout3732::kContribution);
            const auto ab00 = current.Find(layout3732::kAb00);
            const auto callers = current.Find(layout3732::kCallers);
            return contribution && ab00 && callers &&
                current.Follow(current.Branch(mutator + 0x6C)) == contribution &&
                current.Follow(current.Branch(mutator + 0x2DE)) == contribution &&
                current.Branch(callers + 0x283) == mutator && current.Branch(callers + 0x2A3) == ab00 &&
                current.Branch(callers + 0x3DC) == mutator && current.Branch(callers + 0x400) == ab00;
        }
    }
    Image image{};
    if (!image.Init(pointer, length)) return false;
    std::size_t mutator = 0, contribution = 0, ab00 = 0, caller_a = 0, caller_b = 0;
    const auto find_profile = [&](const auto& legacy, const auto& current,
                                  std::size_t& site, bool& current_form) {
        std::size_t current_site = 0;
        const auto old_status = image.Find(legacy, site);
        const auto current_status = image.Find(current, current_site);
        if (old_status == ScanStatus::Ambiguous || current_status == ScanStatus::Ambiguous ||
            (old_status == ScanStatus::Ready && current_status == ScanStatus::Ready)) return false;
        current_form = current_status == ScanStatus::Ready;
        if (current_form) site = current_site;
        return current_form || old_status == ScanStatus::Ready;
    };
    bool current_mutator = false, current_contribution = false;
    if (!find_profile(kModifierMutator, kModifierMutator2944, mutator, current_mutator) ||
        !find_profile(kContribution, kContribution2944, contribution, current_contribution) ||
        current_mutator != current_contribution ||
        image.Find(kAb00, ab00) != ScanStatus::Ready ||
        image.Find(kModifierCallerA, caller_a) != ScanStatus::Ready ||
        image.Find(kModifierCallerB, caller_b) != ScanStatus::Ready) return false;
    Function mutation_function{}, contribution_function{}, ab00_function{}, a{}, b{};
    if (!image.FunctionAt(mutator, mutation_function) || mutation_function.begin != mutator ||
        mutation_function.end != mutator + (current_mutator ? kModifierMutator2944.size : kModifierMutator.size) ||
        !image.FunctionAt(contribution, contribution_function) || contribution_function.begin != contribution ||
        contribution_function.end != contribution + (current_contribution ? kContribution2944.size : kContribution.size) ||
        !image.FunctionAt(ab00, ab00_function) || ab00_function.begin != ab00 ||
        ab00_function.end != ab00 + kAb00.size ||
        !image.FunctionAt(caller_a, a) || !image.FunctionAt(caller_b, b) ||
        a.begin != b.begin || a.end != b.end || caller_a + kModifierCallerA.size > a.end ||
        caller_b + kModifierCallerB.size > b.end) return false;
    if (image.Follow(image.Branch(caller_a + 0x10)) != mutator ||
        image.Follow(image.Branch(caller_b + 0x10)) != mutator ||
        image.Branch(caller_a + 0x30) != ab00 || image.Branch(caller_b + 0x30) != ab00 ||
        image.Follow(image.Branch(mutator + 0x6C)) != contribution ||
        image.Follow(image.Branch(mutator + (current_mutator ? 0x2CE : 0x2DE))) != contribution ||
        !image.Match(image.Follow(image.Branch(contribution + 0x63)), kArithmeticPrefix)) return false;
    for (const std::size_t call : {0x26U, 0x39U, 0x72U, 0x88U, 0xA1U, 0xB8U})
        if (!image.Branch(ab00 + call)) return false;
    return true;
}

MountedResourceScanResult ScanMountedResourceContracts(const void* pointer, std::size_t length) {
    v3446::Image current{};
    if(current.Init(pointer,length)&&psm::v3446::HasCurrentResourceLayout(current)){
        const auto root=current.Find(v3446::kRoot),ride=current.Find(v3446::kRide),binding=current.Find(v3446::kBinding);
        if(!root||!ride||!binding)return MountedFail(ScanStatus::InvalidContract,MountedScanStage::Root);
        const auto player=current.Find(v3446::kPlayer),actor=current.Find(v3446::kPlayerActor),resolve=current.Find(v3446::kResolve);
        const auto horse=current.Find(v3446::kHorse),gauge=current.Find(v3446::kGauge);
        if(!player||!actor||!resolve||!horse||!gauge||current.Branch(root+0x61)!=player||
           current.Branch(ride+0x1D)!=player||current.Branch(player+0x20)!=actor||
           current.Branch(root+0x223)!=resolve||current.Branch(root+0xBF3)!=horse||
           current.Branch(root+0x2F3)!=gauge||current.Branch(horse+0x78)!=gauge)
            return MountedFail(ScanStatus::InvalidContract,MountedScanStage::Binding);
        const auto query=current.Find(v3446::kQuery),slot=current.Find(v3446::kSlot),valid=current.Find(v3446::kValid);
        const auto evaluator=menu_reader::Find(current),metadata=current.Find(v3446::kMetadata);
        if(!query||!slot||!valid||!evaluator||!metadata||current.Branch(gauge+0x123)!=query||
           current.Branch(query+0x144)!=valid||current.Branch(query+0x15F)!=slot||
           current.Branch(query+0x172)!=evaluator||current.Branch(slot+0x44)!=metadata||
           current.Branch(valid+0x1A)!=metadata)
            return MountedFail(ScanStatus::InvalidContract,MountedScanStage::Query);
        // Exact initialization body proves horse-selector registration, output
        // gauge+1B8, role guards and links; full runtime identity remains required.
        const auto base=reinterpret_cast<std::uintptr_t>(pointer);
        MountedResourceScanResult r{};r.status=ScanStatus::Ready;r.stage=MountedScanStage::None;
        r.mount_actor_capture=base+root+0x239;r.query_after_lookup=base+query+0x164;
        r.root_update=base+root;r.ride_refresh=base+ride;r.gauge_update=base+gauge;
        r.current_query=base+query;r.entry_lookup=base+slot;r.current_evaluator=base+evaluator;
        r.current_3446=true;return r;
    }
    Image image{};
    if (!image.Init(pointer, length)) return MountedFail(ScanStatus::InvalidImage, MountedScanStage::Image);
    std::array<std::size_t, 4> sites{};
    auto status = FindMountedFunctions(image, sites);
    if (status != ScanStatus::Ready) return MountedFail(status, MountedScanStage::Root);
    const auto root = sites[0], ride = sites[1], binding = sites[2], query = sites[3];

    if (!MountedFunction(image, root, 0xC8D) || !image.Match(root, kMountRootPrefix) ||
        !MountedReferences(image, root, kMountRootPrefixRefs) ||
        !image.Match(root + 0x240, kMountRootRouting) ||
        !MountedReferences(image, root + 0x240, kMountRootRoutingRefs) ||
        !image.Match(root + 0x28F, kMountRootGauge) ||
        !MountedReferences(image, root + 0x28F, kMountRootGaugeRefs) ||
        image.Branch(root + 0x2BC, 0xE9) != root + 0x559 ||
        !image.Match(root + 0x91D, kMountRootHorseCall))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Root);
    const auto player = image.Branch(root + 0x55);
    const auto player_actor = image.Branch(player + 0x20);
    const auto resolve = image.Branch(root + 0x1F3);
    if (!MountedFunction(image, player, kMountPlayer.size) || !image.Match(player, kMountPlayer) ||
        !MountedReferences(image, player, kMountPlayerRefs) ||
        !MountedFunction(image, player_actor, kMountPlayerActor.size) ||
        !image.Match(player_actor, kMountPlayerActor) ||
        !MountedReferences(image, player_actor, kMountPlayerActorRefs) ||
        !MountedFunction(image, resolve, kMountActorResolve.size) ||
        !image.Match(resolve, kMountActorResolve) ||
        !MountedReferences(image, resolve, kMountActorResolveRefs) ||
        !MountedFunction(image, image.Branch(resolve + 0x11C), 0x7D) ||
        image.Relative(root + 0x32, 3, 7) != image.Relative(root + 0x1E9, 3, 7))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Root);

    // Missing active player leaves root+0x360 unchanged; the exact writer
    // pattern preserves this guard and both null-to-zero ride-chain branches.
    if (!MountedFunction(image, ride, kMountRide.size) || !image.Match(ride, kMountRide) ||
        !MountedReferences(image, ride, kMountRideRefs) || image.Branch(ride + 0x1D) != player ||
        image.Branch(ride + 0x42) != image.Branch(root + 0x8D) ||
        image.Relative(ride + 0x8A, 3, 7) != image.Relative(player_actor + 0x2C, 3, 7) ||
        image.Relative(resolve + 0x2B, 3, 7) != image.Relative(player_actor + 0x2C, 3, 7))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Ride);

    const auto horse = image.Branch(root + 0x92A);
    const auto gauge = image.Branch(root + 0x2AF);
    const auto tail = image.Branch(gauge + 0x1C9, 0xE9);
    if (!MountedFunction(image, horse, 0x1A9) || !image.Match(horse, kMountHorsePrefix) ||
        !MountedReferences(image, horse, kMountHorsePrefixRefs) ||
        image.Branch(horse + 0x78) != gauge ||
        !image.Match(gauge, kGaugePrefix) || !image.Match(tail, kGaugeTail) ||
        !CheckGaugeLinks(image, gauge, tail))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Binding);
    const auto bind = binding + 0x7DB;
    if (!MountedFunction(image, binding, 0x1069) || !image.Match(binding, kMountBindingHead) ||
        !image.Match(bind, kMountBinding) || !MountedReferences(image, bind, kMountBindingRefs) ||
        !MountedFunction(image, image.Branch(bind + 0x18), 0x1CF))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Binding);

    std::size_t slot = 0, evaluator = 0;
    if (!CheckMountedQuery(image, query, gauge, slot, evaluator))
        return MountedFail(ScanStatus::InvalidContract, MountedScanStage::Query);
    // Leave the single executable-byte search until all linked code has passed.
    status = CheckMountedSelector(image, image.Relative(bind, 3, 7));
    if (status != ScanStatus::Ready) return MountedFail(status, MountedScanStage::Binding);

    const auto base = reinterpret_cast<std::uintptr_t>(pointer);
    MountedResourceScanResult result{};
    result.status = ScanStatus::Ready;
    result.stage = MountedScanStage::None;
    result.mount_actor_capture = base + root + 0x207;
    result.query_after_lookup = base + query + 0x167;
    result.root_update = base + root;
    result.ride_refresh = base + ride;
    result.gauge_update = base + gauge;
    result.current_query = base + query;
    result.entry_lookup = base + slot;
    result.current_evaluator = base + evaluator;
    return result;
}

}  // namespace psm::hud
