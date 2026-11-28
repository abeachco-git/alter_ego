#include <set>
#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include "nexus/Nexus.h"
#include "CharacterScopes.h"
#include "ComponentNameOrigin.h"
#include "CharacterProvenance.h"
#include "RefreshStatusCore.h"
#include "ProfessionMetadataTypes.h"
#include "ChatLink.h"
#include "SkillEditCore.h"
#include "ReferenceExtras.h"

namespace AlterEgo {

    namespace Composable { struct Document; }
    namespace RefreshEngine { struct IdBatch; }

    enum class FetchStatus {
        Idle,
        InProgress,
        Success,
        Error
    };

    // Native account configuration status
    enum class AccountStatus { Unknown, Unavailable, Ready };

    // Equipment slot on a character
    struct EquipmentItem {
        uint32_t id = 0;
        std::string slot;           // "Helm", "Shoulders", "Coat", etc.
        uint32_t skin = 0;          // Skin ID (transmuted appearance)
        std::vector<uint32_t> upgrades;    // Sigil/Rune IDs
        std::vector<uint32_t> infusions;
        // Live response explicitly contained the array, including empty.
        // Legacy cached defaults do not establish this provenance.
        bool upgrades_observed=false,infusions_observed=false;
        std::vector<int> dyes;      // Dye color IDs (up to 4)
        std::string binding;        // "Character" or "Account"
        std::string bound_to;       // Character name if character-bound
        bool binding_known = false;
        bool bound_to_known = false;
        int tab = 0;                // Equipment tab number
        uint32_t stat_id = 0;       // Selected itemstat ID (for legendary/ascended)
        std::map<std::string, int> attributes; // e.g. {"Power": 251, "Precision": 179}
        int64_t charges = -1; // absent/unknown, distinct from zero remaining
        bool physical_supplement=false; // fallback provenance, not an equipment-tab row
    };

    // Cached itemstat info from /v2/itemstats (public, no auth)
    struct ItemStatInfo {
        uint32_t id = 0;
        std::string name;           // e.g. "Berserker's", "Minstrel's"
        std::vector<std::string> attributes; // e.g. {"Power", "Precision", "CritDamage"}
        nlohmann::json formulas; // nullable, ordered validated public coefficients
    };

    // A gear slot in a saved build (user-entered stats/upgrades)
    struct BuildGearSlot {
        std::string slot;           // "Helm", "Shoulders", "Coat", etc.
        uint32_t item_id = 0;      // Original live item ID (optional; older saves omit this)
        std::vector<uint32_t> infusion_ids; // Original infusions (optional)
        std::vector<std::string> infusion_attributes; // Optional per-position stat override; blank = exact ID
        bool infusion_snapshot = false; // True even if captured with no infusions
        uint32_t stat_id = 0;       // /v2/itemstats ID
        std::string stat_name;      // "Berserker's", etc.
        std::string rune;           // Rune name (armor only)
        uint32_t rune_id = 0;       // Rune item ID (for compact encoding)
        std::string sigil;          // Sigil name (weapon only)
        uint32_t sigil_id = 0;      // Sigil item ID (for compact encoding)
        std::string sigil2;         // Second sigil name (two-handed weapons)
        uint32_t sigil2_id = 0;     // Second sigil item ID
        std::string infusion;       // Infusion name
        std::string weapon_type;    // "Axe", "Sword", etc. (weapon slots only)
    };

    // Weapon skills for a profession+weapon combination
    // A build template specialization line
    struct SpecLine {
        uint32_t spec_id = 0;       // /v2/specializations ID
        int traits[3] = {0, 0, 0};  // Selected major trait indices (0=none, 1=top, 2=mid, 3=bottom)
        bool explicitly_empty=false;
        uint8_t empty_traits=0;
    };

    // A build template's skill loadout
    struct SkillBar {
        uint32_t heal = 0;
        uint32_t utilities[3] = {0, 0, 0};
        uint32_t elite = 0;
        uint8_t empty_slots=0; // live explicit nulls: heal=1, utilities=2/4/8, elite=16
    };

    // Ranger pets
    struct RangerPets {
        uint32_t terrestrial[2] = {0, 0};
        uint32_t aquatic[2] = {0, 0};
    };

    // Revenant legends
    struct RevenantLegends {
        std::string terrestrial[2];
        std::string aquatic[2];
    };

