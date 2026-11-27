#pragma once
#include "Database.h"

namespace AlterEgo::NativeLibrary {

constexpr int kApplicationId = 0x41454c42; // AELB
constexpr int kSchemaVersion = 6;

inline void UpgradeSchema(Database::Connection& db, int version) {
    // Existing v4 is rebuilt transactionally after a verified backup.
    // Earlier experimental schemas remain unsupported.
    if(version>=1&&version<5)return;
    if(version==6){
        db.Exec(R"SQL(
CREATE TABLE loadout_source_identity(
    loadout_id TEXT PRIMARY KEY REFERENCES loadouts(id) ON DELETE CASCADE,
    account TEXT NOT NULL, character TEXT NOT NULL, created TEXT NOT NULL,
    profession TEXT NOT NULL, race TEXT NOT NULL
) STRICT;
CREATE TABLE fashion_character_identity(
    fashion_id TEXT NOT NULL REFERENCES fashion(id) ON DELETE CASCADE,
    role INTEGER NOT NULL CHECK(role IN (0,1)),
    ordinal INTEGER NOT NULL CHECK(ordinal>=0 AND (role=1 OR ordinal=0)),
    account TEXT NOT NULL, character TEXT NOT NULL, created TEXT NOT NULL,
    profession TEXT NOT NULL, race TEXT NOT NULL,
    PRIMARY KEY(fashion_id,role,ordinal),
    UNIQUE(fashion_id,role,account,created,profession,race)
) STRICT;
)SQL");
        return;
    }
    if(version!=5)throw Database::Error(SQLITE_ERROR,"Unknown library schema");
    // Category is part of each component FK. IDs are stable within a kind.
    // Fingerprints are deliberately nonunique; independent identical records
    // are valid. Ordinals preserve user ordering, not record identity.
    db.Exec(R"SQL(
CREATE TABLE library_state(
    singleton INTEGER PRIMARY KEY CHECK(singleton=1),
    revision INTEGER NOT NULL CHECK(revision>=0)
) STRICT;
INSERT INTO library_state VALUES(1,0);
CREATE TABLE components(
    kind TEXT NOT NULL CHECK(kind IN ('base_build','skills','weapons','armor',
        'trinkets','runes','sigils','infusions','relic','aquatic')),
    id TEXT NOT NULL CHECK(length(id)>0),
    name TEXT NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0),
    fingerprint TEXT NOT NULL,
    PRIMARY KEY(kind,id)
) STRICT;
CREATE INDEX component_fingerprint ON components(kind,fingerprint);
CREATE INDEX component_order ON components(kind,ordinal);
CREATE TABLE component_payloads(
    kind TEXT NOT NULL, component_id TEXT NOT NULL, flags INTEGER NOT NULL,
    profession TEXT, rune_name TEXT, rune_id INTEGER, relic_name TEXT,
    relic_id INTEGER, infusion_summary_present INTEGER, infusion_ar_total INTEGER,
    extension_json TEXT NOT NULL,
    PRIMARY KEY(kind,component_id),
    FOREIGN KEY(kind,component_id) REFERENCES components(kind,id) ON DELETE CASCADE
) STRICT;
CREATE TABLE build_specializations(
    kind TEXT NOT NULL CHECK(kind='base_build'), component_id TEXT NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), traits_present INTEGER NOT NULL,
    spec_id INTEGER, extension_json TEXT NOT NULL,
    PRIMARY KEY(kind,component_id,ordinal),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE build_traits(
    kind TEXT NOT NULL, component_id TEXT NOT NULL, spec_ordinal INTEGER NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), selection INTEGER NOT NULL,
    PRIMARY KEY(kind,component_id,spec_ordinal,ordinal),
    FOREIGN KEY(kind,component_id,spec_ordinal)
        REFERENCES build_specializations(kind,component_id,ordinal) ON DELETE CASCADE
) STRICT;
CREATE TABLE build_choices(
    kind TEXT NOT NULL CHECK(kind='base_build'), component_id TEXT NOT NULL,
    field TEXT NOT NULL CHECK(field IN ('pets','legend_codes','weapons')),
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), value INTEGER NOT NULL,
    PRIMARY KEY(kind,component_id,field,ordinal),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE skill_bars(
    kind TEXT NOT NULL CHECK(kind='skills'), component_id TEXT NOT NULL,
    bar TEXT NOT NULL CHECK(bar IN ('skills','aquatic_skills')),
    utilities_present INTEGER NOT NULL, heal INTEGER, elite INTEGER,
    extension_json TEXT NOT NULL,
    PRIMARY KEY(kind,component_id,bar),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE utility_skills(
    kind TEXT NOT NULL, component_id TEXT NOT NULL, bar TEXT NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), skill_id INTEGER NOT NULL,
    PRIMARY KEY(kind,component_id,bar,ordinal),
    FOREIGN KEY(kind,component_id,bar) REFERENCES skill_bars(kind,component_id,bar)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE gear_slots(
    kind TEXT NOT NULL CHECK(kind IN ('weapons','armor','trinkets','runes','sigils','infusions','aquatic')),
    component_id TEXT NOT NULL, slot TEXT NOT NULL, flags INTEGER NOT NULL,
    item_id INTEGER, stat_id INTEGER, stat_name TEXT, weapon_type TEXT,
    rune_name TEXT, rune_id INTEGER, sigil_name TEXT, sigil_id INTEGER,
    sigil2_name TEXT, sigil2_id INTEGER, infusion_name TEXT, infusion_snapshot INTEGER,
    extension_json TEXT NOT NULL, raw_slot_json TEXT,
    PRIMARY KEY(kind,component_id,slot),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE gear_infusion_ids(
    kind TEXT NOT NULL CHECK(kind IN ('infusions','aquatic')), component_id TEXT NOT NULL,
    slot TEXT NOT NULL, ordinal INTEGER NOT NULL CHECK(ordinal>=0), item_id INTEGER NOT NULL,
    PRIMARY KEY(kind,component_id,slot,ordinal),
    FOREIGN KEY(kind,component_id,slot) REFERENCES gear_slots(kind,component_id,slot)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE gear_infusion_attributes(
    kind TEXT NOT NULL CHECK(kind IN ('infusions','aquatic')), component_id TEXT NOT NULL,
    slot TEXT NOT NULL, ordinal INTEGER NOT NULL CHECK(ordinal>=0), attribute TEXT NOT NULL,
    PRIMARY KEY(kind,component_id,slot,ordinal),
    FOREIGN KEY(kind,component_id,slot) REFERENCES gear_slots(kind,component_id,slot)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE infusion_requirements(
    kind TEXT NOT NULL CHECK(kind='infusions'), component_id TEXT NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), item_id INTEGER, count INTEGER,
    attribute TEXT, extension_json TEXT NOT NULL,
    PRIMARY KEY(kind,component_id,ordinal),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE infusion_stats(
    kind TEXT NOT NULL CHECK(kind='infusions'), component_id TEXT NOT NULL,
    attribute TEXT NOT NULL, total INTEGER NOT NULL,
    PRIMARY KEY(kind,component_id,attribute),
    FOREIGN KEY(kind,component_id) REFERENCES component_payloads(kind,component_id)
        ON DELETE CASCADE
) STRICT;
CREATE TABLE fashion(
    id TEXT PRIMARY KEY CHECK(length(id)>0), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
    name TEXT NOT NULL, notes TEXT NOT NULL, weight TEXT NOT NULL, origin TEXT NOT NULL,
    source_character TEXT NOT NULL, source_tab INTEGER NOT NULL,
    complete INTEGER NOT NULL CHECK(complete IN (0,1)), created INTEGER NOT NULL,
    visibility_flags INTEGER NOT NULL CHECK(visibility_flags BETWEEN 0 AND 65535),
    extension_json TEXT NOT NULL
) STRICT;
CREATE INDEX fashion_order ON fashion(ordinal);
CREATE TABLE fashion_skins(
    fashion_id TEXT NOT NULL REFERENCES fashion(id) ON DELETE CASCADE,
    slot TEXT NOT NULL, skin_id INTEGER NOT NULL CHECK(skin_id BETWEEN 0 AND 65535),
    PRIMARY KEY(fashion_id,slot)
) STRICT;
CREATE TABLE fashion_dyes(
    fashion_id TEXT NOT NULL, slot TEXT NOT NULL,
    ordinal INTEGER NOT NULL CHECK(ordinal BETWEEN 0 AND 3),
    dye_id INTEGER NOT NULL CHECK(dye_id BETWEEN 0 AND 65535),
    PRIMARY KEY(fashion_id,slot,ordinal),
    FOREIGN KEY(fashion_id,slot) REFERENCES fashion_skins(fashion_id,slot) ON DELETE CASCADE
) STRICT;
CREATE TABLE fashion_characters(
    fashion_id TEXT NOT NULL REFERENCES fashion(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), character TEXT NOT NULL,
    PRIMARY KEY(fashion_id,ordinal), UNIQUE(fashion_id,character)
) STRICT;
CREATE TABLE equipment_sets(
    id TEXT PRIMARY KEY NOT NULL CHECK(length(id)>0),
    name TEXT NOT NULL CHECK(length(name)>0),
    ordinal INTEGER NOT NULL CHECK(ordinal>=0)
) STRICT;
CREATE TABLE equipment_set_components(
    set_id TEXT NOT NULL REFERENCES equipment_sets(id) ON DELETE CASCADE,
    kind TEXT NOT NULL CHECK(kind IN ('weapons','armor','trinkets','runes','sigils','infusions','relic','aquatic')),
    component_id TEXT NOT NULL,
    PRIMARY KEY(set_id,kind),
    FOREIGN KEY(kind,component_id) REFERENCES components(kind,id) ON DELETE RESTRICT
) STRICT;
CREATE INDEX equipment_set_component_usage ON equipment_set_components(kind,component_id);
CREATE TABLE loadouts(
    id TEXT PRIMARY KEY CHECK(length(id)>0), ordinal INTEGER NOT NULL CHECK(ordinal>=0),
    fashion_id TEXT REFERENCES fashion(id) ON DELETE SET NULL,
    equipment_set_id TEXT REFERENCES equipment_sets(id) ON DELETE RESTRICT,
    modes_present INTEGER NOT NULL,
    name TEXT, notes TEXT, source_character TEXT, source_url TEXT,
    chat_link TEXT, ae2_code TEXT, game_mode TEXT, import_warning TEXT, created INTEGER,
    extension_json TEXT NOT NULL
) STRICT;
CREATE INDEX loadout_order ON loadouts(ordinal);
CREATE INDEX loadout_fashion ON loadouts(fashion_id);
CREATE INDEX loadout_equipment_set ON loadouts(equipment_set_id);
CREATE TABLE loadout_game_modes(
    loadout_id TEXT NOT NULL REFERENCES loadouts(id) ON DELETE CASCADE,
    ordinal INTEGER NOT NULL CHECK(ordinal>=0), mode TEXT NOT NULL,
    PRIMARY KEY(loadout_id,ordinal)
) STRICT;
CREATE TABLE loadout_components(
    loadout_id TEXT NOT NULL REFERENCES loadouts(id) ON DELETE CASCADE,
    kind TEXT NOT NULL, component_id TEXT NOT NULL,
    PRIMARY KEY(loadout_id,kind),
    FOREIGN KEY(kind,component_id) REFERENCES components(kind,id) ON DELETE RESTRICT
) STRICT;
CREATE INDEX component_usage ON loadout_components(kind,component_id);
CREATE TRIGGER loadout_equipment_exclusive_insert BEFORE INSERT ON loadout_components
WHEN EXISTS(SELECT 1 FROM loadouts l JOIN equipment_set_components s ON s.set_id=l.equipment_set_id
    WHERE l.id=NEW.loadout_id AND s.kind=NEW.kind)
BEGIN SELECT RAISE(ABORT,'Equipment category already owned by set'); END;
CREATE TRIGGER loadout_equipment_exclusive_update BEFORE UPDATE ON loadout_components
WHEN EXISTS(SELECT 1 FROM loadouts l JOIN equipment_set_components s ON s.set_id=l.equipment_set_id
    WHERE l.id=NEW.loadout_id AND s.kind=NEW.kind)
BEGIN SELECT RAISE(ABORT,'Equipment category already owned by set'); END;
CREATE TRIGGER loadout_equipment_exclusive_link BEFORE UPDATE OF equipment_set_id ON loadouts
WHEN EXISTS(SELECT 1 FROM loadout_components r JOIN equipment_set_components s ON s.kind=r.kind
    WHERE r.loadout_id=NEW.id AND s.set_id=NEW.equipment_set_id)
BEGIN SELECT RAISE(ABORT,'Equipment category already owned by Loadout'); END;
CREATE TRIGGER set_equipment_exclusive_insert BEFORE INSERT ON equipment_set_components
WHEN EXISTS(SELECT 1 FROM loadouts l JOIN loadout_components r ON r.loadout_id=l.id
    WHERE l.equipment_set_id=NEW.set_id AND r.kind=NEW.kind)
BEGIN SELECT RAISE(ABORT,'Equipment category already owned by Loadout'); END;
CREATE TRIGGER set_equipment_exclusive_update BEFORE UPDATE ON equipment_set_components
WHEN EXISTS(SELECT 1 FROM loadouts l JOIN loadout_components r ON r.loadout_id=l.id
    WHERE l.equipment_set_id=NEW.set_id AND r.kind=NEW.kind)
BEGIN SELECT RAISE(ABORT,'Equipment category already owned by Loadout'); END;
)SQL");
}

} // namespace AlterEgo::NativeLibrary
