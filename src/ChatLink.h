#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <optional>
#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <utility>

namespace AlterEgo {

    // Chat link type bytes
    enum ChatLinkType : uint8_t {
        LINK_COIN          = 0x01,
        LINK_ITEM          = 0x02,
        LINK_NPC_TEXT      = 0x03,
        LINK_MAP           = 0x04,
        LINK_PVP_GAME      = 0x05,
        LINK_SKILL         = 0x06,
        LINK_TRAIT         = 0x07,
        LINK_USER          = 0x08,
        LINK_RECIPE        = 0x09,
        LINK_SKIN          = 0x0A,
        LINK_OUTFIT        = 0x0B,
        LINK_WVW_OBJECTIVE = 0x0C,
        LINK_BUILD         = 0x0D,
        LINK_ACHIEVEMENT   = 0x0E,
        LINK_WARDROBE_TMPL = 0x0F,
    };

    // Item link flags (byte after item ID)
    enum ItemLinkFlags : uint8_t {
        ITEM_FLAG_SKINNED    = 0x80,  // 1 << 7
        ITEM_FLAG_UPGRADE1   = 0x40,  // 1 << 6
        ITEM_FLAG_UPGRADE2   = 0x20,  // 1 << 5
        ITEM_FLAG_NAME_KEY   = 0x10,  // 1 << 4
        ITEM_FLAG_DESC_KEY   = 0x08,  // 1 << 3
    };

    // Profession codes for build templates
    enum ProfessionCode : uint8_t {
        PROF_GUARDIAN    = 1,
        PROF_WARRIOR     = 2,
        PROF_ENGINEER    = 3,
        PROF_RANGER      = 4,
        PROF_THIEF       = 5,
        PROF_ELEMENTALIST = 6,
        PROF_MESMER      = 7,
        PROF_NECROMANCER = 8,
        PROF_REVENANT    = 9,
    };

    // Weapon type IDs used in build template chat links
    enum WeaponTypeId : uint16_t {
        WEAPON_AXE       = 5,
        WEAPON_LONGBOW   = 35,
        WEAPON_DAGGER    = 47,
        WEAPON_FOCUS     = 49,
        WEAPON_GREATSWORD = 50,
        WEAPON_HAMMER    = 51,
        WEAPON_MACE      = 53,
        WEAPON_PISTOL    = 54,
        WEAPON_RIFLE     = 85,
        WEAPON_SCEPTER   = 86,
        WEAPON_SHIELD    = 87,
        WEAPON_STAFF     = 89,
        WEAPON_SWORD     = 90,
        WEAPON_TORCH     = 102,
        WEAPON_WARHORN   = 103,
        WEAPON_SHORTBOW  = 107,
        WEAPON_SPEAR     = 265,
    };

    // Decoded item link
    struct DecodedItemLink {
        uint32_t item_id = 0;
        int count = 1;
        uint32_t skin_id = 0;       // 0 = no skin override
        uint32_t upgrade1_id = 0;   // 0 = no upgrade
        uint32_t upgrade2_id = 0;   // 0 = no upgrade
    };

    // Decoded build template link
    struct DecodedBuildLink {
        uint8_t profession = 0;
        struct SpecChoice {
            uint8_t spec_id = 0;
            uint8_t traits[3] = {0, 0, 0}; // 0=none, 1=top, 2=mid, 3=bottom
        } specs[3];
        uint16_t terrestrial_skills[5] = {0}; // heal, util1, util2, util3, elite (palette IDs)
        uint16_t aquatic_skills[5] = {0};
        // Ranger pets
        uint8_t pets[4] = {0};      // terr1, terr2, aqua1, aqua2
        // Revenant legends
        uint8_t legends[4] = {0};   // terr1, terr2, aqua1, aqua2
        uint16_t inactive_rev_skills[6] = {0}; // inactive utility palette IDs, exact wire order
        bool active_palettes_known = false; // complete ten-value skill block decoded
        bool companion_choices_known = false; // complete four-byte pair block decoded
        bool inactive_rev_skills_known = false; // complete six-value block decoded
        // Weapons (SotO+)
        std::vector<uint16_t> weapons;
        // Skill overrides (Weaponmaster)
        std::vector<uint32_t> skill_overrides;
    };

