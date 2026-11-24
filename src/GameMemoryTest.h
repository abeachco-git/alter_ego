#pragma once
// TEMPORARY: remove this header and the marked tab/include/session hook in
// dllmain.cpp together. No persistence, networking, or hooks. One explicitly confirmed game call.
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
    DWORD imageTimestamp = 0;
    Address swapContextSlot = 0;
    bool allowDifferentThread = false;
    unsigned viewState = 0;
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
    s.imageTimestamp = nt.FileHeader.TimeDateStamp;
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
    // Only enable the call on the exact inspected PE build, with both
    // independently resolved call sites agreeing and the getter verified.
    if (s.imageTimestamp==0x6afcb045u && s.imageSize==0x2c52000u && w.size()==2) {
        Address getter=Rip(code,bytes,w[0],1,5);
        bool agree=getter==s.base+0x94aec0 && InImage(s,getter,8);
        for (auto hit:w)
            agree=agree && Rip(code,bytes,hit,1,5)==getter &&
                hit+24<=bytes.size() && bytes[hit+23]==0x60;
        std::array<unsigned char,8> g{};
        if (agree && ReadBytes(getter,g.data(),g.size()) &&
            g[0]==0x48 && g[1]==0x8b && g[2]==0x05 && g[7]==0xc3) {
            Address slot=getter+7+static_cast<int64_t>(Field<int32_t>(g.data(),3));
            if (slot==s.base+0x26888b8 && InImage(s,slot,sizeof(Address)))
                s.swapContextSlot=slot;
        }
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
// Do not use a long-lived game pointer or invoke the context getter.
// The inspected getter is a single RIP-relative load; the view-state
// vtable method is a single integer load. Revalidate both before the call.
struct SwapPlan {
    Address context=0, entry=0;
    std::array<wchar_t,21> name{};
};
inline bool Executable(Address p) {
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(reinterpret_cast<LPCVOID>(p),&m,sizeof(m)) ||
        m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD protection=m.Protect&0xff;
    return protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
inline bool PrepareSwap(State& s,const std::string& active,const std::string& target,
                        SwapPlan& plan) {
    s.viewState=0;
    if (!s.swapContextSlot) {
        s.preflight="Blocked: swap resolver does not match the inspected game build.";
        return false;
    }
    if (!Sample(s)) {s.preflight="Blocked: fresh roster validation failed.";return false;}
    const auto present=[&](const std::string& name){
        return std::any_of(s.rows.begin(),s.rows.end(),[&](const Row& row){return row.name==name;});
    };
    if (active.empty() || target.empty() || target==active || !present(active) || !present(target)) {
        s.preflight="Blocked: active or target character is not valid in the fresh roster.";return false;
    }
    if (!s.gameThread) {s.preflight="Blocked: game main thread identity is unknown.";return false;}
    Address ctx=0,vt=0,entry=0,view=0;
    constexpr unsigned char viewCode[]={0x8b,0x41,0x6c,0xc3};
    constexpr unsigned char swapCode[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
        0xec,0x20,0x41,0x8b,0xd8,0x48,0x8b,0xfa,0x45,0x33,0xc0,0x33,0xd2,
        0xb9,0x07,0x00,0x00,0x10,0xe8,0xc1,0x87,0x72,0x00,0x85};
    std::array<unsigned char,sizeof(viewCode)> vBytes{};
    std::array<unsigned char,sizeof(swapCode)> eBytes{};
    if (!Read(s.swapContextSlot,ctx) || !ctx || ctx>UINTPTR_MAX-0x70 ||
        !Read(ctx,vt) || vt!=s.base+0x1b7fb18 ||
        !InImage(s,vt,0x118) || !Read(vt+0x60,entry) ||
        entry!=s.base+0x94b840 || !Read(vt+0x110,view) ||
        view!=s.base+0x4e6470 || !Executable(entry) ||
        !ReadBytes(entry,eBytes.data(),eBytes.size()) ||
        std::memcmp(eBytes.data(),swapCode,sizeof(swapCode))!=0 ||
        !ReadBytes(view,vBytes.data(),vBytes.size()) ||
        std::memcmp(vBytes.data(),viewCode,sizeof(viewCode))!=0 ||
        !Read(ctx+0x6c,s.viewState)) {
        s.preflight="Blocked: live context, vtable or function bytes do not match the inspected build.";return false;
    }
    if (s.viewState!=16) {
        s.preflight="Blocked: game view state is not gameplay (16).";return false;
    }
    if (s.gameThread!=GetCurrentThreadId() && !s.allowDifferentThread) {
        s.preflight="Context and gameplay verified. Render-thread mismatch: experimental opt-in required.";return false;
    }
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,target.c_str(),-1,
                                   plan.name.data(),static_cast<int>(plan.name.size()));
    if (n<2 || n>20) {
        s.preflight="Blocked: invalid UTF-16 character name.";return false;
    }
    // Recheck the live singleton and state immediately before handing it off.
    Address latest=0,latestVt=0,latestEntry=0;
    unsigned latestView=0;
    DWORD latestThread=0;
    if (!Read(s.swapContextSlot,latest) || latest!=ctx ||
        !Read(ctx,latestVt) || latestVt!=vt ||
        !Read(vt+0x60,latestEntry) || latestEntry!=entry ||
        !Read(ctx+0x6c,latestView) || latestView!=16 ||
        !Read(s.threadSlot,latestThread) || latestThread!=s.gameThread) {
        s.preflight="Blocked: game context changed during preflight.";return false;
    }
    plan.context=ctx;plan.entry=entry;
    s.preflight=s.gameThread==GetCurrentThreadId()
        ?"Preflight passed on main thread; no game call made."
        :"Preflight passed with explicit off-main-thread opt-in; thread safety remains unknown.";
    return true;
}
struct Attempt {
    bool issued=false;
    std::string target;
};
inline Attempt& TestAttempt() {static Attempt a;return a;}
inline void Render(const std::string& active) {
    auto& s=Data();
    auto& attempt=TestAttempt();
    ImGui::TextUnformatted("Temporary game memory diagnostics");
    ImGui::TextWrapped("Scanning is read-only. The optional test switch invokes an undocumented game function. It may crash the game, reset position, or violate game policies. No data is saved.");
    if (attempt.issued) {
        ImGui::TextWrapped("One test issued this addon load for %s. %s",attempt.target.c_str(),
            active==attempt.target?"MumbleLink confirms target active.":"Waiting for target identity; no retry.");
    }
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
    ImGui::Text("Swap resolver: %s | gameplay state: %u",s.swapContextSlot?"matched inspected build":"unavailable",s.viewState);
    ImGui::TextDisabled("Map IDs and raw attribute candidates.");
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
        ImGui::Checkbox("Allow experimental switch from non-main thread", &s.allowDifferentThread);
        if (UI::Button("Test swap preflight (no switch)")) {
            const auto target=s.selected;
            try {SwapPlan plan;PrepareSwap(s,active,target,plan);}
            catch (...) {s.rows.clear();s.preflight="Blocked: validation failed.";}
        }
        if (!attempt.issued && s.swapContextSlot && s.selected!=active &&
            UI::Button("Test switch...")) {
            ImGui::OpenPopup("Confirm experimental character switch");
        }
        if (ImGui::BeginPopupModal("Confirm experimental character switch",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Switch to %s? This is an unsupported, one-shot test. It can crash the game or reset your position.",s.selected.c_str());
            if (s.gameThread!=GetCurrentThreadId())
                ImGui::TextWrapped("Render thread is not the main thread. Explicit opt-in is required.");
            if (UI::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (UI::Button("Execute test switch") && !attempt.issued) {
                ImGui::CloseCurrentPopup();
                const auto target=s.selected;
                SwapPlan plan;
                bool ready=false;
                try {ready=PrepareSwap(s,active,target,plan);}
                catch (...) {s.rows.clear();s.preflight="Blocked: validation failed.";}
                if (ready) {
                    // The inspected game method synchronously copies a UTF-16
                    // character name into an owned request. No memory patch,
                    // injected thread, hook, or automated retry.
                    attempt.target=target;
                    attempt.issued=true;
                    s.preflight="Test invoked. Awaiting MumbleLink identity; no automatic retry.";
                    using SwitchFn=void(*)(void*,const wchar_t*,uint32_t);
                    reinterpret_cast<SwitchFn>(plan.entry)(
                        reinterpret_cast<void*>(plan.context),plan.name.data(),0);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::TextWrapped("%s",s.preflight.c_str());
    }
}
} // namespace AlterEgo::GameMemoryTest