    // A single build template
    struct BuildTemplate {
        int tab = 0;
        bool is_active = false;
        std::string name;
        std::string profession;     // Profession name from API
        SpecLine specializations[3];
        SkillBar terrestrial_skills;
        SkillBar aquatic_skills;
        RangerPets pets;
        RevenantLegends legends;
        std::vector<uint32_t> weapons; // Weapon type IDs
        bool pets_known = false;
        bool legends_known = false;
        uint8_t legends_empty_slots = 0; // observed API nulls; legacy cached empty strings are unknown
        bool weapons_known = false;
        template<class LegendCode> nlohmann::json CompanionPayload(bool legend,LegendCode code)const{
            return SkillEdit::CaptureCompanionPayload<nlohmann::json>(
                {pets.terrestrial[0],pets.terrestrial[1],pets.aquatic[0],pets.aquatic[1]},
                {legends.terrestrial[0],legends.terrestrial[1],legends.aquatic[0],legends.aquatic[1]},
                legend,code,legends_empty_slots);
        }
    };

    // Cached info about an item from /v2/items (public, no auth)
    struct ItemInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
        std::string rarity;
        std::string type;
        std::string description;
        std::string chat_link;
        uint32_t default_skin = 0;  // Default appearance skin ID (when not transmuted)
        nlohmann::json details;     // Full details block for stats, etc.
        nlohmann::json optional_metadata; // validated public level/value/restrictions
        int64_t verified_at=0; // request-start microseconds, zero for legacy data
    };

    // Cached info about a skin from /v2/skins (public, no auth)
    struct SkinInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
        std::string type;
        std::string rarity;
        std::string weight_class; // GW2 /v2/skins details.weight_class: Light, Medium, Heavy, Clothing
        int dye_slot_count = -1; // Number of dye channels (-1 = unknown)
        std::vector<std::string> dye_slot_materials; // Per-channel material: "cloth", "leather", "metal"
    };

    // Cached specialization info from /v2/specializations (public, no auth)
    struct SpecializationInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
        std::string background_url;
        std::string profession;
        std::string profession_icon_url;       // Profession/elite emblem (small)
        std::string profession_icon_big_url;   // Profession/elite emblem (large)
        bool elite = false;
        std::vector<uint32_t> minor_traits;
        std::vector<uint32_t> major_traits;
        int64_t verified_at = 0; // durable request-start microseconds; zero is unverified
    };

    // Cached trait info from /v2/traits (public, no auth)
    struct TraitInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
        std::string description;
        int tier = 0;               // 1=Adept, 2=Master, 3=Grandmaster
        int order = 0;              // 0=top, 1=mid, 2=bottom
        std::string slot;           // "Major" or "Minor"
        nlohmann::json facts;       // Tooltip facts array
        int64_t verified_at = 0; // durable request-start microseconds; zero is unverified
        nlohmann::json optional_metadata; // validated public optional fields, never live effects
    };

    // Cached skill info from /v2/skills (public, no auth)
    struct SkillInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
        std::string description;
        std::string type;           // "Heal", "Utility", "Elite", "Weapon", etc.
        uint32_t specialization = 0; // gating elite-spec id (0 = core, usable by all)
        std::vector<std::string> professions; // professions that can slot it; empty = racial
        std::optional<bool> no_underwater; // Absent means eligibility is unverified.
        nlohmann::json facts;       // Tooltip facts array
        int64_t verified_at = 0; // durable request-start microseconds; zero is unverified
        nlohmann::json optional_metadata; // validated public optional fields, never live effects
    };

    // Cached profession info from /v2/professions (public, no auth)
    // Ranger pet info from /v2/pets (public, no auth). The build-template pet
    // byte is the /v2/pets id directly.
    struct PetInfo {
        uint32_t id = 0;
        std::string name;
        std::string icon_url;
    };

    // Dye color info from /v2/colors (public, no auth)
    struct DyeColor {
        int id = 0;
        std::string name;
        int r = 0, g = 0, b = 0;           // Base RGB (fallback)
        int cloth_r = 0, cloth_g = 0, cloth_b = 0;
        int leather_r = 0, leather_g = 0, leather_b = 0;
        int metal_r = 0, metal_g = 0, metal_b = 0;
    };

    // A character on the account
    struct Character {
        std::string name;
        std::string race;
        std::string gender;
        std::string profession;
        int level = 0;
        int age = 0;                // Seconds played
        std::string created;        // ISO date
        std::string last_modified;  // ISO date — last login/modification
        time_t data_refreshed = 0; // Latest committed scope (not proof of complete freshness)
        CharacterScopes::Stamps scope_stamps{};
        int deaths = 0;
        std::vector<std::string> crafting;      // Crafting disciplines (active first)
        std::vector<int> crafting_levels;
        std::vector<bool> crafting_active;      // Whether each discipline is active
        bool crafting_cached = false;          // Fetched once (even if no disciplines)
        std::vector<EquipmentItem> equipment;
        std::vector<BuildTemplate> build_tabs;
        std::map<int, std::string> equipment_tab_names; // tab number -> name
        int active_build_tab = 0;
        int active_equipment_tab = 0;
            std::string account_name;              // Native account GUID; legacy name/unknown rows remain readable
    };

    // Item location result from native API cache query
    struct ItemLocationEntry {
        std::string location;       // e.g. "Bank", character name
        std::string sublocation;    // e.g. "Bag", "Equipped"
        int count = 0;
        std::string account_name;   // Which account this location belongs to
    };

    // Native account projection
    struct AccountInfo {
        std::string name;           // Stable native account GUID
        std::string label;          // User-assigned friendly name (e.g. "Main")
        std::string display_name;   // label if non-empty, else name
        int64_t last_updated = 0;
        bool validated = false;
        std::vector<std::string> characters; // Character names on this account
    };

    struct ItemLocationResult {
        uint32_t item_id = 0;
        std::string name;
        int total_count = 0;
        std::vector<ItemLocationEntry> locations;
    };

    // Game mode tags for saved builds
    enum class GameMode {
        PvE,
        WvW,
        PvP,
        Raid,
        Fractal,
        Other
    };

    // Source-wide infusion requirements; unlike captured equipment infusions,
    // these can be installed in any eligible gear slot.
    struct BuildInfusionTotal {
        uint32_t item_id = 0;
        int count = 0;
        std::string attribute; // Optional stat-family override; exact ID by default.
    };

    // A saved build in the build library
    struct SavedBuild {
        std::string id;             // Unique ID (timestamp-based)
        std::string name;           // User-given name
        std::string source_character; // Origin character, empty for legacy/imported builds
        std::string source_url;     // Snow Crows page, empty for local/other imports
        std::string import_warning; // Source fields missing or not resolved during import
        std::string chat_link;      // Original chat link [&...]
        std::string profession;     // Profession name (decoded)
        GameMode game_mode = GameMode::PvE; // Primary/legacy mode for existing integrations
        std::vector<GameMode> game_modes; // Additional classification tags; empty means legacy primary only
        std::string notes;          // User notes
        time_t created = 0;
        // Decoded build data (populated on import)
        SpecLine specializations[3];
        SkillBar terrestrial_skills; // Skill IDs (resolved from palette)
        SkillBar aquatic_skills;
        RangerPets pets;
        uint8_t legend_codes[4] = {0}; // Revenant legend codes
        bool pets_recorded=false, legends_recorded=false; // explicit saved/link array presence, not live unlock evidence
        // Transient exact source projection, not a second persistent owner.
        // Typed edits overlay only changed slots; native arrays remain authoritative.
        nlohmann::json pets_source_slots, legends_source_slots;
        std::array<uint32_t,4> CompanionValues(bool legend)const {
            if(legend)return {legend_codes[0],legend_codes[1],legend_codes[2],legend_codes[3]};
            return {pets.terrestrial[0],pets.terrestrial[1],pets.aquatic[0],pets.aquatic[1]};
        }
        nlohmann::json CompanionPayload(bool legend)const {
            const auto& source=legend?legends_source_slots:pets_source_slots;
            if(!source.is_array()&&(legend?legends_recorded:pets_recorded))
                return nlohmann::json(CompanionValues(legend)); // complete decoded native pair block
            return SkillEdit::OverlayCompanionSlots(source,CompanionValues(legend),legend?255u:UINT32_MAX);
        }
        SkillEdit::CompanionProjection CompanionSlots(bool legend)const {
            const auto& source=legend?legends_source_slots:pets_source_slots;
            if(source.is_array()||(legend?legends_recorded:pets_recorded))
                return SkillEdit::JsonCompanionSlots(CompanionPayload(legend),legend?255u:UINT32_MAX);
            SkillEdit::CompanionProjection result;
            const auto values=CompanionValues(legend);
            for(std::size_t i=0;i<values.size();++i)if(values[i])result.Set(i,values[i]);
            return result;
        }
        std::optional<InactiveLegendPalettes> inactive_rev_skills; // exact wire order; absent if not reported
        std::optional<RevenantActivePaletteEvidence> rev_active_palettes; // conditional imported wire evidence
        std::vector<uint32_t> weapons;
        // Gear (user-entered)
        std::map<std::string, BuildGearSlot> gear; // slot name -> gear data
        std::vector<BuildInfusionTotal> infusion_totals; // aggregate item requirements, no slot assignment
        // Optional manually edited or calculated totals. Absence means unknown,
        // not zero. Legacy/imported builds derive totals from their item IDs.
        bool infusion_summary_present = false;
        int infusion_ar_total = 0;
        std::map<std::string, int> infusion_stat_totals;
        std::string rune_name;      // Shared rune for all armor pieces
        uint32_t rune_id = 0;       // Shared rune item ID
        std::string relic_name;     // Relic name
        uint32_t relic_id = 0;      // Relic item ID
        // Original AE2 code preserved at import time. Lets us re-resolve
        // names and palette data later if the GW2 API was unavailable or
        // rate-limited at import. Empty for plain chat-link imports.
        std::string ae2_code;
        std::optional<CharacterIdentity::Provenance> source_identity{};
    };

    class GW2API {
    public:
        // --- Nexus integration ---
        static void Initialize(AddonAPI_t* api);
        static void Shutdown();

        // Data path helper
        static std::string GetDataDirectory();

        // --- native API integration ---
        static AccountStatus GetAccountStatus();
        static void RequestCharacterRefresh(const std::string& priorityChar = ""); // Fetch all characters via native API
        static void RequestCharacterList(const std::string& account={}); // Verify roster identities in the requested account scope
        static void RequestCharacterRefreshSelected(
            const std::vector<std::string>& names, bool refreshCrafting = true);
        static void RequestCharacterRefreshAddresses(const std::vector<CharacterIdentity::Address>& addresses,
            CharacterScopes::Mask demand=CharacterScopes::All,bool refreshCrafting=true);
        static void RequestCharacterActivityRefresh(const std::string& account,const std::string& name,CharacterScopes::Mask demand,bool manual=false);
        static void RequestCharacterActivityRefreshAddresses(const std::vector<CharacterIdentity::Address>& addresses);

        // native API cached queries (synchronous, from native API in-memory cache)
        static void QuerySkinUnlocks(const std::vector<uint32_t>& skin_ids);
        static void QueryItemLocation(uint32_t item_id);

        // --- Multi-account ---
        static uint32_t GetAccountCount();               // Number of configured accounts (0 = unknown)
        static std::vector<AccountInfo> GetAccounts();
        static std::string GetCurrentAccountName(); // Account for the logged-in character
        static void SetCurrentAccountFromCharacter(const std::string& charName); // Map character to account
        static bool IsMultiAccount();                    // account_count > 1
        static bool HasAccountsData();                   // true if accounts have been queried
        static std::vector<std::string> GetAllCharacterNamesFromAccounts(const std::string& accountFilter = "");
        static std::string GetAccountForCharacter(const std::string& charName);

        // --- Fetch state ---
        static FetchStatus GetFetchStatus();
        struct RosterRefreshInspection { FetchStatus status=FetchStatus::Idle; std::string message; };
        static RosterRefreshInspection InspectRosterRefresh();
        struct CharacterRefreshInspection { bool busy=false; RefreshStatus::CharacterWork work; std::string message; };
        static CharacterRefreshInspection InspectCharacterRefresh(const std::string& account,const std::string& name,uint64_t generation);
        static std::string GetFetchStatusMessage();
        static std::string GetCurrentFetchCharName();

        // --- Query ---
        static bool HasCharacterData();
        static time_t GetLastUpdated();
        static uint64_t GetCharacterDataRevision();
        static uint64_t GetBuildLibraryRevision();

        // Character access
        static std::shared_ptr<const std::vector<Character>> GetCharacters();
        static std::optional<Character> GetCharacter(const std::string& name);
        static std::vector<std::string> GetPendingCharNames();
        // Cache adoption already committed on disk; discard only affected in-memory rows.
        static void DiscardIdentityCache(const std::string& account,const std::set<std::string>& names,bool all=false);
        // Caller holds the account gate after verified identity/cache adoption.
        static bool PublishDiscoveredCore(const std::string& account,const std::string& name,
            const nlohmann::json& core,int64_t verifiedAt);

        // Skin unlock status (from native API cache)
        static bool IsSkinUnlocked(uint32_t skin_id);  // true if unlocked, false if unknown/locked
        static bool HasSkinUnlockData(uint32_t skin_id); // true if we've queried this skin
        static void MarkSkinUnlocked(uint32_t skin_id);  // mark a skin as unlocked (e.g. from Events: Alerts)

        // Item location (from native API cache)
        static const ItemLocationResult* GetItemLocation(uint32_t item_id);

        // Item/Skin/Spec/Trait/Skill/Color caches (public endpoints, fetched directly)
        static std::shared_ptr<const ItemInfo> GetItemInfo(uint32_t item_id);
        static uint32_t FindItemIdByName(const std::string& name); // searches item cache + name cache
        static std::vector<std::pair<std::string, uint32_t>> SearchKnownInfusions(
            const std::string& query, size_t maxResults = 100, bool enrichment = false);
        static void CacheItemNameId(const std::string& name, uint32_t id); // add to persistent name→ID cache
        static bool LoadItemNameCache();
        static bool SaveItemNameCache();
        static std::shared_ptr<const SkinInfo> GetSkinInfo(uint32_t skin_id);
        static std::shared_ptr<const SpecializationInfo> GetSpecInfo(uint32_t spec_id);
        static std::shared_ptr<const TraitInfo> GetTraitInfo(uint32_t trait_id);
        static std::shared_ptr<const SkillInfo> GetSkillInfo(uint32_t skill_id);
        static std::shared_ptr<const DyeColor> GetDyeColor(int color_id);
        static std::shared_ptr<const ItemStatInfo> GetItemStatInfo(uint32_t stat_id);
        static std::unordered_map<uint32_t, std::shared_ptr<const ItemStatInfo>> GetAllItemStats();
        static void FetchAllItemStatsAsync(bool manual=false);
        static void AdoptPublicItems(const nlohmann::json& rows,bool recordAliases=false);
        static std::shared_ptr<const ProfessionInfo> GetProfessionInfo(const std::string& profession);
        static void FetchProfessionInfoAsync(const std::string& profession,bool manual=false);
        static void RefreshProfessionMetadataAsync();
        static std::string ProfessionMetadataStatus();
        static std::shared_ptr<const std::map<std::string, ProfessionWeaponData>> GetProfessionWeapons(const std::string& profession);

        // Fetch detail data on-demand (direct HTTP — no auth needed)
        static void FetchItemDetails(const std::vector<uint32_t>& item_ids, bool forceRefetch = false); // synchronous
        static void FetchItemDetailsAsync(const std::vector<uint32_t>& item_ids, bool forceRefetch = false);
        // Worker-only shared item consumers. Catalog batches require exact raw
        // coverage; Inventory may retain verified partial metadata.
        static void EnrichInventoryItems(const std::vector<uint32_t>& ids,bool manual);
        static nlohmann::json AcquireCatalogItemBatch(const std::vector<uint32_t>& ids,bool incremental,std::map<uint32_t,int64_t>& stamps);
        static void FetchSkinDetailsAsync(const std::vector<uint32_t>& skin_ids);
        static void FetchSpecDetailsAsync(const std::vector<uint32_t>& spec_ids);
        static void FetchTraitDetailsAsync(const std::vector<uint32_t>& trait_ids);
        static void FetchSkillDetailsAsync(const std::vector<uint32_t>& skill_ids, bool requireEnvironmentMetadata=false);
        // Worker-only complete legend enrichment; joins the existing skill owner.
        static nlohmann::json AcquireLegendSwapSkills(const std::vector<uint32_t>& skillIds,bool manual);
        static void FetchItemStatDetailsAsync(const std::vector<uint32_t>& stat_ids);
        static void FetchDyeColorsAsync(bool manual=false);

        // Persistence
        static bool LoadCharacterData();
        static bool SaveCharacterData(const std::string& account = {}, const std::string& name = {},
            const Character* replacement=nullptr,const std::string& legacyAccount={},const std::function<bool()>& current={});

        // Reference-data caches (items, skins, specs, traits, skills,
        // itemstats, dye colors). Persisted to disk so a refreshed character's
        // gear/builds can render instantly on next launch without re-hitting
        // the GW2 API. Loaded at startup; saved after each batch fetch.
        static bool LoadReferenceCaches();
        static void AdoptCommittedSkins(const nlohmann::json& rows);
        static void AdoptReferenceObservations(int dataset,const nlohmann::json& rows,int64_t stamp=0);

        // --- Build Library ---
        static const std::vector<SavedBuild>& GetSavedBuilds();
        // Inserts the build into the library, grouping by profession so newly
        // imported builds appear under their profession's existing section
        // instead of being appended as a new one. Returns the insertion index,
        // or -1 if persistence failed.
        static int AddSavedBuild(SavedBuild build,const std::string& setId={},const std::string& newSetName={},const std::set<std::string>& kinds={});
        static bool RemoveSavedBuild(const std::string& id,uint64_t expectedRevision=0);
        static bool UpdateSavedBuild(const std::string& id, const std::string& name, const std::string& notes);
        static bool RenameSavedLoadout(const std::string& id,const std::string& name,uint64_t expectedRevision);
        static bool DuplicateSavedLoadout(const std::string& id,const std::string& name,uint64_t expectedRevision,std::string* savedId);
        static bool SetSavedBuildGameMode(const std::string& id, GameMode mode);
        static bool SetSavedBuildGameModes(const std::string& id, const std::vector<GameMode>& modes);
        static bool ReorderSavedBuild(int fromIdx, int toIdx);
        // Move a whole profession block, or an individual build within its own group.
        static bool ReorderSavedBuildGroup(const std::string& movedProfession,
                                           const std::string& targetProfession,
                                           bool after);
        static bool ReorderSavedBuildInGroup(const std::string& movedId,
                                             const std::string& targetId,
                                             bool after);
        static bool LoadBuildLibrary();
        // Native component commands. SavedBuild is a resolved presentation/edit value.
        // Kinds: base_build, skills, weapons, armor, trinkets, runes,
        // sigils, infusions, relic. IDs are stable and names are independent.
        static bool GetComposableLoadouts(nlohmann::json& snapshot);
        static bool AssignLoadoutComponent(const std::string& loadoutId,
                                           const std::string& kind,
                                           const std::string& componentId);
        static bool CopyLoadoutComponent(const std::string& loadoutId,
                                         const std::string& kind,
                                         const std::string& newComponentName);
    static bool CopyEditedLoadoutComponent(const std::string& loadoutId,
        const std::string& kind, const std::string& sourceComponentId,
        const std::string& name, const nlohmann::json& data,uint64_t expectedRevision=0,
        Composable::NameOrigin nameOrigin=Composable::NameOrigin::Manual);
        static bool ReuseEditedLoadoutComponent(const std::string& loadoutId,
            const std::string& kind, const std::string& sourceId,
            const std::string& candidateId, const nlohmann::json& draft,
            uint64_t expectedRevision);
        static bool EditSharedLoadoutComponent(const std::string& kind,
                                                const std::string& componentId,
                                                const nlohmann::json& data);
    static bool EditAndRenameLoadoutComponent(const std::string& kind,
        const std::string& componentId, const std::string& name,
        const nlohmann::json& data,uint64_t expectedRevision=0,
        Composable::NameOrigin nameOrigin=Composable::NameOrigin::Manual);
        static bool SetLoadoutFashion(const std::string& loadoutId,
                                      const std::string& fashionId);
        static bool CreateLoadoutComponent(const std::string& kind,
                                           const std::string& name,
                                           const nlohmann::json& data,
                                           std::string* savedId = nullptr,
                                           bool reuseExact = false, const std::string& profession = {},
                                           Composable::NameOrigin nameOrigin=Composable::NameOrigin::Unknown,
                                           bool generateName=false);
        static bool DuplicateLoadoutComponent(const std::string& kind,
                                              const std::string& componentId,
                                              const std::string& name,std::string* savedId=nullptr,uint64_t expectedRevision=0);
        static bool RenameLoadoutComponent(const std::string& kind,
                                           const std::string& componentId,
                                           const std::string& name,uint64_t expectedRevision=0,
                                           Composable::NameOrigin nameOrigin=Composable::NameOrigin::Manual);
        static bool DeleteUnusedLoadoutComponent(const std::string& kind,
                                                 const std::string& componentId,uint64_t expectedRevision=0);
        // Capture an independent component from live or imported SavedBuild
        // data, without creating a new Loadout or mutating existing ones.
        static std::string SuggestLibraryComponentName(const std::string& kind,
                                                         const nlohmann::json& data,bool requestMissing=false);
        static void RepairPendingLibraryNames();
        static std::string SuggestCapturedComponentName(const SavedBuild& capture,
                                                        const std::string& kind);
        static bool SaveCapturedLoadoutComponent(const SavedBuild& capture,
                                                  const std::string& kind,
                                                  const std::string& name,
                                                  std::string* savedId = nullptr,
                                                  bool reuseExact = true);
    static bool SaveCapturedEquipmentSet(const SavedBuild& capture,const std::string& name,std::string* savedId,const std::set<std::string>& kinds={});
    static bool ApplyEquipmentSet(const std::string& loadoutId,const std::string& setId);
    static bool DetachEquipmentSet(const std::string& loadoutId);
    static bool SaveLoadoutAsEquipmentSet(const std::string& loadoutId,const std::string& name,std::string* savedId,const std::set<std::string>& kinds={});
    static bool DuplicateEquipmentSet(const std::string& setId,const std::string& name,std::string* savedId,uint64_t expectedRevision=0);
    static bool CreateEquipmentSet(const std::string& name,const std::map<std::string,std::string>& refs,uint64_t revision,std::string* savedId);
        static bool SaveEquipmentSetDraft(const std::string& id,const std::string& name,const std::map<std::string,std::string>& refs,uint64_t revision);
    static bool SaveLoadoutReferenceDraft(const std::string& id,const std::string& setId,const std::string& fashionId,const std::map<std::string,std::string>& refs,uint64_t revision);
    static bool AssignEquipmentSetComponent(const std::string& setId,const std::string& kind,const std::string& componentId,uint64_t revision);
    static std::vector<std::pair<std::string,std::string>> FindCapturedEquipmentSets(const SavedBuild& capture);
    static bool RenameEquipmentSet(const std::string& setId,const std::string& name,uint64_t expectedRevision=0);
    static bool RemoveEquipmentSet(const std::string& setId,uint64_t expectedRevision=0);
    static bool ReplaceCapturedLoadoutComponent(const SavedBuild& capture,
        const std::string& kind, const std::string& componentId);
    static bool ImportPortableLoadout(const nlohmann::json& packet);



        // --- Build editor support ---
        // Encode a SavedBuild's definition (traits/skills/pets/legends/weapons) to a GW2 chat link.
        static std::string EncodeSavedBuildToChatLink(const SavedBuild& b);
        // List of specialization IDs (core + elite) for a profession, from /v2/professions.
        static std::vector<uint32_t> GetProfessionSpecIds(const std::string& profession);
        // All slottable skill IDs for a profession (from the palette map). Empty until
        // FetchProfessionPaletteAsync has run for that profession.
        static std::vector<uint32_t> GetProfessionSkillIds(const std::string& profession);
        // Ranger pets (for the build editor's pet picker). Empty until fetched.
        static std::vector<PetInfo> GetPets();
        static void FetchPetsAsync();
        // Revenant legend swap-skill icons. Maps AE legend byte code (13..19) -> the
        // legend's swap skill id (its icon is the legend emblem). 0 if unknown.
        static uint32_t GetLegendSwapSkill(uint8_t code);
        static void FetchLegendsAsync();
        // Make an in-memory blank build (id assigned on AddSavedBuild).
        static SavedBuild CreateBlankBuild(const std::string& profession, GameMode mode);
        // Overwrite a saved build's definition fields (specs/skills/pets/legends/weapons/
        // profession) and regenerate its chat_link, leaving name/notes/gear intact, then save.
        static bool SaveEditedLoadout(const SavedBuild& draft, uint64_t expectedRevision = 0);
        // Compare editable native fields with the same serializer used by persistence.
        static nlohmann::json EditorBuildSnapshot(const SavedBuild& draft);
        static bool RebaseLoadoutDraft(SavedBuild& draft,const std::string& oldSet,const std::string& oldFashion,
            const std::map<std::string,std::string>& oldRefs,const std::string& newSet,const std::string& newFashion,
            const std::map<std::string,std::string>& newRefs,uint64_t revision,std::string& error,
            const std::map<std::string,std::string>& oldInherited={},const std::map<std::string,std::string>& newInherited={});
        // Cache-only, evidence-qualified context; no catalog demand here.
        static std::optional<std::string> RevenantEliteContext(const std::array<uint32_t,3>& specs);
        static std::vector<std::string> RevenantContextWarnings(const nlohmann::json& raw);
        static bool ReviewUnifiedLoadout(const SavedBuild& draft,const std::string& setId,const std::string& fashionId,
            const std::map<std::string,std::string>& refs,uint64_t revision,nlohmann::json& review,std::string& error,bool includeAll=false,
            const std::map<std::string,std::string>& inherited={});
        static bool SaveUnifiedLoadout(const SavedBuild& draft,const std::string& setId,const std::string& fashionId,
            const std::map<std::string,std::string>& refs,const std::set<std::string>& sharedKinds,uint64_t revision,
            const std::map<std::string,std::string>& inherited={},const std::set<std::string>& sharedReferences={});
        static bool ConsolidateLoadoutComponent(const std::string& kind, const std::string& source, const std::string& survivor, uint64_t expectedRevision);
        static bool ReplaceSavedBuildDefinition(const std::string& id, const SavedBuild& def);
        // Write the whole library to an arbitrary path as JSON.
        static bool ExportBuildLibraryToFile(const std::string& path);
        // Load a previously exported library file. If replaceAll is true the
        // current library is wiped first; otherwise builds are appended,
        // skipping any whose id already exists. Returns false (and sets
        // errOut) only if the file can't be opened or contains nothing valid.
        static bool ImportBuildLibraryFromFile(const std::string& path, bool replaceAll,
                                               int& addedOut, int& skippedOut,
                                               std::string& errOut);

        // --- Palette ID mapping ---
        static uint32_t GetSkillIdFromPalette(const std::string& profession, uint16_t palette_id);
        static uint32_t ResolveRevenantPalette(uint16_t palette,uint32_t legend,int environment,int slot);
        static bool HasRevenantPalettePair(uint16_t palette,uint32_t skill);
        static uint16_t GetPaletteIdFromSkill(const std::string& profession, uint32_t skill_id);
        static bool HasPaletteData(const std::string& profession);
        static void FetchProfessionPaletteAsync(const std::string& profession);

    private:
        static bool CommitComposableEdit(
            const std::function<bool(Composable::Document&, std::string&)>& edit);
        static AddonAPI_t* s_api;
        static FetchStatus s_fetch_status;
        static std::string s_fetch_message;
        static RefreshStatus::CharacterBatch s_character_work;

        // Multi-account state
        static std::string s_active_character;

        static std::vector<Character> s_characters;
        static bool s_has_character_data;
        static time_t s_last_updated;

        // Pending character fetch state
        static std::vector<std::string> s_pending_char_names;
        static int s_chars_fetched;
        static int s_chars_total;
        static Character s_current_char;

        // native API cached data

        // Caches (public endpoint data)
        static std::unordered_map<uint32_t, std::shared_ptr<const ItemInfo>> s_item_cache;
        static std::unordered_map<uint32_t, std::shared_ptr<const SkinInfo>> s_skin_cache;
        static std::unordered_map<uint32_t, std::shared_ptr<const SpecializationInfo>> s_spec_cache;
        static std::unordered_map<uint32_t, std::shared_ptr<const TraitInfo>> s_trait_cache;
        static std::unordered_map<uint32_t, std::shared_ptr<const SkillInfo>> s_skill_cache;
        static std::unordered_map<int, std::shared_ptr<const DyeColor>> s_dye_cache;
        static std::unordered_map<uint32_t, std::shared_ptr<const ItemStatInfo>> s_itemstat_cache;

        // Profession info cache
        static std::unordered_map<std::string, std::shared_ptr<const ProfessionInfo>> s_profession_cache;
        static std::unordered_map<std::string, std::shared_ptr<const std::map<std::string, ProfessionWeaponData>>> s_profession_weapons;

        // Ranger pets cache (build editor)


        // Build library
        static std::vector<SavedBuild> s_saved_builds;

        // Persistent name→ID cache (survives restarts, grows organically)
        static std::unordered_map<std::string, uint32_t> s_item_name_id_cache;
        static bool s_item_name_cache_dirty;

        // Palette ID mapping: profession -> (palette_id -> skill_id)
        static std::unordered_map<std::string, std::unordered_map<uint16_t, uint32_t>> s_palette_to_skill;
        static PalettePairs s_revenant_palette_pairs; // published with existing profession snapshot
        static std::unordered_map<std::string, std::unordered_map<uint32_t, uint16_t>> s_skill_to_palette;

        static std::mutex s_mutex;

        // HTTP helpers (for public/unauthenticated endpoints only)
        static std::string HttpGet(const std::string& url,bool* attempted=nullptr);
        static bool EnsureDataDirectory();

        // Batch fetch helpers (public endpoints, synchronous)
        static void FetchSkinDetails(RefreshEngine::IdBatch& claim);
        static void FetchClaimedReferenceDetails(RefreshEngine::IdBatch& claim,const std::vector<uint32_t>& ids,int dataset);
        static void FetchBalanceReferenceAsync(const std::vector<uint32_t>& ids,int dataset,bool requireEnvironmentMetadata=false);
        static std::vector<uint32_t> BalanceReferenceDemand(const std::vector<uint32_t>& ids,int dataset,bool requireEnvironmentMetadata);
        static void DemandSpecializationTraits(const std::vector<uint32_t>& ids);

        // Parse helpers
        static Character ParseCharacterJson(const nlohmann::json& cj);
        static Character ParseVerifiedCore(const std::string& name,const nlohmann::json& core);
        static EquipmentItem ParseEquipmentItem(const nlohmann::json& item_json);
        static BuildTemplate ParseBuildTemplate(const nlohmann::json& tab_json);

        // Sub-endpoint fetch for current character (phase 0/1/2)
        static void RequestNativeCharacters(const std::vector<std::string>& names,bool listOnly,bool refreshCrafting,const std::string& priority,const std::string& accountOnly={},CharacterScopes::Mask demand=CharacterScopes::All,bool manual=true,
            const std::vector<CharacterIdentity::Address>& expected={},
            const std::set<CharacterActivity::Key>& automaticSeeds={});
    };

}