    // Keep opaque protocol IDs rather than guessing legend-specific aliases.
    using InactiveLegendPalettes=std::array<uint16_t,6>;
    // Each environment retains its independent original exact wire context.
    // An all-256 row is unknown provenance, never a real protocol identity.
    using LegendPaletteContext=std::array<uint32_t,3>;
    using InactiveLegendContext=std::array<LegendPaletteContext,2>;
    inline constexpr LegendPaletteContext UnknownLegendPaletteContext={256,256,256};
    inline constexpr InactiveLegendContext UnknownInactiveLegendContext={UnknownLegendPaletteContext,UnknownLegendPaletteContext};
    inline bool ValidLegendPaletteContext(const LegendPaletteContext& row){
        return row==UnknownLegendPaletteContext||
            std::all_of(row.begin(),row.end(),[](uint32_t v){return v<=255;});
    }
    inline bool KnownLegendPaletteContext(const LegendPaletteContext& row){
        return row[0]&&row[1]&&row[0]!=row[1]&&row[0]<=255&&row[1]<=255&&row[2]<=255;
    }
    inline LegendPaletteContext WireLegendPaletteContext(const DecodedBuildLink& link,size_t env){
        return {link.legends[env*2],link.legends[env*2+1],link.specs[2].spec_id};
    }
    inline bool HasInactivePaletteGroup(const InactiveLegendPalettes& values,size_t env){
        return std::any_of(values.begin()+env*3,values.begin()+env*3+3,[](uint16_t v){return v!=0;});
    }
    enum class InactiveContextVerdict { Unknown, Matches, Different };
    inline InactiveContextVerdict InactivePaletteContextVerdict(
        const std::optional<InactiveLegendContext>& context,size_t env,const LegendPaletteContext& current){
        if(!context||!KnownLegendPaletteContext((*context)[env]))return InactiveContextVerdict::Unknown;
        if(!KnownLegendPaletteContext(current))return InactiveContextVerdict::Unknown;
        return (*context)[env]==current?InactiveContextVerdict::Matches:InactiveContextVerdict::Different;
    }
    inline std::optional<InactiveLegendContext> RecordedInactiveLegendContext(const DecodedBuildLink& link){
        if(link.profession!=PROF_REVENANT||!link.inactive_rev_skills_known||!link.companion_choices_known)return std::nullopt;
        return InactiveLegendContext{WireLegendPaletteContext(link,0),WireLegendPaletteContext(link,1)};
    }
    inline std::optional<InactiveLegendPalettes> RecordedInactiveLegendPalettes(const DecodedBuildLink& link) {
        if(link.profession!=PROF_REVENANT||!link.inactive_rev_skills_known)return std::nullopt;
        InactiveLegendPalettes values{};
        for(std::size_t i=0;i<values.size();++i)values[i]=link.inactive_rev_skills[i];
        return values;
    }
    inline void ApplyInactiveLegendPalettes(DecodedBuildLink& link,const std::optional<InactiveLegendPalettes>& values) {
        if(link.profession!=PROF_REVENANT||!values)return;
        for(std::size_t i=0;i<values->size();++i)link.inactive_rev_skills[i]=(*values)[i];
    }

