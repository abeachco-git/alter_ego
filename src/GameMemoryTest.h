#pragma once
// TEMPORARY: remove this header and the marked tab/include/session hook in
// dllmain.cpp together. No persistence, networking, hooks, or game calls.
// Observations from supplied Fast Swap DLL and GW2 executable are hypotheses
// until checked against live data. Do not use this module in production.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "UIControls.h"

namespace AlterEgo::GameMemoryTest {
using Address = uintptr_t;
constexpr size_t Stride = 0x258;
constexpr uint32_t MaxRows = 128;
inline bool ReadBytes(Address p, void* out, size_t n) {
    if (!p || !n || p > UINTPTR_MAX - n) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(p),
                             out, n, &got) && got == n;
}
template<class T> inline bool Read(Address p, T& out) {
    return ReadBytes(p, &out, sizeof(out));
}
template<class T> inline T Field(const unsigned char* p, size_t offset) {
    T value{}; std::memcpy(&value, p + offset, sizeof(value)); return value;
}
struct Row {
    std::string name;
    uint16_t map = 0;
    unsigned level = 0, profession = 0, raceCandidate = 0,
             genderCandidate = 0, specCandidate = 0;
};
struct State {
    Address base = 0, ptrSlot = 0, countSlot = 0, threadSlot = 0;
    size_t imageSize = 0;
    unsigned rosterHits = 0, threadHits = 0, switchHits = 0;
    DWORD gameThread = 0, sampleThread = 0;
    uint32_t count8 = 0, count12 = 0;
    std::vector<Row> rows;
    std::string status = "Click Scan / Refresh while in gameplay.";
    std::string selected, preflight;
};
inline State& Data() { static State state; return state; }
inline bool InImage(const State& s, Address p, size_t n) {
    return p >= s.base && n <= s.imageSize && p - s.base <= s.imageSize - n;
}
inline bool ImageText(State& s, Address& code, std::vector<unsigned char>& bytes) {
    s.base = reinterpret_cast<Address>(GetModuleHandleW(L"Gw2-64.exe"));
    IMAGE_DOS_HEADER dos{};
    if (!Read(s.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) return false;
    IMAGE_NT_HEADERS64 nt{};
    Address ntAddr = s.base + static_cast<unsigned>(dos.e_lfanew);
    if (!Read(ntAddr, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.NumberOfSections > 96) return false;
    s.imageSize = nt.OptionalHeader.SizeOfImage;
    if (!s.imageSize || s.imageSize > 512u * 1024u * 1024u) return false;
    Address sections = ntAddr + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
                       + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER sh{};
        Address p = sections + i * sizeof(sh);
        if (!InImage(s,p,sizeof(sh)) || !Read(p,sh)) return false;
        if (std::memcmp(sh.Name, ".text", 5) != 0 ||
            !(sh.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        size_t n = sh.Misc.VirtualSize;
        code = s.base + sh.VirtualAddress;
        if (!n || n > 64u * 1024u * 1024u || !InImage(s,code,n)) return false;
        bytes.resize(n);
        return ReadBytes(code, bytes.data(), n);
    }
    return false;
}
template<size_t N> inline std::vector<size_t> Matches(
        const std::vector<unsigned char>& bytes, const int (&pattern)[N]) {
    std::vector<size_t> result;
    for (size_t i = 0; i + N <= bytes.size(); ++i) {
        if (bytes[i] != pattern[0]) continue;
        size_t j = 1;
        for (; j < N; ++j)
            if (pattern[j] >= 0 && bytes[i+j] != pattern[j]) break;
        if (j == N) {
            result.push_back(i);
            if (result.size() >= 128) break; // ambiguous; bounded
        }
    }
    return result;
}
inline Address Rip(Address code, const std::vector<unsigned char>& bytes,
                   size_t start, size_t disp, size_t end) {
    return code + start + end + static_cast<int64_t>(
        Field<int32_t>(bytes.data()+start, disp));
}
inline bool Resolve(State& s) {
    Address code = 0;
    std::vector<unsigned char> bytes;
    if (!ImageText(s,code,bytes)) {
        s.status = "Unable to read the executable code section."; return false;
    }
    // Tightened roster signature includes the verified record stride.
    constexpr int roster[] = {0x48,0x8B,0x1D,-1,-1,-1,-1,0x8B,0x05,-1,-1,-1,-1,
                              0x48,0x69,0xF0,0x58,0x02,0x00,0x00,0x48,0x03,0xF3};
    constexpr int thread[] = {0xE8,-1,-1,-1,-1,0x8B,0x0D,-1,-1,-1,-1,0x85,0xC9,
                              0x75,0x08,0x89,0x05,-1,-1,-1,-1,0xEB,0x1D,0x3B,0xC8,0x74,0x19};
    constexpr int swap[] = {0xE8,-1,-1,-1,-1,0x44,0x8B,-1,-1,-1,-1,-1,
                            0x48,0x8B,-1,-1,-1,0x48,0x8B,0x08,0x4C,0x8B,0x49};
    auto r=Matches(bytes,roster), t=Matches(bytes,thread), w=Matches(bytes,swap);
    s.rosterHits=static_cast<unsigned>(r.size());
    s.threadHits=static_cast<unsigned>(t.size());
    s.switchHits=static_cast<unsigned>(w.size());
    if (!t.empty() && t.size()<128) {
        Address candidate=Rip(code,bytes,t[0],7,11);
        bool agree=InImage(s,candidate,sizeof(DWORD));
        for (auto hit:t) agree=agree && Rip(code,bytes,hit,7,11)==candidate;
        if (agree) s.threadSlot=candidate;
    }
    if (r.size()!=1) {
        s.status="Roster pattern missing or ambiguous; no roster read."; return false;
    }
    s.ptrSlot=Rip(code,bytes,r[0],3,7);
    s.countSlot=Rip(code,bytes,r[0],9,13);
    // Current game's own loop uses +12. Fast Swap's reader uses +8.
    // Never substitute the latter without in-game validation.
    if (!InImage(s,s.ptrSlot,16) || !InImage(s,s.countSlot,4) ||
        s.countSlot!=s.ptrSlot+12) {
        s.ptrSlot=s.countSlot=0;
        s.status="Roster header layout differs from inspected game build."; return false;
    }
    return true;
}
inline bool Header(const State& s, Address& ptr, uint32_t& a, uint32_t& b) {
    std::array<unsigned char,16> raw{};
    if (!s.ptrSlot || !ReadBytes(s.ptrSlot,raw.data(),raw.size())) return false;
    ptr=Field<Address>(raw.data(),0);
    a=Field<uint32_t>(raw.data(),8);
    b=Field<uint32_t>(raw.data(),12);
    return true;
}
inline bool ParseRow(const unsigned char* p, Row& row) {
    wchar_t name[21]{};
    std::memcpy(name,p+0x18,40);
    size_t len=0;
    while (len<20 && name[len]) ++len;
    if (!len) return false;
    for (size_t i=0;i<len;++i) if (name[i]<32) return false;
    char utf8[81]{};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,
                             static_cast<int>(len),utf8,sizeof(utf8),nullptr,nullptr);
    if (n<=0) return false;
    row.name.assign(utf8,static_cast<size_t>(n));
    row.map=Field<uint16_t>(p,0x41);
    row.level=p[0x44]; row.profession=p[0x45];
    row.raceCandidate=p[0x61]; row.genderCandidate=p[0x63]; row.specCandidate=p[0xD9];
    return row.level>=1 && row.level<=80 && row.profession>=1 && row.profession<=9;
}
inline bool Sample(State& s) {
    s.rows.clear(); s.preflight.clear();
    s.sampleThread=GetCurrentThreadId(); s.gameThread=0;
    if (s.threadSlot) Read(s.threadSlot,s.gameThread);
    Address ptr=0, ptr2=0;
    uint32_t a=0,b=0,a2=0,b2=0;
    if (!Header(s,ptr,a,b)) { s.status="Roster header unreadable."; return false; }
    s.count8=a; s.count12=b;
    if (!ptr || !b || b>MaxRows) {
        s.status="Roster unavailable or count outside bounds; no rows read."; return false;
    }
    std::vector<unsigned char> first(size_t(b)*Stride), second(first.size());
    if (!ReadBytes(ptr,first.data(),first.size()) ||
        !Header(s,ptr2,a2,b2) || ptr!=ptr2 || a!=a2 || b!=b2 ||
        !ReadBytes(ptr,second.data(),second.size()) ||
        !Header(s,ptr2,a2,b2) || ptr!=ptr2 || a!=a2 || b!=b2 ||
        first!=second) {
        s.status="Roster changed or became unreadable; retry outside transitions."; return false;
    }
    std::vector<Row> rows;
    for (size_t i=0;i<b;++i) {
        Row row;
        if (!ParseRow(first.data()+i*Stride,row) ||
            std::any_of(rows.begin(),rows.end(),[&](const Row& v){return v.name==row.name;})) {
            s.status="Unexpected character fields or duplicate name; snapshot rejected."; return false;
        }
        rows.push_back(std::move(row));
    }
    s.rows=std::move(rows);
    s.status="Read-only snapshot accepted. Verify values against Fast Swap.";
    return true;
}
inline void Render(const std::string& active) {
    auto& s=Data();
    ImGui::TextUnformatted("Temporary game memory diagnostics (read-only)");
    ImGui::TextWrapped("No game functions are called. No data is saved. Close this page before normal play.");
    if (UI::Button("Scan / Refresh")) {
        s=State{};
        try { if (Resolve(s)) Sample(s); }
        catch (...) { s=State{}; s.status="Scan failed; no data retained."; }
    }
    ImGui::SameLine();
    if (UI::Button("Clear")) s=State{};
    ImGui::TextWrapped("%s",s.status.c_str());
    ImGui::Text("Matches: roster %u | thread %u | switch %u",s.rosterHits,s.threadHits,s.switchHits);
    ImGui::Text("Header +8: %u | game count +12: %u | accepted rows: %u",
                s.count8,s.count12,static_cast<unsigned>(s.rows.size()));
    ImGui::Text("Game main thread: %lu | sample thread: %lu | render now: %lu",
                static_cast<unsigned long>(s.gameThread),static_cast<unsigned long>(s.sampleThread),
                static_cast<unsigned long>(GetCurrentThreadId()));
    ImGui::Text("MumbleLink character: %s",active.empty()?"Unknown":active.c_str());
    ImGui::TextDisabled("Map IDs and raw attribute candidates; meanings are not yet runtime-verified.");
    if (ImGui::BeginTable("##temporary_memory_rows",7,
            ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_Resizable)) {
        for (const char* column: {"Name","Map ID","Level","Prof ID","0x61","0x63","0xD9"})
            ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        for (const auto& row:s.rows) {
            ImGui::PushID(row.name.c_str()); ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(row.name.c_str(),s.selected==row.name,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                s.selected=row.name; s.preflight.clear();
            }
            if (row.name==active) { ImGui::SameLine(); ImGui::TextDisabled("(active)"); }
            unsigned values[]={row.map,row.level,row.profession,row.raceCandidate,row.genderCandidate,row.specCandidate};
            for (int i=0;i<6;++i) {ImGui::TableSetColumnIndex(i+1); ImGui::Text("%u",values[i]);}
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!s.selected.empty()) {
        ImGui::Text("Selected: %s",s.selected.c_str());
        if (UI::Button("Test swap preflight (no switch)")) {
            auto target=s.selected;
            try {
                if (!Sample(s)) s.preflight="Blocked: fresh roster validation failed.";
                else if (active.empty() || active==target)
                    s.preflight="Blocked: unknown active character or target is already active.";
                else if (!std::any_of(s.rows.begin(),s.rows.end(),[&](const Row& r){return r.name==target;}) ||
                         !std::any_of(s.rows.begin(),s.rows.end(),[&](const Row& r){return r.name==active;}))
                    s.preflight="Blocked: target or active character not in fresh roster.";
                else if (!s.gameThread || s.gameThread!=GetCurrentThreadId())
                    s.preflight="Blocked: render thread is not verified as game main thread.";
                else s.preflight="Roster/thread checks passed. Actual switching remains disabled (context unverified).";
            } catch (...) { s.rows.clear(); s.preflight="Blocked: validation failed."; }
        }
        ImGui::TextWrapped("%s",s.preflight.c_str());
    }
}
} // namespace AlterEgo::GameMemoryTest