    // Palette IDs are not unique for Revenant. Only exact positive catalog
    // membership in the active legend's role can disambiguate the public pairs.
    // Missing or multiple candidates remain unresolved, never API-order guesses.
    using PalettePairs=std::vector<std::pair<uint16_t,uint32_t>>;
    inline uint32_t ResolveLegendPalette(const PalettePairs& pairs,uint16_t palette,
                                        const std::vector<uint32_t>& roleSkills,bool requireUniqueGlobal=false) {
        if(!palette)return 0;
        uint32_t found=0;
        for(const auto& [p,id]:pairs)if(p==palette&&id&&
            std::find(roleSkills.begin(),roleSkills.end(),id)!=roleSkills.end()){
            if(found&&found!=id)return 0;
            found=id;
        }
        if(requireUniqueGlobal&&found)
            for(const auto& [p,id]:pairs)if(p==palette&&id!=found)return 0;
        return found;
    }
    inline bool ContainsPalettePair(const PalettePairs& pairs,uint16_t palette,uint32_t id){
        return palette&&id&&std::find(pairs.begin(),pairs.end(),std::make_pair(palette,id))!=pairs.end();
    }
    inline uint16_t UniqueSkillPalette(const PalettePairs& pairs,uint32_t id){
        uint16_t found=0;
        for(const auto& [p,skill]:pairs)if(p&&skill==id){
            if(found&&found!=p)return 0;
            found=p;
        }
        return found;
    }
    // Wire evidence is conditional, not a second editable skill bar.
    // For each environment/slot retain the exact palette, its original
    // typed projection (zero if unresolved), exact two legend codes and
    // third specialization. Never reuse it after a contextual mismatch.
    using RevenantActivePaletteEvidence=std::array<std::array<uint32_t,5>,10>;
    inline bool ValidRevenantActivePaletteEvidence(const RevenantActivePaletteEvidence& evidence){
        for(const auto& row:evidence)
            if(row[0]>65535||row[2]>256||row[3]>256||row[4]>255||(!row[0]&&row[1])||
               ((row[2]==256||row[3]==256)&&row[1]))return false;
        return true;
    }
    template<class Resolver> std::optional<RevenantActivePaletteEvidence> RecordRevenantActivePalettes(
        const DecodedBuildLink& link,Resolver resolve){
        if(link.profession!=PROF_REVENANT||!link.active_palettes_known)return std::nullopt;
        RevenantActivePaletteEvidence evidence{};
        for(int env=0;env<2;++env)for(int slot=0;slot<5;++slot){
            const auto palette=env?link.aquatic_skills[slot]:link.terrestrial_skills[slot];
            // 256 is an out-of-wire sentinel for missing companion context.
            // Complete active bytes are still preserved, but cannot be
            // regenerated as a different or fabricated legend selection.
            const uint32_t first=link.companion_choices_known?link.legends[env*2]:256u;
            const uint32_t second=link.companion_choices_known?link.legends[env*2+1]:256u;
            evidence[env*5+slot]={palette,palette&&first<=255?resolve(palette,first,env,slot):0u,
                first,second,link.specs[2].spec_id};
        }
        return evidence;
    }
    template<class Reverse> std::optional<uint32_t> ProjectRevenantPalette(
        const std::optional<RevenantActivePaletteEvidence>& evidence,
        uint32_t id,int env,int slot,const DecodedBuildLink& link,Reverse reverse){
        if(evidence){
            const auto& row=(*evidence)[env*5+slot];
            if(row[1]==id){
                if(row[2]==link.legends[env*2]&&row[3]==link.legends[env*2+1]&&row[4]==link.specs[2].spec_id)
                    return row[0];
                // A still-unresolved nonzero imported palette cannot safely
                // become an empty slot just because another reference changed.
                if(!id&&row[0])return std::nullopt;
            }
        }
        if(!id)return 0u;
        const auto value=reverse(id);
        return value?std::optional<uint32_t>(value):std::nullopt;
    }

    // Shared checked projection from a live or saved typed build. The low-level
    // codec remains available for exact imported wire packets. Adapters resolve
    // the different trait/legend representations; missing nonzero mappings must
    // never silently encode as zero. No requests or persistence here.
    template<class Build,class TraitChoice,class Palette,class LegendChoice>
    std::optional<DecodedBuildLink> ProjectDerivedBuildLink(const Build& b,
        TraitChoice traitChoice,Palette palette,LegendChoice legendChoice,
        const std::optional<InactiveLegendPalettes>& inactive=std::nullopt,
        const std::optional<InactiveLegendContext>& inactiveContext=std::nullopt) {
        DecodedBuildLink link{};
        static constexpr const char* professions[]={"Guardian","Warrior","Engineer",
            "Ranger","Thief","Elementalist","Mesmer","Necromancer","Revenant"};
        for(std::size_t i=0;i<9;++i)if(b.profession==professions[i])link.profession=static_cast<uint8_t>(i+1);
        if(!link.profession||b.weapons.size()>255)return std::nullopt;
        for(int i=0;i<3;++i){
            const auto& spec=b.specializations[i];
            if(spec.spec_id>255)return std::nullopt;
            link.specs[i].spec_id=static_cast<uint8_t>(spec.spec_id);
            for(int t=0;t<3;++t){
                const auto choice=traitChoice(spec.spec_id,t,spec.traits[t]);
                if(!choice||*choice<0||*choice>3)return std::nullopt;
                link.specs[i].traits[t]=static_cast<uint8_t>(*choice);
            }
        }
        // Resolve companion identities before slot-aware palette projection.
        if(link.profession==PROF_RANGER){
            for(int i=0;i<4;++i){
                const auto id=i<2?b.pets.terrestrial[i]:b.pets.aquatic[i-2];
                if(id>255)return std::nullopt;
                link.pets[i]=static_cast<uint8_t>(id);
            }
        }else if(link.profession==PROF_REVENANT){
            for(int i=0;i<4;++i){
                const auto id=legendChoice(i);
                if(!id||*id>255)return std::nullopt;
                link.legends[i]=static_cast<uint8_t>(*id);
            }
            // Do not re-label an old inactive utility order as belonging
            // to a new legend or specialization. Keep original bytes in
            // Skills and refuse derived links with unqualified nonzero data.
            if(inactive)for(size_t env=0;env<2;++env)
                if(HasInactivePaletteGroup(*inactive,env)&&
                   InactivePaletteContextVerdict(inactiveContext,env,WireLegendPaletteContext(link,env))!=InactiveContextVerdict::Matches)
                    return std::nullopt;
            ApplyInactiveLegendPalettes(link,inactive);
        }
        auto bar=[&](const auto& source,uint16_t (&target)[5],int env){
            const uint32_t ids[]={source.heal,source.utilities[0],source.utilities[1],
                source.utilities[2],source.elite};
            for(int i=0;i<5;++i){
                if constexpr(std::is_invocable_v<Palette,uint32_t,int,int,const DecodedBuildLink&>){
                    const auto value=palette(ids[i],env,i,link);
                    if(!value||*value>65535||(ids[i]&&!*value))return false;
                    target[i]=static_cast<uint16_t>(*value);
                }else{
                    if(!ids[i])continue;
                    const auto value=palette(ids[i]);
                    if(!value||value>65535)return false;
                    target[i]=static_cast<uint16_t>(value);
                }
            }
            return true;
        };
        if(!bar(b.terrestrial_skills,link.terrestrial_skills,0)||
           !bar(b.aquatic_skills,link.aquatic_skills,1))return std::nullopt;
        for(auto w:b.weapons){
            if(w>65535)return std::nullopt;
            link.weapons.push_back(static_cast<uint16_t>(w));
        }
        return link;
    }

    // Decoded wardrobe template link
    struct DecodedWardrobeLink {
        uint16_t aquabreather_skin = 0;
        uint16_t backpack_skin = 0;
        uint16_t backpack_dyes[4] = {0};
        uint16_t chest_skin = 0;
        uint16_t chest_dyes[4] = {0};
        uint16_t boots_skin = 0;
        uint16_t boots_dyes[4] = {0};
        uint16_t gloves_skin = 0;
        uint16_t gloves_dyes[4] = {0};
        uint16_t helm_skin = 0;
        uint16_t helm_dyes[4] = {0};
        uint16_t leggings_skin = 0;
        uint16_t leggings_dyes[4] = {0};
        uint16_t shoulders_skin = 0;
        uint16_t shoulders_dyes[4] = {0};
        uint16_t outfit_id = 0;
        uint16_t outfit_dyes[4] = {0};
        uint16_t aquatic_weapon_a = 0;
        uint16_t aquatic_weapon_b = 0;
        uint16_t weapon_a_main = 0;
        uint16_t weapon_a_off = 0;
        uint16_t weapon_b_main = 0;
        uint16_t weapon_b_off = 0;
        uint16_t visibility_flags = 0;
    };

    class ChatLink {
    public:
        // --- Encoding ---

        // Exact single-entity links exposed to UI. Unsupported types and IDs
        // outside the implemented wire format return an empty string.
        static constexpr bool EntityIdValid(uint32_t id) { return id != 0 && id <= 0xFFFFFFu; }
        static std::string EncodeEntity(ChatLinkType type,uint32_t id,
                                        uint32_t skin=0,uint32_t upgrade1=0,uint32_t upgrade2=0);

        // Generate item chat link: [&Base64...]
        static std::string EncodeItem(uint32_t item_id, int count = 1,
                                       uint32_t skin_id = 0,
                                       uint32_t upgrade1 = 0,
                                       uint32_t upgrade2 = 0);

        // Generate skill chat link
        static std::string EncodeSkill(uint32_t skill_id);

        // Generate trait chat link
        static std::string EncodeTrait(uint32_t trait_id);

        // Generate skin chat link
        static std::string EncodeSkin(uint32_t skin_id);

        // Generate build template chat link
        static std::string EncodeBuild(const DecodedBuildLink& build);

        // Generate wardrobe template chat link (inverse of DecodeWardrobe)
        static std::string EncodeWardrobe(const DecodedWardrobeLink& wardrobe);

        // --- Decoding ---

        // Detect link type from a chat link string
        static ChatLinkType DetectType(const std::string& link);

        // Decode an item chat link
        static bool DecodeItem(const std::string& link, DecodedItemLink& out);

        // Decode a build template chat link
        static bool DecodeBuild(const std::string& link, DecodedBuildLink& out);

        // Decode a wardrobe template chat link
        static bool DecodeWardrobe(const std::string& link, DecodedWardrobeLink& out);

        // --- Utilities ---

        // Base64 encode/decode
        static std::string Base64Encode(const uint8_t* data, size_t len);
        static std::vector<uint8_t> Base64Decode(const std::string& encoded);

        // Extract the base64 payload from [&...] wrapper
        static bool ExtractPayload(const std::string& link, std::string& payload);

        // Get profession name from code
        static const char* ProfessionName(uint8_t code);

        // Get weapon name from type ID
        static const char* WeaponName(uint16_t type_id);
    };

}
