#include "AccountIdentity.h"
#include "RecordSelectionCore.h"
#include "CallbackLifetime.h"
#include "UiCallbackInbox.h"
#include "BackgroundWorkers.h"
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <deque>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <set>
#include <mutex>
#include <thread>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <cctype>
#include <atomic>
#include <functional>
#include <memory>

#include "nexus/Nexus.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "GW2API.h"
#include "FashionModel.h"
#include "BuildCompareCore.h"
#include "CharacterLoadoutExpectation.h"
#include "ComposableSemantics.h"
#include "InfusionMatchCore.h"
#include "InfusionTotalsCore.h"
#include "SnowCrowsImport.h"
#include "MetaBattleImport.h"
#include "BuildRelayPolicy.h"
#include "CharacterActivity.h"
#include "CharacterOrderCore.h"
#include "CharacterSelectionCore.h"
#include "AutoRefreshPolicy.h"
#include "FontResolve.h"
#include "FontManager.h"
#include "IconManager.h"
#include "ChatLink.h"
#include "NativeProgress.h"
#include "SpecDescriptions.h"
#include "SkinCache.h"
#include "OwnedSkins.h"
#include "Commerce.h"
#include "PublicCatalog.h"
#include "WikiEventRuntime.h"
#include "SkinSearchCore.h"
#include "SkinFilters.h"
#include "LibraryModeFilter.h"
#include "WikiImage.h"
#include "HttpClient.h"
#include "ApiActivity.h"
#include "ArenaNetTransport.h"
#include <nlohmann/json.hpp>
#include "AddonShared.h"
#include "EquipmentLayout.h"
#include "CatalogPresentation.h"
#include "CharacterToolsCore.h"
#include "Clears.h"
#include "CompletionTheme.h"
#include "EquipmentTabCore.h"
#include "EquipmentChoiceCore.h"
#include "GearEditCore.h"
#include "ComponentGearEditCore.h"
#include "SkillEditCore.h"
#include "SourceRefreshCore.h"
#include "RefreshUI.h"
#include "CacheDatabase.h"
#include "AchievementCatalogCache.h"
#include "AchievementNamesCore.h"
#include "AchievementDefinitionOwner.h"
#include "UntrustedInput.h"
#include "RevisionSnapshot.h"
#include "WikiFreshness.h"
#include "StoragePaths.h"
#include "GameMemory.h"
#include "FastLoadUIState.h"
#include "NativeAccounts.h"
#include "BuildInfo.h"

// Version constants
#define V_MAJOR 1
#define V_MINOR 1
#define V_BUILD 2
#define V_REVISION 1 // Local saved-vs-live comparison build

// Quick Access icon identifiers
#define QA_ID "QA_ALTER_EGO"
#define TEX_ICON "TEX_ALTER_EGO_ICON"
#define TEX_ICON_HOVER "TEX_ALTER_EGO_ICON_HOVER"

// --- Normal icon ---
static const unsigned char ICON_NORMAL[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a,
    0xf4, 0x00, 0x00, 0x08, 0x78, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0x96, 0x5b, 0x6c, 0x23,
    0x57, 0x19, 0xc7, 0xff, 0xe7, 0xcc, 0xdd, 0x63, 0x8f, 0xed, 0xd8, 0xb1, 0x1d, 0xc7, 0x4e, 0x9c,
    0xd8, 0xd9, 0x26, 0xe9, 0x36, 0xd9, 0xdd, 0xb6, 0xbb, 0xa5, 0x2d, 0x65, 0x5b, 0xd4, 0x0b, 0x6a,
    0x41, 0x2a, 0x05, 0x04, 0x68, 0x5b, 0x84, 0xc4, 0x22, 0x24, 0x24, 0x78, 0x40, 0x45, 0x08, 0xf1,
    0x80, 0x90, 0x50, 0x79, 0x00, 0xfa, 0x80, 0xe0, 0x89, 0x4a, 0x54, 0x3c, 0x70, 0x95, 0x0a, 0x2b,
    0x54, 0xa9, 0x52, 0x8b, 0xba, 0xec, 0xb6, 0xdd, 0xed, 0xb6, 0x6c, 0xb3, 0xbb, 0xb9, 0x27, 0x76,
    0xe2, 0x24, 0xbe, 0xdf, 0x62, 0x3b, 0xe3, 0x99, 0xf1, 0xcc, 0x19, 0x1e, 0x96, 0x4a, 0xed, 0xb2,
    0xc9, 0x8a, 0x27, 0x78, 0xe8, 0xf7, 0x76, 0x8e, 0x66, 0xce, 0xf7, 0xd3, 0xff, 0xfb, 0xce, 0x77,
    0xfe, 0xc0, 0x47, 0xf1, 0x3f, 0x0e, 0xee, 0x56, 0x1f, 0x04, 0xfd, 0x5e, 0x5e, 0x12, 0x79, 0xea,
    0x51, 0x24, 0xaa, 0xf9, 0x3c, 0x3e, 0x9f, 0x2a, 0x7b, 0x3c, 0x1e, 0xc9, 0xde, 0xd3, 0x4d, 0x06,
    0x00, 0xb2, 0x2c, 0x8a, 0x87, 0x6f, 0x1b, 0x19, 0x7e, 0xec, 0xe4, 0xd1, 0xd3, 0x3d, 0xc3, 0x6a,
    0x70, 0x1c, 0x75, 0x27, 0xd3, 0x89, 0x87, 0x3e, 0xf5, 0xe0, 0xb1, 0xaf, 0x0d, 0x45, 0x07, 0xdc,
    0x72, 0x75, 0x77, 0xeb, 0xbe, 0xe3, 0x93, 0xf7, 0xe5, 0xf2, 0x95, 0x3c, 0x25, 0x04, 0xee, 0x0d,
    0xe7, 0x93, 0xfd, 0x12, 0x4b, 0xa2, 0x40, 0x9e, 0x7c, 0xf4, 0xc4, 0x23, 0x9f, 0x7e, 0xf8, 0xc4,
    0x73, 0x94, 0x77, 0x25, 0x66, 0xbb, 0xbc, 0xa6, 0xa9, 0x23, 0x9a, 0x2a, 0x8b, 0xbb, 0x5d, 0x3d,
    0xf7, 0xe6, 0x3b, 0x2b, 0x3f, 0x7b, 0xf7, 0xca, 0xda, 0xb9, 0xc9, 0x74, 0xfc, 0xd4, 0x68, 0x22,
    0x3a, 0x3d, 0x3c, 0x14, 0xf2, 0x67, 0x46, 0xa3, 0x0f, 0xd8, 0x36, 0x43, 0xb1, 0xd2, 0x28, 0x67,
    0xf3, 0x65, 0x26, 0x09, 0xbc, 0x5a, 0x69, 0xb4, 0x5f, 0x64, 0x8c, 0xe5, 0x7f, 0xfc, 0x8b, 0x3f,
    0xff, 0x94, 0xa3, 0x14, 0x0e, 0x63, 0x07, 0x03, 0x10, 0x42, 0x20, 0x0a, 0x1c, 0xfd, 0xfa, 0x97,
    0x1e, 0xfb, 0xd6, 0x57, 0xbf, 0xf8, 0xe0, 0xf3, 0x5d, 0xdd, 0x84, 0x6d, 0xdb, 0xf0, 0x79, 0x3d,
    0x20, 0x04, 0x70, 0x6c, 0xc6, 0xda, 0xdd, 0x1e, 0xa5, 0x94, 0x82, 0xe7, 0x28, 0x33, 0xad, 0x7e,
    0xdb, 0xb4, 0xfa, 0x81, 0xc4, 0x50, 0x18, 0x00, 0xb1, 0x15, 0x59, 0x84, 0xa2, 0x08, 0x7c, 0xb7,
    0x63, 0x60, 0x6e, 0x29, 0x87, 0x7b, 0xef, 0x9a, 0x42, 0x4f, 0x37, 0x97, 0x4e, 0x7f, 0xef, 0x97,
    0x4f, 0x9c, 0x7f, 0x7b, 0x71, 0x9d, 0x10, 0x02, 0xd7, 0x75, 0x6f, 0x0e, 0xc0, 0x71, 0x14, 0x8e,
    0xc3, 0x70, 0xf7, 0x6c, 0x26, 0xfd, 0x83, 0x6f, 0x7f, 0x61, 0x6d, 0x30, 0x1c, 0xb0, 0x2f, 0x5f,
    0xcd, 0x32, 0xc7, 0x61, 0xbc, 0x57, 0x95, 0xe0, 0xf7, 0xa9, 0x68, 0x77, 0x74, 0xba, 0x53, 0xae,
    0xb3, 0x5e, 0xaf, 0x6f, 0xa9, 0xaa, 0x24, 0x06, 0x7c, 0x0a, 0xcd, 0xa4, 0xe2, 0x4c, 0x51, 0x44,
    0x48, 0xa2, 0x40, 0x8b, 0x95, 0x16, 0xbc, 0x1e, 0x89, 0x4d, 0x4d, 0x24, 0x21, 0xf0, 0x1c, 0xcd,
    0xef, 0x54, 0xad, 0x63, 0x77, 0xa4, 0xc5, 0x2b, 0x0b, 0x1b, 0x2b, 0x4f, 0x9e, 0x7e, 0xee, 0x48,
    0x36, 0x5f, 0xee, 0x7d, 0x30, 0x27, 0xbd, 0x99, 0xfc, 0x1e, 0x45, 0xf6, 0x74, 0xbb, 0x86, 0x05,
    0xc6, 0xa8, 0x2a, 0x4b, 0x7c, 0x24, 0xa4, 0xd1, 0xc3, 0x87, 0x46, 0x68, 0x50, 0x53, 0x69, 0x3c,
    0x36, 0x80, 0x07, 0x8e, 0xdf, 0x4e, 0x63, 0x83, 0x7e, 0xd9, 0x30, 0x2c, 0x94, 0xab, 0x2d, 0x76,
    0x75, 0x69, 0x93, 0xf6, 0x6d, 0x46, 0x05, 0x9e, 0x43, 0xbf, 0x6f, 0xa3, 0xdd, 0xe9, 0xd1, 0xbf,
    0xbe, 0x72, 0x91, 0xb6, 0x76, 0xf7, 0x10, 0x09, 0x07, 0xc4, 0x5a, 0xb3, 0xa3, 0xcf, 0xcc, 0x4e,
    0x1c, 0x9a, 0x99, 0x4a, 0x1d, 0xbd, 0x31, 0x17, 0xff, 0xa1, 0xd5, 0xbf, 0x95, 0xd1, 0x0d, 0xb3,
    0x76, 0xe4, 0x8e, 0x71, 0x63, 0x22, 0x93, 0xd4, 0xee, 0x39, 0x71, 0x3b, 0x5b, 0x58, 0xc8, 0xe1,
    0xec, 0x5b, 0xf3, 0x38, 0x7e, 0x64, 0x02, 0x5d, 0xdd, 0x40, 0x30, 0xe0, 0xc5, 0x54, 0x26, 0x89,
    0xde, 0x7c, 0x96, 0xba, 0x8c, 0x21, 0x14, 0xf0, 0x61, 0x7e, 0x39, 0x0f, 0x59, 0x12, 0x90, 0x4a,
    0x44, 0x70, 0xe7, 0x4c, 0x06, 0xf5, 0x46, 0x07, 0x8b, 0x6b, 0x79, 0x3c, 0x78, 0xef, 0x0c, 0x4a,
    0x95, 0x26, 0x78, 0x8e, 0x32, 0x55, 0x91, 0x71, 0x20, 0x00, 0xa1, 0x14, 0x60, 0x0c, 0xc7, 0xef,
    0x48, 0xa7, 0xa6, 0x8f, 0x4f, 0x69, 0xcc, 0x72, 0x6c, 0x18, 0x16, 0x3f, 0x96, 0x88, 0x62, 0x3d,
    0x5e, 0xc6, 0x4a, 0xb6, 0x80, 0xc1, 0x90, 0x86, 0x42, 0xa9, 0x8e, 0x80, 0x5f, 0xc5, 0xf4, 0x44,
    0x02, 0x3d, 0xc3, 0x84, 0xcf, 0xeb, 0x41, 0x46, 0x55, 0xd0, 0x68, 0xb5, 0x51, 0xaa, 0x34, 0xb1,
    0x55, 0xa8, 0x42, 0x91, 0x25, 0xa8, 0x1e, 0x05, 0xc5, 0x4a, 0x83, 0xa9, 0x1e, 0x45, 0xce, 0xe6,
    0x4b, 0x74, 0xa7, 0x5c, 0xcf, 0xde, 0x08, 0xf0, 0xa1, 0x12, 0x38, 0x8e, 0x03, 0x42, 0x08, 0xb9,
    0xff, 0xfe, 0x99, 0x27, 0x20, 0x49, 0x70, 0xac, 0x3e, 0x23, 0x0e, 0x43, 0xbb, 0xab, 0x23, 0x16,
    0xf6, 0x23, 0x16, 0x09, 0x82, 0x10, 0x80, 0x52, 0x82, 0xd5, 0x5c, 0x11, 0xdb, 0xc5, 0x1a, 0x34,
    0x9f, 0x17, 0xb5, 0x7a, 0x1b, 0xd7, 0x96, 0x36, 0xb0, 0xb9, 0x55, 0xc5, 0x7b, 0x0b, 0x39, 0x54,
    0xeb, 0x6d, 0x24, 0xe3, 0x61, 0xc4, 0x06, 0x03, 0x98, 0x5f, 0xd9, 0xa2, 0xae, 0xcb, 0xac, 0xd5,
    0x6c, 0x11, 0xdb, 0xc5, 0x3a, 0x39, 0x10, 0xc0, 0x75, 0x5d, 0xf8, 0x54, 0x99, 0xf6, 0x0d, 0x33,
    0xb9, 0x70, 0x7e, 0x0e, 0x9d, 0x6a, 0x13, 0x2e, 0x01, 0xf6, 0x74, 0x13, 0xad, 0xf6, 0x1e, 0x14,
    0x59, 0x02, 0x63, 0x2e, 0x08, 0xa1, 0x08, 0x68, 0x2a, 0x8a, 0x95, 0x26, 0x28, 0x05, 0xee, 0x39,
    0x76, 0x08, 0xd1, 0x48, 0x10, 0xc9, 0x78, 0x18, 0xcf, 0x3c, 0x75, 0x12, 0x43, 0xd1, 0x01, 0x2c,
    0xad, 0x6d, 0xc3, 0x61, 0x0c, 0xb3, 0x53, 0x29, 0x56, 0x2c, 0x37, 0x44, 0xb8, 0x6e, 0x2b, 0xa0,
    0xa9, 0xc6, 0x81, 0x00, 0x00, 0x20, 0x08, 0x3c, 0xf6, 0xba, 0x86, 0xd2, 0xa8, 0xb5, 0xb0, 0xbd,
    0x53, 0x45, 0xdf, 0xb4, 0x90, 0x1e, 0x1f, 0x86, 0xc3, 0x5c, 0x5c, 0xbc, 0xbc, 0x8c, 0x64, 0x7c,
    0x10, 0xa1, 0xa0, 0x0f, 0xe5, 0x6a, 0x0b, 0x7e, 0x4d, 0x45, 0xa1, 0xd4, 0x80, 0x2c, 0x8b, 0x48,
    0x8f, 0xc4, 0x90, 0x4a, 0x46, 0xb1, 0xba, 0x51, 0xc2, 0x5b, 0xef, 0x2e, 0x61, 0x6b, 0xa7, 0x06,
    0x91, 0xe7, 0x51, 0xa9, 0xef, 0x62, 0x6d, 0xb3, 0x44, 0x25, 0x99, 0x6f, 0x52, 0x8e, 0x5a, 0x07,
    0x37, 0x21, 0x80, 0x93, 0x1f, 0x3b, 0x3c, 0xf9, 0x95, 0x2f, 0x3f, 0xf2, 0xb9, 0x76, 0x47, 0x67,
    0x03, 0x61, 0xbf, 0x28, 0x08, 0x3c, 0x2c, 0xdd, 0x04, 0x47, 0x09, 0xc6, 0x92, 0x11, 0x10, 0x42,
    0x10, 0x8b, 0x04, 0x50, 0xac, 0x36, 0xd1, 0x6c, 0x75, 0xb1, 0x9a, 0x2d, 0x60, 0x2a, 0x93, 0x80,
    0xcd, 0x1c, 0x98, 0xa6, 0x0d, 0x4a, 0x29, 0xa6, 0x32, 0x09, 0xa4, 0x92, 0x51, 0x38, 0x94, 0x60,
    0x25, 0xb7, 0x63, 0x25, 0xa2, 0x21, 0x79, 0x61, 0x2d, 0xff, 0x97, 0xb9, 0xf9, 0x9c, 0x7e, 0x20,
    0x40, 0x34, 0xec, 0xe7, 0x7e, 0xf4, 0xec, 0xa9, 0x17, 0x14, 0xaf, 0x02, 0xc9, 0xeb, 0x61, 0x84,
    0x23, 0xb4, 0x5b, 0x6b, 0xa3, 0x5e, 0x6b, 0x62, 0x63, 0xab, 0x82, 0x99, 0xe9, 0x14, 0x82, 0x7e,
    0x2f, 0x36, 0xb7, 0x2b, 0x28, 0x94, 0x1a, 0xb0, 0xac, 0x3e, 0xac, 0xbe, 0x83, 0xd7, 0xde, 0xb8,
    0x82, 0xc7, 0x3f, 0x79, 0x17, 0x14, 0x89, 0x61, 0x4f, 0x37, 0x30, 0x10, 0xf4, 0x22, 0x1c, 0x09,
    0x40, 0x0c, 0x07, 0x10, 0x0c, 0xf8, 0xa8, 0x20, 0x09, 0x00, 0xc7, 0xc5, 0x14, 0x59, 0x24, 0xa6,
    0xd5, 0x77, 0xf7, 0x7d, 0x0b, 0x24, 0x51, 0xa4, 0xc1, 0x80, 0xfa, 0xb8, 0x4a, 0xe9, 0xa1, 0x78,
    0x6c, 0x80, 0x30, 0xcb, 0x26, 0xa5, 0x42, 0x1d, 0x9b, 0x85, 0x1a, 0xea, 0xcd, 0x36, 0x08, 0x21,
    0x38, 0x34, 0x3e, 0x8c, 0x6c, 0xbe, 0x84, 0xad, 0x42, 0x15, 0xbb, 0x1d, 0x1d, 0x84, 0x10, 0xb4,
    0xdb, 0x3a, 0xea, 0xad, 0x0e, 0x06, 0x43, 0x1a, 0xbc, 0xaa, 0x8c, 0x9e, 0x61, 0x41, 0xa4, 0x1c,
    0x04, 0x81, 0x87, 0xa2, 0xca, 0x84, 0x12, 0xea, 0x24, 0xe3, 0xa1, 0xd9, 0x37, 0x2e, 0x2c, 0xfc,
    0x76, 0x35, 0x57, 0x68, 0xee, 0x0b, 0xd0, 0x33, 0x2c, 0xd7, 0xb2, 0xfa, 0xf5, 0x13, 0xc7, 0x6e,
    0x7b, 0xc6, 0x23, 0xf2, 0x26, 0x25, 0x94, 0x5f, 0xdf, 0x2c, 0xc1, 0x75, 0x19, 0x7a, 0x86, 0x85,
    0x6c, 0xbe, 0x84, 0x9e, 0x69, 0x21, 0x1a, 0x0e, 0x40, 0xf3, 0x29, 0x48, 0x25, 0x63, 0xb8, 0xba,
    0xb8, 0x81, 0xe3, 0xb3, 0x19, 0x88, 0xb2, 0x88, 0xb7, 0xde, 0x5d, 0xc6, 0x64, 0x7a, 0x18, 0x82,
    0xc0, 0xc3, 0xe7, 0xf5, 0xc0, 0x31, 0x2c, 0x78, 0x14, 0xc9, 0x75, 0x4c, 0xdb, 0x3d, 0x77, 0x7e,
    0xee, 0xf2, 0x99, 0x57, 0x2f, 0xfd, 0x6e, 0xa7, 0x54, 0xdf, 0x3d, 0xb0, 0x07, 0x98, 0x8b, 0x6b,
    0xf1, 0xd8, 0x00, 0x36, 0xb7, 0x2b, 0x54, 0x14, 0x05, 0x66, 0xf4, 0x4c, 0x4a, 0xf9, 0xeb, 0xbd,
    0x5a, 0xa9, 0xed, 0x42, 0xf3, 0x2a, 0x00, 0x21, 0x88, 0x84, 0x03, 0x08, 0x6a, 0x2a, 0xfa, 0x76,
    0x1f, 0x96, 0x69, 0x83, 0x17, 0x38, 0xf8, 0x35, 0x0f, 0x96, 0xd6, 0xb6, 0x31, 0x35, 0x91, 0x84,
    0x69, 0xf5, 0xb1, 0xa7, 0x9b, 0x76, 0x30, 0xe4, 0x47, 0xa7, 0xd3, 0xb5, 0x7e, 0xfe, 0xc2, 0x99,
    0x6f, 0xbe, 0xfd, 0xde, 0x4a, 0x9e, 0x52, 0x0a, 0xf6, 0x81, 0x07, 0xe9, 0x3f, 0x6e, 0x81, 0xcf,
    0x2b, 0x87, 0x24, 0x49, 0x84, 0xed, 0x38, 0xa8, 0x35, 0xda, 0xb4, 0x6b, 0x98, 0x68, 0xed, 0xea,
    0xa8, 0x35, 0xdb, 0xd0, 0x7c, 0x1e, 0xf8, 0x7d, 0x2a, 0xc2, 0x41, 0x1f, 0x06, 0x43, 0x7e, 0xb4,
    0x3b, 0x3a, 0x08, 0x08, 0x24, 0x49, 0x40, 0x72, 0x28, 0x84, 0x87, 0xef, 0x9f, 0x85, 0x69, 0xf6,
    0xf1, 0xca, 0xd9, 0xcb, 0x58, 0x5c, 0xdd, 0x82, 0xeb, 0x32, 0x9e, 0xf7, 0xab, 0xfc, 0xe5, 0xab,
    0xeb, 0x2f, 0xff, 0xe3, 0xc2, 0xc2, 0x25, 0x8e, 0xa3, 0x60, 0xae, 0x7b, 0xf0, 0x2d, 0x48, 0x0c,
    0x85, 0x0e, 0xc1, 0x65, 0xac, 0x58, 0x69, 0xb2, 0x7a, 0xa3, 0xcb, 0x40, 0x09, 0x7c, 0x1e, 0x99,
    0xf2, 0x94, 0xc3, 0xdd, 0x33, 0x13, 0x08, 0x0f, 0x68, 0xb0, 0xfb, 0x0e, 0x38, 0x4a, 0xb0, 0x53,
    0xaa, 0xe3, 0xea, 0xd2, 0x06, 0x1e, 0xfd, 0xc4, 0x51, 0xc8, 0x92, 0x88, 0x91, 0xf8, 0x20, 0xa2,
    0x83, 0x01, 0xbc, 0x76, 0x7e, 0x0e, 0x7f, 0xfa, 0xdb, 0x79, 0x3b, 0x1a, 0x09, 0xfe, 0xfe, 0x14,
    0x73, 0x3f, 0x93, 0xdb, 0xac, 0x6c, 0xb6, 0xbb, 0x3a, 0xe3, 0x79, 0x0e, 0x70, 0xd9, 0xc1, 0x00,
    0xf3, 0x2b, 0x5b, 0x5b, 0x85, 0x72, 0x93, 0x96, 0x6a, 0xbb, 0x46, 0xbf, 0x6f, 0xcb, 0x3c, 0xcf,
    0x61, 0xb3, 0xd8, 0x65, 0x1f, 0xbf, 0x6b, 0x92, 0x8e, 0x26, 0xa3, 0xb0, 0xcc, 0x3e, 0x5c, 0xb8,
    0x58, 0xc9, 0x16, 0x30, 0xb7, 0xb0, 0x81, 0x63, 0x87, 0xd3, 0x48, 0x0c, 0x85, 0x01, 0x42, 0xd0,
    0x68, 0x75, 0x20, 0x8a, 0x02, 0xee, 0x3c, 0x9c, 0xd6, 0x4b, 0xd5, 0x96, 0xe7, 0xf9, 0x5f, 0x9f,
    0xf9, 0xce, 0x8b, 0x7f, 0xf8, 0xfb, 0xe9, 0xf4, 0xd8, 0xd0, 0x30, 0x21, 0x04, 0x8e, 0xc3, 0x70,
    0xcb, 0x41, 0xb4, 0x9e, 0x2b, 0x2c, 0x2c, 0xae, 0x6f, 0xb7, 0x7c, 0xaa, 0x12, 0xd8, 0x29, 0xd4,
    0x2a, 0xaf, 0x9d, 0xbb, 0xf2, 0x8d, 0xc5, 0x95, 0xed, 0x9f, 0xc4, 0x63, 0x21, 0x50, 0x4a, 0xac,
    0x9e, 0xd9, 0xc7, 0xdb, 0x73, 0x6b, 0x38, 0x7b, 0x61, 0x1e, 0x99, 0xb1, 0x38, 0x92, 0xf1, 0x30,
    0x24, 0x51, 0x40, 0x2c, 0x12, 0x00, 0xaf, 0x48, 0xa8, 0x54, 0x9b, 0xcc, 0xb2, 0xfb, 0xfc, 0x54,
    0x26, 0x09, 0x8e, 0x52, 0xa3, 0x5c, 0xdf, 0x35, 0xde, 0x7c, 0x67, 0x69, 0xdd, 0x75, 0xdd, 0x0f,
    0xf9, 0x80, 0x7d, 0x15, 0xa8, 0x36, 0x3a, 0x7b, 0xbf, 0xfa, 0xcd, 0xcb, 0xb3, 0xe1, 0x01, 0xed,
    0xc4, 0xda, 0x46, 0xf1, 0xf5, 0xcd, 0xed, 0x6a, 0xf5, 0xf3, 0x8f, 0xdf, 0xfb, 0x6c, 0xb7, 0xab,
    0xa3, 0x58, 0x32, 0xd8, 0x5a, 0xbe, 0x84, 0x73, 0x17, 0xe7, 0xaf, 0xcf, 0xfa, 0x48, 0x10, 0xb1,
    0x48, 0x00, 0x94, 0x52, 0x58, 0x7d, 0x07, 0xb7, 0x4f, 0xa7, 0x60, 0x8c, 0xc7, 0xe9, 0xa5, 0x0b,
    0xf3, 0x18, 0x1f, 0x8d, 0x62, 0x66, 0x72, 0x74, 0xe0, 0xf5, 0x0b, 0xf3, 0xed, 0x1b, 0x1b, 0xef,
    0x96, 0x9e, 0xb0, 0x5c, 0x6d, 0xed, 0xe6, 0xf2, 0xe5, 0x85, 0xdd, 0xb6, 0xae, 0x27, 0x86, 0x42,
    0xa3, 0x4f, 0x7f, 0xf6, 0xa1, 0x97, 0x5c, 0xd7, 0x75, 0x96, 0x73, 0x45, 0x61, 0x7e, 0x79, 0x83,
    0xc8, 0xb2, 0x8c, 0xc9, 0x74, 0x02, 0x92, 0xc8, 0x63, 0x65, 0xbd, 0x80, 0xf9, 0x95, 0x3c, 0xd6,
    0x37, 0x8a, 0xc8, 0xae, 0x6d, 0xa3, 0xb3, 0x67, 0x22, 0x93, 0x8a, 0xb9, 0x91, 0xb0, 0x9f, 0xba,
    0xae, 0x6b, 0xbd, 0x7a, 0x7e, 0xee, 0x55, 0xdb, 0x76, 0x40, 0xf6, 0x31, 0x7f, 0xfc, 0xcd, 0x36,
    0x29, 0xa5, 0xb8, 0x6e, 0x9d, 0x18, 0xd2, 0xa3, 0xb1, 0x23, 0x63, 0x23, 0x11, 0xbe, 0x58, 0x6d,
    0x58, 0x9d, 0xee, 0x1e, 0xd5, 0xbc, 0x2a, 0x42, 0x03, 0x1a, 0x6a, 0x8d, 0x36, 0xe6, 0x97, 0xf3,
    0xa0, 0x1c, 0x81, 0x24, 0x0a, 0x70, 0xe1, 0xa2, 0xdf, 0x77, 0xa0, 0x65, 0x77, 0x70, 0x6d, 0xc0,
    0x6f, 0x25, 0xe3, 0xe1, 0x2e, 0xa5, 0x64, 0x8c, 0xe3, 0x38, 0x02, 0xd8, 0xee, 0x7e, 0xde, 0x73,
    0x5f, 0x53, 0x4a, 0x09, 0x01, 0x73, 0x5d, 0x8c, 0x25, 0xa3, 0xfe, 0x53, 0x4f, 0x3e, 0x70, 0xa9,
    0xa3, 0x1b, 0x13, 0x84, 0x10, 0xbb, 0xd7, 0x33, 0x79, 0xcb, 0xb2, 0x91, 0x1c, 0x0e, 0x63, 0x30,
    0xe4, 0x07, 0xcf, 0x71, 0xa0, 0x94, 0x40, 0x91, 0x44, 0x10, 0x4a, 0x59, 0xbb, 0xb3, 0x47, 0x8b,
    0x95, 0xe6, 0xfa, 0xb5, 0xa5, 0x8d, 0x1f, 0xbe, 0x72, 0xf6, 0xbd, 0x3f, 0xea, 0x86, 0x65, 0xdd,
    0xe8, 0x03, 0x6f, 0xa9, 0xc0, 0xf5, 0x81, 0xe4, 0x42, 0x96, 0x04, 0xf2, 0xf4, 0x53, 0x27, 0xbf,
    0x3b, 0x3d, 0x91, 0x48, 0xe6, 0xb6, 0x2b, 0x8c, 0xe7, 0x38, 0x6a, 0x33, 0xc6, 0x42, 0x9a, 0x97,
    0x8e, 0xa5, 0x62, 0x10, 0x28, 0x07, 0xc6, 0x1c, 0x10, 0x42, 0x40, 0x39, 0x0a, 0x91, 0xe7, 0x21,
    0x4a, 0x82, 0xad, 0x79, 0x95, 0xb1, 0x52, 0xab, 0xfb, 0xf4, 0xb5, 0xe5, 0xad, 0x33, 0xab, 0x1b,
    0x45, 0x8b, 0x10, 0x60, 0x9f, 0xfc, 0x37, 0x07, 0x78, 0xff, 0x07, 0x49, 0x12, 0xb9, 0x63, 0x87,
    0xc7, 0xbf, 0x9f, 0x4e, 0x0d, 0x61, 0x7a, 0x62, 0x04, 0x7d, 0xdb, 0x81, 0x61, 0x5a, 0xf0, 0xfb,
    0x3c, 0x88, 0x46, 0x02, 0x60, 0xce, 0xf5, 0x53, 0x65, 0x49, 0x80, 0xe6, 0xf5, 0x00, 0x94, 0x50,
    0x08, 0x1c, 0x05, 0xcf, 0x21, 0x23, 0xf0, 0x8f, 0x0c, 0xc7, 0xc3, 0x23, 0xab, 0x1b, 0xc5, 0x6b,
    0xd7, 0x85, 0x76, 0xff, 0xbb, 0x12, 0xbc, 0x6f, 0xd1, 0xe3, 0x91, 0xa0, 0x6f, 0x76, 0x7a, 0xec,
    0xce, 0xb1, 0x64, 0xf4, 0xee, 0x58, 0x24, 0x70, 0xd4, 0xa3, 0x88, 0xf1, 0xf1, 0x64, 0x74, 0x72,
    0x78, 0x38, 0x12, 0x62, 0x8c, 0x81, 0xb9, 0x0c, 0x22, 0xcf, 0xe9, 0xc4, 0x25, 0xeb, 0xcd, 0xdd,
    0x4e, 0x61, 0xab, 0xd8, 0xb8, 0xb2, 0xb0, 0x9c, 0xff, 0xe7, 0xeb, 0x17, 0xaf, 0xbe, 0x39, 0xb7,
    0xb8, 0x59, 0xb4, 0xac, 0xfd, 0xeb, 0xff, 0x51, 0xfc, 0x5f, 0xc4, 0xbf, 0x00, 0x84, 0xd2, 0xe3,
    0x91, 0xda, 0xe1, 0x80, 0x39, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82,
};
static const unsigned int ICON_NORMAL_size = 2225;

// --- Hover icon ---
static const unsigned char ICON_HOVER[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a,
    0xf4, 0x00, 0x00, 0x09, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0x57, 0x59, 0x6c, 0x5c,
    0xd5, 0x19, 0xfe, 0xce, 0xb9, 0xcb, 0xdc, 0xd9, 0xf7, 0xeb, 0x19, 0x7b, 0xc6, 0x1e, 0x6f, 0x71,
    0x9a, 0xd8, 0x69, 0x82, 0x03, 0x34, 0x90, 0x14, 0xd2, 0x20, 0x40, 0x90, 0x46, 0x42, 0xa2, 0x21,
    0xaa, 0xda, 0x2a, 0x50, 0x55, 0xa2, 0x02, 0x95, 0x3e, 0xb4, 0x95, 0x8a, 0x54, 0xb5, 0xaa, 0x78,
    0x40, 0x95, 0x78, 0x40, 0x42, 0x7d, 0x40, 0xad, 0x9a, 0x96, 0xa5, 0x0a, 0x2d, 0x4b, 0x25, 0x42,
    0x49, 0x20, 0x69, 0x44, 0x48, 0x69, 0x36, 0x87, 0x2c, 0xc6, 0x8e, 0x1d, 0xdb, 0xf1, 0x36, 0x63,
    0x8f, 0x67, 0x9f, 0x3b, 0x77, 0xbf, 0xe7, 0xf6, 0xa1, 0xa2, 0x4a, 0x1c, 0x0c, 0x8f, 0xed, 0x03,
    0xff, 0xdb, 0x39, 0x2f, 0xff, 0x77, 0xbe, 0xef, 0x3b, 0xff, 0xf9, 0x0e, 0xf0, 0x65, 0xfd, 0x8f,
    0x8b, 0xac, 0xde, 0xa0, 0x84, 0x80, 0xb9, 0x2e, 0xbc, 0x92, 0x48, 0xf6, 0x7e, 0x73, 0xfb, 0x8e,
    0x9d, 0xdb, 0x06, 0xf7, 0x36, 0x1a, 0xad, 0x86, 0xe0, 0x11, 0x43, 0xc9, 0x68, 0xa8, 0x57, 0xf2,
    0x09, 0x94, 0x59, 0xf6, 0xf2, 0x91, 0x13, 0x17, 0x0f, 0xbc, 0x7d, 0xec, 0xdc, 0xe9, 0xb0, 0xdf,
    0x9b, 0xd9, 0xbb, 0x67, 0xfb, 0xbe, 0x70, 0xd0, 0xe7, 0x76, 0x66, 0xe4, 0x3b, 0x83, 0x61, 0x5f,
    0xae, 0x56, 0x51, 0x96, 0x55, 0x45, 0x65, 0xad, 0x96, 0xee, 0x97, 0xfc, 0x12, 0x3d, 0x7d, 0x6e,
    0xfc, 0x37, 0xd9, 0x8e, 0x44, 0xf7, 0xef, 0xfe, 0x7c, 0xec, 0xf9, 0xab, 0xb3, 0x05, 0xd5, 0x75,
    0xdd, 0xcf, 0x06, 0x40, 0x29, 0x05, 0x63, 0x0c, 0xf1, 0x48, 0x40, 0xf8, 0xf9, 0x93, 0x0f, 0xff,
    0x6c, 0xff, 0xb7, 0x76, 0x3d, 0xc3, 0xf3, 0x14, 0x3e, 0xaf, 0x00, 0xca, 0x09, 0x70, 0x01, 0xf0,
    0x3e, 0x11, 0x46, 0x43, 0x85, 0xa2, 0xe8, 0x28, 0xd6, 0x94, 0x53, 0xa6, 0x69, 0xea, 0xc5, 0x95,
    0xca, 0x50, 0x6f, 0xb6, 0x4d, 0xec, 0xe9, 0xed, 0x08, 0xd8, 0xa1, 0x00, 0x78, 0xc2, 0x01, 0x9a,
    0x86, 0x0b, 0x17, 0x27, 0x90, 0x9f, 0x5f, 0x61, 0xeb, 0x37, 0xf6, 0xd8, 0x52, 0x30, 0xb0, 0xf8,
    0xc4, 0x8f, 0x9f, 0x7b, 0xf0, 0xcd, 0xbf, 0x9f, 0x1a, 0x5b, 0x93, 0x01, 0x42, 0x08, 0x44, 0x81,
    0x23, 0x4f, 0x3c, 0xfa, 0xc0, 0xe3, 0xcf, 0xfc, 0xe4, 0xbb, 0xbf, 0x6d, 0xe9, 0xba, 0xfe, 0xc9,
    0xe4, 0x3c, 0xef, 0x58, 0x0c, 0xe1, 0x80, 0x0f, 0xd1, 0x88, 0x8f, 0xd6, 0x1a, 0x2a, 0x16, 0x0a,
    0x15, 0x30, 0xd8, 0x2c, 0xe8, 0xf3, 0xf3, 0xe1, 0x90, 0x17, 0xc9, 0x58, 0x84, 0x79, 0x3d, 0x3c,
    0x65, 0x8e, 0x03, 0xdb, 0x21, 0xcc, 0x75, 0x1d, 0xc8, 0x99, 0x24, 0x84, 0xf6, 0x4e, 0xe4, 0xc7,
    0x27, 0x41, 0x79, 0x9e, 0xa5, 0xfb, 0xfb, 0xf8, 0xe9, 0x4b, 0xa3, 0xe7, 0x1e, 0x7d, 0xec, 0xd7,
    0xbb, 0x3f, 0x38, 0x3b, 0xbe, 0xf4, 0x69, 0x4f, 0x6e, 0xb5, 0x04, 0x99, 0xb6, 0xb8, 0xe7, 0xdb,
    0x7b, 0xee, 0x7a, 0xae, 0x5d, 0x0e, 0x77, 0xac, 0x14, 0xab, 0xd6, 0xf8, 0xe4, 0x9c, 0x27, 0x14,
    0xf2, 0xd3, 0x58, 0x3c, 0x44, 0xd5, 0x96, 0x4e, 0x0c, 0xcb, 0x22, 0xa6, 0xed, 0x90, 0x7c, 0xa1,
    0x86, 0xe5, 0x95, 0x8a, 0x53, 0x58, 0xae, 0xb0, 0x52, 0xb5, 0xc9, 0xc5, 0xa3, 0x41, 0x96, 0xce,
    0xb4, 0x11, 0x49, 0x12, 0xc9, 0xd4, 0xec, 0x12, 0x69, 0x95, 0x6b, 0x64, 0x69, 0x61, 0x89, 0xa4,
    0x87, 0x36, 0x10, 0xde, 0x54, 0x60, 0xe8, 0xb6, 0x95, 0xea, 0x19, 0xca, 0x66, 0x93, 0x7c, 0xe0,
    0xa5, 0x83, 0x47, 0x0f, 0xad, 0x09, 0x40, 0x10, 0x78, 0xb2, 0x7d, 0xeb, 0xba, 0xdb, 0x87, 0xb6,
    0x0e, 0x0c, 0xfb, 0x12, 0x49, 0xc7, 0x1f, 0x89, 0xf2, 0x61, 0x9f, 0x80, 0x90, 0xcf, 0x8b, 0xa4,
    0x1c, 0x83, 0x47, 0x14, 0x70, 0xcb, 0x50, 0x2f, 0xbc, 0x02, 0x4f, 0xca, 0x35, 0x85, 0xba, 0x20,
    0xd4, 0x76, 0x1c, 0xe8, 0x86, 0x45, 0x0c, 0xdd, 0x84, 0x61, 0x98, 0x20, 0xb6, 0x03, 0x9f, 0x2c,
    0x63, 0x61, 0xa9, 0x0c, 0x5e, 0x6b, 0x21, 0x3d, 0x90, 0x23, 0x1c, 0x1c, 0x17, 0x30, 0x99, 0xe3,
    0x97, 0xb8, 0x17, 0x9e, 0x3f, 0xf8, 0xe2, 0x7f, 0x65, 0xbf, 0xd9, 0x84, 0xae, 0xdb, 0xdd, 0x9f,
    0x53, 0x12, 0x7d, 0xb7, 0x21, 0xd6, 0xb9, 0x11, 0x5f, 0xbd, 0xe3, 0x6e, 0x24, 0x3a, 0x64, 0x9c,
    0x1b, 0xb9, 0x02, 0xcb, 0xb2, 0xc0, 0x81, 0xa2, 0x56, 0x6f, 0xa1, 0xa7, 0xbb, 0x1d, 0x5d, 0xed,
    0x49, 0x78, 0x44, 0x0e, 0xb9, 0x8c, 0x0c, 0x39, 0x11, 0x46, 0x61, 0xb9, 0x82, 0x7c, 0xbe, 0x84,
    0x74, 0x56, 0x46, 0x6e, 0xf3, 0x00, 0xee, 0xbe, 0x6b, 0x18, 0xa4, 0x59, 0x47, 0xb3, 0xae, 0xc3,
    0x25, 0x12, 0xec, 0x72, 0x81, 0xb7, 0x35, 0x75, 0xe5, 0xfa, 0x7e, 0xfc, 0x6a, 0x00, 0xe9, 0x0e,
    0x59, 0xda, 0x74, 0xe7, 0x5d, 0xf7, 0x02, 0x7e, 0xb8, 0x4e, 0x85, 0x3a, 0x54, 0x47, 0xb2, 0x33,
    0x8d, 0x81, 0x81, 0x2c, 0xc6, 0xae, 0xcc, 0xa1, 0xb3, 0x3d, 0x81, 0x7a, 0xb1, 0x85, 0x70, 0xc8,
    0x8f, 0xfe, 0x9e, 0x76, 0xa4, 0xe4, 0x28, 0x98, 0xeb, 0x22, 0x18, 0xf0, 0xa2, 0xa7, 0xb3, 0x0d,
    0x53, 0xd7, 0x0a, 0x38, 0x79, 0x6e, 0x1c, 0x5f, 0xa7, 0x04, 0x61, 0x39, 0x0a, 0x49, 0x4e, 0xa2,
    0xb5, 0x50, 0x80, 0xb4, 0xae, 0x8b, 0x32, 0x07, 0xb8, 0xfc, 0xe1, 0xf9, 0xcb, 0x9f, 0x0b, 0x60,
    0xd3, 0xc6, 0xbe, 0xbe, 0x44, 0x5b, 0x7a, 0x1d, 0xb3, 0x5b, 0x36, 0x2f, 0xba, 0x3c, 0x21, 0x04,
    0xfa, 0x52, 0x1d, 0x19, 0x39, 0x8a, 0x48, 0x38, 0x08, 0x5d, 0xd7, 0x11, 0xf4, 0x49, 0x58, 0x29,
    0x55, 0x31, 0x76, 0x35, 0x8f, 0xfe, 0xae, 0x76, 0x04, 0x43, 0x5e, 0x5c, 0xfa, 0x64, 0x06, 0x96,
    0xc3, 0x50, 0x57, 0x54, 0x34, 0x6a, 0x0a, 0x8c, 0x5b, 0x06, 0x80, 0x58, 0x0a, 0x71, 0xc3, 0xc5,
    0xd5, 0x0b, 0x63, 0xf0, 0xb7, 0x47, 0x99, 0x56, 0x6f, 0xd1, 0xcb, 0x17, 0xa6, 0x53, 0x9f, 0x0b,
    0x20, 0x15, 0xf1, 0xcb, 0x64, 0xf1, 0x22, 0xdf, 0x84, 0x68, 0xfa, 0xd2, 0x69, 0x2a, 0x88, 0x02,
    0x14, 0x45, 0x85, 0x5e, 0x53, 0xe0, 0x0b, 0xf9, 0xa1, 0xa9, 0x80, 0xcd, 0x1c, 0xc8, 0x89, 0x28,
    0x46, 0xaf, 0xcc, 0x61, 0x79, 0xa5, 0x84, 0xa1, 0xaf, 0x7d, 0x1d, 0x71, 0x39, 0x82, 0xf9, 0xa9,
    0x3c, 0x42, 0x91, 0x2e, 0xc8, 0xdd, 0x69, 0x80, 0x31, 0x34, 0x26, 0x26, 0x21, 0x70, 0x14, 0x7d,
    0x5b, 0x37, 0x30, 0xc7, 0x64, 0x58, 0x2e, 0x94, 0x90, 0xea, 0x4c, 0xad, 0xcd, 0x00, 0xa5, 0x14,
    0x86, 0x61, 0xba, 0xc5, 0x95, 0x12, 0x04, 0x9e, 0x32, 0xc1, 0x47, 0x20, 0x24, 0xb2, 0x48, 0xac,
    0xeb, 0xc3, 0xa9, 0xc3, 0xc7, 0x50, 0xb8, 0x38, 0x85, 0xfb, 0xee, 0xbb, 0x0d, 0x26, 0xe5, 0x31,
    0x36, 0x32, 0x8e, 0x60, 0xc0, 0x0b, 0xd3, 0x76, 0x50, 0x2f, 0x57, 0x91, 0xcc, 0xca, 0xf0, 0x50,
    0x02, 0x17, 0x04, 0x97, 0xce, 0x8e, 0x43, 0x6f, 0xe9, 0xc8, 0x26, 0xc3, 0xe8, 0xde, 0xd8, 0x0d,
    0x55, 0xd5, 0x31, 0x37, 0xb1, 0x00, 0xd3, 0xb2, 0x99, 0x14, 0x09, 0x94, 0x6e, 0xe8, 0x79, 0xfd,
    0x22, 0x19, 0x0d, 0xf2, 0x8f, 0xff, 0xe8, 0x91, 0x9f, 0xb6, 0x0f, 0x0e, 0x22, 0xd6, 0xd5, 0x43,
    0x7d, 0x89, 0x1e, 0xea, 0xba, 0x22, 0xe0, 0x12, 0x78, 0x44, 0x11, 0xb9, 0x4c, 0x12, 0x1c, 0xcf,
    0x23, 0xdc, 0x99, 0x84, 0x69, 0x59, 0x18, 0x9d, 0x98, 0xc7, 0xe4, 0x4c, 0x01, 0x27, 0x8e, 0x9f,
    0x87, 0xcb, 0x71, 0x98, 0x9c, 0x5d, 0x82, 0x06, 0x17, 0x84, 0x31, 0xe4, 0x7a, 0x3b, 0x90, 0xdd,
    0xd0, 0x0f, 0x4a, 0xbd, 0x98, 0x9f, 0x5e, 0x64, 0xae, 0x6b, 0xf0, 0xd1, 0x64, 0xac, 0xf0, 0xc6,
    0xeb, 0xff, 0x38, 0xba, 0x26, 0x03, 0x4f, 0x3f, 0xbd, 0xff, 0x91, 0xf5, 0x9b, 0xb6, 0xdd, 0xe3,
    0xc0, 0x62, 0xd4, 0x23, 0xf1, 0x2e, 0x71, 0x40, 0x48, 0x1d, 0x4e, 0x65, 0x19, 0xe5, 0x42, 0x19,
    0xb7, 0x6e, 0x59, 0x07, 0x5e, 0x12, 0xd1, 0x58, 0xae, 0x63, 0x66, 0x76, 0x09, 0x4d, 0x45, 0x83,
    0x6e, 0x5a, 0xd0, 0x0d, 0x13, 0x9b, 0x3f, 0x99, 0x45, 0x6e, 0xa0, 0x0b, 0xcd, 0x42, 0x09, 0xf1,
    0x48, 0x10, 0xdd, 0xeb, 0x3a, 0x80, 0x70, 0x16, 0x40, 0x04, 0xfd, 0xbe, 0x20, 0xa8, 0xa9, 0x43,
    0xe1, 0x44, 0x66, 0xe9, 0x86, 0xb5, 0x26, 0x03, 0x6d, 0x89, 0xc8, 0x8e, 0xb9, 0xb1, 0xcb, 0x36,
    0x75, 0x14, 0xb8, 0x84, 0x51, 0x38, 0x55, 0xa0, 0x55, 0x46, 0xe1, 0x6a, 0x01, 0xaa, 0x66, 0xa0,
    0xdc, 0x68, 0x81, 0x26, 0xda, 0x51, 0x2f, 0xd7, 0xb1, 0x52, 0x6b, 0xa2, 0x5c, 0x53, 0xc0, 0x71,
    0x14, 0x4a, 0xcb, 0xc0, 0x2b, 0x2f, 0xbd, 0x8b, 0x85, 0x42, 0x05, 0xed, 0xa9, 0x38, 0xfc, 0x7e,
    0x09, 0xf5, 0xa5, 0x0a, 0xa0, 0x96, 0x00, 0x47, 0x05, 0x1f, 0x90, 0x79, 0x12, 0xe9, 0xb0, 0x43,
    0x21, 0xb9, 0xe3, 0xf1, 0xa7, 0xf6, 0x7d, 0xe7, 0xfa, 0x9e, 0x37, 0x0c, 0xa2, 0xc9, 0xf1, 0x6b,
    0xb3, 0x83, 0x99, 0xc0, 0xf7, 0xfa, 0xfa, 0xd2, 0x22, 0xa4, 0xb0, 0x6b, 0x37, 0xea, 0x44, 0xcf,
    0x17, 0xd0, 0x68, 0xe9, 0x30, 0x4c, 0x0b, 0xf3, 0xf9, 0x0a, 0x44, 0xb8, 0x48, 0x08, 0x80, 0xe4,
    0xf5, 0x20, 0xe8, 0xf3, 0x62, 0xa9, 0x58, 0x43, 0xb5, 0xae, 0xe0, 0xfe, 0x5d, 0xc3, 0x38, 0x79,
    0xfc, 0x3c, 0x5c, 0x00, 0xd9, 0x54, 0x0c, 0x3a, 0x38, 0x04, 0xc2, 0x12, 0x40, 0x6c, 0x10, 0xce,
    0x03, 0xc6, 0x6c, 0x9b, 0xd9, 0x55, 0x54, 0x16, 0x0a, 0xca, 0x1f, 0x5e, 0x39, 0xfc, 0xda, 0x67,
    0x32, 0x50, 0xac, 0x36, 0xaf, 0xc8, 0xa9, 0xf4, 0x35, 0xb3, 0x54, 0xa3, 0xd6, 0xe2, 0x24, 0xe3,
    0xd4, 0x3a, 0xaa, 0xf5, 0x16, 0x22, 0x21, 0x3f, 0x18, 0x73, 0x71, 0x6d, 0x2e, 0x0f, 0xad, 0xb4,
    0x0c, 0xaf, 0xdf, 0x07, 0x39, 0x1a, 0xc6, 0x43, 0xbb, 0xb7, 0x61, 0xf7, 0xbd, 0xb7, 0xa2, 0xb7,
    0x2b, 0x05, 0x39, 0x1e, 0x86, 0x2f, 0x20, 0x61, 0x64, 0x74, 0x1a, 0xf5, 0xa6, 0x06, 0x8f, 0xc8,
    0x01, 0x8c, 0x80, 0x69, 0x75, 0x38, 0xa6, 0x66, 0x72, 0x1c, 0x27, 0x4e, 0x5f, 0xba, 0x74, 0xfe,
    0x57, 0xbf, 0x7c, 0xf1, 0xa9, 0x35, 0x3d, 0xc0, 0x6c, 0xdb, 0xf1, 0x87, 0xbc, 0x35, 0x21, 0xe0,
    0x47, 0xa5, 0xb0, 0xcc, 0x78, 0x51, 0x80, 0x6a, 0xd8, 0x00, 0x34, 0x68, 0xba, 0x09, 0x45, 0xd3,
    0xd1, 0x96, 0x6e, 0x03, 0xa1, 0x80, 0x9c, 0x8a, 0x81, 0x17, 0x45, 0xf4, 0x64, 0x64, 0x50, 0x97,
    0xc0, 0x30, 0x2c, 0x6c, 0x1b, 0x1e, 0xc0, 0x62, 0xa1, 0x84, 0x93, 0x67, 0xc7, 0x30, 0xb4, 0xa1,
    0x1b, 0x41, 0x49, 0x04, 0xe5, 0xc0, 0x5c, 0x0f, 0x85, 0xa6, 0xa9, 0xea, 0xc1, 0xd7, 0x8e, 0x3d,
    0x7b, 0xec, 0xcc, 0xf8, 0xc2, 0x9a, 0x1e, 0x08, 0x87, 0x03, 0xa1, 0x44, 0x2c, 0x90, 0xd5, 0x55,
    0x1d, 0x2b, 0x35, 0x8d, 0x2e, 0xe4, 0x2b, 0x50, 0x35, 0x83, 0xe5, 0x57, 0x6a, 0x28, 0x55, 0x1b,
    0x58, 0xdf, 0x93, 0x41, 0x30, 0xe8, 0x07, 0x89, 0x47, 0x10, 0x88, 0x06, 0x40, 0x2d, 0x07, 0x73,
    0x0b, 0x25, 0x14, 0x4b, 0x35, 0x48, 0x1e, 0x1e, 0x9d, 0xed, 0x49, 0xec, 0xba, 0xe7, 0x56, 0xc8,
    0xc9, 0x08, 0x0e, 0x1d, 0x3d, 0x8d, 0x77, 0x8e, 0x9c, 0x65, 0xa5, 0x62, 0x05, 0x9c, 0xe4, 0x11,
    0x1b, 0xd5, 0xe6, 0xc2, 0xdf, 0x0e, 0x7f, 0x74, 0x84, 0x92, 0x1b, 0x23, 0xc8, 0x0d, 0x0c, 0x04,
    0x03, 0x3e, 0x3f, 0xc7, 0x09, 0xbe, 0x66, 0x53, 0xb5, 0x67, 0x66, 0xf3, 0x70, 0x1c, 0xd7, 0x04,
    0x05, 0x1f, 0x0d, 0xfb, 0x91, 0x6d, 0x8b, 0x61, 0x60, 0x43, 0x37, 0x84, 0x64, 0x18, 0x4c, 0xd1,
    0x40, 0x79, 0x8a, 0xd9, 0xc5, 0x22, 0x3e, 0x3a, 0x37, 0x86, 0x2d, 0x43, 0x7d, 0x58, 0x3f, 0xd4,
    0x8f, 0xca, 0x4a, 0x0d, 0xde, 0xb6, 0x38, 0x76, 0xdd, 0x7b, 0x3b, 0x24, 0x51, 0x60, 0x07, 0xdf,
    0x3e, 0x49, 0xff, 0xfa, 0xf6, 0x89, 0xa7, 0x1f, 0xda, 0x33, 0x13, 0xeb, 0xc9, 0xb5, 0x0d, 0x37,
    0x6a, 0xba, 0xc5, 0xae, 0x0b, 0x23, 0x37, 0x99, 0x50, 0xd3, 0x0c, 0x7b, 0xc7, 0xd6, 0x81, 0x7d,
    0x14, 0x24, 0x3a, 0x3e, 0x99, 0xd7, 0xc3, 0x21, 0xaf, 0xaf, 0x52, 0x6f, 0x91, 0x66, 0x5d, 0x55,
    0x77, 0xed, 0x1a, 0x16, 0xe2, 0x19, 0x19, 0xd0, 0x0d, 0x50, 0x42, 0x30, 0x73, 0x75, 0x11, 0xc7,
    0xff, 0x79, 0x09, 0x7d, 0x5d, 0x29, 0xdc, 0x3e, 0xd4, 0x0b, 0x29, 0x1e, 0x80, 0x8f, 0x03, 0xf4,
    0x4a, 0x03, 0x8e, 0x6e, 0x23, 0x99, 0x08, 0x6b, 0x86, 0x61, 0x0a, 0x2f, 0xfc, 0xe9, 0xdd, 0xfd,
    0xbf, 0x7f, 0xf5, 0xf0, 0xeb, 0xa7, 0x47, 0xc6, 0xdf, 0x5b, 0x5c, 0xaa, 0xd4, 0x57, 0xdd, 0xc2,
    0x1b, 0x25, 0x50, 0x54, 0x4d, 0x3b, 0x72, 0xe2, 0xfc, 0x1b, 0x5e, 0x49, 0x94, 0x40, 0x11, 0xfa,
    0xf0, 0xec, 0xd8, 0x5b, 0x7f, 0xfc, 0xcb, 0xb1, 0x9d, 0x4b, 0xa5, 0xea, 0xab, 0xfe, 0x64, 0x0c,
    0x0e, 0x73, 0x4d, 0x5b, 0x33, 0x70, 0x61, 0x64, 0x1c, 0x6f, 0x1d, 0x3a, 0x89, 0x70, 0xc8, 0x8f,
    0x75, 0xfd, 0x59, 0xd8, 0x21, 0x3f, 0x88, 0x3f, 0x04, 0x44, 0xdb, 0x61, 0x10, 0x1f, 0xca, 0xd5,
    0x32, 0x6c, 0xcb, 0xa0, 0xeb, 0x7b, 0x3b, 0x10, 0x0e, 0x78, 0x01, 0x00, 0x23, 0x97, 0xa6, 0xf3,
    0xb5, 0x46, 0xcb, 0x5d, 0x3d, 0xfa, 0x6f, 0x90, 0xc0, 0xb6, 0x99, 0xfb, 0xf2, 0x1b, 0x27, 0x9e,
    0x9d, 0x5b, 0x2c, 0x9f, 0x2e, 0x96, 0xea, 0xad, 0x8b, 0xe3, 0xb3, 0xa7, 0x2a, 0xd5, 0xa6, 0x73,
    0xff, 0xce, 0xcd, 0x3f, 0xb0, 0x2a, 0x75, 0x2c, 0x57, 0xea, 0x6c, 0x71, 0xbe, 0x88, 0x43, 0xef,
    0x9f, 0x81, 0x47, 0x12, 0x91, 0xcb, 0xc8, 0x48, 0xa7, 0xe3, 0xe0, 0xa3, 0x21, 0x58, 0x0d, 0x0d,
    0x62, 0x20, 0x82, 0x68, 0xcf, 0x20, 0x7c, 0xa9, 0x76, 0x4c, 0x9d, 0x3b, 0x8b, 0xce, 0x5c, 0x8a,
    0x0d, 0xdf, 0xf6, 0x95, 0xf8, 0xc4, 0x4c, 0x7e, 0x81, 0xe7, 0x38, 0xd8, 0x8e, 0x73, 0x53, 0x28,
    0xbd, 0x29, 0x90, 0x28, 0x2d, 0xdd, 0x19, 0x9d, 0x98, 0xbf, 0x7a, 0x6d, 0xa1, 0x38, 0xab, 0xe9,
    0xa6, 0x73, 0xc7, 0xf0, 0xc0, 0x3d, 0xfb, 0x1f, 0xfe, 0xc6, 0xb3, 0x2d, 0x45, 0x35, 0x26, 0xa7,
    0xf2, 0xd2, 0xe9, 0x0b, 0x93, 0x50, 0x75, 0x13, 0xb7, 0x0c, 0xf6, 0x22, 0x9b, 0x8e, 0xa3, 0x52,
    0x69, 0x62, 0xe2, 0xe3, 0x49, 0x2c, 0xce, 0xcc, 0xa1, 0xb6, 0xb4, 0x88, 0xf2, 0xf2, 0x22, 0x28,
    0xe5, 0x91, 0xe9, 0x91, 0xe1, 0x4f, 0xc5, 0x39, 0xc9, 0x65, 0xfa, 0xfb, 0xc7, 0x47, 0x8e, 0x36,
    0x5b, 0x1a, 0xa3, 0x94, 0x60, 0x95, 0x05, 0x6e, 0x7e, 0x0d, 0x09, 0x21, 0xe0, 0x38, 0x0a, 0xdb,
    0x76, 0x40, 0x29, 0xe5, 0xef, 0xbf, 0x7b, 0xcb, 0x43, 0x7d, 0xb9, 0x34, 0xbd, 0x3c, 0x31, 0x47,
    0x2b, 0xf5, 0x26, 0x3c, 0x02, 0x8f, 0x2d, 0x1b, 0x7b, 0x20, 0x08, 0x1c, 0xde, 0xfb, 0xe0, 0x63,
    0x28, 0x2d, 0x0d, 0x1e, 0x49, 0x04, 0x21, 0x04, 0xb6, 0xc3, 0x40, 0x19, 0x43, 0x67, 0x3a, 0x86,
    0x5c, 0x5f, 0x86, 0x75, 0xe6, 0x32, 0x7a, 0x7f, 0x5b, 0xec, 0xc1, 0xb4, 0x1c, 0xf9, 0x45, 0xb1,
    0x54, 0x57, 0x28, 0xa1, 0x60, 0x70, 0x3e, 0x3f, 0x96, 0xaf, 0x4e, 0xc8, 0x7b, 0x77, 0xdf, 0xb9,
    0xfd, 0xe1, 0x07, 0xee, 0x38, 0x5a, 0x6b, 0xaa, 0xa2, 0x69, 0x99, 0xd0, 0x54, 0x13, 0x2d, 0xdd,
    0x40, 0x38, 0xe0, 0x43, 0x32, 0x16, 0x82, 0x47, 0x12, 0xc1, 0xf1, 0x14, 0x1e, 0x5e, 0x80, 0x20,
    0xf0, 0xb0, 0x6c, 0x9b, 0x29, 0x2d, 0x8d, 0x4e, 0xcd, 0x2c, 0x7e, 0x5c, 0x2c, 0x56, 0x5f, 0x3e,
    0x33, 0x7a, 0xed, 0xcd, 0x91, 0xd1, 0xe9, 0x19, 0x4d, 0x33, 0x5d, 0x02, 0xc0, 0xc5, 0x17, 0x30,
    0xf0, 0x29, 0x0b, 0xae, 0xcb, 0x10, 0x09, 0x05, 0xf8, 0xcd, 0x1b, 0x73, 0x83, 0x0d, 0xa5, 0x45,
    0xa7, 0x67, 0x0b, 0x66, 0x30, 0x18, 0xe0, 0x6d, 0xcb, 0xa6, 0x1e, 0x81, 0x47, 0x6f, 0x77, 0x1a,
    0x6d, 0x72, 0x0c, 0x60, 0x0c, 0xcc, 0x65, 0x10, 0x04, 0x1e, 0x5e, 0x49, 0x04, 0xcf, 0x51, 0xa8,
    0x86, 0x6d, 0x6f, 0xde, 0xd4, 0x97, 0x3b, 0x73, 0x61, 0x2a, 0xf2, 0xca, 0x3b, 0xff, 0x9a, 0xd7,
    0x34, 0xd3, 0xfd, 0xf4, 0x40, 0x5f, 0xf8, 0x31, 0x01, 0x00, 0x81, 0xe7, 0x60, 0xd9, 0x0e, 0xf6,
    0xed, 0xd9, 0xb1, 0xf5, 0xc0, 0x73, 0x4f, 0x9e, 0xb2, 0xfe, 0x93, 0x13, 0xe0, 0x9a, 0x0e, 0xd5,
    0x0d, 0x13, 0x92, 0x57, 0x42, 0xb8, 0xab, 0x0d, 0x8c, 0xe7, 0xe0, 0x5a, 0x0e, 0x40, 0x09, 0xc0,
    0xf1, 0x20, 0xe0, 0x40, 0x28, 0x0f, 0x42, 0xfc, 0x4c, 0x10, 0x3d, 0x14, 0x6e, 0x08, 0x8f, 0x7d,
    0xff, 0x87, 0x3b, 0x0f, 0x1c, 0x78, 0xeb, 0x38, 0x08, 0x81, 0xeb, 0xba, 0x5f, 0x7e, 0x05, 0xff,
    0xff, 0xea, 0xdf, 0xa6, 0x4e, 0x39, 0xd5, 0x7d, 0x7c, 0x57, 0xa5, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
static const unsigned int ICON_HOVER_size = 2375;

// Global variables
HMODULE hSelf;
AddonDefinition_t AddonDef{};
AddonAPI_t* APIDefs = nullptr;
bool g_WindowVisible = false;

// UI State
static AlterEgo::CharacterIdentity::Selection g_SelectedCharacter;
static int g_SelectedTab = 0;          // Equipment, Build, Tools, Ranger Pets
static bool RenderChipStrip(const std::vector<std::string>& labels,
                            const std::vector<bool>& activeMarkers,
                            int& selectedIdx,
                            float& scrollOffset,
                            const char* idScope,
                            const char* chipTooltip = nullptr,
                            int* rightClickedIdx = nullptr);

static int g_SelectedEquipTab = 0;     // Equipment tab filter
static int g_FashionTab = 0;           // Equipment tab whose fashion menu was opened (1-based)
static float g_EquipTabScroll = 0.0f;  // Horizontal scroll offset for equipment tab chip strip
static float g_BuildTabScroll = 0.0f;  // Horizontal scroll offset for build tab chip strip
static int g_SelectedBuildTab = 0;     // Build tab filter
// A pet membership link targets a verified character and physical build tab.
// Apply at the existing detail tab bar, after the current panel unwinds.
struct PetBuildNavigation {AlterEgo::CharacterIdentity::Ref character;int tab=0;};
static std::optional<PetBuildNavigation> g_PetBuildNavigation;
static int g_BuildMenuTab = -1;        // Right-clicked build tab chip (0-based) for the build options menu
static bool g_ShowQAIcon = true;
static AlterEgo::Font::Config g_FontConfig; // default = {NexusDefault, "", 16}
static bool g_CompactCharList = false;
static AlterEgo::FastLoadUI::Mode g_FastLoadMode=AlterEgo::FastLoadUI::Mode::Enabled;
static bool g_ShowCraftingIcons = false;
static bool g_ShowAge = false;
static bool g_ShowPlaytime = false;
static bool g_ShowLastLogin = false;
// Explicit opt-in: no additional API requests unless enabled in Nexus options.
static bool g_AutoRefreshEnabled = false;
// Login is observed locally; it never independently requests data.
static bool g_AutoRefreshOnSpecialization = true;
static bool g_WarnStaleSpecialization = true;
static int g_BirthdayMode = 1;         // 0 = Always, 1 = A week out, 2 = Never
static float g_CharListWidth = 200.0f; // Resizable character list column width
static bool g_DetailsFetched = false;  // Track if we've requested details for current character
// Render-thread navigation intent. Ordinary filter/refresh changes do not branch history.
static int g_NavIntentFrames=0;
static void MarkNavigation(){g_NavIntentFrames=3;}
void MarkInventoryNavigation(){MarkNavigation();}
static int g_MainTab = 0;             // Stable session IDs: 0=Characters, 1=Build Library, 2=Skinventory, 3=Vault, 4=Achievements, 5=Compare

// Skinventory UI state
static int g_SkinActiveTab = 0; // 0 = Browser, 1 = Shopping List
static bool g_SkinSwitchToBrowser = false;
static bool g_SkinSwitchToShopping = false;
static bool g_SkinScrollToSkin = false;
static Skinventory::SkinFilters g_SkinBrowserFilters;
static float g_InventoryListWidth=420.f;
static float g_SkinListWidth=420.f, g_FashionListWidth=220.f, g_ComponentListWidth=270.f, g_EquipmentSetListWidth=220.f;
static uint32_t g_SkinSelectedId = 0;
static char g_SkinSearchFilter[256] = "";
static bool g_SkinShowOwned = true;
static bool g_SkinShowUnowned = true;
static int  g_SkinViewMode = 0; // 0 = List, 1 = Grid
static bool g_SkinInitialized = false;
static bool g_SwitchToInventory = false;
static void OpenInventoryItem(uint32_t id);
static bool g_SwitchToSkinventory = false; // set by achievements tab to navigate to Skinventory
static bool g_ForceCharactersTab = false;
static bool g_SwitchToCompare = false;     // set by Build Library to open Compare with its selected build

// Portrait cache (for character screenshots in equipment panel)
struct PortraitEntry {
    std::string path;
    int64_t modTime; // file last-write time for cache busting
};
static std::unordered_map<std::string, PortraitEntry> s_portraitPathCache;
static std::unordered_set<std::string> s_portraitMissing;

static int64_t GetFileModTime(const std::string& path) {
    try {
        auto ftime = std::filesystem::last_write_time(std::filesystem::u8path(path));
        return ftime.time_since_epoch().count();
    } catch (...) { return 0; }
}

// Shopping list state
struct SkinShopEntry {
    uint32_t skinId;
    int price;      // in copper
    int source;     // 0 = TP, 1 = Vendor
    std::string name, subtype; // cached sort keys
    uint32_t ownedItem=0;
};
static std::vector<SkinShopEntry> g_SkinShopList;
static std::atomic<bool> g_SkinShopListDirty{true};

// =========================================================================
// Achievement Tracker
// =========================================================================

struct AchGroupDef {
    std::string id;            // UUID string
    std::string name;
    std::string description;
    int order = 0;
    std::vector<uint32_t> categories; // category IDs in this group
};

struct AchCategoryDef {
    uint32_t id = 0;
    std::string name;
    std::string description;
    int order = 0;
    std::string icon_url;
    std::vector<uint32_t> achievements; // achievement IDs in this category
};

using AchBitDef = AlterEgo::AchievementDefinitions::Bit;
using AchDef = AlterEgo::AchievementDefinitions::Definition;

using AchProgress = AlterEgo::Cache::AchievementProgress;

// Achievement tracker state
static std::vector<AchGroupDef> g_AchGroups;
static std::unordered_map<uint32_t, AchCategoryDef> g_AchCategories;
static std::unordered_map<uint32_t, std::shared_ptr<const AchDef>> g_AchDefs;           // cached definitions
static std::unordered_map<uint32_t, AchProgress> g_AchProgress;  // account progress
static std::vector<uint32_t> g_AchPinned;                        // pinned achievement IDs
static std::unordered_map<uint32_t, std::string> g_AchNameIndex; // id -> name (for search)
static std::unordered_map<uint32_t, uint32_t> g_AchIdToCategory; // ach id -> category id
static std::unordered_set<uint32_t> g_AchHiddenCatIds;          // categories to hide (e.g. Character Adventure Guide)

static std::recursive_mutex g_AchMutex;
static std::atomic<bool> g_AchGroupsFetched{false};
static std::atomic<int64_t> g_AchGroupsCacheFetchedAt{0}; // last successful groups/categories snapshot
static uint64_t g_AchGroupsGeneration = 0;   // changes after cache load / network refresh
static std::atomic<bool> g_AchGroupsFetching{false};
static std::atomic<bool> g_AchNameIndexReady{false};
static std::atomic<int64_t> g_AchNameIndexVerifiedAt{0}, g_AchNameIndexRetryAt{0};
static std::atomic<uint64_t> g_AchCatalogGeneration{0};
static std::mutex g_AchGroupPersistMutex;
static std::string g_AchNameIndexStatus, g_AchNameIndexError; // guarded by g_AchMutex
static std::atomic<bool> g_AchNameIndexFetching{false};
static std::string g_AchStatusMsg;

static std::string g_AchSelectedGroupId;   // currently selected group UUID
static uint32_t g_AchSelectedCatId = 0;    // currently selected category ID
static std::atomic<bool> g_AchCatFetching{false};      // fetching defs for selected category
static std::atomic<bool> g_AchProgressFetching{false}; // fetching account progress
static std::atomic<uint64_t> g_AchRequestGeneration{0};
static std::atomic<long long> g_AchProgressQueryStartMs{0}; // watchdog for missing API responses
static bool g_AchNeedRequery = false;      // deferred requery on account change
static std::atomic<bool> g_AchProgressDirty{false};
static bool g_AchSwitchingAccount=false; // guarded by g_AchMutex    // persistent cache needs flushing
static std::string g_AchCachedAccount;     // account whose data is currently in g_AchProgress
static std::atomic<int64_t> g_AchProgressCacheSavedAt{0}; // last successful achievement progress retrieval
// The loaded snapshot timestamp is stable across pinned updates, unlike saved_at.
static int64_t g_AchProgressLoadedSnapshotAt = 0;
static std::atomic<bool> g_AchNeedsInitialFullLoad{true};
static std::atomic<bool> g_AchManualInitialScan{false};
// Refresh timestamps are scoped to the active category and reset on account change.
static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> g_AchCategoryRefreshAt;
static std::chrono::steady_clock::time_point g_AchRefreshSuccessUntil{};
static std::string g_AchRefreshError;
static std::chrono::steady_clock::time_point g_AchLastSave;
static void SaveAchProgress(const std::string& account);
static void LoadAchProgress(const std::string& account);
static std::string GetAchievementCacheAccount();
// Render-thread-only navigation demand; public workers never schedule private progress.
static std::optional<AlterEgo::AchievementNames::CategoryDemand> g_AchCategoryDemand;
static uint64_t g_AchDemandRevision=0;
static bool g_AchBrowserRendered=false;
// Monotonic ms helper for scheduling deferred/backoff queries on the render thread.
static long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::atomic<uint64_t> g_AchProgressGen{0};      // incremented when progress updates, triggers popout cache rebuild
static char g_AchSearchBuf[128] = "";
static bool g_AchPopoutVisible = false;    // popout tracker window visibility
static bool g_AchHideEmptyCats = false;    // rail filter: hide categories with no progress data
static bool g_AchHideCompletedCats = false; // rail filter: hide categories where all achievements are done
static std::unordered_map<uint32_t, bool> g_AchExpandedInPopout; // expanded state per pinned ach
static bool g_AchShowCompletedSteps = false; // popout: show completed bits/steps
static std::unordered_set<uint32_t> g_AchExpandedInList; // expanded state per achievement in main list
static float g_AchTreeWidth = 220.0f;      // resizable left tree panel width
static float g_AchPinnedHeight = 200.0f;   // resizable pinned section height
static float g_AchTreeScrollY = 0.0f;      // saved scroll position of left tree panel
static float g_AchListScrollY = 0.0f;      // saved scroll position of right list panel
static bool g_AchRestoreScroll = false;     // flag to restore scroll positions on next frame
static uint32_t g_AchNavigateToId = 0;     // pending navigation: jump to this achievement ID

// Active special event detection (from /v2/achievements/daily "special" array)
static std::vector<uint32_t> g_AchActiveMainCatIds;   // main/Historical categories for top of tree
static std::vector<uint32_t> g_AchActiveDailyCatIds;   // daily categories for top of Daily group
static std::string g_AchActiveEventName;                // display name for the active event node
static std::atomic<int64_t> g_AchActiveEventNextCheck{0};
static std::atomic<bool> g_AchActiveEventFetching{false};

// Festival daily category IDs (hidden from Daily group when their festival is not active)
static const std::unordered_set<uint32_t> FESTIVAL_DAILY_CAT_IDS = {
    79,  // Halloween Daily
    98,  // Wintersday Daily
    102, // Lunar New Year Dailies
    162, // Daily Super Adventure Festival
    201, // Daily Lunar New Year
    213, // Daily Festival of the Four Winds
    233, // Daily Dragon Bash
};

// Festival daily category -> display name + keywords to match Historical categories
struct FestivalMapping {
    std::string displayName;
    std::vector<std::string> keywords;  // match Historical category names
};
static const std::unordered_map<uint32_t, FestivalMapping> FESTIVAL_MAPPINGS = {
    {79,  {"Shadow of the Mad King", {"Mad King", "Halloween", "Lunatic"}}},
    {98,  {"Wintersday",             {"Wintersday", "Toymaker Tixx", "Winter's Presence"}}},
    {102, {"Lunar New Year",         {"Lunar New Year"}}},
    {162, {"Super Adventure Festival",{"Super Adventure Box"}}},
    {201, {"Lunar New Year",         {"Lunar New Year"}}},
    {213, {"Festival of the Four Winds", {"Four Winds", "Queen's Gauntlet", "Bazaar"}}},
    {233, {"Dragon Bash",            {"Dragon Bash"}}},
};

// Categories from Bonus Events group that are always junk (never surfaced)
static bool IsBonusEventJunk(const std::string& name) {
    return name.find("Marshaling") != std::string::npos ||
           name.find("Mobilization") != std::string::npos ||
           name.find("Black Lion") != std::string::npos ||
           name.find("Wizard's Vault") != std::string::npos ||
           name.find("Adventure Guide") != std::string::npos;
}

// Categories the in-game client lists under "Bonus Events" but the public API
// files elsewhere (usually Historical). Force them into Bonus Events for parity.
// Add new category IDs here as returning bonus events rotate in.
static const std::unordered_set<uint32_t> BONUS_EVENT_CATEGORY_OVERRIDES = {
    452, // Roller Beetle Racing
    401, // World vs. World Rush
    400, // Player vs. Player Rush
    447, // April Fools!
    67,  // Festival of the Four Winds
    231, // Dragon Bash
    386, // Dungeon Rush
    402, // New Hero Jump Start
    342, // Extra Life Donation Drive
    // Already in the Bonus Events group per the API (no relocation needed):
    //   Fractal Rush (393), Fractal Incursion (462), Gnashbash "Birthday" Celebration (473).
};

// Relocate override categories into the "Bonus Events" group. Caller must hold g_AchMutex.
static void ApplyBonusEventOverrides() {
    if (BONUS_EVENT_CATEGORY_OVERRIDES.empty()) return;
    AchGroupDef* bonus = nullptr;
    for (auto& g : g_AchGroups) {
        if (g.name == "Bonus Events") { bonus = &g; break; }
    }
    if (!bonus) return;
    for (uint32_t cid : BONUS_EVENT_CATEGORY_OVERRIDES) {
        // Strip the category out of whatever group the API assigned it to
        for (auto& g : g_AchGroups) {
            if (&g == bonus) continue;
            auto& v = g.categories;
            v.erase(std::remove(v.begin(), v.end(), cid), v.end());
        }
        // Add to Bonus Events (only if the category exists and isn't already there)
        if (g_AchCategories.count(cid) &&
            std::find(bonus->categories.begin(), bonus->categories.end(), cid) == bonus->categories.end()) {
            bonus->categories.push_back(cid);
        }
    }
}

// Maps wiki "Special event" row titles to achievement category IDs.
// Checked case-insensitively as substrings; list most-specific phrases first.
static const std::vector<std::pair<std::string, uint32_t>> WIKI_EVENT_CATEGORY_KEYWORDS = {
    {"roller beetle",        452},
    {"world vs. world rush", 401}, {"wvw rush", 401},
    {"player vs. player rush",400}, {"pvp rush", 400},
    {"dungeon rush",         386},
    {"fractal incursion",    462},
    {"fractal rush",         393},
    {"dragon bash",          231},
    {"festival of the four winds", 67}, {"four winds", 67},
    {"april fools",          447},
    {"world boss rush",      228}, {"boss rush", 228},
    {"new hero jump start",  402},
    {"extra life",           342},
    {"gnashblade",           473}, {"gnashbash", 473},
};

struct ActiveEventResult { std::string name; uint32_t catId; };

// Lowercase copy for case-insensitive matching.
static std::string AeToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

// Read a single-line wiki template field: "| <key> = <value>" up to end of line.
// Returns "" if not found. Trims surrounding whitespace.
static std::string AeWikiField(const std::string& block, const std::string& key) {
    size_t pos = 0;
    while ((pos = block.find('|', pos)) != std::string::npos) {
        size_t lineEnd = block.find('\n', pos);
        std::string line = block.substr(pos, (lineEnd == std::string::npos ? block.size() : lineEnd) - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string name = line.substr(1, eq - 1); // strip leading '|'
            size_t a = name.find_first_not_of(" \t");
            size_t b = name.find_last_not_of(" \t");
            if (a != std::string::npos) name = name.substr(a, b - a + 1);
            if (AeToLower(name) == AeToLower(key)) {
                std::string val = line.substr(eq + 1);
                size_t va = val.find_first_not_of(" \t");
                size_t vb = val.find_last_not_of(" \t\r");
                if (va == std::string::npos) return "";
                return val.substr(va, vb - va + 1);
            }
        }
        pos = (lineEnd == std::string::npos) ? block.size() : lineEnd;
    }
    return "";
}

// Parse "June 30, 2026" -> 20260630. Returns 0 on failure.
static int AeParseWikiDate(const std::string& s) {
    static const char* months[] = {"january","february","march","april","may","june",
        "july","august","september","october","november","december"};
    std::string low = AeToLower(s);
    int mon = 0;
    for (int i = 0; i < 12; ++i) {
        if (low.rfind(months[i], 0) == 0) { mon = i + 1; break; }
    }
    if (!mon) return 0;
    int day = 0, year = 0;
    size_t p = low.find_first_of("0123456789");
    if (p == std::string::npos) return 0;
    day = std::atoi(low.c_str() + p);
    size_t comma = low.find(',');
    if (comma == std::string::npos) return 0;
    size_t yp = low.find_first_of("0123456789", comma);
    if (yp == std::string::npos) return 0;
    year = std::atoi(low.c_str() + yp);
    if (day < 1 || day > 31 || year < 2012) return 0;
    return year * 10000 + mon * 100 + day;
}

// Extract the "== Active events ==" section, find its {{Special event row}} blocks,
// select the one whose [start,end] window contains today, and map its title to a
// category ID. Falls back to the first recognised row if dates are unparseable.
static ActiveEventResult ParseActiveSpecialEvent(const std::string& wikitext, int todayYmd, bool allowUndated) {
    ActiveEventResult empty{"", 0};
    size_t h = wikitext.find("== Active events ==");
    if (h == std::string::npos) return empty;
    size_t sectionStart = h + 19;
    size_t sectionEnd = wikitext.find("\n== ", sectionStart);
    std::string section = wikitext.substr(sectionStart,
        (sectionEnd == std::string::npos ? wikitext.size() : sectionEnd) - sectionStart);

    const std::string ROW = "{{Special event row";
    size_t rp = section.find(ROW);
    if (rp == std::string::npos) return empty; // no active event

    // Return the active row's title even when no keyword matches (catId 0) — the
    // caller resolves the category by name, since some events (e.g. "Return to X")
    // only expose their category in the API while they are live.
    ActiveEventResult firstRow{"", 0};
    bool haveFirst = false;
    while (rp != std::string::npos) {
        size_t next = section.find(ROW, rp + ROW.size());
        std::string block = section.substr(rp,
            (next == std::string::npos ? section.size() : next) - rp);
        rp = next;

        std::string title = AeWikiField(block, "title");
        if (title.empty()) continue;

        std::string lowTitle = AeToLower(title);
        uint32_t catId = 0;
        for (const auto& kv : WIKI_EVENT_CATEGORY_KEYWORDS) {
            if (lowTitle.find(kv.first) != std::string::npos) { catId = kv.second; break; }
        }

        ActiveEventResult r{title, catId};
        int startYmd = AeParseWikiDate(AeWikiField(block, "start date"));
        int endYmd   = AeParseWikiDate(AeWikiField(block, "end date"));
        if (allowUndated && !haveFirst && (!startYmd || !endYmd)) { firstRow=r; haveFirst=true; }
        if (AlterEgo::WikiFreshness::DateMatches(startYmd,endYmd,todayYmd)) {
            return r; // dated window contains today — definitive
        }
    }
    return firstRow; // only an undated row from a fresh source may be a fallback
}

// Optimistic update grace period — protects alert-based increments from stale API snapback
static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> g_AchOptimisticTime;

// Persistence & caching
static std::chrono::steady_clock::time_point g_LastAchProgressQuery{};
static bool SaveAchTrackerState();
static void LoadAchTrackerState();
static void LoadAchGroupCache();
static void LoadAchDefCache();
static void FetchAchGroups();
static void FetchAchCategoryDefs(uint32_t catId, bool automatic = false);
static void SendAchProgressQuery(const std::vector<uint32_t>& ids, bool automatic = false,
                                 uint32_t categoryId = 0, const AlterEgo::AchievementNames::CategoryDemand* demand = nullptr);
static void FetchPinnedAchDefs();
static void FetchActiveSpecialEvent();

// Character search
static char g_CharSearchBuf[128] = "";

// MumbleLink identity for current character indicator
struct MumbleIdentity {
    char Name[20];
    unsigned Profession;
    unsigned Specialization;
    unsigned Race;
    unsigned MapID;
    unsigned WorldID;
    unsigned TeamColorID;
    bool IsCommander;
    float FOV;
    unsigned UISize;
};
static std::string g_CurrentCharName; // render-owned gameplay identity
static uint32_t g_LiveSpecialization=0;
// Read-only presentation projection; CharacterActivity is the sole login writer.
static std::map<AlterEgo::CharacterActivity::Key,int64_t> g_LoginHistory;
static std::map<std::string,int64_t> g_LegacyLoginHistory;
static bool g_LegacyLoginArchivePending=false;
static bool g_ActivityLoadValid=true;
static uint64_t g_LoginProjectionRevision=0;
static void LoadLegacyLoginHistory();
static void ImportPortableLoginHistory();
static void MigrateLegacyLoginHistory();
static void SyncLoginHistory();
static void LoadCharacterActivity();
static bool SaveCharacterActivity(bool force=false);
static void ObserveCharacterActivity();
static int64_t CharacterLastLogin(const AlterEgo::Character& ch){
    auto it=g_LoginHistory.find({ch.account_name,ch.name});
    return it==g_LoginHistory.end()?0:it->second;
}
static std::optional<AlterEgo::CharacterIdentity::Address> DueCharacterAddress(const std::string& account,const std::string& name,const AlterEgo::CharacterScopes::Stamps& stamps,AlterEgo::CharacterScopes::Mask demand);

// Build Library UI state
static float g_LibListWidth = 220.0f; // Resizable build list column width
static int g_LibraryCollection = 0; // 0 Loadouts, 1 Fashion, 2 Components, 3 Equipment Sets
static bool g_SwitchToLibrary = false; // select Library after creation/import/navigation
static std::string g_LibSelectedId,g_LibRevealId;
static void SelectLoadoutAt(int index);
// Build editor: "+ New Loadout" dialog state
static bool g_NewBuildDialogOpen = false;
static std::string g_NewBuildProfessionPending;
static std::vector<AlterEgo::GameMode> g_NewBuildModes{AlterEgo::GameMode::PvE};
// Build editor: working draft + spec picker state
static AlterEgo::SavedBuild g_EditDraft;   // working copy while editing a build's definition
static uint64_t g_EditDraftRevision = 0;
static bool g_EditRefsReady=false;
static std::string g_EditSetId,g_EditFashionId;
static std::map<std::string,std::string> g_EditRefs,g_EditInheritedRefs;
static std::set<std::string> g_EditSharedKinds,g_EditSharedReferenceKinds;
static std::string g_EditDraftId;          // id the draft currently mirrors
static bool g_EditDirty = false;           // edit activity; reconciled against exact baseline
static nlohmann::json g_EditBaseline;
static std::map<std::string,std::string> g_EditBaselineRefs;
static std::string g_EditBaselineSet,g_EditBaselineFashion,g_EditError;
static uint64_t g_DiscardNewBuildRevision=0;
static bool g_EditIsNewBuild = false;      // true while editing a freshly-created build (cancel discards it)
static std::string g_DiscardNewBuildId;    // deferred: id of a new build to delete after the render pass
static int g_SpecPickerSlot = -1;          // 0..2 while spec picker open, else -1
static std::set<std::string> g_LibFilterModes; // empty = All; selected modes are ORed
static char g_LibSearchBuf[128] = "";
static char g_LibImportBuf[4096] = "";
static char g_LibImportName[128] = "";
// Auto-retry pending import once the profession palette finishes loading
static bool g_LibImportPending = false;
static std::string g_LibImportPendingProfession;
// Right-pane edit mode: by default the Name/Mode/Notes inputs are hidden
// behind an Edit button to keep the preview clean.
static bool g_LibEditMode = false;
static void SelectLoadoutAt(int index){
    if(g_LibEditMode)return;
    const auto& builds=AlterEgo::GW2API::GetSavedBuilds();
    g_LibSelectedId=index>=0&&index<(int)builds.size()?builds[index].id:std::string{};
    g_LibRevealId=g_LibSelectedId;
}
static std::vector<AlterEgo::GameMode> g_LibImportModes{AlterEgo::GameMode::PvE};
static bool g_LibShowImport = false;
static std::string g_LibImportError;
// A Snow Crows page is fetched and parsed on a worker; the library is only
// mutated by the main/UI thread after a complete, validated result arrives.
struct BuildSourceFetchResult {
    bool ready = false;
    bool refresh = false;
    std::string targetId;
    uint64_t revision=0;
    AlterEgo::SnowCrows::Page page;
    std::string error;
};
static bool g_BuildSourceBusy = false; // Main thread only
static std::mutex g_BuildSourceResultMutex;
static BuildSourceFetchResult g_BuildSourceResult;
static std::string g_BuildSourceRefreshError;
static bool g_SourceRefreshDraft=false;
static std::vector<std::string> g_SourceRefreshNotes;
static std::optional<std::vector<AlterEgo::SnowCrows::InfusionTotal>> g_SourceInfusionOffer;
static bool g_LibDetailsFetched = false;
static std::unordered_set<std::string> g_LibCollapsedProfessions; // session-persisted
static std::set<std::string> g_ComponentCollapsedKinds;
// Inline edit buffers — std::string + ImGui resize callback so long names
// or notes are never silently truncated by a fixed buffer size.
static std::string g_LibEditName;
static std::string g_LibEditNotes;

// Seed the editing draft + name/notes buffers from a build and clear the dirty flag.
// The on-disk build is itself the pre-edit snapshot (nothing persists until Save), so
// Cancel just discards the draft. Defined here (before the pickers/New-Build call sites).
// Alternate infusion mode values are session-only and reset with the owner draft.
static std::optional<AlterEgo::SavedBuild> g_InfusionSlotDraft, g_InfusionTotalsDraft;
static void EnterBuildEditMode(const AlterEgo::SavedBuild& build) {
    g_InfusionSlotDraft.reset();g_InfusionTotalsDraft.reset();
    g_SourceRefreshDraft=false;g_SourceRefreshNotes.clear();g_SourceInfusionOffer.reset();
    g_EditDraft = build;
    g_EditDraftId = build.id;
    g_EditDraftRevision = AlterEgo::GW2API::GetBuildLibraryRevision();
    g_EditRefsReady=false;g_EditRefs.clear();g_EditInheritedRefs.clear();g_EditSetId.clear();g_EditFashionId.clear();g_EditSharedKinds.clear();g_EditSharedReferenceKinds.clear();
    nlohmann::json graph;
    if(AlterEgo::GW2API::GetComposableLoadouts(graph)&&graph.contains("loadouts")){
        for(const auto& owner:graph.at("loadouts"))if(owner.value("id",std::string())==build.id){
            g_EditRefs=owner.at("refs").get<std::map<std::string,std::string>>();
            g_EditSetId=owner.value("equipment_set_id",std::string());
            g_EditFashionId=owner.value("fashion_id",std::string());
            g_EditRefsReady=true;break;
        }
    }
    g_LibEditName = build.name;
    g_LibEditNotes = build.notes;
    g_EditBaseline=AlterEgo::GW2API::EditorBuildSnapshot(build);
    g_EditBaselineRefs=g_EditRefs;g_EditBaselineSet=g_EditSetId;g_EditBaselineFashion=g_EditFashionId;
    g_EditError.clear();g_EditDirty = false;
    g_LibEditMode = true;
}
static inline void MarkEditDirty() { g_EditDirty = true;g_EditError.clear(); }
static bool LoadoutDraftDirty(){
    if(!g_LibEditMode)return false;
    auto current=AlterEgo::GW2API::EditorBuildSnapshot(g_EditDraft);
    current["name"]=g_LibEditName;current["notes"]=g_LibEditNotes;
    return current!=g_EditBaseline||g_EditRefs!=g_EditBaselineRefs||!g_EditInheritedRefs.empty()||
        g_EditSetId!=g_EditBaselineSet||g_EditFashionId!=g_EditBaselineFashion;
}
// Defined after the editor headers; use the same protection in launchers,
// backup/restore and navigation without introducing a global draft owner.
static bool OtherLibraryEditorOpen();
static bool LibraryDraftOpen();
static bool g_EditSavePending = false;
static bool g_EditReviewPending=false;

static std::string g_LibEditBuildId;   // which build is being edited
static AlterEgo::RecordSelection::RevisionTarget g_LibDeleteTarget;
static bool g_LibDeletePending=false;
static std::string g_LibDeleteError;
static AlterEgo::UI::NameActionDraft g_LibNameAction;
static AlterEgo::RecordSelection::RevisionTarget g_LibNameTarget;
static std::atomic<bool> g_RelaySending{false};    // relay POST in progress
static std::string g_RelayResultCode;  // 4-char code on success
static std::string g_RelayResultError; // error message on failure

// =========================================================================
// Events: Chat integration — detect build links in game chat
// Struct definitions from jsantorek/GW2-Chat (used by "Events: Chat" addon)
// =========================================================================
#define EV_CHAT_MESSAGE "EV_CHAT:Message"

typedef char* ChatStringUTF8;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} ChatGUID;

typedef struct {
    uint32_t Low;
    uint32_t High;
} ChatTimestamp;

enum ChatMessageType {
    ChatMsg_Error = 0,
    ChatMsg_Guild,
    ChatMsg_GuildMotD,
    ChatMsg_Local,
    ChatMsg_Map,
    ChatMsg_Party,
    ChatMsg_Squad,
    ChatMsg_SquadMessage,
    ChatMsg_SquadBroadcast,
    ChatMsg_TeamPvP,
    ChatMsg_TeamWvW,
    ChatMsg_Whisper,
    ChatMsg_Emote,
    ChatMsg_EmoteCustom
};

enum ChatMetadataFlags {
    ChatFlag_None = 0,
    ChatFlag_Whisper_IsFromMe = 1 << 4,
};

typedef struct {
    ChatGUID       Account;
    ChatStringUTF8 CharacterName;
    ChatStringUTF8 AccountName;
    ChatStringUTF8 Content;
} ChatGenericMessage;

typedef struct {
    ChatTimestamp       DateTime;
    ChatMessageType     Type;
    ChatMetadataFlags   Flags;
    union {
        ChatGenericMessage Whisper;
        ChatGenericMessage Local;
        ChatGenericMessage Map;
        ChatGenericMessage Party;
        ChatGenericMessage Squad;
        ChatStringUTF8     SquadMessage;
        ChatGenericMessage TeamPvP;
        ChatGenericMessage ErrorGeneric;
    };
} EvChatMessage;

// =========================================================================
// Events: Alerts integration — real-time skin unlock detection
// Struct definitions from jsantorek/GW2-Alerts (used by "Events: Alerts" addon)
// =========================================================================
#define EV_ALERT_UNLOCKED_SKIN "EV_ALERT:UnlockedSkin"

struct AlertUnlockPayload {
    unsigned int ID;
};

static void OnEvAlertSkinUnlocked(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (!eventArgs) return;
    auto* payload = (AlertUnlockPayload*)eventArgs;
    if (payload->ID != 0) {
        AlterEgo::GW2API::MarkSkinUnlocked(payload->ID);
        Skinventory::OwnedSkins::MarkOwned(payload->ID);
        g_SkinShopListDirty = true;
    }
}

#define EV_ALERT_ACHIEVEMENT_COMPLETED "EV_ALERT:AchievementCompleted"

// When native API becomes available, signal render thread to query progress + clears.
// This handler may run on native API's thread — never call Events_Raise here.

static void OnEvAlertAchievementCompleted(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (!eventArgs) return;
    // This alert describes the logged-in character, not necessarily the
    // account selected in the Achievements view. Never apply it to another
    // account's progress file.
    // The selected-account UI filter is render-owned. The cached account
    // identity is checked under the progress mutex below; the outgoing
    // account's final valid alert will be flushed before switching.
    const std::string loggedIn = AlterEgo::GW2API::GetCurrentAccountName();
    if (loggedIn.empty())return;
    auto* payload = (AlertUnlockPayload*)eventArgs;
    if (payload->ID != 0) {
        {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            if(g_AchSwitchingAccount||g_AchCachedAccount!=loggedIn)return;
            auto it = g_AchProgress.find(payload->ID);
            if (it != g_AchProgress.end()) {
                // Increment progress — handle completion based on achievement type
                it->second.current++;
                if (it->second.max > 0 && it->second.current >= it->second.max) {
                    // Check if this is a repeatable achievement
                    auto defIt = g_AchDefs.find(payload->ID);
                    bool isRepeatable = false;
                    bool isDailyOrWeekly = false;
                    if (defIt != g_AchDefs.end()) {
                        for (const auto& f : defIt->second->flags) {
                            if (f == "Repeatable") isRepeatable = true;
                            if (f == "Daily" || f == "Weekly") isDailyOrWeekly = true;
                        }
                    }
                    if (isRepeatable && !isDailyOrWeekly) {
                        // Infinite repeatable — reset to 0, bump repeat count
                        it->second.current = 0;
                        it->second.repeated++;
                        it->second.done = false;
                        it->second.completed_bits.clear();
                    } else {
                        // Non-repeatable or daily/weekly — mark done
                        it->second.done = true;
                    }
                }
            } else {
                // No existing progress — create with current=1, don't assume done
                AchProgress p;
                p.id = payload->ID;
                p.current = 1;
                g_AchProgress[payload->ID] = std::move(p);
            }
            g_AchProgressGen++;
            g_AchProgressDirty = true;
            g_AchOptimisticTime[payload->ID] = std::chrono::steady_clock::now();
        }
    }
}

// Build toast notification state
struct BuildToast {
    bool active = false;
    std::string sender;         // Character name of who sent it
    std::string chat_link;      // The [&...] build template link
    std::string ae2_code;       // Full AE2: code if detected (empty = plain chat link)
    std::string profession;     // Decoded profession name
    std::string spec_name;      // Elite spec if available
    std::string channel;        // "Party", "Squad", "Whisper", etc.
    bool has_gear = false;      // True if AE2 with gear data
};
static BuildToast g_BuildToast;
// Toast and settings are render-owned. Callbacks only publish copied input.
static bool g_ChatBuildDetection = true;  // master toggle
static std::atomic<bool> g_ChatDetectionEnabled{true};
struct ChatBuildInput {ChatMessageType type;std::string content,sender;};
static AlterEgo::Callbacks::UiInbox<ChatBuildInput,128> g_ChatBuildInbox;
static AlterEgo::Callbacks::UiInbox<std::string,16> g_PieBuildInbox;
static AlterEgo::Callbacks::UiToggles g_UiToggles;
static float g_ToastPosX = -1.0f;        // -1 = auto-center
static float g_ToastPosY = 100.0f;
static bool g_ToastPosInitialized = false;
static bool g_ToastNeedsFocus = false;
static std::atomic<bool> g_ChatAddonConnected{false}; // true once we receive any EV_CHAT:Message

// =========================================================================
// Pie UI inter-addon bridge — presence handshake + explicit build import
// Mirror side of ../pie_ui/src/chat/AlterEgoBridge. When Pie UI is present it
// renders chat build codes as clickable links, so AE suppresses its own
// auto-toast on detection; an explicit click in Pie raises IMPORT_BUILD, which
// AE surfaces as the same toast for confirmation.
// =========================================================================
#define EV_PIE_UI_READY            "EV_PIE_UI_READY"
#define EV_PIE_UI_PING             "EV_PIE_UI_PING"
#define EV_ALTER_EGO_READY         "EV_ALTER_EGO_READY"
#define EV_ALTER_EGO_PING          "EV_ALTER_EGO_PING"
#define EV_ALTER_EGO_IMPORT_BUILD  "EV_ALTER_EGO_IMPORT_BUILD"
// Pie UI generic action: hand it a NUL-terminated "[&...]" chatcode and it
// performs the link's native action on the game thread. For a wardrobe-template
// link that opens the native Wardrobe Template preview window (read-only).
#define EV_PIEUI_OPEN_CHATLINK     "EV_PIEUI_OPEN_CHATLINK"

static std::atomic<bool> g_PieUIPresent{false};
static void OnPieUIReady(void* eventArgs);

// Ask Pie UI to preview a chatcode natively. No-op if Pie isn't loaded or the
// code is empty. Nexus dispatches synchronously, so c_str() is valid for the
// raise; Pie copies the string immediately.
static void PieUI_PreviewChatLink(const std::string& chatCode) {
    if (!APIDefs || !APIDefs->Events_Raise || chatCode.empty()) return;
    if (!g_PieUIPresent.load()) return;
    APIDefs->Events_Raise(EV_PIEUI_OPEN_CHATLINK, (void*)chatCode.c_str());
}
static void OnAlterEgoPing(void* eventArgs);
static void OnPieImportBuild(void* eventArgs);

static void OnEvChatMessage(void* eventArgs);
static void PushGW2Theme();
static void PopGW2Theme();
struct ThemeGuard {
    ThemeGuard()  { PushGW2Theme(); }
    ~ThemeGuard() { PopGW2Theme(); }
};

// Character list sorting
// Append new modes to preserve values already stored in char_sort.json.
enum CharSortMode { Sort_Custom = AlterEgo::CharacterOrder::Custom, Sort_Name, Sort_Profession, Sort_Level, Sort_Age, Sort_Birthday, Sort_LastLogin };
static int g_CharSortMode = Sort_Custom;
static bool g_CharSortAscending = true;            // true = default direction, false = reversed
static std::vector<int> g_CharDisplayOrder;        // indices into GetCharacters()
static nlohmann::json g_LegacyCustomOrder=nlohmann::json::array();
static nlohmann::json g_PendingCustomOrder=nlohmann::json::array();
static uint64_t g_CharOrderPreferenceRevision=0;
static AlterEgo::CharacterIdentity::Selection g_CharDragSelection;
static std::shared_ptr<const std::vector<AlterEgo::Character>> g_CharOrderRoster;
static std::vector<std::string> g_CharOrderAccounts;
static int g_CharOrderBirthdayDay=-1;
static bool g_CharLastLoginSortDirty = false;        // rebuild recency sort after Mumble character change

static bool SaveCharSortConfig();
static void LoadCharSortConfig();
static int DaysUntilBirthday(const std::string& created);

// Canonical sorter shared by the main roster and every character list.
std::vector<std::string> OrderedCharacterAccounts(){
    std::vector<std::string> out;
    const auto current=AlterEgo::GW2API::GetCurrentAccountName();
    if(!current.empty())out.push_back(current);
    for(const auto& acct:AlterEgo::GW2API::GetAccounts())
        if(acct.name!=current)out.push_back(acct.name);
    return out;
}
// A stale cached character row must never resolve to a new owner of its name.
static std::optional<AlterEgo::CharacterIdentity::Address> CharacterAddressFor(const AlterEgo::Character& ch){
    const auto address=AlterEgo::NativeAccounts::ResolveCharacterAddress(ch.account_name,ch.name);
    const auto evidence=AlterEgo::CharacterIdentity::Normalize({ch.created,ch.profession,ch.race});
    if(!address||!evidence||!(address->binding.evidence==*evidence))return std::nullopt;
    return address;
}
static std::optional<AlterEgo::CharacterIdentity::Address> ResolveSourceIdentity(
    const AlterEgo::CharacterIdentity::Provenance& provenance){
    if(!provenance.Valid())return std::nullopt;
    const auto address=AlterEgo::NativeAccounts::ResolveCharacterEvidence(provenance.account,provenance.evidence);
    return address&&provenance.Matches(*address)?address:std::nullopt;
}
static AlterEgo::CharacterIdentity::Selection SelectCharacterIdentity(const AlterEgo::Character& ch){
    using namespace AlterEgo::CharacterIdentity;
    const auto evidence=Normalize({ch.created,ch.profession,ch.race});
    if(!evidence)return {};
    Selection selected{{ch.account_name,0},*evidence,ch.name,true};
    if(const auto address=CharacterAddressFor(ch))selected.Accept(*address);
    return selected;
}
static bool ResolveSelection(AlterEgo::CharacterIdentity::Selection& selected){
    using namespace AlterEgo;
    if(selected.Empty()||selected.ref.account.empty())return false;
    const auto address=selected.ref.Valid()?NativeAccounts::ResolveCharacterAddress(selected.ref):
        NativeAccounts::ResolveCharacterEvidence(selected.ref.account,selected.evidence);
    return address&&selected.Accept(*address);
}
static bool MatchesSelection(const AlterEgo::CharacterIdentity::Selection& selected,const AlterEgo::Character& ch){
    using namespace AlterEgo::CharacterIdentity;
    if(selected.Empty())return false;
    if(selected.ref.Valid()){
        const auto address=CharacterAddressFor(ch);
        return address&&selected.Matches(*address);
    }
    const auto evidence=Normalize({ch.created,ch.profession,ch.race});
    return evidence&&selected.MatchesPreview(ch.account_name,ch.name,*evidence);
}
static bool SelectionRetired(const AlterEgo::CharacterIdentity::Selection& selected){
    using namespace AlterEgo;
    return selected.ref.Valid()&&NativeAccounts::CharacterReferenceRetired(selected.ref,selected.evidence);
}
static std::optional<AlterEgo::Character> BoundCharacter(const std::optional<AlterEgo::CharacterIdentity::Address>& target){
    if(!target)return std::nullopt;
    const auto chars=AlterEgo::GW2API::GetCharacters();
    for(const auto& ch:*chars)if(ch.account_name==target->account&&ch.name==target->name){
        const auto current=CharacterAddressFor(ch);
        if(current&&AlterEgo::CharacterIdentity::SameAddress(*target,*current))return ch;
    }
    return std::nullopt;
}
static AlterEgo::CharacterOrder::Entry CharacterOrderEntry(const AlterEgo::Character& ch){
    AlterEgo::CharacterOrder::Entry entry{{ch.account_name,ch.name},ch.profession,ch.created,ch.level,
        g_CharSortMode==Sort_Birthday?DaysUntilBirthday(ch.created):-1,CharacterLastLogin(ch)};
    if(const auto address=CharacterAddressFor(ch))entry.ref=address->Reference();
    return entry;
}
static std::vector<int> SortCharacterEntries(const std::vector<AlterEgo::CharacterOrder::Entry>& entries){
    const auto ranks=AlterEgo::NativeAccounts::CustomOrderRanks();
    return AlterEgo::CharacterOrder::Indices(entries,g_CharSortMode,g_CharSortAscending,{},
        AlterEgo::GW2API::IsMultiAccount()?OrderedCharacterAccounts():std::vector<std::string>{},&ranks);
}
std::vector<std::pair<std::string,std::string>> OrderedCharacterKeys(
    const std::set<std::pair<std::string,std::string>>& requested){
    using namespace AlterEgo;
    auto remaining=requested;
    std::vector<CharacterOrder::Entry> entries;
    const auto chars=GW2API::GetCharacters();
    for(const auto& ch:*chars){
        const CharacterOrder::Key key{ch.account_name,ch.name};
        if(remaining.erase(key))entries.push_back(CharacterOrderEntry(ch));
    }
    // Roster-only identities can still use Name, Custom and Last Login.
    // For metadata-dependent modes they sort after fully retrieved characters.
    for(const auto& key:remaining){
        CharacterOrder::Entry entry;entry.key=key;
        if(const auto address=NativeAccounts::ResolveCharacterAddress(key.first,key.second))entry.ref=address->Reference();
        auto login=g_LoginHistory.find(key);if(login!=g_LoginHistory.end())entry.login=login->second;
        entries.push_back(std::move(entry));
    }
    std::vector<CharacterOrder::Key> out;
    for(int i:SortCharacterEntries(entries))out.push_back(entries[i].key);
    return out;
}
std::vector<std::string> OrderedCharacterNames(const std::vector<std::string>& names){
    using namespace AlterEgo;
    std::set<CharacterOrder::Key> keys;
    const auto chars=GW2API::GetCharacters();
    for(const auto& name:names){
        auto owner=GW2API::GetAccountForCharacter(name);
        if(owner.empty()){
            // Legacy/name-only UI records may not have a verified roster yet.
            // Use a unique cached identity for presentation only.
            std::set<std::string> candidates;
            for(const auto& ch:*chars)if(ch.name==name&&!ch.account_name.empty())candidates.insert(ch.account_name);
            if(candidates.size()==1)owner=*candidates.begin();
        }
        keys.emplace(owner,name);
    }
    std::vector<std::string> out;
    for(const auto& key:OrderedCharacterKeys(keys))out.push_back(key.second);
    return out;
}
static int CharacterOrderCalendarDay(){
    const auto now=std::time(nullptr);
    const auto* day=std::localtime(&now);
    return day?day->tm_year*366+day->tm_yday:-1;
}
static int CharacterDisplayIndex(const std::pair<std::string,std::string>& key){
    if(!g_CharOrderRoster||key.second.empty())return -1;
    return AlterEgo::RecordSelection::IndexOf(g_CharDisplayOrder,key,[](int i){
        if(i<0||i>=(int)g_CharOrderRoster->size())return std::pair<std::string,std::string>{};
        const auto& ch=(*g_CharOrderRoster)[i];return std::make_pair(ch.account_name,ch.name);
    });
}
static void ReconcileSelectedCharacter(){
    const auto old=g_SelectedCharacter.name;
    if(ResolveSelection(g_SelectedCharacter)&&old!=g_SelectedCharacter.name)g_DetailsFetched=false;
}
static int SelectedCharacterIndex(const std::vector<AlterEgo::Character>& chars){
    ReconcileSelectedCharacter();
    for(size_t i=0;i<chars.size();++i)if(MatchesSelection(g_SelectedCharacter,chars[i]))return static_cast<int>(i);
    return -1;
}
static int SelectedCharacterDisplayIndex(){
    if(!g_CharOrderRoster)return -1;
    const int index=SelectedCharacterIndex(*g_CharOrderRoster);
    if(index<0)return -1;
    const auto it=std::find(g_CharDisplayOrder.begin(),g_CharDisplayOrder.end(),index);
    return it==g_CharDisplayOrder.end()?-1:static_cast<int>(it-g_CharDisplayOrder.begin());
}
static void SelectCharacterAt(int displayIndex){
    g_SelectedCharacter={};
    if(!g_CharOrderRoster||displayIndex<0||displayIndex>=(int)g_CharDisplayOrder.size())return;
    const int i=g_CharDisplayOrder[displayIndex];
    if(i>=0&&i<(int)g_CharOrderRoster->size())g_SelectedCharacter=SelectCharacterIdentity((*g_CharOrderRoster)[i]);
}
static void RebuildCharDisplayOrder(const std::shared_ptr<const std::vector<AlterEgo::Character>>& charsSnapshot = AlterEgo::GW2API::GetCharacters()) {
    std::vector<AlterEgo::CharacterOrder::Entry> entries;
    entries.reserve(charsSnapshot->size());
    for(const auto& ch:*charsSnapshot)entries.push_back(CharacterOrderEntry(ch));
    g_CharDisplayOrder=SortCharacterEntries(entries);
    g_CharOrderRoster=charsSnapshot;
    g_CharOrderAccounts=OrderedCharacterAccounts();
    g_CharOrderPreferenceRevision=AlterEgo::NativeAccounts::CustomOrderRevision();
    g_CharOrderBirthdayDay=g_CharSortMode==Sort_Birthday?CharacterOrderCalendarDay():-1;
    if(g_CharSortMode==Sort_LastLogin)g_CharLastLoginSortDirty=false;
}

// Numeric indices are valid only for the roster they were built from. A data
// refresh with unchanged identities/sort keys does not require a re-sort.
static void EnsureCharDisplayOrder(const std::shared_ptr<const std::vector<AlterEgo::Character>>& roster){
    bool changed=!g_CharOrderRoster||g_CharOrderRoster->size()!=roster->size()||
        g_CharOrderAccounts!=OrderedCharacterAccounts()||
        (g_CharSortMode==Sort_Custom&&g_CharOrderPreferenceRevision!=AlterEgo::NativeAccounts::CustomOrderRevision())||
        (g_CharSortMode==Sort_Birthday&&g_CharOrderBirthdayDay!=CharacterOrderCalendarDay())||
        (g_CharSortMode==Sort_LastLogin&&g_CharLastLoginSortDirty);
    if(!changed&&g_CharOrderRoster!=roster){
        for(size_t i=0;i<roster->size();++i){
            const auto& a=(*g_CharOrderRoster)[i];const auto& b=(*roster)[i];
            if(a.name!=b.name||a.account_name!=b.account_name||
                (g_CharSortMode==Sort_Custom&&(a.created!=b.created||a.profession!=b.profession||a.race!=b.race))||
                (g_CharSortMode==Sort_Profession&&a.profession!=b.profession)||
                (g_CharSortMode==Sort_Level&&a.level!=b.level)||
                ((g_CharSortMode==Sort_Age||g_CharSortMode==Sort_Birthday)&&a.created!=b.created)){changed=true;break;}
        }
    }
    if(!changed){g_CharOrderRoster=roster;return;}
    RebuildCharDisplayOrder(roster);
}

// Character refresh selection popup state
static bool g_RefreshPopupOpen = false;
static bool g_RefreshListFetching = false;     // explicit roster refresh in progress
static ImVec2 g_RefreshPopupAnchor{};
static int64_t g_RefreshListFinished=0;
static std::string g_RefreshListAccount; // exact account scope captured at admission
static std::string g_RefreshListNotice;
static bool g_RefreshListFailed=false;
static bool g_AutoCharListRequested = false;   // auto-fetched char list on native API connect
static std::vector<std::string> g_RefreshNames; // display labels at admission
static std::vector<AlterEgo::CharacterIdentity::Address> g_RefreshAddresses;
static std::vector<bool> g_RefreshSelection;    // parallel checkbox state

// Multi-account selector state
static std::string g_SelectedAccountFilter;    // empty = "All Accounts", else specific account GUID
static void OpenInventoryItem(uint32_t id){if(id){MarkNavigation();g_SelectedAccountFilter=AlterEgo::NativeAccounts::RevealAccount(id,g_SelectedAccountFilter);AlterEgo::NativeAccounts::RevealItem(id);g_SwitchToInventory=true;}}

// Returns the account name to use for per-account queries (dropdown selection only)
std::string GetEffectiveAccountName() {
    if (!g_SelectedAccountFilter.empty()) return g_SelectedAccountFilter;
    // "All Accounts" is a valid character/Compare view but never a source
    // for account-scoped ownership, Vault, Clears or achievement data.
    if (AlterEgo::GW2API::IsMultiAccount()) return {};
    const std::string loggedIn = AlterEgo::GW2API::GetCurrentAccountName();
    if (!loggedIn.empty()) return loggedIn;
    if (AlterEgo::GW2API::HasAccountsData()) {
        const auto& accounts = AlterEgo::GW2API::GetAccounts();
        if (accounts.size() == 1) return accounts.front().name;
    }
    return {};
}

 // Achievement progress is per-account, not per-currently-logged-in character.
 // In multi-account mode the dropdown's selected account is authoritative.
 // In single-account mode, use native API's account name even when the dropdown is empty.
static std::string GetAchievementCacheAccount() {
    if (!g_SelectedAccountFilter.empty()) return g_SelectedAccountFilter;
    if (AlterEgo::GW2API::IsMultiAccount()) return {};
    const std::string current = AlterEgo::GW2API::GetCurrentAccountName();
    if (!current.empty()) return current;
    if (AlterEgo::GW2API::HasAccountsData()) {
        const auto& accounts = AlterEgo::GW2API::GetAccounts();
        if (accounts.size() == 1) return accounts.front().name;
    }
    return {};
}

bool WriteFileAtomic(const std::string& finalPath,const std::string& content) {
    std::string error;
    return AlterEgo::Storage::ReplaceFile(std::filesystem::u8path(finalPath),content,error);
}
static bool SavePreferenceFile(const std::string& path,const nlohmann::json& data) {
    std::string error;
    if(AlterEgo::Storage::ReplaceFile(std::filesystem::u8path(path),data.dump(2),error))return true;
    if(APIDefs)APIDefs->Log(LOGL_WARNING,"AlterEgo","Could not save preferences; previous file retained.");
    return false;
}

// Gear customization dialog state
static bool g_GearDialogOpen = false;
static AlterEgo::SavedBuild g_GearDialogDraft;
static bool g_GearDialogDirty=false;
static nlohmann::json g_GearDialogBaseline;
static bool GearDraftDirty(){
    return g_GearDialogOpen&&g_GearDialogDirty&&AlterEgo::GW2API::EditorBuildSnapshot(g_GearDialogDraft)!=g_GearDialogBaseline;
}
static AddonWindowFocusRequest g_GearDialogFocus;
static std::string g_GearDialogSlot;          // Which slot is being edited
static std::string g_GearDialogBuildId;       // Which saved build
static char g_GearStatSearch[128] = "";       // Search filter for stat combos
static uint32_t g_GearSelectedStatId = 0;     // Currently selected stat in dialog
static int g_GearSelectorTab = 0;             // 0 = Stats, 1 = Rune/Sigil, 2 = Weapon Type
static bool g_GearEditingSigil2 = false;      // true = editing second sigil slot (two-handed)
static int g_GearEditingInfusionIndex = 0;
static uint32_t g_GearExactInfusionId = 0;

// Save to Library dialog state
static bool g_SaveLibDialogOpen = false;
static std::string g_SaveLibError;
static nlohmann::json g_SaveLibBaseline;
static AddonWindowFocusRequest g_SaveLibDialogFocus;
static char g_SaveLibName[128] = "";
static std::vector<AlterEgo::GameMode> g_SaveLibModes{AlterEgo::GameMode::PvE};
static bool g_SaveLibNameManuallyEdited = false;
static constexpr int kSaveEntireEquipmentSet = 12;
static std::string g_EquipmentSetSelectedId, g_SaveLibAccount;
static constexpr int kSaveFashionTarget=3;
static int g_SaveLibEquipmentMode=0; // offer exact match, independent, new named set
static std::string g_SaveLibSetName;
static std::set<std::string> g_SaveLibSetKinds;
static std::vector<std::pair<std::string,std::string>> g_SaveLibExactSets;
static std::string g_SaveLibSelectedSet;
static int g_SaveLibTarget = 0; // 0 Loadout, 1 new component, 2 replace existing, 3 Fashion
static AlterEgo::Fashion::Look g_SaveLibPendingFashion;
static std::string g_SaveLibFashionError;
static std::string g_SaveLibUpdateId;
static std::string g_ComponentSelectIdPending;
static AlterEgo::SavedBuild g_SaveLibPendingCapture;
static std::string g_SaveLibPendingKind, g_SaveLibPendingId, g_SaveLibPendingName;
static bool g_SaveLibUpdateConfirmPending = false;
static int g_SaveLibComponentKind = 0;
static std::set<std::string> g_ComponentKindFilters; // empty = all categories
static int g_ComponentPendingKind = -1; // typed capture reveal, never a filter
static std::string g_ComponentSelectNamePending; // Newly captured component to highlight

static int g_SaveLibEquipTab = 0;          // Selected equipment tab number (1-based)
static std::string g_SaveLibCharName;      // Original capture label
static std::optional<AlterEgo::CharacterIdentity::Address> g_SaveLibAddress;
static int g_SaveLibBuildTab = 0;       // Live build tab ID being captured
static nlohmann::json CaptureFormSnapshot(){
    return {{"target",g_SaveLibTarget},{"kind",g_SaveLibComponentKind},{"update",g_SaveLibUpdateId},
        {"modes",g_SaveLibModes},{"build",g_SaveLibBuildTab},{"equipment",g_SaveLibEquipTab},
        {"equipment_mode",g_SaveLibEquipmentMode},{"set_name",g_SaveLibSetName},
        {"set_kinds",g_SaveLibSetKinds},{"name",g_SaveLibNameManuallyEdited?std::string(g_SaveLibName):std::string{}}};
}

// Saved vs. retrieved character comparison state (never mutates a saved build).
static std::string g_LibCompareBuildId;
static AlterEgo::CharacterIdentity::Selection g_LibCompareSelection; // empty = active (auto)
// Retained only until legacy settings have been transactionally staged.
static nlohmann::json g_LegacyLoadoutExpectations=nlohmann::json::array();
static bool g_LibCompareSavedManual = false;
static AlterEgo::CharacterIdentity::Selection g_LibComparePreviousSelection;
static int g_LibCompareBuildTab = -1;    // -1 follows active build tab
static int g_LibCompareEquipTab = 0;     // 0 follows active equipment tab
static bool g_LibCompareShowMatches = false;
static bool g_LibCompareExactItems = false;
// Stable character displayed by the Compare renderer in this frame.
static AlterEgo::CharacterIdentity::Selection g_LibCompareRenderedSelection;

// Compute days until next character birthday from ISO date string (e.g. "2013-06-25T...")
// Returns -1 if the date can't be parsed. Returns 0 on the birthday itself.
static int DaysUntilBirthday(const std::string& created) {
    if (created.size() < 10) return -1;
    int year, month, day;
    if (sscanf(created.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return -1;

    time_t now = std::time(nullptr);
    struct tm* today = std::localtime(&now);

    // Build this year's birthday
    struct tm bday = {};
    bday.tm_year = today->tm_year;
    bday.tm_mon = month - 1;
    bday.tm_mday = day;
    bday.tm_hour = 0;
    time_t bdayTime = std::mktime(&bday);

    // Build today at midnight for clean day diff
    struct tm todayMid = *today;
    todayMid.tm_hour = 0;
    todayMid.tm_min = 0;
    todayMid.tm_sec = 0;
    time_t todayTime = std::mktime(&todayMid);

    int diff = (int)std::difftime(bdayTime, todayTime) / 86400;
    if (diff < 0) {
        // Birthday already passed this year — next year
        bday.tm_year = today->tm_year + 1;
        bdayTime = std::mktime(&bday);
        diff = (int)std::difftime(bdayTime, todayTime) / 86400;
    }
    return diff;
}

// Compute character age in years from ISO date string
static int CharacterAgeYears(const std::string& created) {
    if (created.size() < 10) return -1;
    int year, month, day;
    if (sscanf(created.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return -1;

    time_t now = std::time(nullptr);
    struct tm* today = std::localtime(&now);
    int age = (today->tm_year + 1900) - year;
    // Haven't had birthday yet this year
    if ((today->tm_mon + 1) < month || ((today->tm_mon + 1) == month && today->tm_mday < day))
        age--;
    return age;
}

// Strip GW2 markup tags from description text
// Converts <br> to newline, removes <c=@...> and </c> tags
static std::string StripGW2Markup(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            // Find closing >
            size_t end = text.find('>', i);
            if (end != std::string::npos) {
                std::string tag = text.substr(i, end - i + 1);
                // Convert <br> to newline
                if (tag == "<br>" || tag == "<br/>" || tag == "<br />") {
                    result += '\n';
                }
                // Skip all other tags (<c=@...>, </c>, etc.)
                i = end + 1;
                continue;
            }
        }
        result += text[i];
        i++;
    }
    return result;
}

// GW2 API weapon type → current in-game display name
static const char* WeaponDisplayName(const std::string& apiName) {
    return AlterEgo::TooltipCore::WeaponTypeLabel(apiName);
}

// GW2 rarity colors
static ImVec4 GetRarityColor(const std::string& rarity) {
    if (rarity == "Legendary")  return ImVec4(0.63f, 0.39f, 0.78f, 1.0f);
    if (rarity == "Ascended")   return ImVec4(0.90f, 0.39f, 0.55f, 1.0f);
    if (rarity == "Exotic")     return ImVec4(1.00f, 0.65f, 0.00f, 1.0f);
    if (rarity == "Rare")       return ImVec4(1.00f, 0.86f, 0.20f, 1.0f);
    if (rarity == "Masterwork") return ImVec4(0.12f, 0.71f, 0.12f, 1.0f);
    if (rarity == "Fine")       return ImVec4(0.35f, 0.63f, 0.90f, 1.0f);
    return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
}

// GW2 profession colors
static ImVec4 GetProfessionColor(const std::string& prof) {
    if (prof == "Guardian")     return ImVec4(0.47f, 0.71f, 0.86f, 1.0f);
    if (prof == "Warrior")      return ImVec4(1.00f, 0.83f, 0.22f, 1.0f);
    if (prof == "Engineer")     return ImVec4(0.85f, 0.56f, 0.25f, 1.0f);
    if (prof == "Ranger")       return ImVec4(0.55f, 0.80f, 0.28f, 1.0f);
    if (prof == "Thief")        return ImVec4(0.78f, 0.31f, 0.31f, 1.0f);
    if (prof == "Elementalist") return ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
    if (prof == "Mesmer")       return ImVec4(0.71f, 0.31f, 0.71f, 1.0f);
    if (prof == "Necromancer")  return ImVec4(0.32f, 0.75f, 0.54f, 1.0f);
    if (prof == "Revenant")     return ImVec4(0.63f, 0.22f, 0.22f, 1.0f);
    return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
}

// One name-color palette for the roster, its header, and Compare.
static void RenderProfessionCharacterName(const std::string& name,
                                          const std::string& profession) {
    ImGui::TextColored(GetProfessionColor(profession), "%s", name.c_str());
}
static void DrawProfessionCharacterName(ImDrawList* draw, ImVec2 pos,
                                        const std::string& name,
                                        const std::string& profession) {
    draw->AddText(pos, ImGui::ColorConvertFloat4ToU32(
        GetProfessionColor(profession)), name.c_str());
}
// Optional URL fallback: fixed profession artwork now comes from DLL resources.
static const char* GetProfessionIconUrl(const std::string& prof) {
    const auto info = AlterEgo::GW2API::GetProfessionInfo(prof);
    if (info && !info->icon_url.empty()) return info->icon_url.c_str();
    return nullptr;
}

// Stable icon ID for profession icons (offset to avoid collision with item/skill IDs)
static uint32_t GetProfessionIconId(const std::string& prof) {
    static const std::unordered_map<std::string, uint32_t> ids = {
        {"Guardian", 9000001}, {"Warrior", 9000002}, {"Engineer", 9000003},
        {"Ranger", 9000004}, {"Thief", 9000005}, {"Elementalist", 9000006},
        {"Mesmer", 9000007}, {"Necromancer", 9000008}, {"Revenant", 9000009}
    };
    auto it = ids.find(prof);
    return it != ids.end() ? it->second : 0;
}

// Stable large-emblem IDs; never depend on a compiler-specific string hash.
static uint32_t GetProfessionBigIconId(const std::string& prof) {
    const uint32_t small = GetProfessionIconId(prof);
    return small ? small + 200000u : 0u;
}

// Crafting discipline icon helpers (wiki tango icons, stable IDs 9100001+)
static uint32_t GetCraftingIconId(const std::string& disc) {
    static const std::unordered_map<std::string, uint32_t> ids = {
        {"Armorsmith", 9100001}, {"Artificer", 9100002}, {"Chef", 9100003},
        {"Huntsman", 9100004}, {"Jeweler", 9100005}, {"Leatherworker", 9100006},
        {"Scribe", 9100007}, {"Tailor", 9100008}, {"Weaponsmith", 9100009}
    };
    auto it = ids.find(disc);
    return it != ids.end() ? it->second : 0;
}

static const char* GetCraftingIconUrl(const std::string& disc) {
    static const std::unordered_map<std::string, std::string> urls = {
        {"Armorsmith",    "https://wiki.guildwars2.com/images/3/32/Armorsmith_tango_icon_20px.png"},
        {"Artificer",     "https://wiki.guildwars2.com/images/b/b7/Artificer_tango_icon_20px.png"},
        {"Chef",          "https://wiki.guildwars2.com/images/8/8f/Chef_tango_icon_20px.png"},
        {"Huntsman",      "https://wiki.guildwars2.com/images/f/f3/Huntsman_tango_icon_20px.png"},
        {"Jeweler",       "https://wiki.guildwars2.com/images/f/f2/Jeweler_tango_icon_20px.png"},
        {"Leatherworker", "https://wiki.guildwars2.com/images/e/e5/Leatherworker_tango_icon_20px.png"},
        {"Scribe",        "https://wiki.guildwars2.com/images/0/0b/Scribe_tango_icon_20px.png"},
        {"Tailor",        "https://wiki.guildwars2.com/images/4/4d/Tailor_tango_icon_20px.png"},
        {"Weaponsmith",   "https://wiki.guildwars2.com/images/4/46/Weaponsmith_tango_icon_20px.png"}
    };
    auto it = urls.find(disc);
    return it != urls.end() ? it->second.c_str() : nullptr;
}

// Explicit user copy actions use the Nexus-owned ImGui callbacks.
void CopyToClipboard(const std::string& text) {
    if(!ImGui::GetCurrentContext()||!ImGui::GetIO().SetClipboardTextFn){
        if(APIDefs)APIDefs->GUI_SendAlert("Clipboard unavailable.");
        return;
    }
    ImGui::SetClipboardText(text.c_str());
}

// Settings persistence
static bool SaveSettings() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "settings.json");
    nlohmann::json j;
    if(!g_LegacyLoadoutExpectations.empty())j["character_loadout_expectations"]=g_LegacyLoadoutExpectations;
    j["show_qa_icon"] = g_ShowQAIcon;
    j["compact_char_list"] = g_CompactCharList;
    j["fast_load_mode"] = AlterEgo::FastLoadUI::ModeName(g_FastLoadMode);
    j["show_crafting_icons"] = g_ShowCraftingIcons;
    j["show_age"] = g_ShowAge;
    j["show_playtime"] = g_ShowPlaytime;
    j["show_last_login"] = g_ShowLastLogin;
    j["auto_refresh_enabled"] = g_AutoRefreshEnabled;
    j["auto_refresh_datasets"] = AlterEgo::AutoRefreshPolicy::GetMask();
    j["auto_refresh_dataset_count"] = static_cast<uint32_t>(AlterEgo::AutoRefreshPolicy::Dataset::Count);
    j["objective_tracking_minutes"] = AlterEgo::RefreshEngine::ObjectiveMinutes();
    j["wallet_pins"] = AlterEgo::NativeAccounts::WalletPins();
    j["inventory_grouped"]=AlterEgo::NativeAccounts::InventoryGrouped();
    j["inventory_expanded_groups"]=AlterEgo::NativeAccounts::InventoryExpandedGroups();
    j["guild_expanded_groups"]=AlterEgo::NativeAccounts::GuildExpandedGroups();
    j["auto_refresh_on_specialization"] = g_AutoRefreshOnSpecialization;
    j["warn_stale_specialization"] = g_WarnStaleSpecialization;
    j["birthday_mode"] = g_BirthdayMode;
    j["char_list_width"] = g_CharListWidth;
    j["lib_list_width"] = g_LibListWidth;
    j["skin_list_width"]=g_SkinListWidth;
    j["inventory_list_width"]=g_InventoryListWidth;
    j["fashion_list_width"]=g_FashionListWidth;
    j["component_list_width"]=g_ComponentListWidth;
    j["equipment_set_list_width"]=g_EquipmentSetListWidth;
    j["library_collection"] = g_LibraryCollection;
    j["chat_build_detection"] = g_ChatBuildDetection;
    j["toast_pos_x"] = g_ToastPosX;
    j["toast_pos_y"] = g_ToastPosY;
    j["font"] = {
        {"face", (int)g_FontConfig.face},
        {"path", g_FontConfig.customPath},
        {"px",   g_FontConfig.px},
    };
    return SavePreferenceFile(path,j);
}

static void LoadSettings() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "settings.json");
    std::ifstream file(path);
    if (!file.is_open()) return;
    nlohmann::json j;
    try { j = nlohmann::json::parse(file); }
    catch (...) { return; }
    auto getBool = [&](const char* k, bool& dst) {
        if (j.contains(k) && j[k].is_boolean()) dst = j[k].get<bool>();
    };
    auto getInt = [&](const char* k, int& dst) {
        if (j.contains(k) && j[k].is_number_integer()) dst = j[k].get<int>();
    };
    auto getFloat = [&](const char* k, float& dst) {
        if (j.contains(k) && j[k].is_number()) dst = j[k].get<float>();
    };
    g_LegacyLoadoutExpectations=nlohmann::json::array();
    if(j.contains("character_loadout_expectations")){
        g_LegacyLoadoutExpectations=j["character_loadout_expectations"];
        try{
            if(AlterEgo::NativeAccounts::StageLoadoutPreferences(AlterEgo::CharacterLoadout::LegacyChoices(g_LegacyLoadoutExpectations)))
                g_LegacyLoadoutExpectations=nlohmann::json::array();
        }catch(...){/* keep original input for recovery */}
    }
    getBool("show_qa_icon", g_ShowQAIcon);
    getBool("compact_char_list", g_CompactCharList);
    if(j.contains("fast_load_mode")&&j["fast_load_mode"].is_string())
        g_FastLoadMode=AlterEgo::FastLoadUI::ParseMode(j["fast_load_mode"].get<std::string>());
    getBool("show_crafting_icons", g_ShowCraftingIcons);
    getBool("show_age", g_ShowAge);
    getBool("show_playtime", g_ShowPlaytime);
    getBool("show_last_login", g_ShowLastLogin);
    getBool("auto_refresh_enabled", g_AutoRefreshEnabled);
    if(j.contains("objective_tracking_minutes")&&j["objective_tracking_minutes"].is_number_integer()){
        try{AlterEgo::RefreshEngine::SetObjectiveMinutes(j["objective_tracking_minutes"].get<int>());}catch(...){}
    }
    AlterEgo::AutoRefreshPolicy::SetMask(AlterEgo::AutoRefreshPolicy::AllMask);
    if(j.contains("auto_refresh_datasets")&&j["auto_refresh_datasets"].is_number_unsigned()){
        auto mask=j["auto_refresh_datasets"].get<uint32_t>();
        const auto count=j.value("auto_refresh_dataset_count",15u);
        if(count<=static_cast<uint32_t>(AlterEgo::AutoRefreshPolicy::Dataset::TradingPrices))
            mask|=1u<<static_cast<uint32_t>(AlterEgo::AutoRefreshPolicy::Dataset::TradingPrices);
        AlterEgo::AutoRefreshPolicy::SetMask(mask);
    }
    // Migrate the old preference on next normal settings save.
    const char* walletPinKey=j.contains("wallet_pins")?"wallet_pins":"wallet_favorites";
    if(j.contains(walletPinKey)&&j[walletPinKey].is_array()){
        try{AlterEgo::NativeAccounts::SetWalletPins(j[walletPinKey].get<std::set<uint32_t>>());}catch(...){}
    }
    bool inventoryGrouped=false;getBool("inventory_grouped",inventoryGrouped);
    AlterEgo::NativeAccounts::SetInventoryGrouped(inventoryGrouped);
    if(j.contains("inventory_expanded_groups")&&j["inventory_expanded_groups"].is_array()&&j["inventory_expanded_groups"].size()<=4096){
        try{AlterEgo::NativeAccounts::SetInventoryExpandedGroups(j["inventory_expanded_groups"].get<std::set<std::string>>());}catch(...){}
    }
    if(j.contains("guild_expanded_groups")&&j["guild_expanded_groups"].is_array()&&j["guild_expanded_groups"].size()<=4096){
        try{AlterEgo::NativeAccounts::SetGuildExpandedGroups(j["guild_expanded_groups"].get<std::set<std::string>>());}catch(...){}
    }
    getBool("auto_refresh_on_specialization", g_AutoRefreshOnSpecialization);
    AlterEgo::CharacterActivity::SpecSignal()=g_AutoRefreshOnSpecialization;
    getBool("warn_stale_specialization", g_WarnStaleSpecialization);
    getInt("birthday_mode", g_BirthdayMode);
    getFloat("char_list_width", g_CharListWidth);
    getFloat("lib_list_width", g_LibListWidth);
    getFloat("skin_list_width",g_SkinListWidth);
    getFloat("inventory_list_width",g_InventoryListWidth);
    getFloat("fashion_list_width",g_FashionListWidth);
    getFloat("component_list_width",g_ComponentListWidth);
    getFloat("equipment_set_list_width",g_EquipmentSetListWidth);
    getInt("library_collection", g_LibraryCollection);
    if (g_LibraryCollection < 0 || g_LibraryCollection > 3) g_LibraryCollection = 0;
    getBool("chat_build_detection", g_ChatBuildDetection);
    g_ChatDetectionEnabled.store(g_ChatBuildDetection);
    getFloat("toast_pos_x", g_ToastPosX);
    getFloat("toast_pos_y", g_ToastPosY);
    if (j.contains("font") && j["font"].is_object()) {
        const auto& f = j["font"];
        if (f.contains("face") && f["face"].is_number_integer()) {
            int face = f["face"].get<int>();
            g_FontConfig.face = AlterEgo::Font::DecodeFace(face);
        }
        if (f.contains("path") && f["path"].is_string())
            g_FontConfig.customPath = AlterEgo::Font::LocalCustomPath(AlterEgo::GW2API::GetDataDirectory(),f["path"].get<std::string>());
        if(g_FontConfig.face==AlterEgo::Font::Face::Custom && g_FontConfig.customPath.empty())g_FontConfig.face=AlterEgo::Font::Face::NexusDefault;
        if(g_FontConfig.face==AlterEgo::Font::Face::NexusDefault)g_FontConfig.customPath.clear();
        if (f.contains("px") && f["px"].is_number()) {
            const auto size=f["px"].get<double>();
            if(std::isfinite(size))g_FontConfig.px=static_cast<float>(std::max(10.0,std::min(32.0,size)));
        }
    }
}

// Portable restores are staged separately from ordinary settings saves. The
// registry is authoritative; a stale settings file cannot reapply a choice.
static void ImportPortableLoadoutPreferences(){
    const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"character_loadouts.json");
    try{
        std::ifstream file(std::filesystem::u8path(path),std::ios::binary);if(!file)return;
        const std::string bytes((std::istreambuf_iterator<char>(file)),{});file.close();
        const auto records=AlterEgo::CharacterLoadout::DecodePortable(AlterEgo::Untrusted::Parse(bytes,AlterEgo::Untrusted::Library));
        if(AlterEgo::NativeAccounts::StageLoadoutPreferences(records)){
            std::error_code ec;std::filesystem::remove(std::filesystem::u8path(path),ec);
        }
    }catch(...){/* preserve pending file */}
}

// Character sort persistence
static bool SaveCharSortConfig() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "char_sort.json");
    nlohmann::json j;
    j["sort_mode"] = g_CharSortMode;
    j["sort_ascending"] = g_CharSortAscending;
    if(!g_LegacyCustomOrder.empty())j["legacy_custom_order"]=g_LegacyCustomOrder;
    if(!g_PendingCustomOrder.empty())j["custom_order_refs"]=g_PendingCustomOrder;
    return SavePreferenceFile(path,j);
}

static void LoadCharSortConfig() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "char_sort.json");
    std::ifstream file(path);
    if (!file.is_open()) return;
    try {
        auto j = nlohmann::json::parse(file);
        if (j.contains("sort_mode")) g_CharSortMode = j["sort_mode"].get<int>();
        if (j.contains("sort_ascending")) g_CharSortAscending = j["sort_ascending"].get<bool>();
        g_LegacyCustomOrder=j.value("legacy_custom_order",nlohmann::json::array());
        if(j.contains("custom_order")&&g_LegacyCustomOrder.empty())g_LegacyCustomOrder=j["custom_order"];
        g_PendingCustomOrder=j.value("custom_order_refs",nlohmann::json::array());
        if(!g_PendingCustomOrder.empty()&&AlterEgo::NativeAccounts::StageCustomOrder(AlterEgo::CharacterOrder::DecodeRanks(g_PendingCustomOrder))){
            g_PendingCustomOrder=nlohmann::json::array();SaveCharSortConfig();
        }
    } catch (...) {}
}

// Session restore persistence
static nlohmann::json g_LegacySessionSelection=nullptr;

static bool SaveSession() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "session.json");

    nlohmann::json j;
    j["main_tab"] = g_MainTab;

    const auto selection=AlterEgo::CharacterIdentity::EncodeSelection(g_SelectedCharacter);
    if(!selection.is_null())j["selected_character_ref"]=selection;
    if(!g_LegacySessionSelection.is_null())j["legacy_selected_character"]=g_LegacySessionSelection;
    j["detail_tab"] = g_SelectedTab;
    j["equip_tab"] = g_SelectedEquipTab;
    j["build_tab"] = g_SelectedBuildTab;

    if(!g_LibSelectedId.empty())j["lib_selected_id"]=g_LibSelectedId;
    j["lib_filter_modes"] = g_LibFilterModes;
    j["lib_collapsed_professions"] = g_LibCollapsedProfessions;
    j["component_collapsed_kinds"] = g_ComponentCollapsedKinds;
    if (!g_LibCompareBuildId.empty()) j["compare_saved_id"] = g_LibCompareBuildId;
    const auto compare=AlterEgo::CharacterIdentity::EncodeSelection(g_LibCompareSelection);
    const auto previous=AlterEgo::CharacterIdentity::EncodeSelection(g_LibComparePreviousSelection);
    if(!compare.is_null())j["compare_character_ref"]=compare;
    if(!previous.is_null())j["compare_previous_ref"]=previous;
    j["compare_manual"]=g_LibCompareSavedManual;
    j["compare_build_tab"]=g_LibCompareBuildTab;
    j["compare_equip_tab"]=g_LibCompareEquipTab;

    // Account selector
    j["selected_account"] = g_SelectedAccountFilter;

    // Skinventory state
    j["skin_active_tab"] = g_SkinActiveTab;
    j["skin_filters"]={{"category",g_SkinBrowserFilters.category},{"weight",g_SkinBrowserFilters.weight},
        {"slot",g_SkinBrowserFilters.slot},{"weapon",g_SkinBrowserFilters.weapon},{"rarity",g_SkinBrowserFilters.rarity}};
    j["skin_selected_id"] = g_SkinSelectedId;
    j["skin_show_owned"] = g_SkinShowOwned;
    j["skin_show_unowned"] = g_SkinShowUnowned;
    j["skin_view_mode"] = g_SkinViewMode;

    return SavePreferenceFile(path,j);
}

static void LoadSession() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "session.json");
    std::ifstream file(path);
    if (!file.is_open()) return;
    try {
        auto j = nlohmann::json::parse(file);

        // g_WindowVisible intentionally not restored — always start closed
        if (j.contains("main_tab")) {
            g_MainTab = j["main_tab"].get<int>();
            if (g_MainTab == 1) g_SwitchToLibrary = true;
        }

        // Legacy name-only choices are archived, never rebound to a new owner.
        g_SelectedCharacter={};g_LegacySessionSelection=j.value("legacy_selected_character",nlohmann::json(nullptr));
        if(j.contains("selected_character_ref")){
            const auto selected=AlterEgo::CharacterIdentity::DecodeSelection(j["selected_character_ref"]);
            if(selected)g_SelectedCharacter=*selected;
            else g_LegacySessionSelection=j["selected_character_ref"];
        }else if(j.contains("selected_char")){
            g_LegacySessionSelection={{"character",j["selected_char"]},{"account",j.value("selected_char_account",std::string{})}};
        }
        if (j.contains("detail_tab")) g_SelectedTab = j["detail_tab"].get<int>();
        if (j.contains("equip_tab")) g_SelectedEquipTab = j["equip_tab"].get<int>();
        if (j.contains("build_tab")) g_SelectedBuildTab = j["build_tab"].get<int>();

        if(j.contains("lib_selected_id"))g_LibSelectedId=j["lib_selected_id"].get<std::string>();
        if(j.contains("lib_filter_modes")&&j["lib_filter_modes"].is_array()) {
            g_LibFilterModes.clear();
            for(const auto& value:j["lib_filter_modes"])if(value.is_string())g_LibFilterModes.insert(value.get<std::string>());
            AlterEgo::LibraryFilter::Validate(g_LibFilterModes);
        } else if(j.contains("lib_filter")&&j["lib_filter"].is_number_integer())
            g_LibFilterModes=AlterEgo::LibraryFilter::Legacy(j["lib_filter"].get<int>());
        if (j.contains("lib_collapsed_professions") &&
            j["lib_collapsed_professions"].is_array()) {
            g_LibCollapsedProfessions.clear();
            for (const auto& prof : j["lib_collapsed_professions"])
                if (prof.is_string())
                    g_LibCollapsedProfessions.insert(prof.get<std::string>());
        }
        if(j.contains("component_collapsed_kinds")&&j["component_collapsed_kinds"].is_array()&&j["component_collapsed_kinds"].size()<=32){
            g_ComponentCollapsedKinds.clear();
            for(const auto& kind:j["component_collapsed_kinds"])if(kind.is_string()&&kind.get<std::string>().size()<=64)g_ComponentCollapsedKinds.insert(kind.get<std::string>());
        }
        if (j.contains("compare_saved_id"))
            g_LibCompareBuildId = j["compare_saved_id"].get<std::string>();
        g_LibCompareSelection={};g_LibComparePreviousSelection={};
        if(j.contains("compare_character_ref"))if(const auto v=AlterEgo::CharacterIdentity::DecodeSelection(j["compare_character_ref"]))g_LibCompareSelection=*v;
        if(j.contains("compare_previous_ref"))if(const auto v=AlterEgo::CharacterIdentity::DecodeSelection(j["compare_previous_ref"]))g_LibComparePreviousSelection=*v;
        g_LibCompareSavedManual=j.value("compare_manual",false);
        g_LibCompareBuildTab=std::clamp(j.value("compare_build_tab",-1),-1,100);
        g_LibCompareEquipTab=std::clamp(j.value("compare_equip_tab",0),0,100);

        // Account selector
        if (j.contains("selected_account"))
            g_SelectedAccountFilter = j["selected_account"].get<std::string>();

        // Skinventory
        if (j.contains("skin_active_tab")) g_SkinActiveTab = j["skin_active_tab"].get<int>();
        if(j.contains("skin_filters")&&j["skin_filters"].is_object()) {
            const auto& sf=j["skin_filters"];
            g_SkinBrowserFilters.category=sf.value("category",Skinventory::Choices{});
            g_SkinBrowserFilters.weight=sf.value("weight",Skinventory::Choices{});
            g_SkinBrowserFilters.slot=sf.value("slot",Skinventory::Choices{});
            g_SkinBrowserFilters.weapon=sf.value("weapon",Skinventory::Choices{});
            g_SkinBrowserFilters.rarity=sf.value("rarity",Skinventory::Choices{});
            g_SkinBrowserFilters.Prune();
        } // Old mandatory category navigation is intentionally not a new filter.
        if (j.contains("skin_selected_id")) g_SkinSelectedId = j["skin_selected_id"].get<uint32_t>();
        if (j.contains("skin_show_owned")) g_SkinShowOwned = j["skin_show_owned"].get<bool>();
        if (j.contains("skin_show_unowned")) g_SkinShowUnowned = j["skin_show_unowned"].get<bool>();
        if (j.contains("skin_view_mode")) g_SkinViewMode = j["skin_view_mode"].get<int>();
    } catch (...) {}
}


// =========================================================================
// Achievement Tracker — Persistence
// =========================================================================

static bool SaveAchTrackerState() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "ach_tracked.json");

    nlohmann::json j;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        j["pinned"] = g_AchPinned;
        j["popout_visible"] = g_AchPopoutVisible;
        j["show_completed_steps"] = g_AchShowCompletedSteps;
        j["hide_empty_cats"] = g_AchHideEmptyCats;
        j["hide_completed_cats"] = g_AchHideCompletedCats;
        j["selected_group"] = g_AchSelectedGroupId;
        j["selected_category"] = g_AchSelectedCatId;
        j["tree_width"] = g_AchTreeWidth;
        j["pinned_height"] = g_AchPinnedHeight;
        j["tree_scroll_y"] = g_AchTreeScrollY;
        j["list_scroll_y"] = g_AchListScrollY;

        nlohmann::json expanded = nlohmann::json::array();
        for (const auto& [id, exp] : g_AchExpandedInPopout) {
            if (exp) expanded.push_back(id);
        }
        j["expanded_in_popout"] = expanded;
    }

    return SavePreferenceFile(path,j);
}

static void LoadAchTrackerState() {
    std::string path = AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(), "ach_tracked.json");
    std::ifstream file(path);
    if (!file.is_open()) return;
    try {
        auto j = nlohmann::json::parse(file);
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        if (j.contains("pinned") && j["pinned"].is_array()) {
            g_AchPinned.clear();
            for (const auto& id : j["pinned"]) g_AchPinned.push_back(id.get<uint32_t>());
        }
        g_AchPopoutVisible = j.value("popout_visible", false);
        g_AchShowCompletedSteps = j.value("show_completed_steps", !j.value("show_remaining", true));
        g_AchHideEmptyCats = j.value("hide_empty_cats", false);
        g_AchHideCompletedCats = j.value("hide_completed_cats", false);
        g_AchSelectedGroupId = j.value("selected_group", "");
        g_AchSelectedCatId = j.value("selected_category", 0u);
        g_AchTreeWidth = j.value("tree_width", 220.0f);
        g_AchPinnedHeight = j.value("pinned_height", 200.0f);
        g_AchTreeScrollY = j.value("tree_scroll_y", 0.0f);
        g_AchListScrollY = j.value("list_scroll_y", 0.0f);
        if (j.contains("expanded_in_popout") && j["expanded_in_popout"].is_array()) {
            g_AchExpandedInPopout.clear();
            for (const auto& id : j["expanded_in_popout"]) {
                g_AchExpandedInPopout[id.get<uint32_t>()] = true;
            }
        }
        if (g_AchSelectedCatId > 0) g_AchRestoreScroll = true;
    } catch (...) {}
}

// Persist the validated candidate, never a projection already published to readers.
static bool SaveAchGroupCache(const decltype(g_AchGroups)& groupsCopy,
                              const decltype(g_AchCategories)& catsCopy,
                              int64_t epochSecs) {
    std::lock_guard<std::mutex> persist(g_AchGroupPersistMutex);
    nlohmann::json j;
    j["fetch_time"] = epochSecs;

    {
        nlohmann::json groups = nlohmann::json::array();
        for (const auto& g : groupsCopy) {
            nlohmann::json gj;
            gj["id"] = g.id;
            gj["name"] = g.name;
            gj["description"] = g.description;
            gj["order"] = g.order;
            gj["categories"] = g.categories;
            groups.push_back(gj);
        }
        j["groups"] = groups;

        nlohmann::json cats = nlohmann::json::object();
        for (const auto& [id, c] : catsCopy) {
            nlohmann::json cj;
            cj["id"] = c.id;
            cj["name"] = c.name;
            cj["description"] = c.description;
            cj["order"] = c.order;
            cj["icon_url"] = c.icon_url;
            cj["achievements"] = c.achievements;
            cats[std::to_string(id)] = cj;
        }
        j["categories"] = cats;
    }

    return AlterEgo::Cache::WriteCatalog(AlterEgo::GW2API::GetDataDirectory(),
        AlterEgo::Cache::AchievementDataset::Groups, j);
}

static void LoadAchGroupCache() {
    try {
        auto snapshot = AlterEgo::Cache::ReadCatalog(AlterEgo::GW2API::GetDataDirectory(), AlterEgo::Cache::AchievementDataset::Groups);
        if (!snapshot) return;
        auto& j = *snapshot;

        // Restore the last-known categories immediately, even across the daily
        // reset. A stale snapshot is usable offline and is refreshed in the
        // background without blanking the navigation on every launch.
        const int64_t fetchEpoch = j.value("fetch_time", (int64_t)0);
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        ++g_AchGroupsGeneration;

        if (j.contains("groups") && j["groups"].is_array()) {
            g_AchGroups.clear();
            for (const auto& gj : j["groups"]) {
                AchGroupDef g;
                g.id = gj.value("id", "");
                g.name = gj.value("name", "");
                g.description = gj.value("description", "");
                g.order = gj.value("order", 0);
                if (gj.contains("categories") && gj["categories"].is_array()) {
                    for (const auto& cid : gj["categories"]) g.categories.push_back(cid.get<uint32_t>());
                }
                g_AchGroups.push_back(std::move(g));
            }
        }

        if (j.contains("categories") && j["categories"].is_object()) {
            g_AchCategories.clear();
            for (auto& [key, cj] : j["categories"].items()) {
                AchCategoryDef c;
                c.id = cj.value("id", 0u);
                c.name = cj.value("name", "");
                c.description = cj.value("description", "");
                c.order = cj.value("order", 0);
                c.icon_url = cj.value("icon_url", "");
                if (cj.contains("achievements") && cj["achievements"].is_array()) {
                    for (const auto& aid : cj["achievements"]) c.achievements.push_back(aid.get<uint32_t>());
                }
                g_AchCategories[c.id] = std::move(c);
            }
        }

        // Build reverse lookup
        g_AchIdToCategory.clear();
        for (const auto& [catId, cat] : g_AchCategories) {
            for (uint32_t achId : cat.achievements) {
                g_AchIdToCategory[achId] = catId;
            }
        }

        // Relocate curated bonus-event categories the API misfiles
        ApplyBonusEventOverrides();

        // Build hidden category set
        g_AchHiddenCatIds.clear();
        for (const auto& g : g_AchGroups) {
            if (g.name == "Character Adventure Guide") {
                for (uint32_t cid : g.categories) g_AchHiddenCatIds.insert(cid);
            }
        }

        if (!g_AchGroups.empty() && !g_AchCategories.empty()) {
            g_AchGroupsFetched = true;
            g_AchGroupsCacheFetchedAt = fetchEpoch;
        }
    } catch (...) {}
}

static void LoadAchDefCache() {
    try {
        auto snapshot = AlterEgo::Cache::ReadCatalog(AlterEgo::GW2API::GetDataDirectory(), AlterEgo::Cache::AchievementDataset::Definitions);
        if (!snapshot) return;
        auto& j = *snapshot;
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        ++g_AchCatalogGeneration;
        for (auto& [key, dj] : j.items()) {
            AchDef d;
            d.id = dj.value("id", 0u);
            d.name = dj.value("name", "");
            d.description = dj.value("description", "");
            d.requirement = dj.value("requirement", "");
            d.icon_url = dj.value("icon_url", "");
            d.type = dj.value("type", "");
            if (dj.contains("flags") && dj["flags"].is_array()) {
                for (const auto& f : dj["flags"]) d.flags.push_back(f.get<std::string>());
            }
            d.max_count = dj.value("max_count", 0);
            d.total_ap = dj.value("total_ap", 0);
            if (dj.contains("prerequisites") && dj["prerequisites"].is_array()) {
                for (const auto& p : dj["prerequisites"]) d.prerequisites.push_back(p.get<uint32_t>());
            }
            if (dj.contains("bits") && dj["bits"].is_array()) {
                for (const auto& bj : dj["bits"]) {
                    AchBitDef b;
                    b.type = bj.value("type", "");
                    b.item_id = bj.value("item_id", 0u);
                    b.text = bj.value("text", "");
                    d.bits.push_back(std::move(b));
                }
            }
            if (d.id > 0) g_AchDefs[d.id] = std::make_shared<const AchDef>(std::move(d));
        }
    } catch (...) {}
}

static void LoadAchNameIndex() {
    // Reuse existing cached definitions without any network traffic at startup.
    try {
        auto snapshot = AlterEgo::Cache::ReadCatalog(AlterEgo::GW2API::GetDataDirectory(), AlterEgo::Cache::AchievementDataset::Names);
        if (snapshot) {
            std::unordered_map<uint32_t, std::string> loaded;
            for (const auto& [key, value] : snapshot->items()) {
                const auto id = AlterEgo::Cache::CatalogId(key);
                const auto name = value.get<std::string>();
                if (id && !name.empty()) loaded.emplace(id, name);
            }
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            g_AchNameIndex = std::move(loaded);
            ++g_AchCatalogGeneration;
        }
        g_AchNameIndexVerifiedAt = AlterEgo::Cache::ReadNameIndexVerification(AlterEgo::GW2API::GetDataDirectory());
    } catch (...) {}
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        for (const auto& [id, def] : g_AchDefs) {
            if (id && !def->name.empty()) g_AchNameIndex.emplace(id, def->name);
        }
        g_AchNameIndexReady = !g_AchNameIndex.empty();
        ++g_AchCatalogGeneration;
    }
}

// Only the canonical definition owner publishes committed observations. Every accepted definition also
// contributes its name to the shared catalog, including pinned/category fetches.
static void PublishAchDefinition(const std::shared_ptr<const AchDef>& def) {
    if(!def||!def->id)return;
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    if(!def->name.empty()){
        g_AchNameIndex[def->id]=def->name;
        g_AchNameIndexReady=true;
    }
    g_AchDefs[def->id]=def;
    ++g_AchCatalogGeneration;
}

// =========================================================================
// Achievement Tracker — API Fetch
// =========================================================================

static void FetchAchGroups() {
    if (g_AchGroupsFetching.exchange(true)) return;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        g_AchStatusMsg = "Fetching achievement groups...";
    }
    AlterEgo::Background::Workers().Launch([]() {
        namespace Names = AlterEgo::AchievementNames;
        const int64_t started = std::time(nullptr);
        try {
            const auto get=[](const std::string& path) {
                AlterEgo::Background::Checkpoint();
                auto body=Skinventory::HttpClient::Get("https://api.guildwars2.com/v2/achievements/"+path);
                AlterEgo::Background::Checkpoint();
                return AlterEgo::Untrusted::Parse(body,AlterEgo::Untrusted::PublicApi);
            };
            const auto groupIds=get("groups");
            const auto groupRows=get("groups?ids=all");
            Names::ValidateCatalogIdentities<std::string>(groupIds,groupRows);
            {
                std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                g_AchStatusMsg = "Fetching achievement categories...";
            }
            const auto categoryIds=get("categories");
            const auto categoryRows=get("categories?ids=all");
            Names::ValidateCatalogIdentities<uint32_t>(categoryIds,categoryRows);
            Names::ValidateGroupCatalog(groupRows,categoryRows);
            std::vector<AchGroupDef> groups;
            for(const auto& row:groupRows) {
                AchGroupDef g;
                g.id=row.at("id").get<std::string>();
                g.name=row.at("name").get<std::string>();
                g.description=row.value("description","");
                g.order=row.value("order",0);
                for(const auto& id:row.at("categories")) g.categories.push_back(Names::Id(id));
                groups.push_back(std::move(g));
            }
            std::sort(groups.begin(),groups.end(),
                [](const AchGroupDef& a,const AchGroupDef& b){return a.order<b.order;});
            std::unordered_map<uint32_t,AchCategoryDef> cats;
            std::unordered_map<uint32_t,uint32_t> idToCat;
            for(const auto& row:categoryRows) {
                AchCategoryDef c;
                c.id=Names::Id(row.at("id"));
                c.name=row.at("name").get<std::string>();
                c.description=row.value("description","");
                c.order=row.value("order",0);
                c.icon_url=row.value("icon","");
                for(const auto& id:row.at("achievements")){
                    const auto achievement=Names::Id(id);
                    c.achievements.push_back(achievement);
                    idToCat[achievement]=c.id;
                }
                cats.emplace(c.id,std::move(c));
            }
            AlterEgo::Background::Checkpoint();
            if(!SaveAchGroupCache(groups,cats,started))
                throw std::runtime_error("Could not save achievement group/category cache");
            AlterEgo::Background::Checkpoint();
            {
                std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                g_AchGroups=std::move(groups);
                g_AchCategories=std::move(cats);
                g_AchIdToCategory=std::move(idToCat);
                ApplyBonusEventOverrides();
                g_AchHiddenCatIds.clear();
                for(const auto& g:g_AchGroups)
                    if(g.name=="Character Adventure Guide")
                        for(uint32_t cid:g.categories) g_AchHiddenCatIds.insert(cid);
                g_AchGroupsCacheFetchedAt=started;
                g_AchGroupsFetched=true;
                ++g_AchGroupsGeneration;
                g_AchGroupsFetching=false;
                g_AchStatusMsg.clear();
            }
            FetchActiveSpecialEvent();
        } catch(const AlterEgo::Background::Cancelled&) {
            throw;
        } catch(...) {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            g_AchStatusMsg="Achievement group/category refresh incomplete; cached catalog retained";
            g_AchGroupsFetching=false;
        }
    }, [] { g_AchGroupsFetching=false; });
}

static void FetchActiveSpecialEvent() {
    namespace W=AlterEgo::WikiFreshness;
    const auto now=static_cast<int64_t>(std::time(nullptr));
    if(now<g_AchActiveEventNextCheck.load()||g_AchActiveEventFetching.exchange(true))return;
    g_AchActiveEventNextCheck=now+W::SpecialRetry;
    AlterEgo::Background::Workers().Launch([]() {
        const auto now=static_cast<int64_t>(std::time(nullptr));
        W::Stamp stamp;
        std::string wikitext;
        const auto cachePath=AlterEgo::Storage::ResolveCache(AlterEgo::GW2API::GetDataDirectory(),"wiki_special_event.json");
        try {
            std::ifstream cf(cachePath);
            if(cf.is_open()){
                nlohmann::json wrap;cf>>wrap;
                const auto body=wrap.value("body",std::string{});
                stamp.attempt=wrap.value("attempt",int64_t{0});
                if(W::ValidSpecialBody(body)){
                    wikitext=body;stamp.success=wrap.value("ts",int64_t{0});
                }
                if(stamp.success<0||stamp.success>now)stamp.success=0;
                if(stamp.attempt<0||stamp.attempt>now)stamp.attempt=0;
            }
        }catch(...){stamp={};wikitext.clear();}
        auto persist=[&]{
            nlohmann::json wrap={{"ts",stamp.success},{"attempt",stamp.attempt},{"body",wikitext}};
            return WriteFileAtomic(cachePath,wrap.dump());
        };
        if(W::Due(stamp,now,W::SpecialInterval,W::SpecialRetry)){
            stamp.attempt=now;
            persist();
            try{
                auto response=Skinventory::HttpClient::GetEx(
                    "https://wiki.guildwars2.com/api.php?action=parse&page=Special_event&prop=wikitext&format=json");
                if(response.status_code!=200)throw std::runtime_error("Wiki special event HTTP failure");
                auto j=AlterEgo::Untrusted::Parse(response.body,AlterEgo::Untrusted::Wiki);
                const auto& parsed=j.at("parse");
                auto body=parsed.at("wikitext").at("*").get<std::string>();
                if(parsed.at("title").get<std::string>()!="Special event"||!W::ValidSpecialBody(body))
                    throw std::runtime_error("Invalid Wiki special event page");
                AlterEgo::Background::Checkpoint();
                wikitext=std::move(body);stamp.success=static_cast<int64_t>(std::time(nullptr));
                persist();
            }catch(const AlterEgo::Background::Cancelled&){throw;}
            catch(...){ /* retain last known good, including original success age */ }
        }
        const bool fresh=stamp.success>0&&now-stamp.success<W::SpecialInterval;
        g_AchActiveEventNextCheck=fresh?stamp.success+W::SpecialInterval:
            std::max(now+int64_t{1},stamp.attempt+W::SpecialRetry);
        if(wikitext.empty()){g_AchActiveEventFetching=false;return;}

        // Today's date as YYYYMMDD (local time — event windows are day-granular).
        std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &tt);
#else
        lt = *std::localtime(&tt);
#endif
        int todayYmd = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;

        ActiveEventResult res = ParseActiveSpecialEvent(wikitext, todayYmd, fresh);

        {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            // Tidy the display name (drop a leading "Bonus Event: ").
            std::string display = res.name;
            const std::string pfx = "Bonus Event: ";
            if (display.rfind(pfx, 0) == 0) display = display.substr(pfx.size());

            uint32_t catId = res.catId;
            // Fallback: resolve the category by name against the live category set.
            // Handles events whose category only appears in the API while active
            // (e.g. "Return to End of Dragons"), and any event the keyword map misses.
            if (catId == 0 && !display.empty()) {
                std::string want = AeToLower(display);
                for (const auto& kv : g_AchCategories) {
                    if (AeToLower(kv.second.name) == want) { catId = kv.first; break; }
                }
            }

            if (display.empty()) {
                g_AchActiveMainCatIds.clear();
                g_AchActiveEventName.clear();
            } else {
                g_AchActiveEventName = display;
                if (catId != 0 && g_AchCategories.count(catId))
                    g_AchActiveMainCatIds = { catId };
                else
                    g_AchActiveMainCatIds.clear(); // name-only banner (no tracked category)
            }
            g_AchActiveDailyCatIds.clear(); // wiki path drives main category only
            g_AchActiveEventFetching = false;
        }
    }, [] { g_AchActiveEventFetching = false; });
}

static int64_t AchievementRefreshInterval(){
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    return !g_AchPinned.empty()?AlterEgo::RefreshEngine::ObjectiveSeconds():AlterEgo::RefreshEngine::BrowseSeconds;
}
static bool IsAchCategoryRefreshDue(uint32_t catId) {
    (void)catId;
    return AlterEgo::NativeProgress::AchievementDue(GetEffectiveAccountName(),
        g_AchProgressCacheSavedAt,AchievementRefreshInterval());
}

static std::vector<uint32_t> AchCategoryIds(uint32_t catId){
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    const auto it=g_AchCategories.find(catId);
    return it==g_AchCategories.end()?std::vector<uint32_t>{}:it->second.achievements;
}
static void RequestCategoryDefinitions(uint32_t catId,bool manual){
    namespace Definitions=AlterEgo::AchievementDefinitions;
    const auto ids=AchCategoryIds(catId);
    Definitions::Request(ids,manual,manual||AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::PublicCatalog),
        manual?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Visible);
    g_AchCatFetching=Definitions::Pending(ids);
}
static void FetchAchCategoryDefs(uint32_t catId,bool automatic){
    const auto account=GetEffectiveAccountName();
    const auto credential=AlterEgo::NativeAccounts::FindLinked(account);
    uint64_t catalogRevision;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        if(!g_AchCategories.count(catId))return;
        catalogRevision=g_AchGroupsGeneration;
    }
    g_AchCategoryDemand=AlterEgo::AchievementNames::CategoryDemand{account,credential.generation,++g_AchDemandRevision,catalogRevision,catId,automatic};
    RequestCategoryDefinitions(catId,!automatic);
}

static void SendAchProgressQuery(const std::vector<uint32_t>& ids,bool automatic,uint32_t categoryId,const AlterEgo::AchievementNames::CategoryDemand* demand){
    if(!APIDefs||(automatic&&!AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Achievements)))return;
    const std::string account=GetEffectiveAccountName();
    if(account.empty())return;
    const auto credential=AlterEgo::NativeAccounts::FindLinked(account);
    uint64_t generation;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        if(account!=g_AchCachedAccount||g_AchProgressFetching)return;
        if(demand&&!demand->Matches(account,credential.generation,g_AchDemandRevision,g_AchGroupsGeneration,g_AchSelectedCatId))return;
        if(automatic&&!AlterEgo::NativeProgress::AchievementDue(account,g_AchProgressCacheSavedAt,AchievementRefreshInterval()))return;
        generation=++g_AchRequestGeneration;g_AchProgressFetching=true;g_AchProgressQueryStartMs=NowMs();
        if(categoryId)g_AchCategoryRefreshAt[categoryId]=std::chrono::steady_clock::now();
    }
    const auto interval=AchievementRefreshInterval();
    AlterEgo::Background::Workers().Launch([account,credential,generation,automatic,ids,interval]{
        auto result=AlterEgo::NativeProgress::FetchAchievements(AlterEgo::GW2API::GetDataDirectory(),credential,!automatic,interval);
        const bool accepted=AlterEgo::NativeAccounts::CommitIfCurrent(credential,[&]{
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        if(generation!=g_AchRequestGeneration||g_AchCachedAccount!=account)return true;
        g_AchProgressFetching=false;g_AchProgressQueryStartMs=0;
        if(AlterEgo::Background::Workers().Stopping())return true;
        if(!result.snapshot){
            g_AchRefreshError=result.error;
            // Shared broker records failed attempts; the next automatic check respects its cooldown.
            return true;
        }
        std::unordered_map<uint32_t,AchProgress> next;
        for(const auto& [id,source]:result.snapshot->entries){
            auto p=source;
            if(p.done&&p.max>0&&p.current<p.max){
                auto def=g_AchDefs.find(id);
                if(def!=g_AchDefs.end()){
                    const auto& flags=def->second->flags;
                    if(std::find(flags.begin(),flags.end(),"Repeatable")!=flags.end()&&
                       std::find(flags.begin(),flags.end(),"Daily")==flags.end()&&
                       std::find(flags.begin(),flags.end(),"Weekly")==flags.end())p.done=false;
                }
            }
            next.emplace(id,std::move(p));
        }
        for(auto it=g_AchOptimisticTime.begin();it!=g_AchOptimisticTime.end();){
            auto old=g_AchProgress.find(it->first);auto incoming=next.find(it->first);
            if(old!=g_AchProgress.end()&&std::chrono::steady_clock::now()-it->second<std::chrono::minutes(10)&&
               (incoming==next.end()||incoming->second.current<old->second.current)){
                auto p=old->second;if(incoming!=next.end())p.completed_bits.insert(incoming->second.completed_bits.begin(),incoming->second.completed_bits.end());
                next[it->first]=std::move(p);++it;
            }else it=g_AchOptimisticTime.erase(it);
        }
        const auto zero=[&](uint32_t id){if(!next.count(id)){AchProgress p;p.id=id;next.emplace(id,p);}};
        for(auto id:ids)zero(id);
        for(const auto& [catId,cat]:g_AchCategories){(void)catId;for(auto id:cat.achievements)zero(id);}
        for(auto id:g_AchPinned)zero(id);
        g_AchProgress=std::move(next);g_AchNeedsInitialFullLoad=false;g_AchManualInitialScan=false;
        g_AchProgressCacheSavedAt=result.snapshot->fetchedAt;g_AchProgressLoadedSnapshotAt=result.snapshot->fetchedAt;
        g_LastAchProgressQuery=std::chrono::steady_clock::now();
        g_AchRefreshSuccessUntil=g_LastAchProgressQuery+std::chrono::seconds(4);
        g_AchRefreshError.clear();++g_AchProgressGen;g_AchProgressDirty=true;
        return true;
        });
        if(!accepted){std::lock_guard<std::recursive_mutex> lock(g_AchMutex);if(generation==g_AchRequestGeneration&&g_AchCachedAccount==account){g_AchProgressFetching=false;g_AchProgressQueryStartMs=0;g_AchRefreshError="Account changed; refresh again.";}}
    },[generation]{std::lock_guard<std::recursive_mutex> lock(g_AchMutex);if(generation==g_AchRequestGeneration){g_AchProgressFetching=false;g_AchProgressQueryStartMs=0;g_AchRefreshError="Achievement refresh cancelled";}});
}

static void FinishAchNameIndex(const std::string& error,bool cancelled=false) {
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    g_AchNameIndexStatus.clear();
    g_AchNameIndexError = error;
    g_AchNameIndexRetryAt = (cancelled||error.empty()) ? 0 : std::time(nullptr) + AlterEgo::AchievementNames::RetryInterval;
    g_AchNameIndexFetching = false;
}

static void FetchAchNameIndex(bool full = false) {
    if (g_AchNameIndexFetching.exchange(true)) return;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        g_AchNameIndexError.clear();
        g_AchNameIndexStatus = "Checking achievement IDs...";
    }
    const auto baseline=full?AlterEgo::AchievementDefinitions::SnapshotStamps():AlterEgo::AchievementDefinitions::Plan{};
    AlterEgo::Background::Workers().Launch([full,baseline]() {
        namespace Names=AlterEgo::AchievementNames;
        namespace Definitions=AlterEgo::AchievementDefinitions;
        AlterEgo::ApiActivity::ReasonScope reason(full?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Visible);
        bool complete=false;
        std::string error;
        try {
            const auto ids=Names::ParseIds(AlterEgo::Untrusted::Parse(Skinventory::HttpClient::Get(
                "https://api.guildwars2.com/v2/achievements"),AlterEgo::Untrusted::PublicApi));
            AlterEgo::Background::Checkpoint();
            std::vector<uint32_t> pending;
            {
                std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                pending=Names::Plan(ids,g_AchNameIndex,full);
                g_AchNameIndexStatus="Retrieving achievement definitions (0/"+std::to_string(pending.size())+")...";
            }
            Definitions::Plan fullPlan;
            if(full)for(auto id:pending){auto it=baseline.find(id);fullPlan.emplace(id,it==baseline.end()?0:it->second);}
            complete=true;
            for(size_t start=0;start<pending.size();start+=Names::BatchSize){
                AlterEgo::Background::Checkpoint();
                const auto end=std::min(start+Names::BatchSize,pending.size());
                std::vector<uint32_t> batch(pending.begin()+start,pending.begin()+end);
                if(!full){
                    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                    batch=Names::Plan(batch,g_AchNameIndex,false);
                }
                if(!batch.empty()&&(full?Definitions::AcquirePlanned(batch,fullPlan,true):Definitions::Acquire(batch,false,false)).size()!=batch.size())complete=false;
                {
                    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                    g_AchNameIndexStatus="Retrieving achievement definitions ("+std::to_string(end)+"/"+std::to_string(pending.size())+")...";
                }
                if(end<pending.size())AlterEgo::Background::Pause(std::chrono::milliseconds(200));
            }
        }catch(const AlterEgo::Background::Cancelled&){throw;}
        catch(...){complete=false;}
        AlterEgo::Background::Checkpoint();
        if(complete){
            const int64_t verifiedAt=std::time(nullptr);
            if(AlterEgo::Cache::VerifyAchievementIdIndex(AlterEgo::GW2API::GetDataDirectory(),verifiedAt))
                g_AchNameIndexVerifiedAt=std::max(g_AchNameIndexVerifiedAt.load(),verifiedAt);
            else error="Could not save achievement catalog verification; retry from the Achievements menu.";
        }else error="Achievement definition check incomplete; cached definitions retained. Retry from the Achievements menu.";
        FinishAchNameIndex(error);
    }, [] { FinishAchNameIndex("Achievement definition check cancelled; cached definitions retained.",true); });
}
static void FetchPinnedAchDefs() {
    std::vector<uint32_t> ids;
    {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);ids=g_AchPinned;}
    AlterEgo::AchievementDefinitions::Request(ids,false,
        AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::PublicCatalog),AlterEgo::ApiActivity::Reason::Objective);
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: hSelf = hModule; break;
    case DLL_PROCESS_DETACH: break;
    case DLL_THREAD_ATTACH: break;
    case DLL_THREAD_DETACH: break;
    }
    return TRUE;
}

// Forward declarations
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
static UINT NavigationWndProc(HWND,UINT,WPARAM,LPARAM);
void AddonOptions();
void OnMumbleIdentityUpdated(void* eventArgs);

// Icon size for item icons
static const float ICON_SIZE = 40.0f;
static const float SMALL_ICON_SIZE = 24.0f;
static const float TRAIT_ICON_SIZE = 32.0f;
static const float MINOR_TRAIT_SIZE = 26.0f;
static const float SPEC_PORTRAIT_SIZE = 100.0f;
static const float SPEC_NAME_BADGE_WIDTH = 190.0f;

// Equipment slot display order (paper doll layout)
static const char* EQUIPMENT_SLOTS_LEFT[] = {
    "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots"
};
static const char* EQUIPMENT_SLOTS_RIGHT[] = {
    "Backpack", "Accessory1", "Accessory2", "Amulet", "Ring1", "Ring2"
};
static const char* EQUIPMENT_SLOTS_WEAPONS[] = {
    "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
    "WeaponAquaticA", "WeaponAquaticB"
};

static const char* SlotDisplayName(const std::string& slot) {
    if (slot == "Helm") return "Helm";
    if (slot == "Shoulders") return "Shoulders";
    if (slot == "Coat") return "Chest";
    if (slot == "Gloves") return "Gloves";
    if (slot == "Leggings") return "Leggings";
    if (slot == "Boots") return "Boots";
    if (slot == "Backpack") return "Back";
    if (slot == "Accessory1") return "Accessory 1";
    if (slot == "Accessory2") return "Accessory 2";
    if (slot == "Amulet") return "Amulet";
    if (slot == "Ring1") return "Ring 1";
    if (slot == "Ring2") return "Ring 2";
    if (slot == "WeaponA1") return "Weapon A1";
    if (slot == "WeaponA2") return "Weapon A2";
    if (slot == "WeaponB1") return "Weapon B1";
    if (slot == "WeaponB2") return "Weapon B2";
    if (slot == "WeaponAquaticA") return "Aquatic A";
    if (slot == "WeaponAquaticB") return "Aquatic B";
    if (slot == "HelmAquatic") return "Aqua Helm";
    if (slot == "Sickle") return "Sickle";
    if (slot == "Axe") return "Logging Axe";
    if (slot == "Pick") return "Mining Pick";
    if (slot == "FishingRod") return "Fishing Rod";
    if (slot == "FishingBait") return "Bait";
    if (slot == "FishingLure") return "Lure";
    if (slot == "PowerCore") return "Jade Bot Core";
    if (slot == "Relic") return "Relic";
    if (slot == "SensoryArray") return "Sensory Array";
    if (slot == "ServiceChip") return "Service Chip";
    return slot.c_str();
}

// Fetch item/skin detail data for selected character on-demand
static void FetchDetailsForCharacter(const AlterEgo::Character& ch) {
    std::vector<uint32_t> item_ids;
    std::vector<uint32_t> skin_ids;
    std::vector<uint32_t> spec_ids;
    std::vector<uint32_t> stat_ids;

    // Gear pieces that are cached but missing default_skin: re-fetch them once so
    // the Fashion wardrobe link can resolve default (non-transmuted) appearances.
    std::vector<uint32_t> backfill_ids;
    auto wardrobeSlot = [](const std::string& s) {
        return s == "Helm" || s == "Shoulders" || s == "Coat" || s == "Gloves" ||
               s == "Leggings" || s == "Boots" || s == "Backpack" ||
               s == "WeaponA1" || s == "WeaponA2" || s == "WeaponB1" || s == "WeaponB2";
    };

    for (const auto& eq : ch.equipment) {
        if (eq.id != 0) item_ids.push_back(eq.id);
        if (eq.skin != 0) skin_ids.push_back(eq.skin);
        if (eq.stat_id != 0) stat_ids.push_back(eq.stat_id);
        for (auto u : eq.upgrades) if (u != 0) item_ids.push_back(u);
        for (auto inf : eq.infusions) if (inf != 0) item_ids.push_back(inf);

        if (eq.id != 0 && eq.skin == 0 && wardrobeSlot(eq.slot)) {
            const auto ii = AlterEgo::GW2API::GetItemInfo(eq.id);
            if (ii && ii->default_skin == 0) backfill_ids.push_back(eq.id);
        }
    }

    std::vector<uint32_t> skill_ids;

    for (const auto& bt : ch.build_tabs) {
        for (int i = 0; i < 3; i++) {
            if (bt.specializations[i].spec_id != 0)
                spec_ids.push_back(bt.specializations[i].spec_id);
        }
        // Collect skill IDs
        auto addSkill = [&](uint32_t id) { if (id != 0) skill_ids.push_back(id); };
        addSkill(bt.terrestrial_skills.heal);
        for (int i = 0; i < 3; i++) addSkill(bt.terrestrial_skills.utilities[i]);
        addSkill(bt.terrestrial_skills.elite);
        addSkill(bt.aquatic_skills.heal);
        for (int i = 0; i < 3; i++) addSkill(bt.aquatic_skills.utilities[i]);
        addSkill(bt.aquatic_skills.elite);
    }

    if (!item_ids.empty()) AlterEgo::GW2API::FetchItemDetailsAsync(item_ids);
    if (!backfill_ids.empty()) AlterEgo::GW2API::FetchItemDetailsAsync(backfill_ids, true);
    if (!skin_ids.empty()) AlterEgo::GW2API::FetchSkinDetailsAsync(skin_ids);
    if (!spec_ids.empty()) AlterEgo::GW2API::FetchSpecDetailsAsync(spec_ids);
    if (!skill_ids.empty()) AlterEgo::GW2API::FetchSkillDetailsAsync(skill_ids);
    if (!stat_ids.empty()) AlterEgo::GW2API::FetchItemStatDetailsAsync(stat_ids);

    // Query skin unlock status from native API cache (instant, no API call)
    if (!skin_ids.empty()) AlterEgo::GW2API::QuerySkinUnlocks(skin_ids);

    // Fetch profession icon for equipment panel silhouette
    if (!ch.profession.empty()) AlterEgo::GW2API::FetchProfessionInfoAsync(ch.profession);
}

// PNG/JPG/JPEG assets are ordinary user-managed files, not cache records.
static Texture_t* GetCharacterPortraitTexture(const AlterEgo::Character& ch) {
    const auto stem=AlterEgo::Storage::PortraitStem(ch.name);
    if(stem.empty())return nullptr;
    auto cacheIt = s_portraitPathCache.find(stem);
    if (cacheIt == s_portraitPathCache.end()) {
        if (s_portraitMissing.find(stem) != s_portraitMissing.end()) return nullptr;
        std::string portraitDir = AlterEgo::GW2API::GetDataDirectory() + "/portraits";
        std::filesystem::create_directories(portraitDir);
        static const char* exts[] = { ".png", ".jpg", ".jpeg" };
        bool found = false;
        for (const char* ext : exts) {
            std::string path = portraitDir + "/" + stem + ext;
            std::error_code ec;
            if (std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) {
                int64_t modTime = GetFileModTime(path);
                s_portraitPathCache[stem] = { path, modTime };
                found = true;
                break;
            }
        }
        if (!found) {
            s_portraitMissing.insert(stem);
            return nullptr;
        }
        cacheIt = s_portraitPathCache.find(stem);
    }
    std::string texId = "AE_PORTRAIT_" + stem + "_" + std::to_string(cacheIt->second.modTime)+"_"+cacheIt->second.path.substr(cacheIt->second.path.find_last_of('.')+1);
    Texture_t* tex = APIDefs->Textures_Get(texId.c_str());
    if (!tex || !tex->Resource) {
        try {
            auto fspath = std::filesystem::u8path(cacheIt->second.path);
            std::ifstream ifs(fspath, std::ios::binary | std::ios::ate);
            if (ifs.is_open()) {
                auto sz = ifs.tellg();
                if (sz > 0 && sz <= 32*1024*1024) {
                    std::vector<uint8_t> buf((size_t)sz);
                    ifs.seekg(0);
                    ifs.read(reinterpret_cast<char*>(buf.data()), sz);
                    tex = APIDefs->Textures_GetOrCreateFromMemory(
                        texId.c_str(), buf.data(), (uint64_t)buf.size());
                }
            }
        } catch (...) {}
    }
    return (tex && tex->Resource) ? tex : nullptr;
}

// Render a right-aligned profession emblem (the wiki "tango" icon fetched via the GW2 API).
// Draws a faint dark backing + gold-ish frame; shows a tooltip with the profession name.
static void RenderProfessionEmblem(const std::string& profession, float size) {
    if (profession.empty()) { ImGui::Dummy(ImVec2(size, size)); return; }

    // Trigger fetch if we don't already have it
    AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
    const auto prof = AlterEgo::GW2API::GetProfessionInfo(profession);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 maxp(pos.x + size, pos.y + size);

    // Backing card (subtle, so it reads as part of the header)
    dl->AddRectFilled(pos, maxp, IM_COL32(20, 20, 26, 180), 4.0f);
    dl->AddRect(pos, maxp, IM_COL32(140, 113, 55, 160), 4.0f, 0, 1.0f);

    bool drew = false;
    if (GetProfessionBigIconId(profession)) {
        uint32_t profIconId = GetProfessionBigIconId(profession);
        Texture_t* tex = AlterEgo::IconManager::GetIcon(profIconId);
        if (tex && tex->Resource) {
            float ir = size - 8.0f;
            float ix = pos.x + (size - ir) * 0.5f;
            float iy = pos.y + (size - ir) * 0.5f;
            dl->AddImage(tex->Resource,
                ImVec2(ix, iy), ImVec2(ix + ir, iy + ir),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(240, 226, 190, 230));
            drew = true;
        } else {
            AlterEgo::IconManager::RequestIcon(profIconId, prof ? prof->icon_big_url : "");
        }
    }
    if (!drew) {
        // Placeholder: profession initial in dim gold
        std::string init = profession.substr(0, 1);
        ImVec2 ts = ImGui::CalcTextSize(init.c_str());
        dl->AddText(ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f),
            IM_COL32(180, 160, 110, 200), init.c_str());
    }

    ImGui::Dummy(ImVec2(size, size));
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(GetProfessionColor(profession), "%s", profession.c_str());
        ImGui::EndTooltip();
    }
}

// Render a circular character avatar (size px). Uses per-character portrait if available,
// falls back to the profession icon. Draws a gold ring around it.
static void RenderCharacterAvatar(const AlterEgo::Character& ch, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
    float r = size * 0.5f;

    // Background
    dl->AddCircleFilled(center, r, IM_COL32(20, 20, 26, 255), 32);

    Texture_t* tex = GetCharacterPortraitTexture(ch);
    bool drewImage = false;
    if (tex && tex->Resource) {
        // Use AddImageRounded with rounding == radius for circular crop
        dl->AddImageRounded(tex->Resource,
            ImVec2(center.x - r, center.y - r),
            ImVec2(center.x + r, center.y + r),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32_WHITE, r, ImDrawCornerFlags_All);
        drewImage = true;
    } else {
        // Fallback: profession icon
        const auto prof = AlterEgo::GW2API::GetProfessionInfo(ch.profession);
        if (GetProfessionIconId(ch.profession)) {
            uint32_t profIconId = GetProfessionIconId(ch.profession);
            Texture_t* ptex = AlterEgo::IconManager::GetIcon(profIconId);
            if (ptex && ptex->Resource) {
                // Tinted, inset slightly so the gold ring shows
                float ir = r * 0.78f;
                dl->AddImageRounded(ptex->Resource,
                    ImVec2(center.x - ir, center.y - ir),
                    ImVec2(center.x + ir, center.y + ir),
                    ImVec2(0, 0), ImVec2(1, 1),
                    IM_COL32(230, 220, 180, 255), ir, ImDrawCornerFlags_All);
                drewImage = true;
            } else {
                AlterEgo::IconManager::RequestIcon(profIconId, prof ? prof->icon_url : "");
            }
        }
    }

    if (!drewImage) {
        // Initial placeholder — first letter
        std::string initial = ch.name.empty() ? "?" : ch.name.substr(0, 1);
        ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
            IM_COL32(180, 160, 110, 220), initial.c_str());
    }

    // Gold ring
    dl->AddCircle(center, r, IM_COL32(197, 161, 85, 220), 32, 1.5f);
    dl->AddCircle(center, r - 1.5f, IM_COL32(0, 0, 0, 90), 32, 1.0f);

    ImGui::Dummy(ImVec2(size, size));
}

// Build a one-line subtitle for an equipped item: "Stat • Sigil/Rune" (truncated where needed).
// Returns empty string if neither piece of info is available.
static std::string BuildEquipSubtitle(const AlterEgo::EquipmentItem* eq) {
    if (!eq || eq->id == 0) return "";
    std::string out;

    // Stat name (e.g. "Berserker", from "Berserker's")
    if (eq->stat_id != 0) {
        const auto si = AlterEgo::GW2API::GetItemStatInfo(eq->stat_id);
        if (si && !si->name.empty()) {
            out = si->name;
            if (!out.empty() && out.back() == 's' && out.size() > 2 &&
                out[out.size() - 2] == '\'') {
                out.erase(out.size() - 2);
            }
        }
    } else {
        const auto item_info = AlterEgo::GW2API::GetItemInfo(eq->id);
        if (item_info && item_info->details.is_object() &&
            item_info->details.contains("infix_upgrade") &&
            item_info->details["infix_upgrade"].contains("id")) {
            uint32_t fixedStatId = item_info->details["infix_upgrade"]["id"].get<uint32_t>();
            const auto si = AlterEgo::GW2API::GetItemStatInfo(fixedStatId);
            if (si && !si->name.empty()) {
                out = si->name;
                if (!out.empty() && out.back() == 's' && out.size() > 2 &&
                    out[out.size() - 2] == '\'') {
                    out.erase(out.size() - 2);
                }
            }
        }
    }

    // First upgrade (rune for armor, sigil for weapon) — short form
    if (!eq->upgrades.empty() && eq->upgrades[0] != 0) {
        const auto uinfo = AlterEgo::GW2API::GetItemInfo(eq->upgrades[0]);
        if (uinfo && !uinfo->name.empty()) {
            std::string upgradeShort = uinfo->name;
            // Strip "Superior Rune of " / "Superior Sigil of " / "the "
            static const char* prefixes[] = {
                "Superior Rune of the ", "Superior Rune of ",
                "Superior Sigil of the ", "Superior Sigil of ",
                "Major Rune of the ", "Major Rune of ",
                "Major Sigil of the ", "Major Sigil of "
            };
            for (const char* p : prefixes) {
                size_t plen = strlen(p);
                if (upgradeShort.rfind(p, 0) == 0) {
                    upgradeShort = upgradeShort.substr(plen);
                    break;
                }
            }
            if (!out.empty()) out += " - "; // separator (ASCII; TTF atlas has no bullet glyph)
            else out = "";
            out += upgradeShort;
        }
    }
    return out;
}

// Render a single equipment slot with icon and tooltip
// Exact cached names only: never copy an ID or trigger metadata retrieval
// merely because the user opens a menu. Reused by all equipment editors.
static void CopyCachedItemNamesMenu(const std::vector<uint32_t>& ids,const char* label){
    std::vector<std::string> names;
    for(auto id:ids){
        const auto info=id?AlterEgo::GW2API::GetItemInfo(id):nullptr;
        if(info&&!info->name.empty()&&std::find(names.begin(),names.end(),info->name)==names.end())
            names.push_back(info->name);
    }
    if(!names.empty()&&ImGui::BeginMenu(label)){
        for(const auto& name:names)CopyNameMenuItem(name,name.c_str());
        ImGui::EndMenu();
    }
}
static void CopyCachedItemLinksMenu(const std::vector<uint32_t>& ids,const char* label){
    std::set<uint32_t> unique;
    for(auto id:ids)if(ChatLinkIdValid(id))unique.insert(id);
    if(!unique.empty()&&AlterEgo::UI::BeginActionMenu(label,AlterEgo::UI::MenuIcon::Copy)){
        for(auto id:unique){
            const auto info=AlterEgo::GW2API::GetItemInfo(id);
            const auto name=info&&!info->name.empty()?info->name:"#"+std::to_string(id);
            ImGui::PushID(static_cast<int>(id));
            CopyEntityChatLink(AlterEgo::LINK_ITEM,id,name.c_str());
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
}
static void RenderEquipmentContextActions(const AlterEgo::Character& ch,const AlterEgo::EquipmentItem* eq){
    if(!eq||!eq->id)return;
    const auto item_info=AlterEgo::GW2API::GetItemInfo(eq->id);
    const auto skin_info=eq->skin?AlterEgo::GW2API::GetSkinInfo(eq->skin):nullptr;
    if(item_info)CopyNameMenuItem(item_info->name);
    CopyEntityChatLink(AlterEgo::LINK_ITEM,eq->id,"Copy Item Chat Link",eq->skin,eq->upgrades.size()>0?eq->upgrades[0]:0,eq->upgrades.size()>1?eq->upgrades[1]:0);
    if(item_info&&!item_info->chat_link.empty())CopyNativeLinkMenuItem(item_info->chat_link,"Copy API Chat Link");
    if(skin_info)CopyNameMenuItem(skin_info->name,"Copy Skin Name");
    CopyEntityChatLink(AlterEgo::LINK_SKIN,eq->skin,"Copy Skin Chat Link");
    CopyCachedItemNamesMenu(eq->upgrades,"Copy Upgrade Name");
    CopyCachedItemLinksMenu(eq->upgrades,"Copy Upgrade Chat Link");
    CopyCachedItemNamesMenu(eq->infusions,"Copy Infusion Name");
    CopyCachedItemLinksMenu(eq->infusions,"Copy Infusion Chat Link");
    ImGui::Separator();
    if(ImGui::MenuItem("Find in Inventory"))OpenInventoryItem(eq->id);
    const bool verifiedOwner=!ch.account_name.empty()&&AlterEgo::NativeAccounts::VerifiedAccountForCharacter(ch.name)==ch.account_name;
    if(ImGui::BeginMenu("Item Locations",verifiedOwner)){
        AlterEgo::NativeAccounts::RenderItemAvailability(eq->id,ch.account_name,ch.name);
        ImGui::EndMenu();
    }
}
// Shared cached entity detail sections. Context-specific equipment evidence stays with its owner.
static void RenderSpecializationTooltip(const std::string& name,ImVec4 color,const std::string& fallback,bool editable){
    using namespace AlterEgo::UI;
    ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
    TooltipLine(name.c_str(),color);
    if(const auto* d=AlterEgo::GetSpecDescription(name)){
        ImGui::Spacing();TooltipLine((std::string("\"")+d->flavor+"\"").c_str(),ImVec4(.7f,.85f,.9f,1.f),350.f);
        ImGui::Spacing();TooltipLine(d->description,TooltipBody,350.f);
    }else TooltipLine(fallback.c_str(),ImVec4(.6f,.6f,.6f,1.f));
    if(editable)TooltipLine("(click to change specialization)",ImVec4(.7f,.62f,.35f,1.f));
    ImGui::EndTooltip();
}
void RenderItemDefinitionDetails(const AlterEgo::ItemInfo& info,unsigned parts,bool tooltip){
    using namespace AlterEgo;
    using namespace TooltipCore;
    auto lines=ItemDefinitionLines(info.type,info.rarity,info.description,info.details,parts);
    if(parts&ItemAcquisition){
        auto extra=ItemAcquisitionLines(info.optional_metadata);
        lines.insert(lines.end(),extra.begin(),extra.end());
    }
    for(const auto& line:lines){
        const auto text=PlainItemDescription(line.text);
        if(text.empty())continue;
        const auto color=line.part==ItemAttributes?ImVec4(.4f,.9f,.4f,1.f):
            line.part==ItemEffects?ImVec4(.4f,.7f,.9f,1.f):
            line.part==ItemDescription?ImVec4(.6f,.8f,.6f,1.f):UI::TooltipBody;
        if(tooltip)UI::TooltipLine(text.c_str(),color);
        else{
            ImGui::PushStyleColor(ImGuiCol_Text,color);
            ImGui::TextWrapped("%s",text.c_str());
            ImGui::PopStyleColor();
        }
    }
}
void RenderItemDefinitionTooltip(const AlterEgo::ItemInfo& info){
    if(info.name.empty())return;
    ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
    AlterEgo::UI::TooltipLine(info.name.c_str(),GetRarityColor(info.rarity));
    RenderItemDefinitionDetails(info);
    ImGui::EndTooltip();
}
static void RenderUpgradeTooltipDetails(const AlterEgo::ItemInfo& info){
    RenderItemDefinitionDetails(info,AlterEgo::TooltipCore::ItemEffects|AlterEgo::TooltipCore::ItemDescription);
}
static void RenderEquipmentSlot(const AlterEgo::Character& ch,const AlterEgo::EquipmentItem* eq, const char* slotName) {
    ImGui::PushID(slotName);

    if (!eq || eq->id == 0) {
        // Empty slot
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE),
            IM_COL32(40, 40, 40, 180));
        ImGui::GetWindowDrawList()->AddRect(
            pos, ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE),
            IM_COL32(80, 80, 80, 200));
        ImGui::Dummy(ImVec2(ICON_SIZE, ICON_SIZE));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s (Empty)", slotName);
            ImGui::EndTooltip();
        }
    } else {
        // Show skin icon when available, item icon as fallback
        const auto item_info = AlterEgo::GW2API::GetItemInfo(eq->id);
        const auto skin_info = eq->skin ? AlterEgo::GW2API::GetSkinInfo(eq->skin) : nullptr;

        // Use offset ID for skin icons to avoid cache collision with item icons
        uint32_t display_id;
        std::string icon_url;
        std::string display_name = slotName;
        std::string rarity = "Basic";

        if (skin_info) {
            display_id = eq->skin + 5000000;
            icon_url = skin_info->icon_url;
            display_name = skin_info->name;
            rarity = skin_info->rarity;
        } else {
            display_id = eq->id;
            if (item_info) {
                icon_url = item_info->icon_url;
                display_name = item_info->name;
                rarity = item_info->rarity;
            }
        }

        // Use item rarity for border color (actual item, not skin)
        if (item_info) rarity = item_info->rarity;

        Texture_t* tex = AlterEgo::IconManager::GetIcon(display_id);
        if (tex && tex->Resource) {
            ImVec4 borderColor = GetRarityColor(rarity);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::Image(tex->Resource, ImVec2(ICON_SIZE, ICON_SIZE));
            ImGui::GetWindowDrawList()->AddRect(
                pos, ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE),
                ImGui::ColorConvertFloat4ToU32(borderColor), 0.0f, 0, 2.0f);
        } else {
            ImVec4 col = GetRarityColor(rarity);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                pos, ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE),
                ImGui::ColorConvertFloat4ToU32(ImVec4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.5f)));
            ImGui::GetWindowDrawList()->AddRect(
                pos, ImVec2(pos.x + ICON_SIZE, pos.y + ICON_SIZE),
                ImGui::ColorConvertFloat4ToU32(col), 0.0f, 0, 1.0f);
            ImGui::Dummy(ImVec2(ICON_SIZE, ICON_SIZE));

            if (!icon_url.empty())
                AlterEgo::IconManager::RequestIcon(display_id, icon_url);
        }

        // Same infusion-count badge as the saved Build Library gear view.
        // This is display-only; all equipment comparison remains separate.
        size_t infusionCount = 0;
        for (uint32_t id : eq->infusions)
            if (id != 0) ++infusionCount;
        if (infusionCount > 0) {
            const std::string marker = "Inf " + std::to_string(infusionCount);
            const ImVec2 ts = ImGui::CalcTextSize(marker.c_str());
            const ImVec2 pos = ImGui::GetItemRectMin();
            const ImVec2 badgePos(pos.x + ICON_SIZE - ts.x - 5.0f,
                                  pos.y + ICON_SIZE - ts.y - 3.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(
                ImVec2(badgePos.x - 3.0f, badgePos.y - 1.0f),
                ImVec2(pos.x + ICON_SIZE - 1.0f, pos.y + ICON_SIZE - 1.0f),
                IM_COL32(22, 27, 34, 235), 2.0f);
            dl->AddText(badgePos, IM_COL32(160, 225, 255, 255), marker.c_str());
        }

        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();

            if (tex && tex->Resource) {
                ImGui::Image(tex->Resource, ImVec2(48, 48));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            AlterEgo::UI::TooltipLine(display_name.c_str(),GetRarityColor(rarity));

            if(eq->charges>=0)ImGui::Text("Charges: %lld",static_cast<long long>(eq->charges));

            // Public definition facts remain separate from the equipped instance.
            if(item_info)RenderItemDefinitionDetails(*item_info,AlterEgo::TooltipCore::ItemCombat);

            // Stat type name (e.g. "Berserker's", "Minstrel's")
            if (eq->stat_id != 0) {
                const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(eq->stat_id);
                if (statInfo && !statInfo->name.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", statInfo->name.c_str());
                }
            } else if (item_info && item_info->details.is_object() &&
                       item_info->details.contains("infix_upgrade") &&
                       item_info->details["infix_upgrade"].contains("id")) {
                uint32_t fixedStatId = item_info->details["infix_upgrade"]["id"].get<uint32_t>();
                const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(fixedStatId);
                if (statInfo && !statInfo->name.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", statInfo->name.c_str());
                }
            }

            // Attribute bonuses
            if (!eq->attributes.empty()) {
                for (const auto& [attr, val] : eq->attributes) {
                    // Format attribute name for display (CamelCase → spaced)
                    std::string displayAttr = AlterEgo::UI::FriendlyAttribute(attr);
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "+%d %s", val, displayAttr.c_str());
                }
            } else if(item_info) {
                RenderItemDefinitionDetails(*item_info,AlterEgo::TooltipCore::ItemAttributes);
            }

            // Show the recorded infusion slots, including IDs whose public
            // item metadata hasn't loaded yet.
            if (infusionCount > 0) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.62f, 0.88f, 1.0f, 1.0f),
                    "Infusions (%zu)", infusionCount);
                for (uint32_t id : eq->infusions) {
                    if (id == 0) continue;
                    const auto info = AlterEgo::GW2API::GetItemInfo(id);
                    if (info && !info->name.empty())
                        ImGui::BulletText("%s", info->name.c_str());
                    else
                        ImGui::BulletText("Infusion details unavailable");
                }
            }

            // Upgrades (Runes/Sigils)
            for (auto upgrade_id : eq->upgrades) {
                if (upgrade_id == 0) continue;
                const auto uinfo = AlterEgo::GW2API::GetItemInfo(upgrade_id);
                if (uinfo) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "%s", uinfo->name.c_str());
                    RenderUpgradeTooltipDetails(*uinfo);
                }
            }

            // Transmuted skin — show original item name (the item before reskin)
            if (skin_info && item_info && skin_info->name != item_info->name) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Transmuted");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", item_info->name.c_str());
            }

            if(item_info){
                ImGui::Spacing();
                ImGui::TextColored(GetRarityColor(item_info->rarity),"%s",item_info->rarity.c_str());
                RenderItemDefinitionDetails(*item_info,AlterEgo::TooltipCore::ItemType|AlterEgo::TooltipCore::ItemEffects|AlterEgo::TooltipCore::ItemDescription|AlterEgo::TooltipCore::ItemAcquisition);
            }

            // Skin unlock status — only show if locked (unlocked is implied for equipped items)
            if (eq->skin != 0 && AlterEgo::GW2API::HasSkinUnlockData(eq->skin) &&
                !AlterEgo::GW2API::IsSkinUnlocked(eq->skin)) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Skin Locked");
            }

            // Dyes (limit to skin's actual dye channel count, use material-specific RGB)
            if (!eq->dyes.empty()) {
                int maxDyes = (int)eq->dyes.size();
                if (skin_info && skin_info->dye_slot_count >= 0) {
                    maxDyes = std::min(maxDyes, skin_info->dye_slot_count);
                }
                bool hasAny = false;
                for (int di = 0; di < maxDyes; di++) {
                    if (eq->dyes[di] == 0) continue;
                    const auto dye = AlterEgo::GW2API::GetDyeColor(eq->dyes[di]);
                    if (dye) {
                        if (!hasAny) { ImGui::Spacing(); hasAny = true; }
                        // Pick material-specific RGB; default to cloth when no skin material info
                        int cr = dye->cloth_r, cg = dye->cloth_g, cb = dye->cloth_b;
                        if (skin_info && di < (int)skin_info->dye_slot_materials.size()) {
                            const auto& mat = skin_info->dye_slot_materials[di];
                            if (mat == "leather") { cr = dye->leather_r; cg = dye->leather_g; cb = dye->leather_b; }
                            else if (mat == "metal") { cr = dye->metal_r; cg = dye->metal_g; cb = dye->metal_b; }
                        }
                        ImVec4 dyeCol((float)cr / 255.0f, (float)cg / 255.0f,
                                      (float)cb / 255.0f, 1.0f);
                        ImGui::ColorButton("##dye", dyeCol, 0, ImVec2(12, 12));
                        ImGui::SameLine();
                        ImGui::Text("%s", dye->name.c_str());
                    }
                }
            }

            ImGui::EndGroup();
            ImGui::EndTooltip();
        }

        ImGui::PushID(static_cast<int>(eq->id));AlterEgo::UI::EntityContext("##eq_ctx",[&]{RenderEquipmentContextActions(ch,eq);});ImGui::PopID();
    }

    ImGui::PopID();
}

// Find equipment for a slot + tab
static const AlterEgo::EquipmentItem* FindEquipment(
    const AlterEgo::Character& ch, const std::string& slot, int tab)
{
    for (const auto& eq : ch.equipment) {
        if (eq.slot == slot && (eq.tab == tab || eq.tab == 0)) {
            return &eq;
        }
    }
    return nullptr;
}

// Helper: render one equipment row with label LEFT of icon (for right column)
// Shared resolution of display name + rarity for an equipped slot
static void ResolveEquipDisplay(const AlterEgo::EquipmentItem* eq, const char* slot,
                                std::string& outName, std::string& outRarity) {
    outName = "---";
    outRarity = "Basic";
    if (!eq || eq->id == 0) {
        outName = SlotDisplayName(slot);
        return;
    }
    const auto info = AlterEgo::GW2API::GetItemInfo(eq->id);
    const auto skin = eq->skin ? AlterEgo::GW2API::GetSkinInfo(eq->skin) : nullptr;
    if (info) outRarity = info->rarity;
    if (std::string(slot) == "Relic" && info && info->rarity == "Legendary") {
        outName = "Legendary Relic";
    } else if (skin) {
        outName = skin->name;
    } else if (info) {
        outName = info->name;
    } else {
        outName = SlotDisplayName(slot);
    }
}

// Truncate a string with "..." so its rendered width fits maxW.
static std::string TruncateToWidth(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    const char* ell = "..."; // …
    float ellW = ImGui::CalcTextSize(ell).x;
    int lo = 0, hi = (int)s.size();
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (ImGui::CalcTextSize(s.substr(0, mid).c_str()).x + ellW <= maxW) lo = mid;
        else hi = mid - 1;
    }
    return s.substr(0, lo) + ell;
}

// Live data keeps its own icon, tooltip and instance-specific actions, but
// shares geometry and text wrapping with saved equipment.
static void RenderEquipRow(const AlterEgo::Character& ch, const char* slot, int tab) {
    const auto* eq=FindEquipment(ch,slot,tab);
    std::string name,rarity;ResolveEquipDisplay(eq,slot,name,rarity);
    const auto pos=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    RenderEquipmentSlot(ch,eq,SlotDisplayName(slot));
    AlterEgo::UI::EquipmentText text(pos,ICON_SIZE,width);
    if(eq&&eq->id){
        text.Line(name,ImGui::ColorConvertFloat4ToU32(GetRarityColor(rarity)));
        text.Line(BuildEquipSubtitle(eq),IM_COL32(150,142,120,230));
        ImGui::PushID(slot);ImGui::PushID(static_cast<int>(eq->id));text.ContextTarget();AlterEgo::UI::EntityContext("##eq_text_actions",[&]{RenderEquipmentContextActions(ch,eq);});ImGui::PopID();ImGui::PopID();
    }else text.Line(name,ImGui::GetColorU32(ImGuiCol_TextDisabled));
    text.Finish();
}

// Draw a race silhouette as a semi-transparent background overlay
static void DrawRaceSilhouette(ImDrawList* dl, const std::string& race, const std::string& gender,
                                ImVec2 center, float height) {
    ImU32 col = IM_COL32(255, 255, 255, 25); // Very faint white
    float s = height; // total height scale
    bool female = (gender == "Female");

    // Helper: offset from center
    auto P = [&](float x, float y) -> ImVec2 {
        return ImVec2(center.x + x * s, center.y + y * s);
    };

    if (race == "Asura") {
        // Large head, big ears, tiny body
        float headR = 0.18f;
        dl->AddCircleFilled(P(0, -0.30f), headR * s, col, 24);
        // Ears (triangles)
        dl->AddTriangleFilled(P(-0.15f, -0.35f), P(-0.28f, -0.48f), P(-0.10f, -0.44f), col);
        dl->AddTriangleFilled(P(0.15f, -0.35f), P(0.28f, -0.48f), P(0.10f, -0.44f), col);
        // Eyes (dark spots)
        ImU32 eyeCol = IM_COL32(0, 0, 0, 40);
        dl->AddCircleFilled(P(-0.07f, -0.32f), 0.035f * s, eyeCol, 12);
        dl->AddCircleFilled(P(0.07f, -0.32f), 0.035f * s, eyeCol, 12);
        // Body (small)
        float bw = female ? 0.09f : 0.11f;
        ImVec2 body[] = { P(-bw, -0.12f), P(bw, -0.12f), P(bw * 0.8f, 0.15f), P(-bw * 0.8f, 0.15f) };
        dl->AddConvexPolyFilled(body, 4, col);
        // Arms
        ImVec2 armL[] = { P(-bw, -0.10f), P(-bw - 0.06f, -0.08f), P(-bw - 0.08f, 0.10f), P(-bw, 0.08f) };
        ImVec2 armR[] = { P(bw, -0.10f), P(bw + 0.06f, -0.08f), P(bw + 0.08f, 0.10f), P(bw, 0.08f) };
        dl->AddConvexPolyFilled(armL, 4, col);
        dl->AddConvexPolyFilled(armR, 4, col);
        // Legs
        ImVec2 legL[] = { P(-bw * 0.7f, 0.15f), P(-0.02f, 0.15f), P(-0.03f, 0.38f), P(-bw * 0.8f, 0.38f) };
        ImVec2 legR[] = { P(0.02f, 0.15f), P(bw * 0.7f, 0.15f), P(bw * 0.8f, 0.38f), P(0.03f, 0.38f) };
        dl->AddConvexPolyFilled(legL, 4, col);
        dl->AddConvexPolyFilled(legR, 4, col);
        // Feet (oversized)
        dl->AddCircleFilled(P(-0.06f, 0.40f), 0.04f * s, col, 12);
        dl->AddCircleFilled(P(0.06f, 0.40f), 0.04f * s, col, 12);
    }
    else if (race == "Charr") {
        // Hunched, horns, snout, tail, digitigrade legs
        float headR = 0.10f;
        ImVec2 headC = P(0.02f, -0.32f);
        dl->AddCircleFilled(headC, headR * s, col, 20);
        // Horns
        dl->AddTriangleFilled(P(-0.06f, -0.40f), P(-0.14f, -0.50f), P(-0.02f, -0.42f), col);
        dl->AddTriangleFilled(P(0.10f, -0.40f), P(0.18f, -0.50f), P(0.06f, -0.42f), col);
        // Snout
        dl->AddTriangleFilled(P(-0.02f, -0.28f), P(0.06f, -0.28f), P(0.02f, -0.22f), col);
        // Hunched torso
        float sw = female ? 0.14f : 0.17f;
        ImVec2 torso[] = { P(-sw, -0.22f), P(sw, -0.22f), P(sw * 0.7f, 0.12f), P(-sw * 0.7f, 0.12f) };
        dl->AddConvexPolyFilled(torso, 4, col);
        // Arms (thick)
        ImVec2 armL[] = { P(-sw, -0.20f), P(-sw - 0.07f, -0.16f), P(-sw - 0.05f, 0.08f), P(-sw + 0.02f, 0.06f) };
        ImVec2 armR[] = { P(sw, -0.20f), P(sw + 0.07f, -0.16f), P(sw + 0.05f, 0.08f), P(sw - 0.02f, 0.06f) };
        dl->AddConvexPolyFilled(armL, 4, col);
        dl->AddConvexPolyFilled(armR, 4, col);
        // Digitigrade legs
        ImVec2 legL[] = { P(-0.10f, 0.12f), P(-0.02f, 0.12f), P(-0.06f, 0.30f), P(-0.14f, 0.28f) };
        ImVec2 legR[] = { P(0.02f, 0.12f), P(0.10f, 0.12f), P(0.14f, 0.28f), P(0.06f, 0.30f) };
        dl->AddConvexPolyFilled(legL, 4, col);
        dl->AddConvexPolyFilled(legR, 4, col);
        // Lower legs (reversed knee)
        ImVec2 lowerL[] = { P(-0.14f, 0.28f), P(-0.06f, 0.30f), P(-0.10f, 0.46f), P(-0.16f, 0.44f) };
        ImVec2 lowerR[] = { P(0.06f, 0.30f), P(0.14f, 0.28f), P(0.16f, 0.44f), P(0.10f, 0.46f) };
        dl->AddConvexPolyFilled(lowerL, 4, col);
        dl->AddConvexPolyFilled(lowerR, 4, col);
        // Tail
        ImVec2 tail[] = { P(-0.08f, 0.08f), P(-0.06f, 0.12f), P(-0.24f, 0.20f), P(-0.26f, 0.16f) };
        dl->AddConvexPolyFilled(tail, 4, col);
    }
    else {
        // Human / Norn / Sylvari — humanoid with race-specific proportions
        float headR, shoulderW, hipW, legLen, neckY;
        if (race == "Norn") {
            headR = 0.09f;
            shoulderW = female ? 0.15f : 0.19f;
            hipW = female ? 0.13f : 0.14f;
            legLen = 0.22f;
            neckY = -0.33f;
        } else if (race == "Sylvari") {
            headR = 0.08f;
            shoulderW = female ? 0.11f : 0.13f;
            hipW = female ? 0.10f : 0.10f;
            legLen = 0.22f;
            neckY = -0.34f;
        } else { // Human
            headR = 0.085f;
            shoulderW = female ? 0.12f : 0.15f;
            hipW = female ? 0.11f : 0.11f;
            legLen = 0.21f;
            neckY = -0.33f;
        }

        // Head
        dl->AddCircleFilled(P(0, neckY - headR * 0.8f), headR * s, col, 24);

        // Sylvari leaf crest
        if (race == "Sylvari") {
            dl->AddTriangleFilled(P(-0.04f, neckY - headR * 2.0f),
                                   P(0.04f, neckY - headR * 2.0f),
                                   P(0, neckY - headR * 3.2f), col);
        }

        // Torso
        float torsoBot = 0.08f;
        ImVec2 torso[] = {
            P(-shoulderW, neckY + 0.02f), P(shoulderW, neckY + 0.02f),
            P(hipW, torsoBot), P(-hipW, torsoBot)
        };
        dl->AddConvexPolyFilled(torso, 4, col);

        // Arms
        float armW = 0.045f;
        ImVec2 armL[] = {
            P(-shoulderW, neckY + 0.03f), P(-shoulderW - armW, neckY + 0.05f),
            P(-shoulderW - armW + 0.01f, 0.14f), P(-shoulderW + 0.02f, 0.12f)
        };
        ImVec2 armR[] = {
            P(shoulderW, neckY + 0.03f), P(shoulderW + armW, neckY + 0.05f),
            P(shoulderW + armW - 0.01f, 0.14f), P(shoulderW - 0.02f, 0.12f)
        };
        dl->AddConvexPolyFilled(armL, 4, col);
        dl->AddConvexPolyFilled(armR, 4, col);

        // Legs
        float legW = hipW * 0.55f;
        ImVec2 legL[] = {
            P(-hipW + 0.01f, torsoBot), P(-0.02f, torsoBot),
            P(-0.03f, torsoBot + legLen), P(-hipW, torsoBot + legLen)
        };
        ImVec2 legR[] = {
            P(0.02f, torsoBot), P(hipW - 0.01f, torsoBot),
            P(hipW, torsoBot + legLen), P(0.03f, torsoBot + legLen)
        };
        dl->AddConvexPolyFilled(legL, 4, col);
        dl->AddConvexPolyFilled(legR, 4, col);

        // Feet
        dl->AddCircleFilled(P(-hipW * 0.5f, torsoBot + legLen + 0.01f), 0.03f * s, col, 10);
        dl->AddCircleFilled(P(hipW * 0.5f, torsoBot + legLen + 0.01f), 0.03f * s, col, 10);
    }
}

// Render the equipment panel (game-matching layout)
// Build a native GW2 wardrobe template chat link ([&...], type 0x0F) from the
// armor + weapons shown on the given equipment tab. Per slot: the transmuted
// skin if present, otherwise the item's default skin; plus up to 4 armor dyes.
// Underwater gear and outfits are intentionally omitted.
static std::string BuildWardrobeLink(const AlterEgo::Character& ch, int tab) {
    using namespace AlterEgo;

    auto skinOf = [](const EquipmentItem* eq) -> uint16_t {
        if (!eq) return 0;
        if (eq->skin) return (uint16_t)eq->skin;
        const auto ii = GW2API::GetItemInfo(eq->id);
        if (ii && ii->default_skin) return (uint16_t)ii->default_skin;
        return 0;
    };
    // Per the wiki spec: unused/empty dye channels are 1 (Dye Remover), not 0.
    auto dyesOf = [](const EquipmentItem* eq, uint16_t out[4]) {
        for (int i = 0; i < 4; i++)
            out[i] = (eq && i < (int)eq->dyes.size() && eq->dyes[i] > 0)
                         ? (uint16_t)eq->dyes[i] : 1;
    };

    DecodedWardrobeLink w{};

    const EquipmentItem* helm      = FindEquipment(ch, "Helm", tab);
    const EquipmentItem* shoulders = FindEquipment(ch, "Shoulders", tab);
    const EquipmentItem* chest     = FindEquipment(ch, "Coat", tab);
    const EquipmentItem* gloves    = FindEquipment(ch, "Gloves", tab);
    const EquipmentItem* leggings  = FindEquipment(ch, "Leggings", tab);
    const EquipmentItem* boots     = FindEquipment(ch, "Boots", tab);
    const EquipmentItem* backpack  = FindEquipment(ch, "Backpack", tab);

    w.helm_skin      = skinOf(helm);      dyesOf(helm, w.helm_dyes);
    w.shoulders_skin = skinOf(shoulders); dyesOf(shoulders, w.shoulders_dyes);
    w.chest_skin     = skinOf(chest);     dyesOf(chest, w.chest_dyes);
    w.gloves_skin    = skinOf(gloves);    dyesOf(gloves, w.gloves_dyes);
    w.leggings_skin  = skinOf(leggings);  dyesOf(leggings, w.leggings_dyes);
    w.boots_skin     = skinOf(boots);     dyesOf(boots, w.boots_dyes);
    w.backpack_skin  = skinOf(backpack);  dyesOf(backpack, w.backpack_dyes);

    // Unused slots (outfit, underwater) get Dye Remover in their dye channels.
    for (int i = 0; i < 4; i++) w.outfit_dyes[i] = 1;

    w.weapon_a_main = skinOf(FindEquipment(ch, "WeaponA1", tab));
    w.weapon_a_off  = skinOf(FindEquipment(ch, "WeaponA2", tab));
    w.weapon_b_main = skinOf(FindEquipment(ch, "WeaponB1", tab));
    w.weapon_b_off  = skinOf(FindEquipment(ch, "WeaponB2", tab));

    // Visibility bit flags: which slots the template shows. Chest/shoes/leggings
    // are always set; other slots are flagged only when populated.
    uint16_t vis = 0x0004 | 0x0008 | 0x0040;
    if (w.backpack_skin)  vis |= 0x0002;
    if (w.gloves_skin)    vis |= 0x0010;
    if (w.helm_skin)      vis |= 0x0020;
    if (w.shoulders_skin) vis |= 0x0080;
    if (w.weapon_a_main)  vis |= 0x0800;
    if (w.weapon_a_off)   vis |= 0x1000;
    if (w.weapon_b_main)  vis |= 0x2000;
    if (w.weapon_b_off)   vis |= 0x4000;
    w.visibility_flags = vis;

    return ChatLink::EncodeWardrobe(w);
}

// Shared action for both Characters detail sub-tabs. Implemented after the
// save-dialog opener, so the Equipment view can call it without duplicating
// build-selection logic.
// One navigation path for the Library menu, saved Fashion and imported builds.
static void NavigateToLibraryCollection(int collection) {
    // Inline editing owns the selected Loadout until Save/Discard.
    if(g_LibEditMode&&collection!=0){if(APIDefs)APIDefs->GUI_SendAlert("Save or cancel the current Loadout edit first.");return;}
    MarkNavigation();
    g_LibraryCollection = (collection >= 0 && collection <= 3) ? collection : 0;
    g_SwitchToLibrary = true;
    g_WindowVisible = true;
    SaveSettings();
}

#include "FashionUI.h"

static void RenderSaveLiveBuildAction(const AlterEgo::Character& ch, int preferredEquipTab = 0,
    bool stacked = false, float buttonWidth = 0.0f);
// Shared by Characters, Save, and Compare. Saved library data is irrelevant.
static int GetRetrievedEquipmentTabCount(const AlterEgo::Character& ch) {
    return AlterEgo::EquipmentTabCore::Count(ch);
}

#include "InfusionTotalsView.h"

static void OpenSaveLiveBuildDialog(const AlterEgo::Character& ch,
                                    const AlterEgo::BuildTemplate& bt,
                                    int equipmentTab = 0, int componentKind = -1);
static void RenderLiveComponentSaveMenu(const AlterEgo::Character& ch, int tab,
                                        int kind, const char* label) {
    if (ImGui::BeginPopupContextItem(label)) {
        if (ImGui::MenuItem("Save to Library...", nullptr, false, !ch.equipment.empty())) {
            int idx = 0;
            for (int i = 0; i < (int)ch.build_tabs.size(); ++i)
                if (ch.build_tabs[i].is_active) { idx = i; break; }
            OpenSaveLiveBuildDialog(ch, ch.build_tabs.empty()?AlterEgo::BuildTemplate{}:ch.build_tabs[idx], tab, kind);
        }
        ImGui::EndPopup();
    }
}

// Explicit visible equipment demand, not an inventory-wide background sweep.
static void DemandVisibleEquipmentMetadata(const AlterEgo::Character& ch,int tab,bool tools){
    std::vector<uint32_t> ids;
    for(const auto& eq:ch.equipment){
        const bool utility=AlterEgo::CharacterTools::Utility(eq.slot);
        if(utility!=tools||(eq.tab!=0&&eq.tab!=tab))continue;
        if(eq.id)ids.push_back(eq.id);
        for(auto id:eq.upgrades)if(id)ids.push_back(id);
        for(auto id:eq.infusions)if(id)ids.push_back(id);
    }
    if(!ids.empty())AlterEgo::GW2API::FetchItemDetailsAsync(ids);
}
// Character-wide utility items use the existing equipment icon, tooltip and
// context menu; no template selector or save-to-loadout action.
static void RenderCharacterToolsPanel(const AlterEgo::Character& ch) {
    using namespace AlterEgo;
    DemandVisibleEquipmentMetadata(ch,ch.active_equipment_tab,true);
    const auto row=[&](const char* slot,const char* label,bool gathering,bool charges) {
        ImGui::PushID(slot);
        const auto* eq=CharacterTools::Find(ch.equipment,slot,ch.active_equipment_tab);
        if(!eq){
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::TextDisabled("Not reported");
            ImGui::PopID();return;
        }
        const auto info = GW2API::GetItemInfo(eq->id);
        const std::string name=info&&!info->name.empty()?info->name:"Item #"+std::to_string(eq->id);
        RenderEquipmentSlot(ch,eq,label);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);
        ImGui::PushStyleColor(ImGuiCol_Text,GetRarityColor(info?info->rarity:"Basic"));
        ImGui::TextWrapped("%s",name.c_str());
        ImGui::PopStyleColor();
        ImGui::PushID(static_cast<int>(eq->id));UI::EntityContext("##tool_item",[&]{RenderEquipmentContextActions(ch,eq);});ImGui::PopID();
        if(eq->skin){
            const auto skin = GW2API::GetSkinInfo(eq->skin);
            const auto skinName=skin&&!skin->name.empty()?skin->name:"Skin #"+std::to_string(eq->skin);
            ImGui::TextWrapped("Skin: %s",skinName.c_str());
            ImGui::PushID(static_cast<int>(eq->skin));UI::EntityContext("##tool_skin",[&]{CopyEntityMenu(skin?skin->name:std::string{},LINK_SKIN,eq->skin);});ImGui::PopID();
        }
        if(charges){
            if(eq->charges>=0)ImGui::Text("Charges: %lld",static_cast<long long>(eq->charges));
            // Unknown charges are omitted; explicit zero is still reported.
        }
        if(gathering){
            bool reported=false;
            for(auto id:eq->upgrades){
                if(!id)continue;
                reported=true;
                const auto glyph = GW2API::GetItemInfo(id);
                const auto glyphName=glyph&&!glyph->name.empty()?glyph->name:"Item #"+std::to_string(id);
                ImGui::TextWrapped("Glyph: %s",glyphName.c_str());
                ImGui::PushID(static_cast<int>(id));UI::EntityContext("##tool_glyph",[&]{CopyEntityMenu(glyph?glyph->name:std::string{},LINK_ITEM,id);});ImGui::PopID();
            }
            if(!reported){
                ImGui::TextDisabled("Glyph: Unknown");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("The GW2 API may omit installed glyphs. Unknown does not mean empty.");
            }
        }
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::PopID();
    };
    RenderSectionHeader("Gathering Tools",UI::SectionGold);
    row("Pick","Mining Pick",true,true);
    row("Axe","Logging Axe",true,true);
    row("Sickle","Harvesting Sickle",true,true);
    RenderSectionHeader("Fishing",UI::SectionGold);
    row("FishingRod","Fishing Rod",false,false);
    row("FishingLure","Lure",false,true);
    row("FishingBait","Bait",false,true);
    RenderSectionHeader("Jade Bot",UI::SectionGold);
    row("PowerCore","Power Core",false,false);
    row("SensoryArray","Sensory Array",false,false);
    row("ServiceChip","Service Chip",false,false);
}

static void RenderEquipmentPanel(const AlterEgo::Character& ch) {
    // Both detail views start at the same origin below the Equipment / Build tabs.
    // Equipment items supply their own padding further down the pane.

    // Equipment tab selector
    const int maxTab = GetRetrievedEquipmentTabCount(ch);
    int tab = (g_SelectedEquipTab > 0) ? g_SelectedEquipTab : ch.active_equipment_tab;
    if (tab < 1 || tab > maxTab)
        tab = (ch.active_equipment_tab >= 1 && ch.active_equipment_tab <= maxTab)
                ? ch.active_equipment_tab : 1;

    // Equipment tab chips. Always shown (even for a single tab) so the tab is a
    // consistent right-click target for the Fashion menu.
    {
        std::vector<std::string> labels;
        std::vector<bool> activeMarkers;
        labels.reserve(maxTab);
        activeMarkers.reserve(maxTab);
        for (int t = 1; t <= maxTab; t++) {
            std::string label;
            auto nameIt = ch.equipment_tab_names.find(t);
            if (nameIt != ch.equipment_tab_names.end() && !nameIt->second.empty()) {
                label = nameIt->second;
            } else {
                label = "Tab " + std::to_string(t);
            }
            labels.push_back(label);
            activeMarkers.push_back(ch.active_equipment_tab == t);
        }
        int selIdx = tab - 1;
        int fashionChip = -1;
        if (RenderChipStrip(labels, activeMarkers, selIdx, g_EquipTabScroll, "##equip_chips",
                            "Right-click for fashion options", &fashionChip)) {
            MarkNavigation();g_SelectedEquipTab = selIdx + 1;
            g_DetailsFetched = false;
        }
        if (fashionChip >= 0) {
            g_FashionTab = fashionChip + 1;
            ImGui::OpenPopup("##fashion_menu");
        }
    }

    // Refresh the displayed tab immediately after a chip selection.
    if (g_SelectedEquipTab > 0 && g_SelectedEquipTab <= maxTab)
        tab = g_SelectedEquipTab;
    DemandVisibleEquipmentMetadata(ch,tab,false);

    // Fashion menu: share the right-clicked tab's look as a wardrobe template chat link.
    if (ImGui::BeginPopup("##fashion_menu")) {
        int fTab = (g_FashionTab > 0) ? g_FashionTab : tab;
        if (ImGui::MenuItem("Save to Library...", nullptr, false, !ch.build_tabs.empty()||!ch.equipment.empty())) {
            int idx = 0;
            for (int i = 0; i < (int)ch.build_tabs.size(); ++i)
                if (ch.build_tabs[i].is_active) { idx = i; break; }
            OpenSaveLiveBuildDialog(ch,ch.build_tabs.empty()?AlterEgo::BuildTemplate{}:ch.build_tabs[idx],
                fTab,ch.build_tabs.empty()?kSaveEntireEquipmentSet:-1);
        }
        if(ImGui::MenuItem("Save Equipment Set...",nullptr,false,!ch.equipment.empty()))
            OpenSaveLiveBuildDialog(ch,AlterEgo::BuildTemplate{},fTab,kSaveEntireEquipmentSet);
        if (ImGui::Selectable("Copy Wardrobe Template chat link")) {
            std::string link = BuildWardrobeLink(ch, fTab);
            if (!link.empty()) {
                CopyToClipboard(link);
                if (APIDefs) APIDefs->GUI_SendAlert("Wardrobe code copied to clipboard! Paste in GW2 chat.");
            } else if (APIDefs) {
                APIDefs->GUI_SendAlert("Could not build a wardrobe code for this tab.");
            }
        }
        // Preview opens Pie UI's native Wardrobe Template window for this look.
        // Only offered when Pie UI is loaded (it performs the native action).
        if (g_PieUIPresent.load() && ImGui::Selectable("Preview in Pie UI")) {
            PieUI_PreviewChatLink(BuildWardrobeLink(ch, fTab));
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    AlterEgo::UI::EquipmentLayout("##live_gear",true,
        [&](const char* slot){RenderEquipRow(ch,slot,tab);},
        [&](const char* label,int kind){
            RenderSectionHeader(label,AlterEgo::UI::SectionGold);
            if(kind>=0){ImGui::PushID(label);RenderLiveComponentSaveMenu(ch,tab,kind,"##save_live_section");ImGui::PopID();}
        });
    RenderLiveInfusionTotals(ch, tab, [&]() {
        RenderLiveComponentSaveMenu(ch, tab, 8, "##save_live_infusions");
    });
}

// Keep every specialization name readable inside the left trait-art gutter.
// Wrapping avoids hiding longer names such as Soul Reaping and Wilderness
// Survival under the trait icons. Both the live and saved-build views use this.
static ImVec2 DrawSpecializationNameBadge(
    ImDrawList* dl, const std::string& name, ImVec4 color,
    ImVec2 rowOrigin, float rowH, ImVec2& badgePos) {
    const float width = SPEC_NAME_BADGE_WIDTH;
    const float padding = 6.0f;
    const float textWidth = width - 2.0f * padding;
    const float fontSize = ImGui::GetFontSize() * 1.20f;
    ImFont* font = ImGui::GetFont();
    const ImVec2 textSize = font->CalcTextSizeA(
        fontSize, 1.0e8f, textWidth, name.c_str());
    const float height = std::min(rowH - 4.0f, std::max(26.0f, textSize.y + 9.0f));
    badgePos = ImVec2(rowOrigin.x + 5.0f,
        rowOrigin.y + (rowH - height) * 0.5f);
    const ImVec2 badgeEnd(badgePos.x + width, badgePos.y + height);
    dl->AddRectFilled(badgePos, badgeEnd, IM_COL32(13, 16, 22, 217), 4.0f);
    dl->AddRect(badgePos, badgeEnd, IM_COL32(158, 155, 140, 90), 4.0f);
    const ImVec2 textPos(badgePos.x + padding, badgePos.y + (height - textSize.y) * 0.5f);
    dl->PushClipRect(badgePos, badgeEnd, true);
    dl->AddText(font, fontSize, textPos,
        ImGui::ColorConvertFloat4ToU32(color), name.c_str(), nullptr, textWidth);
    dl->PopClipRect();
    return ImVec2(width, height);
}

// Render the build panel
// Draw a marching ants animated line between two points
static void DrawDottedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float thickness = 1.5f, float dashLen = 4.0f, float gapLen = 3.0f) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float nx = dx / len, ny = dy / len;
    float cycle = dashLen + gapLen;
    float offset = fmodf((float)ImGui::GetTime() * 21.0f, cycle);
    float pos = offset - cycle;
    while (pos < len) {
        float t0 = pos > 0.0f ? pos : 0.0f;
        float t1 = pos + dashLen < len ? pos + dashLen : len;
        if (t1 > t0)
            dl->AddLine(ImVec2(a.x + nx * t0, a.y + ny * t0),
                        ImVec2(a.x + nx * t1, a.y + ny * t1), color, thickness);
        pos += cycle;
    }
}

// Explicit context actions use the existing verified-name Wiki search URL;
// opening a browser is user-initiated and never fetches addon metadata.
static void ReferenceWikiSearchAction(const std::string& name){
    using namespace AlterEgo;
    if(!name.empty()&&UI::MenuAction("Search Wiki",UI::MenuIcon::Open)){
        const auto url=UI::CatalogWikiSearchUrl(name);
        ShellExecuteA(NULL,"open",url.c_str(),NULL,NULL,SW_SHOWNORMAL);
    }
}
static void ReferenceEntityActions(const std::string& name,AlterEgo::ChatLinkType kind,uint32_t id){
    CopyEntityMenu(name,kind,id);
    if(kind==AlterEgo::LINK_SKILL||kind==AlterEgo::LINK_TRAIT)ReferenceWikiSearchAction(name);
}
// Shared cached semantic tooltip for ordinary, aquatic, trait, companion
// and Compare consumers. Unknown facts never fabricate character-specific values.
static void RenderReferenceTooltip(const std::string& name,const std::string& description,
        const nlohmann::json& facts,const std::string& kind={},const char* role=nullptr,
        std::optional<bool> noUnderwater=std::nullopt,
        const std::vector<std::string>& metadataLines={}){
    if(name.empty())return;
    using namespace AlterEgo;
    ImGui::BeginTooltip();
    UI::TooltipLine(name.c_str(),UI::TooltipTitle);
    if(role&&*role)ImGui::TextDisabled("%s",role);
    if(!kind.empty()&&(!role||kind!=role))ImGui::TextDisabled("%s",kind.c_str());
    if(!description.empty())UI::TooltipLine(StripGW2Markup(description).c_str());
    if(noUnderwater.value_or(false))UI::TooltipLine("Not usable underwater",UI::WarningText);
    for(const auto& line:metadataLines)UI::TooltipLine(StripGW2Markup(line).c_str());
    const auto lines=TooltipCore::FactLines(facts);
    if(!lines.empty()){ImGui::Separator();for(const auto& line:lines)UI::TooltipLine(StripGW2Markup(line).c_str());}
    ImGui::EndTooltip();
}
static void RenderSkillTooltip(const AlterEgo::SkillInfo& skill,const char* role=nullptr){
    RenderReferenceTooltip(skill.name,skill.description,skill.facts,skill.type,role,skill.no_underwater,
        AlterEgo::TooltipCore::MetadataLines(skill.optional_metadata,skill.professions));
}
static void RenderTraitTooltip(const AlterEgo::TraitInfo& trait){
    auto lines=AlterEgo::TooltipCore::MetadataLines(trait.optional_metadata);
    if(trait.optional_metadata.is_object()&&trait.optional_metadata.contains("skills")){
        for(const auto& related:trait.optional_metadata.at("skills")){
            if(lines.size()>=12)break;
            const auto name=related.value("name",std::string{});
            if(!name.empty())lines.push_back("Triggered skill: "+name);
        }
    }
    RenderReferenceTooltip(trait.name,trait.description,trait.facts,trait.slot.empty()?std::string{}:trait.slot+" Trait",nullptr,std::nullopt,lines);
}
// Helper: render a single trait icon with selected/dimmed state and tooltip
// Returns the center screen position of the rendered icon
static ImVec2 RenderTraitIcon(uint32_t trait_id, bool selected, bool isMinor, float size,
                              bool editable = false, bool* clicked = nullptr,bool* clearRequested=nullptr) {
    const auto tinfo = AlterEgo::GW2API::GetTraitInfo(trait_id);

    ImGui::PushID((int)(trait_id + 2000000));

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Editable: an invisible button owns the click; the icon draws via the draw list so
    // the cell layout stays identical to the read-only path. Reset the cursor afterwards.
    bool hovered = false;
    if (editable) {
        if (ImGui::InvisibleButton("##et", ImVec2(size, size)) && clicked) *clicked = true;
        hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(pos);
    }

    Texture_t* tex = AlterEgo::IconManager::GetIcon(trait_id + 2000000);
    if (tex && tex->Resource) {
        if (selected) {
            dl->AddRect(
                pos,
                ImVec2(pos.x + size, pos.y + size),
                IM_COL32(100, 220, 255, 255), 0.0f, 0, 1.5f);
        }

        if (editable) {
            int a = (!selected && !isMinor) ? 76 : 255; // 0.3*255 dim for unselected majors
            if (hovered) a = 255;                        // brighten on hover
            dl->AddImage(tex->Resource, pos, ImVec2(pos.x + size, pos.y + size),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
            if (hovered)
                dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size),
                            IM_COL32(227, 196, 122, 255), 0.0f, 0, 2.0f);
        } else if (!selected && !isMinor) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3f);
            ImGui::Image(tex->Resource, ImVec2(size, size));
            ImGui::PopStyleVar();
        } else {
            ImGui::Image(tex->Resource, ImVec2(size, size));
        }
    } else {
        ImVec4 col = selected ? ImVec4(0.3f, 0.5f, 0.7f, 0.8f) : ImVec4(0.15f, 0.15f, 0.15f, 0.5f);
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
            ImGui::ColorConvertFloat4ToU32(col));
        if (!editable) ImGui::Dummy(ImVec2(size, size));

        if (tinfo && !tinfo->icon_url.empty())
            AlterEgo::IconManager::RequestIcon(trait_id + 2000000, tinfo->icon_url);
    }

    if(ImGui::IsItemHovered()&&tinfo)RenderTraitTooltip(*tinfo);

    const bool canClear=clearRequested&&selected&&!isMinor;
    if((trait_id||canClear)&&ImGui::BeginPopupContextItem("##trait_name")) {
        ReferenceEntityActions(tinfo?tinfo->name:std::string{},AlterEgo::LINK_TRAIT,trait_id);
        if(canClear){
            if(tinfo)ImGui::Separator();
            if(AlterEgo::UI::MenuAction("Clear Trait",AlterEgo::UI::MenuIcon::Delete))
                *clearRequested=true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return center;
}

// Clickable skill icon for the build editor (icon cache = skill_id + 5000000).
// Returns true when clicked. skill_id 0 draws an empty slot.
static bool RenderSkillIconCore(uint32_t skill_id, float size, bool editable,bool nameMenu=true,
                                const std::string& fallbackIconUrl={},
                                const char* slotLabel=nullptr,bool emptyKnown=false,bool unresolved=false) {
    const auto sinfo = skill_id ? AlterEgo::GW2API::GetSkillInfo(skill_id) : nullptr;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max(pos.x + size, pos.y + size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto iconKey=AlterEgo::ArtworkRequest::SkillKey(skill_id);
    const auto& iconUrl=sinfo&&!sinfo->icon_url.empty()?sinfo->icon_url:fallbackIconUrl;
    Texture_t* tex = AlterEgo::IconManager::GetOrRequestIcon(iconKey,iconUrl);
    ImGui::PushID((int)(skill_id + 5000000));
    bool clicked = false;
    if (editable) {
        clicked = ImGui::InvisibleButton("##es", ImVec2(size, size));
        if (tex && tex->Resource) dl->AddImage(tex->Resource, pos, max);
    } else if (tex && tex->Resource) {
        ImGui::Image(tex->Resource, ImVec2(size, size));
    } else {
        ImGui::Dummy(ImVec2(size, size));
    }
    if (!tex || !tex->Resource) {
        dl->AddRectFilled(pos, max, editable ? IM_COL32(35,35,35,180) :
            skill_id ? IM_COL32(40,40,60,200) : IM_COL32(30,30,30,200));
        if (editable) dl->AddRect(pos, max, IM_COL32(90,80,50,200));
    }
    if(ImGui::IsItemHovered()){
        if(sinfo&&!sinfo->name.empty())RenderSkillTooltip(*sinfo,slotLabel);
        else if(skill_id)AlterEgo::UI::DetailTooltip(slotLabel?slotLabel:"Skill","Recorded skill; metadata not retrieved.");
        else if(slotLabel)AlterEgo::UI::DetailTooltip(slotLabel,unresolved?"Unresolved imported selection; original palette retained.":emptyKnown?"Empty slot":editable?"Not recorded. Click to choose.":"Not recorded.");
        else if(editable)AlterEgo::UI::Hint("Click to choose a skill");
    }
    if(!skill_id&&slotLabel){
        const char* marker=unresolved?"!":emptyKnown?"-":"?";
        const auto text=ImGui::CalcTextSize(marker);
        dl->AddText(ImVec2(pos.x+(size-text.x)*.5f,pos.y+(size-text.y)*.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),marker);
    }
    if(nameMenu&&skill_id&&ImGui::BeginPopupContextItem("##skill_name")) {
        ReferenceEntityActions(sinfo?sinfo->name:std::string{},AlterEgo::LINK_SKILL,skill_id);ImGui::EndPopup();
    }
    ImGui::PopID();
    return clicked;
}
static bool RenderEditableSkillIcon(uint32_t skill_id, float size,bool nameMenu=true) {
    return RenderSkillIconCore(skill_id, size, true,nameMenu);
}

static void RenderSkillIcon(uint32_t skill_id, float size);
// Shared display-only strip. Metadata admission remains with the ordinary
// visible skill owner; no per-view cache, timer or request policy.
static void DemandSkillBarMetadata(const std::array<AlterEgo::SkillEdit::DisplaySlot,5>& slots,bool aquatic){
    const auto ids=AlterEgo::SkillEdit::RecordedSkillIds(slots);
    if(!ids.empty())AlterEgo::GW2API::FetchSkillDetailsAsync(ids,aquatic);
}
static void RenderSkillBarPreview(const std::array<AlterEgo::SkillEdit::DisplaySlot,5>& slots,float size,bool aquatic=false) {
    namespace SE=AlterEgo::SkillEdit;
    DemandSkillBarMetadata(slots,aquatic);
    const auto columns=SE::IconColumns(ImGui::GetContentRegionAvail().x,size,ImGui::GetStyle().ItemSpacing.x,5);
    ImGui::PushID("skill_bar_preview");
    for(size_t i=0;i<slots.size();++i){
        if(i%columns)ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        RenderSkillIconCore(slots[i].id,size,false,true,{},SE::SlotLabel(i),
            slots[i].evidence==SE::SlotEvidence::Empty,slots[i].evidence==SE::SlotEvidence::Unresolved);
        ImGui::PopID();
    }
    ImGui::PopID();
}
static bool DecodeBuildLink(const std::string& link, const std::string& name,
                            AlterEgo::GameMode mode, AlterEgo::SavedBuild& out);

// One positive-evidence projection for Characters, Compare, capture and
// native link admission. Cache retains exact positive IDs and legend strings;
// neither zero nor null/omission is treated as a verified API empty.
static nlohmann::json LiveCompanionPayload(const AlterEgo::BuildTemplate& bt,bool legend){
    const auto catalog=AlterEgo::PublicCatalog::Get();
    return bt.CompanionPayload(legend,[&](const std::string& name){
        return AlterEgo::PublicCatalog::LegendCode(catalog->legends,name);
    });
}

// Encode a live build template (a character's build tab) to a GW2 build chat link [&...].
// NOTE: BuildTemplate's SpecLine.traits stores choice indices 0-3 directly (see GW2API.h),
// so they map straight into the chat-link trait fields — no major_traits ID lookup.
static std::string EncodeBuildTabChatLink(const AlterEgo::BuildTemplate& bt) {
    if(bt.profession=="Ranger"||bt.profession=="Revenant"){
        const bool legend=bt.profession=="Revenant";
        const auto slots=AlterEgo::SkillEdit::JsonCompanionSlots(LiveCompanionPayload(bt,legend),legend?255u:UINT32_MAX);
        if(!AlterEgo::SkillEdit::CompanionPairsRepresentable(slots))return {};
    }
    const auto catalog=AlterEgo::PublicCatalog::Get();
    const auto link=AlterEgo::ProjectDerivedBuildLink(bt,
        [](uint32_t,int,int choice)->std::optional<int>{return choice;},
        [&](uint32_t id){return AlterEgo::GW2API::GetPaletteIdFromSkill(bt.profession,id);},
        [&](int i)->std::optional<uint32_t>{
            const auto& name=i<2?bt.legends.terrestrial[i]:bt.legends.aquatic[i-2];
            if(name.empty())return 0;
            const auto id=AlterEgo::PublicCatalog::LegendCode(catalog->legends,name);
            if(!id)return std::nullopt;
            return id;
        });
    return link?AlterEgo::ChatLink::EncodeBuild(*link):std::string{};
}

// Capture an native API build tab directly rather than round-tripping through a chat link.
// A link is still recorded when the profession palette is available, but missing
// palette data cannot prevent saving the real skill/trait IDs already in memory.
static void OpenSaveLiveBuildDialog(const AlterEgo::Character& ch,
                                    const AlterEgo::BuildTemplate& bt,
                                    int equipmentTab, int componentKind) {
    const int requestedEquipment = equipmentTab > 0 ? equipmentTab :
        (ch.active_equipment_tab > 0 ? ch.active_equipment_tab : 1);
    // Compare the original launch context, not the user's edited form fields.
    // Reopening the same dialog should reveal it without discarding its draft.
    const auto address=CharacterAddressFor(ch);
    static int launchBuildTab = 0, launchEquipmentTab = 0, launchKind = -1;
    g_SaveLibDialogFocus.Request();
    if(g_SaveLibDialogOpen){
        if(!address||!g_SaveLibAddress||!AlterEgo::CharacterIdentity::SameAddress(*address,*g_SaveLibAddress)||launchBuildTab!=bt.tab||
           launchEquipmentTab!=requestedEquipment||launchKind!=componentKind)
            g_SaveLibError="Save or cancel the current capture before starting another.";
        return;
    }
    if(g_LibEditMode||OtherLibraryEditorOpen()){
        if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");
        return;
    }
    if(!address){if(APIDefs)APIDefs->GUI_SendAlert("Character identity unavailable. Refresh and try again.");return;}
    g_SaveLibAddress=address;
    g_SaveLibAccount=ch.account_name;
    launchBuildTab = bt.tab;
    launchEquipmentTab = requestedEquipment;
    launchKind = componentKind;
    g_SaveLibCharName = ch.name;
    g_SaveLibBuildTab = bt.tab;
    const auto profession=bt.profession.empty()?ch.profession:bt.profession;
    if(bt.tab>0&&componentKind<2&&!AlterEgo::GW2API::HasPaletteData(profession))
        AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
    std::string defaultName = ch.name + " - " +
        (bt.name.empty() ? ("Tab " + std::to_string(bt.tab)) : bt.name);
    snprintf(g_SaveLibName, sizeof(g_SaveLibName), "%s", defaultName.c_str());
    g_SaveLibModes = {AlterEgo::GameMode::PvE};
    g_SaveLibNameManuallyEdited = false;
    g_SaveLibEquipmentMode=0;g_SaveLibSetName.clear();g_SaveLibSetKinds={"weapons","armor","trinkets","runes","sigils","infusions","relic","aquatic"};g_SaveLibExactSets.clear();g_SaveLibSelectedSet.clear();
    g_SaveLibTarget = componentKind >= 0 ? 1 : 0;
    g_SaveLibComponentKind = componentKind >= 0 ? componentKind : 0;
    g_SaveLibUpdateId.clear();
    g_SaveLibFashionError.clear();
    g_SaveLibEquipTab = requestedEquipment;
    g_SaveLibError.clear();g_SaveLibBaseline=CaptureFormSnapshot();
    g_SaveLibDialogOpen = true;
}

// Capture the same retrieved data for both Save and Share. This is a temporary
// in-memory snapshot; only the Save dialog calls AddSavedBuild and persists it.
static AlterEgo::SavedBuild CaptureRetrievedBuildSnapshot(
    const AlterEgo::Character& character,
    const AlterEgo::BuildTemplate& buildTab,
    int equipTab, bool includeGear, const std::string& name,
    AlterEgo::GameMode gameMode, const std::string& chatLink) {
    AlterEgo::SavedBuild sb;
    const auto* capturedChar = &character;
    const auto* capturedTab = &buildTab;
            sb.name = name;
            sb.source_character = capturedChar->name;
            if(const auto address=CharacterAddressFor(*capturedChar))
                sb.source_identity=AlterEgo::CharacterIdentity::CaptureProvenance(*address);
            sb.profession = capturedTab->profession.empty() ? capturedChar->profession : capturedTab->profession;
            sb.game_mode = gameMode;
            sb.chat_link = chatLink;
            for (int i = 0; i < 3; ++i) sb.specializations[i] = capturedTab->specializations[i];
            sb.terrestrial_skills = capturedTab->terrestrial_skills;
            sb.aquatic_skills = capturedTab->aquatic_skills;
            sb.pets = capturedTab->pets;
            sb.weapons = capturedTab->weapons;
            if(sb.profession=="Ranger"){
                auto payload=LiveCompanionPayload(*capturedTab,false);
                if(!payload.empty())sb.pets_source_slots=std::move(payload);
            }else if(sb.profession=="Revenant"){
                auto payload=LiveCompanionPayload(*capturedTab,true);
                const auto slots=AlterEgo::SkillEdit::JsonCompanionSlots(payload,255u);
                for(size_t i=0;i<4;++i)sb.legend_codes[i]=static_cast<uint8_t>(slots.values[i]);
                if(!payload.empty())sb.legends_source_slots=std::move(payload);
            }
            // Optionally populate gear from character equipment
            if (includeGear) {
                const auto* ch = capturedChar;
                if (ch) {
                    // Pre-fetch any item details not yet cached (weapons, upgrades)
                    std::vector<uint32_t> missingIds;
                    for (const auto& eq : ch->equipment) {
                        if (!AlterEgo::IsEquipmentInCapturedBuild(
                                eq.slot, eq.tab, equipTab)) continue;
                        if (eq.id != 0 && !AlterEgo::GW2API::GetItemInfo(eq.id))
                            missingIds.push_back(eq.id);
                        for (auto uid : eq.upgrades) {
                            if (uid != 0 && !AlterEgo::GW2API::GetItemInfo(uid))
                                missingIds.push_back(uid);
                        }
                    }
                    if (!missingIds.empty())
                        AlterEgo::GW2API::FetchItemDetails(missingIds);

                    std::string sharedRune;
                    for (const auto& eq : ch->equipment) {
                        if (!AlterEgo::IsEquipmentInCapturedBuild(
                                eq.slot, eq.tab, equipTab)) continue;

                        AlterEgo::BuildGearSlot gs;
                        gs.slot = eq.slot;
                        gs.item_id = eq.id;
                        gs.infusion_ids = eq.infusions;
                        gs.infusion_snapshot = true;

                        // Resolve stat name
                        gs.stat_id = eq.stat_id;
                        if (!gs.stat_id && eq.id) {
                            const auto item = AlterEgo::GW2API::GetItemInfo(eq.id);
                            if (item && item->details.contains("infix_upgrade") &&
                                item->details["infix_upgrade"].contains("id") &&
                                item->details["infix_upgrade"]["id"].is_number_unsigned())
                                gs.stat_id = item->details["infix_upgrade"]["id"].get<uint32_t>();
                        }
                        if (gs.stat_id) {
                            const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(gs.stat_id);
                            if (statInfo) gs.stat_name = statInfo->name;
                        }

                        // Determine if this is an armor or weapon slot for rune/sigil
                        bool isArmor = (eq.slot == "Helm" || eq.slot == "Shoulders" ||
                                        eq.slot == "Coat" || eq.slot == "Gloves" ||
                                        eq.slot == "Leggings" || eq.slot == "Boots");
                        bool isWeapon = (eq.slot.find("Weapon") == 0);

                        // Resolve upgrades (runes for armor, sigils for weapons)
                        for (size_t ui = 0; ui < eq.upgrades.size(); ui++) {
                            if (eq.upgrades[ui] == 0) continue;
                            const auto upInfo = AlterEgo::GW2API::GetItemInfo(eq.upgrades[ui]);
                            std::string upName = upInfo?upInfo->name:std::string();
                            if(!upName.empty())AlterEgo::GW2API::CacheItemNameId(upName, eq.upgrades[ui]);
                            if (isArmor && ui == 0) {
                                gs.rune = upName;
                                gs.rune_id = eq.upgrades[ui];
                                if (sharedRune.empty()) sharedRune = upName;
                            } else if (isWeapon && ui == 0) {
                                gs.sigil = upName;
                                gs.sigil_id = eq.upgrades[ui];
                            } else if (isWeapon && ui == 1) {
                                gs.sigil2 = upName;
                                gs.sigil2_id = eq.upgrades[ui];
                            }
                        }

                        // Resolve weapon type from item details
                        if (isWeapon && eq.id != 0) {
                            const auto itemInfo = AlterEgo::GW2API::GetItemInfo(eq.id);
                            if (itemInfo && itemInfo->details.contains("type"))
                                gs.weapon_type = itemInfo->details["type"].get<std::string>();
                        }

                        sb.gear[eq.slot] = gs;
                    }
                    sb.rune_name = sharedRune;
                    // Store shared rune ID from first armor piece
                    for (const auto& eq2 : ch->equipment) {
                        if (eq2.tab != equipTab) continue;
                        bool isArmor2 = (eq2.slot == "Helm" || eq2.slot == "Shoulders" ||
                                         eq2.slot == "Coat" || eq2.slot == "Gloves" ||
                                         eq2.slot == "Leggings" || eq2.slot == "Boots");
                        if (isArmor2 && !eq2.upgrades.empty() && eq2.upgrades[0] != 0) {
                            sb.rune_id = eq2.upgrades[0];
                            break;
                        }
                    }

                    // Relics from /equipment can be shared (tab 0) even when
                    // displayed in a numbered tab. Keep the original item ID
                    // if public item details have not loaded yet.
                    for (const auto& eq : ch->equipment) {
                        if (eq.slot == "Relic" &&
                            AlterEgo::IsEquipmentInCapturedBuild(
                                eq.slot, eq.tab, equipTab) &&
                            eq.id != 0) {
                            const auto relicInfo = AlterEgo::GW2API::GetItemInfo(eq.id);
                            if (relicInfo) {
                                sb.relic_name = relicInfo->name;
                                // Use exotic relic ID from name cache (legendary relic has a shared ID)
                                sb.relic_id = AlterEgo::GW2API::FindItemIdByName(relicInfo->name);
                            }
                            if (sb.relic_id == 0) sb.relic_id = eq.id; // fallback
                            break;
                        }
                    }
                }
            }

    return sb;
}

// One Save action, rendered in the same place in both character detail views.
// It always captures the selected build tab (or the currently active build if
// nothing has been selected). Equipment view additionally preselects its
// currently displayed equipment set; the dialog can still change that choice.
static void RenderBuildShareMenuItems(const AlterEgo::SavedBuild& build,
    const AlterEgo::Character* liveCharacter = nullptr);

// Resolve the same live snapshot regardless of which UI entry point was used.
static int ActiveBuildIndex(const AlterEgo::Character& ch) {
    for (int i = 0; i < (int)ch.build_tabs.size(); ++i)
        if (ch.build_tabs[i].is_active) return i;
    return ch.build_tabs.empty() ? -1 : 0;
}
static int ActiveEquipmentTab(const AlterEgo::Character& ch) {
    return ch.active_equipment_tab > 0 ? ch.active_equipment_tab : 1;
}
static AlterEgo::SavedBuild MakeLiveShareSnapshot(
    const AlterEgo::Character& ch, int buildIndex, int equipmentTab) {
    if (buildIndex < 0 || buildIndex >= (int)ch.build_tabs.size()) return {};
    const auto& bt = ch.build_tabs[buildIndex];
    const std::string profession = bt.profession.empty() ? ch.profession : bt.profession;
    if (!AlterEgo::GW2API::HasPaletteData(profession))
        AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
    const std::string link = AlterEgo::GW2API::HasPaletteData(profession)
        ? EncodeBuildTabChatLink(bt) : std::string();
    return CaptureRetrievedBuildSnapshot(ch, bt, equipmentTab, true,
        ch.name + " - " + (bt.name.empty() ? "Tab " + std::to_string(bt.tab) : bt.name),
        AlterEgo::GameMode::PvE, link);
}
static void RenderSaveLiveBuildAction(const AlterEgo::Character& ch, int preferredEquipTab,
    bool stacked, float buttonWidth) {
    // Keep a snapshot for the Share popup so it never accidentally uses a
    // similarly named entry from the saved library.
    static AlterEgo::SavedBuild shareSnapshot;
    static int shareBuildIdx = -1;
    static std::optional<AlterEgo::Character> shareSource;

    const bool hasBuilds = !ch.build_tabs.empty();
    int buildIdx = g_SelectedBuildTab;
    if (hasBuilds && (buildIdx < 0 || buildIdx >= static_cast<int>(ch.build_tabs.size()))) {
        buildIdx = 0;
        for (int i = 0; i < static_cast<int>(ch.build_tabs.size()); ++i) {
            if (ch.build_tabs[i].is_active) { buildIdx = i; break; }
        }
        MarkNavigation();g_SelectedBuildTab = buildIdx;
    }

    if (!hasBuilds) {
        if(!ch.equipment.empty()) {
            if(RenderActionButton("Save##live_save",ImVec2(buttonWidth,0)))
                OpenSaveLiveBuildDialog(ch,AlterEgo::BuildTemplate{},preferredEquipTab,kSaveEntireEquipmentSet);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Save retrieved equipment to Library.");
        } else ImGui::TextDisabled("No retrieved build or equipment available.");
        return;
    }

    if (RenderActionButton("Save##live_save", ImVec2(buttonWidth, 0))) {
        OpenSaveLiveBuildDialog(ch, ch.build_tabs[ActiveBuildIndex(ch)], preferredEquipTab);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save Current Build + Gear to Library...\n"
            "Save a complete Loadout, equipment set, individual component, or Fashion.");

    if (!stacked) ImGui::SameLine();
    if (RenderActionButton("Share##live_share", ImVec2(buttonWidth, 0))) {
        const int equipTab = preferredEquipTab > 0 ? preferredEquipTab :
            (g_SelectedEquipTab > 0 ? g_SelectedEquipTab : ActiveEquipmentTab(ch));
        shareSnapshot = MakeLiveShareSnapshot(ch, buildIdx, equipTab);
        shareSource=ch;
        shareBuildIdx = buildIdx;
        ImGui::OpenPopup("##live_share_menu");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Share the selected character's retrieved build and equipment, not a saved build.");

    if (ImGui::BeginPopup("##live_share_menu")) {
        if (shareSnapshot.chat_link.empty() && shareSource &&
            shareBuildIdx >= 0 && shareBuildIdx < static_cast<int>(shareSource->build_tabs.size())) {
            const auto& bt = shareSource->build_tabs[shareBuildIdx];
            const std::string profession = bt.profession.empty() ? shareSource->profession : bt.profession;
            if (AlterEgo::GW2API::HasPaletteData(profession))
                shareSnapshot.chat_link = EncodeBuildTabChatLink(bt);
        }
        if (shareSnapshot.chat_link.empty())
            ImGui::TextDisabled("Build link unavailable: missing or unrepresentable data.");
        // Fashion sharing uses the character's active equipment tab and stays
        // available even while the build palette is still loading.
        RenderBuildShareMenuItems(shareSnapshot,shareSource?&*shareSource:nullptr);
        ImGui::EndPopup();
    }
}

// Character-wide actions live in the header, not in either detail tab.
// Keep character metadata in the identity column beside the avatar, rather than
// orphaned beneath the Save / Share / Fashion action column. Wrap complete
// icon/value pairs to another line when the identity column gets narrow.
static void RenderCharacterHeaderMetadata(const AlterEgo::Character& ch, float width) {
    const int birthdayDays = DaysUntilBirthday(ch.created);
    const int age = CharacterAgeYears(ch.created);
    if (birthdayDays == 0 && age >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
            "Happy Birthday! Turning %d today!", age + 1);
        return;
    }

    const ImVec4 dim(0.50f, 0.47f, 0.40f, 1.0f);
    const ImVec4 separatorColor(0.40f, 0.37f, 0.30f, 1.0f);
    const float iconSize = ImGui::GetTextLineHeight();
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 2.0f;
    const float left = ImGui::GetCursorScreenPos().x;
    const float top = ImGui::GetCursorScreenPos().y;
    const float right = left + ImMax(1.0f, width);
    const float itemGap = 8.0f;
    const float separatorWidth = ImGui::CalcTextSize("-").x + 12.0f;
    float nextX = left;
    float rowY = top;
    ImGui::BeginGroup();

    auto drawPair = [&](uint32_t iconId, const char* url, const char* tooltip,
                        const std::string& value, bool separate) {
        Texture_t* tex = AlterEgo::IconManager::GetIcon(iconId);
        if ((!tex || !tex->Resource) && url)
            AlterEgo::IconManager::RequestIcon(iconId, url);
        const float imageWidth = tex && tex->Resource ? iconSize :
            ImGui::CalcTextSize(tooltip).x;
        const float pairWidth = imageWidth + 4.0f +
            ImGui::CalcTextSize(value.c_str()).x;
        bool hasPrevious = nextX > left + 0.5f;
        float leading = hasPrevious && separate ? separatorWidth : 0.0f;
        if (hasPrevious && nextX + leading + pairWidth > right) {
            nextX = left;
            rowY += rowHeight;
            hasPrevious = false;
            leading = 0.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(nextX, rowY));
        ImGui::BeginGroup();
        if (hasPrevious && separate) {
            ImGui::TextColored(separatorColor, "-");
            ImGui::SameLine(0.0f, 6.0f);
        }
        if (tex && tex->Resource) {
            ImGui::Image(tex->Resource, ImVec2(iconSize, iconSize));
        } else {
            ImGui::TextColored(dim, "%s", tooltip);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(dim, "%s", value.c_str());
        ImGui::EndGroup();
        nextX = ImGui::GetItemRectMax().x + itemGap;
    };

    if (age >= 0) {
        drawPair(43766,
            "https://render.guildwars2.com/file/1932B731E2F70F2F1E3D453A4B7C26B24CF647C0/603246.png",
            "Age", std::to_string(age) + "y", true);
    }
    if (birthdayDays > 0) {
        drawPair(98501,
            "https://render.guildwars2.com/file/5C759EC1C95F3BE53C167A7D9F0D27BF3AE56277/625611.png",
            "Birthday", std::to_string(birthdayDays) + "d away", true);
    }
    const auto login = CharacterLastLogin(ch);
    if (login > 0) {
        const int elapsed = (int)difftime(std::time(nullptr), (time_t)login);
        const std::string ago = elapsed < 60 ? "Just now" :
            elapsed < 3600 ? std::to_string(elapsed / 60) + "m ago" :
            elapsed < 86400 ? std::to_string(elapsed / 3600) + "h ago" :
            std::to_string(elapsed / 86400) + "d ago";
        drawPair(68326,
            "https://render.guildwars2.com/file/01E7C8B0EB04F6E0CB25AA0403C96E0355B63A39/924581.png",
            "Last login", ago, true);
    }
    for (size_t i = 0; i < ch.crafting.size(); ++i) {
        const auto& craft = ch.crafting[i];
        const int level = i < ch.crafting_levels.size() ? ch.crafting_levels[i] : 0;
        drawPair(GetCraftingIconId(craft), GetCraftingIconUrl(craft),
                 craft.c_str(), std::to_string(level), i == 0);
    }
    ImGui::EndGroup();
}

static void RenderCharacterHeaderActions(const AlterEgo::Character& ch, float buttonWidth) {
    ImGui::PushID("##character_header_actions");
    // GroupOffset preserves the manually positioned header X across multiple
    // ImGui::Button calls. Without this, only Save stays right-aligned and the
    // following Share button jumps to the left margin.
    ImGui::BeginGroup();
    RenderSaveLiveBuildAction(ch, 0, true, buttonWidth);
    ImGui::EndGroup();
    ImGui::PopID();
}

static void RenderLiveProfessionPairs(const AlterEgo::BuildTemplate& bt);
static void RenderBuildPanel(const AlterEgo::Character& ch) {
    if (ch.build_tabs.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No build tabs available.");
        return;
    }

    // Build tab selector — chip strip
    {
        std::vector<std::string> labels;
        std::vector<bool> activeMarkers;
        labels.reserve(ch.build_tabs.size());
        activeMarkers.reserve(ch.build_tabs.size());
        for (const auto& bt : ch.build_tabs) {
            labels.push_back(bt.name.empty() ? ("Tab " + std::to_string(bt.tab)) : bt.name);
            activeMarkers.push_back(bt.is_active);
        }
        int buildMenuChip = -1;
        RenderChipStrip(labels, activeMarkers, g_SelectedBuildTab, g_BuildTabScroll, "##build_chips",
                        "Right-click for build options", &buildMenuChip);
        if (buildMenuChip >= 0) {
            g_BuildMenuTab = buildMenuChip;
            ImGui::OpenPopup("##build_menu");
        }
    }

    // Build menu: actions for the right-clicked build tab (mirrors the equipment
    // tab Fashion menu). Acts on the right-clicked chip, not the selected one.
    if (ImGui::BeginPopup("##build_menu")) {
        int mTab = (g_BuildMenuTab >= 0 && g_BuildMenuTab < (int)ch.build_tabs.size())
                       ? g_BuildMenuTab : g_SelectedBuildTab;
        if (mTab < 0 || mTab >= (int)ch.build_tabs.size()) mTab = 0;
        const auto& mbt = ch.build_tabs[mTab];
        bool hasPalette = AlterEgo::GW2API::HasPaletteData(mbt.profession);
        if (!hasPalette)
            AlterEgo::GW2API::FetchProfessionPaletteAsync(mbt.profession);

        const auto link=hasPalette?EncodeBuildTabChatLink(mbt):std::string{};
        if (AlterEgo::UI::MenuAction("Copy Build Chat Link",AlterEgo::UI::MenuIcon::Copy,!link.empty())) {
            CopyToClipboard(link);
            if (APIDefs) APIDefs->GUI_SendAlert("Build link copied!");
        }
        if(link.empty())ImGui::TextDisabled("%s",hasPalette?"Build link unavailable for retrieved data.":"Build-link palette loading.");
        if (ImGui::Selectable("Save to Library..."))
            OpenSaveLiveBuildDialog(ch, mbt);
        ImGui::EndPopup();
    }

    // Chip selections may change within this frame; use the current selection.
    const auto& bt = ch.build_tabs[g_SelectedBuildTab];
    AlterEgo::GW2API::FetchSpecDetailsAsync({bt.specializations[0].spec_id,bt.specializations[1].spec_id,bt.specializations[2].spec_id});
    AlterEgo::GW2API::FetchProfessionInfoAsync(bt.profession);


    // Small left/right padding so spec hex portraits & trait selection borders
    // don't sit flush against the scroll-child edges (icon borders extend ~1px).
    ImGui::Indent(4.0f);

    // Record start position for profession/elite spec overlay
    ImVec2 buildPanelStart = ImGui::GetCursorScreenPos();

    // Specializations — game-like trait grid with dotted lines
    const float iconSz = TRAIT_ICON_SIZE; // uniform size for all trait icons
    const float colW = iconSz * 2.0f;

    for (int i = 0; i < 3; i++) {
        const auto& spec = bt.specializations[i];
        if (spec.spec_id == 0) {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "  (empty)");
            continue;
        }

        const auto specInfo = AlterEgo::GW2API::GetSpecInfo(spec.spec_id);
        std::string specName = specInfo ? specInfo->name : ("Spec " + std::to_string(spec.spec_id));
        bool isElite = specInfo ? specInfo->elite : false;
        ImVec4 specColor = isElite ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        ImGui::PushID(spec.spec_id);

        if (specInfo && specInfo->major_traits.size() >= 9) {
            // Positions for dotted lines. specCenter stays at (0,0) — the hex
            // icon has been replaced by the spec name overlay on the banner.
            ImVec2 specCenter(0, 0);
            ImVec2 minorCenters[3] = {};
            ImVec2 selectedCenters[3] = {};
            bool hasSelected[3] = {false, false, false};

            // Row geometry — used for banner backdrop and name overlay.
            ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            float rowW = ImGui::GetContentRegionAvail().x;
            float rowH = iconSz * 3 + ImGui::GetStyle().CellPadding.y * 6;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Spec background banner (panoramic art) behind the trait grid.
            // Scaled to fit row height, native aspect preserved, anchored left,
            // with a fade-to-dark overlay on the right so trait icons stay readable.
            if (!specInfo->background_url.empty()) {
                uint32_t bgKey = spec.spec_id + 1000000;
                Texture_t* bgTex = AlterEgo::IconManager::GetIcon(bgKey);
                if (bgTex && bgTex->Resource && bgTex->Height > 0) {
                    // Zoom the banner and anchor it to the row's bottom-left.
                    // 15 specs ship with banners whose top ~66% is transparent
                    // (opaque art lives only in the bottom band). Those need a
                    // much higher zoom so the opaque content fills the row;
                    // everyone else gets the standard 1.4x.
                    auto IsTransparentTopSpec = [](uint32_t id) {
                        switch (id) {
                            case 4: case 7: case 11: case 20: case 22:
                            case 28: case 35: case 36: case 44: case 51:
                            case 54: case 68: case 70: case 71: case 72:
                            case 73: case 74: case 75: case 76: case 77:
                            case 78: case 79: case 80: case 81:
                                return true;
                            default:
                                return false;
                        }
                    };
                    const float zoom = IsTransparentTopSpec(spec.spec_id) ? 2.94f : 1.4f;
                    float drawH = rowH * zoom;
                    float scale = drawH / (float)bgTex->Height;
                    float drawW = (float)bgTex->Width * scale;
                    float bannerLeft = rowOrigin.x;
                    float bannerTop  = rowOrigin.y + rowH - drawH;
                    dl->PushClipRect(rowOrigin,
                        ImVec2(rowOrigin.x + rowW, rowOrigin.y + rowH), true);
                    dl->AddImage(bgTex->Resource,
                        ImVec2(bannerLeft, bannerTop),
                        ImVec2(bannerLeft + drawW, bannerTop + drawH),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 200));
                    // Fade-to-dark anchored to where the trait grid begins,
                    // not the banner. Trait icons start just past column 0.
                    float fadeStart = rowOrigin.x + SPEC_NAME_BADGE_WIDTH + 12.0f;
                    float fadeEnd   = rowOrigin.x + SPEC_NAME_BADGE_WIDTH + 72.0f;
                    dl->AddRectFilledMultiColor(
                        ImVec2(fadeStart, rowOrigin.y),
                        ImVec2(fadeEnd,   rowOrigin.y + rowH),
                        IM_COL32(15, 17, 23, 0),   IM_COL32(15, 17, 23, 200),
                        IM_COL32(15, 17, 23, 200), IM_COL32(15, 17, 23, 0));
                    if (fadeEnd < rowOrigin.x + rowW) {
                        dl->AddRectFilled(
                            ImVec2(fadeEnd, rowOrigin.y),
                            ImVec2(rowOrigin.x + rowW, rowOrigin.y + rowH),
                            IM_COL32(15, 17, 23, 200));
                    }
                    dl->PopClipRect();
                } else {
                    AlterEgo::IconManager::RequestIcon(bgKey, specInfo->background_url);
                }
            }

            // Spec name overlay on the banner's left. Drop shadow underneath
            // for legibility against bright banner art. Tooltip is attached
            // to an invisible button covering the same area.
            {
                ImVec2 nameAnchor;
                ImVec2 prevPos = ImGui::GetCursorScreenPos();
                const ImVec2 hitSz = DrawSpecializationNameBadge(
                    dl, specName, specColor, rowOrigin, rowH, nameAnchor);
                ImGui::SetCursorScreenPos(nameAnchor);
                ImGui::InvisibleButton("##specname", hitSz);
                if(!g_LibEditMode&&ImGui::BeginPopupContextItem("##spec_name")){
                    CopyNameMenuItem(specName);ImGui::EndPopup();
                }
                if(ImGui::IsItemHovered())RenderSpecializationTooltip(specName,specColor,specInfo->profession+(isElite?" (Elite)":""),false);
                ImGui::SetCursorScreenPos(prevPos);
            }

            // Outer 2-column table: [reserved left gutter] [6-col trait grid]
            char outerId[32];
            snprintf(outerId, sizeof(outerId), "##so_%u", spec.spec_id);
            if (ImGui::BeginTable(outerId, 2,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                    SPEC_NAME_BADGE_WIDTH);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                // Column 0: left gutter (banner art shows through; spec name
                // is overlaid above via the drawlist).
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(SPEC_NAME_BADGE_WIDTH - 8.0f, rowH - 2.0f));

                // Column 1: 6-column × 3-row trait grid
                ImGui::TableNextColumn();
                char innerId[32];
                snprintf(innerId, sizeof(innerId), "##tr_%u", spec.spec_id);
                if (ImGui::BeginTable(innerId, 6,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX)) {
                    for (int c = 0; c < 6; c++)
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, colW);

                    for (int row = 0; row < 3; row++) {
                        ImGui::TableNextRow();
                        for (int tier = 0; tier < 3; tier++) {
                            // Minor trait column: render only in middle row
                            ImGui::TableNextColumn();
                            if (row == 1 && tier < (int)specInfo->minor_traits.size()) {
                                minorCenters[tier] = RenderTraitIcon(
                                    specInfo->minor_traits[tier], true, true, iconSz);
                            }

                            // Major trait column
                            ImGui::TableNextColumn();
                            int majorIdx = tier * 3 + row;
                            uint32_t tid = specInfo->major_traits[majorIdx];
                            bool selected = ((uint32_t)spec.traits[tier] == tid);
                            bool clearRequested=false;
                            ImVec2 c = RenderTraitIcon(tid, selected, false, iconSz,false,nullptr,
                                                       g_LibEditMode?&clearRequested:nullptr);
                            if(clearRequested){
                                g_EditDraft.specializations[i].traits[tier] = 0;
                                MarkEditDirty();
                            }
                            if (selected) {
                                selectedCenters[tier] = c;
                                hasSelected[tier] = true;
                            }
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::EndTable();
            }

            // Draw dotted lines connecting traits (spec→minor0 is skipped
            // because the hex icon is gone). Lines stop at icon edges.
            ImU32 lineCol = IM_COL32(180, 230, 255, 180);
            float minorR = MINOR_TRAIT_SIZE * 0.5f;
            float majorR = iconSz * 0.5f;

            auto InsetLine = [](ImVec2 a, ImVec2 b, float rA, float rB, ImVec2& outA, ImVec2& outB) {
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len < rA + rB) return false;
                float nx = dx / len, ny = dy / len;
                outA = ImVec2(a.x + nx * rA, a.y + ny * rA);
                outB = ImVec2(b.x - nx * rB, b.y - ny * rB);
                return true;
            };

            // Skip the first connector — the hex spec icon is gone, so there
            // is no anchor on the left for a leading dotted line.
            ImVec2 prev;
            float prevR = 0;
            bool havePrev = false;
            (void)specCenter;
            for (int tier = 0; tier < 3; tier++) {
                if (minorCenters[tier].x > 0) {
                    if (havePrev) {
                        ImVec2 la, lb;
                        if (InsetLine(prev, minorCenters[tier], prevR, minorR, la, lb))
                            DrawDottedLine(dl, la, lb, lineCol);
                    }
                    prev = minorCenters[tier]; prevR = minorR; havePrev = true;
                }
                if (hasSelected[tier]) {
                    if (havePrev) {
                        ImVec2 la, lb;
                        if (InsetLine(prev, selectedCenters[tier], prevR, majorR, la, lb))
                            DrawDottedLine(dl, la, lb, lineCol);
                    }
                    prev = selectedCenters[tier]; prevR = majorR; havePrev = true;
                }
            }
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::Spacing();

    // The live API reports only recorded skills and explicit null slots.
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Heal / Utility / Elite");
    ImGui::PushID("live_land_skills");
    RenderSkillBarPreview(AlterEgo::SkillEdit::DisplayBar(bt.terrestrial_skills,bt.terrestrial_skills.empty_slots),ICON_SIZE);
    ImGui::PopID();
    const auto& water=bt.aquatic_skills;
    if(water.heal||water.elite||water.utilities[0]||water.utilities[1]||water.utilities[2]||water.empty_slots){
        if(ImGui::TreeNode("Aquatic skills##live")){
            RenderSkillBarPreview(AlterEgo::SkillEdit::DisplayBar(water,water.empty_slots),ICON_SIZE,true);
            ImGui::TreePop();
        }
    }

    RenderLiveProfessionPairs(bt);
    ImGui::Unindent(4.0f);
    RenderLiveInfusionTotals(ch,
        g_SelectedEquipTab > 0 ? g_SelectedEquipTab :
        (ch.active_equipment_tab > 0 ? ch.active_equipment_tab : 1));
}

// --- Build Library helpers ---

// Profession code to name mapping
static const char* ProfessionFromCode(uint8_t code) {
    switch (code) {
        case 1: return "Guardian";
        case 2: return "Warrior";
        case 3: return "Engineer";
        case 4: return "Ranger";
        case 5: return "Thief";
        case 6: return "Elementalist";
        case 7: return "Mesmer";
        case 8: return "Necromancer";
        case 9: return "Revenant";
        default: return "Unknown";
    }
}

static const char* GameModeImportNames[] = { "PvE", "WvW", "PvP", "Raid", "Fractal", "Other" };

static AlterEgo::GameMode GameModeFromIndex(int idx) {
    switch (idx) {
        case 0: return AlterEgo::GameMode::PvE;
        case 1: return AlterEgo::GameMode::WvW;
        case 2: return AlterEgo::GameMode::PvP;
        case 3: return AlterEgo::GameMode::Raid;
        case 4: return AlterEgo::GameMode::Fractal;
        default: return AlterEgo::GameMode::Other;
    }
}

static const char* GameModeLabel(AlterEgo::GameMode m);
// Shared across New, Import, Save and saved-build Edit. Always retain at least one mode.
// Use one height for every compact mode chip, including the editor header.
static float CompactGameModeChipHeight() {
    return ImMax(22.0f, ImGui::GetTextLineHeight() + 4.0f);
}

static bool RenderGameModeSelector(const char* id, std::vector<AlterEgo::GameMode>& chosen, bool wrap=false) {
    static const AlterEgo::GameMode modes[] = {
        AlterEgo::GameMode::PvE, AlterEgo::GameMode::WvW,
        AlterEgo::GameMode::PvP, AlterEgo::GameMode::Raid,
        AlterEgo::GameMode::Fractal, AlterEgo::GameMode::Other
    };
    if (chosen.empty()) chosen.push_back(AlterEgo::GameMode::PvE);
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < IM_ARRAYSIZE(modes); ++i) {
        const auto mode = modes[i];
        if(i){if(wrap)AlterEgo::UI::SameLineIfActionFits(GameModeLabel(mode));else ImGui::SameLine(0.0f,4.0f);}
        const bool active = std::find(chosen.begin(), chosen.end(), mode) != chosen.end();
        if (RenderChipButton(GameModeLabel(mode), active, ImVec4(0,0,0,0), true)) {
            if (active) {
                if (chosen.size() > 1)
                    chosen.erase(std::remove(chosen.begin(), chosen.end(), mode), chosen.end());
            } else chosen.push_back(mode);
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

static bool GameModeMatchesFilter(const AlterEgo::SavedBuild& build,const std::set<std::string>& filters) {
    return AlterEgo::LibraryFilter::Matches(filters,build.game_modes,build.game_mode,GameModeLabel);
}

static const char* GameModeLabel(AlterEgo::GameMode m) {
    switch (m) {
        case AlterEgo::GameMode::PvE:     return "PvE";
        case AlterEgo::GameMode::WvW:     return "WvW";
        case AlterEgo::GameMode::PvP:     return "PvP";
        case AlterEgo::GameMode::Raid:    return "Raid";
        case AlterEgo::GameMode::Fractal: return "Fractal";
        default:                          return "Other";
    }
}

// =========================================================================
// AE2: Compact binary build format — fits in GW2 chat (~130-150 chars)
// =========================================================================
//
// Format (all multi-byte values are little-endian):
//   [0]     uint8   version = 2
//   [1]     uint8   flags: bits 0-2 = game_mode (0-5), bit 3 = has_gear,
//                          bit 4 = has_rune, bit 5 = has_relic
//   [2]     uint8   build_link_len (N)
//   [3..3+N-1]     raw build link bytes (base64 payload of [&...])
//   If has_gear:
//     uint16  gear_mask (bitmask of populated slots, see AE2_SLOT_*)
//     For each set bit in gear_mask (in bit order):
//       uint16  stat_id
//   If has_rune:
//     uint32  rune_id (GW2 item ID)
//   If has_relic:
//     uint32  relic_id (GW2 item ID)
//   For each weapon slot set in gear_mask (bits 6-9):
//     uint32  sigil_id (0 if none)
//     uint16  weapon_type_id (see WeaponTypeId enum)

static const int AE2_SLOT_Helm        = 0;
static const int AE2_SLOT_Shoulders   = 1;
static const int AE2_SLOT_Coat        = 2;
static const int AE2_SLOT_Gloves      = 3;
static const int AE2_SLOT_Leggings    = 4;
static const int AE2_SLOT_Boots       = 5;
static const int AE2_SLOT_WeaponA1    = 6;
static const int AE2_SLOT_WeaponA2    = 7;
static const int AE2_SLOT_WeaponB1    = 8;
static const int AE2_SLOT_WeaponB2    = 9;
static const int AE2_SLOT_Back        = 10;
static const int AE2_SLOT_Accessory1  = 11;
static const int AE2_SLOT_Accessory2  = 12;
static const int AE2_SLOT_Amulet      = 13;
static const int AE2_SLOT_Ring1       = 14;
static const int AE2_SLOT_Ring2       = 15;

static int AE2SlotIndex(const std::string& slot) {
    if (slot == "Helm")        return AE2_SLOT_Helm;
    if (slot == "Shoulders")   return AE2_SLOT_Shoulders;
    if (slot == "Coat")        return AE2_SLOT_Coat;
    if (slot == "Gloves")      return AE2_SLOT_Gloves;
    if (slot == "Leggings")    return AE2_SLOT_Leggings;
    if (slot == "Boots")       return AE2_SLOT_Boots;
    if (slot == "WeaponA1")    return AE2_SLOT_WeaponA1;
    if (slot == "WeaponA2")    return AE2_SLOT_WeaponA2;
    if (slot == "WeaponB1")    return AE2_SLOT_WeaponB1;
    if (slot == "WeaponB2")    return AE2_SLOT_WeaponB2;
    if (slot == "Backpack")    return AE2_SLOT_Back;
    if (slot == "Accessory1")  return AE2_SLOT_Accessory1;
    if (slot == "Accessory2")  return AE2_SLOT_Accessory2;
    if (slot == "Amulet")      return AE2_SLOT_Amulet;
    if (slot == "Ring1")       return AE2_SLOT_Ring1;
    if (slot == "Ring2")       return AE2_SLOT_Ring2;
    return -1;
}

static const char* AE2SlotName(int idx) {
    static const char* names[] = {
        "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
        "Backpack", "Accessory1", "Accessory2", "Amulet", "Ring1", "Ring2"
    };
    if (idx >= 0 && idx < 16) return names[idx];
    return "";
}

static uint16_t WeaponTypeToId(const std::string& wtype) {
    if (wtype == "Axe")        return 5;
    if (wtype == "Longbow")    return 35;
    if (wtype == "Dagger")     return 47;
    if (wtype == "Focus")      return 49;
    if (wtype == "Greatsword") return 50;
    if (wtype == "Hammer")     return 51;
    if (wtype == "Mace")       return 53;
    if (wtype == "Pistol")     return 54;
    if (wtype == "Rifle")      return 85;
    if (wtype == "Scepter")    return 86;
    if (wtype == "Shield")     return 87;
    if (wtype == "Staff")      return 89;
    if (wtype == "Sword")      return 90;
    if (wtype == "Torch")      return 102;
    if (wtype == "Warhorn")    return 103;
    if (wtype == "Shortbow")   return 107;
    if (wtype == "Spear")      return 265;
    return 0;
}

static const char* WeaponTypeFromId(uint16_t id) {
    switch (id) {
        case 5:   return "Axe";
        case 35:  return "Longbow";
        case 47:  return "Dagger";
        case 49:  return "Focus";
        case 50:  return "Greatsword";
        case 51:  return "Hammer";
        case 53:  return "Mace";
        case 54:  return "Pistol";
        case 85:  return "Rifle";
        case 86:  return "Scepter";
        case 87:  return "Shield";
        case 89:  return "Staff";
        case 90:  return "Sword";
        case 102: return "Torch";
        case 103: return "Warhorn";
        case 107: return "Shortbow";
        case 265: return "Spear";
        default:  return "";
    }
}

static void PushU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}

static void PushU32LE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}

static uint16_t ReadU16LE(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static std::string ExportBuildToAE2(const AlterEgo::SavedBuild& build) {
    std::vector<uint8_t> buf;

    // Extract raw build link bytes from chat link
    std::string payload;
    if (!AlterEgo::ChatLink::ExtractPayload(build.chat_link, payload))
        return "";
    auto linkBytes = AlterEgo::ChatLink::Base64Decode(payload);
    if (linkBytes.empty()) return "";

    // Resolve relic ID from persistent name→ID cache
    uint32_t relicId = build.relic_id;
    if (relicId == 0 && !build.relic_name.empty())
        relicId = AlterEgo::GW2API::FindItemIdByName(build.relic_name);

    // Resolve rune/sigil IDs — try stored IDs first, then persistent name→ID cache
    uint32_t runeId = build.rune_id;
    if (runeId == 0 && !build.rune_name.empty())
        runeId = AlterEgo::GW2API::FindItemIdByName(build.rune_name);

    std::map<std::string, uint32_t> resolvedSigilIds;
    std::map<std::string, uint32_t> resolvedSigil2Ids;
    for (const auto& [slot, gs] : build.gear) {
        if (slot.find("Weapon") == 0) {
            uint32_t sid = gs.sigil_id;
            if (sid == 0 && !gs.sigil.empty())
                sid = AlterEgo::GW2API::FindItemIdByName(gs.sigil);
            resolvedSigilIds[slot] = sid;
            uint32_t sid2 = gs.sigil2_id;
            if (sid2 == 0 && !gs.sigil2.empty())
                sid2 = AlterEgo::GW2API::FindItemIdByName(gs.sigil2);
            resolvedSigil2Ids[slot] = sid2;
        }
    }

    // Determine flags
    bool hasGear = !build.gear.empty();
    bool hasRune = (runeId != 0);
    bool hasRelic = (relicId != 0);
    uint8_t gameMode = (uint8_t)build.game_mode;
    if (gameMode > 5) gameMode = 0;

    uint8_t flags = (gameMode & 0x07);
    if (hasGear)  flags |= (1 << 3);
    if (hasRune)  flags |= (1 << 4);
    if (hasRelic) flags |= (1 << 5);

    // Header
    buf.push_back(3); // version (3 = added sigil2 per weapon slot)
    buf.push_back(flags);
    buf.push_back((uint8_t)linkBytes.size());
    buf.insert(buf.end(), linkBytes.begin(), linkBytes.end());

    // Gear
    if (hasGear) {
        uint16_t gearMask = 0;
        // First pass: determine which slots are present
        for (const auto& [slot, gs] : build.gear) {
            int idx = AE2SlotIndex(slot);
            if (idx >= 0 && gs.stat_id != 0) gearMask |= (1 << idx);
        }
        PushU16LE(buf, gearMask);

        // Second pass: write stat IDs in bit order
        for (int i = 0; i < 16; i++) {
            if (!(gearMask & (1 << i))) continue;
            const char* slotName = AE2SlotName(i);
            auto it = build.gear.find(slotName);
            uint16_t statId = (it != build.gear.end()) ? (uint16_t)it->second.stat_id : 0;
            PushU16LE(buf, statId);
        }
    }

    // Rune
    if (hasRune) PushU32LE(buf, runeId);

    // Relic
    if (hasRelic) PushU32LE(buf, relicId);

    // Sigils + weapon types — for each weapon slot present in gear_mask
    if (hasGear) {
        for (int i = AE2_SLOT_WeaponA1; i <= AE2_SLOT_WeaponB2; i++) {
            const char* slotName = AE2SlotName(i);
            auto it = build.gear.find(slotName);
            if (it != build.gear.end() && it->second.stat_id != 0) {
                auto sigilIt = resolvedSigilIds.find(slotName);
                uint32_t sigilId = (sigilIt != resolvedSigilIds.end()) ? sigilIt->second : 0;
                PushU32LE(buf, sigilId);
                PushU16LE(buf, WeaponTypeToId(it->second.weapon_type));
                // Second sigil for two-handed weapons
                auto sigil2It = resolvedSigil2Ids.find(slotName);
                uint32_t sigil2Id = (sigil2It != resolvedSigil2Ids.end()) ? sigil2It->second : 0;
                PushU32LE(buf, sigil2Id);
            }
        }
    }

    std::string b64 = AlterEgo::ChatLink::Base64Encode(buf.data(), buf.size());
    return "AE2:" + b64;
}

static bool ImportBuildFromAE2(const std::string& ae2code, AlterEgo::SavedBuild& out, std::string& error) {
    if (ae2code.size() < 5 || ae2code.size()>4096 || ae2code.substr(0, 4) != "AE2:") {
        error = "Not an AE2 code.";
        return false;
    }

    auto data = AlterEgo::ChatLink::Base64Decode(ae2code.substr(4));
    if (data.size() < 4) { error = "AE2 code too short."; return false; }

    size_t pos = 0;
    uint8_t version = data[pos++];
    if (version != 2 && version != 3) { error = "Unsupported AE2 version."; return false; }

    uint8_t flags = data[pos++];
    uint8_t gameMode = flags & 0x07;
    bool hasGear  = (flags >> 3) & 1;
    bool hasRune  = (flags >> 4) & 1;
    bool hasRelic = (flags >> 5) & 1;

    uint8_t linkLen = data[pos++];
    if (pos + linkLen > data.size()) { error = "AE2 build link truncated."; return false; }

    // Validate the complete AE2 envelope before any metadata lookup or fetch.
    if (gameMode>5 || (flags&0xC0) || linkLen<2) { error="Invalid AE2 flags or build length."; return false; }
    size_t expected=pos+linkLen;
    if(hasGear){
        if(expected+2>data.size()){error="AE2 gear data truncated.";return false;}
        const auto mask=ReadU16LE(&data[expected]);expected+=2;
        for(int i=0;i<16;++i)if(mask&(1u<<i)){
            expected+=2;
            if(i>=AE2_SLOT_WeaponA1&&i<=AE2_SLOT_WeaponB2)expected+=version>=3?10:6;
        }
    }
    if(hasRune)expected+=4;
    if(hasRelic)expected+=4;
    if(expected!=data.size()){error="AE2 payload length does not match its fields.";return false;}

    // Reconstruct chat link from raw bytes
    std::vector<uint8_t> linkBytes(data.begin() + pos, data.begin() + pos + linkLen);
    pos += linkLen;

    std::string b64 = AlterEgo::ChatLink::Base64Encode(linkBytes.data(), linkBytes.size());
    std::string chatLink = "[&" + b64 + "]";

    // Decode build
    AlterEgo::DecodedBuildLink decoded;
    if (!AlterEgo::ChatLink::DecodeBuild(chatLink, decoded)) {
        error = "Failed to decode build from AE2.";
        return false;
    }

    std::string profession = ProfessionFromCode(decoded.profession);
    if (profession == "Unknown") { error = "Unknown profession in AE2."; return false; }

    // Ensure palette data is available
    if (!AlterEgo::GW2API::HasPaletteData(profession)) {
        AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
        error = "Loading " + profession + " skill data... try again in a moment.";
        return false;
    }

    // Use DecodeBuildLink to populate the SavedBuild
    AlterEgo::GameMode mode = (AlterEgo::GameMode)gameMode;
    std::string buildName = profession; // placeholder, caller should set
    if (!DecodeBuildLink(chatLink, buildName, mode, out)) {
        error = "Failed to decode AE2 build data.";
        return false;
    }

    // Preserve the original AE2 code so we can re-resolve missing data later
    // if the GW2 API was unavailable or rate-limited during this import.
    out.ae2_code = ae2code;

    // Parse gear
    if (hasGear) {
        if (pos + 2 > data.size()) { error = "AE2 gear data truncated."; return false; }
        uint16_t gearMask = ReadU16LE(&data[pos]); pos += 2;

        int slotCount = 0;
        for (int i = 0; i < 16; i++) {
            if (gearMask & (1 << i)) slotCount++;
        }
        if (pos + slotCount * 2 > data.size()) { error = "AE2 gear stats truncated."; return false; }

        for (int i = 0; i < 16; i++) {
            if (!(gearMask & (1 << i))) continue;
            uint16_t statId = ReadU16LE(&data[pos]); pos += 2;

            AlterEgo::BuildGearSlot gs;
            gs.slot = AE2SlotName(i);
            gs.stat_id = statId;

            // Resolve stat name from cache
            const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(statId);
            if (statInfo) gs.stat_name = statInfo->name;

            out.gear[gs.slot] = gs;
        }

        // Read rune
        if (hasRune) {
            if (pos + 4 > data.size()) { error = "AE2 rune data truncated."; return false; }
            out.rune_id = ReadU32LE(&data[pos]); pos += 4;
            // Resolve rune name from item cache
            const auto runeInfo = AlterEgo::GW2API::GetItemInfo(out.rune_id);
            if (runeInfo) out.rune_name = runeInfo->name;
        }

        // Read relic
        if (hasRelic) {
            if (pos + 4 > data.size()) { error = "AE2 relic data truncated."; return false; }
            out.relic_id = ReadU32LE(&data[pos]); pos += 4;
            const auto relicInfo = AlterEgo::GW2API::GetItemInfo(out.relic_id);
            if (relicInfo) out.relic_name = relicInfo->name;
        }

        // Read sigils + weapon types for weapon slots
        for (int i = AE2_SLOT_WeaponA1; i <= AE2_SLOT_WeaponB2; i++) {
            if (!(gearMask & (1 << i))) continue;
            size_t needed = (version >= 3) ? 10 : 6; // v3: +4 for sigil2
            if (pos + needed > data.size()) break;
            uint32_t sigilId = ReadU32LE(&data[pos]); pos += 4;
            uint16_t weaponTypeId = ReadU16LE(&data[pos]); pos += 2;
            uint32_t sigil2Id = 0;
            if (version >= 3) { sigil2Id = ReadU32LE(&data[pos]); pos += 4; }
            const char* slotName = AE2SlotName(i);
            auto it = out.gear.find(slotName);
            if (it != out.gear.end()) {
                it->second.sigil_id = sigilId;
                it->second.sigil2_id = sigil2Id;
                const char* wt = WeaponTypeFromId(weaponTypeId);
                if (wt[0]) it->second.weapon_type = wt;
            }
        }

        // Fetch item details for rune/relic/sigils synchronously so names are available
        std::vector<uint32_t> fetchIds;
        if (out.rune_id != 0) fetchIds.push_back(out.rune_id);
        if (out.relic_id != 0) fetchIds.push_back(out.relic_id);
        for (const auto& [slot, gs] : out.gear) {
            if (gs.sigil_id != 0) fetchIds.push_back(gs.sigil_id);
            if (gs.sigil2_id != 0) fetchIds.push_back(gs.sigil2_id);
        }
        if (!fetchIds.empty()) AlterEgo::GW2API::FetchItemDetails(fetchIds);

        // Now resolve names from the cache (after sync fetch) and populate name→ID cache
        if (out.rune_id != 0 && out.rune_name.empty()) {
            const auto runeInfo = AlterEgo::GW2API::GetItemInfo(out.rune_id);
            if (runeInfo) out.rune_name = runeInfo->name;
        }
        if (out.rune_id != 0 && !out.rune_name.empty())
            AlterEgo::GW2API::CacheItemNameId(out.rune_name, out.rune_id);
        if (out.relic_id != 0 && out.relic_name.empty()) {
            const auto relicInfo = AlterEgo::GW2API::GetItemInfo(out.relic_id);
            if (relicInfo) out.relic_name = relicInfo->name;
        }
        if (out.relic_id != 0 && !out.relic_name.empty())
            AlterEgo::GW2API::CacheItemNameId(out.relic_name, out.relic_id);
        for (auto& [slot, gs] : out.gear) {
            if (gs.sigil_id != 0 && gs.sigil.empty()) {
                const auto sigilInfo = AlterEgo::GW2API::GetItemInfo(gs.sigil_id);
                if (sigilInfo) gs.sigil = sigilInfo->name;
            }
            if (gs.sigil_id != 0 && !gs.sigil.empty())
                AlterEgo::GW2API::CacheItemNameId(gs.sigil, gs.sigil_id);
            if (gs.sigil2_id != 0 && gs.sigil2.empty()) {
                const auto sigil2Info = AlterEgo::GW2API::GetItemInfo(gs.sigil2_id);
                if (sigil2Info) gs.sigil2 = sigil2Info->name;
            }
            if (gs.sigil2_id != 0 && !gs.sigil2.empty())
                AlterEgo::GW2API::CacheItemNameId(gs.sigil2, gs.sigil2_id);
        }
        // Set rune name on armor gear slots
        if (!out.rune_name.empty()) {
            const char* armorSlots[] = {"Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
            for (const char* s : armorSlots) {
                auto ait = out.gear.find(s);
                if (ait != out.gear.end()) {
                    ait->second.rune = out.rune_name;
                    ait->second.rune_id = out.rune_id;
                }
            }
        }
    } else {
        // No gear — still parse rune/relic if flags set
        if (hasRune && pos + 4 <= data.size()) {
            out.rune_id = ReadU32LE(&data[pos]); pos += 4;
        }
        if (hasRelic && pos + 4 <= data.size()) {
            out.relic_id = ReadU32LE(&data[pos]); pos += 4;
        }
    }

    return true;
}

static AlterEgo::GameMode GameModeFromLabel(const std::string& label) {
    if (label == "PvE")     return AlterEgo::GameMode::PvE;
    if (label == "WvW")     return AlterEgo::GameMode::WvW;
    if (label == "PvP")     return AlterEgo::GameMode::PvP;
    if (label == "Raid")    return AlterEgo::GameMode::Raid;
    if (label == "Fractal") return AlterEgo::GameMode::Fractal;
    return AlterEgo::GameMode::Other;
}

// ---- Mobile relay import ----

static bool IsRelayCode(const std::string& s) {
    return AlterEgo::BuildRelay::AcceptsCode(s);
}

// Cache the last relay response so a retry (e.g. waiting for palette data) doesn't
// consume another single-use code.
static std::string g_RelayCache_Code;
static std::string g_RelayCache_Body;

static bool ImportBuildFromRelay(const std::string& code, AlterEgo::SavedBuild& out, std::string& error) {
    if constexpr (!AlterEgo::BuildRelay::Enabled) {
        error = "Not a recognized build or wardrobe format.";
        return false;
    }
    // Defense in depth: callers validate via IsRelayCode, but reject anything
    // unexpected here too before splicing it into the URL.
    if (!IsRelayCode(code)) {
        error = "Invalid code format.";
        return false;
    }

    std::string body;

    // Use cached response if this is a retry for the same code
    if (code == g_RelayCache_Code && !g_RelayCache_Body.empty()) {
        body = g_RelayCache_Body;
    } else {
        std::string url = "https://pie.rocks.cc/relay.php?code=" + code;
        std::string headers = "X-App-Secret: ae-relay-v1\r\n";
        auto resp = Skinventory::HttpClient::GetEx(url, headers);

        if (resp.status_code == 0) {
            error = "Network error — could not reach relay server.";
            return false;
        }
        if (resp.status_code == 400) { error = "Invalid code format."; return false; }
        if (resp.status_code == 403) { error = "Relay server rejected the request."; return false; }
        if (resp.status_code == 404) { error = "Code not found or expired."; return false; }
        if (resp.status_code != 200) {
            error = "Relay error (HTTP " + std::to_string(resp.status_code) + ")";
            return false;
        }

        body = resp.body;
        // Cache for potential retry
        g_RelayCache_Code = code;
        g_RelayCache_Body = body;
    }

    try {
        auto j = AlterEgo::Untrusted::Parse(body,AlterEgo::Untrusted::Library);

        std::string name = j.value("name", "Mobile Build");
        std::string profession = j.value("profession", "");
        std::string gameMode = j.value("gameMode", "PvE");
        std::string chatLink = j.value("chatLink", "");

        if (chatLink.empty()) {
            error = "Relay response missing chat link.";
            return false;
        }

        // Need palette data to resolve skills — kick off async fetch and ask user to retry
        if (!profession.empty() && !AlterEgo::GW2API::HasPaletteData(profession)) {
            AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
            error = "Loading " + profession + " skill data... click Import again in a moment.";
            return false;
        }

        AlterEgo::GameMode mode = GameModeFromLabel(gameMode);
        if (!DecodeBuildLink(chatLink, name, mode, out)) {
            error = "Failed to decode build from relay.";
            return false;
        }

        // Parse gear from gearJson (JSON string within the response)
        std::string gearJsonStr = j.value("gearJson", "");
        if (!gearJsonStr.empty()) {
            try {
                auto gj = nlohmann::json::parse(gearJsonStr);

                if (gj.contains("slots") && gj["slots"].is_object()) {
                    for (auto& [slot, slotData] : gj["slots"].items()) {
                        AlterEgo::BuildGearSlot gs;
                        gs.slot = slot;
                        gs.stat_id = slotData.value("statId", 0u);
                        gs.stat_name = slotData.value("statName", "");
                        gs.weapon_type = slotData.value("weaponType", "");
                        gs.sigil_id = slotData.value("sigilId", 0u);
                        gs.sigil2_id = slotData.value("sigil2Id", 0u);
                        gs.rune_id = slotData.value("runeId", 0u);
                        out.gear[slot] = gs;
                    }
                }

                // Top-level rune/relic
                out.rune_name = gj.value("rune", "");
                out.rune_id = gj.value("runeId", 0u);
                out.relic_name = gj.value("relic", "");
                out.relic_id = gj.value("relicId", 0u);

                // Fetch item details for rune/relic/sigils so names resolve
                std::vector<uint32_t> fetchIds;
                if (out.rune_id != 0) fetchIds.push_back(out.rune_id);
                if (out.relic_id != 0) fetchIds.push_back(out.relic_id);
                for (const auto& [slot, gs] : out.gear) {
                    if (gs.sigil_id != 0) fetchIds.push_back(gs.sigil_id);
                    if (gs.sigil2_id != 0) fetchIds.push_back(gs.sigil2_id);
                }
                if (!fetchIds.empty()) AlterEgo::GW2API::FetchItemDetails(fetchIds);

                // Resolve names and populate name→ID cache
                if (out.rune_id != 0 && out.rune_name.empty()) {
                    const auto info = AlterEgo::GW2API::GetItemInfo(out.rune_id);
                    if (info) out.rune_name = info->name;
                }
                if (out.rune_id != 0 && !out.rune_name.empty())
                    AlterEgo::GW2API::CacheItemNameId(out.rune_name, out.rune_id);
                if (out.relic_id != 0 && out.relic_name.empty()) {
                    const auto info = AlterEgo::GW2API::GetItemInfo(out.relic_id);
                    if (info) out.relic_name = info->name;
                }
                if (out.relic_id != 0 && !out.relic_name.empty())
                    AlterEgo::GW2API::CacheItemNameId(out.relic_name, out.relic_id);
                for (auto& [slot, gs] : out.gear) {
                    if (gs.sigil_id != 0 && gs.sigil.empty()) {
                        const auto info = AlterEgo::GW2API::GetItemInfo(gs.sigil_id);
                        if (info) gs.sigil = info->name;
                    }
                    if (gs.sigil_id != 0 && !gs.sigil.empty())
                        AlterEgo::GW2API::CacheItemNameId(gs.sigil, gs.sigil_id);
                    if (gs.sigil2_id != 0 && gs.sigil2.empty()) {
                        const auto info = AlterEgo::GW2API::GetItemInfo(gs.sigil2_id);
                        if (info) gs.sigil2 = info->name;
                    }
                    if (gs.sigil2_id != 0 && !gs.sigil2.empty())
                        AlterEgo::GW2API::CacheItemNameId(gs.sigil2, gs.sigil2_id);
                }

                // Set rune name on armor gear slots
                if (!out.rune_name.empty()) {
                    const char* armorSlots[] = {"Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
                    for (const char* s : armorSlots) {
                        auto ait = out.gear.find(s);
                        if (ait != out.gear.end()) {
                            ait->second.rune = out.rune_name;
                            ait->second.rune_id = out.rune_id;
                        }
                    }
                }
            } catch (...) {
                // Gear parsing failed — build still usable without gear
            }
        }

        // Clear cache on successful import
        g_RelayCache_Code.clear();
        g_RelayCache_Body.clear();

        return true;
    } catch (const std::exception& e) {
        error = std::string("Failed to parse relay response: ") + e.what();
        return false;
    }
}

// Send a saved build to the relay server, returning the 4-char code or an error.
static void SendBuildToRelay(const AlterEgo::SavedBuild& build) {
    if constexpr (!AlterEgo::BuildRelay::Enabled) return;
    g_RelaySending = true;
    g_RelayResultCode.clear();
    g_RelayResultError.clear();

    AlterEgo::Background::Workers().Launch([build]() {
        // Build the buildJson string (matches mobile app schema)
        nlohmann::json bj;
        {
            nlohmann::json specs = nlohmann::json::array();
            for (int i = 0; i < 3; i++) {
                if (build.specializations[i].spec_id == 0) continue;
                nlohmann::json s;
                s["specId"] = build.specializations[i].spec_id;
                nlohmann::json traits = nlohmann::json::array();
                for (int t = 0; t < 3; t++)
                    traits.push_back(build.specializations[i].traits[t]);
                s["selectedTraitIds"] = traits;
                specs.push_back(s);
            }
            bj["specializations"] = specs;

            // Skills: reverse-resolve to palette IDs for mobile app compatibility
            auto toPalette = [&](uint32_t skill_id) -> int {
                if (skill_id == 0) return 0;
                uint16_t p = AlterEgo::GW2API::GetPaletteIdFromSkill(build.profession, skill_id);
                return (p != 0) ? (int)p : (int)skill_id;
            };
            bj["healSkillId"] = toPalette(build.terrestrial_skills.heal);
            nlohmann::json utils = nlohmann::json::array();
            for (int i = 0; i < 3; i++)
                utils.push_back(toPalette(build.terrestrial_skills.utilities[i]));
            bj["utilitySkillIds"] = utils;
            bj["eliteSkillId"] = toPalette(build.terrestrial_skills.elite);
        }

        // Build the gearJson string (matches mobile app schema)
        nlohmann::json gj;
        {
            nlohmann::json slots = nlohmann::json::object();
            for (const auto& [slot, gs] : build.gear) {
                nlohmann::json s;
                if (gs.stat_id != 0) s["statId"] = gs.stat_id;
                if (!gs.stat_name.empty()) s["statName"] = gs.stat_name;
                if (!gs.weapon_type.empty()) s["weaponType"] = gs.weapon_type;
                if (gs.sigil_id != 0) s["sigilId"] = gs.sigil_id;
                if (gs.sigil2_id != 0) s["sigil2Id"] = gs.sigil2_id;
                if (gs.rune_id != 0) s["runeId"] = gs.rune_id;
                if (!s.empty()) slots[slot] = s;
            }
            gj["slots"] = slots;
            if (!build.rune_name.empty()) gj["rune"] = build.rune_name;
            if (build.rune_id != 0) gj["runeId"] = build.rune_id;
            if (!build.relic_name.empty()) gj["relic"] = build.relic_name;
            if (build.relic_id != 0) gj["relicId"] = build.relic_id;
        }

        // Assemble relay payload
        nlohmann::json payload;
        payload["name"] = build.name;
        payload["profession"] = build.profession;
        payload["gameMode"] = GameModeLabel(build.game_mode);
        payload["chatLink"] = build.chat_link;
        payload["buildJson"] = bj.dump();
        payload["gearJson"] = gj.dump();

        std::string url = "https://pie.rocks.cc/relay.php";
        std::string headers = "X-App-Secret: ae-relay-v1\r\n";
        auto resp = Skinventory::HttpClient::PostJson(url, payload.dump(), headers);

        if (resp.status_code == 200) {
            try {
                auto r = AlterEgo::Untrusted::Parse(resp.body,AlterEgo::Untrusted::Clipboard);
                std::string code = r.value("code", "");
                if (!code.empty()) {
                    g_RelayResultCode = code;
                } else {
                    g_RelayResultError = "Server returned no code.";
                }
            } catch (...) {
                g_RelayResultError = "Failed to parse server response.";
            }
        } else if (resp.status_code == 0) {
            g_RelayResultError = "Network error — could not reach relay server.";
        } else if (resp.status_code == 429) {
            g_RelayResultError = "Rate limited — try again in a minute.";
        } else if (resp.status_code == 413) {
            g_RelayResultError = "Build data too large to send.";
        } else {
            g_RelayResultError = "Relay error (HTTP " + std::to_string(resp.status_code) + ")";
        }
        g_RelaySending = false;
    }, [] { g_RelayResultError = "Relay request interrupted."; g_RelaySending = false; });
}

// Both saved-library builds and retrieved character snapshots share the same
// export actions. The caller owns the popup and supplies the exact build data.
static void RenderBuildShareMenuItems(const AlterEgo::SavedBuild& build,
    const AlterEgo::Character* liveCharacter) {
                if (AlterEgo::UI::MenuAction("Copy build-only chat link",AlterEgo::UI::MenuIcon::Copy,!build.chat_link.empty())) {
                    CopyToClipboard(build.chat_link);
                    if (APIDefs) APIDefs->GUI_SendAlert("Build link copied to clipboard!");
                }
                if (AlterEgo::UI::MenuAction("Copy entire build + equipment code",AlterEgo::UI::MenuIcon::Copy,!build.chat_link.empty())) {
                    std::string ae2 = ExportBuildToAE2(build);
                    if (!ae2.empty()) {
                        CopyToClipboard(ae2);
                        char info[196];
                        snprintf(info, sizeof(info), "Build code copied to clipboard (%d chars). Paste in GW2 chat to share!", (int)ae2.size());
                        if (APIDefs) APIDefs->GUI_SendAlert(info);
                    } else {
                        if (APIDefs) APIDefs->GUI_SendAlert("Failed to generate build code. Build may be missing a chat link.");
                    }
                }
                if (liveCharacter) {
                    if (ImGui::Selectable("Copy active fashion chat link")) {
                        // "Active Fashion" is always the active in-game tab,
                        // independent of the equipment tab being viewed.
                        const int tab = liveCharacter->active_equipment_tab > 0
                            ? liveCharacter->active_equipment_tab : 1;
                        const std::string link = BuildWardrobeLink(*liveCharacter, tab);
                        if (!link.empty()) {
                            CopyToClipboard(link);
                            if (APIDefs) APIDefs->GUI_SendAlert("Active fashion chat link copied.");
                        } else if (APIDefs) APIDefs->GUI_SendAlert("Fashion link unavailable; refresh character data.");
                    }
                }
                if constexpr (AlterEgo::BuildRelay::Enabled) {
                    if (!g_RelaySending && AlterEgo::UI::MenuAction("Share complete build via one-time code",AlterEgo::UI::MenuIcon::Share,!build.chat_link.empty())) {
                        if (build.chat_link.empty()) {
                            if (APIDefs) APIDefs->GUI_SendAlert("Cannot send: build has no chat link.");
                        } else {
                            SendBuildToRelay(build);
                        }
                    }
                    if (g_RelaySending) {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Sending...");
                    }
                }
 }

// Returns true if input is a shared build (AE2, chat link, or relay code)
// On success, populates 'out' with the imported build
static bool ImportSharedBuild(const std::string& input, AlterEgo::SavedBuild& out, std::string& error) {
    // AE2 compact binary format
    if (input.size() > 4 && input.substr(0, 4) == "AE2:") {
        return ImportBuildFromAE2(input, out, error);
    }
    // 4-character alphanumeric code — mobile relay import
    if (AlterEgo::BuildRelay::HasCodeShape(input)) {
        if (IsRelayCode(input)) return ImportBuildFromRelay(input, out, error);
        error = "Not a recognized build or wardrobe format.";
        return false;
    }
    // Chat link
    if (input.size() > 4 && input[0] == '[' && input[1] == '&') {
        std::string name = "Imported Build";
        AlterEgo::GameMode mode = AlterEgo::GameMode::PvE;
        // Detect profession for palette data
        AlterEgo::DecodedBuildLink peek;
        if (AlterEgo::ChatLink::DecodeBuild(input, peek)) {
            std::string prof = ProfessionFromCode(peek.profession);
            if (!prof.empty() && prof != "Unknown" && !AlterEgo::GW2API::HasPaletteData(prof)) {
                AlterEgo::GW2API::FetchProfessionPaletteAsync(prof);
                error = "Loading " + prof + " skill data... try again in a moment.";
                return false;
            }
        }
        if (!DecodeBuildLink(input, name, mode, out)) {
            error = "Failed to decode build chat link.";
            return false;
        }
        return true;
    }
    return false; // Not a recognized format
}

// Decode a build chat link and create a SavedBuild
static bool DecodeBuildLink(const std::string& link, const std::string& name,
                            AlterEgo::GameMode mode, AlterEgo::SavedBuild& out) {
    AlterEgo::DecodedBuildLink decoded;
    if (!AlterEgo::ChatLink::DecodeBuild(link, decoded)) return false;

    std::string profession = ProfessionFromCode(decoded.profession);
    if (profession == "Unknown") return false;

    out.name = name;
    out.chat_link = link;
    out.profession = profession;
    out.game_mode = mode;

    // Copy specializations — trait choices stored as indices (1=top, 2=mid, 3=bot)
    // Need to resolve to actual trait IDs using spec info
    for (int i = 0; i < 3; i++) {
        out.specializations[i].spec_id = decoded.specs[i].spec_id;
        // Store trait choices temporarily as indices; resolve to trait IDs below
        out.specializations[i].traits[0] = 0;
        out.specializations[i].traits[1] = 0;
        out.specializations[i].traits[2] = 0;
    }

    // Resolve trait choices to trait IDs using spec cache
    for (int i = 0; i < 3; i++) {
        if (decoded.specs[i].spec_id == 0) continue;
        const auto specInfo = AlterEgo::GW2API::GetSpecInfo(decoded.specs[i].spec_id);
        if (specInfo && specInfo->major_traits.size() >= 9) {
            for (int tier = 0; tier < 3; tier++) {
                uint8_t choice = decoded.specs[i].traits[tier]; // 1=top, 2=mid, 3=bot
                if (choice >= 1 && choice <= 3) {
                    int majorIdx = tier * 3 + (choice - 1);
                    out.specializations[i].traits[tier] = (int)specInfo->major_traits[majorIdx];
                }
            }
        } else {
            // Spec info not loaded yet — store choice indices negated as placeholder
            for (int tier = 0; tier < 3; tier++) {
                out.specializations[i].traits[tier] = -(int)decoded.specs[i].traits[tier];
            }
        }
    }

    // Preserve the exact original Revenant palette per physical slot.
    // The catalog may not yet resolve a contextual alias; never substitute
    // a skill from an unrelated legend just because its palette matches.
    out.rev_active_palettes=AlterEgo::RecordRevenantActivePalettes(decoded,
        [](uint16_t palette,uint32_t legend,int env,int slot){
            return AlterEgo::GW2API::ResolveRevenantPalette(palette,legend,env,slot);
        });
    auto resolveSkill = [&](uint16_t palette,int env,int slot) -> uint32_t {
        if(!palette)return 0;
        if(profession=="Revenant")
            return out.rev_active_palettes?(*out.rev_active_palettes)[env*5+slot][1]:0;
        return AlterEgo::GW2API::GetSkillIdFromPalette(profession,palette);
    };
    auto bar=[&](AlterEgo::SkillBar& target,const uint16_t (&palettes)[5],int env){
        target.heal=resolveSkill(palettes[0],env,0);
        for(int i=0;i<3;++i)target.utilities[i]=resolveSkill(palettes[i+1],env,i+1);
        target.elite=resolveSkill(palettes[4],env,4);
    };
    bar(out.terrestrial_skills,decoded.terrestrial_skills,0);
    bar(out.aquatic_skills,decoded.aquatic_skills,1);

    // Pets (Ranger)
    out.pets.terrestrial[0] = decoded.pets[0];
    out.pets.terrestrial[1] = decoded.pets[1];
    out.pets.aquatic[0] = decoded.pets[2];
    out.pets.aquatic[1] = decoded.pets[3];

    // Legends (Revenant)
    for (int i = 0; i < 4; i++)
        out.legend_codes[i] = decoded.legends[i];
    out.pets_recorded=decoded.profession==AlterEgo::PROF_RANGER&&decoded.companion_choices_known;
    out.legends_recorded=decoded.profession==AlterEgo::PROF_REVENANT&&decoded.companion_choices_known;
    out.inactive_rev_skills=AlterEgo::RecordedInactiveLegendPalettes(decoded);

    // Weapons
    out.weapons.clear();
    for (auto w : decoded.weapons)
        out.weapons.push_back((uint32_t)w);

    return true;
}

// Saved previews and selected Compare inputs share the public reference owners.
// Demand aquatic IDs first so a duplicate terrestrial ID cannot mask incomplete
// environment evidence. Neither raw palette values nor unknown slots are IDs.
template<class Build>
static void DemandBuildReferenceMetadata(const Build& build){
    using namespace AlterEgo;
    GW2API::FetchSpecDetailsAsync({build.specializations[0].spec_id,build.specializations[1].spec_id,build.specializations[2].spec_id});
    DemandSkillBarMetadata(SkillEdit::DisplayBar(build.aquatic_skills),true);
    DemandSkillBarMetadata(SkillEdit::DisplayBar(build.terrestrial_skills),false);
    GW2API::FetchProfessionInfoAsync(build.profession);
}
static void FetchDetailsForSavedBuild(const AlterEgo::SavedBuild& build){
    DemandBuildReferenceMetadata(build);
}
static void DemandSavedGearMetadata(const AlterEgo::SavedBuild& build){
    std::vector<uint32_t> ids{build.rune_id,build.relic_id};
    for(const auto& inf:build.infusion_totals)if(inf.item_id)ids.push_back(inf.item_id);
    for(const auto& entry:build.gear){
        const auto& gear=entry.second;
        for(auto id:{gear.item_id,gear.rune_id,gear.sigil_id,gear.sigil2_id})if(id)ids.push_back(id);
        for(auto id:gear.infusion_ids)if(id)ids.push_back(id);
    }
    AlterEgo::GW2API::FetchItemDetailsAsync(ids);
}

// =========================================================================
// Chat Build Detection — event handler + toast notification
// =========================================================================

static const char* ChatChannelName(ChatMessageType type) {
    switch (type) {
        case ChatMsg_Party:         return "Party";
        case ChatMsg_Squad:         return "Squad";
        case ChatMsg_Whisper:       return "Whisper";
        case ChatMsg_Local:         return "Local";
        case ChatMsg_Map:           return "Map";
        case ChatMsg_Guild:         return "Guild";
        case ChatMsg_TeamPvP:       return "PvP";
        case ChatMsg_TeamWvW:       return "WvW";
        default:                    return "Chat";
    }
}

static void OnEvChatMessage(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (!eventArgs) return;
    g_ChatAddonConnected = true;
    if (!g_ChatDetectionEnabled.load()) return;

    EvChatMessage* msg = (EvChatMessage*)eventArgs;

    // Only process message types that have ChatGenericMessage with Content
    if (msg->Type != ChatMsg_Party && msg->Type != ChatMsg_Squad &&
        msg->Type != ChatMsg_Whisper && msg->Type != ChatMsg_Local &&
        msg->Type != ChatMsg_Map && msg->Type != ChatMsg_Guild &&
        msg->Type != ChatMsg_TeamPvP && msg->Type != ChatMsg_TeamWvW)
        return;

    // Skip our own outgoing whispers
    if (msg->Type == ChatMsg_Whisper && (msg->Flags & ChatFlag_Whisper_IsFromMe))
        return;

    // All GenericMessage types share the same union offset
    ChatGenericMessage* gm = &msg->Whisper;
    if (!gm->Content) return;

    // The host owns eventArgs and all pointers inside it; do not retain them.
    // Chat is short; bound copying before allocating or parsing.
    try {
        auto copy=[](const char* p,size_t limit)->std::string{
            if(!p)return {};
            size_t n=0;while(n<limit && p[n])++n;
            if(n==limit)return {};
            return std::string(p,n);
        };
        auto content=copy(gm->Content,16384);
        if(content.find("AE2:")==std::string::npos&&content.find("[&")==std::string::npos)return;
        auto sender=gm->CharacterName?copy(gm->CharacterName,256):std::string("Unknown");
        g_ChatBuildInbox.Push({msg->Type,std::move(content),std::move(sender)});
    }catch(...){ /* Never unwind into the event dispatcher. */ }
}

static void HandleChatBuildInput(const ChatBuildInput& input) {
    if(!g_ChatBuildDetection)return;
    const auto& content=input.content;
    const auto& sender=input.sender;
    // Skip our own messages (no toast for builds we posted ourselves)
    if (!g_CurrentCharName.empty() && sender == g_CurrentCharName)
        return;

    // --- Priority 1: Detect AE2: compact build codes (includes gear) ---
    {
        size_t ae2pos = content.find("AE2:");
        if (ae2pos != std::string::npos) {
            // Extract the AE2 code (runs until whitespace or end of string)
            size_t ae2end = content.find_first_of(" \t\n\r", ae2pos);
            std::string ae2code = (ae2end != std::string::npos)
                ? content.substr(ae2pos, ae2end - ae2pos)
                : content.substr(ae2pos);

            // Quick-decode to extract profession info for the toast
            auto ae2data = AlterEgo::ChatLink::Base64Decode(ae2code.substr(4));
            if (ae2data.size() >= 4 && (ae2data[0] == 2 || ae2data[0] == 3)) {
                uint8_t ae2flags = ae2data[1];
                bool ae2hasGear = (ae2flags >> 3) & 1;
                uint8_t linkLen = ae2data[2];
                if (3 + linkLen <= ae2data.size()) {
                    std::vector<uint8_t> linkBytes(ae2data.begin() + 3, ae2data.begin() + 3 + linkLen);
                    std::string b64 = AlterEgo::ChatLink::Base64Encode(linkBytes.data(), linkBytes.size());
                    std::string chatLink = "[&" + b64 + "]";

                    AlterEgo::DecodedBuildLink decoded;
                    if (AlterEgo::ChatLink::DecodeBuild(chatLink, decoded)) {
                        std::string profession = ProfessionFromCode(decoded.profession);
                        if (profession != "Unknown") {
                            std::string specName;
                            for (int i = 2; i >= 0; i--) {
                                if (decoded.specs[i].spec_id != 0) {
                                    const auto si = AlterEgo::GW2API::GetSpecInfo(decoded.specs[i].spec_id);
                                    if (si && si->elite) specName = si->name;
                                    break;
                                }
                            }

                            // Suppress the auto-toast when Pie UI is present — it renders
                            // chat build codes as clickable links and imports explicitly.
                            if (!g_PieUIPresent.load()) {
                                g_BuildToast.active = true;
                                g_BuildToast.sender = sender;
                                g_BuildToast.chat_link = chatLink;
                                g_BuildToast.ae2_code = ae2code;
                                g_BuildToast.profession = profession;
                                g_BuildToast.spec_name = specName;
                                g_BuildToast.channel = ChatChannelName(input.type);
                                g_BuildToast.has_gear = ae2hasGear;
                                g_ToastNeedsFocus = true;
                            }

                            // Pre-fetch palette data so import is instant
                            if (!AlterEgo::GW2API::HasPaletteData(profession)) {
                                AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
                            }

                            if (APIDefs) {
                                std::string log = "AE2 build detected in " + std::string(ChatChannelName(input.type))
                                    + " from " + sender + ": " + profession;
                                if (!specName.empty()) log += " (" + specName + ")";
                                if (ae2hasGear) log += " [with gear]";
                                APIDefs->Log(LOGL_DEBUG, "AlterEgo", log.c_str());
                            }
                            return; // AE2 found, done
                        }
                    }
                }
            }
        }
    }

    // --- Priority 2: Detect native GW2 build chat links [&...] ---
    {
        size_t pos = 0;
        while ((pos = content.find("[&", pos)) != std::string::npos) {
            size_t end = content.find(']', pos);
            if (end == std::string::npos) break;

            std::string link = content.substr(pos, end - pos + 1);
            // GW2 internal chat format escapes ] as \] — strip the backslash
            size_t bs = link.size() - 2; // position before ]
            if (bs > 0 && link[bs] == '\\') link.erase(bs, 1);
            pos = end + 1;

            auto linkType = AlterEgo::ChatLink::DetectType(link);
            if (linkType != AlterEgo::LINK_BUILD) continue;

            AlterEgo::DecodedBuildLink decoded;
            if (!AlterEgo::ChatLink::DecodeBuild(link, decoded)) continue;

            std::string profession = ProfessionFromCode(decoded.profession);
            if (profession == "Unknown") continue;

            std::string specName;
            for (int i = 2; i >= 0; i--) {
                if (decoded.specs[i].spec_id != 0) {
                    const auto si = AlterEgo::GW2API::GetSpecInfo(decoded.specs[i].spec_id);
                    if (si && si->elite) specName = si->name;
                    break;
                }
            }

            // Suppress the auto-toast when Pie UI is present (see AE2 branch above).
            if (!g_PieUIPresent.load()) {
                g_BuildToast.active = true;
                g_BuildToast.sender = sender;
                g_BuildToast.chat_link = link;
                g_BuildToast.ae2_code.clear();
                g_BuildToast.profession = profession;
                g_BuildToast.spec_name = specName;
                g_BuildToast.channel = ChatChannelName(input.type);
                g_BuildToast.has_gear = false;
                g_ToastNeedsFocus = true;
            }

            // Pre-fetch palette data so import is instant
            if (!AlterEgo::GW2API::HasPaletteData(profession)) {
                AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
            }

            if (APIDefs) {
                std::string log = "Build link detected in " + std::string(ChatChannelName(input.type))
                                + " from " + sender + ": " + profession;
                if (!specName.empty()) log += " (" + specName + ")";
                APIDefs->Log(LOGL_DEBUG, "AlterEgo", log.c_str());
            }

            break;
        }
    }
}

static void RenderBuildToast() {
    if (!g_BuildToast.active) return;

    ThemeGuard themeGuard;

    const float toastWidth = 380.0f;
    const float toastMinHeight = 140.0f;

    // Position: use saved position, or auto-center horizontally
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float posX = (g_ToastPosX < 0) ? (displaySize.x - toastWidth) * 0.5f : g_ToastPosX;
    float posY = g_ToastPosY;

    if (!g_ToastPosInitialized) {
        ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
        g_ToastPosInitialized = true;
    } else {
        ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSize(ImVec2(toastWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (g_ToastNeedsFocus) {
        ImGui::SetNextWindowFocus();
        g_ToastNeedsFocus = false;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings;

    // Gold highlight border for attention
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.90f, 0.75f, 0.25f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));

    bool open = true;
    if (ImGui::Begin("Build Detected##AE_Toast", &open, flags)) {
        // Track position for drag-to-reposition
        ImVec2 wp = ImGui::GetWindowPos();
        if (wp.x != posX || wp.y != posY) {
            g_ToastPosX = wp.x;
            g_ToastPosY = wp.y;
        }

        // Header — gold accent bar
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        dl->AddRectFilled(
            ImVec2(winPos.x + 1, winPos.y + 1),
            ImVec2(winPos.x + winSize.x - 1, winPos.y + 5),
            IM_COL32(230, 190, 65, 220));

        ImGui::Spacing();

        // Title line
        ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.25f, 1.0f), "BUILD SHARED");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.47f, 0.40f, 1.0f), "via %s", g_BuildToast.channel.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Sender + profession info (no sender on an explicit Pie UI hand-off)
        if (!g_BuildToast.sender.empty()) {
            ImGui::Text("From:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", g_BuildToast.sender.c_str());
        }

        ImGui::Text("Build:");
        ImGui::SameLine();
        if (!g_BuildToast.spec_name.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.87f, 0.78f, 1.0f), "%s (%s)",
                g_BuildToast.spec_name.c_str(), g_BuildToast.profession.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.87f, 0.78f, 1.0f), "%s",
                g_BuildToast.profession.c_str());
        }

        if (g_BuildToast.has_gear) {
            ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.35f, 1.0f), "Includes gear, runes & sigils");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.47f, 0.40f, 1.0f), "Traits & skills only");
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Action buttons — large and prominent
        float buttonWidth = (toastWidth - 28 - 8) * 0.5f; // half width minus padding and gap
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.18f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.60f, 0.25f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.70f, 0.30f, 1.00f));
        if (RenderActionButton("Import to Library", ImVec2(buttonWidth, 32))) {
            std::string buildName = g_BuildToast.spec_name.empty()
                ? g_BuildToast.profession
                : g_BuildToast.spec_name;
            if (!g_BuildToast.sender.empty())
                buildName += " (" + g_BuildToast.sender + ")";

            AlterEgo::SavedBuild sb;
            bool imported = false;

            if (!g_BuildToast.ae2_code.empty()) {
                // AE2 import — includes gear data
                std::string ae2err;
                if (ImportBuildFromAE2(g_BuildToast.ae2_code, sb, ae2err)) {
                    sb.name = buildName;
                    AlterEgo::GW2API::AddSavedBuild(std::move(sb));
                    imported = true;
                    if (APIDefs) APIDefs->GUI_SendAlert("Full build imported from chat!");
                } else if (ae2err.find("Loading") != std::string::npos) {
                    // Palette data still loading — don't dismiss, let user retry
                    if (APIDefs) APIDefs->GUI_SendAlert("Loading skill data... click Import again.");
                } else {
                    if (APIDefs) {
                        std::string alertMsg = "AE2 import failed: " + ae2err;
                        APIDefs->GUI_SendAlert(alertMsg.c_str());
                    }
                }
            } else {
                // Plain chat link — traits & skills only
                std::string profession = g_BuildToast.profession;
                if (!profession.empty() && !AlterEgo::GW2API::HasPaletteData(profession)) {
                    AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
                    if (APIDefs) APIDefs->GUI_SendAlert("Loading skill data... click Import again.");
                } else if (DecodeBuildLink(g_BuildToast.chat_link, buildName, AlterEgo::GameMode::PvE, sb)) {
                    AlterEgo::GW2API::AddSavedBuild(std::move(sb));
                    imported = true;
                    if (APIDefs) APIDefs->GUI_SendAlert("Build imported from chat!");
                } else {
                    if (APIDefs) APIDefs->GUI_SendAlert("Failed to import build. Try again.");
                }
            }

            if (imported) g_BuildToast.active = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.20f, 0.12f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.30f, 0.14f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.38f, 0.16f, 1.00f));
        if (RenderActionButton("Dismiss", ImVec2(buttonWidth, 32))) {
            g_BuildToast.active = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
    }
    ImGui::End();

    // X button also dismisses
    if (!open) {
        g_BuildToast.active = false;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

// =========================================================================
// Pie UI bridge handlers
// =========================================================================

// Pie UI announced itself (on its load, or replying to our ping).
static void OnPieUIReady(void* /*eventArgs*/) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    g_PieUIPresent.store(true);
}

// Pie UI (or any addon) is probing for Alter Ego — reply with our presence.
static void OnAlterEgoPing(void* /*eventArgs*/) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (APIDefs && APIDefs->Events_Raise)
        APIDefs->Events_Raise(EV_ALTER_EGO_READY, nullptr);
}

// User clicked a build link in Pie UI's chat. Payload is the full "AE2:..." code
// (or a bare "[&...]" build link). Surface it via the build toast for confirmation —
// the toast's Import button does the actual import. Runs on Pie UI's thread (Nexus
// dispatches synchronously), so copy the payload immediately.
static void OnPieImportBuild(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (!eventArgs) return;
    try {
        const auto* p=static_cast<const char*>(eventArgs);
        size_t n=0;while(n<4096 && p[n])++n;
        if(n==0||n==4096)return;
        g_PieBuildInbox.Push(std::string(p,n));
    }catch(...){ /* Never unwind into the event dispatcher. */ }
}

static void HandlePieBuildInput(const std::string& payload) {
    std::string ae2code, chatLink;
    bool hasGear = false;

    if (payload.rfind("AE2:", 0) == 0) {
        ae2code = payload;
        auto ae2data = AlterEgo::ChatLink::Base64Decode(payload.substr(4));
        if (ae2data.size() >= 4 && (ae2data[0] == 2 || ae2data[0] == 3)) {
            uint8_t flags = ae2data[1];
            hasGear = (flags >> 3) & 1;
            uint8_t linkLen = ae2data[2];
            if (3 + (size_t)linkLen <= ae2data.size()) {
                std::vector<uint8_t> linkBytes(ae2data.begin() + 3, ae2data.begin() + 3 + linkLen);
                chatLink = "[&" + AlterEgo::ChatLink::Base64Encode(linkBytes.data(), linkBytes.size()) + "]";
            }
        }
    } else if (payload.rfind("[&", 0) == 0) {
        chatLink = payload;
    } else {
        return; // unrecognized payload
    }
    if (chatLink.empty()) return;

    AlterEgo::DecodedBuildLink decoded;
    if (!AlterEgo::ChatLink::DecodeBuild(chatLink, decoded)) return;

    std::string profession = ProfessionFromCode(decoded.profession);
    if (profession == "Unknown") return;

    std::string specName;
    for (int i = 2; i >= 0; i--) {
        if (decoded.specs[i].spec_id != 0) {
            const auto si = AlterEgo::GW2API::GetSpecInfo(decoded.specs[i].spec_id);
            if (si && si->elite) specName = si->name;
            break;
        }
    }

    {
        g_BuildToast.active = true;
        g_BuildToast.sender.clear();        // no chat sender on an explicit Pie hand-off
        g_BuildToast.chat_link = chatLink;
        g_BuildToast.ae2_code = ae2code;    // empty if a bare [&...] link
        g_BuildToast.profession = profession;
        g_BuildToast.spec_name = specName;
        g_BuildToast.channel = "Pie UI";
        g_BuildToast.has_gear = hasGear;
        g_ToastNeedsFocus = true;
    }

    // Pre-fetch palette data so the import is instant when the user confirms.
    if (!AlterEgo::GW2API::HasPaletteData(profession))
        AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
}

// Render a skill icon with tooltip
static void RenderSkillIcon(uint32_t skill_id, float size) {
    RenderSkillIconCore(skill_id, size, false);
}

// =========================================================================
// Gear Customization Helpers
// =========================================================================

// Friendly attribute name mapping
static const char* FriendlyAttrName(const std::string& attr) {return AlterEgo::UI::FriendlyAttribute(attr);}

// Curated rune list (name + item_id (offline bootstrap only)). IDs from GW2 API.
struct RuneEntry { const char* name; uint32_t item_id; };
static const RuneEntry RUNE_LIST[] = {
    {"Superior Rune of the Scholar",      91595},
    {"Superior Rune of the Eagle",        91433},
    {"Superior Rune of Strength",         91423},
    {"Superior Rune of the Dragonhunter", 91432},
    {"Superior Rune of the Berserker",    91397},
    {"Superior Rune of the Thief",        91579},
    {"Superior Rune of the Firebrand",    91538},
    {"Superior Rune of Balthazar",        91513},
    {"Superior Rune of the Trapper",      91508},
    {"Superior Rune of the Afflicted",    91460},
    {"Superior Rune of Tormenting",       91516},
    {"Superior Rune of the Tempest",      91583},
    {"Superior Rune of Surging",          91591},
    {"Superior Rune of the Pack",         91592},
    {"Superior Rune of Vampirism",        91545},
    {"Superior Rune of the Monk",         91501},
    {"Superior Rune of the Water",        91518},
    {"Superior Rune of Durability",       91444},
    {"Superior Rune of the Defender",     91404},
    {"Superior Rune of the Traveler",     91485},
    {"Superior Rune of the Rebirth",      91627},
    {"Superior Rune of the Nightmare",    91410},
    {"Superior Rune of Thorns",           91381},
    {"Superior Rune of the Citadel",      91588},
    {"Superior Rune of Fireworks",        91482},
    {"Superior Rune of the Grove",        91475},
    {"Superior Rune of Sanctuary",        91605},
    {"Superior Rune of Snowfall",         91512},
    {"Superior Rune of the Sunless",      91515},
    {"Superior Rune of Resistance",       91411},
    {"Superior Rune of the Weaver",       91451},
    {"Superior Rune of Scavenging",       91553},
    {"Superior Rune of Antitoxin",        91641},
    {"Superior Rune of the Aristocracy",  91602},
    {"Superior Rune of Rage",             91503},
    // --- Additional runes ---
    {"Superior Rune of Melandru",         24771},
    {"Superior Rune of the Chronomancer", 73399},
    {"Superior Rune of Hoelbrak",         24729},
    {"Superior Rune of Divinity",         24732},
    {"Superior Rune of Dwayna",           24768},
    {"Superior Rune of the Undead",       24757},
    {"Superior Rune of the Ogre",         24756},
    {"Superior Rune of Altruism",         38206},
    {"Superior Rune of the Warrior",      24821},
    {"Superior Rune of the Engineer",     24812},
    {"Superior Rune of Grenth",           24779},
    {"Superior Rune of the Herald",       76100},
    {"Superior Rune of the Dolyak",       24699},
    {"Superior Rune of the Ranger",       24815},
    {"Superior Rune of Leadership",       70600},
    // --- Remaining superior runes ---
    {"Superior Rune of Evasion",          67344},
    {"Superior Rune of Exuberance",       44951},
    {"Superior Rune of Infiltration",     24703},
    {"Superior Rune of Lyssa",            24776},
    {"Superior Rune of Mercy",            24708},
    {"Superior Rune of Nature's Bounty",  81091},
    {"Superior Rune of Orr",              24860},
    {"Superior Rune of Perplexity",       44957},
    {"Superior Rune of Radiance",         67342},
    {"Superior Rune of Rata Sum",         24726},
    {"Superior Rune of Speed",            24720},
    {"Superior Rune of Svanir",           24794},
    {"Superior Rune of the Adventurer",   24830},
    {"Superior Rune of the Air",          24750},
    {"Superior Rune of the Baelfire",     24854},
    {"Superior Rune of the Brawler",      24833},
    {"Superior Rune of the Cavalier",     83367},
    {"Superior Rune of the Centaur",      24788},
    {"Superior Rune of the Daredevil",    72852},
    {"Superior Rune of the Deadeye",      82791},
    {"Superior Rune of the Druid",        70450},
    {"Superior Rune of the Earth",        24744},
    {"Superior Rune of the Elementalist", 24800},
    {"Superior Rune of the Fire",         24747},
    {"Superior Rune of the Flame Legion", 24797},
    {"Superior Rune of the Flock",        24696},
    {"Superior Rune of the Forgeman",     24851},
    {"Superior Rune of the Golemancer",   24785},
    {"Superior Rune of the Guardian",     24824},
    {"Superior Rune of the Holosmith",    82633},
    {"Superior Rune of the Ice",          24753},
    {"Superior Rune of the Krait",        24762},
    {"Superior Rune of the Lich",         24688},
    {"Superior Rune of the Mad King",     36044},
    {"Superior Rune of the Mesmer",       24803},
    {"Superior Rune of the Mirage",       84127},
    {"Superior Rune of the Necromancer",  24806},
    {"Superior Rune of the Privateer",    24782},
    {"Superior Rune of the Reaper",       70829},
    {"Superior Rune of the Renegade",     83502},
    {"Superior Rune of the Revenant",     69370},
    {"Superior Rune of the Scourge",      83663},
    {"Superior Rune of the Scrapper",     71276},
    {"Superior Rune of the Soulbeast",    83964},
    {"Superior Rune of the Spellbreaker", 84749},
    {"Superior Rune of the Stars",        85713},
    {"Superior Rune of the Trooper",      24827},
    {"Superior Rune of the Wurm",         24791},
    {"Superior Rune of the Zephyrite",    88118},
};
static const int RUNE_COUNT = sizeof(RUNE_LIST) / sizeof(RUNE_LIST[0]);

// Curated sigil list (name + item_id (offline bootstrap only)). IDs from GW2 API.
struct SigilEntry { const char* name; uint32_t item_id; };
static const SigilEntry SIGIL_LIST[] = {
    {"Superior Sigil of Force",           91439},
    {"Superior Sigil of Impact",          91405},
    {"Superior Sigil of Accuracy",        91607},
    {"Superior Sigil of the Night",       91389},
    {"Superior Sigil of Undead Slaying",  91524},
    {"Superior Sigil of Demon Slaying",   91431},
    {"Superior Sigil of Air",             91520},
    {"Superior Sigil of Fire",            91559},
    {"Superior Sigil of Earth",           91531},
    {"Superior Sigil of Geomancy",        91552},
    {"Superior Sigil of Doom",            91480},
    {"Superior Sigil of Torment",         91412},
    {"Superior Sigil of Malice",          91478},
    {"Superior Sigil of Smoldering",      91488},
    {"Superior Sigil of Venom",           91532},
    {"Superior Sigil of Agony",           91534},
    {"Superior Sigil of Bursting",        91416},
    {"Superior Sigil of Concentration",   91473},
    {"Superior Sigil of Transference",    91448},
    {"Superior Sigil of Renewal",         91400},
    {"Superior Sigil of Blood",           91604},
    {"Superior Sigil of Strength",        91561},
    {"Superior Sigil of Severance",       91499},
    {"Superior Sigil of Energy",          91441},
    {"Superior Sigil of Paralyzation",    91398},
    {"Superior Sigil of Absorption",      91589},
    {"Superior Sigil of Cleansing",       91548},
    {"Superior Sigil of Draining",        91544},
    {"Superior Sigil of Demons",          91388},
    {"Superior Sigil of Serpent Slaying", 91456},
    // --- Additional sigils ---
    {"Superior Sigil of Battle",          24601},
    {"Superior Sigil of Vision",          24600},
    {"Superior Sigil of Hydromancy",      24597},
    {"Superior Sigil of Nullification",   24572},
    {"Superior Sigil of Bloodlust",       24575},
    {"Superior Sigil of Corruption",      24578},
    {"Superior Sigil of Ice",             24555},
    {"Superior Sigil of Chilling",        24630},
    {"Superior Sigil of Leeching",        24599},
    {"Superior Sigil of Celerity",        24865},
    // --- Remaining superior sigils ---
    {"Superior Sigil of Agility",         72092},
    {"Superior Sigil of Benevolence",     24584},
    {"Superior Sigil of Blight",          67913},
    {"Superior Sigil of Bounty",          81045},
    {"Superior Sigil of Centaur Slaying", 24645},
    {"Superior Sigil of Cruelty",         67341},
    {"Superior Sigil of Debility",        24636},
    {"Superior Sigil of Destroyer Slaying",24654},
    {"Superior Sigil of Dreams",          24681},
    {"Superior Sigil of Elemental Slaying",24661},
    {"Superior Sigil of Frailty",         24567},
    {"Superior Sigil of Frenzy",          82876},
    {"Superior Sigil of Generosity",      38294},
    {"Superior Sigil of Ghost Slaying",   24809},
    {"Superior Sigil of Grawl Slaying",   24648},
    {"Superior Sigil of Hobbling",        24627},
    {"Superior Sigil of Hologram Slaying",91339},
    {"Superior Sigil of Icebrood Slaying",24651},
    {"Superior Sigil of Incapacitation",  67343},
    {"Superior Sigil of Justice",         24678},
    {"Superior Sigil of Karka Slaying",   37912},
    {"Superior Sigil of Life",            24582},
    {"Superior Sigil of Luck",            24591},
    {"Superior Sigil of Mad Scientists",  24672},
    {"Superior Sigil of Mischief",        68436},
    {"Superior Sigil of Momentum",        49457},
    {"Superior Sigil of Ogre Slaying",    24655},
    {"Superior Sigil of Perception",      24580},
    {"Superior Sigil of Peril",           24621},
    {"Superior Sigil of Purity",          24571},
    {"Superior Sigil of Rage",            24561},
    {"Superior Sigil of Rending",         73532},
    {"Superior Sigil of Restoration",     24594},
    {"Superior Sigil of Ruthlessness",    71130},
    {"Superior Sigil of Smothering",      24675},
    {"Superior Sigil of Sorrow",          24684},
    {"Superior Sigil of Speed",           24589},
    {"Superior Sigil of Stamina",         24592},
    {"Superior Sigil of Water",           24551},
    {"Superior Sigil of Wrath",           24667},
    {"Superior Sigil of the Stars",       86170},
};
static const int SIGIL_COUNT = sizeof(SIGIL_LIST) / sizeof(SIGIL_LIST[0]);

// Curated relic list with exotic item IDs from GW2 API
struct RelicEntry { const char* name; uint32_t item_id; };
static const RelicEntry RELIC_LIST[] = {
    {"Relic of the Eagle",          104241},
    {"Relic of Fireworks",          100947},
    {"Relic of the Thief",          100976},
    {"Relic of Cerus",              100074},
    {"Relic of Isgarren",            99997},
    {"Relic of the Brawler",        100527},
    {"Relic of the Claw",           103574},
    {"Relic of the Deadeye",        100924},
    {"Relic of the Dragonhunter",   100090},
    {"Relic of Mount Balrior",      103872},
    {"Relic of Peitha",             100177},
    {"Relic of Lyhr",               100461},
    {"Relic of Fire",               104501},
    {"Relic of Dagda",              100942},
    {"Relic of the Fractal",        100153},
    {"Relic of Akeem",              100432},
    {"Relic of the Aristocracy",    100849},
    {"Relic of the Afflicted",      100693},
    {"Relic of the Nightmare",      100579},
    {"Relic of the Krait",          100230},
    {"Relic of the Sorcerer",       101863},
    {"Relic of the Mirage",         100158},
    {"Relic of Thorns",             104424},
    {"Relic of the Blightbringer",  102199},
    {"Relic of Nourys",             101191},
    {"Relic of the Scourge",        100368},
    {"Relic of the Demon Queen",    101166},
    {"Relic of Mosyn",              101801},
    {"Relic of the Biomancer",      106364},
    {"Relic of the First Revenant", 105585},
    {"Relic of Durability",         100562},
    {"Relic of the Monk",           100031},
    {"Relic of the Flock",          100633},
    {"Relic of Castora",            105652},
    {"Relic of the Water",          100659},
    {"Relic of the Defender",       100934},
    {"Relic of Vampirism",          100676},
    {"Relic of Dwayna",             100442},
    {"Relic of the Centaur",        100385},
    {"Relic of Mercy",              100429},
    {"Relic of Nayos",              101198},
    {"Relic of Karakosa",           101268},
    {"Relic of the Nautical Beast", 106920},
    {"Relic of the Living City",    104938},
    {"Relic of Leadership",         100625},
    {"Relic of the Herald",         100219},
    {"Relic of the Firebrand",      100453},
    {"Relic of Rivers",             103015},
    {"Relic of the Chronomancer",   100450},
    {"Relic of the Pack",           100752},
    {"Relic of Altruism",           104256},
    {"Relic of the Cavalier",       100542},
    {"Relic of the Twin Generals",  101767},
    {"Relic of Mabon",              100115},
    {"Relic of the Midnight King",  101139},
    {"Relic of Speed",              100148},
    {"Relic of the Wayfinder",      101943},
    {"Relic of Febe",               101116},
    {"Relic of the Adventurer",     100561},
    {"Relic of Evasion",            100886},
    {"Relic of Fog",                107030},
    {"Relic of the Daredevil",      100345},
    {"Relic of Geysers",            103763},
    {"Relic of Resistance",         100794},
    {"Relic of Antitoxin",          100390},
    {"Relic of the Trooper",        100411},
    {"Relic of the Astral Ward",    100388},
    {"Relic of Reunification",      103984},
    {"Relic of Surging",            100063},
    {"Relic of the Earth",          100435},
    {"Relic of the Warrior",        100299},
    {"Relic of the Alliance",       107192},
    {"Relic of Shackles",           106916},
    {"Relic of the Necromancer",    100580},
    {"Relic of the Scoundrel",      106355},
    {"Relic of the Reaper",         100739},
    {"Relic of Atrocity",           102245},
    {"Relic of the Mist Stranger",  106206},
    {"Relic of the Ogre",           100311},
    {"Relic of the Lich",           100238},
    {"Relic of the Golemancer",     100403},
    {"Relic of the Privateer",      100479},
    {"Relic of the Beehive",        103977},
    {"Relic of Sorrow",             103424},
    {"Relic of the Citadel",        100448},
    {"Relic of the Holosmith",      100908},
    {"Relic of the Ice",            100048},
    {"Relic of the Sunless",        100400},
    {"Relic of the Wizard's Tower", 100557},
    {"Relic of the Pirate Queen",   106221},
    {"Relic of the Phenom",         104733},
    {"Relic of the Weaver",         100194},
    {"Relic of Bava Nisos",         104848},
    {"Relic of the Zephyrite",      100893},
    {"Relic of Bloodstone",         104800},
    {"Relic of the Unseen Invasion",100694},
    {"Relic of Mistburn",           104994},
    {"Relic of the Forest Dweller", 107124},
    {"Relic of the Founding",       101737},
    {"Relic of the Mists Tide",     103901},
    {"Relic of the Stormsinger",    102595},
    {"Relic of the Steamshrieker",  104022},
    {"Relic of Vass",               100775},
    {"Relic of Zakiros",            101955},
    {"Relic of the Coral Heart",    107061},
    {"Relic of Agony",              104849},
    {"Relic of Galdra",             109264},
    {"Relic of the Cruel Overseer", 109664},
    {"Relic of the Director",       109351},
    {"Relic of the Doyen",          109522},
    {"Relic of the Sacred Grounds", 109267},
    {"Relic of Watch",              109709},
};
static const int RELIC_COUNT = sizeof(RELIC_LIST) / sizeof(RELIC_LIST[0]);

// Seed the persistent name→ID cache from the curated rune/sigil/relic tables.
// Called once on startup so that even fresh installs can resolve names instantly.
// Prefer validated public rows; legacy identities remain an offline bootstrap only.
static std::vector<AlterEgo::PublicCatalog::Entry> GearCatalog(const std::string& kind) {
    const auto snapshot=AlterEgo::PublicCatalog::Get();
    std::vector<AlterEgo::PublicCatalog::Entry> rows;
    for(const auto& e:snapshot->entries)if(e.kind==kind &&
        ((kind!="Rune"&&kind!="Sigil")||e.rarity=="Exotic") &&
        (kind!="Relic"||e.rarity=="Exotic"))rows.push_back(e);
    if(snapshot->upgrades)return rows;
    // Never present old hand-written effect summaries as current game data.
    if(kind=="Rune")for(const auto& e:RUNE_LIST)rows.push_back({e.name,"Legacy entry (not refreshed)",kind,"",e.item_id});
    if(kind=="Sigil")for(const auto& e:SIGIL_LIST)rows.push_back({e.name,"Legacy entry (not refreshed)",kind,"",e.item_id});
    if(kind=="Relic")for(const auto& e:RELIC_LIST)rows.push_back({e.name,"Legacy entry (not refreshed)",kind,"",e.item_id});
    return rows;
}
// Catalog selection has one eligibility/filter/ID policy. The caller chooses
// card or compact option presentation and retains ownership of its draft.
template<class OnChoose>
static void RenderGearCatalogChoices(const std::string& family,const std::string& filter,
    uint32_t currentId,const std::string& currentName,bool cards,OnChoose choose){
    const auto searchWords=AlterEgo::Search::Words(filter);
    for(const auto& e:GearCatalog(family)){
        if(!AlterEgo::Search::MatchesFields(searchWords,{e.name,e.bonus}))continue;
        ImGui::PushID(static_cast<int>(e.item_id));
        const bool selected=currentId?currentId==e.item_id:currentName==e.name;
        bool clicked=cards?AlterEgo::UI::CatalogCard("##gear_option",e.name.c_str(),e.bonus.c_str(),selected):
            AlterEgo::UI::CatalogOption(e.name.c_str(),e.bonus.c_str(),selected);
        AlterEgo::UI::EntityContext("##catalog_item_actions",[&]{CopyEntityMenu(e.name,AlterEgo::LINK_ITEM,e.item_id);});
        if(clicked)choose(e.item_id,e.name);
        ImGui::PopID();
    }
}
static void SeedItemNameCache() {
    for (int i = 0; i < RUNE_COUNT; i++)
        if(!AlterEgo::GW2API::FindItemIdByName(RUNE_LIST[i].name))
            AlterEgo::GW2API::CacheItemNameId(RUNE_LIST[i].name, RUNE_LIST[i].item_id);
    for (int i = 0; i < SIGIL_COUNT; i++)
        if(!AlterEgo::GW2API::FindItemIdByName(SIGIL_LIST[i].name))
            AlterEgo::GW2API::CacheItemNameId(SIGIL_LIST[i].name, SIGIL_LIST[i].item_id);
    for (int i = 0; i < RELIC_COUNT; i++)
        if(!AlterEgo::GW2API::FindItemIdByName(RELIC_LIST[i].name))
            AlterEgo::GW2API::CacheItemNameId(RELIC_LIST[i].name, RELIC_LIST[i].item_id);
}

// Slot display name for gear panel
static const char* GearSlotDisplayName(const std::string& slot) {
    if (slot == "WeaponA1") return "Main Hand A";
    if (slot == "WeaponA2") return "Off Hand A";
    if (slot == "WeaponB1") return "Main Hand B";
    if (slot == "WeaponB2") return "Off Hand B";
    if (slot == "WeaponAquaticA" || slot=="AquaticA") return "Aquatic A";
    if (slot == "WeaponAquaticB" || slot=="AquaticB") return "Aquatic B";
    if (slot == "HelmAquatic") return "Aquatic Headgear";
    if (slot == "Accessory1") return "Accessory 1";
    if (slot == "Accessory2") return "Accessory 2";
    if (slot == "Ring1") return "Ring 1";
    if (slot == "Ring2") return "Ring 2";
    return slot.c_str();
}

static bool IsArmorSlot(const std::string& s) {
    return s == "Helm" || s == "Shoulders" || s == "Coat" ||
           s == "Gloves" || s == "Leggings" || s == "Boots" || s == "HelmAquatic";
}
static bool IsWeaponSlot(const std::string& s) {
    return s == "WeaponA1" || s == "WeaponA2" || s == "WeaponB1" || s == "WeaponB2" || AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(s);
}
static bool IsMainhandSlot(const std::string& s) {
    return s == "WeaponA1" || s == "WeaponB1";
}
static bool IsOffhandSlot(const std::string& s) {
    return s == "WeaponA2" || s == "WeaponB2";
}
static bool IsTrinketSlot(const std::string& s) {
    return s == "Backpack" || s == "Accessory1" || s == "Accessory2" ||
           s == "Amulet" || s == "Ring1" || s == "Ring2";
}
static bool IsWeaponTwoHanded(const std::string& profession, const std::string& weaponType) {
    if (weaponType.empty()) return false;
    if(weaponType=="Harpoon"||weaponType=="Speargun"||weaponType=="Trident")return true;
    const auto profWeapons = AlterEgo::GW2API::GetProfessionWeapons(profession);
    if (profWeapons) {
        auto it = profWeapons->find(weaponType);
        if (it != profWeapons->end()) return it->second.two_handed;
    }
    return weaponType == "Greatsword" || weaponType == "Hammer" ||
           weaponType == "LongBow" || weaponType == "Longbow" ||
           weaponType == "ShortBow" || weaponType == "Shortbow" ||
           weaponType == "Rifle" || weaponType == "Staff" ||
           weaponType == "Spear" || weaponType == "Harpoon" ||
           weaponType == "Trident" || weaponType == "Speargun";
}

// Both gear editors use the same eligibility, descriptions, filtering and catalog controls.
template<class OnChoose>
static void RenderWeaponTypeChoices(const std::string& profession,const std::string& slot,
    const std::string& current,const std::string& filter,bool cards,OnChoose choose){
    const auto weapons=AlterEgo::GW2API::GetProfessionWeapons(profession);
    if(!weapons)return;
    const auto searchWords=AlterEgo::Search::Words(filter);
    for(const auto& [name,wd]:*weapons){
        if(!AlterEgo::EquipmentChoice::WeaponTypeEligible(slot,name,wd.mainhand,wd.offhand,wd.two_handed))continue;
        std::string desc=wd.two_handed?"Two-Handed":wd.mainhand&&wd.offhand?"Main / Off Hand":wd.mainhand?"Main Hand":"Off Hand";
        if(wd.specialization){
            const auto spec=AlterEgo::GW2API::GetSpecInfo(wd.specialization);
            desc+=" ("+(spec?spec->name:"Specialization #"+std::to_string(wd.specialization))+")";
        }
        const auto label=WeaponDisplayName(name);
        if(!AlterEgo::Search::MatchesFields(searchWords,{label,name,desc}))continue;
        ImGui::PushID(name.c_str());
        const bool selected=current==name;
        const bool clicked=cards?AlterEgo::UI::CatalogCard("##weapon_option",label,desc.c_str(),selected):
            AlterEgo::UI::CatalogOption(label,desc.c_str(),selected);
        NameOptionContext(label);
        if(clicked&&!selected)choose(name,wd);
        ImGui::PopID();
    }
}
template<class Definition>
static void FetchChosenWeaponMetadata(const Definition& wd){
    std::vector<uint32_t> skillIds;
    for(auto id:wd.skills)if(id)skillIds.push_back(id);
    for(const auto& [att,skills]:wd.attunement_skills)for(auto id:skills)if(id)skillIds.push_back(id);
    for(const auto& [weapon,id]:wd.dual_wield)if(id)skillIds.push_back(id);
    if(!skillIds.empty())AlterEgo::GW2API::FetchSkillDetailsAsync(skillIds);
}

// Profession -> armor weight class
static int GetArmorWeight(const std::string& profession) {
    // 0=Heavy, 1=Medium, 2=Light
    if (profession == "Warrior" || profession == "Guardian" || profession == "Revenant") return 0;
    if (profession == "Ranger" || profession == "Thief" || profession == "Engineer") return 1;
    if (profession == "Elementalist" || profession == "Mesmer" || profession == "Necromancer") return 2;
    // Elite specs map to base profession via the profession field in SavedBuild
    return 0; // fallback heavy
}

// Obsidian armor item IDs [weight][slot]: weight 0=Heavy,1=Medium,2=Light, slot 0=Helm..5=Boots
static const uint32_t OBSIDIAN_ARMOR[3][6] = {
    // Heavy: Helmet, Pauldrons, Breastplate, Gauntlets, Cuisses, Greaves
    { 101544, 101551, 101521, 101609, 101568, 101460 },
    // Medium: Mask, Shoulders, Jacket, Gloves, Leggings, Boots
    { 101614, 101645, 101556, 101570, 101579, 101602 },
    // Light: Crown, Mantle, Regalia, Gloves, Pants, Shoes
    { 101516, 101462, 101499, 101536, 101501, 101535 },
};

// Gen1 legendary weapon item IDs by weapon type name
static uint32_t GetLegendaryWeaponId(const std::string& wtype) {
    if (wtype == "Axe") return 30684;           // Frostfang
    if (wtype == "Dagger") return 30687;         // Incinerator
    if (wtype == "Focus") return 30701;          // The Minstrel
    if (wtype == "Greatsword") return 30703;     // Sunrise
    if (wtype == "Hammer") return 30690;         // The Juggernaut
    if (wtype == "Longbow") return 30685;        // Kudzu
    if (wtype == "Mace") return 30692;           // The Moot
    if (wtype == "Pistol") return 30693;         // Quip
    if (wtype == "Rifle") return 30694;          // The Predator
    if (wtype == "Scepter") return 30695;        // Meteorlogicus
    if (wtype == "Shield") return 30696;         // The Flameseeker Prophecies
    if (wtype == "Shortbow") return 30686;       // The Dreamer
    if (wtype == "Staff") return 30698;          // The Bifrost
    if (wtype == "Sword") return 30699;          // Bolt
    if (wtype == "Torch") return 30700;          // Rodgort
    if (wtype == "Warhorn") return 30702;        // Howler
    if (wtype == "Spear") return 30691;          // Kamohoali'i Kotaki
    return 0;
}

// Legendary trinket item IDs by slot
static uint32_t GetTrinketIconId(const std::string& slot) {
    if (slot == "Backpack") return 74155;        // Ad Infinitum
    if (slot == "Accessory1") return 81908;      // Aurora
    if (slot == "Accessory2") return 91048;      // Vision
    if (slot == "Amulet") return 95380;          // Prismatic Champion's Regalia
    if (slot == "Ring1") return 93105;           // Conflux
    if (slot == "Ring2") return 93105;           // Conflux
    if (slot == "Relic") return 101582;          // Legendary Relic
    return 0;
}

// Get the representative item ID for a build gear slot icon
static uint32_t GetGearSlotIconItemId(const AlterEgo::SavedBuild& build, const std::string& slot) {
    if (slot=="HelmAquatic"||AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(slot)) {
        const auto it=build.gear.find(slot);
        if(it!=build.gear.end()&&it->second.item_id)return it->second.item_id;
        return 0;
    }
    if (IsArmorSlot(slot)) {
        int weight = GetArmorWeight(build.profession);
        int idx = -1;
        if (slot == "Helm") idx = 0;
        else if (slot == "Shoulders") idx = 1;
        else if (slot == "Coat") idx = 2;
        else if (slot == "Gloves") idx = 3;
        else if (slot == "Leggings") idx = 4;
        else if (slot == "Boots") idx = 5;
        if (idx >= 0) return OBSIDIAN_ARMOR[weight][idx];
    } else if (IsWeaponSlot(slot)) {
        auto it = build.gear.find(slot);
        if (it != build.gear.end() && !it->second.weapon_type.empty())
            return GetLegendaryWeaponId(it->second.weapon_type);
    } else {
        return GetTrinketIconId(slot);
    }
    return 0;
}

// Open the gear customize dialog for a specific slot
static void OpenGearDialog(const std::string& buildId, const std::string& slot,
                           const AlterEgo::BuildGearSlot& current) {
    g_GearDialogFocus.Request();
    if(g_GearDialogOpen)return; // Focus the existing draft; never silently replace it.
    if(!g_LibEditMode||g_EditDraft.id!=buildId)return;
    g_GearDialogDraft=g_EditDraft;
    g_GearDialogBaseline=AlterEgo::GW2API::EditorBuildSnapshot(g_GearDialogDraft);
    g_GearDialogDirty=false;
    g_GearDialogOpen = true;
    g_GearDialogSlot = slot;
    g_GearDialogBuildId = buildId;
    g_GearSelectedStatId = current.stat_id;
    bool isWeaponSlot = (slot.find("Weapon") == 0);
    g_GearSelectorTab = isWeaponSlot ? 2 : 0;
    g_GearEditingSigil2 = false;
    g_GearEditingInfusionIndex = 0;
    g_GearExactInfusionId = current.infusion_ids.empty()
        ? 0 : current.infusion_ids.front();
    memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
}

// Build a sorted list of stat combos for the selection panel
static std::vector<std::shared_ptr<const AlterEgo::ItemStatInfo>> GetSortedStatCombos(const char* filter) {
    std::vector<std::shared_ptr<const AlterEgo::ItemStatInfo>> result;
    const auto& allStats = AlterEgo::GW2API::GetAllItemStats();
    const auto searchWords=AlterEgo::Search::Words(filter?filter:"");
    std::set<std::string> seenNames;
    for (const auto& [id, row] : allStats) {
        const auto& info=*row;
        if (info.name.empty() || info.attributes.empty()) continue;
        // Deduplicate: GW2 API has multiple stat IDs with the same name (different tiers)
        if (!seenNames.insert(info.name).second) continue;
        if (!searchWords.empty()) {
            std::vector<std::string> fields{info.name};
            for(const auto& a:info.attributes)fields.push_back(FriendlyAttrName(a));
            if(!AlterEgo::Search::MatchesFields(searchWords,fields))continue;
        }
        result.push_back(row);
    }
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) {
            return a->name < b->name;
        });
    return result;
}

// Share the sorted stat catalog, selected ID and attribute presentation.
template<class OnChoose> static void RenderGearStatChoices(const char* filter,
    uint32_t current,bool cards,OnChoose choose){
    for(const auto& stat:GetSortedStatCombos(filter)){
        ImGui::PushID(static_cast<int>(stat->id));
        const bool selected=current==stat->id;
        if(cards){
            const ImVec2 pos=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x;
            const float line=std::max(14.0f,ImGui::GetTextLineHeight());
            const float height=line*(1+stat->attributes.size())+6.0f;
            auto* dl=ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos,ImVec2(pos.x+width,pos.y+height),selected?IM_COL32(60,80,100,200):IM_COL32(35,35,40,180),3.0f);
            dl->AddRect(pos,ImVec2(pos.x+width,pos.y+height),selected?IM_COL32(100,160,220,255):IM_COL32(80,80,80,180),3.0f);
            if(ImGui::InvisibleButton("##stat_choice",ImVec2(width,height)))choose(stat->id,stat->name);
            ImVec2 text(pos.x+6,pos.y+3);
            dl->PushClipRect(pos,ImVec2(pos.x+width,pos.y+height),true);
            dl->AddText(text,IM_COL32(255,210,80,255),stat->name.c_str());
            for(const auto& attr:stat->attributes){
                text.y+=line;auto label=std::string("+")+FriendlyAttrName(attr);
                dl->AddText(text,IM_COL32(100,200,100,255),label.c_str());
            }
            dl->PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(pos.x,pos.y+height+2));
        }else{
            if(AlterEgo::UI::SelectableOption(stat->name.c_str(),selected))choose(stat->id,stat->name);
            if(ImGui::IsItemHovered()){
                ImGui::BeginTooltip();
                for(const auto& attr:stat->attributes)ImGui::BulletText("%s",FriendlyAttrName(attr));
                ImGui::EndTooltip();
            }
        }
        NameOptionContext(stat->name);
        ImGui::PopID();
    }
}

// Helper: render a card-based selector list and return the selected name (empty = no change)
// Returns true if a card was clicked (and sets outName)
static bool RenderCardSelector(const char* searchBuf, const char* childId,
                               const char* const* names, const char* const* descs, int count,
                               const std::string& currentSelection) {
    bool changed = false;
    const auto searchWords=AlterEgo::Search::Words(searchBuf?searchBuf:"");

    ImGui::BeginChild(childId, ImVec2(0, -30), true);
    for (int i = 0; i < count; i++) {
        if(!AlterEgo::Search::MatchesFields(searchWords,{names[i],descs[i]}))continue;

        ImGui::PushID(i);
        bool isSelected = (currentSelection == names[i]);

        ImVec2 cardStart = ImGui::GetCursorScreenPos();
        float cardW = ImGui::GetContentRegionAvail().x;
        float cardH = 32.0f;

        ImU32 bgCol = isSelected ? IM_COL32(60, 80, 100, 200) : IM_COL32(35, 35, 40, 180);
        ImGui::GetWindowDrawList()->AddRectFilled(
            cardStart, ImVec2(cardStart.x + cardW, cardStart.y + cardH), bgCol, 3.0f);
        ImGui::GetWindowDrawList()->AddRect(
            cardStart, ImVec2(cardStart.x + cardW, cardStart.y + cardH),
            isSelected ? IM_COL32(100, 160, 220, 255) : IM_COL32(80, 80, 80, 180), 3.0f);

        ImGui::SetCursorScreenPos(cardStart);
        if (ImGui::InvisibleButton("##sel", ImVec2(cardW, cardH))) {
            changed = true;
        }

        ImVec2 textPos(cardStart.x + 6, cardStart.y + 2);
        ImGui::GetWindowDrawList()->AddText(textPos,
            IM_COL32(200, 180, 255, 255), names[i]);
        textPos.y += 14.0f;
        ImGui::GetWindowDrawList()->AddText(textPos,
            IM_COL32(150, 150, 150, 220), descs[i]);
        ImGui::SetCursorScreenPos(ImVec2(cardStart.x, cardStart.y + cardH + 2));

        ImGui::PopID();
        if (changed) {
            ImGui::EndChild();
            return true; // caller should set the name from names[i]
        }
    }
    ImGui::EndChild();
    return false;
}

// Render the Customize popup dialog
// Profession names in GW2 build-link order (1..9). Shared by the build editor.
static const char* kBuildProfessions[9] = {
    "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
    "Elementalist", "Mesmer", "Necromancer", "Revenant"
};

// Public legend definitions replace the old name-keyword mapping. Known
// protocol labels are retained only for offline display/selection.
struct LegendOption { uint8_t code; std::string name; uint32_t swap=0; std::string icon; };
static std::vector<LegendOption> LegendOptions() {
    const auto catalog=AlterEgo::PublicCatalog::Get();
    std::vector<LegendOption> out;
    for(const auto& l:catalog->legends)out.push_back({l.code,l.name,l.swap,l.icon});
    if(out.empty())out={{1,"Dragon (Herald)",0,{}},{2,"Assassin (Shiro)",0,{}},{3,"Dwarf (Jalis)",0,{}},
        {4,"Demon (Mallyx)",0,{}},{5,"Renegade (Kalla)",0,{}},{6,"Centaur (Ventari)",0,{}},
        {7,"Alliance (Vindicator)",0,{}},{8,"Entity (Conduit)",0,{}}};
    return out;
}
static LegendOption LegendOptionForCode(uint8_t code) {
    const auto catalog=AlterEgo::PublicCatalog::Get();
    if(const auto* l=AlterEgo::PublicCatalog::FindLegend(catalog->legends,code))
        return {l->code,l->name,l->swap,l->icon};
    code=AlterEgo::PublicCatalog::CanonicalLegendCode(code);
    for (const auto& l : LegendOptions()) if (l.code == code) return l;
    return {code,code?"Legend metadata unavailable":"Choose legend",0,{}};
}
struct ProfessionChoiceDisplay {
    std::string name,icon,description;
    uint32_t key=0,swap=0;
    std::vector<AlterEgo::PublicCatalog::CompanionSkillReference> skills;
    bool skillsKnown=false, nameKnown=false;
};
// Cache-only presentation resolution shared by slots and Compare. Requests
// remain at the existing visible picker/slot demand boundary.
static ProfessionChoiceDisplay ResolveProfessionChoice(bool legend,uint32_t current) {
    ProfessionChoiceDisplay result;
    if(legend){
        const auto option=current<=255?LegendOptionForCode(static_cast<uint8_t>(current)):
            LegendOption{0,"Legend metadata unavailable",0,{}};
        result.name=option.name;result.swap=option.swap;result.icon=option.icon;
        const auto catalog=AlterEgo::PublicCatalog::Get();
        if(const auto* def=AlterEgo::PublicCatalog::FindLegend(catalog->legends,current)){
            result.nameKnown=!def->name.empty();
            result.skills=AlterEgo::PublicCatalog::CompanionSkills(*def);
            result.skillsKnown=def->heal.has_value()&&def->elite.has_value()&&def->utilities.has_value();
        }
        if(const auto skill=result.swap?AlterEgo::GW2API::GetSkillInfo(result.swap):nullptr){
            if(!skill->icon_url.empty())result.icon=skill->icon_url;
            result.description=StripGW2Markup(skill->description);
        }
        result.key=AlterEgo::ArtworkRequest::SkillKey(result.swap);
    }else{
        const auto catalog=AlterEgo::PublicCatalog::Get();
        result.name=current?"Pet metadata unavailable":"Choose pet";
        if(const auto* pet=AlterEgo::PublicCatalog::FindPet(catalog->pets,current)){
            result.name=pet->name;result.icon=pet->icon;result.nameKnown=!pet->name.empty();
            if(pet->description)result.description=StripGW2Markup(*pet->description);
            result.skills=AlterEgo::PublicCatalog::CompanionSkills(*pet);
            result.skillsKnown=pet->skills.has_value();
        }
        result.key=AlterEgo::ArtworkRequest::PetKey(current);
    }
    return result;
}
static void ProfessionChoiceActions(bool legend,const ProfessionChoiceDisplay& value){
    using namespace AlterEgo;
    if(value.nameKnown)CopyNameMenuItem(value.name);
    if(legend&&value.swap)CopyEntityChatLink(LINK_SKILL,value.swap,"Copy Invocation Skill Chat Link");
    // The fixed Wiki search endpoint avoids inventing an article title for
    // a verified catalog name (particularly alternate legend names).
    if(value.nameKnown)ReferenceWikiSearchAction(value.name);
}
// Common companion tooltip across icon-and-name, picker, collection and Compare.
// Skill names/roles are cached reference evidence, never actual equipped order.
static void RenderCompanionTooltip(const ProfessionChoiceDisplay& choice,const std::string& note={}){
    using namespace AlterEgo;
    if(choice.name.empty())return;
    ImGui::BeginTooltip();
    UI::TooltipLine(choice.name.c_str(),UI::TooltipTitle);
    if(!choice.description.empty())UI::TooltipLine(choice.description.c_str());
    if(!note.empty())UI::TooltipLine(note.c_str());
    if(!choice.skills.empty()){
        ImGui::Separator();ImGui::TextDisabled("%s",choice.skillsKnown?"Catalog skill references":"Catalog skill references (partial)");
        size_t shown=0,unresolved=0;
        for(const auto& ref:choice.skills){
            const auto skill=GW2API::GetSkillInfo(ref.id);
            if(!skill||skill->name.empty()){++unresolved;continue;}
            if(shown++>=8)continue;
            const auto line=std::string(PublicCatalog::RoleLabel(ref.role))+": "+skill->name;
            UI::TooltipLine(line.c_str());
        }
        if(shown>8)ImGui::TextDisabled("Additional references in details");
        if(unresolved)ImGui::TextDisabled("%zu skill reference(s) not retrieved",unresolved);
    }else if(choice.nameKnown&&!choice.skillsKnown)ImGui::TextDisabled("Associated skills not retrieved");
    ImGui::EndTooltip();
}

// Render the same GW2 profession image used in the Characters roster in
// Shared profession icon and class color renderer for character selectors and
// build dialogs. Drawn on top of an existing ImGui control so all actions
// retain native focus, hover, accessibility and theme styling.
static void DrawCompareCharacterProfessionIcon(ImDrawList* dl,
                                               ImVec2 itemMin, float itemHeight,
                                               const std::string& profession) {
    constexpr float iconSize = 17.0f;
    const ImVec2 iconMin(itemMin.x + 8.0f,
                         itemMin.y + (itemHeight - iconSize) * 0.5f);
    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
    const uint32_t iconId = GetProfessionIconId(profession);
    // Fixed class artwork is embedded. Opening a picker must not fetch metadata.
    if (iconId) {
        Texture_t* tex = AlterEgo::IconManager::GetIcon(iconId);
        if (tex && tex->Resource) {
            dl->AddImage(tex->Resource, iconMin, iconMax);
            return;
        }
        // The DLL image and disk cache both work without metadata.
        AlterEgo::IconManager::RequestIcon(iconId, "");
    }
    // Keep a readable fallback if the embedded texture is not yet available.
    if (!profession.empty()) {
        const std::string initial = profession.substr(0, 1);
        dl->AddText(ImVec2(iconMin.x + 3.0f,
                           itemMin.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(GetProfessionColor(profession)),
                    initial.c_str());
    }
}

// Use the same icon and profession color for Characters, Compare and the New
// Build profession choices. Optional prefixes stay neutral.
static void DrawProfessionCharacterEntry(ImDrawList* draw,
    ImVec2 min, ImVec2 max, const std::string& profession,
    const std::string& name, const char* prefix = nullptr,
    bool preview = false) {
    const float h = max.y - min.y;
    DrawCompareCharacterProfessionIcon(draw, min, h, profession);
    const float y = min.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    float x = min.x + 32.0f;
    const float right = max.x - (preview ? 29.0f : 6.0f);
    if (right <= x) return;
    draw->PushClipRect(ImVec2(x,min.y), ImVec2(right,max.y), true);
    if (prefix && *prefix) {
        draw->AddText(ImVec2(x,y), ImGui::GetColorU32(ImGuiCol_Text), prefix);
        x += ImGui::CalcTextSize(prefix).x;
    }
    DrawProfessionCharacterName(draw, ImVec2(x,y), name, profession);
    draw->PopClipRect();
}

void DrawInventoryCharacterLabel(ImDrawList* draw,ImVec2 min,ImVec2 max,const std::string& profession,const std::string& name){
    DrawProfessionCharacterEntry(draw,min,max,profession,name);
}

static void CharacterSelectorActions(const std::string& name,const std::string& account);
// Shared selectable row for single-target and multi-target character pickers.
// Keep the native ImGui control responsible for selection, hover and keyboard focus.
static bool SelectProfessionCharacterEntry(const char* id, bool selected,
    const std::string& profession, const std::string& name,
    const char* prefix = nullptr, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None,const std::string& account={}) {
    const ImVec2 size(0.0f, ImMax(24.0f, ImGui::GetTextLineHeightWithSpacing()+3.0f));
    const bool clicked=ImGui::Selectable(id,selected,flags,size);
    DrawProfessionCharacterEntry(ImGui::GetWindowDrawList(),
        ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),profession,name,prefix);
    AlterEgo::UI::EntityContext("##character_name",[&]{CharacterSelectorActions(name,account.empty()?AlterEgo::NativeAccounts::VerifiedAccountForCharacter(name):account);});
    return clicked;
}

bool RenderCharacterMultiFilter(const char* label,AlterEgo::NativeInventory::CharacterSelections& selected,
                                const std::vector<AlterEgo::CharacterIdentity::Address>& candidates) {
    using namespace AlterEgo;
    ImGui::PushID(label);UI::FilterFieldLabel(label);
    std::map<CharacterOrder::Key,CharacterIdentity::Address> available;
    std::set<CharacterOrder::Key> keys;
    for(const auto& address:candidates){
        const CharacterOrder::Key key{address.account,address.name};
        available.emplace(key,address);keys.insert(key);
    }
    const auto order=OrderedCharacterKeys(keys);
    const bool single=selected.size()==1;
    const auto preview=selected.empty()?std::string("All"):
        single?selected.begin()->second.name:std::to_string(selected.size())+" selected";
    const auto profession=single?selected.begin()->second.evidence.profession:std::string{};
    const bool decorated=!profession.empty();
    const auto pos=ImGui::GetCursorScreenPos();
    const float width=UI::FilterFieldWidth(),height=ImGui::GetFrameHeight();
    auto* draw=ImGui::GetWindowDrawList();
    bool changed=false;
    if(UI::BeginCombo("##value",decorated?"":preview.c_str(),0,width)){
        if(ImGui::Selectable("All",selected.empty(),ImGuiSelectableFlags_DontClosePopups)&&!selected.empty()){
            selected.clear();changed=true;
        }
        std::set<CharacterIdentity::Ref> visible;
        for(const auto& key:order){
            const auto& address=available.at(key);
            const auto ref=address.Reference();visible.insert(ref);
            ImGui::PushID(ref.account.c_str());ImGui::PushID(std::to_string(ref.id).c_str());
            const auto prior=selected.find(ref);
            const bool on=prior!=selected.end()&&prior->second.Matches(address);
            if(SelectProfessionCharacterEntry("##character",on,address.binding.evidence.profession,
                address.name,nullptr,ImGuiSelectableFlags_DontClosePopups,address.account)){
                if(on)selected.erase(ref);
                else selected[ref]={ref,address.binding.evidence,address.name,false};
                changed=true;
            }
            ImGui::PopID();ImGui::PopID();
        }
        for(auto it=selected.begin();it!=selected.end();){
            if(visible.count(it->first)){++it;continue;}
            ImGui::PushID(it->first.account.c_str());ImGui::PushID(std::to_string(it->first.id).c_str());
            const auto name=it->second.name+" (unavailable)";
            const bool remove=ImGui::Selectable(name.c_str(),true,ImGuiSelectableFlags_DontClosePopups);
            ImGui::PopID();ImGui::PopID();
            if(remove){it=selected.erase(it);changed=true;}else ++it;
        }
        ImGui::EndCombo();
    }else if(single){
        const auto& selection=selected.begin()->second;
        const auto current=NativeAccounts::ResolveCharacterAddress(selection.ref);
        if(current&&selection.Matches(*current)){
            ImGui::PushID(preview.c_str());UI::EntityContext("##character_filter_actions",[&]{CharacterSelectorActions(current->name,current->account);});ImGui::PopID();
        }
    }
    if(decorated)DrawProfessionCharacterEntry(draw,pos,ImVec2(pos.x+width,pos.y+height),profession,preview,nullptr,true);
    ImGui::PopID();return changed;
}

// "+ New Loadout" dialog: pick a profession + game mode, create a blank build,
// and drop straight into edit mode.
static void RenderNewBuildDialog() {
    if(g_NewBuildDialogOpen&&(g_LibEditMode||OtherLibraryEditorOpen())){g_NewBuildDialogOpen=false;return;}
    if (g_NewBuildDialogOpen && !ImGui::IsPopupOpen("New Loadout###newbuild"))
        ImGui::OpenPopup("New Loadout###newbuild");

    const ImVec2 dispSize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(dispSize.x * 0.5f, dispSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Loadout###newbuild", &g_NewBuildDialogOpen,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        constexpr float kGridWidth = 480.0f;
        ImGui::TextUnformatted("Game modes");
        RenderGameModeSelector("new-build-modes", g_NewBuildModes);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Choose a profession");
        // Keep the modal's profession choices consistent with Characters and
        // Compare: shared icon loader, shared color, shared action button sizing.
        if (ImGui::BeginTable("##newBuildProfessions", 3,
                              ImGuiTableFlags_SizingStretchSame, ImVec2(kGridWidth, 0))) {
            for (int i = 0; i < 9; ++i) {
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                const char* profession = kBuildProfessions[i];
                const float width = ImGui::GetContentRegionAvail().x;
                const bool clicked = RenderActionButton("##newProfession",
                    ImVec2(width, ImMax(32.0f, ImGui::GetFrameHeight())));
                DrawProfessionCharacterEntry(ImGui::GetWindowDrawList(),
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    profession, profession);
                if (clicked) {
                    AlterEgo::SavedBuild blank = AlterEgo::GW2API::CreateBlankBuild(
                        profession, g_NewBuildModes.front());
                    blank.game_modes = g_NewBuildModes;
                    int idx = AlterEgo::GW2API::AddSavedBuild(std::move(blank));
                    if (idx >= 0) {
                        const auto& builds = AlterEgo::GW2API::GetSavedBuilds();
                        SelectLoadoutAt(idx);
                        NavigateToLibraryCollection(0);
                        g_LibEditBuildId = builds[idx].id;
                        EnterBuildEditMode(builds[idx]);
                        g_EditIsNewBuild = true;
                        AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
                        AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
                    }
                    g_NewBuildDialogOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (RenderActionButton("Cancel##newBuild")) {
            g_NewBuildDialogOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Specialization picker for the build editor. Lists the profession's specs,
// enforces a single elite spec and no duplicate specs across the 3 slots.
// Common selection rows; modal and inline combo retain their own scope.
static bool RenderSpecializationChoices(const std::string& profession,
    const std::array<uint32_t,3>& ids,int slot,uint32_t& next) {
    if(slot<0||slot>=3)return false;
    bool changed=false;
    if(!profession.empty())AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
    auto specIds=AlterEgo::GW2API::GetProfessionSpecIds(profession);
    // A durable membership record does not prove its dependent detail cache.
    if(!specIds.empty())AlterEgo::GW2API::FetchSpecDetailsAsync(specIds);
    if(specIds.empty()){
        if(!profession.empty()){RenderSpinner("Loading specializations...");}
        else ImGui::TextDisabled("No profession selected.");
    }
    for(uint32_t sid:specIds){
        const auto si=AlterEgo::GW2API::GetSpecInfo(sid);
        if(!si||(si->elite&&slot!=2))continue;
        const bool allowed=AlterEgo::SkillEdit::SpecializationChoiceAllowed(ids,slot,sid,si->elite);
        const auto label=si->name+(si->elite?"  (Elite)":"");
        ImGui::PushID(static_cast<int>(sid));
        constexpr float size=22.0f;
        const uint32_t key=sid+1500000u;
        auto* icon=AlterEgo::IconManager::GetIcon(key);
        if(icon&&icon->Resource)ImGui::Image(icon->Resource,ImVec2(size,size),ImVec2(0,0),ImVec2(1,1),
            allowed?ImVec4(1,1,1,1):ImVec4(1,1,1,0.4f));
        else {ImGui::Dummy(ImVec2(size,size));if(!si->icon_url.empty())AlterEgo::IconManager::RequestIcon(key,si->icon_url);}
        ImGui::SameLine(0,8);ImGui::AlignTextToFramePadding();
        if(!allowed)ImGui::TextDisabled("%s",label.c_str());
        else if(ImGui::Selectable(label.c_str(),ids[slot]==sid)){next=sid;changed=true;}
        if(ImGui::IsItemHovered()){
            if(!allowed)AlterEgo::UI::Hint("Already selected in another specialization slot.");
            else RenderSpecializationTooltip(si->name,AlterEgo::UI::TooltipTitle,si->profession,false);
        }
        NameOptionContext(si->name);
        ImGui::PopID();
    }
    ImGui::Separator();
    if(ImGui::Selectable("(None)",ids[slot]==0)){next=0;changed=true;}
    return changed;
}
static void RenderSpecPickerDialog() {
    if(g_SpecPickerSlot<0)return;
    const char* POPUP="Choose Specialization##specpick";
    if(!ImGui::IsPopupOpen(POPUP))ImGui::OpenPopup(POPUP);
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(size.x*0.5f,size.y*0.5f),ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
    bool open=true;
    if(ImGui::BeginPopupModal(POPUP,&open,ImGuiWindowFlags_AlwaysAutoResize)){
        const int slot=g_SpecPickerSlot;
        std::array<uint32_t,3> ids{};
        for(int i=0;i<3;++i)ids[i]=g_EditDraft.specializations[i].spec_id;
        uint32_t next=ids[slot];
        if(RenderSpecializationChoices(g_EditDraft.profession,ids,slot,next)){
            if(next!=ids[slot]){
                const auto info=AlterEgo::GW2API::GetSpecInfo(next);
                if(info&&info->elite)for(int i=0;i<3;++i)if(i!=slot){
                    const auto other=AlterEgo::GW2API::GetSpecInfo(ids[i]);
                    if(other&&other->elite)g_EditDraft.specializations[i]={};
                }
                g_EditDraft.specializations[slot]={};
                g_EditDraft.specializations[slot].spec_id=next;
                MarkEditDirty();
            }
            g_SpecPickerSlot=-1;ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(!open)g_SpecPickerSlot=-1;
}

// Shared eligibility policy for both the monolithic Loadout editor and the
// v3 standalone Skills component editor. Keep the GW2 skill-type, profession
// and selected elite-spec checks in ONE place.
static std::vector<uint32_t> SelectableBuildSkillIds(
    const std::string& profession, const char* type,
    const std::vector<uint32_t>& specializationIds,
    bool allowAllSpecializations = false,bool aquatic=false,bool refreshEnvironment=false) {
    if (profession.empty()) return {};
    AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
    const auto allIds = AlterEgo::GW2API::GetProfessionSkillIds(profession);
    if (allIds.empty()) {
        return {};
    }
    // Catalog admission sees every demanded ID, including stale cached definitions.
    AlterEgo::GW2API::FetchSkillDetailsAsync(allIds);
    std::vector<uint32_t> missing;
    std::vector<uint32_t> matches;
    for (uint32_t id : allIds) {
        const auto skill = AlterEgo::GW2API::GetSkillInfo(id);
        if (!skill) { missing.push_back(id); continue; }
        if (skill->type != type) continue;
        if (!allowAllSpecializations && skill->specialization &&
            std::find(specializationIds.begin(), specializationIds.end(),
                      skill->specialization) == specializationIds.end()) continue;
        const bool owned = std::find(skill->professions.begin(),
            skill->professions.end(), profession) != skill->professions.end();
        if(!owned)continue; // never include racial skills
        if(aquatic&&refreshEnvironment&&!skill->no_underwater.has_value())missing.push_back(id);
        if(AlterEgo::SkillEdit::EnvironmentChoiceAllowed(aquatic,skill->no_underwater))matches.push_back(id);
    }
    if (!missing.empty()) AlterEgo::GW2API::FetchSkillDetailsAsync(missing,aquatic&&refreshEnvironment);
    return matches;
}

// Both editors use a native field-scoped popup; click-away and Escape are
// handled by ImGui instead of a global reopen latch.
static bool RenderSkillPickerPopup(const char* popup,const std::string& profession,
        const char* role,const std::vector<uint32_t>& specs,bool allowAnySpec,
        uint32_t current,uint32_t& next,bool aquatic=false) {
    if(!ImGui::BeginPopup(popup))return false;
    bool selected=false;
    ImGui::TextDisabled("%s - %s",profession.empty()?"Choose a profession":profession.c_str(),role);
    const auto choices=SelectableBuildSkillIds(profession,role,specs,allowAnySpec,aquatic,ImGui::IsWindowAppearing());
    if(aquatic){
        ImGui::TextDisabled("Underwater: ? = eligibility unknown.");
        const auto info=AlterEgo::GW2API::GetSkillInfo(current);
        if(info&&info->no_underwater.value_or(false))ImGui::TextWrapped("Current skill is marked NoUnderwater; retained until you change it.");
    }
    if(choices.empty())ImGui::TextDisabled(profession.empty()?"Select a profession first.":
        "No eligible skills available (metadata may be loading).");
    else {
        constexpr int columns=4;
        constexpr float cell=42.0f,gap=4.0f;
        const float width=columns*(cell+gap)+gap*2+ImGui::GetStyle().ScrollbarSize;
        const float height=ImMin(360.0f,ImMax(80.0f,ImGui::GetIO().DisplaySize.y-100.0f));
        if(ImGui::BeginChild("##skill_grid",ImVec2(width,height),true)) {
            for(size_t i=0;i<choices.size();++i) {
                if(i%columns)ImGui::SameLine(0,gap);
                ImGui::PushID(static_cast<int>(choices[i]));
                const auto pos=ImGui::GetCursorScreenPos();
                if(RenderEditableSkillIcon(choices[i],cell)){next=choices[i];selected=true;}
                const auto info=AlterEgo::GW2API::GetSkillInfo(choices[i]);
                if(aquatic&&info&&!info->no_underwater.has_value())ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x+cell-10,pos.y+2),IM_COL32(255,205,90,255),"?");
                if(choices[i]==current)ImGui::GetWindowDrawList()->AddRect(pos,
                    ImVec2(pos.x+cell,pos.y+cell),IM_COL32(255,255,255,255),0,0,3.0f);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    if(AlterEgo::UI::Button("Clear slot",ImVec2(0,0),AlterEgo::UI::ButtonRole::Compact)){
        next=0;selected=true;
    }
    if(selected)ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return selected;
}

// Both editors share the slot icon, context actions and picker; the
// caller owns its draft and the standalone editor alone exposes exact IDs.
static bool RenderSkillEditorSlot(uint32_t current,float size,const std::string& profession,
        const char* role,const std::vector<uint32_t>& specs,bool allowAnySpec,bool aquatic,
        bool advanced,uint32_t& next,const char* label=nullptr,
        AlterEgo::SkillEdit::SlotEvidence evidence=AlterEgo::SkillEdit::SlotEvidence::Unrecorded){
    using E=AlterEgo::SkillEdit::SlotEvidence;
    const bool unresolved=evidence==E::Unresolved;
    // Both editors use the canonical demand owner, independent of hover/picker.
    if(current)AlterEgo::GW2API::FetchSkillDetailsAsync({current},aquatic);
    const auto pos=ImGui::GetCursorScreenPos();
    if(RenderSkillIconCore(current,size,true,false,{},label,evidence==E::Empty,unresolved))ImGui::OpenPopup("##skill_picker");
    const bool hovered=ImGui::IsItemHovered();
    bool changed=false;
    auto choose=[&](uint32_t id){
        const auto info=id?AlterEgo::GW2API::GetSkillInfo(id):nullptr;
        if((id!=current||unresolved)&&(!info||AlterEgo::SkillEdit::EnvironmentChoiceAllowed(aquatic,info->no_underwater))){
            next=id;changed=true;
        }
    };
    if(ImGui::BeginPopupContextItem("##skill_edit_context")){
        const auto info=AlterEgo::GW2API::GetSkillInfo(current);
        ReferenceEntityActions(info?info->name:std::string{},AlterEgo::LINK_SKILL,current);
        if(AlterEgo::UI::MenuAction("Clear Skill",AlterEgo::UI::MenuIcon::Delete,current!=0||unresolved))choose(0);
        if(advanced){
            ImGui::TextDisabled("Advanced: exact skill ID");
            uint32_t exact=current;
            ImGui::SetNextItemWidth(150.0f);
            if(ImGui::InputScalar("Skill ID",ImGuiDataType_U32,&exact))choose(exact);
        }
        if(aquatic&&info&&info->no_underwater.value_or(false))ImGui::TextDisabled("Existing skill is marked NoUnderwater.");
        ImGui::EndPopup();
    }
    if(hovered)ImGui::GetWindowDrawList()->AddRect(pos,ImVec2(pos.x+size,pos.y+size),IM_COL32(227,196,122,255),0,0,2.0f);
    uint32_t chosen=current;
    if(RenderSkillPickerPopup("##skill_picker",profession,role,specs,allowAnySpec,current,chosen,aquatic))choose(chosen);
    return changed;
}

// Both native editors use this field-scoped selector. The catalog does not
// record environment eligibility, so never invent an aquatic restriction.
// Selected slots share geometry and exact entity actions in read and edit modes.
static bool RenderProfessionPairChoice(bool legend,uint32_t current,uint32_t other,uint32_t& next,
                                       bool editable,bool known,bool unresolved,
                                       const std::vector<AlterEgo::SkillEdit::EliteContext>& contexts) {
    using AlterEgo::SkillEdit::PairChoiceAllowed;
    const auto catalog=AlterEgo::PublicCatalog::Get();
    const auto& pets=catalog->pets;
    auto display=ResolveProfessionChoice(legend,current);
    if(!current)display.name=unresolved?"Unresolved selection":known?"Empty":editable?"Choose (not recorded)":"Not recorded";
    auto* texture=AlterEgo::IconManager::GetOrRequestIcon(display.key,display.icon);
    // A single hit target spans icon and wrapped label. Center the label
    // against the icon, retaining the existing icon geometry.
    const ImVec2 pos=ImGui::GetCursorScreenPos(), size(ICON_SIZE,ICON_SIZE);
    const float gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float wrap=std::max(1.0f,ImGui::GetContentRegionAvail().x-size.x-gap);
    const auto labelSize=ImGui::CalcTextSize(display.name.c_str(),nullptr,false,wrap);
    const float rowHeight=std::max(size.y,labelSize.y);
    const bool clicked=ImGui::InvisibleButton("##profession_slot",ImVec2(size.x+gap+std::min(wrap,labelSize.x),rowHeight));
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 iconPos(pos.x,pos.y+(rowHeight-size.y)*.5f);
    const ImVec2 end(iconPos.x+size.x,iconPos.y+size.y);
    if(texture&&texture->Resource)draw->AddImage(texture->Resource,iconPos,end);
    else draw->AddRectFilled(iconPos,end,IM_COL32(40,40,60,200));
    if(editable)draw->AddRect(iconPos,end,IM_COL32(90,80,50,200));
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
        ImVec2(end.x+gap,pos.y+(rowHeight-labelSize.y)*.5f),
        ImGui::GetColorU32(current||editable?ImGuiCol_Text:ImGuiCol_TextDisabled),
        display.name.c_str(),nullptr,wrap);
    if(ImGui::IsItemHovered()){
        RenderCompanionTooltip(display,editable&&display.description.empty()?"Click to choose; right-click for actions.":"");
    }
    if(clicked&&editable)ImGui::OpenPopup("##pair_choice");
    bool changed=false;
    AlterEgo::UI::EntityContext("##pair_context",[&]{
        if(current)ProfessionChoiceActions(legend,display);
        if(editable&&AlterEgo::UI::MenuAction("Clear",AlterEgo::UI::MenuIcon::Delete,current!=0||unresolved)){
            next=0;changed=true;
        }
    });
    if(!editable)return false;
    if(!ImGui::BeginPopup("##pair_choice"))return changed;
    ImGui::TextDisabled("Environment eligibility not verified by catalog.");
    if(AlterEgo::UI::SelectableOption("(None)",current==0)){next=0;changed=true;ImGui::CloseCurrentPopup();}
    static std::string query;
    if(ImGui::IsWindowAppearing())query.clear();
    AlterEgo::UI::SearchField("companion_picker_search",legend?"Search legends...":"Search pets...",query,280.f);
    const auto words=AlterEgo::Search::Words(query);
    struct Option {uint32_t id;std::string label;uint32_t artwork;std::string url;bool disabled,selected;std::string reason;};
    std::vector<Option> options;
    auto add=[&](uint32_t id,const std::string& label,uint32_t artwork,const std::string& url,bool disabled,bool selected,const std::string& reason={}){
        if(AlterEgo::Search::Matches(label,words))options.push_back({id,label,artwork,url,disabled,selected,reason});
    };
    if(legend){
        AlterEgo::GW2API::FetchLegendsAsync();
        auto canonical=[](uint32_t value){return value<=255?static_cast<uint32_t>(
            AlterEgo::PublicCatalog::CanonicalLegendCode(static_cast<uint8_t>(value))):value;};
        for(const auto& l:LegendOptions()){
            const bool selected=canonical(current)==l.code;
            const bool duplicate=!PairChoiceAllowed(canonical(other),l.code);
            const bool incompatible=!AlterEgo::SkillEdit::LegendChoiceAllowed(l.code,contexts);
            const auto* required=AlterEgo::SkillEdit::LegendRequiredElite(l.code);
            const std::string reason=duplicate?"Already selected in the other slot.":
                incompatible&&required?std::string("Requires ")+required+" in every affected Loadout.":"";
            add(l.code,l.name,AlterEgo::ArtworkRequest::SkillKey(l.swap),l.icon,
                duplicate||(!selected&&incompatible),selected,reason);
        }
    }else if(pets.empty()){
        RenderSpinner("Loading pets...");AlterEgo::GW2API::FetchPetsAsync();
    }else for(const auto& p:pets)add(p.id,p.name,AlterEgo::ArtworkRequest::PetKey(p.id),p.icon,
        !PairChoiceAllowed(other,p.id),current==p.id);
    if(!options.empty()){
        namespace UI=AlterEgo::UI;
        const float iconSize=30.f,pitch=UI::CatalogRowHeight(iconSize);
        if(ImGui::BeginChild("##companion_options",ImVec2(280.f,std::min(360.f,pitch*options.size())),true)){
            UI::CatalogLines(static_cast<int>(options.size()),pitch,0.f,[&](int index){
                const auto& option=options[index];
                ImGui::PushID(static_cast<int>(option.id));
                const auto hit=UI::CatalogRow(index,option.selected,option.artwork,option.url,option.label,"",
                    {},option.disabled,-1,[&]{
                        const auto value=ResolveProfessionChoice(legend,option.id);
                        RenderCompanionTooltip(value,!option.reason.empty()?option.reason:
                            option.disabled?"Already selected in the other slot.":"");
                    },iconSize,true);
                UI::EntityContext("##pair_option_actions",[&]{ProfessionChoiceActions(legend,ResolveProfessionChoice(legend,option.id));});
                if(hit.clicked&&!option.disabled){if(!option.selected){next=option.id;changed=true;}ImGui::CloseCurrentPopup();}
                ImGui::PopID();
            });
        }
        ImGui::EndChild();
    }else if(!query.empty())ImGui::TextDisabled("No matching companions.");
    ImGui::EndPopup();
    return changed;
}

// The catalog defines associated skills, not the equipped bar or active
// stance. No synthesis of character-specific utility order or missing variants.
// The same read-only skill renderer retains tooltips and native links.
static void RenderCompanionAssociatedSkills(const ProfessionChoiceDisplay& choice,float size){
    if(choice.skills.empty()){
        ImGui::TextDisabled("%s",choice.skillsKnown?"No associated skills reported":"Associated skills not retrieved");
        return;
    }
    ImGui::TextDisabled("%s",choice.skillsKnown?"Catalog skills (reference)":"Catalog skills (partial)");
    ImGui::PushID("associated_skills");
    const int columns=AlterEgo::SkillEdit::IconColumns(ImGui::GetContentRegionAvail().x,size,ImGui::GetStyle().ItemSpacing.x,static_cast<int>(choice.skills.size()));
    for(size_t i=0;i<choice.skills.size();++i){
        if(i%static_cast<size_t>(columns))ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        const auto& ref=choice.skills[i];
        RenderSkillIconCore(ref.id,size,false,true,{},AlterEgo::PublicCatalog::RoleLabel(ref.role));
        ImGui::PopID();
    }
    ImGui::PopID();
}
// Four positional values: terrestrial 1/2, aquatic 1/2. Each caller owns
// its source projection and commit boundary; rendering does not save data.
static uint32_t CanonicalCompanionIdentity(bool legend,uint32_t id){
    return legend&&id<=255?static_cast<uint32_t>(AlterEgo::PublicCatalog::CanonicalLegendCode(static_cast<uint8_t>(id))):id;
}
static bool CompanionPairConflict(bool legend,const AlterEgo::SkillEdit::CompanionProjection& slots,int env){
    return AlterEgo::SkillEdit::CompanionPairContext(slots,env,[&](uint32_t id){return CanonicalCompanionIdentity(legend,id);})==
        AlterEgo::SkillEdit::PairContext::Incompatible;
}
static AlterEgo::SkillEdit::CompanionProjection LiveCompanionSlots(const AlterEgo::BuildTemplate& bt,bool legend){
    return AlterEgo::SkillEdit::JsonCompanionSlots(LiveCompanionPayload(bt,legend),legend?255u:UINT32_MAX);
}
static uint8_t RenderProfessionPairSection(bool legend,AlterEgo::SkillEdit::CompanionProjection& slots,
                                        bool editable,const std::vector<AlterEgo::SkillEdit::EliteContext>& contexts={}) {
    auto& values=slots.values;
    ImGui::PushID(legend?"legend_pairs":"pet_pairs");
    if(legend)AlterEgo::GW2API::FetchLegendsAsync();
    else AlterEgo::GW2API::FetchPetsAsync();
    // Demand only the selected companions' skills; the canonical skill owner
    // handles cache hits, deduplication, existing claims and retry policy.
    std::vector<uint32_t> missing;
    for(auto id:values)if(id){
        const auto choice=ResolveProfessionChoice(legend,id);
        for(const auto& skill:choice.skills)if(skill.id)missing.push_back(skill.id);
    }
    if(!missing.empty()){
        std::sort(missing.begin(),missing.end());missing.erase(std::unique(missing.begin(),missing.end()),missing.end());
        AlterEgo::GW2API::FetchSkillDetailsAsync(missing);
    }
    uint8_t changed=0;
    for(int env=0;env<2;++env){
        ImGui::TextDisabled("%s",env?"Aquatic":"Terrestrial");
        if(CompanionPairConflict(legend,slots,env))
            ImGui::TextColored(AlterEgo::UI::WarningText,"Duplicate companion choices");
        for(int i=0;i<2;++i){
            const int slot=env*2+i,other=env*2+1-i;
            ImGui::PushID(slot);
            auto next=values[slot];
            const auto otherId=CanonicalCompanionIdentity(legend,values[other]);
            if(RenderProfessionPairChoice(legend,values[slot],values[other],next,editable,slots.Known(slot),slots.Unresolved(slot),contexts)&&
                AlterEgo::SkillEdit::PairChoiceAllowed(otherId,CanonicalCompanionIdentity(legend,next))&&
                (!legend||AlterEgo::SkillEdit::LegendChoiceAllowed(CanonicalCompanionIdentity(legend,next),contexts))&&
                (values[slot]!=next||!slots.Known(slot)||slots.Unresolved(slot))){
                slots.Set(slot,next);changed|=static_cast<uint8_t>(1u<<slot);
            }
            if(values[slot]){
                const auto choice=ResolveProfessionChoice(legend,values[slot]);
                ImGui::Indent(8.0f);
                RenderCompanionAssociatedSkills(choice,30.0f);
                ImGui::Unindent(8.0f);
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
    return changed;
}
// Ranger collection consumes the same immutable public catalog and
// identity-qualified durable projection. Filters never modify ownership.
static void RenderCharacterPetsPanel(const AlterEgo::Character& ch){
    using namespace AlterEgo;
    namespace PO=PetOwnership;
    const auto address=CharacterAddressFor(ch);
    const auto ownership=address?NativeAccounts::CharacterPets(address->Reference()):PO::Collection{};
    const bool buildsVerified=address.has_value()&&ch.scope_stamps[CharacterScopes::Index(CharacterScopes::Builds)]>0;
    const auto membership=buildsVerified?PO::BuildMemberships(ch.build_tabs):PO::Memberships{};
    const auto active=[&](uint32_t id){return PO::InTab(membership,id,ch.active_build_tab);};
    const int viewedTab=g_SelectedBuildTab>=0&&g_SelectedBuildTab<static_cast<int>(ch.build_tabs.size())
        ?ch.build_tabs[g_SelectedBuildTab].tab:0;
    auto viewBuild=[&](int tab){
        if(address&&buildsVerified&&PO::TabIndex(ch.build_tabs,tab)>=0)
            g_PetBuildNavigation=PetBuildNavigation{address->Reference(),tab};
    };
    auto membershipText=[](const PO::Membership& m){
        return std::string(m.aquatic?"Aquatic":"Terrestrial")+" "+std::to_string(m.slot);
    };
    auto tabName=[&](int tab){
        const int index=PO::TabIndex(ch.build_tabs,tab);
        if(index<0)return "Tab "+std::to_string(tab);
        const auto& bt=ch.build_tabs[index];
        return "Tab "+std::to_string(tab)+(bt.name.empty()?"":" - "+bt.name);
    };
    GW2API::FetchPetsAsync();
    const auto catalog=PublicCatalog::Get();
    if(catalog->pets.empty()){RenderSpinner("Loading pet catalog...");return;}
    static std::string search;
    static int filter=0,mode=0;
    static uint32_t selected=0;
    const char* filters[]={"All","Confirmed / observed","Observed","Manually owned","Not owned","Unknown","Active build","Any build"};
    UI::SearchToolbar("ranger_pet_search","Search pets...",search,[&]{
        UI::FilterChips chips;
        if(filter)chips.push_back({"ownership",filters[filter],[]{filter=0;}});
        return chips;
    },[&]{UI::FilterChoice("Ownership",filter,filters,IM_ARRAYSIZE(filters));},[]{filter=0;});
    const char* views[]={"List","Grid"};
    UI::FilterChoice("View",mode,views,IM_ARRAYSIZE(views));
    ImGui::TextDisabled("Ownership is manual or observed in retrieved builds; not a game unlock list.");
    if(!buildsVerified)ImGui::TextDisabled("Build membership unavailable until verified character data is retrieved.");
    if(!address)ImGui::TextDisabled("Verified character identity unavailable; ownership edits disabled.");
    const auto words=Search::Words(search);
    std::vector<const PublicCatalog::Pet*> visible;
    PO::Counts counts;
    for(const auto& pet:catalog->pets){
        const auto evidence=PO::Get(ownership,pet.id);
        counts.Add(evidence);
        if(!PO::Matches(static_cast<PO::Filter>(filter),evidence,active(pet.id),membership.count(pet.id)!=0))continue;
        if(!Search::Matches(pet.name,words))continue;
        visible.push_back(&pet);
    }
    ImGui::TextDisabled("%zu / %zu confirmed or observed | %zu shown",counts.confirmed,catalog->pets.size(),visible.size());
    const float progress=catalog->pets.empty()?0.f:static_cast<float>(counts.confirmed)/catalog->pets.size();
    ImGui::ProgressBar(progress,ImVec2(-1,6),"");
    if(ImGui::IsItemHovered()){
        const std::string details=std::to_string(counts.observed)+" observed; "+std::to_string(counts.manualOwned)+
            " manually owned (may overlap). "+std::to_string(counts.notOwned)+" marked not owned; "+
            std::to_string(counts.unknown)+" unknown. Progress is not an API unlock total.";
        UI::DetailTooltip("Collection evidence",details.c_str());
    }
    if(const auto* pet=PublicCatalog::FindPet(catalog->pets,selected)){
        ImGui::Separator();
        const auto choice=ResolveProfessionChoice(false,pet->id);
        ImGui::TextUnformatted(choice.name.c_str());
        UI::EntityContext("##selected_pet_name",[&]{ProfessionChoiceActions(false,choice);});
        if(!choice.description.empty())ImGui::TextWrapped("%s",choice.description.c_str());
        std::vector<uint32_t> missing;
        for(const auto& skill:choice.skills)if(skill.id)missing.push_back(skill.id);
        if(!missing.empty())GW2API::FetchSkillDetailsAsync(missing);
        RenderCompanionAssociatedSkills(choice,30.f);
        const auto evidence=PO::Get(ownership,pet->id);
        ImGui::TextDisabled("Ownership: %s%s",PO::Label(evidence.State()),evidence.observed&&evidence.manual==PO::Manual::Owned?" (also observed)":"");
        const auto found=membership.find(pet->id);
        if(found!=membership.end()){
            ImGui::TextDisabled("Recorded build membership");
            for(size_t i=0;i<found->second.size();){
                const int tab=found->second[i].tab;
                std::string label=tabName(tab);
                if(tab==ch.active_build_tab)label+=" [Active build]";
                if(tab==viewedTab)label+=" [Viewed]";
                label+=": ";
                size_t end=i;
                for(;end<found->second.size()&&found->second[end].tab==tab;++end){
                    if(end>i)label+=", ";
                    label+=membershipText(found->second[end]);
                }
                ImGui::PushID(tab);
                if(UI::SelectableOption((label+"###pet_build_membership").c_str(),false))viewBuild(tab);
                if(ImGui::IsItemHovered())UI::Hint("View this retrieved build tab in Alter Ego. Does not change the in-game build or summoned pet.");
                UI::EntityContext("##pet_membership_actions",[&]{
                    const int index=PO::TabIndex(ch.build_tabs,tab);
                    if(index>=0){
                        const auto& bt=ch.build_tabs[index];
                        CopyNameMenuItem(bt.name.empty()?"Tab "+std::to_string(tab):bt.name);
                        CopyNativeLinkMenuItem(EncodeBuildTabChatLink(bt),"Copy Build Chat Link");
                        if(UI::MenuAction("View Build Tab",UI::MenuIcon::Open))viewBuild(tab);
                    }
                });
                ImGui::PopID();
                i=end;
            }
        }else ImGui::TextDisabled("%s",buildsVerified?"No recorded build membership":"Build membership unavailable");
        ImGui::Separator();
    }
    auto actions=[&](const PublicCatalog::Pet& pet,const PO::Evidence& evidence){
        ProfessionChoiceActions(false,ResolveProfessionChoice(false,pet.id));
        ImGui::Separator();
        auto save=[&](PO::Manual value){
            if(address&&!NativeAccounts::SetCharacterPetOwnership(*address,pet.id,value)&&APIDefs)
                APIDefs->GUI_SendAlert("Pet ownership could not be saved; refresh the character identity.");
        };
        if(UI::MenuAction("Mark owned",UI::MenuIcon::Edit,address.has_value()&&evidence.manual!=PO::Manual::Owned))save(PO::Manual::Owned);
        if(UI::MenuAction("Mark not owned",UI::MenuIcon::Edit,address.has_value()&&!evidence.observed&&evidence.manual!=PO::Manual::NotOwned,
                evidence.observed?"A verified build has selected this pet.":nullptr))save(PO::Manual::NotOwned);
        if(UI::MenuAction("Clear manual status",UI::MenuIcon::Detach,address.has_value()&&evidence.manual!=PO::Manual::Unknown))save(PO::Manual::Unknown);
    };
    auto hover=[&](const PublicCatalog::Pet& pet,const PO::Evidence& evidence){
        if(!ImGui::IsItemHovered())return;
        std::string body=PO::Label(evidence.State());
        if(evidence.observed&&evidence.manual==PO::Manual::Owned)body+=" (also observed)";
        if(active(pet.id))body+=" | Selected in active build";
        if(const auto found=membership.find(pet.id);found!=membership.end()){
            for(const auto& m:found->second)body+="\n"+tabName(m.tab)+" - "+membershipText(m);
        }
        if(active(pet.id))body+="\nSummoned pet is not reported.";
        RenderCompanionTooltip(ResolveProfessionChoice(false,pet.id),body);
    };
    ImGui::BeginChild("##ranger_pet_catalog",ImVec2(0,0),false);
    if(mode==1){
        const float size=40.f,gap=4.f;
        const float pitch=UI::CatalogGridPitch(size,gap);
        const int cols=UI::CatalogGridColumns(ImGui::GetContentRegionAvail().x,size,gap);
        const int rows=(static_cast<int>(visible.size())+cols-1)/cols;
        UI::CatalogLines(rows,pitch,gap,[&](int r){
            for(int c=0;c<cols;++c){
                const size_t index=static_cast<size_t>(r*cols+c);if(index>=visible.size())break;
                const auto& pet=*visible[index];const auto evidence=PO::Get(ownership,pet.id);
                if(c)ImGui::SameLine(0,gap);
                ImGui::PushID(static_cast<int>(pet.id));
                const auto pos=ImGui::GetCursorScreenPos();const ImVec2 end(pos.x+size,pos.y+size);
                auto* draw=ImGui::GetWindowDrawList();
                const auto choice=ResolveProfessionChoice(false,pet.id);
                const auto hit=UI::CatalogTile("##pet_tile",size,choice.key,choice.icon,selected==pet.id,IM_COL32(40,40,60,200));
                if(hit.clicked)selected=pet.id;
                const auto state=evidence.State();
                const char* marker=state==PO::Status::Owned?"O":state==PO::Status::Observed?"V":state==PO::Status::NotOwned?"N":"?";
                draw->AddRectFilled(ImVec2(pos.x,pos.y+size-14),ImVec2(pos.x+14,end.y),IM_COL32(20,20,20,220));
                draw->AddText(ImVec2(pos.x+3,pos.y+size-14),IM_COL32(240,240,240,255),marker);
                if(active(pet.id))draw->AddCircleFilled(ImVec2(end.x-5,pos.y+5),4,IM_COL32(111,204,122,255));
                hover(pet,evidence);UI::EntityContext("##pet_actions",[&]{actions(pet,evidence);});
                ImGui::PopID();
            }
        });
    }else{
        const float iconSize=30.f;
        UI::CatalogLines(static_cast<int>(visible.size()),UI::CatalogRowHeight(iconSize),0.f,[&](int index){
            const auto& pet=*visible[index];const auto evidence=PO::Get(ownership,pet.id);
            ImGui::PushID(static_cast<int>(pet.id));
            const auto choice=ResolveProfessionChoice(false,pet.id);
            const auto tail=std::string(PO::Label(evidence.State()))+(active(pet.id)?" | Active build":"");
            const auto hit=UI::CatalogRow(index,selected==pet.id,choice.key,choice.icon,pet.name,"",tail,false,-1,
                [&]{hover(pet,evidence);},iconSize,true);
            if(hit.clicked)selected=pet.id;
            UI::EntityContext("##pet_actions",[&]{actions(pet,evidence);});ImGui::PopID();
        });
    }
    ImGui::EndChild();
}
// Same cache-only validator for typed live/saved and resolved component views.
template<class Build>
static nlohmann::json RevenantTypedContext(const Build& build,const AlterEgo::SkillEdit::CompanionProjection& slots){
    using J=nlohmann::json;
    auto bar=[](const auto& b){return J{{"heal",b.heal},{"utilities",{b.utilities[0],b.utilities[1],b.utilities[2]}},{"elite",b.elite}};};
    return J{{"profession",build.profession},{"specializations",J::array({J{{"id",build.specializations[0].spec_id}},
        J{{"id",build.specializations[1].spec_id}},J{{"id",build.specializations[2].spec_id}}})},
        {"legend_codes",slots.values},{"skills",bar(build.terrestrial_skills)},{"aquatic_skills",bar(build.aquatic_skills)}};
}
static void RenderRevenantContextWarnings(const nlohmann::json& raw,const std::string& owner={}){
    for(const auto& issue:AlterEgo::GW2API::RevenantContextWarnings(raw)){
        ImGui::PushStyleColor(ImGuiCol_Text,AlterEgo::UI::WarningText);
        ImGui::TextWrapped("%s%s%s",owner.c_str(),owner.empty()?"":": ",issue.c_str());
        ImGui::PopStyleColor();
    }
}
static void RenderLiveProfessionPairs(const AlterEgo::BuildTemplate& bt) {
    if(bt.profession!="Ranger"&&bt.profession!="Revenant")return;
    const bool legend=bt.profession=="Revenant";
    ImGui::Spacing();RenderSectionHeader(legend?"Legends":"Pets",AlterEgo::UI::SectionGold);
    auto slots=LiveCompanionSlots(bt,legend);
    RenderProfessionPairSection(legend,slots,false);
    if(legend)RenderRevenantContextWarnings(RevenantTypedContext(bt,slots));
}

static const char* InfusionAttributeLabel(const std::string& attribute);

static std::string InfusionPositionLabel(const AlterEgo::GearEdit::InfusionPosition& requirement) {
    if(!requirement.attribute.empty())return std::string("Stat: ")+InfusionAttributeLabel(requirement.attribute);
    if(!requirement.exactId)return "Exact: (choose item)";
    const auto item=AlterEgo::GW2API::GetItemInfo(requirement.exactId);
    const auto id="#"+std::to_string(requirement.exactId);
    return item&&!item->name.empty()?"Exact: "+item->name+" ("+id+")":"Exact: "+id+" (metadata pending)";
}
static bool RenderInfusionPositionSelect(const std::string& label,bool selected) {
    const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x-
        AlterEgo::UI::CompactButtonWidth("X")-ImGui::GetStyle().ItemSpacing.x);
    const bool clicked=ImGui::Selectable(label.c_str(),selected,0,ImVec2(width,0));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",label.c_str());
    return clicked;
}
static bool RenderInfusionRemoveButton() {
    ImGui::SameLine();
    const bool clicked=AlterEgo::UI::SmallButton("X##removeInfusion");
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Remove infusion");
    return clicked;
}
// Same local catalog, matching mode, full uint32 item IDs and display for both
// private gear customization and native component editors.
static void RenderInfusionPositionPicker(AlterEgo::GearEdit::InfusionPosition requirement,
        uint32_t& exactInput,const char* query,float resultsHeight,
        const std::function<void(const AlterEgo::GearEdit::InfusionPosition&)>& apply) {
    const bool flexible=!requirement.attribute.empty();
    if(ImGui::RadioButton("By Attribute",flexible)&&!flexible){requirement.attribute="Power";apply(requirement);}
    ImGui::SameLine();
    if(ImGui::RadioButton("Exact item",!flexible)&&flexible){
        requirement.attribute.clear();exactInput=requirement.exactId;apply(requirement);
    }
    if(!requirement.attribute.empty()){
        ImGui::TextWrapped("Matches any positive amount of this attribute. Agony resistance and cosmetic appearance are ignored.");
        ImGui::BeginChild("##infusionAttributes",ImVec2(0,resultsHeight),true);
        const auto searchWords=AlterEgo::Search::Words(query?query:"");
        for(const auto* key:kInfusionStatNames){
            const auto* label=InfusionAttributeLabel(key);
            if(!AlterEgo::Search::Matches(label,searchWords))continue;
            if(ImGui::Selectable(label,requirement.attribute==key)){requirement.attribute=key;apply(requirement);}
        }
        ImGui::EndChild();
        return;
    }
    ImGui::TextWrapped("%s",InfusionPositionLabel(requirement).c_str());
    ImGui::SetNextItemWidth(130.0f);
    ImGui::InputScalar("Item ID",ImGuiDataType_U32,&exactInput);
    auto setExact=[&](uint32_t id){
        requirement.exactId=id;requirement.attribute.clear();exactInput=id;apply(requirement);
        if(id&&!AlterEgo::GW2API::GetItemInfo(id))AlterEgo::GW2API::FetchItemDetailsAsync({id});
    };
    if(exactInput&&RenderActionButton("Set exact ID"))setExact(exactInput);
    if(query&&*query){
        const auto entries=AlterEgo::GW2API::SearchKnownInfusions(query,100,false);
        ImGui::BeginChild("##knownInfusions",ImVec2(0,resultsHeight),true);
        for(const auto& entry:entries){
            ImGui::PushID(static_cast<int>(entry.second));
            const std::string label=entry.first+" (#"+std::to_string(entry.second)+")";
            if(ImGui::Selectable(label.c_str(),requirement.exactId==entry.second))setExact(entry.second);
            AlterEgo::UI::EntityContext("##infusion_catalog_actions",[&]{CopyEntityMenu(entry.first,AlterEgo::LINK_ITEM,entry.second);});
            ImGui::PopID();
        }
        if(entries.empty())ImGui::TextDisabled("No cached matches. Enter an item ID instead.");
        ImGui::EndChild();
    }else ImGui::TextDisabled("Use Search to filter cached infusion names.");
}

// Clipboard is typed and local to Alter Ego. Copying across slot types never
// transfers an incompatible exact item ID or overwrites a weapon type.
using GearCopyField=AlterEgo::GearEdit::CopyField;
struct GearClipboard {
    bool valid = false;
    GearCopyField field = GearCopyField::All;
    std::string slot;
    AlterEgo::BuildGearSlot data;
    uint32_t relicId = 0;
    std::string relicName;
};
static GearClipboard g_GearClipboard;
static bool GearSlotBlocked(const AlterEgo::SavedBuild& b, const std::string& slot) {
    if (!IsOffhandSlot(slot)) return false;
    auto it = b.gear.find(slot == "WeaponA2" ? "WeaponA1" : "WeaponB1");
    return it != b.gear.end() && IsWeaponTwoHanded(b.profession,it->second.weapon_type);
}
// Both native editors use this same exact-item capacity resolver. Local
// metadata only; rendering and clipboard checks never fetch item details.
static AlterEgo::GearEdit::SocketCapacity ResolveGearInfusionCapacity(
    const std::string& slot,const std::string& profession,const std::string& weaponType,
    uint32_t itemId,bool blocked=false) {
    using namespace AlterEgo::GearEdit;
    const bool two=weaponType.empty()||IsWeaponTwoHanded(profession,weaponType);
    std::optional<size_t> sockets;
    if(itemId){
        const auto info=AlterEgo::GW2API::GetItemInfo(itemId);
        if(info&&info->details.is_object()&&info->details.contains("infusion_slots")&&
            info->details["infusion_slots"].is_array())sockets=info->details["infusion_slots"].size();
    }
    return ResolveSocketCapacity(InfusionCapacity(slot,two,blocked),sockets);
}
static int GearInfusionCapacity(const AlterEgo::SavedBuild& b,const std::string& slot,
    bool* capacityVerified=nullptr) {
    if(capacityVerified)*capacityVerified=false;
    if(AlterEgo::GetInfusionMode(b)!=AlterEgo::InfusionMode::Slots)return 0;
    const auto it=b.gear.find(slot);
    const auto capacity=ResolveGearInfusionCapacity(slot,b.profession,
        it==b.gear.end()?std::string{}:it->second.weapon_type,it==b.gear.end()?0:it->second.item_id,GearSlotBlocked(b,slot));
    if(capacityVerified)*capacityVerified=capacity.verified;
    return capacity.limit;
}
static int GearSigilCapacity(const AlterEgo::SavedBuild& b,const std::string& slot) {
    if(!IsWeaponSlot(slot)||GearSlotBlocked(b,slot))return 0;
    const auto it=b.gear.find(slot);
    if(it==b.gear.end()||it->second.weapon_type.empty())return 0;
    return AlterEgo::GearEdit::SigilCapacity(slot,IsWeaponTwoHanded(b.profession,it->second.weapon_type));
}
static bool GearSameGroup(const std::string& a,const std::string& b) {
    return (IsArmorSlot(a) && IsArmorSlot(b)) ||
           (IsWeaponSlot(a) && IsWeaponSlot(b)) ||
           (IsTrinketSlot(a) && IsTrinketSlot(b));
}
static bool GearCanPaste(const AlterEgo::SavedBuild& b,const std::string& slot,const GearClipboard& c) {
    if (!c.valid || GearSlotBlocked(b,slot)) return false;
    if (c.field == GearCopyField::Relic) return slot == "Relic";
    if (!IsArmorSlot(slot) && !IsTrinketSlot(slot) && !IsWeaponSlot(slot)) return false;
    if (c.field == GearCopyField::Stats) return !IsWeaponSlot(slot) || GearSigilCapacity(b,slot) > 0;
    if (c.field == GearCopyField::Infusions) return AlterEgo::GetInfusionMode(b)==AlterEgo::InfusionMode::Slots && GearInfusionCapacity(b,slot) > 0 &&
        (!IsWeaponSlot(slot) || GearSigilCapacity(b,slot) > 0);
    if (c.field == GearCopyField::Upgrade)
        return (IsArmorSlot(c.slot) && IsArmorSlot(slot)) ||
            (IsWeaponSlot(c.slot) && GearSigilCapacity(b,slot) > 0);
    if (!GearSameGroup(slot,c.slot)) return false;
    if (!IsWeaponSlot(slot) || GearSigilCapacity(b,slot) > 0) return true;
    if (c.data.weapon_type.empty()) return false;
    const auto weapons = AlterEgo::GW2API::GetProfessionWeapons(b.profession);
    if (!weapons) return false;
    const auto it = weapons->find(c.data.weapon_type);
    return it != weapons->end() && AlterEgo::EquipmentChoice::WeaponTypeEligible(
        slot,it->first,it->second.mainhand,it->second.offhand,it->second.two_handed);
}
static void ExpandSharedRune(AlterEgo::SavedBuild& b) {
    if (!b.rune_id && b.rune_name.empty()) return;
    bool explicitRunes = false;
    for (const auto& entry : b.gear)
        if (IsArmorSlot(entry.first) && (entry.second.rune_id || !entry.second.rune.empty()))
            explicitRunes = true;
    if (!explicitRunes) for (auto& entry : b.gear) if (IsArmorSlot(entry.first)) {
        entry.second.rune = b.rune_name; entry.second.rune_id = b.rune_id;
    }
    b.rune_id = 0; b.rune_name.clear();
}
static void MarkGearEditDirty(const AlterEgo::SavedBuild& b) {
    if(&b==&g_GearDialogDraft)g_GearDialogDirty=true;
    else MarkEditDirty();
}
static void ApplyGearClipboard(AlterEgo::SavedBuild& b,const std::string& slot,const GearClipboard& c) {
    if (!GearCanPaste(b,slot,c)) return;
    if (c.field == GearCopyField::Relic) {
        b.relic_id = c.relicId; b.relic_name = c.relicName;
        auto it = b.gear.find("Relic");
        if (it != b.gear.end()) it->second.item_id = c.relicId;
        MarkGearEditDirty(b); return;
    }
    if ((c.field == GearCopyField::All || c.field == GearCopyField::Upgrade) && IsArmorSlot(slot))
        ExpandSharedRune(b);
    auto& dest = b.gear[slot]; dest.slot = slot;
    if (c.field == GearCopyField::All && IsWeaponSlot(slot) && dest.weapon_type.empty()) {
        dest.weapon_type = c.data.weapon_type;
        dest.item_id = 0;
        if (IsMainhandSlot(slot) && IsWeaponTwoHanded(b.profession,dest.weapon_type)) {
            b.gear.erase(slot == "WeaponA1" ? "WeaponA2" : "WeaponB2");
        }
    }
    if (c.field == GearCopyField::All || c.field == GearCopyField::Stats) {
        AlterEgo::GearEdit::CopyStats(dest,c.data);
    }
    if (c.field == GearCopyField::All || c.field == GearCopyField::Upgrade) {
        if (IsArmorSlot(slot)) AlterEgo::GearEdit::CopyRune(dest,c.data);
        if (IsWeaponSlot(slot)) AlterEgo::GearEdit::CopySigils(dest,c.data,GearSigilCapacity(b,slot));
    }
    if ((c.field == GearCopyField::All || c.field == GearCopyField::Infusions) &&
        AlterEgo::GetInfusionMode(b)==AlterEgo::InfusionMode::Slots) {
        bool capacityVerified=false;
        const int capacity=GearInfusionCapacity(b,slot,&capacityVerified);
        if(capacity>0)AlterEgo::GearEdit::CopyInfusions(dest,c.data,capacity,capacityVerified);
    }
    MarkGearEditDirty(b);
}
static void ClearGearSlot(AlterEgo::SavedBuild& b,const std::string& slot,GearCopyField field) {
    if (slot == "Relic") { b.relic_id = 0; b.relic_name.clear(); b.gear.erase(slot); MarkGearEditDirty(b); return; }
    if (IsArmorSlot(slot) && (field == GearCopyField::All || field == GearCopyField::Upgrade)) ExpandSharedRune(b);
    auto it = b.gear.find(slot);
    if (it == b.gear.end()) return;
    if (field == GearCopyField::All) {
        // Do not invalidate a pending child draft through an outside clear.
        if (&b!=&g_GearDialogDraft && g_GearDialogOpen && g_GearDialogBuildId == b.id)return;
        b.gear.erase(it);
    } else {
        auto& item = it->second;
        if (field == GearCopyField::Stats) { item.stat_id = 0; item.stat_name.clear(); }
        if (field == GearCopyField::Upgrade) {
            item.rune_id = item.sigil_id = item.sigil2_id = 0;
            item.rune.clear(); item.sigil.clear(); item.sigil2.clear();
        }
        if (field == GearCopyField::Infusions && AlterEgo::GetInfusionMode(b)==AlterEgo::InfusionMode::Slots) {
            AlterEgo::GearEdit::ClearInfusions(item);
        }
    }
    MarkGearEditDirty(b);
}
static GearClipboard CaptureGearClipboard(const AlterEgo::SavedBuild& b,const std::string& slot,GearCopyField field) {
    GearClipboard c; c.valid = true; c.slot = slot; c.field = field;
    auto it = b.gear.find(slot); if (it != b.gear.end()) c.data = it->second;
    if (IsArmorSlot(slot) && c.data.rune.empty() && !c.data.rune_id) {
        bool any = false;
        for (const auto& entry : b.gear) if (IsArmorSlot(entry.first) &&
            (entry.second.rune_id || !entry.second.rune.empty())) any = true;
        if (!any) { c.data.rune = b.rune_name; c.data.rune_id = b.rune_id; }
    }
    c.relicId = b.relic_id; c.relicName = b.relic_name;
    if(field==GearCopyField::Infusions)
        c.valid=AlterEgo::GearEdit::HasInfusionRequirements(c.data);
    return c;
}
static void CopyGearToGroup(AlterEgo::SavedBuild& b,const GearClipboard& c,const std::string& group) {
    // Terrestrial planning may populate armor/trinkets; optional aquatic
    // slots participate only when they already have recorded equipment.
    std::vector<std::string> slots;
    for (const auto& s : AlterEgo::GearEdit::BulkSlots(b.gear)) {
        if ((c.field == GearCopyField::Infusions || IsWeaponSlot(s)) &&
            !b.gear.count(s)) continue;
        if (IsWeaponSlot(s) && GearSigilCapacity(b,s) == 0) continue;
        if ((s != c.slot || c.field == GearCopyField::Infusions) &&
            (group == "All" || (group == "Armor" && IsArmorSlot(s)) ||
            (group == "Weapons" && IsWeaponSlot(s)) || (group == "Trinkets" && IsTrinketSlot(s))))
            slots.push_back(s);
    }
    for (const auto& slot : slots) ApplyGearClipboard(b,slot,c);
}
static void CopyWeaponSetSigils(AlterEgo::SavedBuild& b,const std::string& source) {
    if (!IsWeaponSlot(source)||AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(source)) return;
    const std::string from = source.substr(0,7), to = from == "WeaponA" ? "WeaponB" : "WeaponA";
    std::vector<std::pair<uint32_t,std::string>> pattern;
    for (const auto& suffix : {"1","2"}) {
        const auto slot = from + suffix;
        auto it = b.gear.find(slot);
        if (it == b.gear.end() || !GearSigilCapacity(b,slot)) continue;
        const auto& s = it->second;
        pattern.push_back({s.sigil_id,s.sigil});
        if (GearSigilCapacity(b,slot) == 2) pattern.push_back({s.sigil2_id,s.sigil2});
    }
    if (pattern.empty()) return;
    size_t index = 0;
    for (const auto& suffix : {"1","2"}) {
        const auto slot = to + suffix;
        const int capacity = GearSigilCapacity(b,slot);
        if (!capacity) continue;
        auto& s = b.gear.at(slot);
        auto first = pattern[index++ % pattern.size()];
        s.sigil_id = first.first; s.sigil = first.second;
        s.sigil2_id = 0; s.sigil2.clear();
        if (capacity == 2) {
            auto second = pattern[index++ % pattern.size()];
            s.sigil2_id = second.first; s.sigil2 = second.second;
        }
    }
    MarkGearEditDirty(b);
}
static void RenderGearEditContext(AlterEgo::SavedBuild& b,const std::string& slot,bool& clearAfter) {
    if (!ImGui::BeginPopupContextItem("##gear_edit_context")) return;
    const bool relic = slot == "Relic";
    const auto saved=b.gear.find(slot);
    if(relic)CopyEntityMenu(b.relic_name,AlterEgo::LINK_ITEM,b.relic_id);
    else if(saved!=b.gear.end()){
        const auto& gear=saved->second;
        const auto actual=gear.item_id?AlterEgo::GW2API::GetItemInfo(gear.item_id):nullptr;
        const auto name=actual&&!actual->name.empty()?actual->name:gear.stat_name.empty()?std::string{}:gear.stat_name+" "+GearSlotDisplayName(slot);
        CopyEntityMenu(name,AlterEgo::LINK_ITEM,gear.item_id);
        CopyNameMenuItem(gear.rune,"Copy Rune Name");
        CopyEntityChatLink(AlterEgo::LINK_ITEM,gear.rune_id,"Copy Rune Chat Link");
        CopyNameMenuItem(gear.sigil,"Copy Sigil Name");
        CopyEntityChatLink(AlterEgo::LINK_ITEM,gear.sigil_id,"Copy Sigil Chat Link");
        CopyNameMenuItem(gear.sigil2,"Copy Second Sigil Name");
        CopyEntityChatLink(AlterEgo::LINK_ITEM,gear.sigil2_id,"Copy Second Sigil Chat Link");
        CopyCachedItemNamesMenu(gear.infusion_ids,"Copy Infusion Name");
        CopyCachedItemLinksMenu(gear.infusion_ids,"Copy Infusion Chat Link");
    }
    auto capture = [&](GearCopyField field) { return CaptureGearClipboard(b,slot,field); };
    ImGui::Separator();
    if (AlterEgo::UI::MenuAction("Edit",AlterEgo::UI::MenuIcon::Edit)) {
        auto c = capture(GearCopyField::All); c.data.slot = slot;
        OpenGearDialog(b.id,slot,c.data);
    }
    ImGui::Separator();
    if (AlterEgo::UI::BeginActionMenu("Copy",AlterEgo::UI::MenuIcon::Copy)) {
        if (ImGui::MenuItem(relic ? "Relic" : "Configuration")) g_GearClipboard = capture(relic ? GearCopyField::Relic : GearCopyField::All);
        if (!relic) {
            if (ImGui::MenuItem("Stats")) g_GearClipboard = capture(GearCopyField::Stats);
            if ((IsArmorSlot(slot) || IsWeaponSlot(slot)) && ImGui::MenuItem("Rune / Sigils")) g_GearClipboard = capture(GearCopyField::Upgrade);
            if (GearInfusionCapacity(b,slot) && ImGui::MenuItem("Infusions")) g_GearClipboard = capture(GearCopyField::Infusions);
        }
        ImGui::EndMenu();
    }
    if (AlterEgo::UI::MenuAction("Paste",AlterEgo::UI::MenuIcon::Paste,GearCanPaste(b,slot,g_GearClipboard)))
        ApplyGearClipboard(b,slot,g_GearClipboard);
    if (!relic) {
        if (AlterEgo::UI::BeginActionMenu("Copy to all slots",AlterEgo::UI::MenuIcon::Copy)) {
            const std::string group = IsArmorSlot(slot) ? "Armor" : IsWeaponSlot(slot) ? "Weapons" : "Trinkets";
            if (ImGui::MenuItem("Configuration")) CopyGearToGroup(b,capture(GearCopyField::All),group);
            if (ImGui::MenuItem("Stats")) CopyGearToGroup(b,capture(GearCopyField::Stats),group);
            if ((IsArmorSlot(slot) || IsWeaponSlot(slot)) && ImGui::MenuItem("Rune / Sigils")) CopyGearToGroup(b,capture(GearCopyField::Upgrade),group);
            if (GearInfusionCapacity(b,slot) && ImGui::BeginMenu("Infusions")) {
                if (ImGui::MenuItem("This item (fill slots)")) ApplyGearClipboard(b,slot,capture(GearCopyField::Infusions));
                for (const auto& target : {"Armor","Weapons","Trinkets","All"})
                    if (ImGui::MenuItem(target)) CopyGearToGroup(b,capture(GearCopyField::Infusions),target);
                ImGui::EndMenu();
            }
            if (IsWeaponSlot(slot) && !AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(slot) && ImGui::MenuItem("Sigils to other weapon set")) CopyWeaponSetSigils(b,slot);
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    if (AlterEgo::UI::BeginActionMenu("Clear",AlterEgo::UI::MenuIcon::Delete)) {
        if (ImGui::MenuItem(relic ? "Relic" : "Entire slot")) clearAfter = true;
        if (!relic) {
            if (ImGui::MenuItem("Stats")) ClearGearSlot(b,slot,GearCopyField::Stats);
            if ((IsArmorSlot(slot) || IsWeaponSlot(slot)) && ImGui::MenuItem("Rune / Sigils")) ClearGearSlot(b,slot,GearCopyField::Upgrade);
            if (GearInfusionCapacity(b,slot) && ImGui::MenuItem("Infusions")) ClearGearSlot(b,slot,GearCopyField::Infusions);
        }
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
}


static void RenderGearCustomizeDialog() {
    if (!g_GearDialogOpen) return;
    if (!g_LibEditMode || g_GearDialogBuildId != g_EditDraft.id) {
        g_GearDialogOpen = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(540, 440), ImGuiCond_FirstUseEver);
    g_GearDialogFocus.ApplyBeforeBegin();
    bool windowOpen=true;
    if (!ImGui::Begin("Customize##GearDialog", &windowOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // The child owns a separate equipment draft; Cancel or the window close
    // button cannot modify the parent Loadout draft.
    AlterEgo::SavedBuild* editBuild=&g_GearDialogDraft;
    auto commitGear = [&]() { MarkGearEditDirty(*editBuild); };

    auto& gs = editBuild->gear[g_GearDialogSlot];
    gs.slot = g_GearDialogSlot;
    bool isArmor = IsArmorSlot(g_GearDialogSlot);
    bool isWeapon = IsWeaponSlot(g_GearDialogSlot);
    bool isRelic = (g_GearDialogSlot == "Relic");

    ImGui::TextDisabled("Save applies to the Loadout draft; save the Loadout to persist.");
    ImGui::BeginChild("##gear_customize_body",ImVec2(0,-AlterEgo::UI::ActionButtonHeight()-ImGui::GetStyle().ItemSpacing.y*2),false);
    // Two-column layout: left = current item, right = selector
    if (ImGui::BeginTable("##custLayout", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("##custLeft", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("##custRight", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // === LEFT: Current slot info ===
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", GearSlotDisplayName(g_GearDialogSlot));
        ImGui::Separator();

        // Weapon type area FIRST (clickable to switch to weapon type tab, weapon slots only)
        if (isWeapon) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = 30.0f;
            ImU32 bg = (g_GearSelectorTab == 2) ? IM_COL32(50, 60, 70, 200) : IM_COL32(35, 35, 40, 150);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 3.0f);
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + w, p.y + h),
                (g_GearSelectorTab == 2) ? IM_COL32(100, 160, 220, 255) : IM_COL32(60, 60, 60, 180), 3.0f);

            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("##weaponTypeArea", ImVec2(w, h))) {
                g_GearSelectorTab = 2;
                memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
            }

            ImVec2 tp(p.x + 6, p.y + 3);
            if (!gs.weapon_type.empty()) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Weapon: %s", WeaponDisplayName(gs.weapon_type));
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(220, 200, 120, 255), buf);
            } else {
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(120, 120, 120, 200), "Weapon: (click to select)");
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4));
        }

        if (isRelic) {
            // Relic display area (clickable to select relic)
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = 44.0f;
            ImU32 bg = (g_GearSelectorTab == 0) ? IM_COL32(50, 60, 70, 200) : IM_COL32(35, 35, 40, 150);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 3.0f);
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + w, p.y + h),
                (g_GearSelectorTab == 0) ? IM_COL32(100, 160, 220, 255) : IM_COL32(60, 60, 60, 180), 3.0f);

            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("##relicArea", ImVec2(w, h))) {
                g_GearSelectorTab = 0;
                memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
            }

            if(ImGui::BeginPopupContextItem("##relic_name")){
                CopyEntityMenu(editBuild->relic_name,AlterEgo::LINK_ITEM,editBuild->relic_id);ImGui::EndPopup();
            }
            ImVec2 tp(p.x + 6, p.y + 3);
            if (!editBuild->relic_name.empty()) {
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(200, 180, 255, 255), editBuild->relic_name.c_str());
                // Show bonus text if we can find it in the list
                for (const auto& entry : GearCatalog("Relic")) {
                    if (editBuild->relic_name == entry.name) {
                        tp.y += 14.0f;
                        ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(150, 150, 150, 220), entry.bonus.c_str());
                        break;
                    }
                }
            } else {
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(120, 120, 120, 200), "Relic: (click to select)");
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4));
        } else {
            // Stat combo (clickable to switch to stats tab)
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            // Dynamic height: stat name line + one line per attribute + padding
            int numAttrs = 0;
            if (gs.stat_id != 0) {
                const auto si = AlterEgo::GW2API::GetItemStatInfo(gs.stat_id);
                if (si) numAttrs = (int)si->attributes.size();
            }
            float h = 6.0f + 16.0f + numAttrs * 13.0f;
            if (h < 30.0f) h = 30.0f;
            ImU32 bg = (g_GearSelectorTab == 0) ? IM_COL32(50, 60, 70, 200) : IM_COL32(35, 35, 40, 150);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 3.0f);
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + w, p.y + h),
                (g_GearSelectorTab == 0) ? IM_COL32(100, 160, 220, 255) : IM_COL32(60, 60, 60, 180), 3.0f);

            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("##statArea", ImVec2(w, h))) {
                g_GearSelectorTab = 0;
                memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
            }

            ImVec2 tp(p.x + 6, p.y + 3);
            if (gs.stat_id != 0 || !gs.stat_name.empty()) {
                const auto label=gs.stat_name.empty()?"Stat #"+std::to_string(gs.stat_id)+" (metadata pending)":gs.stat_name;
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(255, 210, 80, 255), label.c_str());
                const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(gs.stat_id);
                if (statInfo) {
                    for (const auto& a : statInfo->attributes) {
                        tp.y += 13.0f;
                        char buf[64];
                        snprintf(buf, sizeof(buf), "  +%s", FriendlyAttrName(a));
                        ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(100, 200, 100, 255), buf);
                    }
                }
            } else {
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(120, 120, 120, 200), "Stat: (click to select)");
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4));
        }

        // Rune/Sigil area (clickable to switch to rune/sigil tab)
        if (isArmor || isWeapon) {
            bool isTwoHand = isWeapon && editBuild && IsWeaponTwoHanded(editBuild->profession, gs.weapon_type);
            int sigilCount = isWeapon?std::max(1,GearSigilCapacity(*editBuild,g_GearDialogSlot)):1;
            if(isWeapon&&sigilCount<2&&(gs.sigil2_id||!gs.sigil2.empty()))ImGui::TextDisabled("Recorded second sigil exceeds known capacity; retained.");

            for (int sigilIdx = 0; sigilIdx < sigilCount; sigilIdx++) {
                const char* upgradeLabel;
                const std::string* upgradeName;
                if (isArmor) {
                    upgradeLabel = "Rune";
                    upgradeName = &gs.rune;
                } else if (sigilIdx == 0) {
                    upgradeLabel = isTwoHand ? "Sigil 1" : "Sigil";
                    upgradeName = &gs.sigil;
                } else {
                    upgradeLabel = "Sigil 2";
                    upgradeName = &gs.sigil2;
                }

                bool isActiveCard = (g_GearSelectorTab == 1) &&
                    (isArmor || (!g_GearEditingSigil2 && sigilIdx == 0) || (g_GearEditingSigil2 && sigilIdx == 1));

                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                float h = 30.0f;
                ImU32 bg = isActiveCard ? IM_COL32(50, 60, 70, 200) : IM_COL32(35, 35, 40, 150);
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 3.0f);
                ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + w, p.y + h),
                    isActiveCard ? IM_COL32(100, 160, 220, 255) : IM_COL32(60, 60, 60, 180), 3.0f);

                char btnId[32];
                snprintf(btnId, sizeof(btnId), "##upgradeArea%d", sigilIdx);
                ImGui::SetCursorScreenPos(p);
                if (ImGui::InvisibleButton(btnId, ImVec2(w, h))) {
                    g_GearSelectorTab = 1;
                    g_GearEditingSigil2 = (sigilIdx == 1);
                    memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
                }

                if (ImGui::BeginPopupContextItem("##single_upgrade_edit")) {
                    if(upgradeName)CopyNameMenuItem(*upgradeName);
                    CopyEntityChatLink(AlterEgo::LINK_ITEM,isArmor?gs.rune_id:sigilIdx==1?gs.sigil2_id:gs.sigil_id);
                    auto snapshot = CaptureGearClipboard(*editBuild,g_GearDialogSlot,GearCopyField::Upgrade);
                    if (isWeapon) {
                        if (sigilIdx == 1) {
                            snapshot.data.sigil = gs.sigil2; snapshot.data.sigil_id = gs.sigil2_id;
                        }
                        snapshot.data.sigil2.clear(); snapshot.data.sigil2_id = 0;
                    }
                    ImGui::Separator();
                    if (AlterEgo::UI::MenuAction("Copy",AlterEgo::UI::MenuIcon::Copy)) g_GearClipboard = snapshot;
                    const bool compatible = g_GearClipboard.valid &&
                        g_GearClipboard.field == GearCopyField::Upgrade &&
                        GearSameGroup(g_GearClipboard.slot,g_GearDialogSlot);
                    if (AlterEgo::UI::MenuAction("Paste",AlterEgo::UI::MenuIcon::Paste,compatible)) {
                        if (isArmor) {
                            ExpandSharedRune(*editBuild);
                            gs.rune = g_GearClipboard.data.rune; gs.rune_id = g_GearClipboard.data.rune_id;
                        } else if (sigilIdx == 1) {
                            gs.sigil2 = g_GearClipboard.data.sigil; gs.sigil2_id = g_GearClipboard.data.sigil_id;
                        } else {
                            gs.sigil = g_GearClipboard.data.sigil; gs.sigil_id = g_GearClipboard.data.sigil_id;
                        }
                        commitGear();
                    }
                    if (AlterEgo::UI::MenuAction("Copy to All Compatible Slots",AlterEgo::UI::MenuIcon::Copy))
                        CopyGearToGroup(*editBuild,snapshot,isArmor ? "Armor" : "Weapons");
                    ImGui::Separator();
                    if (AlterEgo::UI::MenuAction("Clear",AlterEgo::UI::MenuIcon::Delete)) {
                        if (isArmor) { ExpandSharedRune(*editBuild); gs.rune.clear(); gs.rune_id = 0; }
                        else if (sigilIdx == 1) { gs.sigil2.clear(); gs.sigil2_id = 0; }
                        else { gs.sigil.clear(); gs.sigil_id = 0; }
                        commitGear();
                    }
                    ImGui::EndPopup();
                }
                ImVec2 tp(p.x + 6, p.y + 3);
                if (!upgradeName->empty()) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s: %s", upgradeLabel, upgradeName->c_str());
                    ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(200, 180, 255, 255), buf);
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s: (click to select)", upgradeLabel);
                    ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(120, 120, 120, 200), buf);
                }
                tp.y += 14.0f;
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4));
            }
        }

        // Infusion requirements are per physical equipment slot. Captured
        // infusions remain exact by default; newly added requirements default
        // to a stat and leave the exact ID empty.
        if (!isRelic && g_GearDialogSlot != "Amulet" &&
            AlterEgo::GetInfusionMode(*editBuild)==AlterEgo::InfusionMode::Slots) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Infusions");
            for(size_t i=0;i<AlterEgo::GearEdit::InfusionPositionCount(gs);++i){
                const auto req=AlterEgo::GearEdit::InfusionPositionAt(gs,i);
                const auto& attr=req.attribute;const auto id=req.exactId;
                const auto item=id?AlterEgo::GW2API::GetItemInfo(id):nullptr;
                const auto label="Slot "+std::to_string(i+1)+": "+InfusionPositionLabel(req);
                ImGui::PushID(static_cast<int>(i));
                if(RenderInfusionPositionSelect(label,g_GearSelectorTab==3&&g_GearEditingInfusionIndex==static_cast<int>(i))){
                    g_GearSelectorTab = 3;
                    g_GearEditingInfusionIndex = static_cast<int>(i);
                    g_GearExactInfusionId = id;
                    memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
                }
                bool removeInfusion = false;
                if (ImGui::BeginPopupContextItem("##single_infusion_edit")) {
                    CopyEntityMenu(item?item->name:std::string{},AlterEgo::LINK_ITEM,id);
                    auto snapshot = CaptureGearClipboard(*editBuild,g_GearDialogSlot,GearCopyField::Infusions);
                    snapshot.data.infusion_ids = {id};
                    snapshot.data.infusion_attributes = {attr};
                    snapshot.valid=id||!attr.empty();
                    if(item)ImGui::Separator();
                    if (AlterEgo::UI::MenuAction("Copy",AlterEgo::UI::MenuIcon::Copy,snapshot.valid))
                        g_GearClipboard = snapshot;
                    const bool compatible = g_GearClipboard.valid &&
                        g_GearClipboard.field == GearCopyField::Infusions &&
                        AlterEgo::GearEdit::InfusionPositionCount(g_GearClipboard.data)>0;
                    if (AlterEgo::UI::MenuAction("Paste",AlterEgo::UI::MenuIcon::Paste,compatible)) {
                        if(AlterEgo::GearEdit::SetInfusionPosition(gs,i,AlterEgo::GearEdit::InfusionPositionAt(g_GearClipboard.data,0)))commitGear();
                    }
                    if (AlterEgo::UI::MenuAction("Fill This Item",AlterEgo::UI::MenuIcon::Copy,snapshot.valid))
                        ApplyGearClipboard(*editBuild,g_GearDialogSlot,snapshot);
                    if (AlterEgo::UI::BeginActionMenu("Copy to All Slots",AlterEgo::UI::MenuIcon::Copy,snapshot.valid)) {
                        for (const auto& group : {"Armor","Weapons","Trinkets","All"})
                            if (ImGui::MenuItem(group)) CopyGearToGroup(*editBuild,snapshot,group);
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (AlterEgo::UI::MenuAction("Clear",AlterEgo::UI::MenuIcon::Delete)) removeInfusion = true;
                    ImGui::EndPopup();
                }
                const bool clickedRemove=RenderInfusionRemoveButton();
                if (clickedRemove || removeInfusion) {
                    if(AlterEgo::GearEdit::RemoveInfusion(gs,i)){
                        g_GearEditingInfusionIndex=AlterEgo::GearEdit::InfusionSelectionAfterRemoval(
                            g_GearEditingInfusionIndex,i,AlterEgo::GearEdit::InfusionPositionCount(gs));
                        g_GearExactInfusionId=g_GearEditingInfusionIndex>=0
                            ?AlterEgo::GearEdit::InfusionPositionAt(gs,g_GearEditingInfusionIndex).exactId:0;
                        if(g_GearEditingInfusionIndex<0&&g_GearSelectorTab==3)g_GearSelectorTab=0;
                        commitGear();
                    }
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            bool socketCountVerified=false;
            const int maxSlots = GearInfusionCapacity(*editBuild,g_GearDialogSlot,&socketCountVerified);
            if(maxSlots>0&&!socketCountVerified)ImGui::TextDisabled("Socket capacity unverified (maximum %d).",maxSlots);
            if(AlterEgo::GearEdit::InfusionPositionCount(gs)>static_cast<size_t>(maxSlots))ImGui::TextDisabled("Recorded requirements exceed capacity; retained.");
            if (AlterEgo::GearEdit::InfusionPositionCount(gs)<static_cast<size_t>(maxSlots)&&
                RenderActionButton("+ Add infusion")&&AlterEgo::GearEdit::AddInfusionPosition(gs,maxSlots)) {
                g_GearEditingInfusionIndex=static_cast<int>(AlterEgo::GearEdit::InfusionPositionCount(gs)-1);
                g_GearExactInfusionId = 0;
                g_GearSelectorTab = 3;
                memset(g_GearStatSearch, 0, sizeof(g_GearStatSearch));
                commitGear();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Visible bulk buttons and context menus share the same operations.
        if (gs.stat_id != 0 && IsArmorSlot(g_GearDialogSlot) &&
            RenderActionButton("Apply Stat to All Armor"))
            CopyGearToGroup(*editBuild,CaptureGearClipboard(*editBuild,g_GearDialogSlot,GearCopyField::Stats),"Armor");
        if (IsArmorSlot(g_GearDialogSlot) && (!gs.rune.empty()||gs.rune_id) &&
            RenderActionButton("Apply Rune to All Armor"))
            CopyGearToGroup(*editBuild,CaptureGearClipboard(*editBuild,g_GearDialogSlot,GearCopyField::Upgrade),"Armor");
        if (gs.stat_id != 0 && IsTrinketSlot(g_GearDialogSlot) &&
            RenderActionButton("Apply Stat to All Trinkets"))
            CopyGearToGroup(*editBuild,CaptureGearClipboard(*editBuild,g_GearDialogSlot,GearCopyField::Stats),"Trinkets");

        // === RIGHT: Selector panel ===
        ImGui::TableNextColumn();

        // Search bar (shared between tabs)
        AlterEgo::UI::SearchField("gear_catalog", "Search...", g_GearStatSearch);

        if (g_GearSelectorTab == 0 && isRelic) {
            ImGui::BeginChild("##relicList",ImVec2(0,-30),true);
            RenderGearCatalogChoices("Relic",g_GearStatSearch,editBuild->relic_id,editBuild->relic_name,true,
                [&](uint32_t id,const std::string& name){editBuild->relic_id=id;editBuild->relic_name=name;commitGear();});
            ImGui::EndChild();
        } else if (g_GearSelectorTab == 0) {
            ImGui::BeginChild("##statList",ImVec2(0,-30),true);
            RenderGearStatChoices(g_GearStatSearch,g_GearSelectedStatId,true,[&](uint32_t id,const std::string& name){
                g_GearSelectedStatId=id;gs.stat_id=id;gs.stat_name=name;commitGear();
            });
            ImGui::EndChild();
        } else if (g_GearSelectorTab == 1) {
            ImGui::BeginChild("##upgradeList",ImVec2(0,-30),true);
            if(isArmor)RenderGearCatalogChoices("Rune",g_GearStatSearch,gs.rune_id,gs.rune,true,
                [&](uint32_t id,const std::string& name){ExpandSharedRune(*editBuild);gs.rune_id=id;gs.rune=name;commitGear();});
            else if(isWeapon)RenderGearCatalogChoices("Sigil",g_GearStatSearch,
                g_GearEditingSigil2?gs.sigil2_id:gs.sigil_id,g_GearEditingSigil2?gs.sigil2:gs.sigil,true,
                [&](uint32_t id,const std::string& name){
                    if(g_GearEditingSigil2){gs.sigil2_id=id;gs.sigil2=name;}
                    else{gs.sigil_id=id;gs.sigil=name;}
                    commitGear();
                });
            ImGui::EndChild();
        } else if (g_GearSelectorTab == 2 && isWeapon) {
            // --- Weapon type selector ---
            AlterEgo::GW2API::FetchProfessionPaletteAsync(editBuild->profession);
            const auto profWeapons = AlterEgo::GW2API::GetProfessionWeapons(editBuild->profession);
            if (!profWeapons) {
                RenderSpinner("Loading weapon data...");
            } else {
                ImGui::BeginChild("##weaponTypeList",ImVec2(0,-30),true);
                RenderWeaponTypeChoices(editBuild->profession,g_GearDialogSlot,gs.weapon_type,g_GearStatSearch,true,
                    [&](const std::string& name,const auto& wd){
                        const auto plan=AlterEgo::EquipmentChoice::PlanWeaponChange(g_GearDialogSlot,gs.weapon_type,name,wd.two_handed);
                        AlterEgo::GearEdit::ApplyWeaponChange(gs,name,plan);
                        if(!plan.displacedOffhand.empty())editBuild->gear.erase(plan.displacedOffhand);
                        FetchChosenWeaponMetadata(wd);
                        const auto id=GetLegendaryWeaponId(name);
                        if(id)AlterEgo::GW2API::FetchItemDetailsAsync({id});
                        commitGear();
                    });
                ImGui::EndChild();
            }
        }

        else if(g_GearSelectorTab==3&&!isRelic&&g_GearEditingInfusionIndex>=0&&
                static_cast<size_t>(g_GearEditingInfusionIndex)<AlterEgo::GearEdit::InfusionPositionCount(gs)){
            const auto index=static_cast<size_t>(g_GearEditingInfusionIndex);
            ImGui::Text("Infusion slot %d",g_GearEditingInfusionIndex+1);
            RenderInfusionPositionPicker(AlterEgo::GearEdit::InfusionPositionAt(gs,index),
                g_GearExactInfusionId,g_GearStatSearch,-30,[&](const auto& req){
                    if(AlterEgo::GearEdit::SetInfusionPosition(gs,index,req))commitGear();
                });
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();
    const bool dirty=GearDraftDirty();
    const auto action=AlterEgo::UI::SaveCancel("gear_customize",dirty,"Apply","No equipment changes to apply.");
    if(action==AlterEgo::UI::EditAction::Save){
        AlterEgo::GearEdit::ApplyEquipmentDraft(g_EditDraft,g_GearDialogDraft);
        MarkEditDirty();
        g_GearDialogOpen=false;
    }else if(AlterEgo::UI::DiscardRequested("Discard gear changes?",!windowOpen||action==AlterEgo::UI::EditAction::Cancel||AlterEgo::UI::EditorEscape(),dirty)){
        g_GearDialogOpen=false;
    }
    ImGui::End();
}

// Save to Library: destination and name precede the relevant source selectors.
static void RenderSaveToLibraryDialog() {
    if (!g_SaveLibDialogOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
    g_SaveLibDialogFocus.ApplyBeforeBegin();
    bool windowOpen=true;
    if (!ImGui::Begin("Save to Library##SaveLibDialog", &windowOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    using namespace AlterEgo;
    auto sourceChar=BoundCharacter(g_SaveLibAddress);
    ImGui::Text("Character: %s (%s)",g_SaveLibCharName.c_str(),
        sourceChar?sourceChar->profession.c_str():"unavailable");
    auto field=[](const char* label) {
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::SameLine(130);
    };
    auto width=[](){return ImMax(1.0f,ImGui::GetContentRegionAvail().x);};
    field("Save as");
    const char* targets[]={"Complete Loadout","New component","Update existing component","Fashion"};
    if(RenderThemedCombo("##save_target",targets[g_SaveLibTarget],width())) {
        for(int i=0;i<IM_ARRAYSIZE(targets);++i)if(UI::SelectableOption(targets[i],g_SaveLibTarget==i)) {
            if(g_SaveLibTarget!=i){g_SaveLibUpdateId.clear();g_SaveLibFashionError.clear();}
            g_SaveLibTarget=i;
            if(i==2&&g_SaveLibComponentKind==kSaveEntireEquipmentSet)g_SaveLibComponentKind=0;
        }
        ImGui::EndCombo();
    }
    static const char* kinds[]={"Base Build","Skills","Weapons","Sigils","Armor","Runes",
        "Trinkets","Relic","Infusions","Aquatic","Pets","Legends","Equipment Set"};
    static const char* keys[]={"base_build","skills","weapons","sigils","armor","runes",
        "trinkets","relic","infusions","aquatic","pets","legends"};
    const bool fashion=g_SaveLibTarget==kSaveFashionTarget;
    if(g_SaveLibTarget!=0&&!fashion) {
        field("Component");
        if(RenderThemedCombo("##save_component_type",kinds[g_SaveLibComponentKind],width())) {
            auto option=[&](int i) {
                if(UI::SelectableOption(kinds[i],g_SaveLibComponentKind==i)) {
                    if(i!=g_SaveLibComponentKind)g_SaveLibUpdateId.clear();
                    g_SaveLibComponentKind=i;
                }
            };
            ImGui::TextDisabled("BUILD COMPONENTS");option(0);option(1);
            if(sourceChar&&Composable::KindApplies("pets",sourceChar->profession))option(10);
            if(sourceChar&&Composable::KindApplies("legends",sourceChar->profession))option(11);
            ImGui::Separator();ImGui::TextDisabled("EQUIPMENT COMPONENTS");
            if(g_SaveLibTarget==1)option(kSaveEntireEquipmentSet);
            for(int i:{2,4,6,5,3,8,7,9})option(i);
            ImGui::EndCombo();
        }
    }
    const bool entireSet=g_SaveLibTarget==1&&g_SaveLibComponentKind==kSaveEntireEquipmentSet;
    const bool componentBuild=g_SaveLibComponentKind<2||g_SaveLibComponentKind==10||g_SaveLibComponentKind==11;
    const bool needsBuild=!fashion&&(g_SaveLibTarget==0||componentBuild);
    const bool needsEquipment=fashion||g_SaveLibTarget==0||!componentBuild;
    auto nameForBuild=[](const BuildTemplate& bt) {
        return bt.name.empty()?"Tab "+std::to_string(bt.tab):bt.name;
    };
    auto equipName=[&](int tab) {
        if(sourceChar) {
            const auto it=sourceChar->equipment_tab_names.find(tab);
            if(it!=sourceChar->equipment_tab_names.end()&&!it->second.empty())return it->second;
        }
        return "Tab "+std::to_string(tab);
    };
    auto currentBuild=[&]() -> const BuildTemplate* {
        if(sourceChar)for(const auto& bt:sourceChar->build_tabs)
            if(bt.tab==g_SaveLibBuildTab)return &bt;
        return nullptr;
    };
    auto hasEquipment=[&](int tab) {
        return sourceChar&&std::any_of(sourceChar->equipment.begin(),sourceChar->equipment.end(),
            [&](const auto& item){return item.id!=0&&IsEquipmentInCapturedBuild(item.slot,item.tab,tab);});
    };
    if(needsBuild&&sourceChar&&g_SaveLibBuildTab==0&&!sourceChar->build_tabs.empty())
        g_SaveLibBuildTab=sourceChar->build_tabs[ActiveBuildIndex(*sourceChar)].tab;
    // Explicitly distinguish creating an independent object from updating a
    // shared one. Display reference usage before allowing replacement.
    std::string existingComponentName;
    int existingComponentUses = 0;
    if (g_SaveLibTarget == 2) {
        nlohmann::json snapshot;
        if (AlterEgo::GW2API::GetComposableLoadouts(snapshot) &&
            snapshot.contains("components") && snapshot["components"].is_object()) {
            const std::string kind = keys[g_SaveLibComponentKind];
            auto components = snapshot["components"].find(kind);
            if (components != snapshot["components"].end() && components->is_array()) {
                for (const auto& comp : *components)
                    if (comp.is_object() &&
                        comp.value("id", std::string()) == g_SaveLibUpdateId)
                        existingComponentName = comp.value("name", std::string());
                if (snapshot.contains("loadouts") && snapshot["loadouts"].is_array())
                    for (const auto& loadout : snapshot["loadouts"])
                        if (loadout.is_object() && loadout.contains("refs") &&
                            loadout["refs"].is_object() &&
                            loadout["refs"].value(kind, std::string()) == g_SaveLibUpdateId)
                            ++existingComponentUses;
                if(snapshot.contains("equipment_sets"))
                    for(const auto& set:snapshot.at("equipment_sets"))
                        if(!g_SaveLibUpdateId.empty()&&set.at("refs").value(kind,std::string())==g_SaveLibUpdateId)++existingComponentUses;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Replace");
                ImGui::SameLine(130);
                ImGui::SetNextItemWidth(ImMax(140.f, ImGui::GetContentRegionAvail().x));
                if (RenderThemedCombo("##replace_saved_component",
                    existingComponentName.empty() ? "(select component)" :
                    existingComponentName.c_str(),
                    ImMax(140.f, ImGui::GetContentRegionAvail().x))) {
                    for (const auto& component : *components) {
                        if (!component.is_object()) continue;
                        const std::string candidateId = component.value("id", std::string());
                        const std::string candidateName = component.value("name", candidateId);
                        bool compatible = true;
                        const std::string profession = sourceChar ? sourceChar->profession : "";
                        if (kind == "base_build" && component.contains("data") &&
                            component["data"].is_object()) {
                            const std::string prior = component["data"].value(
                                "profession", std::string());
                            if (!prior.empty() && !profession.empty() &&
                                prior != profession) compatible = false;
                        }
                        if (kind == "skills" && !profession.empty() &&
                            snapshot.contains("loadouts") && snapshot["loadouts"].is_array()) {
                            for (const auto& loadout : snapshot["loadouts"]) {
                                if (!loadout.is_object() ||
                                    !loadout.contains("refs") ||
                                    !loadout["refs"].is_object()) continue;
                                const auto& refs = loadout["refs"];
                                if (refs.value("skills", std::string()) != candidateId) continue;
                                const auto base = snapshot["components"].find("base_build");
                                if (base == snapshot["components"].end() || !base->is_array())
                                    continue;
                                for (const auto& item : *base)
                                    if (item.is_object() &&
                                        item.value("id", std::string()) ==
                                            refs.value("base_build", std::string()) &&
                                        item.contains("data") && item["data"].is_object()) {
                                        const std::string prior = item["data"].value(
                                            "profession", std::string());
                                        if (!prior.empty() && prior != profession)
                                            compatible = false;
                                    }
                            }
                        }
                        if (!compatible) continue;
                        ImGui::PushID(candidateId.c_str());
                        if (UI::EntityOption((candidateName + "##" + candidateId).c_str(),candidateId == g_SaveLibUpdateId,[&]{CopyNameMenuItem(candidateName,"Copy Name",true);})) {
                            g_SaveLibUpdateId = candidateId;
                            existingComponentName = candidateName;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }else if(!existingComponentName.empty())NameOptionContext(existingComponentName);
            }
        }
        ImGui::TextDisabled("Updates the stored fields without changing its existing name or ID.");
        if (existingComponentUses > 0)
            ImGui::TextColored(ImVec4(.95f,.78f,.40f,1),
                "SHARED: affects %d referencing record%s.",
                existingComponentUses, existingComponentUses == 1 ? "" : "s");
    } else {
        if(sourceChar&&!g_SaveLibNameManuallyEdited) {
            const auto* bt=currentBuild();
            const std::string source=needsBuild?(bt?nameForBuild(*bt):"Build"):equipName(g_SaveLibEquipTab);
            std::string suggested=sourceChar->name+" - "+source;
            if(g_SaveLibTarget==1)suggested+=" - "+std::string(entireSet?"Equipment Set":kinds[g_SaveLibComponentKind]);
            else if(fashion)suggested+=" - Fashion";
            snprintf(g_SaveLibName,sizeof(g_SaveLibName),"%s",suggested.c_str());
        }
        field("Name");
        ImGui::SetNextItemWidth(width());
        if(UI::InputText("##savelib_name",g_SaveLibName,sizeof(g_SaveLibName)))g_SaveLibNameManuallyEdited=true;
    }
    if(g_SaveLibTarget==0) {
        ImGui::TextUnformatted("Game modes");RenderGameModeSelector("live-save-modes",g_SaveLibModes);
    }
    ImGui::Spacing();ImGui::Separator();
    if(needsBuild) {
        field("Build tab");
        const auto* bt=currentBuild();
        const std::string shown=bt?nameForBuild(*bt):"(unavailable)";
        if(RenderThemedCombo("##save_build_tab",shown.c_str(),width())) {
            if(sourceChar)for(const auto& candidate:sourceChar->build_tabs) {
                const std::string label=nameForBuild(candidate)+(candidate.is_active?" (Active)":"")+
                    "##build_"+std::to_string(candidate.tab);
                ImGui::PushID(candidate.tab);
                if(UI::EntityOption(label.c_str(),candidate.tab==g_SaveLibBuildTab,[&]{CopyNameMenuItem(candidate.name,"Copy Name",true);CopyNativeLinkMenuItem(EncodeBuildTabChatLink(candidate),"Copy Build Chat Link");}))g_SaveLibBuildTab=candidate.tab;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }else if(bt){ImGui::PushID(bt->tab);UI::EntityContext("##save_build_context",[&]{CopyNameMenuItem(bt->name);CopyNativeLinkMenuItem(EncodeBuildTabChatLink(*bt),"Copy Build Chat Link");});ImGui::PopID();}
    }
    if(needsEquipment) {
        field("Equipment tab");
        const std::string shown=equipName(g_SaveLibEquipTab);
        if(RenderThemedCombo("##save_equip_tab",shown.c_str(),width())) {
            if(sourceChar)for(int t=1;t<=GetRetrievedEquipmentTabCount(*sourceChar);++t) {
                const std::string label=equipName(t)+(t==sourceChar->active_equipment_tab?" (Active)":"")+
                    "##equip_"+std::to_string(t);
                ImGui::PushID(t);
                if(UI::EntityOption(label.c_str(),t==g_SaveLibEquipTab,[&]{CopyNameMenuItem(equipName(t),"Copy Name",true);}))g_SaveLibEquipTab=t;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }else if(sourceChar&&g_SaveLibEquipTab>0)NameOptionContext(shown);
    }
    if(g_SaveLibTarget==0) {
        field("Equipment storage");
        const char* storageModes[]={"Offer matching set","Independent components","Create named set"};
        if(RenderThemedCombo("##equipment_storage",storageModes[g_SaveLibEquipmentMode],width())) {
            for(int i=0;i<3;++i)if(UI::SelectableOption(storageModes[i],g_SaveLibEquipmentMode==i))g_SaveLibEquipmentMode=i;
            ImGui::EndCombo();
        }
        if(g_SaveLibEquipmentMode==2) {
            if(g_SaveLibSetName.empty())g_SaveLibSetName=std::string(g_SaveLibName)+" - Equipment";
            field("Set name");ImGui::SetNextItemWidth(width());UI::InputText("##new_set_name",g_SaveLibSetName);
        }
    }
    if(entireSet||(g_SaveLibTarget==0&&g_SaveLibEquipmentMode==2)) {
        ImGui::TextUnformatted("Included categories");
        for(int i=2;i<10;++i) {
            bool included=g_SaveLibSetKinds.count(keys[i])!=0;
            if(ImGui::Checkbox(kinds[i],&included)){if(included)g_SaveLibSetKinds.insert(keys[i]);else g_SaveLibSetKinds.erase(keys[i]);}
        }
    }
    auto finishLoadout=[&](SavedBuild capture,const std::string& setId,const std::string& newName) {
        const int idx=GW2API::AddSavedBuild(std::move(capture),setId,newName,g_SaveLibSetKinds);
        if(idx>=0){SelectLoadoutAt(idx);g_LibDetailsFetched=false;NavigateToLibraryCollection(0);g_SaveLibDialogOpen=false;}
        if(idx<0)g_SaveLibError="Could not save Loadout; review equipment selection. Draft retained.";
        if(APIDefs&&idx>=0)APIDefs->GUI_SendAlert("Loadout saved to library!");
        return idx>=0;
    };
    const bool ready=sourceChar&&(!needsBuild||currentBuild())&&(!needsEquipment||hasEquipment(g_SaveLibEquipTab))&&
        (g_SaveLibTarget==2?!existingComponentName.empty():!Composable::ComponentNameKey(g_SaveLibName).empty())&&
        (g_SaveLibTarget!=0||g_SaveLibEquipmentMode!=2||!Composable::ComponentNameKey(g_SaveLibSetName).empty())&&
        (!(entireSet||(g_SaveLibTarget==0&&g_SaveLibEquipmentMode==2))||!g_SaveLibSetKinds.empty());
    if(!sourceChar)ImGui::TextDisabled("Character source changed or is unavailable. Cancel and reopen capture.");
    else if(needsBuild&&!currentBuild())ImGui::TextDisabled("Select a retrieved build tab.");
    else if(needsEquipment&&!hasEquipment(g_SaveLibEquipTab))ImGui::TextDisabled("No retrieved equipment in this tab.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const auto captureAction=UI::SaveCancel("capture",ready,g_SaveLibTarget==2?"Review Update":"Save",
        "Complete the required capture fields.",false,g_SaveLibError.c_str());
    const bool saveClicked=captureAction==UI::EditAction::Save;
    if(saveClicked&&ready&&fashion) {
        g_SaveLibFashionError.clear();
        if(!CaptureLiveFashion(*sourceChar,g_SaveLibEquipTab,g_SaveLibName,g_SaveLibPendingFashion))
            g_SaveLibFashionError="No loaded equipment skins for that tab. Refresh character data and try again.";
        else if(g_FashionLibrary.FindExact(g_SaveLibPendingFashion.wardrobe_link)>=0)
            ImGui::OpenPopup("Existing Fashion##save_library");
        else if(SaveNewFashion(g_SaveLibPendingFashion))g_SaveLibDialogOpen=false;
        else g_SaveLibFashionError="Could not save Fashion. Original library data is preserved.";
    }
    if(saveClicked&&ready&&!fashion) {
        g_SaveLibError.clear();
        std::string name = g_SaveLibName;
        if (name.empty()) name = g_SaveLibTarget == 0 ? "Unnamed Loadout" : "Unnamed Component";

        const auto* capturedTab=currentBuild();
        const BuildTemplate emptyBuild{};
        if(sourceChar&&(!needsBuild||capturedTab)) {
            const std::string profession=capturedTab&&!capturedTab->profession.empty()?capturedTab->profession:sourceChar->profession;
            const std::string chatLink=needsBuild&&capturedTab&&GW2API::HasPaletteData(profession)?
                EncodeBuildTabChatLink(*capturedTab):std::string();
            SavedBuild sb=CaptureRetrievedBuildSnapshot(*sourceChar,needsBuild?*capturedTab:emptyBuild,
                g_SaveLibEquipTab,needsEquipment,name,g_SaveLibModes.front(),chatLink);
            sb.game_modes = g_SaveLibModes;

            if (g_SaveLibTarget == 2) {
                if (g_SaveLibUpdateId.empty()) {
                    if (APIDefs) APIDefs->GUI_SendAlert("Select a component to update.");
                } else {
                    g_SaveLibPendingCapture = std::move(sb);
                    g_SaveLibPendingKind = keys[g_SaveLibComponentKind];
                    g_SaveLibPendingId = g_SaveLibUpdateId;
                    g_SaveLibPendingName = existingComponentName;
                    g_SaveLibUpdateConfirmPending = true;
                }
            } else if(entireSet) {
                std::string savedId;
                if(GW2API::SaveCapturedEquipmentSet(sb,name,&savedId,g_SaveLibSetKinds)) {
                    g_EquipmentSetSelectedId=savedId;
                    NavigateToLibraryCollection(3);
                    g_SaveLibDialogOpen=false;
                    if(APIDefs)APIDefs->GUI_SendAlert("Equipment set saved.");
                } else if(APIDefs)APIDefs->GUI_SendAlert("Could not save equipment set.");
            } else if (g_SaveLibTarget == 1) {
                std::string savedId;
                const bool saved = AlterEgo::GW2API::SaveCapturedLoadoutComponent(
                    sb, keys[g_SaveLibComponentKind], name, &savedId,
                    !g_SaveLibNameManuallyEdited);
                if (saved) {
                    g_ComponentPendingKind = g_SaveLibComponentKind;
                    g_ComponentSelectIdPending = savedId;
                    g_ComponentSelectNamePending.clear();
                    NavigateToLibraryCollection(2);
                    g_SaveLibDialogOpen = false;
                    if (APIDefs) APIDefs->GUI_SendAlert("Component saved or existing identical component reused.");
                } else g_SaveLibError="Could not save the selected component. Draft retained.";
            } else {
                if(g_SaveLibEquipmentMode==0) {
                    g_SaveLibExactSets=GW2API::FindCapturedEquipmentSets(sb);
                    if(g_SaveLibExactSets.empty()) {
                        finishLoadout(std::move(sb),"","");
                    } else {
                        g_SaveLibPendingCapture=std::move(sb);
                        g_SaveLibSelectedSet=g_SaveLibExactSets.front().first;
                        ImGui::OpenPopup("Link matching equipment set");
                    }
                } else finishLoadout(std::move(sb),"",g_SaveLibEquipmentMode==2?g_SaveLibSetName:std::string());

            }
        } else {
            ImGui::OpenPopup("Live build unavailable##save");
        }
    }
    if(g_SaveLibDialogOpen&&UI::DiscardRequested("Discard capture changes?",
        !windowOpen||captureAction==UI::EditAction::Cancel||UI::EditorEscape(),
        CaptureFormSnapshot()!=g_SaveLibBaseline))g_SaveLibDialogOpen=false;
    if(ImGui::BeginPopupModal("Link matching equipment set",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        for(const auto& set:g_SaveLibExactSets){ImGui::PushID(set.first.c_str());
            if(UI::EntityOption((set.second+"##"+set.first).c_str(),set.first==g_SaveLibSelectedSet,[&]{CopyNameMenuItem(set.second,"Copy Name",true);}))g_SaveLibSelectedSet=set.first;
            ImGui::PopID();
        }
        if(UI::AvailableActionButton("Link & Save",UI::ButtonRole::Standard,sourceChar.has_value(),"Character source changed; reopen capture.")&&finishLoadout(g_SaveLibPendingCapture,g_SaveLibSelectedSet,""))ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if(UI::AvailableActionButton("Use independent components",UI::ButtonRole::Standard,sourceChar.has_value(),"Character source changed; reopen capture.")&&finishLoadout(g_SaveLibPendingCapture,"",""))ImGui::CloseCurrentPopup();
        ImGui::SameLine();if(RenderActionButton("Cancel"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("Existing Fashion##save_library",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const int duplicate=g_FashionLibrary.FindExact(g_SaveLibPendingFashion.wardrobe_link);
        if(duplicate>=0) {
            ImGui::TextWrapped("Same appearance saved as '%s'.",g_FashionLibrary.looks[duplicate].name.c_str());
            if(UI::AvailableActionButton("Select Existing",UI::ButtonRole::Standard,sourceChar.has_value(),"Character source changed; reopen capture.")) {
                if(SelectExistingFashion(duplicate,g_SaveLibPendingFashion))g_SaveLibDialogOpen=false;
                else g_SaveLibFashionError="Could not save character association.";
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if(UI::AvailableActionButton("Save Separate Copy",UI::ButtonRole::Standard,sourceChar.has_value(),"Character source changed; reopen capture.")) {
            if(SaveNewFashion(g_SaveLibPendingFashion))g_SaveLibDialogOpen=false;
            else g_SaveLibFashionError="Could not save Fashion.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(RenderActionButton("Cancel##fashion_duplicate"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(fashion&&!g_SaveLibFashionError.empty())
        ImGui::TextWrapped("%s",g_SaveLibFashionError.c_str());
    if (g_SaveLibUpdateConfirmPending) {
        ImGui::OpenPopup("Replace shared component?##save_component");
        g_SaveLibUpdateConfirmPending = false;
    }
    if (ImGui::BeginPopupModal("Replace shared component?##save_component",
                              nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Replace saved %s: %s",
            g_SaveLibPendingKind.c_str(), g_SaveLibPendingName.c_str());
        ImGui::TextWrapped("This updates saved fields for ALL Loadouts and equipment sets using "
                           "the component. It does not create a private copy.");
        if(Composable::IsCompanionKind(g_SaveLibPendingKind))
            ImGui::TextDisabled("Only reported selections replace slots; omitted slots are retained.");
        const auto action=UI::ConfirmCancel("replace_shared",sourceChar.has_value()&&!g_SaveLibPendingId.empty(),"Confirm Update",sourceChar?"Select the component to replace.":"Character source changed; reopen capture.",g_SaveLibError.c_str());
        if (action==UI::EditAction::Save) {
            const bool applied = AlterEgo::GW2API::ReplaceCapturedLoadoutComponent(
                g_SaveLibPendingCapture, g_SaveLibPendingKind, g_SaveLibPendingId);
            if (applied) {
                g_ComponentPendingKind = g_SaveLibComponentKind;
                g_ComponentSelectIdPending = g_SaveLibPendingId;
                NavigateToLibraryCollection(2);
                g_SaveLibDialogOpen = false;
                if (APIDefs) APIDefs->GUI_SendAlert("Saved component replaced.");
                ImGui::CloseCurrentPopup();
            } else g_SaveLibError="Component update failed. Original data is preserved.";
        }else if(action==UI::EditAction::Cancel||UI::EditorEscape())ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Live build unavailable##save")) {
        ImGui::TextWrapped("The character's retrieved build tab is no longer available. Refresh character data and try again.");
        ImGui::EndPopup();
    }

    ImGui::End();
}


// Render a single gear slot with icon for the build gear panel
static void RenderBuildGearSlot(AlterEgo::SavedBuild& build, const char* slot,bool editable) {
    // Check if off-hand is blocked by a two-handed main hand
    bool disabled = false;
    if (strcmp(slot, "WeaponA2") == 0 || strcmp(slot, "WeaponB2") == 0) {
        const char* mainSlot = (strcmp(slot, "WeaponA2") == 0) ? "WeaponA1" : "WeaponB1";
        auto mhIt = build.gear.find(mainSlot);
        if (mhIt != build.gear.end() && !mhIt->second.weapon_type.empty()) {
            disabled = IsWeaponTwoHanded(build.profession, mhIt->second.weapon_type);
        }
    }

    auto it = build.gear.find(slot);
    // Source builds can have an explicit, usable stat name before the item
    // details cache resolves its numeric ID. Don't show an imported trinket
    // as an empty slot just because its itemstat metadata is still loading.
    bool hasStat = it != build.gear.end() &&
        (it->second.stat_id != 0 || !it->second.stat_name.empty());
    bool hasWeaponType = (it != build.gear.end() && !it->second.weapon_type.empty());
    bool hasRelic = (strcmp(slot, "Relic") == 0 &&
                     (build.relic_id != 0 || !build.relic_name.empty() ||
                      (it != build.gear.end() && it->second.item_id != 0)));
    bool hasData = hasStat || hasWeaponType || hasRelic;
    const auto* gearSlot = it != build.gear.end() ? &it->second : nullptr;
    size_t infusionCount = 0;
    if (gearSlot) {
        for(size_t i=0;i<AlterEgo::GearEdit::InfusionPositionCount(*gearSlot);++i){
            const auto req=AlterEgo::GearEdit::InfusionPositionAt(*gearSlot,i);
            if(req.exactId||!req.attribute.empty())++infusionCount;
        }
    }

    ImGui::PushID(slot);

    // Draw icon (same size as equipment panel)
    float iconSz = ICON_SIZE;
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Try to render a real item icon
    uint32_t iconItemId = GetGearSlotIconItemId(build, slot);
    Texture_t* tex = nullptr;
    if (iconItemId != 0) {
        // The display icon's representative item is not the saved exact item.
        // Let the existing owner retry and revalidate rather than a permanent UI latch.
        AlterEgo::GW2API::FetchItemDetailsAsync({iconItemId});
        const auto itemInfo = AlterEgo::GW2API::GetItemInfo(iconItemId);
        if (itemInfo && !itemInfo->icon_url.empty()) {
            AlterEgo::IconManager::RequestIcon(iconItemId, itemInfo->icon_url);
            tex = AlterEgo::IconManager::GetIcon(iconItemId);
        }
    }

    if (tex && tex->Resource) {
        // Render real icon with legendary border (drawn on the icon edge, not outside it,
        // so a flush-left layout doesn't clip the border pixel)
        ImVec4 borderColor = hasData ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.5f, 0.3f, 0.6f, 0.8f);
        ImGui::GetWindowDrawList()->AddRect(
            pos,
            ImVec2(pos.x + iconSz, pos.y + iconSz),
            ImGui::ColorConvertFloat4ToU32(borderColor), 0.0f, 0, 1.5f);
        ImGui::Image(tex->Resource, ImVec2(iconSz, iconSz));
    } else {
        // Fallback: colored placeholder with slot initial
        ImU32 bgCol = disabled ? IM_COL32(30, 30, 30, 120) : (hasData ? IM_COL32(50, 45, 30, 200) : IM_COL32(40, 40, 40, 180));
        ImU32 borderCol = disabled ? IM_COL32(50, 50, 50, 120) : (hasData ? IM_COL32(255, 200, 60, 200) : IM_COL32(80, 80, 80, 200));
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2(pos.x + iconSz, pos.y + iconSz), bgCol);
        ImGui::GetWindowDrawList()->AddRect(
            pos, ImVec2(pos.x + iconSz, pos.y + iconSz), borderCol, 0.0f, 0, 1.5f);

        const char* initial = slot;
        if (strcmp(slot, "Helm") == 0) initial = "H";
        else if (strcmp(slot, "Shoulders") == 0) initial = "S";
        else if (strcmp(slot, "Coat") == 0) initial = "C";
        else if (strcmp(slot, "Gloves") == 0) initial = "G";
        else if (strcmp(slot, "Leggings") == 0) initial = "L";
        else if (strcmp(slot, "Boots") == 0) initial = "B";
        else if (strcmp(slot, "WeaponA1") == 0 || strcmp(slot, "WeaponB1") == 0) initial = "M";
        else if (strcmp(slot, "WeaponA2") == 0 || strcmp(slot, "WeaponB2") == 0) initial = "O";
        else if (strcmp(slot, "Backpack") == 0) initial = "Bk";
        else if (strcmp(slot, "Accessory1") == 0 || strcmp(slot, "Accessory2") == 0) initial = "A";
        else if (strcmp(slot, "Amulet") == 0) initial = "Am";
        else if (strcmp(slot, "Ring1") == 0 || strcmp(slot, "Ring2") == 0) initial = "R";
        else if (strcmp(slot, "Relic") == 0) initial = "Re";
        else if (strcmp(slot, "HelmAquatic") == 0) initial = "AH";
        else if (strcmp(slot, "WeaponAquaticA") == 0) initial = "A";
        else if (strcmp(slot, "WeaponAquaticB") == 0) initial = "B";

        ImVec2 textSz = ImGui::CalcTextSize(initial);
        ImU32 textCol = disabled ? IM_COL32(60, 60, 60, 120) : (hasData ? IM_COL32(255, 210, 80, 200) : IM_COL32(100, 100, 100, 180));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + (iconSz - textSz.x) * 0.5f, pos.y + (iconSz - textSz.y) * 0.5f),
            textCol, initial);
        ImGui::Dummy(ImVec2(iconSz, iconSz));
    }

    // Compact infusion count badge; hover over the gear icon for item names.
    if (infusionCount > 0) {
        std::string marker = "Inf " + std::to_string(infusionCount);
        ImVec2 ts = ImGui::CalcTextSize(marker.c_str());
        ImVec2 badgePos(pos.x + iconSz - ts.x - 5.0f,
                        pos.y + iconSz - ts.y - 3.0f);
        ImDrawList* badgeDraw = ImGui::GetWindowDrawList();
        badgeDraw->AddRectFilled(
            ImVec2(badgePos.x - 3.0f, badgePos.y - 1.0f),
            ImVec2(pos.x + iconSz - 1.0f, pos.y + iconSz - 1.0f),
            IM_COL32(22, 27, 34, 235), 2.0f);
        badgeDraw->AddText(badgePos, IM_COL32(160, 225, 255, 255), marker.c_str());
    }

    // Only editable in edit mode; read-only gear can still show tooltips.
    ImGui::SetCursorScreenPos(pos);
    if (disabled || !editable) {
        ImGui::Dummy(ImVec2(iconSz, iconSz));
    } else if (ImGui::InvisibleButton("##icon", ImVec2(iconSz, iconSz))) {
        AlterEgo::BuildGearSlot current;
        if (it != build.gear.end()) current = it->second;
        current.slot = slot;
        OpenGearDialog(build.id, slot, current);
    }
    bool hovered = !disabled && ImGui::IsItemHovered();
    bool clearAfter = false;
    if (editable && !disabled) RenderGearEditContext(build,slot,clearAfter);
    // Apply only after the menu returns; iterators below must not refer to erased slots.
    if (clearAfter) ClearGearSlot(build,slot,GearCopyField::All);
    it = build.gear.find(slot);
    gearSlot = it != build.gear.end() ? &it->second : nullptr;
    hasStat = it != build.gear.end() && (it->second.stat_id || !it->second.stat_name.empty());
    hasWeaponType = it != build.gear.end() && !it->second.weapon_type.empty();

    // Text is wrapped by the same responsive helper as live equipment.
    bool isRelicSlot = (strcmp(slot, "Relic") == 0);
    AlterEgo::UI::EquipmentText text(pos,iconSz,ImGui::GetContentRegionAvail().x);

    // Line 1: stat/slot name
    char line1[256] = {};
    ImU32 line1Col;
    if (isRelicSlot) {
        if (!build.relic_name.empty()) {
            snprintf(line1, sizeof(line1), "%s", build.relic_name.c_str());
            line1Col = IM_COL32(128, 204, 204, 255);
        } else {
            snprintf(line1, sizeof(line1), "Relic");
            line1Col = IM_COL32(128, 128, 128, 255);
        }
    } else if (hasStat && hasWeaponType) {
        snprintf(line1, sizeof(line1), "%s %s", it->second.stat_name.c_str(), WeaponDisplayName(it->second.weapon_type));
        line1Col = IM_COL32(255, 204, 51, 255);
    } else if (hasStat) {
        snprintf(line1, sizeof(line1), "%s %s", it->second.stat_name.c_str(), GearSlotDisplayName(slot));
        line1Col = IM_COL32(255, 204, 51, 255);
    } else if (hasWeaponType) {
        snprintf(line1, sizeof(line1), "%s %s", WeaponDisplayName(it->second.weapon_type), GearSlotDisplayName(slot));
        line1Col = IM_COL32(204, 191, 128, 255);
    } else if (disabled) {
        snprintf(line1, sizeof(line1), "%s (2H equipped)", GearSlotDisplayName(slot));
        line1Col = IM_COL32(89, 89, 89, 153);
    } else {
        snprintf(line1, sizeof(line1), "%s", GearSlotDisplayName(slot));
        line1Col = IM_COL32(128, 128, 128, 255);
    }
    auto savedGearActions=[&]{
        const auto actual=gearSlot&&gearSlot->item_id?AlterEgo::GW2API::GetItemInfo(gearSlot->item_id):nullptr;
        CopyEntityMenu(actual&&!actual->name.empty()?actual->name:std::string(line1),AlterEgo::LINK_ITEM,isRelicSlot?build.relic_id:gearSlot?gearSlot->item_id:0);
        if(gearSlot){
            CopyNameMenuItem(gearSlot->rune,"Copy Rune Name");
            CopyEntityChatLink(AlterEgo::LINK_ITEM,gearSlot->rune_id,"Copy Rune Chat Link");
            CopyNameMenuItem(gearSlot->sigil,"Copy Sigil Name");
            CopyEntityChatLink(AlterEgo::LINK_ITEM,gearSlot->sigil_id,"Copy Sigil Chat Link");
            CopyNameMenuItem(gearSlot->sigil2,"Copy Second Sigil Name");
            CopyEntityChatLink(AlterEgo::LINK_ITEM,gearSlot->sigil2_id,"Copy Second Sigil Chat Link");
            CopyCachedItemNamesMenu(gearSlot->infusion_ids,"Copy Infusion Name");
        CopyCachedItemLinksMenu(gearSlot->infusion_ids,"Copy Infusion Chat Link");
        }
    };
    if(!editable&&!disabled&&hasData)AlterEgo::UI::EntityContext("##saved_gear_name",savedGearActions);
    text.Line(line1,line1Col);
    if (!isRelicSlot && it != build.gear.end()) {
        text.Line(it->second.rune,IM_COL32(153,153,255,255));
        text.Line(it->second.sigil,IM_COL32(153,153,255,255));
        text.Line(it->second.sigil2,IM_COL32(153,153,255,255));
    }
    if(!disabled&&hasData){text.ContextTarget();AlterEgo::UI::EntityContext("##saved_gear_text",savedGearActions);}
    text.Finish();

    // Tooltip
    if (hovered && isRelicSlot && (!build.relic_name.empty()||build.relic_id)) {
        const auto info=AlterEgo::GW2API::GetItemInfo(build.relic_id);
        const auto label=AlterEgo::TooltipCore::EntityName(info?info->name:std::string{},build.relic_name);
        if(!label.empty()){
        ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
        AlterEgo::UI::TooltipLine(label.c_str(),ImVec4(.5f,.8f,.8f,1.f));
        if(info)RenderItemDefinitionDetails(*info);
        for (const auto& entry : GearCatalog("Relic")) {
            if ((!info||(info->description.empty()&&!(info->type=="Relic"&&info->rarity=="Legendary")))&&build.relic_name == entry.name) {
                AlterEgo::UI::TooltipLine(entry.bonus.c_str());
                break;
            }
        }
        ImGui::EndTooltip();
        }
    } else if (hovered && (hasData || infusionCount > 0 || (gearSlot&&gearSlot->item_id))) {
        ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
        const auto cachedItem=gearSlot&&gearSlot->item_id?AlterEgo::GW2API::GetItemInfo(gearSlot->item_id):nullptr;
        if(cachedItem&&!cachedItem->name.empty())AlterEgo::UI::TooltipLine(cachedItem->name.c_str(),GetRarityColor(cachedItem->rarity));
        else AlterEgo::UI::TooltipLine(GearSlotDisplayName(slot),AlterEgo::UI::TooltipTitle);
        if(cachedItem)RenderItemDefinitionDetails(*cachedItem,AlterEgo::TooltipCore::ItemType|AlterEgo::TooltipCore::ItemCombat|AlterEgo::TooltipCore::ItemEffects|AlterEgo::TooltipCore::ItemDescription|AlterEgo::TooltipCore::ItemAcquisition);
        if(gearSlot){
            const auto upgrade=[](const char* label,uint32_t id,const std::string& stored){
                if(!id&&stored.empty())return;
                const auto info=AlterEgo::GW2API::GetItemInfo(id);
                const auto name=AlterEgo::TooltipCore::EntityName(info?info->name:std::string{},stored);
                if(!name.empty())AlterEgo::UI::TooltipLine((std::string(label)+": "+name).c_str());
                if(info)RenderUpgradeTooltipDetails(*info);
            };
            upgrade("Rune",gearSlot->rune_id,gearSlot->rune);upgrade("Sigil",gearSlot->sigil_id,gearSlot->sigil);upgrade("Second sigil",gearSlot->sigil2_id,gearSlot->sigil2);
        }
        if (hasWeaponType) {
            ImGui::TextColored(ImVec4(0.8f, 0.75f, 0.5f, 1.0f), "%s", WeaponDisplayName(it->second.weapon_type));
        }
        if (hasStat) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s %s",
                it->second.stat_name.c_str(), GearSlotDisplayName(slot));
            const auto statInfo = AlterEgo::GW2API::GetItemStatInfo(it->second.stat_id);
            if (statInfo) {
                for (const auto& a : statInfo->attributes) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "  +%s", FriendlyAttrName(a));
                }
            }
        }
        if (gearSlot && infusionCount > 0) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.62f, 0.88f, 1.0f, 1.0f),
                "Infusions (%zu)", infusionCount);
            for(size_t i=0;i<AlterEgo::GearEdit::InfusionPositionCount(*gearSlot);++i){
                const auto req=AlterEgo::GearEdit::InfusionPositionAt(*gearSlot,i);
                if(!req.attribute.empty())ImGui::BulletText("Stat: %s",InfusionAttributeLabel(req.attribute));
                else if(req.exactId){const auto info=AlterEgo::GW2API::GetItemInfo(req.exactId);if(info&&!info->name.empty())ImGui::BulletText("%s",info->name.c_str());}
            }
        }
        ImGui::EndTooltip();
    } else if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "%s%s", GearSlotDisplayName(slot),
            editable ? " (click to customize)" : "");
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

// Render the full gear section for a saved build
static void RenderBuildGearPanel(AlterEgo::SavedBuild& build,bool editable) {
    AlterEgo::UI::EquipmentLayout("##saved_gear",g_LibEditMode||AlterEgo::GearEdit::HasRecordedAquaticGear(build.gear),
        [&](const char* slot){
            // Keep exact historical slot keys until a deliberate data edit.
            if(!build.gear.count(slot)&&std::strcmp(slot,"WeaponAquaticA")==0&&build.gear.count("AquaticA"))
                RenderBuildGearSlot(build,"AquaticA",editable);
            else if(!build.gear.count(slot)&&std::strcmp(slot,"WeaponAquaticB")==0&&build.gear.count("AquaticB"))
                RenderBuildGearSlot(build,"AquaticB",editable);
            else RenderBuildGearSlot(build,slot,editable);
        },
        [](const char* label,int){RenderSectionHeader(label,AlterEgo::UI::SectionGold);});
}

// Re-resolve item names (rune, relic, sigils, stats) for a saved build from
// the GW2 API cache. If IDs are present but names are missing, queues async
// fetches so the next render attempt can fill them in. Handles the case where
// the GW2 API was unavailable or rate-limited at import time.
static void ResolveBuildItemNames(AlterEgo::SavedBuild& build) {
    bool changed = false;
    std::vector<uint32_t> missingItems;
    std::vector<uint32_t> missingStats;

    auto tryItem = [&](uint32_t id, std::string& name) {
        if (id == 0 || !name.empty()) return;
        const auto info = AlterEgo::GW2API::GetItemInfo(id);
        if (info && !info->name.empty()) {
            name = info->name;
            changed = true;
        } else {
            missingItems.push_back(id);
        }
    };

    tryItem(build.rune_id, build.rune_name);
    tryItem(build.relic_id, build.relic_name);
    // Imported builds specify infusions across the entire equipment set.
    // Resolve their display names after a restart even if the item cache
    // was missing when the build was first imported.
    for (const auto& inf : build.infusion_totals)
        if (inf.item_id && !AlterEgo::GW2API::GetItemInfo(inf.item_id))
            missingItems.push_back(inf.item_id);

    for (auto& [slot, gs] : build.gear) {
        tryItem(gs.sigil_id, gs.sigil);
        tryItem(gs.sigil2_id, gs.sigil2);
        for (uint32_t infId : gs.infusion_ids) {
            if (infId != 0 && !AlterEgo::GW2API::GetItemInfo(infId))
                missingItems.push_back(infId);
        }
        if (gs.rune_id == 0 && build.rune_id != 0) {
            // Propagate shared rune to armor slot if not yet set
            static const char* armorSlots[] = {"Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
            for (const char* s : armorSlots) {
                if (slot == s) { gs.rune_id = build.rune_id; changed = true; break; }
            }
        }
        if (gs.rune_id != 0 && gs.rune.empty() && !build.rune_name.empty()) {
            gs.rune = build.rune_name;
            changed = true;
        }
        if (gs.stat_id == 0 && gs.item_id != 0) {
            const auto item = AlterEgo::GW2API::GetItemInfo(gs.item_id);
            if (!item) {
                missingItems.push_back(gs.item_id);
            } else if (item->details.is_object() &&
                       item->details.contains("infix_upgrade") &&
                       item->details["infix_upgrade"].is_object() &&
                       item->details["infix_upgrade"].contains("id") &&
                       item->details["infix_upgrade"]["id"].is_number_unsigned()) {
                const uint32_t id = item->details["infix_upgrade"]["id"].get<uint32_t>();
                const auto info = AlterEgo::GW2API::GetItemStatInfo(id);
                if (!info) missingStats.push_back(id);
                // A fixed item's public stats verify the text prefix. If the
                // source requested a different selected stat, do not silently
                // replace it with the item's default.
                if (info && (gs.stat_name.empty() ||
                    AlterEgo::NamedStats::SameCanonicalStat(gs.stat_name, info->name))) {
                    gs.stat_id = id;
                    gs.stat_name = info->name;
                    changed = true;
                }
            }
        }
        if (gs.stat_id != 0 && gs.stat_name.empty()) {
            const auto si = AlterEgo::GW2API::GetItemStatInfo(gs.stat_id);
            if (si && !si->name.empty()) {
                gs.stat_name = si->name;
                changed = true;
            } else {
                missingStats.push_back(gs.stat_id);
            }
        }
    }

    if (!missingItems.empty())
        AlterEgo::GW2API::FetchItemDetailsAsync(missingItems);
    if (!missingStats.empty())
        AlterEgo::GW2API::FetchItemStatDetailsAsync(missingStats);

    (void)changed; // display-only enrichment
}

// Resolve negative trait placeholders in a saved build once spec data is available
static void ResolveBuildTraitPlaceholders(AlterEgo::SavedBuild& build) {
    bool changed = false;
    for (int i = 0; i < 3; i++) {
        if (build.specializations[i].spec_id == 0) continue;
        const auto specInfo = AlterEgo::GW2API::GetSpecInfo(build.specializations[i].spec_id);
        if (!specInfo || specInfo->major_traits.size() < 9) continue;
        for (int tier = 0; tier < 3; tier++) {
            if (build.specializations[i].traits[tier] >= 0) continue;
            int choice = -(build.specializations[i].traits[tier]);
            if (choice >= 1 && choice <= 3) {
                int mIdx = tier * 3 + (choice - 1);
                build.specializations[i].traits[tier] = (int)specInfo->major_traits[mIdx];
                changed = true;
            }
        }
    }
    if (!build.source_url.empty()) {
        // Correct partially specified Snow Crows lines using the canonical
        // specialization metadata once it has arrived from the GW2 API.
        for (int i = 0; i < 3; ++i) {
            const auto spec = AlterEgo::GW2API::GetSpecInfo(build.specializations[i].spec_id);
            if (!spec || spec->major_traits.size() < 9) continue;
            std::array<int, 3> arranged{};
            bool allKnown = true;
            for (int trait : build.specializations[i].traits) {
                if (!trait) continue;
                auto pos = std::find(spec->major_traits.begin(), spec->major_traits.end(), (uint32_t)trait);
                if (pos == spec->major_traits.end()) { allKnown = false; break; }
                arranged[(pos - spec->major_traits.begin()) / 3] = trait;
            }
            if (allKnown)
                for (int tier = 0; tier < 3; ++tier)
                    if (build.specializations[i].traits[tier] != arranged[tier]) {
                        build.specializations[i].traits[tier] = arranged[tier];
                        changed = true;
                    }
        }
    }
    if (!build.source_url.empty() && build.chat_link.empty() &&
        AlterEgo::GW2API::HasPaletteData(build.profession)) {
        build.chat_link = AlterEgo::GW2API::EncodeSavedBuildToChatLink(build);
        changed = true;
    }
    (void)changed; // display-only enrichment
}

// Heal/Utility/Elite icon row for a build. When `editable`, each icon opens the skill
// picker for its slot (0=heal, 1-3=util, 4=elite) and draws a gold hover outline.
static void RenderBuildSkillRow(const AlterEgo::SavedBuild& src,float sz,bool editable,bool aquatic=false) {
    namespace SE=AlterEgo::SkillEdit;
    const auto& bar=aquatic?src.aquatic_skills:src.terrestrial_skills;
    ImGui::PushID(aquatic?"aquatic_bar":"terrestrial_bar");
    const auto slots=SE::WithPaletteBar(SE::DisplayBar(bar),src.rev_active_palettes,aquatic);
    if(!editable){
        RenderSkillBarPreview(slots,sz,aquatic);
    }else{
        std::vector<uint32_t> specs;
        for(const auto& spec:src.specializations)if(spec.spec_id)specs.push_back(spec.spec_id);
        const auto columns=SE::IconColumns(ImGui::GetContentRegionAvail().x,sz,ImGui::GetStyle().ItemSpacing.x,5);
        for(size_t i=0;i<slots.size();++i){
            if(i%columns)ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i)+700);
            uint32_t next=slots[i].id;
            if(RenderSkillEditorSlot(next,sz,src.profession,SE::SlotRole(i),specs,false,aquatic,false,next,SE::SlotLabel(i),slots[i].evidence)){
                auto& draft=aquatic?g_EditDraft.aquatic_skills:g_EditDraft.terrestrial_skills;
                if(SE::SetSlotWithPaletteEvidence(draft,g_EditDraft.rev_active_palettes,aquatic,static_cast<int>(i),next))MarkEditDirty();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

// The saved-build game mode should be legible against both dark and bright
// profession-tinted header backgrounds, without turning it into a button.
static void RenderBuildModeBadge(const char* mode) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 label = ImGui::CalcTextSize(mode);
    const float padX = 7.0f, padY = 2.0f;
    const ImVec2 size(label.x + 2.0f * padX, label.y + 2.0f * padY);
    const ImVec2 end(start.x + size.x, start.y + size.y);
    dl->AddRectFilled(start, end, IM_COL32(54, 65, 62, 235), 3.0f);
    dl->AddRect(start, end, IM_COL32(129, 157, 142, 220), 3.0f);
    dl->AddText(ImVec2(start.x + padX, start.y + padY),
        IM_COL32(237, 240, 233, 255), mode);
    ImGui::Dummy(size);
}

// Render the build preview panel for a saved build (reuses spec/trait rendering logic)
// Fixed build identity and actions; kept outside the scrolling detail child.
// One provider-neutral patch for initial import and later refresh. Initial
// imports can keep unresolved trait IDs in their published order (there is
// no local choice to overwrite); refreshes require known tiers for partial lines.
static int SourceTraitTier(uint32_t specId,uint32_t trait){
    using namespace AlterEgo;
    const auto spec=GW2API::GetSpecInfo(specId);
    if(spec&&spec->major_traits.size()>=9){
        const auto pos=std::find(spec->major_traits.begin(),spec->major_traits.end(),trait);
        return pos==spec->major_traits.end()?-2:static_cast<int>((pos-spec->major_traits.begin())/3);
    }
    const auto info=GW2API::GetTraitInfo(trait);
    return info&&info->tier>=1&&info->tier<=3?info->tier-1:-1;
}
static AlterEgo::SourceRefresh::ItemCompatibility SourceItemCompatibility(const AlterEgo::BuildGearSlot& old,const AlterEgo::SourceRefresh::GearPatch& next){
    using namespace AlterEgo;
    using C=SourceRefresh::ItemCompatibility;
    const auto item=GW2API::GetItemInfo(old.item_id);
    if(!item||!item->details.is_object())return C::Unknown;
    const auto& details=item->details;
    const auto choices=details.find("stat_choices");
    if(choices!=details.end()&&choices->is_array()&&!choices->empty()){
        if(next.stat&&std::find(choices->begin(),choices->end(),nlohmann::json(*next.stat))!=choices->end())return C::Compatible;
        return C::Unknown; // alternate itemstat IDs may represent the same family
    }
    const auto fixed=details.find("infix_upgrade");
    if(fixed==details.end()||!fixed->is_object()||!fixed->contains("id")||!fixed->at("id").is_number_unsigned())return C::Unknown;
    const uint32_t id=fixed->at("id").get<uint32_t>();
    if(next.stat&&id==*next.stat)return C::Compatible;
    const auto info=GW2API::GetItemStatInfo(id);
    const auto target=next.stat?GW2API::GetItemStatInfo(*next.stat):nullptr;
    const auto name=next.statName.value_or(target?target->name:std::string{});
    if(info&&!name.empty()&&!NamedStats::CanonicalStatName(info->name).empty()&&!NamedStats::CanonicalStatName(name).empty())
        return NamedStats::SameCanonicalStat(info->name,name)?C::Compatible:C::Incompatible;
    return C::Unknown;
}
static bool SourcePageToSavedBuild(const AlterEgo::SnowCrows::Page& page,AlterEgo::SavedBuild& result,std::string& error){
    using namespace AlterEgo;
    GameMode mode=page.gameMode=="Raid"?GameMode::Raid:page.gameMode=="Fractal"?GameMode::Fractal:
        page.gameMode=="WvW"?GameMode::WvW:page.gameMode=="PvP"?GameMode::PvP:GameMode::PvE;
    auto blank=GW2API::CreateBlankBuild(page.profession,mode);
    blank.name=page.name;blank.source_url=page.sourceUrl;
    const auto patch=SourceRefresh::FromPage(page,SourceTraitTier,true);
    blank.chat_link=patch.verifiedTemplate;
    SourceRefresh::Report report;
    return SourceRefresh::Apply(blank,patch,result,report,error,
        [&](const auto& type){return IsWeaponTwoHanded(page.profession,type);},SourceItemCompatibility,true);
}
static void FetchBuildSource(const std::string& url, const std::string& refreshId = "") {
    if (g_BuildSourceBusy) return;
    if(!refreshId.empty()&&LibraryDraftOpen()){g_BuildSourceRefreshError="Finish the current Library editor first.";return;}
    const uint64_t revision=AlterEgo::GW2API::GetBuildLibraryRevision();
    g_BuildSourceBusy = true;
    g_BuildSourceRefreshError.clear();
    std::string normalized = AlterEgo::SnowCrows::NormalizeUrl(url);
    if (normalized.empty()) normalized = AlterEgo::MetaBattle::NormalizeUrl(url);
    AlterEgo::Background::Workers().Launch([normalized, refreshId,revision]() {
        BuildSourceFetchResult result;
        result.refresh = !refreshId.empty();
        result.targetId = refreshId;result.revision=revision;
        if (normalized.empty()) {
            result.error = "Unsupported build URL. Use Snow Crows or MetaBattle.";
        } else {
            const bool metaBattle = normalized.find("https://metabattle.com/") == 0;
            const char* source = metaBattle ? "MetaBattle" : "Snow Crows";
            const auto response = Skinventory::HttpClient::GetEx(normalized,
                "User-Agent: Mozilla/5.0 (compatible; AlterEgo/1.0)\r\n"
                "Accept: text/html\r\n");
            if (response.status_code != 200) {
                result.error = std::string(source) + " download failed (HTTP " +
                    std::to_string(response.status_code) + ").";
            } else {
                // All providers produce one common, validated Page structure.
                // Never write an incomplete result to the build library.
                const bool parsed = metaBattle
                    ? AlterEgo::MetaBattle::Parse(normalized, response.body, result.page, result.error)
                    : AlterEgo::SnowCrows::Parse(normalized, response.body, result.page, result.error);
                (void)parsed; // parser provides the actionable error on failure
            }
        }
        result.ready = true;
        std::lock_guard<std::mutex> lock(g_BuildSourceResultMutex);
        g_BuildSourceResult = std::move(result);
    }, [refreshId,revision] { BuildSourceFetchResult result; result.revision=revision; result.refresh = !refreshId.empty(); result.targetId = refreshId; result.error = "Build download interrupted."; result.ready = true; std::lock_guard<std::mutex> lock(g_BuildSourceResultMutex); g_BuildSourceResult = std::move(result); });
}

static void PollBuildSource() {
    BuildSourceFetchResult received;
    {
        std::lock_guard<std::mutex> lock(g_BuildSourceResultMutex);
        if (!g_BuildSourceResult.ready) return;
        received = std::move(g_BuildSourceResult);
        g_BuildSourceResult = {};
    }
    g_BuildSourceBusy = false;
    if (!received.refresh && !g_LibShowImport) return; // dismissed import
    if (!received.error.empty()) {
        if (received.refresh) g_BuildSourceRefreshError = received.error;
        else g_LibImportError = received.error;
        return;
    }
    AlterEgo::SavedBuild build;
    std::string projectionError;
    if(!SourcePageToSavedBuild(received.page,build,projectionError)){
        if(received.refresh)g_BuildSourceRefreshError=projectionError;else g_LibImportError=projectionError;
        return;
    }
    // Opportunistic name/icon resolution; public API calls run asynchronously.
    std::vector<uint32_t> items, stats, traits, specs, skills;
    for (const auto& [slot, gs] : build.gear) {
        if (gs.item_id) items.push_back(gs.item_id);
        if (gs.stat_id) stats.push_back(gs.stat_id);
        if (gs.rune_id) items.push_back(gs.rune_id);
        if (gs.sigil_id) items.push_back(gs.sigil_id);
        if (gs.sigil2_id) items.push_back(gs.sigil2_id);
    }
    if (build.relic_id) items.push_back(build.relic_id);
    if (build.rune_id) items.push_back(build.rune_id);
    for (const auto& req : build.infusion_totals) items.push_back(req.item_id);
    for (const auto& spec : received.page.specs) {
        specs.push_back(spec.id);
        for (auto id : spec.traits) traits.push_back(id);
    }
    for (auto id : received.page.skills) skills.push_back(id);
    AlterEgo::GW2API::FetchItemDetailsAsync(items);
    AlterEgo::GW2API::FetchItemStatDetailsAsync(stats);
    AlterEgo::GW2API::FetchSpecDetailsAsync(specs);
    AlterEgo::GW2API::FetchTraitDetailsAsync(traits);
    AlterEgo::GW2API::FetchSkillDetailsAsync(skills);
    AlterEgo::GW2API::FetchProfessionPaletteAsync(build.profession);
    if (build.chat_link.empty() && AlterEgo::GW2API::HasPaletteData(build.profession))
        build.chat_link = AlterEgo::GW2API::EncodeSavedBuildToChatLink(build);
    if (received.refresh) {
        using namespace AlterEgo;
        auto reject=[&](const std::string& why){g_BuildSourceRefreshError=why;if(APIDefs)APIDefs->GUI_SendAlert(why.c_str());};
        if(received.revision!=GW2API::GetBuildLibraryRevision()||LibraryDraftOpen()){
            reject("Library changed or another editor opened during download. Refresh Source again.");return;
        }
        const auto& builds=GW2API::GetSavedBuilds();
        const auto it=std::find_if(builds.begin(),builds.end(),[&](const auto& b){return b.id==received.targetId;});
        if(it==builds.end()){reject("The source Loadout no longer exists.");return;}
        const auto old=*it;
        const auto patch=SourceRefresh::FromPage(received.page,SourceTraitTier);
        SavedBuild merged;SourceRefresh::Report report;std::string error;
        if(!SourceRefresh::Apply(old,patch,merged,report,error,[&](const auto& type){return IsWeaponTwoHanded(old.profession,type);},SourceItemCompatibility)){
            reject(error);return;
        }
        if(received.revision!=GW2API::GetBuildLibraryRevision()){
            reject("Library changed during source review. Refresh Source again.");return;
        }
        if(GW2API::EditorBuildSnapshot(old)==GW2API::EditorBuildSnapshot(merged)&&!report.offerInfusions){
            if(APIDefs)APIDefs->GUI_SendAlert("No supplied source fields changed this Loadout.");return;
        }
        g_LibSelectedId=old.id;g_LibEditBuildId=old.id;g_EditIsNewBuild=false;
        EnterBuildEditMode(old);
        if(!g_EditRefsReady||g_EditDraftRevision!=received.revision){
            g_LibEditMode=false;g_EditDraftId.clear();reject("Source review could not open at the original revision.");return;
        }
        g_EditDraft=std::move(merged);g_SourceRefreshDraft=true;g_SourceRefreshNotes=std::move(report.notes);
        if(report.offerInfusions)g_SourceInfusionOffer=patch.infusions;
        MarkEditDirty();g_LibDetailsFetched=false;NavigateToLibraryCollection(0);
        if(APIDefs)APIDefs->GUI_SendAlert("Source changes staged. Review the Loadout and Save or Cancel.");
    } else {
        // Optional user-provided import name overrides the page's full title.
        if (g_LibImportName[0]) build.name = g_LibImportName;
        int idx = AlterEgo::GW2API::AddSavedBuild(std::move(build));
        if (idx >= 0) {
            SelectLoadoutAt(idx);
            NavigateToLibraryCollection(0);
            g_LibDetailsFetched = false;
            g_LibShowImport = false;
            g_LibImportBuf[0] = '\0';
            g_LibImportName[0] = '\0';
            g_LibImportError.clear();
            if (APIDefs) APIDefs->GUI_SendAlert(
                received.page.sourceUrl.find("metabattle.com") != std::string::npos
                    ? "MetaBattle build imported." : "Snow Crows build imported.");
        } else {
            g_LibImportError = "Could not save imported build.";
        }
    }
}

static void ActivateSavedBuildCompare(const AlterEgo::SavedBuild& build) {
    g_LibCompareSelection={};g_LibComparePreviousSelection={};
    g_LibCompareBuildTab=-1;g_LibCompareEquipTab=0;
    // A portable association can be resolved only by unique retained
    // evidence, never by the original capture name. Keep an unresolved
    // source selected so it cannot silently substitute another character.
    if(build.source_identity){
        const auto& source=*build.source_identity;
        g_LibCompareSelection={{source.account,0},source.evidence,source.name,false};
        if(const auto address=ResolveSourceIdentity(source))g_LibCompareSelection.Accept(*address);
        g_LibComparePreviousSelection=g_LibCompareSelection;
    }
    g_LibCompareBuildId=build.id;g_LibCompareSavedManual=true;
    MarkNavigation();g_SwitchToCompare=true;
}

namespace { static void CopyPortableLoadout(const std::string& loadoutId); }
// Shared domain action wiring for header and context menu.
enum class SavedLoadoutAction { Copy, Compare, Edit, Rename, Duplicate, Delete };
static void ExecuteSavedLoadoutAction(SavedLoadoutAction action,
                                      const AlterEgo::SavedBuild& build) {
    switch (action) {
    case SavedLoadoutAction::Copy:
        CopyPortableLoadout(build.id);
        break;
    case SavedLoadoutAction::Compare:
        g_LibSelectedId=build.id;
        ActivateSavedBuildCompare(build);
        break;
    case SavedLoadoutAction::Edit:
        if(OtherLibraryEditorOpen()){if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");break;}
        g_LibSelectedId=build.id;
        g_LibEditBuildId = build.id;
        EnterBuildEditMode(build);
        break;
    case SavedLoadoutAction::Rename:
    case SavedLoadoutAction::Duplicate:
        if(LibraryDraftOpen()){if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");break;}
        g_LibNameTarget={build.id,build.name,AlterEgo::GW2API::GetBuildLibraryRevision()};
        g_LibNameAction.Start(build.name,action==SavedLoadoutAction::Duplicate);
        break;
    case SavedLoadoutAction::Delete:
        if(LibraryDraftOpen()){if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");break;}
        g_LibDeleteTarget={build.id,build.name,AlterEgo::GW2API::GetBuildLibraryRevision()};g_LibDeletePending=true;g_LibDeleteError.clear();
        break;
    }
}
static void RenderSavedBuildHeader(const AlterEgo::SavedBuild& build, bool showEditButton = true) {
    ImVec4 profColor = GetProfessionColor(build.profession);
    {
        // Detect elite spec — use its icon and name in the header when present
        std::shared_ptr<const AlterEgo::SpecializationInfo> eliteSpec;
        for (int s = 0; s < 3; s++) {
            uint32_t sid = build.specializations[s].spec_id;
            if (!sid) continue;
            const auto si = AlterEgo::GW2API::GetSpecInfo(sid);
            if (si && si->elite) { eliteSpec = si; break; }
        }

        // Build header card: gold accent bar, name in profession color, sub-line with prof + mode
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        const float headerIconSz = 44.0f;
        float h = std::max(lineH * 2.0f + 6.0f,
            g_LibEditMode ? 68.0f : headerIconSz + 10.0f);
        // Gradient bg using profession color
        ImU32 left = ImGui::ColorConvertFloat4ToU32(
            ImVec4(profColor.x * 0.24f, profColor.y * 0.24f, profColor.z * 0.24f, 0.55f));
        ImU32 right = IM_COL32(0, 0, 0, 0);
        dl->AddRectFilledMultiColor(
            ImVec2(pos.x + 4, pos.y), ImVec2(pos.x + w, pos.y + h),
            left, right, right, left);
        // Gold left bar
        ImU32 barTop = IM_COL32(227, 196, 122, 255);
        ImU32 barBot = IM_COL32(140, 113, 55, 255);
        dl->AddRectFilledMultiColor(
            ImVec2(pos.x, pos.y + 2),
            ImVec2(pos.x + 3, pos.y + h - 2),
            barTop, barTop, barBot, barBot);
        // Underline
        dl->AddLine(
            ImVec2(pos.x + 4, pos.y + h),
            ImVec2(pos.x + w, pos.y + h),
            ImGui::ColorConvertFloat4ToU32(ImVec4(profColor.x, profColor.y, profColor.z, 0.35f)), 1.0f);

        // Class icon on the left — prefer elite-spec emblem, fall back to profession
        float iconLeft = pos.x + 10.0f;
        float iconTop = pos.y + (h - headerIconSz) * 0.5f;
        Texture_t* iconTex = nullptr;
        if (eliteSpec) {
            uint32_t specIconId = 2000000u + eliteSpec->id;
            iconTex = AlterEgo::IconManager::GetIcon(specIconId);
            if (!iconTex || !iconTex->Resource) {
                AlterEgo::IconManager::RequestIcon(specIconId,
                    eliteSpec->profession_icon_big_url, /*priority=*/true);
            }
        }
        if (!iconTex || !iconTex->Resource) {
            uint32_t profIconId = GetProfessionBigIconId(build.profession);
            iconTex = AlterEgo::IconManager::GetIcon(profIconId);
            if (!iconTex || !iconTex->Resource) {
                const auto prof = AlterEgo::GW2API::GetProfessionInfo(build.profession);
                AlterEgo::IconManager::RequestIcon(profIconId,
                    prof ? prof->icon_big_url : "", /*priority=*/true);
            }
        }
        if (iconTex && iconTex->Resource) {
            dl->AddImage(iconTex->Resource,
                ImVec2(iconLeft, iconTop),
                ImVec2(iconLeft + headerIconSz, iconTop + headerIconSz),
                ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, 255));
        }

        // Title + subtitle to the right of the icon. Use elite spec name as the
        // primary identifier when present; profession name as a subtitle line.
        float textX = iconLeft + headerIconSz + 10.0f;
        // An editable name is an input frame (taller than a text line).
        // Calculate both row heights explicitly so its bottom never overlaps
        // the game-mode chips when action controls gain vertical padding.
        const float titleRowH = g_LibEditMode ? ImGui::GetFrameHeight()
                                               : ImGui::GetTextLineHeight();
        const float modeRowH = g_LibEditMode
            ? ImMax(22.0f, ImGui::GetTextLineHeight() + 4.0f)
            : ImGui::GetTextLineHeight();
        const float headerTextGap = 3.0f;
        float textTop = pos.y + (h - titleRowH - modeRowH - headerTextGap) * 0.5f;

        // Title: build name (editable input while editing). Subtitle: elite spec +
        // profession + mode (mode becomes clickable chips bound to the draft).
        ImGui::SetCursorScreenPos(ImVec2(textX, textTop));
        if (g_LibEditMode) {
            const float nameWidth=std::max(1.f,std::min(220.f,ImGui::GetContentRegionAvail().x));
            ImGui::SetNextItemWidth(nameWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, profColor);
            if (AlterEgo::UI::InputText("##edit_name", g_LibEditName)) MarkEditDirty();
            ImGui::PopStyleColor();

            // "EDITING" badge with a drawn dot (Nexus's font atlas has no ● glyph).
            const char* badge = "EDITING";
            ImVec2 ts = ImGui::CalcTextSize(badge);
            float dotR = ts.y * 0.26f;
            float bx = textX + nameWidth + 10.0f, by = textTop + 1.0f;
            float gap = dotR * 2.0f + 6.0f;
            if(bx+gap+ts.x+5<=pos.x+w){
            dl->AddRectFilled(ImVec2(bx - 5, by - 2),
                              ImVec2(bx + gap + ts.x + 5, by + ts.y + 2),
                              IM_COL32(120, 90, 30, 230), 3.0f);
            dl->AddCircleFilled(ImVec2(bx + dotR, by + ts.y * 0.5f), dotR,
                                IM_COL32(240, 120, 120, 255));
            dl->AddText(ImVec2(bx + gap, by), IM_COL32(245, 210, 120, 255), badge);
            }
        } else {
            ImGui::TextColored(profColor, "%s", build.name.c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",build.name.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(textX, textTop + titleRowH + headerTextGap));
        std::string sub;
        if (eliteSpec) { sub = eliteSpec->name; sub += " "; }
        sub += build.profession;
        // Brighten the specialization/profession text. Show the read-only mode
        // in a compact high-contrast badge; editing keeps its existing chips.
        ImGui::TextColored(ImVec4(0.87f, 0.86f, 0.79f, 1.0f),
            "%s", sub.c_str());
        ImGui::SetCursorScreenPos(ImVec2(pos.x,pos.y+h+4));
        if(!g_LibEditMode){
            const auto displayModes=build.game_modes.empty()?std::vector<AlterEgo::GameMode>{build.game_mode}:build.game_modes;
            for(size_t m=0;m<displayModes.size();++m){
                if(m)AlterEgo::UI::SameLineIfActionFits(GameModeLabel(displayModes[m]));
                RenderBuildModeBadge(GameModeLabel(displayModes[m]));
            }
        }else{
            if(g_EditDraft.game_modes.empty())g_EditDraft.game_modes={g_EditDraft.game_mode};
            if(RenderGameModeSelector("saved-build-edit",g_EditDraft.game_modes,true)){
                g_EditDraft.game_mode=g_EditDraft.game_modes.front();MarkEditDirty();
            }
        }

        // Top-right header buttons. Editing: Save / Cancel. Otherwise: Share / Edit / Delete.
        if (showEditButton) {
            if (g_LibEditMode) {
                // Compare the actual editable fields and reference IDs, not sticky activity flags.
                g_EditDirty=LoadoutDraftDirty();
                const bool stale=g_EditDraftRevision!=AlterEgo::GW2API::GetBuildLibraryRevision();
                const bool valid=g_EditRefsReady&&!stale&&!g_GearDialogOpen&&!g_LibEditName.empty();
                const bool canSave=AlterEgo::EditorLifecycle::CanSave(valid,g_EditDirty,g_EditIsNewBuild,g_EditSavePending);
                const char* why=stale?"Library changed. Reopen the Loadout before saving.":g_GearDialogOpen?"Apply or cancel gear customization first.":!g_EditRefsReady?"References unavailable.":g_LibEditName.empty()?"Enter a name.":"No changes to save.";
                const auto action=AlterEgo::UI::SaveCancel("loadout_header",canSave,"Save",why,g_EditSavePending,g_EditError.c_str());
                if(action==AlterEgo::UI::EditAction::Save){
                    g_EditDraft.name=g_LibEditName;g_EditDraft.notes=g_LibEditNotes;
                    if(g_EditDraft.game_modes.empty())g_EditDraft.game_modes={g_EditDraft.game_mode};
                    g_EditDraft.game_mode=g_EditDraft.game_modes.front();
                    g_EditReviewPending=true;
                }
                if(AlterEgo::UI::DiscardRequested("Discard changes?##editcancel",
                    action==AlterEgo::UI::EditAction::Cancel,g_EditDirty||GearDraftDirty(),g_EditSavePending,
                    "Unsaved Loadout and gear changes will be lost.")){
                    if(g_EditIsNewBuild){
                        g_DiscardNewBuildId=build.id;g_DiscardNewBuildRevision=g_EditDraftRevision;
                    }else{
                        g_LibEditMode=false;g_EditDirty=false;g_EditDraftId.clear();g_EditError.clear();
                        g_GearDialogOpen=false;
                    }
                }
            } else {
            if (RenderActionButton("Edit##headerBuild")) ExecuteSavedLoadoutAction(SavedLoadoutAction::Edit, build);
            AlterEgo::UI::SameLineIfActionFits("Rename");
            if (RenderActionButton("Rename##headerBuild")) ExecuteSavedLoadoutAction(SavedLoadoutAction::Rename, build);
            AlterEgo::UI::SameLineIfActionFits("Duplicate");
            if (RenderActionButton("Duplicate##headerBuild")) ExecuteSavedLoadoutAction(SavedLoadoutAction::Duplicate, build);
            AlterEgo::UI::SameLineIfActionFits("Copy");
            if (RenderActionButton("Copy##headerBuild")) ExecuteSavedLoadoutAction(SavedLoadoutAction::Copy, build);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy Portable Loadout");
            AlterEgo::UI::SameLineIfActionFits("Share");
            if (RenderActionButton("Share##headerBuild")) ImGui::OpenPopup("##share_menu");
            AlterEgo::UI::SameLineIfActionFits("Compare");
            if (RenderActionButton("Compare##headerBuild")) ExecuteSavedLoadoutAction(SavedLoadoutAction::Compare, build);
            AlterEgo::UI::SameLineIfActionFits("Delete");
            if (AlterEgo::UI::Button("Delete##headerBuild", ImVec2(0,0), AlterEgo::UI::ButtonRole::Destructive)) ExecuteSavedLoadoutAction(SavedLoadoutAction::Delete, build);

            // Share menu: the three share actions that used to live in the
            // (now-removed) right-click context menu.
            if (ImGui::BeginPopup("##share_menu")) {
                RenderBuildShareMenuItems(build);
                ImGui::EndPopup();
            }

            } // end else (non-edit Share / Edit / Delete cluster)

        }

        // Native layout continues after the mode and action rows.
    }

}

namespace { static void RenderLoadoutComponentSelectors(const std::string& loadoutId); }
static void RenderSavedBuildPreview(const AlterEgo::SavedBuild& source) {
    auto build=source;
    // Try to resolve any placeholder traits (negative values from import without spec cache)
    // Safe to cast: we only mutate trait values from negative to positive
    ResolveBuildTraitPlaceholders(build);
    // Re-resolve any item/stat names that weren't available at import time
    // (e.g. API rate-limited or offline). Queues async fetches for missing IDs.
    ResolveBuildItemNames(build);
    RenderLoadoutComponentSelectors(source.id);
    const auto& metadataSource=g_LibEditMode?g_EditDraft:build;
    DemandSavedGearMetadata(metadataSource);
    AlterEgo::GW2API::FetchSpecDetailsAsync({metadataSource.specializations[0].spec_id,metadataSource.specializations[1].spec_id,metadataSource.specializations[2].spec_id});
    AlterEgo::GW2API::FetchProfessionInfoAsync(metadataSource.profession);
    ImGui::Spacing();

    // Specializations with trait grid (same logic as RenderBuildPanel)
    const float iconSz = TRAIT_ICON_SIZE;
    const float colW = iconSz * 2.0f;

    for (int i = 0; i < 3; i++) {
        // When editing, the grid reflects the mutable draft; otherwise the saved build.
        const auto& spec = g_LibEditMode ? g_EditDraft.specializations[i] : build.specializations[i];
        if (spec.spec_id == 0) {
            if (g_LibEditMode) {
                ImGui::PushID(i);
                if (RenderChipButton("+ Choose specialization", false)) g_SpecPickerSlot = i;
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 4));
            } else {
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "  (empty)");
            }
            continue;
        }

        const auto specInfo = AlterEgo::GW2API::GetSpecInfo(spec.spec_id);
        std::string specName = specInfo ? specInfo->name : ("Spec " + std::to_string(spec.spec_id));
        bool isElite = specInfo ? specInfo->elite : false;
        ImVec4 specColor = isElite ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        ImGui::PushID(spec.spec_id);

        if (specInfo && specInfo->major_traits.size() >= 9) {
            ImVec2 specCenter(0, 0);
            ImVec2 minorCenters[3] = {};
            ImVec2 selectedCenters[3] = {};
            bool hasSelected[3] = {false, false, false};

            ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            float rowW = ImGui::GetContentRegionAvail().x;
            float rowH = iconSz * 3 + ImGui::GetStyle().CellPadding.y * 6;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Spec background banner behind the trait grid (panoramic art,
            // native aspect, anchored left, fade to dark on the right).
            if (!specInfo->background_url.empty()) {
                uint32_t bgKey = spec.spec_id + 1000000;
                Texture_t* bgTex = AlterEgo::IconManager::GetIcon(bgKey);
                if (bgTex && bgTex->Resource && bgTex->Height > 0) {
                    // Zoom the banner and anchor it to the row's bottom-left.
                    // 15 specs ship with banners whose top ~66% is transparent
                    // (opaque art lives only in the bottom band). Those need a
                    // much higher zoom so the opaque content fills the row;
                    // everyone else gets the standard 1.4x.
                    auto IsTransparentTopSpec = [](uint32_t id) {
                        switch (id) {
                            case 4: case 7: case 11: case 20: case 22:
                            case 28: case 35: case 36: case 44: case 51:
                            case 54: case 68: case 70: case 71: case 72:
                            case 73: case 74: case 75: case 76: case 77:
                            case 78: case 79: case 80: case 81:
                                return true;
                            default:
                                return false;
                        }
                    };
                    const float zoom = IsTransparentTopSpec(spec.spec_id) ? 2.94f : 1.4f;
                    float drawH = rowH * zoom;
                    float scale = drawH / (float)bgTex->Height;
                    float drawW = (float)bgTex->Width * scale;
                    float bannerLeft = rowOrigin.x;
                    float bannerTop  = rowOrigin.y + rowH - drawH;
                    dl->PushClipRect(rowOrigin,
                        ImVec2(rowOrigin.x + rowW, rowOrigin.y + rowH), true);
                    dl->AddImage(bgTex->Resource,
                        ImVec2(bannerLeft, bannerTop),
                        ImVec2(bannerLeft + drawW, bannerTop + drawH),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 200));
                    // Fade-to-dark anchored to where the trait grid begins,
                    // not the banner. Trait icons start just past column 0.
                    float fadeStart = rowOrigin.x + SPEC_NAME_BADGE_WIDTH + 12.0f;
                    float fadeEnd   = rowOrigin.x + SPEC_NAME_BADGE_WIDTH + 72.0f;
                    dl->AddRectFilledMultiColor(
                        ImVec2(fadeStart, rowOrigin.y),
                        ImVec2(fadeEnd,   rowOrigin.y + rowH),
                        IM_COL32(15, 17, 23, 0),   IM_COL32(15, 17, 23, 200),
                        IM_COL32(15, 17, 23, 200), IM_COL32(15, 17, 23, 0));
                    if (fadeEnd < rowOrigin.x + rowW) {
                        dl->AddRectFilled(
                            ImVec2(fadeEnd, rowOrigin.y),
                            ImVec2(rowOrigin.x + rowW, rowOrigin.y + rowH),
                            IM_COL32(15, 17, 23, 200));
                    }
                    dl->PopClipRect();
                } else {
                    AlterEgo::IconManager::RequestIcon(bgKey, specInfo->background_url);
                }
            }

            // Spec name overlay on the banner's left.
            {
                ImVec2 nameAnchor;
                ImVec2 prevPos = ImGui::GetCursorScreenPos();
                const ImVec2 hitSz = DrawSpecializationNameBadge(
                    dl, specName, specColor, rowOrigin, rowH, nameAnchor);
                ImGui::SetCursorScreenPos(nameAnchor);
                ImGui::InvisibleButton("##specname", hitSz);
                if (g_LibEditMode) {
                    if (ImGui::IsItemClicked()) g_SpecPickerSlot = i;
                    if (ImGui::BeginPopupContextItem("##spec_edit_context")) {
                        CopyNameMenuItem(specName);
                        if (ImGui::MenuItem("Clear Specialization")) {
                            g_EditDraft.specializations[i] = {};
                            MarkEditDirty();
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered())
                        dl->AddRect(nameAnchor,
                            ImVec2(nameAnchor.x + hitSz.x, nameAnchor.y + hitSz.y),
                            IM_COL32(227, 196, 122, 200), 2.0f);
                }
                if(ImGui::IsItemHovered())RenderSpecializationTooltip(specName,specColor,{},g_LibEditMode);
                ImGui::SetCursorScreenPos(prevPos);
            }

            char outerId[32];
            snprintf(outerId, sizeof(outerId), "##lso_%u", spec.spec_id);
            if (ImGui::BeginTable(outerId, 2,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                    SPEC_NAME_BADGE_WIDTH);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(SPEC_NAME_BADGE_WIDTH - 8.0f, rowH - 2.0f));

                ImGui::TableNextColumn();
                char innerId[32];
                snprintf(innerId, sizeof(innerId), "##ltr_%u", spec.spec_id);
                if (ImGui::BeginTable(innerId, 6,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX)) {
                    for (int c = 0; c < 6; c++)
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, colW);

                    for (int row = 0; row < 3; row++) {
                        ImGui::TableNextRow();
                        for (int tier = 0; tier < 3; tier++) {
                            ImGui::TableNextColumn();
                            if (row == 1 && tier < (int)specInfo->minor_traits.size()) {
                                minorCenters[tier] = RenderTraitIcon(
                                    specInfo->minor_traits[tier], true, true, iconSz);
                            }

                            ImGui::TableNextColumn();
                            int majorIdx = tier * 3 + row;
                            uint32_t tid = specInfo->major_traits[majorIdx];
                            bool selected;
                            if (spec.traits[tier] < 0) {
                                // Placeholder: negative = -(choice) where 1=top,2=mid,3=bot
                                int choice = -(spec.traits[tier]);
                                selected = (choice >= 1 && choice <= 3 && (choice - 1) == row);
                            } else {
                                selected = ((uint32_t)spec.traits[tier] == tid);
                            }
                            bool tClicked = false, clearRequested=false;
                            ImVec2 c = RenderTraitIcon(tid, selected, false, iconSz,
                                                       g_LibEditMode, &tClicked,
                                                       g_LibEditMode?&clearRequested:nullptr);
                            if(clearRequested){
                                g_EditDraft.specializations[i].traits[tier] = 0;
                                MarkEditDirty();
                            }else if (tClicked) {
                                g_EditDraft.specializations[i].traits[tier] = (int)tid;
                                MarkEditDirty();
                            }
                            if (selected) {
                                selectedCenters[tier] = c;
                                hasSelected[tier] = true;
                            }
                        }
                    }
                    ImGui::EndTable();
                }

                ImGui::EndTable();
            }

            ImU32 lineCol = IM_COL32(180, 230, 255, 180);
            float minorR = MINOR_TRAIT_SIZE * 0.5f;
            float majorR = iconSz * 0.5f;
            (void)specCenter;

            auto InsetLine = [](ImVec2 a, ImVec2 b, float rA, float rB, ImVec2& outA, ImVec2& outB) {
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len < rA + rB) return false;
                float nx = dx / len, ny = dy / len;
                outA = ImVec2(a.x + nx * rA, a.y + ny * rA);
                outB = ImVec2(b.x - nx * rB, b.y - ny * rB);
                return true;
            };

            // Skip the first connector — the hex spec icon is gone.
            ImVec2 prev;
            float prevR = 0;
            bool havePrev = false;
            for (int tier = 0; tier < 3; tier++) {
                if (minorCenters[tier].x > 0) {
                    if (havePrev) {
                        ImVec2 la, lb;
                        if (InsetLine(prev, minorCenters[tier], prevR, minorR, la, lb))
                            DrawDottedLine(dl, la, lb, lineCol);
                    }
                    prev = minorCenters[tier]; prevR = minorR; havePrev = true;
                }
                if (hasSelected[tier]) {
                    if (havePrev) {
                        ImVec2 la, lb;
                        if (InsetLine(prev, selectedCenters[tier], prevR, majorR, la, lb))
                            DrawDottedLine(dl, la, lb, lineCol);
                    }
                    prev = selectedCenters[tier]; prevR = majorR; havePrev = true;
                }
            }
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Helper lambda: get weapon skills for a weapon set from gear slots
    // Handles: Elementalist attunements (Fire default), Thief dual wield, standard MH+OH
    // Weapon skills and the gear panel reflect the draft while editing.
    const AlterEgo::SavedBuild& gearSrc = g_LibEditMode ? g_EditDraft : build;
    auto GetWeaponSkills = [&](const char* mhSlot, const char* ohSlot,
                               std::vector<uint32_t>& outSkills, std::string& outLabel) {
        outSkills.clear();
        outLabel.clear();
        const auto profWeapons = AlterEgo::GW2API::GetProfessionWeapons(gearSrc.profession);
        if (!profWeapons) return;

        auto mhIt = gearSrc.gear.find(mhSlot);
        auto ohIt = gearSrc.gear.find(ohSlot);
        std::string mhType = (mhIt != gearSrc.gear.end()) ? mhIt->second.weapon_type : "";
        std::string ohType = (ohIt != gearSrc.gear.end()) ? ohIt->second.weapon_type : "";

        // Mainhand skills (1-3, or 1-5 for two-handed)
        if (!mhType.empty()) {
            auto wit = profWeapons->find(mhType);
            if (wit != profWeapons->end()) {
                const auto& wd = wit->second;
                int count = wd.two_handed ? 5 : 3;

                // Check for attunement skills (Elementalist) — use Fire by default
                if (!wd.attunement_skills.empty()) {
                    auto attIt = wd.attunement_skills.find("Fire");
                    if (attIt == wd.attunement_skills.end())
                        attIt = wd.attunement_skills.begin(); // fallback to first
                    for (int i = 0; i < count; i++) {
                        outSkills.push_back(attIt->second[i]);
                    }
                } else {
                    for (int i = 0; i < count; i++) {
                        outSkills.push_back(wd.skills[i]);
                    }
                }

                // Thief dual wield: replace Weapon_3 if offhand-specific variant exists
                if (!wd.dual_wield.empty() && !ohType.empty() && count >= 3) {
                    auto dwIt = wd.dual_wield.find(ohType);
                    if (dwIt != wd.dual_wield.end()) {
                        outSkills[2] = dwIt->second;
                    }
                }

                outLabel = WeaponDisplayName(mhType);
                if (wd.two_handed) { return; } // two-handed uses all 5 slots
            }
        }

        // Offhand skills (4-5)
        if (!ohType.empty()) {
            auto wit = profWeapons->find(ohType);
            if (wit != profWeapons->end()) {
                const auto& wd = wit->second;
                // Pad to 3 if mainhand didn't fill
                while (outSkills.size() < 3) outSkills.push_back(0);

                // Check for attunement skills (Elementalist offhand)
                if (!wd.attunement_skills.empty()) {
                    auto attIt = wd.attunement_skills.find("Fire");
                    if (attIt == wd.attunement_skills.end())
                        attIt = wd.attunement_skills.begin();
                    outSkills.push_back(attIt->second[3]);
                    outSkills.push_back(attIt->second[4]);
                } else {
                    outSkills.push_back(wd.skills[3]);
                    outSkills.push_back(wd.skills[4]);
                }

                if (!outLabel.empty()) outLabel += " / ";
                outLabel += WeaponDisplayName(ohType);
            }
        }
    };

    // Weapon set A skills
    std::vector<uint32_t> wpnSkillsA, wpnSkillsB;
    std::string wpnLabelA, wpnLabelB;
    GetWeaponSkills("WeaponA1", "WeaponA2", wpnSkillsA, wpnLabelA);
    GetWeaponSkills("WeaponB1", "WeaponB2", wpnSkillsB, wpnLabelB);

    // Fetch skill details for weapon skills
    {
        std::vector<uint32_t> toFetch;
        for (auto id : wpnSkillsA) { if (id) toFetch.push_back(id); }
        for (auto id : wpnSkillsB) { if (id) toFetch.push_back(id); }
        if (!toFetch.empty()) AlterEgo::GW2API::FetchSkillDetailsAsync(toFetch);
    }

    float skillSz = ICON_SIZE;

    // While editing, the heal/util/elite icons reflect the draft and are clickable.
    // Revenant skills are bound to its legends, so they are not independently slottable.
    const AlterEgo::SavedBuild& skillSrc = g_LibEditMode ? g_EditDraft : build;
    bool skillEditable = g_LibEditMode && build.profession != "Revenant";

    // Compute heal/utility/elite offset: max weapon skill count + 1 icon gap
    int maxWpnCount = 5; // default for two-handed
    if (!wpnSkillsA.empty() && wpnSkillsA.size() > (size_t)maxWpnCount)
        maxWpnCount = (int)wpnSkillsA.size();
    if (!wpnSkillsB.empty() && wpnSkillsB.size() > (size_t)maxWpnCount)
        maxWpnCount = (int)wpnSkillsB.size();
    float utilOffset = (maxWpnCount + 1) * (skillSz + 4.0f);
    const bool sideBySide=ImGui::GetContentRegionAvail().x>=utilOffset+5*skillSz+4*ImGui::GetStyle().ItemSpacing.x;

    // Skills layout: weapon skills on the left, utility skills on the right
    if (!wpnSkillsA.empty() || !wpnSkillsB.empty()) {
        // Weapon Set A
        if (!wpnSkillsA.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", wpnLabelA.c_str());
            if(sideBySide){
                ImGui::SameLine(utilOffset);
                ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Heal / Utility / Elite");
            }

            ImGui::BeginGroup();
            for (size_t i = 0; i < wpnSkillsA.size(); i++) {
                if (i > 0) ImGui::SameLine();
                RenderSkillIcon(wpnSkillsA[i], skillSz);
            }
            ImGui::EndGroup();

            if(sideBySide)ImGui::SameLine(utilOffset);
            else ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Heal / Utility / Elite");
            ImGui::BeginGroup();
            RenderBuildSkillRow(skillSrc, skillSz, skillEditable);
            ImGui::EndGroup();
        } else {
            // No weapon set A, just show utility skills
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Heal / Utility / Elite");
            ImGui::BeginGroup();
            RenderBuildSkillRow(skillSrc, skillSz, skillEditable);
            ImGui::EndGroup();
        }

        // Weapon Set B
        if (!wpnSkillsB.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", wpnLabelB.c_str());
            ImGui::BeginGroup();
            for (size_t i = 0; i < wpnSkillsB.size(); i++) {
                if (i > 0) ImGui::SameLine();
                RenderSkillIcon(wpnSkillsB[i], skillSz);
            }
            ImGui::EndGroup();
        }
    } else {
        // Fallback: no weapon types selected, just show utility skills
        ImGui::Text("Skills");
        ImGui::BeginGroup();
        RenderBuildSkillRow(skillSrc, skillSz, skillEditable);
        ImGui::EndGroup();

        // Legacy weapons from chat link
        if (!build.weapons.empty()) {
            ImGui::Spacing();
            ImGui::Text("Weapons:");
            ImGui::SameLine();
            std::string wpnStr;
            for (size_t i = 0; i < build.weapons.size(); i++) {
                if (i > 0) wpnStr += ", ";
                wpnStr += AlterEgo::ChatLink::WeaponName((uint16_t)build.weapons[i]);
            }
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", wpnStr.c_str());
        }
    }

    const auto& aquaticBar=skillSrc.aquatic_skills;
    const bool recordedAquatic=aquaticBar.heal||aquaticBar.elite||
        aquaticBar.utilities[0]||aquaticBar.utilities[1]||aquaticBar.utilities[2]||
        (skillSrc.rev_active_palettes&&std::any_of(skillSrc.rev_active_palettes->begin()+5,
            skillSrc.rev_active_palettes->end(),[](const auto& row){return row[0]!=0;}));
    if((g_LibEditMode||recordedAquatic)&&ImGui::TreeNode("Aquatic skills##loadout")){
        RenderBuildSkillRow(skillSrc,skillSz,skillEditable,true);ImGui::TreePop();
    }
    if(skillSrc.profession=="Ranger"||skillSrc.profession=="Revenant"){
        const bool legend=skillSrc.profession=="Revenant";
        ImGui::Spacing();RenderSectionHeader(legend?"Legends":"Pets",AlterEgo::UI::SectionGold);
        auto slots=skillSrc.CompanionSlots(legend);
        const std::vector<AlterEgo::SkillEdit::EliteContext> contexts=legend?
            std::vector<AlterEgo::SkillEdit::EliteContext>{AlterEgo::GW2API::RevenantEliteContext({skillSrc.specializations[0].spec_id,skillSrc.specializations[1].spec_id,skillSrc.specializations[2].spec_id})}:
            std::vector<AlterEgo::SkillEdit::EliteContext>{};
        if(const auto changed=RenderProfessionPairSection(legend,slots,g_LibEditMode,contexts)){
            auto payload=skillSrc.CompanionPayload(legend);
            for(int i=0;i<4;++i)if(changed&(1u<<i)){
                const auto id=slots.values[i];
                AlterEgo::SkillEdit::SetCompanionPayloadSlot(payload,i,id);
                if(legend)g_EditDraft.legend_codes[i]=static_cast<uint8_t>(id);
                else if(i<2)g_EditDraft.pets.terrestrial[i]=id;
                else g_EditDraft.pets.aquatic[i-2]=id;
            }
            if(legend){g_EditDraft.legends_recorded=true;g_EditDraft.legends_source_slots=std::move(payload);}
            else{g_EditDraft.pets_recorded=true;g_EditDraft.pets_source_slots=std::move(payload);}
            MarkEditDirty();
        }
        if(legend)RenderRevenantContextWarnings(RevenantTypedContext(skillSrc,slots));
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Gear slots are already grouped as Armor, Weapons, and Trinkets.
    if(g_GearDialogOpen)ImGui::TextDisabled("Save or cancel the gear customization draft first.");
    RenderBuildGearPanel(g_LibEditMode ? g_EditDraft : build,g_LibEditMode&&g_EditRefsReady&&!g_GearDialogOpen&&
        AlterEgo::GetInfusionMode(g_EditDraft)!=AlterEgo::InfusionMode::Conflict);

    // One shared footer for Saved and Edit; notes always follow infusion totals.
    const auto& snapshot = g_LibEditMode ? g_EditDraft : build;
    RenderSavedInfusionMode(snapshot,
        g_LibEditMode&&g_EditRefsReady&&!g_GearDialogOpen ? &g_EditDraft : nullptr);
    ImGui::Spacing();
    // Use the standard white, regular-size section label in both modes.
    // Editing previously used muted brown and the read-only view compressed
    // the heading into the notes body.
    if (g_LibEditMode || !build.notes.empty()) {
        ImGui::TextUnformatted("Notes");
        if (g_LibEditMode) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (AlterEgo::UI::InputTextMultiline("##edit_notes", g_LibEditNotes, ImVec2(0, 50)))
                MarkEditDirty();
            if (ImGui::IsItemDeactivatedAfterEdit()) MarkEditDirty();
        } else {
            ImGui::TextWrapped("%s", build.notes.c_str());
        }
    }

}

// Render the full Build Library tab
// Read-only saved-vs-retrieved comparison. Only checks gear fields that were
// actually recorded in the saved set, so legacy builds don't produce false
// mismatches for unknown infusions or exact item IDs.
static int GetActiveBuildIndex(const AlterEgo::Character& ch) {
    for (int i = 0; i < (int)ch.build_tabs.size(); ++i)
        if (ch.build_tabs[i].is_active) return i;
    for (int i = 0; i < (int)ch.build_tabs.size(); ++i)
        if (ch.build_tabs[i].tab == ch.active_build_tab) return i;
    return ch.build_tabs.empty() ? -1 : 0;
}
static bool HasLiveSpecCacheMismatch(const AlterEgo::Character& ch) {
    if (!g_WarnStaleSpecialization) return false;
    const int index = GetActiveBuildIndex(ch);
    if (index < 0) return false;
    const auto cached=ch.build_tabs[index].specializations[2].spec_id;
    return ch.name==g_CurrentCharName&&g_LiveSpecialization!=0&&cached!=0&&g_LiveSpecialization!=cached;
}
static const char* GetSpecNameForWarning(uint32_t id) {
    const auto info = AlterEgo::GW2API::GetSpecInfo(id);
    return info && !info->name.empty() ? info->name.c_str() : "Unknown";
}
static int GetActiveEquipmentTab(const AlterEgo::Character& ch) {
    return ch.active_equipment_tab > 0 ? ch.active_equipment_tab : 1;
}
// GW2 API infusion attributes use internal names rather than the UI labels.
// Only a known *positive* modifier satisfies an attribute requirement;
// agony resistance and cosmetic effects never count as stat matches.
static const char* InfusionAttributeLabel(const std::string& attribute) {
    if (attribute == "Power") return "Power";
    if (attribute == "Precision") return "Precision";
    if (attribute == "ConditionDamage") return "Condition Damage";
    if (attribute == "ConditionDuration") return "Expertise";
    if (attribute == "BoonDuration") return "Concentration";
    if (attribute == "Healing") return "Healing Power";
    if (attribute == "Toughness") return "Toughness";
    if (attribute == "Vitality") return "Vitality";
    if (attribute == "CritDamage") return "Ferocity";
    return attribute.c_str();
}

static AlterEgo::ObservedInfusion GetObservedInfusion(uint32_t itemId) {
    AlterEgo::ObservedInfusion observed;
    observed.itemId = itemId;
    if (!itemId) return observed;
    const auto info = AlterEgo::GW2API::GetItemInfo(itemId);
    if (!info || !info->details.is_object()) return observed;
    auto upgrade = info->details.find("infix_upgrade");
    if (upgrade == info->details.end() || !upgrade->is_object()) return observed;
    auto attrs = upgrade->find("attributes");
    if (attrs == upgrade->end() || !attrs->is_array()) return observed;
    observed.metadataKnown = true;
    for (const auto& attr : *attrs) {
        if (!attr.is_object()||!attr.contains("attribute")||!attr["attribute"].is_string()||!attr.contains("modifier")||!attr["modifier"].is_number()){
            observed.metadataKnown=false;observed.positiveAttributes.clear();return observed;
        }
        const auto name = attr.value("attribute", std::string());
        auto modifier = attr.find("modifier");
        if (!name.empty() && modifier != attr.end() &&
            modifier->is_number() && modifier->get<double>() > 0)
            observed.positiveAttributes.push_back(name);
    }
    return observed;
}

static AlterEgo::BuildComparison CalculateSavedVsLiveComparison(
    const AlterEgo::SavedBuild& saved, const AlterEgo::Character& liveCharacter,
    const AlterEgo::BuildTemplate& live, int equipmentTab, bool compareExactItems) {
    AlterEgo::BuildComparison result;
    auto idKey = [](uint32_t id) { return std::to_string(id); };
    auto numberLabel = [](uint32_t id) { return id ? ("#" + std::to_string(id)) : "(empty)"; };
    auto specName = [&](uint32_t id) {
        const auto info = AlterEgo::GW2API::GetSpecInfo(id);
        return info ? info->name : numberLabel(id);
    };
    auto traitName = [&](uint32_t id) {
        const auto info = AlterEgo::GW2API::GetTraitInfo(id);
        return info ? info->name : numberLabel(id);
    };
    auto skillName = [&](uint32_t id) {
        const auto info = AlterEgo::GW2API::GetSkillInfo(id);
        return info&&!info->name.empty()?info->name:id?std::string("Metadata pending"):std::string("Unknown");
    };
    auto itemName = [&](uint32_t id) {
        const auto info = AlterEgo::GW2API::GetItemInfo(id);
        return info ? info->name : numberLabel(id);
    };
    auto addId = [&](const std::string& group, const std::string& field,
                     uint32_t a, uint32_t b, const std::function<std::string(uint32_t)>& display) {
        result.Add(group, field, idKey(a), idKey(b), display(a), display(b));
        if(group=="Traits")result.SetLastRowLinks(AlterEgo::LINK_TRAIT,{a,0,0},{b,0,0});
        else if(group=="Relic"||group=="Exact item IDs"||group=="Aquatic")result.SetLastRowLinks(AlterEgo::LINK_ITEM,{a,0,0},{b,0,0});
    };
    auto gearLinks=[](const nlohmann::json& gear,std::initializer_list<const char*> fields){
        std::vector<uint32_t> ids;
        for(const auto& entry:gear.at("gear"))for(const auto* field:fields){auto i=entry.find(field);if(i!=entry.end()&&i->is_number_unsigned())ids.push_back(i->get<uint32_t>());}
        return ids;
    };
    auto traitId = [&](uint32_t spec, int tier, int id) -> uint32_t {
        if (id >= 0) return (uint32_t)id;
        // Negative values in older imports encode an unresolved trait choice.
        const auto info = AlterEgo::GW2API::GetSpecInfo(spec);
        int choice = -id;
        if (info && info->major_traits.size() >= 9 && choice >= 1 && choice <= 3)
            return info->major_traits[tier * 3 + choice - 1];
        return 0; // unresolved: emit unverified evidence below
    };

    for (int i = 0; i < 3; ++i) {
        const auto& s = saved.specializations[i];
        const auto& l = live.specializations[i];
        std::string slot = "Line " + std::to_string(i + 1);
        if(!s.spec_id||(!l.spec_id&&!l.explicitly_empty))result.AddUnverified("Specializations",slot,s.spec_id?specName(s.spec_id):"Unknown",l.spec_id?specName(l.spec_id):"Unknown");
        else addId("Specializations",slot,s.spec_id,l.spec_id,specName);
        if (s.spec_id != l.spec_id || !s.spec_id) continue;
        const char* tiers[] = {"Adept", "Master", "Grandmaster"};
        for (int t = 0; t < 3; ++t) {
            uint32_t a = traitId(s.spec_id, t, s.traits[t]);
            uint32_t b = traitId(l.spec_id, t, l.traits[t]);
            const auto field=slot + " " + tiers[t];
            if (!a || (!b&&!(l.traits[t]==0&&(l.empty_traits&(1u<<t))))) result.AddUnverified("Traits",field, a?traitName(a):"Unknown", b?traitName(b):"Unknown");
            else addId("Traits",field,a,b,traitName);
            result.SetLastRowLinks(AlterEgo::LINK_TRAIT,{a,0,0},{b,0,0});
        }
    }
    auto compareSkills = [&](const char* group, const AlterEgo::SkillBar& a,
                              const AlterEgo::SkillBar& b) {
        result.AddSkill(group,"Heal",idKey(a.heal),idKey(b.heal),skillName(a.heal),skillName(b.heal),a.heal&&b.heal,a.heal&&(b.empty_slots&1));
        result.SetLastRowLinks(AlterEgo::LINK_SKILL,{a.heal,0,0},{b.heal,0,0});
        // Utility order is a control-layout preference, not a combat
        // configuration difference. Keep original order on disk/export.
        auto sortedUtilities = [&](const AlterEgo::SkillBar& bar) {
            const auto ids=AlterEgo::CanonicalUtilityIds({bar.utilities[0],bar.utilities[1],bar.utilities[2]});
            std::string key, names;
            for (uint32_t id : ids) {
                if (!key.empty()) { key += ","; names += ", "; }
                key += std::to_string(id);
                names += skillName(id);
            }
            return std::make_pair(key, names);
        };
        const auto sa = sortedUtilities(a), sb = sortedUtilities(b);
        const bool savedComplete=AlterEgo::CompleteUtilityIds({a.utilities[0],a.utilities[1],a.utilities[2]});
        const bool liveComplete=AlterEgo::CompleteUtilityIds({b.utilities[0],b.utilities[1],b.utilities[2]});
        bool liveKnown=true;
        for(int i=0;i<3;++i)liveKnown=liveKnown&&(b.utilities[i]||(b.empty_slots&(2u<<i)));
        result.AddSkill(group,"Utilities",sa.first,sb.first,sa.second,sb.second,
            savedComplete&&liveComplete,savedComplete&&liveKnown);
        result.SetLastRowLinks(AlterEgo::LINK_SKILL,{a.utilities[0],a.utilities[1],a.utilities[2]},{b.utilities[0],b.utilities[1],b.utilities[2]});
        result.AddSkill(group,"Elite",idKey(a.elite),idKey(b.elite),skillName(a.elite),skillName(b.elite),a.elite&&b.elite,a.elite&&(b.empty_slots&16));
        result.SetLastRowLinks(AlterEgo::LINK_SKILL,{a.elite,0,0},{b.elite,0,0});
    };
    compareSkills("Terrestrial skills", saved.terrestrial_skills, live.terrestrial_skills);
    const auto& waterSkills=saved.aquatic_skills;
    if(waterSkills.heal||waterSkills.elite||waterSkills.utilities[0]||waterSkills.utilities[1]||waterSkills.utilities[2])
        compareSkills("Aquatic skills", waterSkills, live.aquatic_skills);
    if(saved.profession=="Ranger"||saved.profession=="Revenant"){
        const bool legend=saved.profession=="Revenant";
        const auto group=legend?"Revenant legends":"Ranger pets";
        const auto expectedSlots=saved.CompanionSlots(legend),observedSlots=LiveCompanionSlots(live,legend);
        const auto display=[&](const AlterEgo::SkillEdit::CompanionProjection& slots,int i){
            const auto id=slots.values[i];
            return id?ResolveProfessionChoice(legend,id).name:std::string(AlterEgo::SkillEdit::CompanionMissingLabel(slots,i));
        };
        for(int i=0;i<4;++i){
            const bool land=i<2;const int slot=i%2;
            const auto expected=CanonicalCompanionIdentity(legend,expectedSlots.values[i]);
            const auto observed=CanonicalCompanionIdentity(legend,observedSlots.values[i]);
            const bool known=observedSlots.Known(i)&&(legend?live.legends_known:live.pets_known);
            const auto field=std::string(land?"Terrestrial ":"Aquatic ")+std::to_string(slot+1);
            result.AddCompanion(group,field,expected,observed,known,land||expectedSlots.Unresolved(i),
                display(expectedSlots,i),display(observedSlots,i));
        }
    }
    auto weaponList = [](const std::vector<uint32_t>& ids) {
        std::string str;
        for (uint32_t id : ids) {
            if (!str.empty()) str += ", ";
            str += std::to_string(id);
        }
        return str;
    };
    if (!saved.weapons.empty()) {
        if(!live.weapons_known&&live.weapons.empty())result.AddUnverified("Build","Weapon proficiency IDs",weaponList(saved.weapons),"Unknown");
        else result.Add("Build","Weapon proficiency IDs",weaponList(saved.weapons),weaponList(live.weapons),weaponList(saved.weapons),weaponList(live.weapons));
    }

    if (!saved.gear.empty() || !saved.infusion_totals.empty() || saved.relic_id || !saved.relic_name.empty()) {
        auto getEquipped = [&](const std::string& slot) -> const AlterEgo::EquipmentItem* {
            return AlterEgo::FindComparisonEquipment(liveCharacter.equipment,slot,equipmentTab);
        };
        for (const auto& entry : saved.gear) {
            const std::string& slot = entry.first;
            const auto& gs = entry.second;
            const auto* eq = getEquipped(slot);
            const bool aquatic=AlterEgo::IsAquaticComparisonSlot(slot);
            const auto group=[&](const char* land){return aquatic?"Aquatic":land;};
            if (!eq || !eq->id) {
                const bool tabKnown=liveCharacter.scope_stamps[AlterEgo::CharacterScopes::Index(AlterEgo::CharacterScopes::Tabs)]>0 &&
                    (liveCharacter.equipment_tab_names.count(equipmentTab)||std::any_of(liveCharacter.equipment.begin(),liveCharacter.equipment.end(),[&](const auto& item){return item.tab==equipmentTab;}));
                if(tabKnown)result.Add(group("Equipment"),slot,"present","missing","Equipped","Not equipped");
                else result.AddUnverified(group("Equipment"),slot,"Equipped","Equipment not verified");
                continue;
            }
            uint32_t liveStat = eq->stat_id;
            if (!liveStat && eq->id) {
                const auto info = AlterEgo::GW2API::GetItemInfo(eq->id);
                if (info && info->details.contains("infix_upgrade") &&
                    info->details["infix_upgrade"].contains("id") &&
                    info->details["infix_upgrade"]["id"].is_number_unsigned())
                    liveStat = info->details["infix_upgrade"]["id"].get<uint32_t>();
            }
            // A MetaBattle tile may supply a fixed ascended item ID and
            // canonical text but no selected itemstat ID. Resolve the saved
            // side from the same public item metadata as live equipment,
            // even if the library detail page has not yet been opened.
            uint32_t savedStat = gs.stat_id;
            if (!savedStat && gs.item_id) {
                const auto savedItem = AlterEgo::GW2API::GetItemInfo(gs.item_id);
                if (savedItem && savedItem->details.is_object() &&
                    savedItem->details.contains("infix_upgrade") &&
                    savedItem->details["infix_upgrade"].is_object() &&
                    savedItem->details["infix_upgrade"].contains("id") &&
                    savedItem->details["infix_upgrade"]["id"].is_number_unsigned()) {
                    const uint32_t possible = savedItem->details["infix_upgrade"]["id"].get<uint32_t>();
                    const auto definition = AlterEgo::GW2API::GetItemStatInfo(possible);
                    if (definition && (gs.stat_name.empty() ||
                        AlterEgo::NamedStats::SameCanonicalStat(gs.stat_name, definition->name)))
                        savedStat = possible;
                }
            }
            if (savedStat || !gs.stat_name.empty()) {
                const auto savedInfo = savedStat
                    ? AlterEgo::GW2API::GetItemStatInfo(savedStat) : nullptr;
                const auto liveInfo = liveStat
                    ? AlterEgo::GW2API::GetItemStatInfo(liveStat) : nullptr;
                const std::string savedName =
                    (savedInfo && !savedInfo->name.empty()) ? savedInfo->name
                    : (!gs.stat_name.empty() ? gs.stat_name : numberLabel(savedStat));
                const std::string liveName =
                    (liveInfo && !liveInfo->name.empty()) ? liveInfo->name
                    : (liveStat ? numberLabel(liveStat) : "(unknown)");
                const std::vector<std::string> emptyAttributes;
                const auto verdict = AlterEgo::CompareStatDefinitions(
                    savedStat, liveStat, savedInfo && !savedInfo->name.empty() ? savedInfo->name : gs.stat_name,
                    liveInfo ? liveInfo->name : std::string(),
                    savedInfo ? savedInfo->attributes : emptyAttributes,
                    liveInfo ? liveInfo->attributes : emptyAttributes);
                if (verdict == AlterEgo::StatEquivalence::Matches) {
                    // Identical itemstat IDs, or different IDs with the same
                    // public name and attribute definition, are equivalent.
                    result.Add(group("Equipment stats"), slot, "equivalent", "equivalent",
                               savedName, liveName);
                } else if (verdict == AlterEgo::StatEquivalence::Different) {
                    result.Add(group("Equipment stats"), slot, "saved-different", "live-different",
                               savedName, liveName);
                } else {
                    result.AddUnverified(group("Equipment stats"), slot, savedName,
                        liveName + " (not verified)");
                }
            }
            if (compareExactItems && gs.item_id)
                addId(group("Exact item IDs"), slot, gs.item_id, eq->id, itemName);
            // Rune and sigil placement is preserved in gear, but their
            // functional comparison is aggregated by type and weapon set.
            if (slot.rfind("Weapon", 0) == 0 || AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(slot)) {
                if (!gs.weapon_type.empty()) {
                    const auto info = AlterEgo::GW2API::GetItemInfo(eq->id);
                    if (info && info->details.contains("type") &&
                        info->details["type"].is_string()) {
                        auto liveType = info->details["type"].get<std::string>();
                        result.Add(group("Weapon types"), slot, gs.weapon_type, liveType,
                                   gs.weapon_type, liveType);
                    } else result.AddUnverified(group("Weapon types"),slot,gs.weapon_type,"Metadata unavailable");
                }
            }
        }
        // Aggregate runes across the six armor slots. Per-slot rune placement
        // is a physical detail; identical effects/counts are equivalent.
        nlohmann::json savedRunes = {{"gear", nlohmann::json::object()}};
        nlohmann::json liveRunes = {{"gear", nlohmann::json::object()}};
        const char* armorSlots[] = {
            "Helm","Shoulders","Coat","Gloves","Leggings","Boots"
        };
        std::set<std::string> observedEmptyRunes;
        int savedArmorPieces = 0;
        bool runeRequired = saved.rune_id != 0 || !saved.rune_name.empty();
        for (const char* slot : armorSlots) {
            auto savedItem = saved.gear.find(slot);
            if (savedItem != saved.gear.end()) {
                ++savedArmorPieces;
                const auto& gear = savedItem->second;
                const uint32_t runeId = gear.rune_id ? gear.rune_id : saved.rune_id;
                const std::string runeName = gear.rune.empty() ? saved.rune_name : gear.rune;
                if (runeId) savedRunes["gear"][slot]["rune_id"] = runeId;
                else if (!runeName.empty()) savedRunes["gear"][slot]["rune"] = runeName;
                if (runeId || !runeName.empty()) runeRequired = true;
            }
            const auto* liveItem = getEquipped(slot);
            if (liveItem && !liveItem->upgrades.empty() && liveItem->upgrades[0])
                liveRunes["gear"][slot]["rune_id"] = liveItem->upgrades[0];
            else if(liveItem&&liveItem->upgrades_observed&&liveItem->upgrades.empty())observedEmptyRunes.insert(slot);
        }
        if (runeRequired) {
            const auto expected = AlterEgo::Composable::Semantics::Runes(savedRunes);
            const auto observed = AlterEgo::Composable::Semantics::Runes(liveRunes,observedEmptyRunes);
            const auto verdict = AlterEgo::Composable::Semantics::Compare(expected, observed);
            auto label = [](const AlterEgo::Composable::Semantics::Summary& summary) {
                std::string str;
                for (const auto& [id, count] : summary.counts) {
                    if (!str.empty()) str += ", ";
                    str += std::to_string(count) + " x " + id;
                }
                return str.empty() ? std::string("(none)") : str;
            };
            if (savedArmorPieces < 6 ||
                verdict == AlterEgo::Composable::Semantics::Verdict::Unverified)
                result.AddUnverified("Runes", "Types / counts",
                    label(expected) + " (incomplete requirement)", label(observed));
            else result.AddVerdict("Runes","Types / counts",verdict==AlterEgo::Composable::Semantics::Verdict::Match?AlterEgo::BuildComparison::Verdict::Match:AlterEgo::BuildComparison::Verdict::Different,label(expected),label(observed));
            result.SetLastRowLinks(AlterEgo::LINK_ITEM,gearLinks(savedRunes,{"rune_id"}),gearLinks(liveRunes,{"rune_id"}));
        }

        // Sigils are interchangeable between hands in a weapon set, but
        // weapon sets A and B remain distinct. Preserve item positions for
        // inspection and original game-facing export.
        nlohmann::json savedSigils = {{"gear", nlohmann::json::object()}};
        nlohmann::json liveSigils = {{"gear", nlohmann::json::object()}};
        const char* weaponSlots[] = {"WeaponA1","WeaponA2","WeaponB1","WeaponB2"};
        std::set<std::string> comparedSets;
        bool unknownSigilIdentity = false;
        bool missingLiveSigilData = false;
        for (const char* slot : weaponSlots) {
            auto savedItem = saved.gear.find(slot);
            if (savedItem == saved.gear.end()) continue;
            const auto& gear = savedItem->second;
            if (gear.sigil_id || gear.sigil2_id || !gear.sigil.empty() || !gear.sigil2.empty()) {
                comparedSets.insert(AlterEgo::Composable::Semantics::WeaponSet(slot));
                if (!gear.sigil.empty() && !gear.sigil_id) unknownSigilIdentity = true;
                if (!gear.sigil2.empty() && !gear.sigil2_id) unknownSigilIdentity = true;
                if (gear.sigil_id) savedSigils["gear"][slot]["sigil_id"] = gear.sigil_id;
                else if (!gear.sigil.empty()) savedSigils["gear"][slot]["sigil"] = gear.sigil;
                if (gear.sigil2_id) savedSigils["gear"][slot]["sigil2_id"] = gear.sigil2_id;
                else if (!gear.sigil2.empty()) savedSigils["gear"][slot]["sigil2"] = gear.sigil2;
            }
        }
        for (const char* slot : weaponSlots) {
            if (!comparedSets.count(AlterEgo::Composable::Semantics::WeaponSet(slot))) continue;
            const auto* equipped = getEquipped(slot);
            if (!equipped) {
                // Missing live equipment is not evidence that a saved sigil
                // requirement was satisfied (or even definitely differs).
                if (savedSigils["gear"].contains(slot))
                    missingLiveSigilData = true;
                continue;
            }
            if(equipped->upgrades.empty()&&!equipped->upgrades_observed)missingLiveSigilData=true;
            if (!equipped->upgrades.empty() && equipped->upgrades[0])
                liveSigils["gear"][slot]["sigil_id"] = equipped->upgrades[0];
            if (equipped->upgrades.size() > 1 && equipped->upgrades[1])
                liveSigils["gear"][slot]["sigil2_id"] = equipped->upgrades[1];
        }
        if (!comparedSets.empty()) {
            const auto expected = AlterEgo::Composable::Semantics::Sigils(savedSigils);
            const auto observed = AlterEgo::Composable::Semantics::Sigils(liveSigils,!missingLiveSigilData);
            auto label = [](const AlterEgo::Composable::Semantics::Summary& summary) {
                std::string str;
                for (const auto& [id, count] : summary.counts) {
                    if (!str.empty()) str += ", ";
                    str += std::to_string(count) + " x " + id;
                }
                return str.empty() ? std::string("(none)") : str;
            };
            const auto verdict = AlterEgo::Composable::Semantics::Compare(expected, observed);
            if (unknownSigilIdentity || missingLiveSigilData ||
                verdict == AlterEgo::Composable::Semantics::Verdict::Unverified)
                result.AddUnverified("Sigils", "By weapon set", label(expected), label(observed));
            else result.AddVerdict("Sigils","By weapon set",verdict==AlterEgo::Composable::Semantics::Verdict::Match?AlterEgo::BuildComparison::Verdict::Match:AlterEgo::BuildComparison::Verdict::Different,label(expected),label(observed));
            result.SetLastRowLinks(AlterEgo::LINK_ITEM,gearLinks(savedSigils,{"sigil_id","sigil2_id"}),gearLinks(liveSigils,{"sigil_id","sigil2_id"}));
        }

        // Aquatic runes use five shared armor pieces plus the breather.
        const auto waterHelm=std::find_if(saved.gear.begin(),saved.gear.end(),[](const auto& entry){
            return AlterEgo::CanonicalComparisonSlot(entry.first)=="HelmAquatic";});
        if(waterHelm!=saved.gear.end()){
            const auto& g=waterHelm->second;
            if(g.rune_id||!g.rune.empty()){
                const auto* observed=getEquipped("HelmAquatic");
                if(g.rune_id&&observed&&observed->upgrades_observed&&observed->upgrades.empty())
                    addId("Aquatic","Breather rune",g.rune_id,0,itemName);
                else if(!g.rune_id||!observed||observed->upgrades.empty()||!observed->upgrades[0])
                    result.AddUnverified("Aquatic","Breather rune",g.rune_id?numberLabel(g.rune_id):g.rune,"Unverified");
                else {
                    auto expected=savedRunes,actual=liveRunes;
                    expected["gear"]["Helm"]={{"rune_id",g.rune_id}};
                    actual["gear"]["Helm"]={{"rune_id",observed->upgrades[0]}};
                    auto underwaterEmpty=observedEmptyRunes;underwaterEmpty.erase("Helm");
                    const auto e=AlterEgo::Composable::Semantics::Runes(expected),a=AlterEgo::Composable::Semantics::Runes(actual,underwaterEmpty);
                    const auto verdict=AlterEgo::Composable::Semantics::Compare(e,a);
                    if(verdict==AlterEgo::Composable::Semantics::Verdict::Unverified)
                        addId("Aquatic","Breather rune",g.rune_id,observed->upgrades[0],itemName);
                    else {
                        const auto label=[](const auto& summary){
                            std::string text;for(const auto& [key,count]:summary.counts){if(!text.empty())text+=", ";text+=std::to_string(count)+" x "+key;}return text;
                        };
                        result.AddVerdict("Aquatic","Underwater rune configuration",verdict==AlterEgo::Composable::Semantics::Verdict::Match?AlterEgo::BuildComparison::Verdict::Match:AlterEgo::BuildComparison::Verdict::Different,label(e),label(a));
                        result.SetLastRowLinks(AlterEgo::LINK_ITEM,gearLinks(expected,{"rune_id"}),gearLinks(actual,{"rune_id"}));
                    }
                }
            }
        }
        // Underwater weapons never pool upgrades with each other or land sets.
        for(const auto& [slot,g]:saved.gear){
            if(!AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(slot))continue;
            if(!g.sigil_id&&!g.sigil2_id&&g.sigil.empty()&&g.sigil2.empty())continue;
            const auto* eq=getEquipped(slot);
            std::vector<uint32_t> expected,observed;
            if(g.sigil_id)expected.push_back(g.sigil_id);
            if(g.sigil2_id)expected.push_back(g.sigil2_id);
            if(eq)for(auto id:eq->upgrades)if(id)observed.push_back(id);
            auto label=[&](const auto& ids){
                std::string s;for(auto id:ids){if(!s.empty())s+=", ";s+=itemName(id);}return s.empty()?std::string("Unknown"):s;
            };
            expected=AlterEgo::CanonicalUtilitySequence(std::move(expected));
            observed=AlterEgo::CanonicalUtilitySequence(std::move(observed));
            const auto field=AlterEgo::CanonicalComparisonSlot(slot)+" sigils";
            if((!g.sigil.empty()&&!g.sigil_id)||(!g.sigil2.empty()&&!g.sigil2_id)||!eq||(observed.empty()&&!eq->upgrades_observed))
                result.AddUnverified("Aquatic",field,label(expected),label(observed));
            else result.AddVerdict("Aquatic",field,expected==observed?AlterEgo::BuildComparison::Verdict::Match:AlterEgo::BuildComparison::Verdict::Different,label(expected),observed.empty()?"(empty)":label(observed));
            result.SetLastRowLinks(AlterEgo::LINK_ITEM,expected,observed);
        }

        // Slot mode is authoritative: compare only recorded physical requirements.
        if (AlterEgo::GetInfusionMode(saved)==AlterEgo::InfusionMode::Slots) {
            for(const auto& [slot,gear]:saved.gear){
                if(!AlterEgo::HasSlotInfusionData(gear))continue;
                if(slot=="Amulet"||slot=="Relic")continue;
                std::vector<AlterEgo::InfusionRequirement> wanted;
                for(size_t n=0;n<std::max(gear.infusion_ids.size(),gear.infusion_attributes.size());++n){
                    const uint32_t id=n<gear.infusion_ids.size()?gear.infusion_ids[n]:0;
                    const std::string attr=n<gear.infusion_attributes.size()?gear.infusion_attributes[n]:"";
                    if(id||!attr.empty())wanted.push_back({id,attr});
                }
                const auto* eq=getEquipped(slot);
                const char* infusionGroup=AlterEgo::IsAquaticComparisonSlot(slot)?"Aquatic":"Infusions";
                const auto infusionField=GearSlotDisplayName(slot)+std::string(" infusions");
                if(!eq||(!gear.infusion.empty()&&wanted.empty()&&!gear.infusion_snapshot)){
                    result.AddUnverified(infusionGroup,infusionField,"Recorded requirements","Equipment or exact metadata unavailable");
                    continue;
                }
                std::vector<AlterEgo::ObservedInfusion> installed;
                for(auto id:eq->infusions)if(id)installed.push_back(GetObservedInfusion(id));
                const auto capacity=ResolveGearInfusionCapacity(slot,saved.profession,gear.weapon_type,eq->id,false);
                const bool observedKnown=eq->infusions_observed||!installed.empty()||(capacity.verified&&capacity.limit==0);
                const auto verdict=AlterEgo::CompareInfusionRequirements(wanted,installed,observedKnown);
                std::string expected,actual;
                for(const auto& item:wanted){
                    if(!expected.empty())expected+=", ";
                    expected+=item.attribute.empty()?"#"+std::to_string(item.exactId):InfusionAttributeLabel(item.attribute);
                }
                for(const auto& item:installed){
                    if(!actual.empty())actual+=", ";
                    actual+="#"+std::to_string(item.itemId);
                }
                if(expected.empty())expected="(none)";
                if(actual.empty())actual="(none)";
                if(verdict==AlterEgo::InfusionMatchVerdict::NotVerified)
                    result.AddUnverified(infusionGroup,infusionField,expected,actual+" (metadata pending)");
                else result.Add(infusionGroup,infusionField,"equivalent",
                    verdict==AlterEgo::InfusionMatchVerdict::Matches?"equivalent":"different",expected,actual);
                std::vector<uint32_t> exact;for(const auto& requirement:wanted)if(requirement.attribute.empty()&&requirement.exactId)exact.push_back(requirement.exactId);
                result.SetLastRowLinks(AlterEgo::LINK_ITEM,std::move(exact),eq->infusions);
            }
        }

        if (saved.relic_id || !saved.relic_name.empty()) {
            const auto* eq = getEquipped("Relic");
            uint32_t id = eq ? eq->id : 0;
            const auto info = AlterEgo::GW2API::GetItemInfo(id);
            if (saved.relic_id&&id==saved.relic_id) addId("Relic","Item ID",saved.relic_id,id,itemName);
            else if (info && !saved.relic_name.empty())
                result.Add("Relic", "Effect / item name", saved.relic_name, info->name,
                           saved.relic_name, info->name);
            else if (saved.relic_id && id)
                addId("Relic", "Item ID", saved.relic_id, id, itemName);
            else result.AddUnverified("Relic","Effect / item",saved.relic_name.empty()?numberLabel(saved.relic_id):saved.relic_name,"Unavailable");
            result.SetLastRowLinks(AlterEgo::LINK_ITEM,{saved.relic_id,0,0},{id,0,0});
        }
    }

    // Compare the *total bonuses* as minimum requirements. AR caps at 150.
    // Source-wide imported infusion counts and per-slot snapshots use the same
    // calculator; unknown metadata is never reported as a mismatch.
    const auto mode=AlterEgo::GetInfusionMode(saved);
    if (mode==AlterEgo::InfusionMode::Conflict) {
        result.AddUnverified("Infusions", "Mode", "Mixed legacy requirements", "Resolve in Loadout Edit");
    } else if (mode==AlterEgo::InfusionMode::Totals) {
        const auto requiredTotals=GetSavedInfusionSummary(saved);
        // Aggregate guide targets are terrestrial. Compare each configured land
        // weapon set independently; never add the alternate set or aquatic gear.
        const auto available=LiveInfusionConfigurations(liveCharacter,equipmentTab);
        const bool specified=std::any_of(saved.gear.begin(),saved.gear.end(),[](const auto& entry){
            const auto& slot=entry.first;const auto& gear=entry.second;
            return (slot=="WeaponA1"||slot=="WeaponA2"||slot=="WeaponB1"||slot=="WeaponB2")&&
                (gear.item_id||!gear.weapon_type.empty());
        });
        auto configs=specified?SavedInfusionConfigurations(saved):available;
        configs.erase(std::remove_if(configs.begin(),configs.end(),[](auto c){
            return c==InfusionConfig::WaterA||c==InfusionConfig::WaterB;
        }),configs.end());
        for(auto config:configs){
        const auto liveTotals=GetLiveInfusionSummary(liveCharacter,equipmentTab,config);
        bool observedComplete=liveCharacter.scope_stamps[AlterEgo::CharacterScopes::Index(AlterEgo::CharacterScopes::Tabs)]>0;
        for(const auto& eq:liveCharacter.equipment){
            if(!eq.id||eq.tab!=equipmentTab||!AlterEgo::InfusionSlotInConfiguration(eq.slot,config)||eq.infusions_observed||!eq.infusions.empty())continue;
            const auto capacity=ResolveGearInfusionCapacity(eq.slot,liveCharacter.profession,"",eq.id,false);
            if(!capacity.verified||capacity.limit>0)observedComplete=false;
        }
        const std::string prefix=configs.size()>1?std::string(AlterEgo::InfusionConfigurationLabel(config))+" / ":"";
        if (!requiredTotals.complete || !liveTotals.complete ||
            liveCharacter.equipment.empty()||std::find(available.begin(),available.end(),config)==available.end()) {
            result.AddUnverified("Infusion totals", prefix+"Totals", "Unknown", "Metadata unavailable");
        } else {
            const int minAR = AlterEgo::AgonyRequirement(requiredTotals.agonyResistance);
            if (minAR > 0) {
                if(liveTotals.agonyResistance<minAR&&!observedComplete)
                    result.AddUnverified("Infusion totals",prefix+"Agony Resistance",std::to_string(minAR),"Incomplete observations");
                else result.Add("Infusion totals", prefix+"Agony Resistance",
                    "sufficient", liveTotals.agonyResistance >= minAR ? "sufficient" : "low",
                    std::to_string(requiredTotals.agonyResistance) +
                        " (minimum " + std::to_string(minAR) + ")",
                    std::to_string(liveTotals.agonyResistance));
            }
            for (const auto& [attr, target] : requiredTotals.stats) {
                if (target <= 0) continue;
                auto it = liveTotals.stats.find(attr);
                const int current = it == liveTotals.stats.end() ? 0 : it->second;
                if(current<target&&!observedComplete)
                    result.AddUnverified("Infusion totals",prefix+InfusionAttributeLabel(attr),"+"+std::to_string(target),"Incomplete observations");
                else result.Add("Infusion totals", prefix+InfusionAttributeLabel(attr),
                    "sufficient", current >= target ? "sufficient" : "low",
                    "+" + std::to_string(target), "+" + std::to_string(current));
            }
        }
        }
    }
    // Recognition coverage is separate from the directional comparison rows:
    // a partial requirement may match without proving a complete Loadout.
    bool completeBuild=liveCharacter.scope_stamps[AlterEgo::CharacterScopes::Index(AlterEgo::CharacterScopes::Builds)]>0;
    for(int i=0;i<3;++i)completeBuild=completeBuild&&saved.specializations[i].spec_id&&live.specializations[i].spec_id;
    std::set<std::string> recorded;
    for(const auto& [slot,g]:saved.gear)
        if(g.item_id||g.stat_id||!g.stat_name.empty()||!g.weapon_type.empty())
            recorded.insert(AlterEgo::CanonicalComparisonSlot(slot));
    if(saved.relic_id||!saved.relic_name.empty())recorded.insert("Relic");
    bool completeGear=liveCharacter.scope_stamps[AlterEgo::CharacterScopes::Index(AlterEgo::CharacterScopes::Tabs)]>0;
    bool hasLand=false;
    for(const auto& e:liveCharacter.equipment){
        const auto slot=AlterEgo::CanonicalComparisonSlot(e.slot);
        if(!e.id||!AlterEgo::IsEquipmentInCapturedBuild(slot,e.tab,equipmentTab)||AlterEgo::IsAquaticComparisonSlot(slot))continue;
        hasLand=true;
        if(!recorded.count(slot))completeGear=false;
    }
    bool aquaticPartial=false;
    if(std::any_of(saved.gear.begin(),saved.gear.end(),[](const auto& entry){return AlterEgo::IsAquaticComparisonSlot(entry.first);})){
        for(const auto& e:liveCharacter.equipment){
            const auto slot=AlterEgo::CanonicalComparisonSlot(e.slot);
            if(e.id&&e.tab==equipmentTab&&AlterEgo::IsAquaticComparisonSlot(slot)&&!recorded.count(slot))aquaticPartial=true;
        }
    }
    result.SetCoverage(completeBuild,completeGear&&hasLand&&!recorded.empty(),aquaticPartial);
    return result;
}

// The component collection renderer is defined later in this translation unit
// inside ComposableUI.h's anonymous namespace. Forward-declare in the same
// namespace so this call resolves to that implementation at link time.
namespace {
static void ComponentUIOpenLibrary(const std::string& kind, const std::string& id);
}

// Selection is character-first: empty means follow the active MumbleLink
// character, even if the previously selected saved build came from another hero.
static const AlterEgo::Character* ResolveComparisonCharacter(
    const std::vector<const AlterEgo::Character*>& matches) {
    if (matches.empty()) return nullptr;
    if (!g_LibCompareSelection.Empty()) {
        ResolveSelection(g_LibCompareSelection);
        for(const auto* ch:matches)if(MatchesSelection(g_LibCompareSelection,*ch))return ch;
        return nullptr; // keep manual target through incomplete adoption
    }
    const auto account=AlterEgo::GW2API::GetCurrentAccountName();
    for (const auto* ch : matches)
        if (!account.empty()&&ch->name == g_CurrentCharName&&ch->account_name==account) return ch;
    return matches.front();
}

static void RenderUnavailableCompareTarget(const std::vector<const AlterEgo::Character*>& matches){
    if(g_LibCompareSelection.Empty()){ImGui::TextDisabled("No retrieved characters. Refresh characters first.");return;}
    ImGui::TextDisabled("Selected character unavailable.");
    ImGui::SetNextItemWidth(ImMax(1.f,ImMin(290.f,ImGui::GetContentRegionAvail().x)));
    if(AlterEgo::UI::BeginCombo("##unavailableCompareTarget",g_LibCompareSelection.name.c_str())){
        if(AlterEgo::UI::SelectableOption("Automatic",false)){
            MarkNavigation();g_LibCompareSelection={};g_LibCompareSavedManual=false;
            g_LibCompareBuildTab=-1;g_LibCompareEquipTab=0;
        }
        for(const auto* ch:matches){
            ImGui::PushID(ch->account_name.c_str());ImGui::PushID(ch->name.c_str());
            if(SelectProfessionCharacterEntry("##availableCompareTarget",false,ch->profession,ch->name,nullptr,ImGuiSelectableFlags_None,ch->account_name)){
                MarkNavigation();g_LibCompareSelection=SelectCharacterIdentity(*ch);
                g_LibCompareSavedManual=false;g_LibCompareBuildTab=-1;g_LibCompareEquipTab=0;
            }
            ImGui::PopID();ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
static void RenderSavedVsLiveComparison(const AlterEgo::SavedBuild& saved) {
    const auto charactersSnapshot = AlterEgo::GW2API::GetCharacters();
    const auto& characters = *charactersSnapshot;
    std::vector<const AlterEgo::Character*> matches;
    EnsureCharDisplayOrder(charactersSnapshot);
    for(int i:g_CharDisplayOrder){
        const auto& ch=characters[i];if(!ch.build_tabs.empty())matches.push_back(&ch);
    }
    if (matches.empty()) {
        ImGui::TextWrapped("No retrieved characters with this profession. Refresh character data in the Characters tab.");
        return;
    }
    const AlterEgo::Character* character = ResolveComparisonCharacter(matches);
    if(!character){RenderUnavailableCompareTarget(matches);return;}
    if (g_LibCompareBuildTab >= (int)character->build_tabs.size())
        g_LibCompareBuildTab = -1;
    const int activeBuildIndex = GetActiveBuildIndex(*character);
    if (activeBuildIndex < 0) return;
    const int displayedBuildTab = g_LibCompareBuildTab < 0
        ? activeBuildIndex : g_LibCompareBuildTab;
    const auto& live = character->build_tabs[displayedBuildTab];

    if (character->name == g_CurrentCharName &&
        HasLiveSpecCacheMismatch(*character)) {
        const int cachedIndex = GetActiveBuildIndex(*character);
        const uint32_t currentId = g_LiveSpecialization;
        const uint32_t cachedId =
            character->build_tabs[cachedIndex].specializations[2].spec_id;
        ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.34f, 1.0f),
            "Cached specialization may be outdated (hover for details)");
        if (ImGui::IsItemHovered())
            AlterEgo::UI::DetailTooltip("Cached specialization may be outdated",(std::string("Live: ")+GetSpecNameForWarning(currentId)+"\nRetrieved: "+GetSpecNameForWarning(cachedId)+"\nRefresh character data to update.").c_str());
    }

    // A detailed comparison can perform many metadata lookups. Reuse its result
    // while the inputs are unchanged, rather than recomputing every render frame.
    // Expire after 500 ms as public item/stat metadata may arrive asynchronously
    // without changing the character or saved-build revision.
    struct ComparisonCache {
        AlterEgo::BuildComparison result;
        nlohmann::json componentDoc = nlohmann::json::object();
        std::string savedBuildId;
        std::string characterName,characterAccount;
        uint64_t characterRevision = 0;
        uint64_t libraryRevision = 0;
        int buildTab = -1;
        int equipmentTab = -1;
        bool exactItems = false;
        std::chrono::steady_clock::time_point updated{};
    };
    static ComparisonCache cache;
    const int equipmentTab = g_LibCompareEquipTab == 0
        ? GetActiveEquipmentTab(*character) : g_LibCompareEquipTab;
    const uint64_t characterRevision = AlterEgo::GW2API::GetCharacterDataRevision();
    const uint64_t libraryRevision = AlterEgo::GW2API::GetBuildLibraryRevision();
    const auto now = std::chrono::steady_clock::now();
    if (cache.savedBuildId != saved.id ||
        cache.characterName != character->name || cache.characterAccount!=character->account_name ||
        cache.characterRevision != characterRevision ||
        cache.libraryRevision != libraryRevision ||
        cache.buildTab != displayedBuildTab ||
        cache.equipmentTab != equipmentTab ||
        cache.exactItems != g_LibCompareExactItems ||
        now - cache.updated >= std::chrono::milliseconds(500)) {
        // Only the selected inputs need public definitions. Keep acquisition
        // separate from the pure comparison and its hover/context actions.
        DemandBuildReferenceMetadata(saved);
        DemandBuildReferenceMetadata(live);
        DemandSavedGearMetadata(saved);
        DemandVisibleEquipmentMetadata(*character,equipmentTab,false);
        cache.result = CalculateSavedVsLiveComparison(
            saved, *character, live, equipmentTab, g_LibCompareExactItems);
        if(cache.libraryRevision!=libraryRevision||cache.savedBuildId!=saved.id){
            cache.componentDoc=nlohmann::json::object();
            AlterEgo::GW2API::GetComposableLoadouts(cache.componentDoc);
        }
        cache.savedBuildId = saved.id;
        cache.characterName = character->name;
        cache.characterAccount=character->account_name;
        cache.characterRevision = characterRevision;
        cache.libraryRevision = libraryRevision;
        cache.buildTab = displayedBuildTab;
        cache.equipmentTab = equipmentTab;
        cache.exactItems = g_LibCompareExactItems;
        cache.updated = now;
    }
    const auto& result = cache.result;
    if(saved.profession=="Ranger"||saved.profession=="Revenant"){
        const bool legend=saved.profession=="Revenant";
        const auto expected=saved.CompanionSlots(legend),observed=LiveCompanionSlots(live,legend);
        if(legend){
            RenderRevenantContextWarnings(RevenantTypedContext(saved,expected),"Saved");
            RenderRevenantContextWarnings(RevenantTypedContext(live,observed),"Retrieved");
        }
        for(int env=0;env<2;++env){
            const bool a=CompanionPairConflict(legend,expected,env),b=CompanionPairConflict(legend,observed,env);
            if(a||b)ImGui::TextColored(AlterEgo::UI::WarningText,"%s duplicate companion choices (%s)",
                env?"Aquatic":"Terrestrial",a&&b?"saved and retrieved":a?"saved":"retrieved");
        }
    }

    // One independently expandable section per v3 component; fields retain
    // the same semantic comparison rules as the active-character matcher.
    struct CompareSection {
        std::string kind, label, sourceName, sourceId;
        bool recorded = false;
        std::vector<const AlterEgo::ComparisonRow*> rows;
        size_t differences = 0, unverified = 0, matches = 0;
    };
    const struct { const char* kind; const char* label; } categories[] = {
        {"base_build", "Base Build"}, {"skills", "Skills"}, {"pets", "Pets"}, {"legends", "Legends"},
        {"weapons", "Weapons"}, {"armor", "Armor"},
        {"trinkets", "Trinkets"}, {"runes", "Runes"},
        {"sigils", "Sigils"}, {"infusions", "Infusions"}, {"relic", "Relic"}, {"aquatic", "Aquatic"}
    };
    std::vector<CompareSection> sections;
    for (const auto& kind : categories)
        if(AlterEgo::Composable::KindApplies(kind.kind,saved.profession))sections.push_back({kind.kind, kind.label});

    // Resolve linked Equipment Set ownership using the canonical graph helper.
    // Cache this projection by the same library revision rather than parsing
    // the graph or touching storage for every render frame.
    struct SectionSources {
        uint64_t revision=0;std::string id;
        std::map<std::string,std::pair<std::string,std::string>> records;
    };
    static SectionSources sources;
    if(sources.revision!=libraryRevision||sources.id!=saved.id){
        sources={};sources.revision=libraryRevision;sources.id=saved.id;
        AlterEgo::Composable::Document doc;std::string error;
        if(AlterEgo::Composable::FromJson(cache.componentDoc,doc,error)){
            for(const auto& loadout:doc.loadouts){
                if(loadout.id!=saved.id)continue;
                for(const auto& [kind,id]:AlterEgo::Composable::EffectiveRefs(doc,loadout)){
                    const auto* part=AlterEgo::Composable::Find(doc,kind,id);
                    if(part&&!part->data.empty())sources.records[kind]={id,part->name};
                }
                break;
            }
        }
    }
    for(auto& section:sections){
        auto found=sources.records.find(section.kind);
        if(found==sources.records.end())continue;
        section.sourceId=found->second.first;section.sourceName=found->second.second;section.recorded=true;
    }

    for (const auto& row : result.Rows()) {
        const std::string kind =
            AlterEgo::Composable::Semantics::ComparisonComponent(row.group, row.field);
        auto found = std::find_if(sections.begin(), sections.end(),
            [&](const CompareSection& value) { return value.kind == kind; });
        if (found == sections.end()) {
            found = std::find_if(sections.begin(), sections.end(),
                [](const CompareSection& value) { return value.kind == "other"; });
            if (found == sections.end()) {
                sections.push_back({"other", "Other fields"});
                found = sections.end() - 1;
            }
        }
        found->rows.push_back(&row);
        if (!row.verified) ++found->unverified;
        else if (!row.matches) ++found->differences;
        else ++found->matches;
    }
    size_t matchingComponents = 0, differentComponents = 0;
    size_t unknownComponents = 0, optionalComponents=0, actionableComponents = 0;
    for (const auto& section : sections) {
        if (section.differences) ++differentComponents;
        else if(AlterEgo::Composable::IsOptionalKind(section.kind)&&!section.recorded&&section.rows.empty())++optionalComponents;
        else if ((section.kind != "other" && !section.recorded) ||
                 section.unverified || section.rows.empty()||(section.kind=="aquatic"&&result.AquaticPartial())) ++unknownComponents;
        else ++matchingComponents;
        if (section.differences || section.unverified) ++actionableComponents;
    }
    const size_t differences = result.DifferenceCount();
    const size_t unverified = result.UnverifiedCount();
    const ImVec4 warningColor = AlterEgo::UI::WarningText;
    const ImVec4 differenceColor = AlterEgo::UI::DifferenceText;
    if (differences)
        ImGui::TextColored(differenceColor, "%zu difference%s",
            differences, differences == 1 ? "" : "s");
    else if (unknownComponents)
        ImGui::TextColored(warningColor, "No verified differences");
    else
        ImGui::TextUnformatted("All compared components match");
    if (unverified) {
        ImGui::SameLine();
        ImGui::TextColored(warningColor, "| %zu unverified", unverified);
    }
    if (ImGui::IsItemHovered())
        AlterEgo::UI::Hint("Compared against retrieved character data. Unverified fields lack sufficient API data.");
    ImGui::TextDisabled("%zu matching / %zu different / %zu not fully checked",
        matchingComponents, differentComponents, unknownComponents);
    if (ImGui::IsItemHovered())
        AlterEgo::UI::Hint("Components with no saved data, no comparison rows, or unverified fields are not counted as matches.");
    if (saved.gear.empty())
        ImGui::TextDisabled("No saved equipment snapshot.");
    else if (character->equipment.empty())
        ImGui::TextDisabled("No retrieved equipment data.");
    ImGui::Checkbox("Show all components##compare", &g_LibCompareShowMatches);
    if (ImGui::IsItemHovered())
        AlterEgo::UI::Hint("Include matching fields and components with no recorded data.");
    ImGui::Separator();
    if (!actionableComponents && !g_LibCompareShowMatches)
        ImGui::TextDisabled("Nothing to change in the verified fields.");

    // Separate exact-ID acquisition aid; never changes semantic comparison.
    // Aggregate deficits across the selected tab before checking stock.
    if(!saved.gear.empty()&&!character->equipment.empty()){
        std::map<uint32_t,int> wanted,have;
        std::map<uint32_t,int> upgradesWanted,upgradesHave;
        for(const auto& [slot,gs]:saved.gear){
            if(gs.item_id)++wanted[gs.item_id];
            if(gs.rune_id)++upgradesWanted[gs.rune_id];
            else if(saved.rune_id&&(slot=="Helm"||slot=="Shoulders"||slot=="Coat"||slot=="Gloves"||slot=="Leggings"||slot=="Boots"))++upgradesWanted[saved.rune_id];
            if(gs.sigil_id)++upgradesWanted[gs.sigil_id];
            if(gs.sigil2_id)++upgradesWanted[gs.sigil2_id];
            if(AlterEgo::GetInfusionMode(saved)==AlterEgo::InfusionMode::Slots)for(size_t n=0;n<gs.infusion_ids.size();++n)
                if(gs.infusion_ids[n]&&(n>=gs.infusion_attributes.size()||gs.infusion_attributes[n].empty()))++upgradesWanted[gs.infusion_ids[n]];
        }
        if(saved.relic_id&&std::none_of(saved.gear.begin(),saved.gear.end(),[](const auto& entry){return entry.first=="Relic"&&entry.second.item_id!=0;}))++wanted[saved.relic_id];
        for(const auto& e:character->equipment)if(AlterEgo::IsEquipmentInCapturedBuild(e.slot,e.tab,equipmentTab)){
            for(auto id:e.upgrades)if(id)++upgradesHave[id];
            for(auto id:e.infusions)if(id)++upgradesHave[id];
            if(e.slot=="Relic"&&!saved.gear.count("Relic")&&e.id)++have[e.id];
        }
        for(const auto& [slot,gs]:saved.gear){
            (void)gs;const auto* eq=FindEquipment(*character,slot,equipmentTab);
            if(eq&&eq->id)++have[eq->id];
        }
        std::vector<std::pair<uint32_t,int>> missing;
        for(const auto& [id,count]:wanted)if(count>have[id])missing.emplace_back(id,count-have[id]);
        for(const auto& [id,count]:upgradesWanted)if(count>upgradesHave[id])missing.emplace_back(id,count-upgradesHave[id]);
        if(!missing.empty()&&ImGui::TreeNode("Exact saved item and upgrade locations")){
            ImGui::TextWrapped("Exact gear and upgrade ID deficits only; equivalent gear or attribute-based infusions may already match. Verify stats, restrictions and template reuse before moving items.");
            const auto verifiedOwner=AlterEgo::NativeAccounts::VerifiedAccountForCharacter(character->name);
            const auto owner=verifiedOwner==character->account_name?verifiedOwner:std::string{};
            std::map<uint32_t,int> totals;
            for(const auto& [id,count]:missing)totals[id]+=count;
            for(const auto& [id,count]:totals){
                ImGui::PushID(static_cast<int>(id));
                const auto item = AlterEgo::GW2API::GetItemInfo(id);
                std::string label=(item&&!item->name.empty()?item->name:"Item #"+std::to_string(id))+" x"+std::to_string(count)+"###acquisition";
                const bool expanded=ImGui::TreeNode(label.c_str());
                AlterEgo::UI::EntityContext("##acquisition_item_actions",[&]{CopyEntityMenu(item?item->name:std::string{},AlterEgo::LINK_ITEM,id);});
                if(expanded){
                    if(!owner.empty())AlterEgo::NativeAccounts::RenderItemAvailability(id,owner,character->name);
                    else ImGui::TextDisabled("Personal stock ownership unverified");
                    if(AlterEgo::UI::Button("Find in Inventory",ImVec2(0,0),AlterEgo::UI::ButtonRole::Compact))OpenInventoryItem(id);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    for (const auto& section : sections) {
        if (!g_LibCompareShowMatches && !section.differences && !section.unverified && !(section.kind=="aquatic"&&result.AquaticPartial()))
            continue;
        ImGui::PushID(section.kind.c_str());
        const std::string status =
            section.differences
                ? std::to_string(section.differences) + (section.differences == 1 ? " difference" : " differences")
            : section.unverified
                ? std::to_string(section.unverified) + " unverified"
            : (section.kind != "other" && !section.recorded) ? "Not recorded"
            : section.rows.empty()
                ? "Not checked"
                : section.kind=="aquatic"&&result.AquaticPartial()?"Partial match":"Matches";
        const std::string title = section.label + "  -  " + status +
            "###component_compare_section";
        const ImGuiTreeNodeFlags flags = section.differences
            ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        const bool expanded = ImGui::CollapsingHeader(title.c_str(), flags);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%zu match%s, %zu difference%s, %zu unverified\nSaved component: %s\nRight-click the header for component actions.",
                section.matches, section.matches == 1 ? "" : "es",
                section.differences, section.differences == 1 ? "" : "s",
                section.unverified,
                section.sourceName.empty() ? "No saved component name" :
                    section.sourceName.c_str());
        if (!section.sourceId.empty() &&
            ImGui::BeginPopupContextItem("##component_actions")) {
            CopyNameMenuItem(section.sourceName);
            if (ImGui::MenuItem("Open saved component"))
                ComponentUIOpenLibrary(section.kind, section.sourceId);
            ImGui::EndPopup();
        }
        if (expanded) {
            if (g_LibCompareShowMatches && !section.sourceName.empty()) {
                ImGui::TextDisabled("Saved: %s", section.sourceName.c_str());
                if (!section.sourceId.empty()) {
                    ImGui::SameLine();
                    if (RenderActionButton("Open component##compare_source"))
                        ComponentUIOpenLibrary(section.kind, section.sourceId);
                }
            }
            if (section.kind != "other" && !section.recorded)
                ImGui::TextDisabled("No component data recorded. Default field values cannot establish a match.");
            if (section.rows.empty()) {
                ImGui::TextDisabled(section.recorded
                    ? "No verifiable fields in retrieved character data."
                    : "No fields saved for this component.");
            } else if (ImGui::BeginTable("##comparison_section", 3,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();
                size_t displayed = 0;
                for (const auto* row : section.rows) {
                    if (row->matches && !g_LibCompareShowMatches) continue;
                    ++displayed;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (row->group == section.label)
                        ImGui::TextWrapped("%s", row->field.c_str());
                    else
                        ImGui::TextWrapped("%s / %s", row->group.c_str(),
                                           row->field.c_str());
                    auto renderEntity=[&](bool live){
                        const auto& label=live?row->current:row->saved;
                        const auto& ids=live?row->liveLinks:row->savedLinks;
                        const auto companion=live?row->liveCompanion:row->savedCompanion;
                        ImGui::PushID(row->group.c_str());ImGui::PushID(row->field.c_str());ImGui::PushID(live?1:0);
                        bool handled=false;
                        if(companion&&(row->group=="Ranger pets"||row->group=="Revenant legends")){
                            const bool legend=row->group=="Revenant legends";
                            const auto choice=ResolveProfessionChoice(legend,companion);
                            const auto hit=AlterEgo::UI::CatalogRow(0,false,choice.key,choice.icon,label,"",{},false,-1,
                                [&]{RenderCompanionTooltip(choice);},24.f,true);
                            (void)hit;
                            AlterEgo::UI::EntityContext("##companion_compare",[&]{ProfessionChoiceActions(legend,choice);});
                            handled=true;
                        }else if(row->linkType==AlterEgo::LINK_SKILL&&AlterEgo::IsSkillComparisonGroup(row->group)){
                            const auto count=std::count_if(ids.begin(),ids.end(),[](uint32_t id){return id!=0;});
                            if(count){
                                const float size=30.f;
                                const int columns=AlterEgo::SkillEdit::IconColumns(ImGui::GetContentRegionAvail().x,size,ImGui::GetStyle().ItemSpacing.x,3);
                                int shown=0;
                                for(size_t i=0;i<ids.size();++i)if(ids[i]){
                                    if(shown++%columns)ImGui::SameLine();
                                    ImGui::PushID(static_cast<int>(i));RenderSkillIcon(ids[i],size);ImGui::PopID();
                                }
                            }
                            ImGui::TextWrapped("%s",label.c_str());
                            handled=true;
                        }
                        if(!handled)ImGui::TextWrapped("%s",label.c_str());
                        ImGui::PopID();ImGui::PopID();ImGui::PopID();
                        return handled;
                    };
                    ImGui::TableSetColumnIndex(1);
                    const bool savedHandled=renderEntity(false);
                    auto linkMenu=[&](const std::vector<uint32_t>& ids,const char* popup){
                        if(!row->linkType)return;
                        std::set<uint32_t> unique;for(auto id:ids)if(ChatLinkIdValid(id))unique.insert(id);
                        if(unique.empty())return;
                        if(unique.size()==1&&ImGui::IsItemHovered()){
                            const auto id=*unique.begin();
                            if(row->linkType==AlterEgo::LINK_SKILL){const auto v=AlterEgo::GW2API::GetSkillInfo(id);if(v)RenderSkillTooltip(*v);}
                            else if(row->linkType==AlterEgo::LINK_TRAIT){const auto v=AlterEgo::GW2API::GetTraitInfo(id);if(v)RenderTraitTooltip(*v);}
                            else if(row->linkType==AlterEgo::LINK_ITEM){const auto v=AlterEgo::GW2API::GetItemInfo(id);if(v)RenderItemDefinitionTooltip(*v);}
                        }
                        ImGui::PushID(row->group.c_str());ImGui::PushID(row->field.c_str());
                        for(auto id:unique)ImGui::PushID(static_cast<int>(id));
                        AlterEgo::UI::EntityContext(popup,[&]{for(auto id:unique){
                            const auto type=static_cast<AlterEgo::ChatLinkType>(row->linkType);
                            std::string name;
                            if(type==AlterEgo::LINK_SKILL){const auto info=AlterEgo::GW2API::GetSkillInfo(id);if(info)name=info->name;}
                            else if(type==AlterEgo::LINK_TRAIT){const auto info=AlterEgo::GW2API::GetTraitInfo(id);if(info)name=info->name;}
                            else if(type==AlterEgo::LINK_ITEM){const auto info=AlterEgo::GW2API::GetItemInfo(id);if(info)name=info->name;}
                            ImGui::PushID(static_cast<int>(id));
                            if(unique.size()>1){if(AlterEgo::UI::BeginActionMenu(name.empty()?("#"+std::to_string(id)).c_str():name.c_str(),AlterEgo::UI::MenuIcon::Copy)){ReferenceEntityActions(name,type,id);ImGui::EndMenu();}}
                            else ReferenceEntityActions(name,type,id);
                            ImGui::PopID();
                        }});
                        for(size_t i=0;i<unique.size();++i)ImGui::PopID();ImGui::PopID();ImGui::PopID();
                    };
                    if(!savedHandled)linkMenu(row->savedLinks,"##saved_link_actions");
                    ImGui::TableSetColumnIndex(2);
                    if (!row->verified)
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0.92f, 0.81f, 0.50f, 1));
                    else if (!row->matches)
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(1, 0.58f, 0.50f, 1));
                    const bool liveHandled=renderEntity(true);
                    if (!row->verified || !row->matches) ImGui::PopStyleColor();
                    if(!liveHandled)linkMenu(row->liveLinks,"##live_link_actions");
                }
                if (!displayed) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("No differences in displayed fields.");
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }

}

// Expected identity, actual recognition and evidence are distinct. Both
// Characters and automatic Compare consume the same ranked candidate results.
static const AlterEgo::SavedBuild* FindCompatibleLoadout(const AlterEgo::Character& ch,const std::string& id){
    for(const auto& saved:AlterEgo::GW2API::GetSavedBuilds())
        if(saved.id==id&&saved.profession==ch.profession)return &saved;
    return nullptr;
}
static AlterEgo::CharacterLoadout::Choice ExpectedLoadout(const AlterEgo::Character& ch){
    using namespace AlterEgo::CharacterLoadout;
    const auto address=CharacterAddressFor(ch);
    if(!address)return {};
    return Resolve(AlterEgo::NativeAccounts::ExpectedLoadout(address->Reference()),[&](const auto& id){
        return FindCompatibleLoadout(ch,id)!=nullptr;
    });
}
static void SetExpectedLoadout(const AlterEgo::Character& ch,const AlterEgo::CharacterLoadout::Choice& choice){
    const auto address=CharacterAddressFor(ch);
    if(!address){if(APIDefs)APIDefs->GUI_SendAlert("Character identity unavailable. Refresh and try again.");return;}
    if(AlterEgo::NativeAccounts::ExpectedLoadout(address->Reference())==choice)return;
    if(choice.mode==AlterEgo::CharacterLoadout::Mode::Explicit&&!FindCompatibleLoadout(ch,choice.id))return;
    if(!AlterEgo::NativeAccounts::SetExpectedLoadout(*address,choice))
        if(APIDefs)APIDefs->GUI_SendAlert("Could not save Loadout assignment.");
}
struct RankedRecognition {
    std::string candidateId,matchedId;
    AlterEgo::ConfigurationHealth health;
    bool source=false,credible=false;
};
static RankedRecognition RankCharacterLoadouts(const AlterEgo::Character& ch,int buildIndex,int equipTab,bool credibleOnly){
    RankedRecognition best;
    if(buildIndex<0||buildIndex>=static_cast<int>(ch.build_tabs.size()))return best;
    const auto address=CharacterAddressFor(ch);
    const auto unique=address?AlterEgo::NativeAccounts::ResolveCharacterEvidence(address->account,address->binding.evidence):std::nullopt;
    const bool verifiedSource=address&&unique&&AlterEgo::CharacterIdentity::SameAddress(*address,*unique);
    for(const auto& saved:AlterEgo::GW2API::GetSavedBuilds()){
        if(saved.profession!=ch.profession)continue;
        const auto compared=CalculateSavedVsLiveComparison(saved,ch,ch.build_tabs[buildIndex],equipTab,false);
        auto health=AlterEgo::SummarizeHealth(compared);
        const auto& e=health.evidence;
        const bool source=verifiedSource&&saved.source_identity&&saved.source_identity->Matches(*address);
        const auto type=AlterEgo::ClassifyTemplateMatch(compared);
        bool gearVerified=compared.EquipmentCovered();
        for(const auto& row:compared.Rows())
            if(AlterEgo::IsCharacterEquipmentMatchGroup(row.group)&&(!row.verified||!row.matches))gearVerified=false;
        const bool credible=e.Full()||(type==AlterEgo::TemplateMatchStatus::Exact&&e.buildMatches>=3)||
            (type==AlterEgo::TemplateMatchStatus::SkillsDiffer&&gearVerified)||
            (source&&e.buildMatches>=3&&e.buildMatches>e.buildDifferences);
        if(e.Full()&&best.matchedId.empty())best.matchedId=saved.id;
        if(!e.Meaningful()||(credibleOnly&&!credible))continue;
        if(best.candidateId.empty()||AlterEgo::PreferRecognition(e,best.health.evidence,source,best.source)){
            best.candidateId=saved.id;best.health=std::move(health);best.source=source;best.credible=credible;
        }
    }
    return best;
}
struct ActiveSavedBuildMatch {
    std::string id,name,matchedId,tooltip;
    std::vector<std::string> warnings;
    bool show=false,full=false,explicitChoice=false;
};
static const ActiveSavedBuildMatch& GetLoggedInSavedBuildMatch(const AlterEgo::Character& ch) {
    static ActiveSavedBuildMatch cached;
    static std::pair<std::string,std::string> key;
    static uint64_t charRevision=0,libraryRevision=0,assignmentRevision=0;
    static int buildTab=-99,equipTab=-99;
    static std::chrono::steady_clock::time_point updated{};
    const auto currentKey=std::make_pair(ch.account_name,ch.name);
    const auto cr=AlterEgo::GW2API::GetCharacterDataRevision(),lr=AlterEgo::GW2API::GetBuildLibraryRevision();
    const auto ar=AlterEgo::NativeAccounts::LoadoutPreferenceRevision();
    const int bi=GetActiveBuildIndex(ch),et=GetActiveEquipmentTab(ch);
    const auto now=std::chrono::steady_clock::now();
    if(key==currentKey&&charRevision==cr&&libraryRevision==lr&&assignmentRevision==ar&&
        buildTab==bi&&equipTab==et&&now-updated<std::chrono::milliseconds(500))return cached;
    key=currentKey;charRevision=cr;libraryRevision=lr;assignmentRevision=ar;
    buildTab=bi;equipTab=et;updated=now;cached={};
    using Mode=AlterEgo::CharacterLoadout::Mode;
    const auto choice=ExpectedLoadout(ch);
    if(choice.mode==Mode::None)return cached;
    cached.show=true;
    cached.explicitChoice=choice.mode==Mode::Explicit;
    const auto ranked=RankCharacterLoadouts(ch,bi,et,true);
    cached.matchedId=ranked.matchedId;
    const auto* expected=FindCompatibleLoadout(ch,cached.explicitChoice?choice.id:ranked.candidateId);
    if(!expected){cached.name="No matching Loadout";cached.tooltip="Automatic recognition found no credible candidate in retrieved data.";return cached;}
    cached.id=expected->id;cached.name=expected->name;
    AlterEgo::ConfigurationHealth health;
    if(cached.explicitChoice){
        if(bi>=0)health=AlterEgo::SummarizeHealth(CalculateSavedVsLiveComparison(*expected,ch,ch.build_tabs[bi],et,false));
        else health=AlterEgo::SummarizeHealth(AlterEgo::BuildComparison{});
    }else health=ranked.health;
    cached.full=health.evidence.Full();
    if(cached.full)cached.matchedId=cached.id;
    cached.warnings=health.warnings;
    cached.tooltip=(cached.explicitChoice?"Expected: ":"Automatic candidate: ")+cached.name;
    if(cached.full)cached.tooltip+="\nVerified functional match";
    else for(const auto& detail:health.details)cached.tooltip+="\n"+detail;
    if(!cached.matchedId.empty()&&cached.matchedId!=cached.id)
        if(const auto* match=FindCompatibleLoadout(ch,cached.matchedId))cached.tooltip+="\nMatched: "+match->name;
    cached.tooltip+="\nBased on retrieved API data.";
    return cached;
}
static void NavigateToCharacterLoadoutCompare(const AlterEgo::Character& ch,const AlterEgo::SavedBuild& saved){
    ExecuteSavedLoadoutAction(SavedLoadoutAction::Compare,saved);
    g_LibCompareSelection=SelectCharacterIdentity(ch);
    g_LibComparePreviousSelection=g_LibCompareSelection;
    g_LibCompareBuildTab=-1;g_LibCompareEquipTab=0;
}
static void SavedLoadoutSelectorActions(const AlterEgo::SavedBuild& saved,const AlterEgo::Character* ch){
    using namespace AlterEgo::UI;
    CopyNameMenuItem(saved.name,"Copy Name",true);
    CopyNativeLinkMenuItem(saved.chat_link,"Copy Build Chat Link");
    if(MenuAction("Copy Portable Loadout",MenuIcon::Copy))ExecuteSavedLoadoutAction(SavedLoadoutAction::Copy,saved);
    if(BeginActionMenu("Share",MenuIcon::Share)){RenderBuildShareMenuItems(saved);ImGui::EndMenu();}
    ImGui::Separator();
    if(MenuAction("Compare",MenuIcon::Compare,!LibraryDraftOpen(),"Finish the current Library draft first.")){
        if(ch)NavigateToCharacterLoadoutCompare(*ch,saved);else ExecuteSavedLoadoutAction(SavedLoadoutAction::Compare,saved);
    }
    if(MenuAction("Open in Library",MenuIcon::Open,!LibraryDraftOpen(),"Finish the current Library draft first.")){
        g_LibSelectedId=saved.id;g_LibRevealId=saved.id;g_LibDetailsFetched=false;NavigateToLibraryCollection(0);
    }
}
static void RenderCharacterLoadoutContext(const AlterEgo::Character& ch,const AlterEgo::SavedBuild* saved,bool preferenceActions){
    using namespace AlterEgo::UI;
    if(saved)SavedLoadoutSelectorActions(*saved,&ch);
    if(preferenceActions){
        if(saved)ImGui::Separator();
        using namespace AlterEgo::CharacterLoadout;
        if(MenuAction("Reset to Automatic",MenuIcon::Regenerate))SetExpectedLoadout(ch,{});
        if(MenuAction("None",MenuIcon::Detach))SetExpectedLoadout(ch,{Mode::None,{}});
        if(const auto address=CharacterAddressFor(ch)){
            const auto pending=AlterEgo::NativeAccounts::RecoverableLoadoutPreferences(*address);
            if(!pending.empty()){
                ImGui::Separator();
                if(BeginActionMenu("Recover Previous Assignment",MenuIcon::Regenerate)){
                    int index=0;
                    for(const auto& record:pending){
                        const auto* target=FindCompatibleLoadout(ch,record.choice.id);
                        const bool available=record.choice.mode==Mode::None||target;
                        std::string label=record.name+" / "+(record.choice.mode==Mode::None?"None":target?target->name:"Unavailable Loadout");
                        ImGui::PushID(index++);
                        if(MenuAction(label.c_str(),MenuIcon::Open,available,"The referenced Loadout is missing or incompatible.")){
                            if(!AlterEgo::NativeAccounts::RecoverLoadoutPreference(*address,record)&&APIDefs)
                                APIDefs->GUI_SendAlert("Could not recover Loadout assignment.");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndMenu();
                }
            }
        }
    }
}
static const AlterEgo::SavedBuild* CharacterContextLoadout(const AlterEgo::Character& ch){
    const auto choice=ExpectedLoadout(ch);
    if(choice.mode==AlterEgo::CharacterLoadout::Mode::Explicit)return FindCompatibleLoadout(ch,choice.id);
    if(choice.mode==AlterEgo::CharacterLoadout::Mode::Automatic&&ch.name==g_CurrentCharName&&
        ch.account_name==AlterEgo::GW2API::GetCurrentAccountName()&&!ch.account_name.empty())
        return FindCompatibleLoadout(ch,GetLoggedInSavedBuildMatch(ch).id);
    return nullptr;
}
static void CharacterSelectorActions(const std::string& name,const std::string& account){
    using namespace AlterEgo;
    using namespace AlterEgo::UI;
    CopyNameMenuItem(name,"Copy Name",true);
    if(account.empty())return;
    const auto roster=GW2API::GetCharacters();
    const auto target=std::find_if(roster->begin(),roster->end(),[&](const auto& ch){return ch.name==name&&ch.account_name==account;});
    if(target==roster->end())return;
    const bool inScope=g_SelectedAccountFilter.empty()||g_SelectedAccountFilter==account;
    const bool canNavigate=!LibraryDraftOpen()&&!g_SaveLibDialogOpen&&inScope;
    ImGui::Separator();
    if(MenuAction("Open in Characters",MenuIcon::Open,canNavigate,"Finish the current draft or select the character's account first.")){
        MarkNavigation();g_SelectedCharacter=SelectCharacterIdentity(*target);
        g_CharSearchBuf[0]=0;g_DetailsFetched=false;g_SelectedBuildTab=-1;g_SelectedEquipTab=0;g_ForceCharactersTab=true;
    }
    if(const auto* loadout=CharacterContextLoadout(*target)){
        if(MenuAction("Compare",MenuIcon::Compare,canNavigate,"Finish the current draft or select the character's account first."))NavigateToCharacterLoadoutCompare(*target,*loadout);
    }else if(MenuAction("Compare",MenuIcon::Compare,canNavigate&&!target->build_tabs.empty(),"Requires retrieved build data and no open draft.")){
        MarkNavigation();g_LibCompareSelection=SelectCharacterIdentity(*target);g_LibComparePreviousSelection=g_LibCompareSelection;
        g_LibCompareSavedManual=false;g_LibCompareBuildId.clear();g_LibCompareBuildTab=-1;g_LibCompareEquipTab=0;g_SwitchToCompare=true;
    }
    if(FastLoadActionsEnabled()&&!LibraryDraftOpen()&&!g_SaveLibDialogOpen){ImGui::Separator();RenderCharacterLoadMenu("Load Character",name,account);}
}
static void RenderCharacterLoadoutSelector(const AlterEgo::Character& ch){
    using namespace AlterEgo::CharacterLoadout;
    if(!ValidKey({ch.account_name,ch.name}))return;
    if(!CharacterAddressFor(ch)){
        ImGui::TextDisabled("Loadout: Unavailable");
        if(ImGui::IsItemHovered())AlterEgo::UI::DetailTooltip("Expected Loadout","Character identity is not verified yet.");
        return;
    }
    const auto choice=ExpectedLoadout(ch);
    const auto* assigned=FindCompatibleLoadout(ch,choice.id);
    const std::string preview=assigned?assigned->name:choice.mode==Mode::None?"None":"Automatic";
    ImGui::PushID("character_expected_loadout");
    ImGui::PushID(ch.account_name.c_str());ImGui::PushID(ch.name.c_str());
    ImGui::TextUnformatted("Loadout");ImGui::SameLine();
    ImGui::SetNextItemWidth(ImMax(1.f,ImMin(290.f,ImGui::GetContentRegionAvail().x)));
    const bool open=AlterEgo::UI::BeginCombo("##expected",preview.c_str());
    if(open){
        if(AlterEgo::UI::SelectableOption("Automatic",choice.mode==Mode::Automatic))SetExpectedLoadout(ch,{});
        if(AlterEgo::UI::SelectableOption("None",choice.mode==Mode::None))SetExpectedLoadout(ch,{Mode::None,{}});
        ImGui::Separator();
        for(const auto& saved:AlterEgo::GW2API::GetSavedBuilds()){
            if(saved.profession!=ch.profession)continue;
            const auto label=saved.name+" ("+GameModeLabel(saved.game_mode)+")##"+saved.id;
            ImGui::PushID(saved.id.c_str());
            if(AlterEgo::UI::EntityOption(label.c_str(),assigned&&assigned->id==saved.id,[&]{SavedLoadoutSelectorActions(saved,&ch);}))
                SetExpectedLoadout(ch,{Mode::Explicit,saved.id});
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }else{
    if(ImGui::IsItemHovered())AlterEgo::UI::DetailTooltip("Expected Loadout","Independent of in-game tabs. Automatic recognizes a saved Loadout; None disables warnings. Right-click for actions.");
    const auto* contextLoadout=CharacterContextLoadout(ch);
    ImGui::PushID(static_cast<int>(choice.mode));ImGui::PushID(contextLoadout?contextLoadout->id.c_str():"");
    AlterEgo::UI::EntityContext("##expected_context",[&]{RenderCharacterLoadoutContext(ch,contextLoadout,true);});
    ImGui::PopID();ImGui::PopID();
    }
    ImGui::PopID();ImGui::PopID();ImGui::PopID();
}

// Dedicated top-level Compare tab. Keep the selection by saved build ID so
// reordering or deleting entries in Build Library doesn't select a wrong build.
static void RenderCompareTab() {
    g_LibCompareRenderedSelection={};
    const auto& builds = AlterEgo::GW2API::GetSavedBuilds();
    // Live selectors remain useful even with an empty saved-build library.
    // Use the exact character ordering selected on the Characters tab:
    // Custom, Name, Class, Level, Age, Birthday and Last Login, including the
    // ascending/descending toggle and multi-account grouping.
    // Active (auto) remains the first popup option regardless of sort mode.
    const auto compareRosterSnapshot = AlterEgo::GW2API::GetCharacters();
    const auto& compareRoster = *compareRosterSnapshot;
    EnsureCharDisplayOrder(compareRosterSnapshot);

    std::vector<const AlterEgo::Character*> compareMatches;
    compareMatches.reserve(compareRoster.size());
    std::vector<bool> added(compareRoster.size(), false);
    for (const int realIdx : g_CharDisplayOrder) {
        if (realIdx < 0 || realIdx >= static_cast<int>(compareRoster.size()) ||
            added[realIdx]) continue;
        added[realIdx] = true;
        const auto& ch = compareRoster[realIdx];
        if (!ch.build_tabs.empty()) compareMatches.push_back(&ch);
    }
    // Defensive fallback if roster data changed while order was rebuilt.
    for (size_t i = 0; i < compareRoster.size(); ++i)
        if (!added[i] && !compareRoster[i].build_tabs.empty())
            compareMatches.push_back(&compareRoster[i]);

    const AlterEgo::Character* target = ResolveComparisonCharacter(compareMatches);
    if(!target){RenderUnavailableCompareTarget(compareMatches);return;}
    g_LibCompareRenderedSelection=SelectCharacterIdentity(*target);
    ResolveSelection(g_LibComparePreviousSelection);
    if(!g_LibComparePreviousSelection.SameIdentity(g_LibCompareRenderedSelection)){
        if(!g_LibComparePreviousSelection.Empty())g_LibCompareSavedManual=false;
        g_LibComparePreviousSelection=g_LibCompareRenderedSelection;
        g_LibCompareBuildTab = -1;
        g_LibCompareEquipTab = 0;
    }

    static std::string autoChar,autoAccount,autoId;
    static uint64_t autoRev=0,autoLibraryRev=0;
    static int autoBuildTab=-99,autoEquipTab=-99;
    static std::chrono::steady_clock::time_point autoUpdated{};
    const uint64_t charRev=AlterEgo::GW2API::GetCharacterDataRevision(),libraryRev=AlterEgo::GW2API::GetBuildLibraryRevision();
    const int activeIndex=GetActiveBuildIndex(*target);
    const int currentBuild=g_LibCompareBuildTab>=0&&g_LibCompareBuildTab<static_cast<int>(target->build_tabs.size())?g_LibCompareBuildTab:activeIndex;
    const int currentEquip=g_LibCompareEquipTab>0?g_LibCompareEquipTab:GetActiveEquipmentTab(*target);
    const auto now=std::chrono::steady_clock::now();
    if (!g_LibCompareSavedManual &&
        (autoChar!=target->name||autoAccount!=target->account_name||autoRev!=charRev||autoLibraryRev!=libraryRev||
         autoBuildTab!=currentBuild||autoEquipTab!=currentEquip||now-autoUpdated>=std::chrono::milliseconds(500))) {
        autoChar=target->name;autoAccount=target->account_name;autoRev=charRev;autoLibraryRev=libraryRev;
        autoBuildTab=currentBuild;autoEquipTab=currentEquip;autoUpdated=now;
        autoId=RankCharacterLoadouts(*target,currentBuild,currentEquip,false).candidateId;
    }
    if (!g_LibCompareSavedManual) g_LibCompareBuildId = autoId;

    const AlterEgo::SavedBuild* saved = nullptr;
    for (const auto& b : builds) {
        if (b.id == g_LibCompareBuildId && b.profession == target->profession) {
            saved = &b;
            break;
        }
    }
    if (g_LibCompareSavedManual && !saved) {
        g_LibCompareSavedManual = false;
        g_LibCompareBuildId = autoId;
        for (const auto& b : builds)
            if (b.id == autoId && b.profession == target->profession) {
                saved = &b; break;
            }
    }
    const AlterEgo::Character* character = target;
    if (g_LibCompareBuildTab >= (int)character->build_tabs.size())
        g_LibCompareBuildTab = -1;
    const int activeBuild = GetActiveBuildIndex(*character);
    if (activeBuild < 0) return;
    const auto& live = character->build_tabs[g_LibCompareBuildTab < 0
        ? activeBuild : g_LibCompareBuildTab];
    const ImVec4 compareLabelColor = ImGui::ColorConvertU32ToFloat4(
        AlterEgo::CompletionTheme::kAvailableText);
    if (ImGui::BeginTable("##compareSelectors", 2,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(compareLabelColor, "Character");
        ImGui::TableNextColumn();
        ImGui::TextColored(compareLabelColor, "Saved Loadout");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        // Keep native ImGui popup/keyboard behavior, but draw profession names in
        // the same colors as the character list, including the closed preview.
        const auto currentAccount=AlterEgo::GW2API::GetCurrentAccountName();
        const auto isActive=[&](const AlterEgo::Character& ch){return AlterEgo::FastLoadUI::IsActive(ch.name,ch.account_name,g_CurrentCharName,currentAccount);};
        const char* autoPrefix = isActive(*character)?"Active: ":"Fallback: ";
        const ImVec2 previewMin = ImGui::GetCursorScreenPos();
        const float previewWidth = ImGui::GetContentRegionAvail().x;
        const float previewHeight = ImGui::GetFrameHeight();
        ImDrawList* previewDraw = ImGui::GetWindowDrawList();
        if (AlterEgo::UI::BeginCombo("##compareCharacter", "")) {
            const AlterEgo::Character* autoCharacter = compareMatches.front();
            for (const auto* ch : compareMatches)
                if (isActive(*ch)) { autoCharacter = ch; break; }
            const char* automaticPrefix=isActive(*autoCharacter)?"Active: ":"Fallback: ";
            const bool autoClicked = SelectProfessionCharacterEntry("##compareAutoCharacter",
                g_LibCompareSelection.Empty(),autoCharacter->profession,autoCharacter->name,automaticPrefix,ImGuiSelectableFlags_None,autoCharacter->account_name);
            if (autoClicked) {
                MarkNavigation();g_LibCompareSelection={};
                g_LibCompareSavedManual = false;
            }
            for (const auto* ch : compareMatches) {
                ImGui::PushID(ch->account_name.c_str());ImGui::PushID(ch->name.c_str());
                const bool selected=MatchesSelection(g_LibCompareSelection,*ch);
                const bool clicked = SelectProfessionCharacterEntry("##compareCharacterEntry",
                    selected,ch->profession,ch->name,nullptr,ImGuiSelectableFlags_None,ch->account_name);
                if (clicked) {
                    MarkNavigation();g_LibCompareSelection=SelectCharacterIdentity(*ch);
                    g_LibCompareSavedManual = false;
                    g_LibCompareBuildTab = -1;
                    g_LibCompareEquipTab = 0;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndCombo();
        }else{ImGui::PushID(character->account_name.c_str());ImGui::PushID(character->name.c_str());AlterEgo::UI::EntityContext("##compare_character_actions",[&]{CharacterSelectorActions(character->name,character->account_name);});ImGui::PopID();ImGui::PopID();}
        DrawProfessionCharacterEntry(previewDraw, previewMin,
            ImVec2(previewMin.x+previewWidth, previewMin.y+previewHeight),
            character->profession, character->name,
            g_LibCompareSelection.Empty() ? autoPrefix : nullptr, true);
        ImGui::TableNextColumn();
        const std::string chosenName = saved ?
            (g_LibCompareSavedManual ? "" : "Auto: ") +
                saved->name :
            "Choose a saved loadout";
        ImGui::SetNextItemWidth(-1.0f);
        if (AlterEgo::UI::BeginCombo("##savedBuildCompare", chosenName.c_str())) {
            if (ImGui::Selectable("Auto-match for selected character", !g_LibCompareSavedManual)) {
                MarkNavigation();
                g_LibCompareSavedManual = false;
                g_LibCompareBuildId.clear();
            }
            for (const auto& b : builds) {
                if (b.profession != target->profession) continue;
                const bool selected = saved && b.id == saved->id;
                const std::string label = b.name + " (" + GameModeLabel(b.game_mode) + ")";
                ImGui::PushID(b.id.c_str());
                if (AlterEgo::UI::EntityOption((label + "##" + b.id).c_str(), selected,[&]{SavedLoadoutSelectorActions(b,character);})) {
                    MarkNavigation();g_LibCompareBuildId = b.id;
                    g_LibCompareSavedManual = true;
                    saved = &b;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }else if(saved){ImGui::PushID(saved->id.c_str());AlterEgo::UI::EntityContext("##saved_compare_context",[&]{SavedLoadoutSelectorActions(*saved,character);});ImGui::PopID();}
        ImGui::EndTable();
    }
    auto tabLabel = [](const AlterEgo::BuildTemplate& bt) {
        return bt.name.empty() ? ("Tab " + std::to_string(bt.tab)) : bt.name;
    };
    std::string currentTabLabel = g_LibCompareBuildTab < 0
        ? "Active: " + tabLabel(live) : tabLabel(live);
    // The equipment selector belongs to retrieved character data, not the
    // saved library. It remains available when the character has no matching
    // saved build or when a saved build contains no equipment snapshot.
    const int maxTab = GetRetrievedEquipmentTabCount(*character);
    if (g_LibCompareEquipTab < 0 || g_LibCompareEquipTab > maxTab)
        g_LibCompareEquipTab = 0;
    const int activeTab = GetActiveEquipmentTab(*character);
    const int selectedEquipTab = g_LibCompareEquipTab == 0
        ? ((activeTab >= 1 && activeTab <= maxTab) ? activeTab : 1)
        : g_LibCompareEquipTab;
    auto equipmentTabName = [&](int tab) {
        const auto it = character->equipment_tab_names.find(tab);
        if (it != character->equipment_tab_names.end() && !it->second.empty())
            return it->second;
        return std::string("Tab ") + std::to_string(tab);
    };
    std::string equipmentLabel = equipmentTabName(selectedEquipTab);
    if (g_LibCompareEquipTab == 0)
        equipmentLabel = "Active: " + equipmentLabel;
    ImGui::Spacing();
    const bool customTabs = g_LibCompareBuildTab >= 0 || g_LibCompareEquipTab != 0;
    const char* optionsLabel = customTabs || g_LibCompareExactItems
        ? "Comparison options (custom)###compare_options"
        : "Comparison options###compare_options";
    const bool optionsOpen = ImGui::CollapsingHeader(optionsLabel);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Build: %s\nEquipment: %s\nItem matching: %s",
            currentTabLabel.c_str(), equipmentLabel.c_str(),
            g_LibCompareExactItems ? "Exact IDs" : "Equivalent stats");
    if (optionsOpen) {
        if (ImGui::BeginTable("##compareTabs", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(compareLabelColor, "Build Tab");
            ImGui::TableNextColumn();
            ImGui::TextColored(compareLabelColor, "Equipment Tab");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (AlterEgo::UI::BeginCombo("##compareBuildTab", currentTabLabel.c_str())) {
                if (ImGui::Selectable("Active (auto)##build", g_LibCompareBuildTab < 0)) {MarkNavigation();g_LibCompareBuildTab = -1;}
                for (int i = 0; i < (int)character->build_tabs.size(); ++i) {
                    const auto& bt = character->build_tabs[i];
                    std::string label = tabLabel(bt) + (bt.is_active ? " (active)" : "");
                    ImGui::PushID(bt.tab);
                    if (AlterEgo::UI::EntityOption(label.c_str(), i == g_LibCompareBuildTab,[&]{CopyNameMenuItem(bt.name,"Copy Name",true);CopyNativeLinkMenuItem(EncodeBuildTabChatLink(bt),"Copy Build Chat Link");})) {MarkNavigation();g_LibCompareBuildTab = i;}
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }else{ImGui::PushID(character->account_name.c_str());ImGui::PushID(character->name.c_str());ImGui::PushID(live.tab);AlterEgo::UI::EntityContext("##compare_build_tab_actions",[&]{CopyNameMenuItem(live.name,"Copy Name",true);CopyNativeLinkMenuItem(EncodeBuildTabChatLink(live),"Copy Build Chat Link");});ImGui::PopID();ImGui::PopID();ImGui::PopID();}
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (AlterEgo::UI::BeginCombo("##compareEquipTab", equipmentLabel.c_str())) {
                if (ImGui::Selectable("Active (auto)##equipment", g_LibCompareEquipTab == 0)) {MarkNavigation();g_LibCompareEquipTab = 0;}
                for (int tab = 1; tab <= maxTab; ++tab) {
                    std::string name = equipmentTabName(tab);
                    if (tab == character->active_equipment_tab) name += " (active)";
                    ImGui::PushID(tab);
                    if (AlterEgo::UI::EntityOption(name.c_str(), tab == g_LibCompareEquipTab,[&]{CopyNameMenuItem(equipmentTabName(tab),"Copy Name",true);})) {MarkNavigation();g_LibCompareEquipTab = tab;}
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }else if(g_LibCompareEquipTab>0)NameOptionContext(equipmentTabName(g_LibCompareEquipTab));
            ImGui::EndTable();
        }
        if (saved && !saved->gear.empty()) {
            ImGui::Checkbox("Exact item IDs##compare", &g_LibCompareExactItems);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Require identical item IDs instead of equivalent equipment stats.");
        }
    }
    if (!saved) {
        if (builds.empty())
            ImGui::TextDisabled("No builds saved yet. Use Save on Characters to capture this build and equipment.");
        else
            ImGui::TextDisabled("No saved build matches %s. Choose one above or save a build for this character.",
                target->name.c_str());
        return;
    }
    ImGui::Spacing();
    ImGui::BeginChild("##CompareResultScroll", ImVec2(0, 0), false);
    RenderSavedVsLiveComparison(*saved);
    ImGui::EndChild();
}

#include "LibraryBackupUI.h"
#include "CacheDiagnostics.h"
#include "CacheMaintenance.h"
static AlterEgo::Storage::MaintenanceResult g_StorageMaintenanceResult;
static bool g_StorageMaintenancePending=false;
static std::string g_StorageMaintenanceMessage;
#include "ComposableUI.h"

static bool OtherLibraryEditorOpen(){
    return g_FashionDialog||g_ComponentEditorOpen||g_ComponentEditorSaveQueued||
        g_ReferenceEditor.open||g_ReferenceEditor.queued||g_SaveLibDialogOpen;
}
static bool LibraryDraftOpen(){
    return g_LibEditMode||g_GearDialogOpen||OtherLibraryEditorOpen()||g_NewBuildDialogOpen||g_LibShowImport;
}

static void RenderLibraryImportDialog() {
    // Keep the import form and its asynchronous palette-data retry inside a
    // modal; it no longer shifts the library list and details while open.
    if (g_LibShowImport && !ImGui::IsPopupOpen("Import to Library##lib"))
        ImGui::OpenPopup("Import to Library##lib");
    const ImVec2 popupDisplay = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(
        ImVec2(popupDisplay.x * 0.5f, popupDisplay.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(550.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import to Library##lib", &g_LibShowImport,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Build, wardrobe chat link, or URL");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        AlterEgo::UI::InputTextMultiline("##import_link", g_LibImportBuf,
            sizeof(g_LibImportBuf), ImVec2(-1.0f, 75.0f));
        if (g_LibImportBuf[0] == '\0') {
            // ImGui 1.80 has no InputTextMultilineWithHint. Draw the hint on
            // top of the *empty* multiline input, with the normal disabled-text
            // color and font; the native input remains the only hit target.
            const ImVec2 from = ImGui::GetItemRectMin();
            const ImVec2 to = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(from, to, true);
            const ImVec2 pad = ImGui::GetStyle().FramePadding;
            dl->AddText(ImVec2(from.x + pad.x + 2.0f, from.y + pad.y + 1.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                AlterEgo::BuildRelay::ImportHint());
            dl->PopClipRect();
        }

        ImGui::TextUnformatted(AlterEgo::BuildRelay::ImportNameLabel());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        AlterEgo::UI::InputText("##import_name", g_LibImportName, sizeof(g_LibImportName));
        const std::string importInput(g_LibImportBuf);
        const bool sourceUrl = importInput.find("://") != std::string::npos;
        const bool wardrobe = AlterEgo::ChatLink::DetectType(importInput) == AlterEgo::LINK_WARDROBE_TMPL;
        if (!sourceUrl && !wardrobe) {
            ImGui::TextUnformatted("Game modes");
            RenderGameModeSelector("import-modes", g_LibImportModes);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Chat: %s (optional)",
            g_ChatAddonConnected ? "Connected" : "Not detected");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Events: Chat integration is optional. Install it from the Nexus addon library to import links shared in GW2 chat.");

        // Auto-retry once the profession's palette data finishes loading
        bool autoRetry = false;
        if (g_LibImportPending && !g_LibImportPendingProfession.empty() &&
            AlterEgo::GW2API::HasPaletteData(g_LibImportPendingProfession)) {
            g_LibImportPending = false;
            g_LibImportPendingProfession.clear();
            autoRetry = true;
        }

        ImGui::Spacing();
        if ((RenderActionButton("Import##libSnow") || autoRetry) && !g_BuildSourceBusy) {
            std::string input(g_LibImportBuf);
            std::string name(g_LibImportName);
            if (input.empty()) {
                g_LibImportError = AlterEgo::BuildRelay::EmptyImportError();
            } else if (input.find("://") != std::string::npos) {
                std::string url = AlterEgo::SnowCrows::NormalizeUrl(input);
                if (url.empty()) url = AlterEgo::MetaBattle::NormalizeUrl(input);
                if (url.empty())
                    g_LibImportError = "Use a Snow Crows or MetaBattle build-page URL.";
                else {
                    FetchBuildSource(url);
                    g_LibImportError.clear();
                }
            } else if (AlterEgo::ChatLink::DetectType(input) == AlterEgo::LINK_WARDROBE_TMPL) {
                if (StartFashionImport(input, name)) {
                    g_LibShowImport = false;
                    ImGui::CloseCurrentPopup();
                    NavigateToLibraryCollection(1);
                    g_LibImportBuf[0] = '\0';
                    g_LibImportName[0] = '\0';
                    g_LibImportError.clear();
                } else {
                    g_LibImportError = "Invalid or truncated Wardrobe Template chat link.";
                }
            } else {
                // Try shared build format (AE2, relay code, or chat link)
                AlterEgo::SavedBuild sharedBuild;
                std::string sharedError;
                if (ImportSharedBuild(input, sharedBuild, sharedError)) {
                    // If the user entered a name, honour it (AE2/relay imports
                    // fall back to the profession as a placeholder otherwise).
                    if (!name.empty()) sharedBuild.name = name;
                    sharedBuild.game_modes = g_LibImportModes;
                    sharedBuild.game_mode = g_LibImportModes.front();
                    int newIdx = AlterEgo::GW2API::AddSavedBuild(std::move(sharedBuild));
                    g_LibShowImport = false;
                    ImGui::CloseCurrentPopup();
                    g_LibImportBuf[0] = '\0';
                    g_LibImportName[0] = '\0';
                    g_LibImportError.clear();
                    if (newIdx >= 0) { SelectLoadoutAt(newIdx); NavigateToLibraryCollection(0); }
                    g_LibDetailsFetched = false;
                    if (APIDefs) APIDefs->GUI_SendAlert("Shared build imported!");
                } else if (!sharedError.empty()) {
                    // Shared format detected but failed. If the failure is just
                    // that the palette is still loading, arm an auto-retry so
                    // the import happens once the data arrives.
                    if (sharedError.rfind("Loading ", 0) == 0) {
                        // Extract profession from "Loading <Prof> skill data..."
                        size_t end = sharedError.find(' ', 8);
                        if (end != std::string::npos) {
                            g_LibImportPendingProfession = sharedError.substr(8, end - 8);
                            g_LibImportPending = true;
                        }
                    }
                    g_LibImportError = sharedError;
                } else {
                    // Not a shared format — try as chat link
                    if (name.empty()) {
                        g_LibImportError = "Enter a build name (required for chat links).";
                    } else {
                        auto linkType = AlterEgo::ChatLink::DetectType(input);
                        if (linkType != AlterEgo::LINK_BUILD) {
                            g_LibImportError = "Not a recognized build or wardrobe format.";
                        } else {
                            AlterEgo::DecodedBuildLink raw;
                            if (!AlterEgo::ChatLink::DecodeBuild(input, raw)) {
                                g_LibImportError = "Failed to decode build link.";
                            } else {
                                std::string prof = ProfessionFromCode(raw.profession);
                                if (prof == "Unknown") {
                                    g_LibImportError = "Unknown profession in link.";
                                } else if (!AlterEgo::GW2API::HasPaletteData(prof)) {
                                    AlterEgo::GW2API::FetchProfessionPaletteAsync(prof);
                                    g_LibImportError = "Loading " + prof + " skill data... try again in a moment.";
                                } else {
                                    AlterEgo::SavedBuild build;
                                    if (DecodeBuildLink(input, name,
                                            g_LibImportModes.front(), build)) {
                                        build.game_modes = g_LibImportModes;
                                        int newIdx = AlterEgo::GW2API::AddSavedBuild(std::move(build));
                                        g_LibShowImport = false;
                                        ImGui::CloseCurrentPopup();
                                        g_LibImportBuf[0] = '\0';
                                        g_LibImportName[0] = '\0';
                                        g_LibImportError.clear();
                                        if (newIdx >= 0) { SelectLoadoutAt(newIdx); NavigateToLibraryCollection(0); }
                                        g_LibDetailsFetched = false;
                                        if (APIDefs) APIDefs->GUI_SendAlert("Build imported!");
                                    } else {
                                        g_LibImportError = "Failed to decode build.";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ImGui::SameLine();
        if (RenderActionButton("Cancel##libSnow")) {
            g_LibShowImport = false;
            g_LibImportPending = false;
            g_LibImportPendingProfession.clear();
            g_LibImportError.clear();
            ImGui::CloseCurrentPopup();
        }
        if (g_BuildSourceBusy)
            ImGui::TextDisabled("Fetching build source...");
        if (!g_LibImportError.empty())
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text),
                               "%s", g_LibImportError.c_str());
        ImGui::EndPopup();
    }
    if (!g_LibShowImport) {
        g_LibImportPending = false;
        g_LibImportPendingProfession.clear();
    }

    ImGui::Spacing();


}

static void LoadoutCreationMenu(){
    using namespace AlterEgo::UI;
    const bool ready=!LibraryDraftOpen();
    if(MenuAction("New",MenuIcon::New,ready,"Finish the current Library editor first.")){
        g_NewBuildModes={AlterEgo::GameMode::PvE};g_NewBuildDialogOpen=true;
    }
    if(MenuAction("Paste",MenuIcon::Paste,ready,"Finish the current Library editor first."))PastePortableLoadout();
}
static void RenderBuildLibrary() {
    const auto& builds = AlterEgo::GW2API::GetSavedBuilds();

    // Left panel: build list, Right panel: preview
    const AlterEgo::UI::SplitPanel split(g_LibListWidth,220.f);

    // Defer drag operations until neither pane holds a reference to a build.
    std::string pendingGroup, pendingGroupTarget;
    std::string pendingItemId, pendingItemTarget;
    bool pendingGroupAfter = false, pendingItemAfter = false;
    bool selectedMatches=false;
    ImGui::BeginChild("LibList", ImVec2(split.left, 0), true);
    {
        AlterEgo::UI::SearchToolbar("loadouts", "Search loadouts...", g_LibSearchBuf,
            [&] {
                AlterEgo::UI::FilterChips chips;
                for(const auto& mode:g_LibFilterModes)
                    chips.push_back({"mode:"+mode,mode,[mode]{g_LibFilterModes.erase(mode);SaveSession();}});
                return chips;
            },
            [&] {
                std::vector<std::pair<std::string,std::string>> options;
                for(const auto& mode:AlterEgo::LibraryFilter::Modes())options.emplace_back(mode,mode);
                if(AlterEgo::UI::MultiFilterChoice("Game mode",g_LibFilterModes,options))SaveSession();
            },
            [&] {g_LibFilterModes.clear();SaveSession();});
        ImGui::Separator();

        if (builds.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "No saved Loadouts.\nUse Library > New or\nImport from Link / URL\nto add one.");
        }
        // Build one visual profession section for every distinct profession.
        // Existing JSON is still a flat array and retains the original order.
        struct LibraryGroup {
            std::string profession;
            std::vector<int> visible;
            int total = 0;
        };
        std::vector<LibraryGroup> groups;
        std::unordered_map<std::string, size_t> groupLookup;
        auto searchWords=AlterEgo::Search::Words(g_LibSearchBuf);
        const auto matchesSearch=[&](const auto& b){
            if(searchWords.empty())return true;
            std::vector<std::string> fields{b.name,b.profession};
            for(const auto& specialization:b.specializations){
                const auto info=AlterEgo::GW2API::GetSpecInfo(specialization.spec_id);
                if(info)fields.push_back(info->name);
            }
            return AlterEgo::Search::MatchesFields(searchWords,fields);
        };
        // Explicit open/create/duplicate reveals the selected record, but
        // Back/Forward restores the captured scroll and filters instead.
        const auto revealId=g_LibRevealId;
        if(!revealId.empty()&&revealId==g_LibSelectedId){
            const auto index=AlterEgo::RecordSelection::ById(builds,revealId);
            if(index>=0){
                const auto& target=builds[index];
                bool changed=AlterEgo::GroupBrowse::Reveal(g_LibCollapsedProfessions,target.profession,true);
                if(!GameModeMatchesFilter(target,g_LibFilterModes)){g_LibFilterModes.clear();changed=true;}
                if(!matchesSearch(target)){g_LibSearchBuf[0]='\0';searchWords.clear();}
                if(changed)SaveSession();
            }
        }
        const bool searching=!searchWords.empty();
        for (int i = 0; i < static_cast<int>(builds.size()); ++i) {
            const auto& b = builds[i];
            auto found = groupLookup.find(b.profession);
            if (found == groupLookup.end()) {
                const size_t index = groups.size();
                found = groupLookup.emplace(b.profession, index).first;
                groups.push_back(LibraryGroup{b.profession, {}, 0});
            }
            LibraryGroup& group = groups[found->second];
            ++group.total;
            if (!GameModeMatchesFilter(b, g_LibFilterModes)) continue;
            if(!matchesSearch(b))continue;
            group.visible.push_back(i);
            if(b.id==g_LibSelectedId)selectedMatches=true;
        }

        bool anyGroupDrawn = false;
        for (const auto& group : groups) {
            if (group.visible.empty()) continue;
            const std::string& profession = group.profession;
            ImGui::PushID(profession.c_str());
            if (anyGroupDrawn) ImGui::Spacing();
            anyGroupDrawn = true;
            const bool expanded = AlterEgo::GroupBrowse::Expanded(g_LibCollapsedProfessions,profession,true,searching);

            // No sidebar font override: same heading scale, +/- control,
            // and vertical metrics as Vault and Clears.
            if (AlterEgo::CompletionTheme::CenteredSectionToggle(
                    expanded, profession.c_str()) && AlterEgo::GroupBrowse::Toggle(g_LibCollapsedProfessions,profession,searching)) SaveSession();
            const std::string displayName =
                profession == "Necromancer" ? "Necro" : profession;
            const std::string count = std::to_string(group.visible.size());
            const uint32_t iconId = GetProfessionIconId(profession);
            const char* iconUrl = GetProfessionIconUrl(profession);
            if (iconId && !iconUrl)
                AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
            Texture_t* tex = iconId ? AlterEgo::IconManager::GetIcon(iconId) : nullptr;
            if (iconId && (!tex || !tex->Resource))
                AlterEgo::IconManager::RequestIcon(iconId, iconUrl ? iconUrl : "", true);
            AlterEgo::CompletionTheme::SectionHeading(displayName.c_str(), nullptr,
                count.c_str(), AlterEgo::CompletionTheme::kAvailableText,
                tex && tex->Resource ? tex->Resource : nullptr, 18.0f);

            // SectionHeading lays out the real row. This overlay makes the
            // title draggable while leaving the +/- button fully clickable.
            const auto heading=AlterEgo::CompletionTheme::BeginHeadingTarget("##moveProfession");
            const auto headerMin=heading.min,headerMax=heading.max;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag to reorder %s and all its builds. Right-click to create a build.",
                    profession.c_str());
            if (!g_LibEditMode && ImGui::BeginPopupContextItem("##profession_actions")) {
                if (AlterEgo::UI::MenuAction("New",AlterEgo::UI::MenuIcon::New,!OtherLibraryEditorOpen(),"Finish the current Library editor first.")) g_NewBuildProfessionPending = profession;
                ImGui::EndPopup();
            }
            if (!g_LibEditMode && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("AE_BUILD_GROUP",
                    profession.c_str(), profession.size()+1);
                ImGui::Text("Move %s (%d builds)", profession.c_str(), group.total);
                ImGui::EndDragDropSource();
            }
            if (!g_LibEditMode && ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    "AE_BUILD_GROUP", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                      ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload && payload->Data && payload->DataSize > 0) {
                    const std::string moved(static_cast<const char*>(payload->Data));
                    if (moved != profession) {
                        const bool after = ImGui::GetMousePos().y >=
                            (headerMin.y+headerMax.y)*0.5f;
                        const float y = after ? headerMax.y : headerMin.y;
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(headerMin.x,y), ImVec2(headerMax.x,y),
                            IM_COL32(100,180,255,240), 2.0f);
                        if (payload->IsDelivery()) {
                            pendingGroup = moved;
                            pendingGroupTarget = profession;
                            pendingGroupAfter = after;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            AlterEgo::CompletionTheme::EndHeadingTarget(heading);
            if (!expanded) { ImGui::PopID(); continue; }

            for (const int i : group.visible) {
                const auto& b = builds[i];
            ImGui::PushID(b.id.c_str());
            bool selected=(g_LibSelectedId==b.id);
            ImVec4 bProfColor = GetProfessionColor(b.profession);

            // ===== Compact row card =====
            float rowH = 30.0f;
            ImVec2 cmin = ImGui::GetCursorScreenPos();
            float availW = ImGui::GetContentRegionAvail().x;
            ImVec2 cmax(cmin.x + availW, cmin.y + rowH);

            ImGui::InvisibleButton("##buildrow", ImVec2(availW, rowH));
            if(selected&&AlterEgo::GroupBrowse::ConsumeReveal(g_LibRevealId,b.id))ImGui::SetScrollHereY(.5f);
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked()||ImGui::IsItemClicked(ImGuiMouseButton_Right);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Background
            if (selected) {
                dl->AddRectFilled(cmin, cmax, IM_COL32(80, 64, 28, 70), 3.0f);
            } else if (hovered) {
                dl->AddRectFilled(cmin, cmax, IM_COL32(255, 255, 255, 18), 3.0f);
            }
            // Selection outline
            if (selected) {
                dl->AddRect(cmin, cmax, IM_COL32(232, 196, 122, 180), 3.0f, 0, 1.0f);
            }

            // Profession-coloured 2px left accent
            ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(bProfColor.x, bProfColor.y, bProfColor.z,
                    selected ? 1.0f : (hovered ? 0.8f : 0.55f)));
            dl->AddRectFilled(ImVec2(cmin.x, cmin.y + 2),
                              ImVec2(cmin.x + 2, cmax.y - 2), accentCol);

            // Determine elite spec for this build (if any). Search all three
            // specialization slots — elite isn't always in position 2.
            uint32_t eliteSpecId = 0;
            std::shared_ptr<const AlterEgo::SpecializationInfo> eliteSpec;
            for (int s = 0; s < 3; s++) {
                uint32_t sid = b.specializations[s].spec_id;
                if (!sid) continue;
                const auto si = AlterEgo::GW2API::GetSpecInfo(sid);
                if (si && si->elite) { eliteSpecId = sid; eliteSpec = si; break; }
            }
            // If we have a spec id but no info yet, kick off a fetch so we can
            // tell whether it's elite next frame.
            {
                std::vector<uint32_t> toFetch;
                for (int s = 0; s < 3; s++) {
                    uint32_t sid = b.specializations[s].spec_id;
                    if (sid && !AlterEgo::GW2API::GetSpecInfo(sid)) toFetch.push_back(sid);
                }
                if (!toFetch.empty()) AlterEgo::GW2API::FetchSpecDetailsAsync(toFetch);
            }

            // Icon (18px), priority-loaded. Prefer the elite spec's emblem
            // when present; fall back to the profession icon.
            const float iconSz = 18.0f;
            float iconX = cmin.x + 8.0f;
            float iconY = cmin.y + (rowH - iconSz) * 0.5f;
            Texture_t* iconTex = nullptr;
            if (eliteSpecId) {
                uint32_t specIconId = 2000000u + eliteSpecId;
                iconTex = AlterEgo::IconManager::GetIcon(specIconId);
                if (!iconTex || !iconTex->Resource) {
                    AlterEgo::IconManager::RequestIcon(specIconId,
                        eliteSpec ? eliteSpec->profession_icon_big_url : "", /*priority=*/true);
                }
            }
            if (!iconTex || !iconTex->Resource) {
                uint32_t profIconId = GetProfessionBigIconId(b.profession);
                iconTex = AlterEgo::IconManager::GetIcon(profIconId);
                if (!iconTex || !iconTex->Resource) {
                    const auto prof = AlterEgo::GW2API::GetProfessionInfo(b.profession);
                    AlterEgo::IconManager::RequestIcon(profIconId,
                        prof ? prof->icon_big_url : "", /*priority=*/true);
                }
            }
            if (iconTex && iconTex->Resource) {
                dl->AddImage(iconTex->Resource,
                    ImVec2(iconX, iconY), ImVec2(iconX + iconSz, iconY + iconSz),
                    ImVec2(0,0), ImVec2(1,1),
                    selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220));
            } else {
                // Fallback: profession-coloured tile with a dim ring
                dl->AddRectFilled(ImVec2(iconX, iconY), ImVec2(iconX + iconSz, iconY + iconSz),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(bProfColor.x * 0.25f, bProfColor.y * 0.25f, bProfColor.z * 0.25f, 0.7f)), 2.0f);
                dl->AddRect(ImVec2(iconX, iconY), ImVec2(iconX + iconSz, iconY + iconSz),
                    accentCol, 2.0f, 0, 1.0f);
            }

            // Game-mode chip (right side)
            const char* mode = GameModeLabel(b.game_mode);
            const int extraModes = b.game_modes.size() > 1 ? (int)b.game_modes.size() - 1 : 0;
            const std::string modeText = extraModes ? std::string(mode) + " +" + std::to_string(extraModes) : std::string(mode);
            ImU32 chipFg, chipBg, chipBorder;
            switch (b.game_mode) {
                case AlterEgo::GameMode::PvE:
                    chipFg = IM_COL32(160, 220, 170, 255);
                    chipBg = IM_COL32(40, 70, 45, 200);
                    chipBorder = IM_COL32(120, 200, 130, 200);
                    break;
                case AlterEgo::GameMode::WvW:
                    chipFg = IM_COL32(230, 160, 160, 255);
                    chipBg = IM_COL32(70, 30, 30, 200);
                    chipBorder = IM_COL32(200, 110, 110, 200);
                    break;
                case AlterEgo::GameMode::PvP:
                    chipFg = IM_COL32(240, 220, 130, 255);
                    chipBg = IM_COL32(70, 60, 18, 200);
                    chipBorder = IM_COL32(220, 190, 90, 200);
                    break;
                case AlterEgo::GameMode::Raid:
                    chipFg = IM_COL32(232, 196, 122, 255);
                    chipBg = IM_COL32(60, 44, 18, 200);
                    chipBorder = IM_COL32(200, 160, 90, 200);
                    break;
                case AlterEgo::GameMode::Fractal:
                    chipFg = IM_COL32(140, 220, 240, 255);
                    chipBg = IM_COL32(20, 60, 75, 200);
                    chipBorder = IM_COL32(100, 180, 220, 200);
                    break;
                default:
                    chipFg = IM_COL32(190, 180, 160, 230);
                    chipBg = IM_COL32(40, 40, 44, 180);
                    chipBorder = IM_COL32(120, 110, 90, 170);
                    break;
            }
            ImVec2 mSz = ImGui::CalcTextSize(modeText.c_str());
            float chipPadX = 6.0f, chipPadY = 2.0f;
            float chipW = mSz.x + chipPadX * 2;
            float chipH = mSz.y + chipPadY * 2;
            ImVec2 chipMin(cmax.x - 8.0f - chipW, cmin.y + (rowH - chipH) * 0.5f);
            ImVec2 chipMax(chipMin.x + chipW, chipMin.y + chipH);
            dl->AddRectFilled(chipMin, chipMax, chipBg, 3.0f);
            dl->AddRect(chipMin, chipMax, chipBorder, 3.0f, 0, 1.0f);
            dl->AddText(ImVec2(chipMin.x + chipPadX, chipMin.y + chipPadY), chipFg, modeText.c_str());

            // Build name (clipped between icon and chip)
            float nameX = iconX + iconSz + 8.0f;
            float nameEndX = chipMin.x - 8.0f;
            ImU32 nameCol = selected ? IM_COL32(245, 220, 150, 255)
                          : hovered  ? IM_COL32(245, 240, 220, 255)
                                     : IM_COL32(225, 220, 205, 255);
            ImVec2 nSz = ImGui::CalcTextSize(b.name.c_str());
            float nameY = cmin.y + (rowH - nSz.y) * 0.5f;
            dl->PushClipRect(ImVec2(nameX, cmin.y), ImVec2(nameEndX, cmax.y), true);
            dl->AddText(ImVec2(nameX, nameY), nameCol, b.name.c_str());
            dl->PopClipRect();

            // Hover tooltip: full name (if clipped) + elite spec (if any)
            if (hovered) {
                bool nameOverflowed = nSz.x > (nameEndX - nameX);
                bool hasSpec = eliteSpec && !eliteSpec->name.empty();
                if (nameOverflowed || hasSpec) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(b.name.c_str());
                    if (hasSpec) {
                        ImGui::TextColored(ImVec4(bProfColor.x, bProfColor.y, bProfColor.z, 1.0f),
                            "%s %s", eliteSpec->name.c_str(), b.profession.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(bProfColor.x, bProfColor.y, bProfColor.z, 1.0f),
                            "%s", b.profession.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }

            if (clicked && !g_LibEditMode) {
                MarkNavigation();g_LibSelectedId=b.id;selectedMatches=true;
                g_LibDetailsFetched = false;
            }
            if (!g_LibEditMode && ImGui::BeginPopupContextItem("##build_actions")) {
                using namespace AlterEgo::UI;
                LoadoutCreationMenu();ImGui::Separator();
                if(MenuAction("Edit",MenuIcon::Edit))ExecuteSavedLoadoutAction(SavedLoadoutAction::Edit,b);
                if(MenuAction("Rename",MenuIcon::Rename))ExecuteSavedLoadoutAction(SavedLoadoutAction::Rename,b);
                if(MenuAction("Duplicate",MenuIcon::Duplicate))ExecuteSavedLoadoutAction(SavedLoadoutAction::Duplicate,b);
                ImGui::Separator();CopyNameMenuItem(b.name,"Copy Name",true);
                CopyNativeLinkMenuItem(b.chat_link,"Copy Build Chat Link");
                if(MenuAction("Copy Portable Loadout",MenuIcon::Copy))ExecuteSavedLoadoutAction(SavedLoadoutAction::Copy,b);
                if(BeginActionMenu("Share",MenuIcon::Share)){
                    RenderBuildShareMenuItems(b);ImGui::EndMenu();
                }
                ImGui::Separator();
                if(MenuAction("Compare",MenuIcon::Compare))ExecuteSavedLoadoutAction(SavedLoadoutAction::Compare,b);
                ImGui::Separator();
                if(MenuAction("Delete",MenuIcon::Delete))ExecuteSavedLoadoutAction(SavedLoadoutAction::Delete,b);
                ImGui::EndPopup();
            }

            // Drag an item within its own profession only. A completed drop
            // is applied after the preview pane finishes drawing.
            if (!g_LibEditMode && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("AE_BUILD_ITEM", b.id.c_str(), b.id.size() + 1);
                ImGui::Text("%s", b.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (!g_LibEditMode && ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    "AE_BUILD_ITEM", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                     ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload && payload->Data && payload->DataSize > 0) {
                    const std::string movedId(static_cast<const char*>(payload->Data));
                    const auto dragged = std::find_if(builds.begin(), builds.end(),
                        [&](const AlterEgo::SavedBuild& item) { return item.id == movedId; });
                    if (movedId != b.id && dragged != builds.end() &&
                        dragged->profession == b.profession) {
                        const bool after = ImGui::GetMousePos().y >= (cmin.y+cmax.y)*0.5f;
                        const float y = after ? cmax.y : cmin.y;
                        dl->AddLine(ImVec2(cmin.x, y), ImVec2(cmax.x, y),
                            IM_COL32(100, 180, 255, 240), 2.0f);
                        if (payload->IsDelivery()) {
                            pendingItemId = movedId;
                            pendingItemTarget = b.id;
                            pendingItemAfter = after;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }


            ImGui::PopID();
            } // within the profession
            ImGui::PopID();
        } // profession groups
        if(g_LibRevealId==revealId)g_LibRevealId.clear();
        if (!g_LibEditMode&&ImGui::BeginPopupContextWindow("##loadout_empty_actions",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            LoadoutCreationMenu();ImGui::EndPopup();
        }
    }

    ImGui::EndChild();

    if(split.Divider("##liblist_splitter",g_LibListWidth))SaveSettings();

    // Right panel: preview
    ImGui::BeginChild("LibDetail", ImVec2(0, 0), true);
    const int selectedIndex=AlterEgo::RecordSelection::ById(builds,g_LibSelectedId);
    if(selectedIndex>=0){
        const auto& build=builds[selectedIndex];

        // A change of record identity invalidates the existing detail demand.
        static std::string detailsForId;
        if(detailsForId!=build.id){detailsForId=build.id;g_LibDetailsFetched=false;}
        // Fetch details on-demand
        if (!g_LibDetailsFetched) {
            FetchDetailsForSavedBuild(build);
            // Ensure palette data for this profession
            if (!AlterEgo::GW2API::HasPaletteData(build.profession)) {
                AlterEgo::GW2API::FetchProfessionPaletteAsync(build.profession);
            }
            // Prefetch all gear-slot icons up front so the preview doesn't
            // sit on letter-badge placeholders waiting for per-slot fetches.
            {
                const char* slots[] = {
                    "Helm","Shoulders","Coat","Gloves","Leggings","Boots",
                    "WeaponA1","WeaponA2","WeaponB1","WeaponB2",
                    "Backpack","Accessory1","Accessory2","Amulet","Ring1","Ring2","Relic"
                };
                std::vector<uint32_t> gearItemIds;
                for (const char* s : slots) {
                    uint32_t id = GetGearSlotIconItemId(build, s);
                    if (id != 0) gearItemIds.push_back(id);
                }
                if (!gearItemIds.empty())
                    AlterEgo::GW2API::FetchItemDetailsAsync(gearItemIds);
            }
            g_LibDetailsFetched = true;
        }

        // Inline rename + notes editing
        if (g_LibEditBuildId != build.id) {
            g_LibEditBuildId = build.id;
            g_LibEditName = build.name;
            g_LibEditNotes = build.notes;
            // Exit edit mode when switching to a different build
            g_LibEditMode = false;
        }
        // Keep the editor draft in sync with the selected build, but never re-seed
        // while actively editing (that would clobber unsaved changes).
        if (!g_LibEditMode && g_EditDraftId != build.id) { g_EditDraft = build; g_EditDraftId = build.id; }

        // Build identity and actions never scroll away. Only the detail
        // section below has a scroll bar.
        if(!selectedMatches)ImGui::TextDisabled("Selected Loadout is outside current filters.");
        RenderSavedBuildHeader(build, /*showEditButton=*/true);
        if(g_LibEditMode&&g_SourceRefreshDraft){
            ImGui::TextWrapped("Source refresh: proposed changes only. Review this draft before Save.");
            if(ImGui::CollapsingHeader("Source refresh details"))for(const auto& note:g_SourceRefreshNotes)ImGui::BulletText("%s",note.c_str());
            if(g_SourceInfusionOffer&&!g_GearDialogOpen&&RenderActionButton("Use guide infusion totals..."))ImGui::OpenPopup("##source_infusion_offer");
            if(ImGui::BeginPopup("##source_infusion_offer")){
                ImGui::TextWrapped("Replace local infusion requirements with these guide-wide terrestrial targets? Slot placement, including aquatic infusions, is removed only on Save.");
                if(g_SourceInfusionOffer)for(const auto& inf:*g_SourceInfusionOffer){
                    const auto item=AlterEgo::GW2API::GetItemInfo(inf.itemId);
                    ImGui::BulletText("%d x %s (#%u)",inf.count,item?item->name.c_str():"Metadata pending",inf.itemId);
                }
                if(g_SourceInfusionOffer&&RenderActionButton("Adopt guide totals")){
                    if(AlterEgo::SourceRefresh::HasLocalInfusions(g_EditDraft)&&AlterEgo::GetInfusionMode(g_EditDraft)!=AlterEgo::InfusionMode::Totals)
                        g_InfusionSlotDraft=g_EditDraft;
                    AlterEgo::SourceRefresh::AdoptInfusions(g_EditDraft,*g_SourceInfusionOffer);
                    g_InfusionTotalsDraft=g_EditDraft;
                    g_SourceInfusionOffer.reset();g_SourceRefreshNotes.push_back("Guide infusion targets explicitly selected.");MarkEditDirty();ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        if (!g_LibEditMode && !build.import_warning.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.76f, 0.38f, 1.0f));
            ImGui::TextWrapped("Import needs review: %s", build.import_warning.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (!g_LibEditMode && !build.source_url.empty()) {
            ImGui::TextWrapped("Source: %s", build.source_url.c_str());
            if (RenderActionButton("Open Source##snow"))
                ShellExecuteA(NULL, "open", build.source_url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            AlterEgo::UI::SameLineIfActionFits("Refresh Source");
            if (RenderActionButton("Refresh Source##snow") && !g_BuildSourceBusy)
                FetchBuildSource(build.source_url, build.id);
            if (g_BuildSourceBusy) {
                ImGui::SameLine();
                ImGui::TextDisabled("Fetching...");
            }
            if (!g_BuildSourceRefreshError.empty())
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_BuildSourceRefreshError.c_str());
            ImGui::Separator();
        }
        // The animated edit outline sits just inside the viewport boundary.
        // Keep a persistent inner gutter so labels, icons and input controls
        // never run under that outline, including in the read-only view.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::BeginChild("##LibDetailScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        if (g_LibEditMode) {
            // Resolve draft reference data for the in-place editors.
            if (AlterEgo::GW2API::GetProfessionSpecIds(g_EditDraft.profession).empty())
                AlterEgo::GW2API::FetchProfessionInfoAsync(g_EditDraft.profession);
            std::vector<uint32_t> cur = {
                g_EditDraft.terrestrial_skills.heal,
                g_EditDraft.terrestrial_skills.utilities[0],
                g_EditDraft.terrestrial_skills.utilities[1],
                g_EditDraft.terrestrial_skills.utilities[2],
                g_EditDraft.terrestrial_skills.elite,
            };
            std::vector<uint32_t> need;
            for (uint32_t id : cur)
                if (id) need.push_back(id);
            if (!need.empty()) AlterEgo::GW2API::FetchSkillDetailsAsync(need);
        }

        ImGui::Separator();
        RenderSavedBuildPreview(build);
        ImGui::EndChild(); // ##LibDetailScroll
        // Paint the edit-state outline around the fixed viewport instead of
        // inside its scrolling contents. The 12px inner padding above reserves
        // space, so the dashes cannot overlap section labels or input text.
        if (g_LibEditMode) {
            ImVec2 a = ImGui::GetItemRectMin();
            ImVec2 b = ImGui::GetItemRectMax();
            a.x += 2.0f; a.y += 2.0f;
            b.x -= 2.0f; b.y -= 2.0f;
            ImDrawList* outline = ImGui::GetWindowDrawList();
            const ImU32 gold = IM_COL32(227, 196, 122, 255);
            DrawDottedLine(outline, ImVec2(a.x, a.y), ImVec2(b.x, a.y), gold, 2.0f, 6.0f, 4.0f);
            DrawDottedLine(outline, ImVec2(b.x, a.y), ImVec2(b.x, b.y), gold, 2.0f, 6.0f, 4.0f);
            DrawDottedLine(outline, ImVec2(b.x, b.y), ImVec2(a.x, b.y), gold, 2.0f, 6.0f, 4.0f);
            DrawDottedLine(outline, ImVec2(a.x, b.y), ImVec2(a.x, a.y), gold, 2.0f, 6.0f, 4.0f);
        }


        // Deferred: a cancelled brand-new build is removed here, after the preview
        // has finished using `build` (removing it mid-draw would dangle the ref).
        if (!g_DiscardNewBuildId.empty()) {
            if(AlterEgo::GW2API::RemoveSavedBuild(g_DiscardNewBuildId,g_DiscardNewBuildRevision)){
                g_EditDraftId.clear();g_LibSelectedId.clear();g_LibEditMode=false;
                g_EditDirty=false;g_EditIsNewBuild=false;g_GearDialogOpen=false;g_EditError.clear();
            }else g_EditError="Could not discard the provisional Loadout. Library changed; draft retained.";
            g_DiscardNewBuildId.clear();
        }
    } else {
        g_LibSelectedId.clear();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a Loadout from the list.");
    }
    ImGui::EndChild();

    if(!g_LibNameTarget.id.empty()){
        using API=AlterEgo::GW2API;
        const bool valid=g_LibNameTarget.Current(API::GetBuildLibraryRevision())&&
            AlterEgo::RecordSelection::ById(API::GetSavedBuilds(),g_LibNameTarget.id)>=0;
        AlterEgo::UI::RenderNameAction("Name Loadout##loadout_name_action",g_LibNameAction,valid,
            g_LibNameAction.duplicate?"Create a separate Loadout with the same component, Equipment Set and Fashion references.":"Only the name will change.",
            [&](const std::string& name,bool duplicate){
                std::string savedId;
                const bool ok=duplicate?API::DuplicateSavedLoadout(g_LibNameTarget.id,name,g_LibNameTarget.revision,&savedId):
                    API::RenameSavedLoadout(g_LibNameTarget.id,name,g_LibNameTarget.revision);
                if(ok){
                    if(duplicate){g_LibSelectedId=savedId;g_LibRevealId=savedId;MarkNavigation();}
                    g_LibDetailsFetched=false;g_EditDraftId.clear();g_LibNameTarget={};
                }
                return ok;
            });
        if(!ImGui::IsPopupOpen("Name Loadout##loadout_name_action")&&!g_LibNameAction.pending)g_LibNameTarget={};
    }
    if(g_LibDeletePending){g_LibDeletePending=false;ImGui::OpenPopup("Delete Loadout?##ctx");}
    if(ImGui::BeginPopupModal("Delete Loadout?##ctx",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove)){
        const auto target=g_LibDeleteTarget;
        const bool valid=target.Current(AlterEgo::GW2API::GetBuildLibraryRevision())&&AlterEgo::RecordSelection::ById(AlterEgo::GW2API::GetSavedBuilds(),target.id)>=0;
        ImGui::TextWrapped("Delete \"%s\"?",target.name.c_str());
        if(!valid)ImGui::TextWrapped("Library changed. Cancel and review again.");
        const auto action=AlterEgo::UI::ConfirmCancel("loadout_delete",valid,"Delete","Library changed; review again.",g_LibDeleteError.c_str());
        if(action==AlterEgo::UI::EditAction::Save){
            if(AlterEgo::GW2API::RemoveSavedBuild(target.id,target.revision)){
                if(g_LibSelectedId==target.id)g_LibSelectedId.clear();
                g_LibDeleteTarget={};ImGui::CloseCurrentPopup();
            }else g_LibDeleteError="Could not delete the selected Loadout. Original data retained.";
        }else if(action==AlterEgo::UI::EditAction::Cancel||AlterEgo::UI::EditorEscape()){
            g_LibDeleteTarget={};ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // Post-render insertion avoids invalidating the list's references.
    if (!g_NewBuildProfessionPending.empty()&&!OtherLibraryEditorOpen()&&!g_LibEditMode) {
        const std::string profession = g_NewBuildProfessionPending;
        g_NewBuildProfessionPending.clear();
        auto blank = AlterEgo::GW2API::CreateBlankBuild(profession, AlterEgo::GameMode::PvE);
        const int idx = AlterEgo::GW2API::AddSavedBuild(std::move(blank));
        if (idx >= 0) {
            const auto& updated = AlterEgo::GW2API::GetSavedBuilds();
            SelectLoadoutAt(idx);
            g_LibEditBuildId = updated[idx].id;
            EnterBuildEditMode(updated[idx]);
            g_EditIsNewBuild = true;
            g_LibDetailsFetched = false;
            AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
            AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
        }
    }

    if (!pendingGroup.empty() || !pendingItemId.empty()) {
        const bool moved = !pendingGroup.empty()
            ? AlterEgo::GW2API::ReorderSavedBuildGroup(
                pendingGroup, pendingGroupTarget, pendingGroupAfter)
            : AlterEgo::GW2API::ReorderSavedBuildInGroup(
                pendingItemId, pendingItemTarget, pendingItemAfter);
        if(moved)SaveSession();
    }
    RenderComponentUIEditor();
    ProcessComponentUICommand();
}

// Share relay progress/result popups must render regardless of the selected
// main tab, because Share is now also available from Characters.
static void RenderShareRelayPopups() {
    if constexpr (!AlterEgo::BuildRelay::Enabled) return;
    // Relay code result popup (triggered by SendBuildToRelay)
    // The worker publishes strings before clearing the atomic busy flag.
    // Do not read or mutate them while a worker owns the result.
    const bool sending = g_RelaySending.load();
    if (sending) {
        ImGui::OpenPopup("Sending to Mobile...##relay");
    }
    if (!sending && !g_RelayResultCode.empty()) {
        ImGui::OpenPopup("Build Sent##relay_ok");
    }
    if (!sending && !g_RelayResultError.empty()) {
        ImGui::OpenPopup("Send Failed##relay_err");
    }

    // "Sending..." spinner popup
    if (ImGui::BeginPopupModal("Sending to Mobile...##relay", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Uploading build to relay...");
        if (!sending) ImGui::CloseCurrentPopup(); // closes when done
        ImGui::EndPopup();
    }

    if (sending) return;
    // Success popup — show the code
    if (ImGui::BeginPopupModal("Build Sent##relay_ok", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Enter this code in the Alter Ego mobile app,\nor share it with another player.");
        ImGui::Spacing();

        // Large centered code display
        float codeW = ImGui::CalcTextSize("W").x * 8.0f; // generous width for 4 chars with spacing
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > codeW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - codeW) * 0.5f);
        ImGui::PushFont(nullptr); // default font, scaled below
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("  %s", g_RelayResultCode.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Code expires in 15 minutes. Single use.");

        ImGui::Spacing();
        if (RenderGoldButton("Copy Code", ImVec2(100, 0))) {
            CopyToClipboard(g_RelayResultCode);
            if (APIDefs) APIDefs->GUI_SendAlert("Relay code copied!");
        }
        ImGui::SameLine();
        if (RenderChipButton("Done", false)) {
            g_RelayResultCode.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Error popup
    if (ImGui::BeginPopupModal("Send Failed##relay_err", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_RelayResultError.c_str());
        ImGui::Spacing();
        if (RenderChipButton("OK", false)) {
            g_RelayResultError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- GW2-Themed UI Style ---

static ImGuiStyle g_GW2Style;
static std::vector<ImGuiStyle> g_StyleStack;
static bool g_GW2StyleBuilt = false;

static void PushGW2Theme() {
    g_StyleStack.push_back(ImGui::GetStyle());
    ImGui::GetStyle() = g_GW2Style;
}

static void PopGW2Theme() {
    if (!g_StyleStack.empty()) {
        ImGui::GetStyle() = g_StyleStack.back();
        g_StyleStack.pop_back();
    }
}

static void BuildGW2Theme() {
    // Start from the current (Nexus default) style so we inherit base values
    g_GW2Style = ImGui::GetStyle();
    ImGuiStyle& s = g_GW2Style;

    // Rounding
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 5.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;

    // Spacing & padding
    s.WindowPadding     = ImVec2(10, 10);
    // Tabs, buttons and combos share a font-responsive baseline.
    // Toolbars use RenderActionButton instead of SmallButton (which drops Y padding).
    s.FramePadding      = ImVec2(8, 7);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 8.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.5f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    // Colors — dark slate base with warm gold accents
    ImVec4* c = s.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.07f, 0.09f, 0.80f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.085f, 0.075f, 0.06f, 0.97f);

    // Borders
    c[ImGuiCol_Border]               = ImVec4(0.28f, 0.25f, 0.18f, 0.50f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames (input boxes, combos)
    c[ImGuiCol_FrameBg]              = ImVec4(0.14f, 0.13f, 0.11f, 0.80f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.20f, 0.14f, 0.80f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.25f, 0.16f, 0.90f);

    // Title bar
    c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.09f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.14f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.07f, 0.05f, 0.75f);

    // Menu bar
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.11f, 0.09f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.06f, 0.07f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.27f, 0.18f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.22f, 0.90f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.44f, 0.26f, 1.00f);

    // Checkmark, slider
    c[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.75f, 0.25f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.85f, 0.70f, 0.25f, 1.00f);

    // Buttons — warm gold
    c[ImGuiCol_Button]               = ImVec4(0.22f, 0.20f, 0.12f, 0.80f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.45f, 0.38f, 0.16f, 1.00f);

    // Headers (selectables, collapsing headers)
    c[ImGuiCol_Header]               = ImVec4(0.18f, 0.16f, 0.10f, 0.70f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.24f, 0.12f, 0.80f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);

    // Separator
    c[ImGuiCol_Separator]            = ImVec4(0.28f, 0.25f, 0.18f, 0.40f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.50f, 0.42f, 0.20f, 0.70f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.65f, 0.55f, 0.25f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.27f, 0.18f, 0.30f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.50f, 0.44f, 0.26f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.65f, 0.55f, 0.25f, 0.90f);

    // Tabs — gold accent for active
    c[ImGuiCol_Tab]                  = ImVec4(0.14f, 0.13f, 0.10f, 0.86f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_TabActive]            = ImVec4(0.28f, 0.24f, 0.10f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.09f, 0.07f, 0.97f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);

    // Text
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.87f, 0.78f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.47f, 0.40f, 1.00f);

    // Modal dim background
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

    // Nav highlight
    c[ImGuiCol_NavHighlight]         = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);

    // Table
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.13f, 0.10f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.28f, 0.25f, 0.18f, 0.60f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.22f, 0.20f, 0.15f, 0.40f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.08f, 0.30f);

    // Plot (progress bars)
    c[ImGuiCol_PlotHistogram]        = ImVec4(0.65f, 0.55f, 0.15f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.68f, 0.20f, 1.00f);
}

// --- Addon Lifecycle ---

void AddonLoad(AddonAPI_t* aApi) {
    const auto maintenanceRoot=AlterEgo::GW2API::GetDataDirectory();
    g_StorageMaintenanceResult=AlterEgo::Storage::RunStartupMaintenance(maintenanceRoot);
    g_StorageMaintenancePending=AlterEgo::Storage::MaintenancePending(maintenanceRoot);
    g_StorageMaintenanceMessage.clear();
    AlterEgo::Background::Workers().Open();
    g_AchActiveEventNextCheck=0;
    g_AchActiveEventFetching=false;
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc,
                                 (void(*)(void*, void*))APIDefs->ImguiFree);

    BuildGW2Theme();

    // Initialize subsystems
    AlterEgo::GW2API::Initialize(APIDefs);
    AlterEgo::FontManager::Initialize(APIDefs);
    AlterEgo::IconManager::Initialize(APIDefs);

    // Load cached character data and build library from disk
    AlterEgo::GW2API::LoadCharacterData();
    AlterEgo::GW2API::LoadBuildLibrary();
    g_FashionLibrary.Load(AlterEgo::GW2API::GetDataDirectory());
    AlterEgo::GW2API::LoadItemNameCache();
    AlterEgo::GW2API::LoadReferenceCaches();
    AlterEgo::PublicCatalog::Load(AlterEgo::GW2API::GetDataDirectory());
    AlterEgo::WikiEvents::Load(AlterEgo::GW2API::GetDataDirectory());
    if(!AlterEgo::PublicCatalog::Get()->upgrades)SeedItemNameCache();
    AlterEgo::GW2API::RepairPendingLibraryNames();
    LoadCharacterActivity();
    // Replay any interrupted identity/cache adoption before exposing character data.
    AlterEgo::NativeAccounts::Initialize(AlterEgo::GW2API::GetDataDirectory());
    LoadLegacyLoginHistory();
    MigrateLegacyLoginHistory();
    ImportPortableLoginHistory();
    SyncLoginHistory();
    LoadCharSortConfig();
    RebuildCharDisplayOrder();

    // Load icon textures from embedded PNG data
    APIDefs->Textures_LoadFromMemory(TEX_ICON, (void*)ICON_NORMAL, ICON_NORMAL_size, nullptr);
    APIDefs->Textures_LoadFromMemory(TEX_ICON_HOVER, (void*)ICON_HOVER, ICON_HOVER_size, nullptr);

    // Load settings, session, and cached data
    LoadSettings();
    ImportPortableLoadoutPreferences();
    AlterEgo::AutoRefreshPolicy::SetEnabled(g_AutoRefreshEnabled);
    LoadSession();
    // Restore the selected account's achievement progress during addon startup.
    // If native API has not revealed the single-account name yet, the global render
    // poll loads it as soon as that identity becomes available.
    const std::string startupAchAccount = GetAchievementCacheAccount();
    if (!startupAchAccount.empty()) LoadAchProgress(startupAchAccount);
    SyncCompletionAccountCaches();
    LoadAchTrackerState();
    LoadAchGroupCache();
    LoadAchDefCache();
    LoadAchNameIndex();
    AlterEgo::AchievementDefinitions::Configure(AlterEgo::GW2API::GetDataDirectory(),g_AchDefs,PublishAchDefinition);

    // Dye data loads independently. Profession metadata is now fetched lazily
    // when a relevant view needs it: avoid a burst of nine background HTTP
    // workers immediately during addon startup.
    AlterEgo::GW2API::FetchAllItemStatsAsync();
    AlterEgo::GW2API::FetchDyeColorsAsync();

    // Ping native API to check availability

    // Achievement definitions and progress are restored from disk above.
    // Do not fetch pinned or selected-category definitions during startup;
    // the Achievements tab and pinned popout load them when actually used.

    // Initialize Skinventory subsystems
    {
        std::string dataDir = AlterEgo::GW2API::GetDataDirectory();
        std::string skinDataDir = dataDir + "\\Skinventory";
        try { std::filesystem::create_directories(skinDataDir); } catch (...) {}
        std::string wikiCacheDir = AlterEgo::Storage::ResolveCache(dataDir, "Skinventory/wiki_cache", "wiki_images");

        Skinventory::SkinCache::SetDataDirectory(skinDataDir);
        Skinventory::Commerce::SetDataDirectory(skinDataDir);
        Skinventory::OwnedSkins::SetDataDirectory(skinDataDir);
        Skinventory::OwnedSkins::Initialize(APIDefs);
        Skinventory::WikiImage::Initialize(APIDefs, wikiCacheDir);

        if (Skinventory::SkinCache::LoadFromDisk()) {
            // Refreshing cached API metadata obeys the global option;
            // initial catalog creation is required to use Skinventory.
            if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::PublicCatalog))
                Skinventory::SkinCache::UpdateCacheAsync();
        } else {
            Skinventory::SkinCache::FetchAllSkinsAsync();
        }

        if (!Skinventory::Commerce::LoadItemMap()) {
            const auto catalog=AlterEgo::PublicCatalog::Get();
            if(!catalog->upgradeAttempt || AlterEgo::PublicCatalog::Now()-catalog->upgradeAttempt>=AlterEgo::PublicCatalog::Retry)
                Skinventory::Commerce::BuildItemMapAsync();
        }
        Skinventory::Commerce::LoadPriceCache();
        Skinventory::Commerce::RefreshUpgradeCatalogAsync(false);
        AlterEgo::PublicCatalog::RefreshVault(dataDir,false);
        AlterEgo::WikiEvents::Refresh(dataDir);

        // Ping native API so Skinventory's OwnedSkins receives the pong
        // (must be after Initialize which subscribes to EV_HOARD_PONG)

        g_SkinInitialized = true;
    }

    // Startup state, settings and subsystem caches are ready before exposing
    // callback-driven refresh scheduling or rendering to the host.
    AlterEgo::Callbacks::Runtime().Open();
    // Subscribe to MumbleLink identity for current character indicator
    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", OnMumbleIdentityUpdated);

    // Subscribe to Events: Chat for build link detection
    APIDefs->Events_Subscribe(EV_CHAT_MESSAGE, OnEvChatMessage);

    // Subscribe to Events: Alerts for real-time skin unlock + achievement completion
    APIDefs->Events_Subscribe(EV_ALERT_UNLOCKED_SKIN, OnEvAlertSkinUnlocked);
    APIDefs->Events_Subscribe(EV_ALERT_ACHIEVEMENT_COMPLETED, OnEvAlertAchievementCompleted);

    // Pie UI inter-addon bridge — subscribe before announcing so we catch Pie's
    // synchronous reply to our ping.
    APIDefs->Events_Subscribe(EV_ALTER_EGO_PING, OnAlterEgoPing);
    APIDefs->Events_Subscribe(EV_ALTER_EGO_IMPORT_BUILD, OnPieImportBuild);
    APIDefs->Events_Subscribe(EV_PIE_UI_READY, OnPieUIReady);


    // Pie UI bridge: announce AE (so an already-loaded Pie sees us) and probe for
    // an already-loaded Pie (which replies EV_PIE_UI_READY).
    APIDefs->Events_Raise(EV_ALTER_EGO_READY, nullptr);
    APIDefs->Events_Raise(EV_PIE_UI_PING, nullptr);

    // Register keybind
    APIDefs->InputBinds_RegisterWithString("KB_ALTER_EGO_TOGGLE", ProcessKeybind, "CTRL+SHIFT+E");
    APIDefs->InputBinds_RegisterWithString("KB_ALTER_EGO_TRACKER", ProcessKeybind, "(null)");

    // Register quick access shortcut
    if (g_ShowQAIcon) {
        APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON_HOVER, "KB_ALTER_EGO_TOGGLE", "Alter Ego");
    }

    // Register close-on-escape
    APIDefs->GUI_RegisterCloseOnEscape("Alter Ego", &g_WindowVisible);

    // Register render functions
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->WndProc_Register(NavigationWndProc);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    APIDefs->Log(LOGL_INFO, "AlterEgo", "Addon loaded successfully");
}

void AddonUnload() {
    const auto shutdownAt=AlterEgo::CharacterActivity::Now();
    AlterEgo::Callbacks::Runtime().Close();
    AlterEgo::Background::Workers().RequestStop();
    AlterEgo::IconManager::RequestStop();
    Skinventory::WikiImage::RequestStop();
    APIDefs->GUI_DeregisterCloseOnEscape("Alter Ego");
    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->WndProc_Deregister(NavigationWndProc);
    APIDefs->InputBinds_Deregister("KB_ALTER_EGO_TOGGLE");
    APIDefs->InputBinds_Deregister("KB_ALTER_EGO_TRACKER");
    // Unsubscribe MumbleLink identity
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", OnMumbleIdentityUpdated);

    // Unsubscribe chat events
    APIDefs->Events_Unsubscribe(EV_CHAT_MESSAGE, OnEvChatMessage);

    // Unsubscribe Events: Alerts
    APIDefs->Events_Unsubscribe(EV_ALERT_UNLOCKED_SKIN, OnEvAlertSkinUnlocked);
    APIDefs->Events_Unsubscribe(EV_ALERT_ACHIEVEMENT_COMPLETED, OnEvAlertAchievementCompleted);

    // Pie UI inter-addon bridge
    APIDefs->Events_Unsubscribe(EV_ALTER_EGO_PING, OnAlterEgoPing);
    APIDefs->Events_Unsubscribe(EV_ALTER_EGO_IMPORT_BUILD, OnPieImportBuild);
    APIDefs->Events_Unsubscribe(EV_PIE_UI_READY, OnPieUIReady);


    // No new callback body can enter; drain admitted work before teardown.
    // Do not hold a subsystem lock while waiting (events may reenter synchronously).
    AlterEgo::Callbacks::Runtime().Drain();
    // Preserve a final tracker toggle in settings; do not import or notify during unload.
    if(g_UiToggles.Take()&2)g_AchPopoutVisible=!g_AchPopoutVisible;
    (void)g_ChatBuildInbox.Take();(void)g_PieBuildInbox.Take();
    // End observation at shutdown, not after a slow HTTP worker joins.
    {std::lock_guard<std::mutex> lock(AlterEgo::CharacterActivity::Mutex());
        AlterEgo::CharacterActivity::State().Finish(shutdownAt);}

    // All one-shot jobs must finish before their state/API is destroyed.
    AlterEgo::Background::Workers().JoinAll();
    // Dedicated artwork workers also finish before shared cache/state reset.
    Skinventory::WikiImage::Shutdown();
    AlterEgo::IconManager::Shutdown();

    // Persist achievement progress before shutting down the data subsystem,
    // including updates received while the Achievements tab was not visible.
    if (g_AchProgressDirty && !g_AchCachedAccount.empty())
        SaveAchProgress(g_AchCachedAccount);

    SaveCharacterActivity(true);
    AlterEgo::NativeProgress::Reset();
    AlterEgo::RefreshEngine::ResetMetadata();
    ResetClearsPublicEpochCache();

    // Shutdown Skinventory subsystems
    Skinventory::OwnedSkins::Shutdown();
    AlterEgo::NativeAccounts::Shutdown();
    AlterEgo::GW2API::Shutdown();

    SaveSession();
    SaveSettings();
    SaveAchTrackerState();
    AlterEgo::GW2API::SaveItemNameCache();
    AlterEgo::FontManager::Shutdown();
    AlterEgo::NativeLibrary::RuntimeStore().Close();
    APIDefs = nullptr;
}

static void LoadCharacterActivity(){
    namespace A=AlterEgo::CharacterActivity;
    std::lock_guard<std::mutex> lock(A::Mutex());A::State()=A::Tracker{};
    const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"character_activity.json");
    g_ActivityLoadValid=true;
    try{
        std::error_code ec;
        const bool exists=std::filesystem::exists(std::filesystem::u8path(path),ec);
        if(ec)throw std::runtime_error("Cannot inspect login history");
        if(exists){
            const auto bytes=AlterEgo::Storage::ReadMigrationBytes(std::filesystem::u8path(path));
            A::State().Load(AlterEgo::Untrusted::Parse(bytes,AlterEgo::Untrusted::Library),true);
        }
    }catch(...){g_ActivityLoadValid=false;}
    g_LoginProjectionRevision=~uint64_t(0);
}
static bool SaveCharacterActivity(bool force){
    namespace A=AlterEgo::CharacterActivity;
    // Preserve an unreadable ledger for explicit recovery.
    if(!g_ActivityLoadValid)return false;
    static std::mutex saveMutex;
    std::lock_guard<std::mutex> saving(saveMutex);
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(!force&&now-last<std::chrono::seconds(30))return false;
    last=now;
    nlohmann::json data;uint64_t revision=0;
    {std::lock_guard<std::mutex> lock(A::Mutex());if(!A::State().DirtyState())return true;
        data=A::State().Save();revision=A::State().Revision();}
    const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"character_activity.json");
    if(WriteFileAtomic(path,data.dump())){
        std::lock_guard<std::mutex> lock(A::Mutex());A::State().Saved(revision);return true;
    }
    return false;
}
static void LoadLegacyLoginHistory(){
    g_LegacyLoginHistory.clear();g_LegacyLoginArchivePending=false;
    const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"login_times.json");
    try{
        std::ifstream f(path);if(!f)return;
        const auto j=nlohmann::json::parse(f);if(!j.is_object())return;
        bool valid=true;
        for(auto it=j.begin();it!=j.end();++it){
            if(!it.key().empty()&&it.value().is_number_integer()&&it.value().get<int64_t>()>0)
                g_LegacyLoginHistory.emplace(it.key(),it.value().get<int64_t>());
            else valid=false;
        }
        g_LegacyLoginArchivePending=valid&&g_ActivityLoadValid;
    }catch(...){g_LegacyLoginArchivePending=false;}
}
static void MigrateLegacyLoginHistory(){
    namespace A=AlterEgo::CharacterActivity;
    if(!g_ActivityLoadValid)return;
    for(auto it=g_LegacyLoginHistory.begin();it!=g_LegacyLoginHistory.end();){
        // Name-only history has no verified owner. Keep the original file
        // available for portable backup and retain the unresolved timestamp.
        {std::lock_guard<std::mutex> lock(A::Mutex());
            A::State().RetainUnresolvedLogin({{},it->first},it->second);}
        g_LegacyLoginArchivePending=false;
        it=g_LegacyLoginHistory.erase(it);
    }
    if(g_LegacyLoginArchivePending&&g_LegacyLoginHistory.empty()&&SaveCharacterActivity(true)){
        const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"login_times.json");
        std::error_code ec;
        const auto archive=path+".migrated";
        if(!std::filesystem::exists(archive,ec)&&!ec)std::filesystem::rename(path,archive,ec);
        // If archival fails, leave the original intact; the import is idempotent.
        g_LegacyLoginArchivePending=false;
    }
}
static void ImportPortableLoginHistory(){
    namespace A=AlterEgo::CharacterActivity;
    if(!g_ActivityLoadValid)return;
    const auto path=AlterEgo::Storage::Resolve(AlterEgo::GW2API::GetDataDirectory(),"character_logins.json");
    try{
        A::PortableLogins logins;
        {std::ifstream f(path);if(!f)return;logins=A::DecodePortableLogins(nlohmann::json::parse(f));}
        {std::lock_guard<std::mutex> lock(A::Mutex());A::State().StagePortableHistory(logins);}
        AlterEgo::NativeAccounts::AdoptPendingLoginHistory();
        if(SaveCharacterActivity(true)){
            std::error_code ec;std::filesystem::remove(std::filesystem::u8path(path),ec);
        } // on a failed write, leave the staged file for the next startup
    }catch(...){/* retain invalid input; do not overwrite activity */}
}
static void SyncLoginHistory(){
    namespace A=AlterEgo::CharacterActivity;
    std::lock_guard<std::mutex> lock(A::Mutex());
    const auto revision=A::State().LoginRevision();
    if(revision==g_LoginProjectionRevision)return;
    g_LoginHistory=A::State().LoginTimes();g_LoginProjectionRevision=revision;
    g_CharLastLoginSortDirty=true;
}

// Sanitize an account name to a filename-safe slug (account names contain '.').
static std::string SanitizeForFilename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_') out += c;
        else out += '_';
    }
    return out;
}

// Persist g_AchProgress for the given account to disk. Per-account so that
// switching accounts doesn't clobber the previous account's cache.
static void SaveAchProgress(const std::string& account) {
    if (account.empty()) return;
    // Serialize capture and write so two saves of the same fetch timestamp
    // cannot publish their generations in reverse order.
    static std::mutex saveMutex;
    std::lock_guard<std::mutex> saving(saveMutex);
    AlterEgo::Cache::AchievementSnapshot snapshot;
    uint64_t generation = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        if (g_AchCachedAccount != account) return;
        for (const auto& [id, p] : g_AchProgress) snapshot.entries.emplace(id, p);
        snapshot.fetchedAt = g_AchProgressCacheSavedAt;
        generation = g_AchProgressGen;
    }
    bool saved = false;
    try {
        saved = AlterEgo::Cache::WriteAchievements(
            AlterEgo::GW2API::GetDataDirectory(), account, snapshot);
    } catch (...) {
        saved = false;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        g_AchLastSave = std::chrono::steady_clock::now();
        if (saved && g_AchCachedAccount == account && g_AchProgressGen == generation)
            g_AchProgressDirty = false;
    }
    if (!saved && APIDefs)
        APIDefs->Log(LOGL_WARNING, "AlterEgo",
                     "Could not save achievement progress cache (will retry)");
}

// Load achievement progress for an account from disk. Bumps the progress gen
// so dependent UI caches (rail counts, popout) refresh.
static void LoadAchProgress(const std::string& account) {
    if (account.empty()) return;
    const auto root = AlterEgo::GW2API::GetDataDirectory();
    const auto path = root + "/cache/ach_progress_" + SanitizeForFilename(account) + ".json";
    std::unordered_map<uint32_t, AchProgress> restored;
    int64_t fetchedAt = 0;
    bool valid = false;
    try {
        auto snapshot = AlterEgo::Cache::ReadAchievements(root, account);
        if (!snapshot) {
            // Legacy cache is disposable. Import only when the embedded
            // identity proves this slug belongs to the requested account.
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                const auto j = nlohmann::json::parse(file);
                file.close();
                if (!j.is_object() || !j.contains("entries") || !j["entries"].is_object() ||
                    !j.contains("account") || j["account"].get<std::string>() != account)
                    throw std::runtime_error("Unverified legacy achievement cache identity");
                AlterEgo::Cache::AchievementSnapshot legacy;
                legacy.fetchedAt = j.value("saved_at", (int64_t)0);
                for (auto it = j["entries"].begin(); it != j["entries"].end(); ++it) {
                    AchProgress p;
                    size_t consumed = 0;
                    const auto id = std::stoull(it.key(), &consumed);
                    if (consumed != it.key().size() || id > UINT32_MAX)
                        throw std::runtime_error("Invalid achievement id");
                    p.id = static_cast<uint32_t>(id);
                    const auto& e = it.value();
                    p.current = e.value("c", 0);
                    p.max = e.value("m", 0);
                    p.done = e.value("d", false);
                    p.repeated = e.value("r", 0);
                    p.unlocked = e.value("u", true);
                    if (e.contains("b") && e["b"].is_array())
                        for (const auto& bit : e["b"])
                            p.completed_bits.insert(bit.get<uint32_t>());
                    if (!legacy.entries.emplace(p.id, std::move(p)).second)
                        throw std::runtime_error("Duplicate achievement id");
                }
                // Re-read the authoritative transaction before removing the
                // old cache, including when a newer snapshot won the race.
                AlterEgo::Cache::WriteAchievements(root, account, legacy);
                snapshot = AlterEgo::Cache::ReadAchievements(root, account);
                if (snapshot) {
                    std::error_code ec;
                    std::filesystem::remove(std::filesystem::u8path(path), ec);
                }
            }
        }
        if (snapshot) {
            fetchedAt = snapshot->fetchedAt;
            restored.reserve(snapshot->entries.size());
            for (auto& [id,p] : snapshot->entries) restored.emplace(id,std::move(p));
            valid = true;
        }
    } catch (...) {
        if (APIDefs)
            APIDefs->Log(LOGL_WARNING, "AlterEgo",
                         "Achievement progress cache is unreadable; retaining data for recovery");
    }

    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        g_AchProgress = std::move(restored);
        g_AchOptimisticTime.clear();
        g_AchCachedAccount = account;
        g_AchProgressCacheSavedAt = valid ? fetchedAt : 0;
        g_AchProgressLoadedSnapshotAt = valid ? fetchedAt : 0;
        g_AchNeedsInitialFullLoad = !valid;
        g_AchManualInitialScan = false;
        g_AchCategoryRefreshAt.clear();
        g_AchProgressDirty = false;
        ++g_AchProgressGen;  // invalidate popout and category count caches
    }
}

void OnMumbleIdentityUpdated(void* eventArgs) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (!eventArgs) return;
    const MumbleIdentity* id = (const MumbleIdentity*)eventArgs;
    char buf[20] = {};
    memcpy(buf, id->Name, 19);
    std::string newName(buf);
    // Validate: GW2 character names contain only letters, spaces, hyphens, accented chars
    for (unsigned char c : newName) {
        if (c == ' ' || c == '-' || isalpha(c) || c >= 0x80) continue;
        return; // invalid — likely garbage data from uninitialized MumbleLink
    }
    if(newName.empty())return;
    // The event publishes verified account context immediately for alert
    // callbacks. Gameplay sampling is the sole owner of activity and UI state.
    AlterEgo::GW2API::SetCurrentAccountFromCharacter(newName);
}

void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    if (aIsRelease || !aIdentifier) return;
    if (strcmp(aIdentifier, "KB_ALTER_EGO_TOGGLE") == 0) g_UiToggles.Toggle(1);
    else if (strcmp(aIdentifier, "KB_ALTER_EGO_TRACKER") == 0) g_UiToggles.Toggle(2);
}


// =========================================================================
// Skinventory UI
// =========================================================================

ImVec4 GetSkinRarityColor(const std::string& rarity) {
    if (rarity == "Junk")       return ImVec4(0.67f, 0.67f, 0.67f, 1.0f);
    if (rarity == "Basic")      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    if (rarity == "Fine")       return ImVec4(0.38f, 0.58f, 1.0f, 1.0f);
    if (rarity == "Masterwork") return ImVec4(0.12f, 0.72f, 0.12f, 1.0f);
    if (rarity == "Rare")       return ImVec4(0.98f, 0.82f, 0.0f, 1.0f);
    if (rarity == "Exotic")     return ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
    if (rarity == "Ascended")   return ImVec4(0.98f, 0.35f, 0.56f, 1.0f);
    if (rarity == "Legendary")  return ImVec4(0.63f, 0.21f, 0.93f, 1.0f);
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

static const ImVec4 COIN_GOLD   = ImVec4(0.85f, 0.75f, 0.10f, 1.0f);
static const ImVec4 COIN_SILVER = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
static const ImVec4 COIN_COPPER = ImVec4(0.72f, 0.45f, 0.20f, 1.0f);

static void DrawCoinIcon(ImVec4 color, float radius) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float y_center = pos.y + ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(pos.x + radius, y_center), radius,
        ImGui::GetColorU32(color));
    ImGui::GetWindowDrawList()->AddCircle(
        ImVec2(pos.x + radius, y_center), radius,
        ImGui::GetColorU32(ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 1.0f)));
    ImGui::Dummy(ImVec2(radius * 2.0f + 1.0f, ImGui::GetTextLineHeight()));
}

// Match the native coin renderer's text, icon and spacing widths.
float CoinAmountWidth(int64_t copper) {
    if(copper<=0)return ImGui::CalcTextSize("0").x+13.f;
    const auto gold=copper/10000,silver=(copper%10000)/100,cop=copper%100;
    float width=0.f;
    const auto add=[&](int64_t value){
        if(width>0.f)width+=2.f;
        width+=ImGui::CalcTextSize(std::to_string(value).c_str()).x+12.f;
    };
    if(gold>0)add(gold);
    if(silver>0||gold>0)add(silver);
    add(cop);
    return width;
}
void RenderCoins(int64_t copper) {
    if (copper <= 0) {
        ImGui::TextColored(COIN_COPPER, "0");
        ImGui::SameLine(0, 2);
        DrawCoinIcon(COIN_COPPER, 5.0f);
        return;
    }
    const auto gold = copper / 10000;
    const int silver = static_cast<int>((copper % 10000) / 100);
    const int cop = static_cast<int>(copper % 100);

    bool needSame = false;
    if (gold > 0) {
        ImGui::TextColored(COIN_GOLD, "%lld", static_cast<long long>(gold));
        ImGui::SameLine(0, 1);
        DrawCoinIcon(COIN_GOLD, 5.0f);
        needSame = true;
    }
    if (silver > 0 || gold > 0) {
        if (needSame) ImGui::SameLine(0, 2);
        ImGui::TextColored(COIN_SILVER, "%d", silver);
        ImGui::SameLine(0, 1);
        DrawCoinIcon(COIN_SILVER, 5.0f);
        needSame = true;
    }
    if (needSame) ImGui::SameLine(0, 2);
    ImGui::TextColored(COIN_COPPER, "%d", cop);
    ImGui::SameLine(0, 1);
    DrawCoinIcon(COIN_COPPER, 5.0f);
}

// One cached catalog/ownership projection serves Browser and Shopping facets.
struct SkinBrowserData {
    uint64_t catalog=uint64_t(-1), ownership=uint64_t(-1);
    bool hasOwnership=false;
    std::vector<Skinventory::SkinInfo> rows;
    std::unordered_set<uint32_t> owned;
    Skinventory::SkinFacets facets;
};
static const SkinBrowserData& GetSkinBrowserData() {
    static SkinBrowserData data;
    const auto catalog=Skinventory::SkinCache::GetGeneration();
    const auto owner=Skinventory::OwnedSkins::GetGeneration();
    const bool has=Skinventory::OwnedSkins::HasData();
    if(data.catalog!=catalog) {
        data.rows=Skinventory::SkinCache::GetCatalogSnapshot();
        std::sort(data.rows.begin(),data.rows.end(),[](const auto& a,const auto& b){
            return a.name==b.name?a.id<b.id:a.name<b.name;
        });
    }
    if(data.ownership!=owner||data.hasOwnership!=has)
        data.owned=Skinventory::OwnedSkins::GetOwnedSnapshot();
    if(data.catalog!=catalog||data.ownership!=owner||data.hasOwnership!=has) {
        data.facets=Skinventory::CountFacets(data.rows,data.owned,has);
        data.catalog=catalog;data.ownership=owner;data.hasOwnership=has;
    }
    return data;
}
static AlterEgo::UI::FilterChips SkinFilterChips(Skinventory::SkinFilters& filters,
    std::function<void()> changed) {
    AlterEgo::UI::FilterChips chips;
    auto add=[&](const char* group,Skinventory::Choices& values) {
        for(const auto& value:values) {
            auto* set=&values;
            std::string label=value=="Weapon"?"Weapons":value;
            chips.push_back({std::string(group)+":"+value,label,[set,value,&filters,changed] {
                set->erase(value);filters.Prune();changed();
            }});
        }
    };
    add("category",filters.category);add("weight",filters.weight);add("slot",filters.slot);
    add("weapon",filters.weapon);add("rarity",filters.rarity);
    return chips;
}
static void DrawSkinFilters(Skinventory::SkinFilters& filters,const SkinBrowserData& data,
                            const std::function<void()>& changed) {
    const auto draw=[&](const char* label,Skinventory::Choices& selected,
                        const std::map<std::string,Skinventory::SkinCounts>& counts,
                        const std::vector<std::string>& order) {
        std::vector<std::pair<std::string,std::string>> options;
        auto append=[&](const std::string& value) {
            if(value.empty())return;
            std::string text=value=="Weapon"?"Weapons":value;
            const auto it=counts.find(value);
            if(it!=counts.end())text+=" ("+(data.hasOwnership?std::to_string(it->second.owned)+"/":"")+std::to_string(it->second.total)+")";
            options.emplace_back(value,text);
        };
        for(const auto& v:order)append(v);
        for(const auto& kv:counts)if(std::find(order.begin(),order.end(),kv.first)==order.end())append(kv.first);
        if(AlterEgo::UI::MultiFilterChoice(label,selected,options)) {filters.Prune();changed();}
    };
    draw("Category",filters.category,data.facets.category,{"Armor","Weapon","Back","Gathering"});
    if(Skinventory::Allows(filters.category,"Armor")) {
        draw("Armor weight",filters.weight,data.facets.weight,{"Heavy","Medium","Light"});
        draw("Armor slot",filters.slot,data.facets.slot,{"Helm","HelmAquatic","Shoulders","Coat","Gloves","Leggings","Boots"});
    }
    if(Skinventory::Allows(filters.category,"Weapon"))
        draw("Weapon type",filters.weapon,data.facets.weapon,Skinventory::SkinCache::GetWeaponTypes());
    draw("Rarity",filters.rarity,data.facets.rarity,{"Junk","Basic","Fine","Masterwork","Rare","Exotic","Ascended","Legendary"});
}
static void RevealSkinInBrowser(uint32_t id) {
    const auto skin=Skinventory::SkinCache::GetSkin(id);
    if(skin && !g_SkinBrowserFilters.Matches(*skin)) g_SkinBrowserFilters={};
    if(skin&&!Skinventory::Search::Matches(skin->name,Skinventory::Search::Words(g_SkinSearchFilter)))g_SkinSearchFilter[0]='\0';
    if(Skinventory::OwnedSkins::HasData()) {
        const bool owned=Skinventory::OwnedSkins::IsOwned(id);
        if((owned&&!g_SkinShowOwned)||(!owned&&!g_SkinShowUnowned))g_SkinShowOwned=g_SkinShowUnowned=true;
    }
}
// All Skinventory surfaces use this same menu, scoped to the actual row/skin.
static void RenderSkinContextMenu(const Skinventory::SkinInfo& skin,uint32_t stockItem=0) {
    if (!ImGui::BeginPopupContextItem("##skin_actions")) return;
    ImGui::TextDisabled("%s",skin.name.c_str());
    ImGui::Separator();
    CopyNameMenuItem(skin.name);
    CopyEntityChatLink(AlterEgo::LINK_SKIN,skin.id,"Copy Skin Chat Link");
    // Pie UI documents native skin-link preview through EV_PIEUI_OPEN_CHATLINK.
    if(g_PieUIPresent.load() && ImGui::MenuItem("Preview in Game"))
        PieUI_PreviewChatLink(AlterEgo::ChatLink::EncodeSkin(skin.id));
    const auto price=Skinventory::Commerce::GetPrice(skin.id);
    if(price && price->tradeable && price->item_id) {
        const auto name=Skinventory::Commerce::GetItemName(price->item_id);
        if(!name.empty()) {
            CopyNameMenuItem(name,"Copy Trading Post Item Name");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",name.c_str());
        }
        CopyEntityChatLink(AlterEgo::LINK_ITEM,price->item_id,"Copy Item Chat Link");
        if(!stockItem&&ImGui::MenuItem("Find in Inventory"))OpenInventoryItem(price->item_id);
    }
    ImGui::Separator();

    if(stockItem&&ImGui::MenuItem("Find in Inventory"))OpenInventoryItem(stockItem);
    const auto wiki=Skinventory::WikiImage::GetWikiData(skin.id);
    if(!wiki.vendor_waypoint.empty() && ImGui::MenuItem("Copy Vendor Waypoint")) CopyToClipboard(wiki.vendor_waypoint);
    if(ImGui::MenuItem("Open Wiki Page")) {
        std::string url="https://wiki.guildwars2.com/wiki/"+skin.name;
        std::replace(url.begin(),url.end(),' ','_');
        ShellExecuteA(NULL,"open",url.c_str(),NULL,NULL,SW_SHOWNORMAL);
    }
    ImGui::EndPopup();
}

// Shared Fast Load entry points. All UI actions enqueue exact account/name;
// only execute after the active page and its popup have unwound.
static std::pair<std::string,std::string> g_QueuedCharacterLoad;
static AlterEgo::FastLoadUI::Confirmation g_FastLoadConfirmation;
bool FastLoadActionsEnabled(){return g_FastLoadMode!=AlterEgo::FastLoadUI::Mode::Disabled;}
static void CancelFastLoadUI(){g_QueuedCharacterLoad={};g_FastLoadConfirmation.Clear();}
std::string CharacterLoadUnavailable(const std::string& character,const std::string& account){
    using namespace AlterEgo;
    if(!g_CurrentCharName.empty()&&character==g_CurrentCharName)return "Character already active.";
    const auto active=NativeAccounts::VerifiedAccountForCharacter(g_CurrentCharName);
    const auto target=NativeAccounts::VerifiedAccountForCharacter(character);
    if(active.empty()||target.empty()||active!=target||(!account.empty()&&account!=target))
        return "Character ownership is unknown or belongs to another linked account.";
    return GameMemory::LoadUnavailableReason(g_CurrentCharName,character,GW2API::GetCurrentAccountName(),target);
}
bool RenderCharacterLoadMenu(const char* label,const std::string& character,const std::string& account){
    if(!FastLoadActionsEnabled())return false;
    const auto reason=CharacterLoadUnavailable(character,account);
    const bool clicked=AlterEgo::UI::MenuAction(label,AlterEgo::UI::MenuIcon::Open,reason.empty(),reason.c_str());
    if(clicked){
        const auto verified=AlterEgo::NativeAccounts::VerifiedAccountForCharacter(character);
        if(g_FastLoadMode==AlterEgo::FastLoadUI::Mode::Confirm){
            if(!g_FastLoadConfirmation.Pending())
                g_FastLoadConfirmation.Begin({character,verified},g_CurrentCharName,
                    AlterEgo::GW2API::GetCurrentAccountName(),g_SelectedAccountFilter);
        }else g_QueuedCharacterLoad={character,verified};
    }
    return clicked;
}
static void TickCharacterLoad(){
    const auto notice=AlterEgo::GameMemory::Tick(g_CurrentCharName);
    if(!notice.empty()&&APIDefs)APIDefs->GUI_SendAlert(notice.c_str());
}
void ProcessCharacterLoad(){
    auto request=std::move(g_QueuedCharacterLoad);g_QueuedCharacterLoad={};
    if(request.first.empty())return;
    auto reason=CharacterLoadUnavailable(request.first,request.second);
    if(reason.empty())reason=AlterEgo::GameMemory::Load(g_CurrentCharName,request.first,AlterEgo::GW2API::GetCurrentAccountName(),request.second);
    if(APIDefs)APIDefs->GUI_SendAlert(reason.c_str());
}
// Stable root-window modal after the page and its popup have unwound.
static void RenderFastLoadConfirmation(){
    using namespace AlterEgo;
    constexpr const char* popup="Load Character?##fast_load_confirm";
    auto& request=g_FastLoadConfirmation;
    const auto currentAccount=GW2API::GetCurrentAccountName();
    if(!request.Matches(g_FastLoadMode,g_CurrentCharName,currentAccount,g_SelectedAccountFilter)||
       LibraryDraftOpen()||g_SaveLibDialogOpen)request.Clear();
    if(request.needsOpen){ImGui::OpenPopup(popup);request.needsOpen=false;}
    bool submit=false;
    if(ImGui::BeginPopupModal(popup,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        if(!request.Pending())ImGui::CloseCurrentPopup();
        else{
            ImGui::TextWrapped("Switch to %s?",request.target.first.c_str());
            const auto reason=CharacterLoadUnavailable(request.target.first,request.target.second);
            const auto action=UI::ConfirmCancel("fast_load",reason.empty(),"Load",reason.c_str());
            if(action==UI::EditAction::Cancel||UI::EditorEscape()){request.Clear();ImGui::CloseCurrentPopup();}
            else if(action==UI::EditAction::Save&&reason.empty()){
                g_QueuedCharacterLoad=request.target;
                request.Clear();submit=true;ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }else if(request.Pending())request.Clear();
    if(submit)ProcessCharacterLoad();
}
std::string PlainItemDescription(const std::string& text){return StripGW2Markup(text);}

static void RenderSkinEntityTooltip(const std::string& name,const std::string& rarity,bool hasOwnerData,bool owned){
    using namespace AlterEgo::UI;
    ImGui::BeginTooltipEx(ImGuiWindowFlags_None,ImGuiTooltipFlags_OverridePreviousTooltip);
    TooltipLine(name.c_str(),GetSkinRarityColor(rarity));
    if(hasOwnerData)TooltipLine(owned?"Owned":"Not owned",owned?ImVec4(.35f,.82f,.35f,1.f):ImVec4(.55f,.55f,.55f,1.f));
    ImGui::EndTooltip();
}

static void RenderSkinList(float width) {
    ImGui::BeginChild("SkinList", ImVec2(width, 0), true);
    const auto& data=GetSkinBrowserData();
    const bool hasOwnerData=data.hasOwnership;
    const auto changed=[] {SaveSession();};
    AlterEgo::UI::SearchToolbar("skin_browser","Search all skins...",g_SkinSearchFilter,
        [&] {
            auto chips=SkinFilterChips(g_SkinBrowserFilters,changed);
            if(hasOwnerData&&g_SkinShowOwned!=g_SkinShowUnowned)
                chips.push_back({"ownership",g_SkinShowOwned?"Owned":"Unowned",[]{g_SkinShowOwned=g_SkinShowUnowned=true;SaveSession();}});
            return chips;
        },
        [&] {
            DrawSkinFilters(g_SkinBrowserFilters,data,changed);
            int ownership=g_SkinShowOwned&&!g_SkinShowUnowned?1:!g_SkinShowOwned&&g_SkinShowUnowned?2:0;
            const char* options[]={"All","Owned","Unowned"};
            if(hasOwnerData) {
                if(AlterEgo::UI::FilterChoice("Ownership",ownership,options,3)) {
                    g_SkinShowOwned=ownership!=2;g_SkinShowUnowned=ownership!=1;changed();
                }
            } else ImGui::TextDisabled("Ownership data not loaded");
        },[&]{g_SkinBrowserFilters={};g_SkinShowOwned=g_SkinShowUnowned=true;changed();});
    struct SkinDisplayEntry {uint32_t id;bool owned;};
    static std::vector<SkinDisplayEntry> displaySkins;
    static Skinventory::FilterResult result;
    static Skinventory::SkinFilters previous;
    static uint64_t catalog=uint64_t(-1),owner=uint64_t(-1);
    static bool has=false,showOwned=true,showUnowned=true;
    static std::string query;
    const std::string current=Skinventory::Search::Lower(g_SkinSearchFilter);
    if(catalog!=data.catalog||owner!=data.ownership||has!=hasOwnerData||previous!=g_SkinBrowserFilters||
       query!=current||showOwned!=g_SkinShowOwned||showUnowned!=g_SkinShowUnowned) {
        catalog=data.catalog;owner=data.ownership;has=hasOwnerData;previous=g_SkinBrowserFilters;query=current;
        showOwned=g_SkinShowOwned;showUnowned=g_SkinShowUnowned;
        result=Skinventory::FilterSkins(data.rows,data.owned,previous,Skinventory::Search::Words(query),has,showOwned,showUnowned);
        displaySkins.clear();displaySkins.reserve(result.displayed.size());
        for(const auto& e:result.displayed)displaySkins.push_back({e.first,e.second});
    }
    const int ownedCount=result.scope.owned,totalCount=result.scope.total;
    ImGui::TextDisabled("%d matches / %d shown",result.searchMatches,(int)displaySkins.size());
    ImGui::Separator();

    bool showStatus = hasOwnerData;
    float statusHeight = showStatus ? (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y) : 0;
    float scrollHeight = ImGui::GetContentRegionAvail().y - statusHeight;

    ImGui::BeginChild("SkinListScroll", ImVec2(0, scrollHeight));
    // Grid and compact list share stable activation semantics and domain action.
    const auto selectSkin=[&](const Skinventory::SkinInfo& skin){
        MarkNavigation();g_SkinSelectedId=skin.id;
        Skinventory::WikiImage::RequestImage(skin.id,skin.name,skin.weight_class);
        Skinventory::Commerce::FetchPriceForSkin(skin.id);
    };
    if (g_SkinViewMode == 1) {
        // ===== Grid view =====
        const float tileSize = 36.0f;
        const float tileGap  = 3.0f;
        const float pitch=AlterEgo::UI::CatalogGridPitch(tileSize,tileGap);
        float availW = ImGui::GetContentRegionAvail().x;
        int cols = AlterEgo::UI::CatalogGridColumns(availW,tileSize,tileGap);
        int rows = ((int)displaySkins.size() + cols - 1) / cols;

        // Scroll-to-selected
        if (g_SkinScrollToSkin) {
            for (int i = 0; i < (int)displaySkins.size(); i++) {
                if (displaySkins[i].id == g_SkinSelectedId) {
                    AlterEgo::UI::CatalogScrollToRow(i/cols,pitch);
                    g_SkinScrollToSkin = false;
                    break;
                }
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        AlterEgo::UI::CatalogLines(rows,pitch,tileGap,[&](int r){
                for (int c = 0; c < cols; c++) {
                    int idx = r * cols + c;
                    if (idx >= (int)displaySkins.size()) break;
                    const auto& entry = displaySkins[idx];
                    auto skinOpt = Skinventory::SkinCache::GetSkin(entry.id);
                    if (c > 0) ImGui::SameLine(0, tileGap);
                    // Preserve the tile slot even if metadata is temporarily
                    // unavailable. Every grid line advances exactly pitch.
                    if (!skinOpt){ImGui::Dummy(ImVec2(tileSize,tileSize));continue;}
                    const auto& skin = *skinOpt;

                    bool selected = (g_SkinSelectedId == entry.id);
                    bool dim = hasOwnerData && !entry.owned;

                    const uint32_t displayId=entry.id+5000000;
                    const auto tmin=ImGui::GetCursorScreenPos();
                    const ImVec2 tmax(tmin.x+tileSize,tmin.y+tileSize);
                    const auto rc=GetSkinRarityColor(skin.rarity);
                    const auto bg=ImGui::ColorConvertFloat4ToU32(ImVec4(rc.x*.18f,rc.y*.18f,rc.z*.18f,1.f));
                    const auto border=ImGui::ColorConvertFloat4ToU32(dim?ImVec4(rc.x*.55f,rc.y*.55f,rc.z*.55f,.85f):rc);
                    ImGui::PushID(static_cast<int>(entry.id));
                    const auto hit=AlterEgo::UI::CatalogTile("##tile",tileSize,displayId,skin.icon_url,selected,bg,border,
                        dim?IM_COL32(160,160,160,110):IM_COL32_WHITE);

                    // Owned check (small green corner mark)
                    if (hasOwnerData && entry.owned) {
                        float cx = tmax.x - 6.0f;
                        float cy = tmin.y + 6.0f;
                        dl->AddCircleFilled(ImVec2(cx, cy), 4.0f, IM_COL32(0, 0, 0, 200));
                        dl->AddCircle(ImVec2(cx, cy), 4.0f, IM_COL32(111, 204, 122, 255), 12, 1.2f);
                        dl->AddLine(ImVec2(cx - 2, cy + 0), ImVec2(cx - 0.5f, cy + 1.5f),
                            IM_COL32(160, 230, 170, 255), 1.5f);
                        dl->AddLine(ImVec2(cx - 0.5f, cy + 1.5f), ImVec2(cx + 2.5f, cy - 2),
                            IM_COL32(160, 230, 170, 255), 1.5f);
                    }

                    if(hit.hovered)RenderSkinEntityTooltip(skin.name,skin.rarity,hasOwnerData,entry.owned);
                    RenderSkinContextMenu(skin);
                    ImGui::PopID();
                    if(hit.clicked)selectSkin(skin);
                }
                // The last tile already advances to the next line. An extra
                // Dummy here would add spacing not represented in the clipper.
        });
    } else {
        // ===== Compact list view =====
        const float rowH=AlterEgo::UI::CatalogRowHeight();
        if(g_SkinScrollToSkin){
            for(int i=0;i<(int)displaySkins.size();++i)if(displaySkins[i].id==g_SkinSelectedId){
                AlterEgo::UI::CatalogScrollToRow(i,rowH);g_SkinScrollToSkin=false;break;
            }
        }
        AlterEgo::UI::CatalogRows(static_cast<int>(displaySkins.size()),[&](int row){
            const auto& entry=displaySkins[row];auto skinOpt=Skinventory::SkinCache::GetSkin(entry.id);
            if(!skinOpt){ImGui::Dummy(ImVec2(0,rowH));return;}
            const auto& skin=*skinOpt;
            ImGui::PushID((int)entry.id);
            const auto hit=AlterEgo::UI::CatalogRow(row,g_SkinSelectedId==entry.id,entry.id+5000000,skin.icon_url,skin.name,skin.rarity,{},hasOwnerData&&!entry.owned,hasOwnerData?int(entry.owned):-1,[&]{RenderSkinEntityTooltip(skin.name,skin.rarity,hasOwnerData,entry.owned);});
            RenderSkinContextMenu(skin);ImGui::PopID();
            if(hit.clicked)selectSkin(skin);
        });
    }
    ImGui::EndChild();

    if (showStatus && totalCount > 0) {
        ImGui::Separator();
        float fraction = (float)ownedCount / (float)totalCount;
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%d / %d owned", ownedCount, totalCount);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.25f, 0.65f, 0.25f, 1.0f));
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

static void RenderSkinDetailPanel() {
    ImGui::BeginChild("SkinDetailPanel", ImVec2(0, 0), true);

    if (g_SkinSelectedId == 0) {
        ImGui::TextWrapped("Select a skin to view details.");
        ImGui::EndChild();
        return;
    }

    auto skinOpt = Skinventory::SkinCache::GetSkin(g_SkinSelectedId);
    if (!skinOpt) {
        ImGui::Text("Skin not found.");
        ImGui::EndChild();
        return;
    }
    const auto& skin = *skinOpt;

    auto sectionHeader = [](const char* label) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.75f, 0.9f, 1.0f), "%s", label);
        ImGui::Separator();
    };

    std::string subtitle=skin.rarity;
    if(!skin.type.empty())subtitle+="  /  "+skin.type;
    if(!skin.subtype.empty())subtitle+=" "+Skinventory::SkinCategorySubtype(skin.type,skin.subtype);
    if(!skin.weight_class.empty())subtitle+="  /  "+skin.weight_class;
    AlterEgo::UI::CatalogBanner(skin.name,subtitle,skin.rarity,
        Skinventory::OwnedSkins::HasData()?(Skinventory::OwnedSkins::IsOwned(g_SkinSelectedId)?"OWNED":"NOT OWNED"):"");
    ImGui::PushID((int)skin.id);RenderSkinContextMenu(skin);ImGui::PopID();
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "ID: %u", skin.id);

    // Pricing (unowned only)
    bool isOwned = Skinventory::OwnedSkins::HasData() &&
                   Skinventory::OwnedSkins::IsOwned(g_SkinSelectedId);
    if (!isOwned) {
        int vendorPrice = Skinventory::Commerce::GetVendorPrice(g_SkinSelectedId);
        const auto price = Skinventory::Commerce::GetPrice(g_SkinSelectedId);
        bool hasPrice = (vendorPrice > 0) ||
                        (price && price->tradeable && (price->sell_price > 0 || price->buy_price > 0)) ||
                        (!price && vendorPrice == 0);

        if (hasPrice) {
            sectionHeader("Pricing");

            if (vendorPrice > 0) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Vendor:");
                ImGui::SameLine();
                RenderCoins(vendorPrice);
            }

            if (price) {
                if (price->tradeable) {
                    const auto itemName = Skinventory::Commerce::GetItemName(price->item_id);
                    if (!itemName.empty() && itemName != skin.name) {
                        ImGui::TextDisabled("TP item:"); ImGui::SameLine();
                        ImGui::TextWrapped("%s", itemName.c_str());
                    }
                    if (price->sell_price > 0 && price->sell_quantity > 0) {
                        ImGui::Text("TP Buy:");
                        ImGui::SameLine();
                        RenderCoins(price->sell_price);
                    }
                    if (price->buy_price > 0) {
                        ImGui::Text("TP Sell:");
                        ImGui::SameLine();
                        RenderCoins(price->buy_price);
                    }
                }
            } else if (vendorPrice == 0) {
                if (Skinventory::Commerce::IsItemMapReady()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Price: click to load");
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Item map loading...");
                }
            }
        }
    }

    // Acquisition
    {
        auto wikiData = Skinventory::WikiImage::GetWikiData(g_SkinSelectedId);
        bool hasAny = !wikiData.acquisition.empty() || !wikiData.collection.empty() ||
                      !wikiData.set_name.empty() || !wikiData.vendor_name.empty();
        if (hasAny) {
            sectionHeader("Acquisition");
            if (!wikiData.acquisition.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Source:");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", wikiData.acquisition.c_str());
            }
            if (!wikiData.collection.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Collection:");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", wikiData.collection.c_str());
            }
            if (!wikiData.set_name.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Set:");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", wikiData.set_name.c_str());
            }
            if (!wikiData.vendor_name.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Vendor:");
                ImGui::SameLine();
                if (!wikiData.vendor_location.empty()) {
                    ImGui::TextWrapped("%s (%s)", wikiData.vendor_name.c_str(),
                                       wikiData.vendor_location.c_str());
                } else {
                    ImGui::TextWrapped("%s", wikiData.vendor_name.c_str());
                }
            }
            if (!wikiData.vendor_cost.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Cost:");
                ImGui::SameLine();
                std::string costStr = wikiData.vendor_cost;
                std::string keysStr = wikiData.vendor_cost_keys;
                std::vector<std::string> iconKeys;
                {
                    std::istringstream kss(keysStr);
                    std::string kk;
                    while (std::getline(kss, kk, ',')) {
                        if (!kk.empty()) iconKeys.push_back(kk);
                    }
                }
                std::vector<std::string> costParts;
                {
                    size_t p = 0;
                    while (true) {
                        size_t sep = costStr.find(" + ", p);
                        if (sep == std::string::npos) {
                            costParts.push_back(costStr.substr(p));
                            break;
                        }
                        costParts.push_back(costStr.substr(p, sep - p));
                        p = sep + 3;
                    }
                }
                for (size_t ci = 0; ci < costParts.size(); ci++) {
                    if (ci > 0) {
                        ImGui::SameLine(0, 2);
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "+");
                        ImGui::SameLine(0, 2);
                    }
                    if (ci < iconKeys.size()) {
                        Texture_t* cIcon = Skinventory::WikiImage::GetCurrencyIcon(iconKeys[ci]);
                        if (cIcon && cIcon->Resource) {
                            float iconH = ImGui::GetTextLineHeight();
                            float iconW = iconH * ((float)cIcon->Width / (float)cIcon->Height);
                            ImGui::Image(cIcon->Resource, ImVec2(iconW, iconH));
                            ImGui::SameLine(0, 3);
                        }
                    }
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "%s", costParts[ci].c_str());
                }
            }
            if (!wikiData.vendor_waypoint.empty()) {
                static float s_copiedTimer = 0.0f;
                static uint32_t s_copiedSkinId = 0;

                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "Waypoint:");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                if (RenderActionButton(wikiData.vendor_waypoint.c_str())) {
                    CopyToClipboard(wikiData.vendor_waypoint);
                    s_copiedTimer = 2.0f;
                    s_copiedSkinId = g_SkinSelectedId;
                }
                ImGui::PopStyleColor();

                if (s_copiedTimer > 0.0f && s_copiedSkinId == g_SkinSelectedId) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.35f, s_copiedTimer / 2.0f),
                                       "Copied!");
                    s_copiedTimer -= ImGui::GetIO().DeltaTime;
                }
            }
        }
    }

    // Preview
    sectionHeader("Preview");

    using Skinventory::WikiImage;
    // Visible demand, including restored navigation; never scan the catalog.
    static uint32_t previewDemandId=0;
    static auto previewDemandAt=std::chrono::steady_clock::time_point{};
    const auto previewNow=std::chrono::steady_clock::now();
    if(previewDemandId!=g_SkinSelectedId||previewNow-previewDemandAt>=std::chrono::seconds(60)){
        previewDemandId=g_SkinSelectedId;previewDemandAt=previewNow;
        WikiImage::RequestImage(g_SkinSelectedId,skin.name,skin.weight_class);
    }
    ImGui::PushID(static_cast<int>(g_SkinSelectedId));
    ImGui::BeginGroup();
    Texture_t* wikiTex = WikiImage::GetImage(g_SkinSelectedId);
    if (wikiTex && wikiTex->Resource) {
        float panelWidth = ImGui::GetContentRegionAvail().x;
        float maxHeight = 400.0f;
        float imgW = (float)wikiTex->Width;
        float imgH = (float)wikiTex->Height;

        float scale = panelWidth / imgW;
        if (imgH * scale > maxHeight) {
            scale = maxHeight / imgH;
        }

        float displayW = imgW * scale;
        float displayH = imgH * scale;

        ImGui::Image(wikiTex->Resource, ImVec2(displayW, displayH));
        const auto status=WikiImage::ImageStatus(g_SkinSelectedId);
        if(WikiImage::IsLoading(g_SkinSelectedId)||status!="Preview cached")ImGui::TextDisabled("%s",status.c_str());
    } else if (Skinventory::WikiImage::IsLoading(g_SkinSelectedId)) {
        const auto status=Skinventory::WikiImage::ImageStatus(g_SkinSelectedId);
        RenderSpinner(status.c_str(), ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
    } else {
        const auto status=Skinventory::WikiImage::ImageStatus(g_SkinSelectedId);
        AlterEgo::RefreshUI::Status(AlterEgo::RefreshUI::State::Waiting,status);
    }
    ImGui::EndGroup();
    if(ImGui::BeginPopupContextItem("##preview_actions")){
        if(ImGui::MenuItem("Refresh Preview",nullptr,false,!WikiImage::IsLoading(g_SkinSelectedId)))
            WikiImage::RequestImage(g_SkinSelectedId,skin.name,skin.weight_class,true);
        ImGui::Separator();
        const auto links=WikiImage::GetPreviewLinks(g_SkinSelectedId,skin.name);
        if(ImGui::MenuItem("Copy Image URL",nullptr,false,!links.imageUrl.empty()))CopyToClipboard(links.imageUrl);
        if(ImGui::MenuItem("Copy Wiki URL",nullptr,false,!links.wikiUrl.empty()))CopyToClipboard(links.wikiUrl);
        if(ImGui::MenuItem("Copy Local File Path",nullptr,false,!links.localPath.empty()))CopyToClipboard(links.localPath);
        ImGui::EndPopup();
    }
    if(ImGui::IsItemHovered())AlterEgo::UI::Hint("Right-click for preview refresh and copy actions.");
    ImGui::PopID();

    // Action buttons
    ImGui::Separator();
    if (RenderActionButton("Open Wiki Page")) {
        std::string wikiUrl = "https://wiki.guildwars2.com/wiki/" + skin.name;
        std::replace(wikiUrl.begin(), wikiUrl.end(), ' ', '_');
        ShellExecuteA(NULL, "open", wikiUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    ImGui::EndChild();
}

static std::string g_ShopNavigationQuery;
static Skinventory::SkinFilters g_ShopNavigationFilters;
static int g_ShopNavigationSource=0,g_ShopNavigationMaxGold=0;
static bool g_ShopNavigationFilterDirty=true;
static void RenderSkinShoppingList() {
    if (!Skinventory::OwnedSkins::HasData()) {
        ImGui::TextWrapped("No ownership data. Link an API key and refresh.");
        return;
    }

    if (!Skinventory::Commerce::IsItemMapReady()) {
        std::string fetchMsg = Skinventory::Commerce::GetFetchStatus();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", fetchMsg.c_str());
        ImGui::TextWrapped("Building the skin-to-item map from the GW2 API. This is a one-time operation and will be cached for future sessions.");
        if(fetchMsg.find("failed")!=std::string::npos && RenderActionButton("Retry item map")) Skinventory::Commerce::BuildItemMapAsync();
        return;
    }

    static bool s_needsFetch = true;
    static bool s_forcePriceFetch = false;
    auto& query=g_ShopNavigationQuery;
    auto& filters=g_ShopNavigationFilters;
    auto& source=g_ShopNavigationSource;
    auto& maxGold=g_ShopNavigationMaxGold;
    const auto& data=GetSkinBrowserData();
    auto& filterDirty=g_ShopNavigationFilterDirty;
    const auto changed = [&] { filterDirty = true; };
    bool refreshClicked = false;
    if (AlterEgo::UI::SearchToolbar("shopping", "Search skins or TP items...", query,
        [&] {
            auto chips=SkinFilterChips(filters,changed);
            if(source) chips.push_back({"source",source==1?"Trading Post":"Vendor",[&]{source=0;changed();}});
            if(maxGold) chips.push_back({"price","Max "+std::to_string(maxGold)+"g",[&] {maxGold=0;changed();}});
            return chips;
        },
        [&] {
            const char* srcs[]={"All","Trading Post","Vendor"};
            DrawSkinFilters(filters,data,changed);
            if(AlterEgo::UI::FilterChoice("Source",source,srcs,3)) changed();
            if(AlterEgo::UI::FilterInt("Max gold (0 = any)",maxGold)) {maxGold=std::max(0,maxGold);changed();}
        },
        [&] {source=maxGold=0;filters={};changed();},
        "Refresh Prices", &refreshClicked)) changed();
    if(refreshClicked && !Skinventory::Commerce::IsFetching()) {
        g_SkinShopListDirty=true; s_needsFetch=true; s_forcePriceFetch=true;
    }

    // Thin progress bar when fetching (replaces verbose status text)
    if (Skinventory::Commerce::IsFetching()) {
        std::string fetchMsg = Skinventory::Commerce::GetPriceFetchStatus();
        int curN = 0, totN = 0;
        // Parse "...N/M" out of the status message
        for (size_t i = 0; i < fetchMsg.size(); i++) {
            if (isdigit((unsigned char)fetchMsg[i])) {
                int a = 0, b = 0; int consumed = 0;
                if (sscanf(fetchMsg.c_str() + i, "%d/%d%n", &a, &b, &consumed) == 2 && b > 0) {
                    curN = a; totN = b;
                    break;
                }
                while (i < fetchMsg.size() && isdigit((unsigned char)fetchMsg[i])) i++;
            }
        }
        float frac = (totN > 0) ? std::min(1.0f, (float)curN / (float)totN) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.95f, 0.76f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.13f, 1.0f));
        ImGui::ProgressBar(frac, ImVec2(-1, 3.0f), "");
        ImGui::PopStyleColor(2);
        ImGui::TextColored(ImVec4(0.85f, 0.74f, 0.40f, 1.0f), "%s", fetchMsg.c_str());
    }

    ImGui::Separator();

    static bool s_wasFetching = false;
    bool isFetching = Skinventory::Commerce::IsFetching();
    if (s_wasFetching && !isFetching) {
        g_SkinShopListDirty = true;
    }
    s_wasFetching = isFetching;
    if(!isFetching) {
        const auto status=Skinventory::Commerce::GetPriceFetchStatus();
        if(status.find("Some prices could not be retrieved")==0)
            ImGui::TextColored(AlterEgo::UI::WarningText,"%s",status.c_str());
    }

    // Build the unfiltered acquisition list only on data changes. Local
    // filters never request prices or restart the ownership/refresh lifecycle.
    static uint64_t lastOwned=0,lastPrice=0,lastCatalog=0;
    struct ShopCandidate { Skinventory::SkinInfo skin; std::string itemName; int tp, vendor; uint32_t ownedItem=0; };
    static std::vector<ShopCandidate> candidates;
    static bool needsInitial=true;
    const auto owned=Skinventory::OwnedSkins::GetGeneration();
    const auto priceGen=Skinventory::Commerce::GetPriceGeneration();
    static uint64_t lastInventory=0;
    static int64_t lastFreshnessBucket=-1;
    static std::string lastShopAccount;
    const auto inventoryGen=AlterEgo::NativeAccounts::InventoryGeneration();
    const auto shopAccount=GetEffectiveAccountName();
    const auto freshnessBucket=static_cast<int64_t>(std::time(nullptr))/30;
    if(lastInventory!=inventoryGen||lastShopAccount!=shopAccount||lastFreshnessBucket!=freshnessBucket){lastInventory=inventoryGen;lastShopAccount=shopAccount;lastFreshnessBucket=freshnessBucket;g_SkinShopListDirty=true;}
    if(lastOwned!=owned || lastPrice!=priceGen || lastCatalog!=data.catalog || needsInitial) {
        lastOwned=owned; lastPrice=priceGen; lastCatalog=data.catalog; needsInitial=false;
        g_SkinShopListDirty=true;
    }
    if(!isFetching&&g_SkinShopListDirty.exchange(false)) {
        candidates.clear();
        std::vector<uint32_t> priceIds;
        const auto acquisitions=Skinventory::Commerce::UniqueSkinCandidates();
        for(const auto& row:data.rows) {
            const auto id=row.id;
            if(data.owned.count(id)||!Skinventory::EligibleSkin(row))continue;
            const auto* skin=&row;
            if(s_needsFetch) priceIds.push_back(id);
            const auto p=Skinventory::Commerce::GetPrice(id);
            const int tp=p && p->tradeable && p->sell_quantity>0 ? p->sell_price : 0;
            const int vendor=Skinventory::Commerce::GetVendorPrice(id);
            uint32_t ownedItem=0;
            const auto acq=acquisitions.find(id);
            if(acq!=acquisitions.end())for(auto item:acq->second)
                if(AlterEgo::NativeAccounts::LooseCount(item,shopAccount)>0){ownedItem=item;break;}
            if(tp>0 || vendor>0 || ownedItem)
                candidates.push_back({*skin,ownedItem?Skinventory::Commerce::GetItemName(ownedItem):p?Skinventory::Commerce::GetItemName(p->item_id):std::string(),tp,vendor,ownedItem});
        }
        if(s_needsFetch) {
            s_needsFetch=false;
            Skinventory::Commerce::FetchPricesForSkins(priceIds,s_forcePriceFetch);s_forcePriceFetch=false;
        }
        filterDirty=true;
    }
    static int sortCol=4;
    static bool ascending=true;
    auto sortRows=[&]() {
        std::sort(g_SkinShopList.begin(),g_SkinShopList.end(),[&](const auto& a,const auto& b) {
            int cmp=0;
            if(sortCol==4) cmp=a.price<b.price?-1:a.price>b.price?1:0;
            else if(sortCol==2) cmp=a.source-b.source;
            else {
                cmp=sortCol==0?a.name.compare(b.name):a.subtype.compare(b.subtype);
            }
            return cmp==0?a.skinId<b.skinId:(ascending?cmp<0:cmp>0);
        });
    };
    if(filterDirty) {
        filterDirty=false;
        g_SkinShopList.clear();
        const auto tokens=Skinventory::Search::Words(query);
        for(const auto& entry:candidates) {
            const auto& skin=entry.skin;
            if(!filters.Matches(skin))continue;
            if(!tokens.empty() && !Skinventory::Search::Matches(skin.name+" "+entry.itemName,tokens)) continue;
            const bool useVendor=source==2 || (source==0 && entry.vendor>0 && (entry.tp<=0 || entry.vendor<=entry.tp));
            const int cost=useVendor?entry.vendor:entry.tp;
            if(entry.ownedItem){
                if(source==0||(source==1&&entry.tp>0)||(source==2&&entry.vendor>0))
                    g_SkinShopList.push_back({skin.id,0,2,skin.name,Skinventory::SkinCategorySubtype(skin.type,skin.subtype),entry.ownedItem});
                continue;
            }
            if(cost<=0 || (maxGold>0 && int64_t(cost)>int64_t(maxGold)*10000)) continue;
            g_SkinShopList.push_back({skin.id,cost,useVendor?1:0,skin.name,Skinventory::SkinCategorySubtype(skin.type,skin.subtype),0});
        }
        sortRows();
    }

    int tpCount = 0, vendorCount = 0, stockCount=0;
    for (const auto& e : g_SkinShopList) {
        if (e.source == 0) tpCount++;
        else if(e.source==1)vendorCount++;else stockCount++;
    }

    ImGui::Text("Matching locked skins: %d TP, %d Vendor, %d in stock",tpCount,vendorCount,stockCount);
    if(stockCount)ImGui::TextDisabled("Stock = cached loose acquisition item; confirm eligibility in game.");
    ImGui::Separator();

    float reserveHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImVec2 tableSize(0.0f, -reserveHeight);
    if (ImGui::BeginTable("ShoppingList", 4,
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Sortable,
        tableSize)) {

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80, 1);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 55, 2);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 120, 4);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sortCol = spec.ColumnUserID;
                sortRows();
                sortSpecs->SpecsDirty = false;
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)g_SkinShopList.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const auto& entry = g_SkinShopList[row];
                auto skinOpt = Skinventory::SkinCache::GetSkin(entry.skinId);
                if (!skinOpt) continue;
                const auto& skin = *skinOpt;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                // Rarity-coloured left accent bar in the Name cell
                {
                    ImVec4 rc = GetSkinRarityColor(skin.rarity);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float h = ImGui::GetTextLineHeightWithSpacing();
                    dl->AddRectFilled(ImVec2(p.x, p.y + 2),
                                      ImVec2(p.x + 2, p.y + h - 2),
                                      ImGui::ColorConvertFloat4ToU32(rc));
                    ImGui::Dummy(ImVec2(4.0f, 0.0f));
                    ImGui::SameLine(0, 0);
                }

                std::string selectLabel = skin.name + "##shop" + std::to_string(entry.skinId);
                ImGui::PushStyleColor(ImGuiCol_Text, GetSkinRarityColor(skin.rarity));
                if (ImGui::Selectable(selectLabel.c_str(), g_SkinSelectedId == entry.skinId,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                    MarkNavigation();g_SkinSelectedId = entry.skinId;
                    Skinventory::WikiImage::RequestImage(entry.skinId, skin.name, skin.weight_class);
                    Skinventory::Commerce::FetchPriceForSkin(entry.skinId);
                    RevealSkinInBrowser(entry.skinId);
                    g_SkinSwitchToBrowser = true;
                    g_SkinScrollToSkin = true;
                }
                ImGui::PopStyleColor();
                ImGui::PushID((int)entry.skinId);
                RenderSkinContextMenu(skin,entry.ownedItem);
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::Text("%s", skin.type=="Back" ? "Back" : Skinventory::SkinCategorySubtype(skin.type,skin.subtype).c_str());
                if (ImGui::IsItemHovered() && skin.type=="Armor") ImGui::SetTooltip("%s",skin.weight_class.c_str());

                ImGui::TableNextColumn();
                if(entry.ownedItem){ImGui::TextUnformatted("Stock");if(ImGui::IsItemHovered())ImGui::SetTooltip("A fresh cached acquisition item is in personal loose stock. Right-click the row to find it in Inventory.");}
                else if (entry.source == 1) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Vendor");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "TP");
                }

                ImGui::TableNextColumn();
                RenderCoins(entry.ownedItem?0:entry.price);
                if(entry.source==0 && ImGui::IsItemHovered()) {
                    const auto p=Skinventory::Commerce::GetPrice(entry.skinId);
                    if(p) { const auto name=Skinventory::Commerce::GetItemName(p->item_id); if(!name.empty()) ImGui::SetTooltip("%s",name.c_str()); }
                }
            }
        }

        ImGui::EndTable();
    }

    if (g_SkinShopList.empty() && !Skinventory::Commerce::IsFetching()) ImGui::TextDisabled("No matching purchasable skins in cached results.");
    if (!g_SkinShopList.empty()) {
        int64_t tpTotal = 0, vendorTotal = 0;
        for (const auto& e : g_SkinShopList) {
            if (e.source == 0) tpTotal += e.price;
            else if(e.source==1)vendorTotal += e.price;
        }
        ImGui::Separator();
        ImGui::Text("TP total:");
        ImGui::SameLine();
        RenderCoins(tpTotal);
        if (vendorTotal > 0) {
            ImGui::SameLine();
            ImGui::Text("  Vendor total:");
            ImGui::SameLine();
            RenderCoins(vendorTotal);
        }
    }
}

// Helper: draw a gradient-backed section header with colored accent underline
// Shared indicator for the currently active character or in-game template.
// Keep geometry and color identical; callers choose the center position.


// Render a scrollable chip strip (used for equipment & build tab selectors).
// Returns true if the user clicked a different chip (selectedIdx is updated in place).
static bool RenderChipStrip(const std::vector<std::string>& labels,
                            const std::vector<bool>& activeMarkers,
                            int& selectedIdx,
                            float& scrollOffset,
                            const char* idScope,
                            const char* chipTooltip,
                            int* rightClickedIdx) {
    int n = (int)labels.size();
    if (n <= 0) return false;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float chipMinW = 90.0f;
    const float chipH = ImMax(kAlterEgoActionButtonHeight, ImGui::GetFrameHeight());
    const float gap = 5.0f;
    const float arrowW = 22.0f;
    const float arrowGap = 4.0f;

    float availW = ImGui::GetContentRegionAvail().x - 6.0f;
    float idealW = chipMinW * n + gap * (n - 1);
    bool needsScroll = idealW > availW;

    float chipW = chipMinW;
    float stripW = availW;
    if (!needsScroll) {
        chipW = (availW - gap * (n - 1)) / (float)n;
    } else {
        stripW = availW - (arrowW + arrowGap) * 2.0f;
    }

    ImVec2 stripOrigin = ImGui::GetCursorScreenPos();
    if (needsScroll) stripOrigin.x += arrowW + arrowGap;

    bool changed = false;

    if (needsScroll) {
        float maxScroll = (chipW * n + gap * (n - 1)) - stripW;
        if (maxScroll < 0) maxScroll = 0;
        if (scrollOffset < 0) scrollOffset = 0;
        if (scrollOffset > maxScroll) scrollOffset = maxScroll;
        bool canLeft = scrollOffset > 0.5f;
        bool canRight = scrollOffset < maxScroll - 0.5f;

        auto DrawArrow = [&](float ax, bool right, bool enabled) -> bool {
            ImVec2 amin(ax, stripOrigin.y);
            ImVec2 amax(ax + arrowW, stripOrigin.y + chipH);
            ImGui::PushID(right ? "##chipR" : "##chipL");
            ImGui::SetCursorScreenPos(amin);
            bool clicked = ImGui::InvisibleButton("##chipArrow",
                ImVec2(arrowW, chipH)) && enabled;
            bool hov = ImGui::IsItemHovered() && enabled;
            ImGui::PopID();

            ImU32 bg = !enabled ? IM_COL32(20, 18, 14, 200)
                      : hov ? IM_COL32(50, 42, 24, 255)
                            : IM_COL32(34, 28, 18, 255);
            dl->AddRectFilled(amin, amax, bg, 4.0f);
            ImU32 border = !enabled ? IM_COL32(60, 50, 30, 80)
                          : IM_COL32(140, 115, 60, 200);
            dl->AddRect(amin, amax, border, 4.0f, 0, 1.0f);

            ImU32 chev = !enabled ? IM_COL32(110, 95, 60, 110)
                        : IM_COL32(227, 196, 122, 255);
            float cx = amin.x + arrowW * 0.5f;
            float cy = amin.y + chipH * 0.5f;
            float s = 6.0f;
            if (right) {
                dl->AddTriangleFilled(
                    ImVec2(cx - s * 0.4f, cy - s),
                    ImVec2(cx - s * 0.4f, cy + s),
                    ImVec2(cx + s * 0.6f, cy), chev);
            } else {
                dl->AddTriangleFilled(
                    ImVec2(cx + s * 0.4f, cy - s),
                    ImVec2(cx + s * 0.4f, cy + s),
                    ImVec2(cx - s * 0.6f, cy), chev);
            }
            return clicked;
        };

        if (DrawArrow(stripOrigin.x - arrowW - arrowGap, false, canLeft)) {
            scrollOffset -= chipW + gap;
            if (scrollOffset < 0) scrollOffset = 0;
        }
        if (DrawArrow(stripOrigin.x + stripW + arrowGap, true, canRight)) {
            scrollOffset += chipW + gap;
            if (scrollOffset > maxScroll) scrollOffset = maxScroll;
        }
    } else {
        scrollOffset = 0;
    }

    if (needsScroll) {
        dl->PushClipRect(stripOrigin,
            ImVec2(stripOrigin.x + stripW, stripOrigin.y + chipH), true);
    }

    ImVec2 origin = ImVec2(stripOrigin.x - scrollOffset, stripOrigin.y);

    ImGui::PushID(idScope);
    for (int i = 0; i < n; i++) {
        bool active = (selectedIdx == i);
        bool isActiveInGame = (i < (int)activeMarkers.size()) && activeMarkers[i];

        ImVec2 cMin(origin.x + (chipW + gap) * i, origin.y);
        ImVec2 cMax(cMin.x + chipW, cMin.y + chipH);

        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(cMin);
        if (ImGui::InvisibleButton("##chip", ImVec2(chipW, chipH))) {
            if (selectedIdx != i) {
                selectedIdx = i;
                changed = true;
            }
        }
        bool hovered = ImGui::IsItemHovered();
        if (hovered && chipTooltip) ImGui::SetTooltip("%s", chipTooltip);
        if (rightClickedIdx && ImGui::IsItemClicked(ImGuiMouseButton_Right)) *rightClickedIdx = i;
        ImGui::PopID();

        if (active) {
            ImU32 bgTop = IM_COL32(42, 36, 24, 255);
            ImU32 bgBot = IM_COL32(28, 24, 16, 255);
            dl->AddRectFilledMultiColor(cMin, cMax, bgTop, bgTop, bgBot, bgBot);
        } else {
            ImU32 bg = hovered ? IM_COL32(37, 35, 44, 255) : IM_COL32(29, 28, 36, 255);
            dl->AddRectFilled(cMin, cMax, bg, 4.0f);
        }
        ImU32 border = active ? IM_COL32(197, 161, 85, 255)
                      : (hovered ? IM_COL32(110, 95, 55, 180)
                                 : IM_COL32(70, 60, 40, 110));
        dl->AddRect(cMin, cMax, border, 4.0f, 0, active ? 1.5f : 1.0f);
        if (active) {
            dl->AddRectFilled(
                ImVec2(cMin.x, cMax.y - 3),
                ImVec2(cMax.x, cMax.y + 4),
                IM_COL32(197, 161, 85, 40), 3.0f);
        }

        const std::string& label = labels[i];
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        // Truncate if needed
        std::string drawLabel = label;
        if (ts.x > chipW - 12.0f) {
            // Find truncated length
            const char* ell = "...";
            float ellW = ImGui::CalcTextSize(ell).x;
            int lo = 0, hi = (int)label.size();
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ImGui::CalcTextSize(label.substr(0, mid).c_str()).x + ellW <= chipW - 12.0f) lo = mid;
                else hi = mid - 1;
            }
            drawLabel = label.substr(0, lo) + ell;
            ts = ImGui::CalcTextSize(drawLabel.c_str());
        }
        ImU32 textCol = active ? IM_COL32(240, 226, 190, 255)
                               : IM_COL32(170, 162, 140, 255);
        ImVec2 textPos(cMin.x + chipW * 0.5f - ts.x * 0.5f,
                       cMin.y + chipH * 0.5f - ts.y * 0.5f);
        dl->AddText(textPos, textCol, drawLabel.c_str());

        if (isActiveInGame) {
            AlterEgo::UI::ActiveIndicator(dl, ImVec2(cMax.x - 6, cMin.y + 6));
        }
    }
    ImGui::PopID();

    if (needsScroll) dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(stripOrigin.x - (needsScroll ? (arrowW + arrowGap) : 0),
                                     stripOrigin.y + chipH + 4.0f));
    return changed;
}

void RenderSectionHeader(const char* label, ImVec4 color, const char* suffix) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImMax(kAlterEgoActionButtonHeight, ImGui::GetTextLineHeightWithSpacing() + 8.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Gradient background
    ImU32 left = ImGui::ColorConvertFloat4ToU32(
        ImVec4(color.x * 0.22f, color.y * 0.22f, color.z * 0.22f, 0.55f));
    ImU32 right = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x + 4.0f, pos.y),
        ImVec2(pos.x + w, pos.y + h),
        left, right, right, left);

    // Left gold accent bar (vertical)
    ImU32 barTop = ImGui::ColorConvertFloat4ToU32(ImVec4(
        std::min(1.0f, color.x * 1.30f),
        std::min(1.0f, color.y * 1.30f),
        std::min(1.0f, color.z * 1.30f), 1.0f));
    ImU32 barBot = ImGui::ColorConvertFloat4ToU32(ImVec4(
        color.x * 0.70f, color.y * 0.70f, color.z * 0.70f, 1.0f));
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x, pos.y + 2.0f),
        ImVec2(pos.x + 3.0f, pos.y + h - 2.0f),
        barTop, barTop, barBot, barBot);

    // Accent underline
    dl->AddLine(ImVec2(pos.x + 4.0f, pos.y + h), ImVec2(pos.x + w, pos.y + h),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.35f)), 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f,
        pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextColored(color, "%s", label);
    if (suffix) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.47f, 0.40f, 1.0f), "%s", suffix);
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 3.0f));
}

// Standard action button: font-responsive height and centered label.
bool RenderActionButton(const char* label, ImVec2 size) {
    return AlterEgo::UI::Button(label, size);
}

// Gold-trimmed button — primary-action vocabulary matching RenderSectionHeader.
// size = ImVec2(0,0) auto-sizes to label; pass explicit size for fixed-width rows.
// Returns true on click.
bool RenderGoldButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.14f, 0.06f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.22f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.36f, 0.28f, 0.12f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.70f, 0.58f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.95f, 0.85f, 0.55f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    bool clicked = RenderActionButton(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return clicked;
}

// Small chip-style button — secondary action vocabulary.
// active: highlighted (gold tint) when true.
// accentColor: optional accent (e.g. profession colour or status colour). Pass ImVec4(0,0,0,0) for default gold.
// Returns true on click.
bool RenderChipButton(const char* label, bool active, ImVec4 accentColor, bool compact) {
    bool hasAccent = (accentColor.w > 0.01f);
    ImVec4 base    = hasAccent ? ImVec4(accentColor.x * 0.30f, accentColor.y * 0.30f, accentColor.z * 0.30f, 0.55f)
                               : ImVec4(0.15f, 0.13f, 0.09f, 0.55f);
    ImVec4 hover   = hasAccent ? ImVec4(accentColor.x * 0.45f, accentColor.y * 0.45f, accentColor.z * 0.45f, 0.75f)
                               : ImVec4(0.25f, 0.20f, 0.10f, 0.75f);
    ImVec4 actVar  = hasAccent ? ImVec4(accentColor.x * 0.55f, accentColor.y * 0.55f, accentColor.z * 0.55f, 0.90f)
                               : ImVec4(0.50f, 0.40f, 0.18f, 0.80f);
    ImVec4 text    = active    ? ImVec4(0.95f, 0.85f, 0.55f, 1.00f)
                               : ImVec4(0.78f, 0.76f, 0.66f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Button,        active ? actVar : base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  actVar);
    ImGui::PushStyleColor(ImGuiCol_Text,          text);
    bool clicked = false;
    if (compact) {
        // The normal 7px vertical FramePadding is taller than the spare room
        // inside a 22px chip. ImGui clips its text content to the padded rect,
        // placing the glyphs visibly too low. Match padding to compact height.
        // This single renderer serves New, Import, Save and Edit game modes.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        clicked = AlterEgo::UI::Button(label, ImVec2(0.0f, CompactGameModeChipHeight()), AlterEgo::UI::ButtonRole::Compact);
        ImGui::PopStyleVar(2);
    } else {
        clicked = RenderActionButton(label);
    }
    ImGui::PopStyleColor(4);
    return clicked;
}

// Gold-bordered combo box. Wraps ImGui::BeginCombo with themed styling for both
// the closed combo button and the open popup.
//
// `id`:      ImGui id (e.g. "##AccountSelect").
// `current`: the label shown when collapsed.
// `width`:   <= 0 means caller already used SetNextItemWidth; > 0 sets the width here.
//
// Usage:
//   if (RenderThemedCombo("##acct", currentLabel.c_str(), 180.0f)) {
//       if (ImGui::Selectable("...", isSel)) {...}
//       ImGui::EndCombo();
//   }
//
// Returns true when the combo is open (same contract as ImGui::BeginCombo).
// Caller is responsible for ImGui::EndCombo() when the function returns true.
bool RenderThemedCombo(const char* id, const char* current, float width) {
    return AlterEgo::UI::BeginCombo(id, current, 0, width, AlterEgo::UI::ComboRole::Gold);
}

// Centered themed empty-state / first-run card.
// iconTex:        optional Nexus texture. Pass nullptr to skip the icon row.
// headline:       bold title text (gold).
// body:           multi-line body text (wraps inside the card).
// primaryLabel /  primary action button (gold). Pass nullptr to omit.
//   primaryCB
// secondaryLabel/ secondary action button (chip). Pass nullptr to omit.
//   secondaryCB
void RenderEmptyCard(Texture_t* iconTex,
                     const char* headline,
                     const char* body,
                     const char* primaryLabel, std::function<void()> primaryCB,
                     const char* secondaryLabel, std::function<void()> secondaryCB) {
    const float cardW = 420.0f;
    const float cardH = 260.0f;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float offsetX = (avail.x - cardW) * 0.5f;
    float offsetY = (avail.y - cardH) * 0.5f;
    if (offsetX < 0) offsetX = 0;
    if (offsetY < 0) offsetY = 0;

    ImGui::Dummy(ImVec2(0, offsetY));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

    ImVec2 cardOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(cardOrigin, ImVec2(cardOrigin.x + cardW, cardOrigin.y + cardH),
                      IM_COL32(20, 17, 11, 235), 6.0f);
    dl->AddRect(cardOrigin, ImVec2(cardOrigin.x + cardW, cardOrigin.y + cardH),
                IM_COL32(178, 148, 51, 255), 6.0f, 0, 1.5f);

    ImGui::BeginChild("##empty_card_inner", ImVec2(cardW, cardH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Dummy(ImVec2(0, 12));

    if (iconTex && iconTex->Resource) {
        float iconSize = 64.0f;
        ImGui::SetCursorPosX((cardW - iconSize) * 0.5f);
        ImGui::Image(iconTex->Resource, ImVec2(iconSize, iconSize));
        ImGui::Dummy(ImVec2(0, 8));
    }

    if (headline && *headline) {
        ImVec2 sz = ImGui::CalcTextSize(headline);
        ImGui::SetCursorPosX((cardW - sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "%s", headline);
        ImGui::Dummy(ImVec2(0, 6));
    }

    if (body && *body) {
        float bodyW = cardW - 40.0f;
        ImGui::SetCursorPosX((cardW - bodyW) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bodyW);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.76f, 0.66f, 1.0f));
        ImGui::TextWrapped("%s", body);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14));
    }

    bool hasPrimary   = primaryLabel   && *primaryLabel;
    bool hasSecondary = secondaryLabel && *secondaryLabel;
    if (hasPrimary || hasSecondary) {
        float pW = hasPrimary   ? ImGui::CalcTextSize(primaryLabel).x   + 24.0f : 0;
        float sW = hasSecondary ? ImGui::CalcTextSize(secondaryLabel).x + 16.0f : 0;
        float gap = (hasPrimary && hasSecondary) ? 10.0f : 0.0f;
        float totalW = pW + sW + gap;
        ImGui::SetCursorPosX((cardW - totalW) * 0.5f);

        if (hasPrimary) {
            if (RenderGoldButton(primaryLabel, ImVec2(pW, 0))) {
                if (primaryCB) primaryCB();
            }
            if (hasSecondary) ImGui::SameLine(0, gap);
        }
        if (hasSecondary) {
            if (RenderChipButton(secondaryLabel, false)) {
                if (secondaryCB) secondaryCB();
            }
        }
    }

    ImGui::EndChild();
}

// Small inline loading spinner: rotating partial arc + label.
// Used in place of plain "Loading..." text to signal active work.
void RenderSpinner(const char* label, ImVec4 color) {
    float lineH = ImGui::GetTextLineHeight();
    float radius = lineH * 0.40f;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 center(cursor.x + radius + 2.0f, cursor.y + lineH * 0.5f);
    float t = (float)ImGui::GetTime();
    float start = t * 6.0f;
    float end = start + IM_PI * 1.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PathClear();
    dl->PathArcTo(center, radius, start, end, 32);
    dl->PathStroke(ImGui::ColorConvertFloat4ToU32(color), false, 2.0f);
    ImGui::Dummy(ImVec2(radius * 2.0f + 6.0f, lineH));
    if (label && *label) {
        ImGui::SameLine(0, 6);
        ImGui::TextColored(color, "%s", label);
    }
}

// =========================================================================
// Achievement Tracker — UI
// =========================================================================

// Immutable presentation ownership. Definition payloads are shared with the
// canonical catalog; the pointer/name index, group tree and account progress
// are independently revision cached, captured together under the domain lock.
// No renderer keeps a reference to a mutable map or holds the mutex over UI.
struct AchCatalogFrame {
    decltype(g_AchDefs) defs;
    decltype(g_AchNameIndex) names;
    std::unordered_map<std::string,uint32_t> reverseNames;
};
struct AchGroupsFrame {
    decltype(g_AchGroups) groups;
    decltype(g_AchCategories) categories;
    decltype(g_AchIdToCategory) idToCategory;
    decltype(g_AchHiddenCatIds) hiddenCats;
};
struct AchFrame {
    std::shared_ptr<const AchCatalogFrame> catalog;
    std::shared_ptr<const AchGroupsFrame> groups;
    std::shared_ptr<const decltype(g_AchProgress)> progress;
    uint64_t catalogRevision=0,groupsRevision=0,progressRevision=0;
    std::vector<uint32_t> pinned,activeMain,activeDaily;
    std::string activeEvent;
};
static AchFrame CaptureAchFrame() {
    static AlterEgo::RevisionSnapshot<AchCatalogFrame> catalog;
    static AlterEgo::RevisionSnapshot<AchGroupsFrame> groups;
    static AlterEgo::RevisionSnapshot<decltype(g_AchProgress)> progress;
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    AchFrame frame;
    frame.catalogRevision=g_AchCatalogGeneration.load();
    frame.groupsRevision=g_AchGroupsGeneration;
    frame.progressRevision=g_AchProgressGen.load();
    frame.catalog=catalog.Acquire(frame.catalogRevision,[]{
        AchCatalogFrame next{g_AchDefs,g_AchNameIndex,{}};
        next.reverseNames.reserve(g_AchNameIndex.size());
        // Match the original first-in-iteration exact-name precedence.
        for(const auto& [id,name]:g_AchNameIndex)next.reverseNames.emplace(name,id);
        return next;
    });
    frame.groups=groups.Acquire(frame.groupsRevision,[]{
        return AchGroupsFrame{g_AchGroups,g_AchCategories,g_AchIdToCategory,g_AchHiddenCatIds};
    });
    frame.progress=progress.Acquire(frame.progressRevision,[]{return g_AchProgress;});
    frame.pinned=g_AchPinned;
    frame.activeMain=g_AchActiveMainCatIds;
    frame.activeDaily=g_AchActiveDailyCatIds;
    frame.activeEvent=g_AchActiveEventName;
    return frame;
}

static uint32_t FindAchIdByName(const AchFrame& frame,const std::string& name,const std::string& parentName = "") {
    const auto& names=frame.catalog->reverseNames;
    auto it=names.find(name);
    if(it!=names.end())return it->second;
    if(!parentName.empty()){
        it=names.find(parentName+": "+name);
        if(it!=names.end())return it->second;
    }
    return 0;
}

// Navigate to a specific achievement: find its category, select it, fetch defs, expand it
static uint32_t g_NavAchievementId=0;
static void NavigateToAchievement(const AchFrame& frame,uint32_t achId) {
    MarkNavigation();g_NavAchievementId=achId;
    // Find which category contains this achievement
    auto catIt = frame.groups->idToCategory.find(achId);
    if (catIt != frame.groups->idToCategory.end()) {
        uint32_t catId = catIt->second;
        if (g_AchSelectedCatId != catId) {
            g_AchSelectedCatId = catId;
            // Find which group owns this category
            for (const auto& group : frame.groups->groups) {
                for (uint32_t gCatId : group.categories) {
                    if (gCatId == catId) {
                        g_AchSelectedGroupId = group.id;
                        break;
                    }
                }
            }
            FetchAchCategoryDefs(catId, true);
        }
    }
    // Mark this achievement as expanded and set scroll target
    g_AchExpandedInList.insert(achId);
    g_AchNavigateToId = achId;
}

static bool IsAchPinned(uint32_t id) {
    std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
    return std::find(g_AchPinned.begin(), g_AchPinned.end(), id) != g_AchPinned.end();
}

static void ToggleAchPin(uint32_t id) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        auto it = std::find(g_AchPinned.begin(), g_AchPinned.end(), id);
        if (it != g_AchPinned.end()) g_AchPinned.erase(it);
        else if (g_AchPinned.size() < 20) g_AchPinned.push_back(id);
    }
    SaveAchTrackerState();
}

static void MoveAchPin(uint32_t from,uint32_t to) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        auto a=std::find(g_AchPinned.begin(),g_AchPinned.end(),from);
        auto b=std::find(g_AchPinned.begin(),g_AchPinned.end(),to);
        if(a==g_AchPinned.end()||b==g_AchPinned.end()||a==b)return;
        std::iter_swap(a,b);
    }
    SaveAchTrackerState();
}

static void RenderAchProgressBar(float fraction, const ImVec4& color, float height = 4.0f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 0.8f)));
    if (fraction > 0.0f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width * fraction, pos.y + height),
            ImGui::GetColorU32(color));
    }
    ImGui::Dummy(ImVec2(width, height));
}

static void AchievementNameContextMenu(const std::string& name){
    if(ImGui::BeginPopupContextItem("##ach_name_actions")){
        CopyNameMenuItem(name);
        if(ImGui::MenuItem("Open in Wiki")){
            std::string url="https://wiki.guildwars2.com/wiki/"+name;
            std::replace(url.begin(),url.end(),' ','_');
            ShellExecuteA(NULL,"open",url.c_str(),NULL,NULL,SW_SHOWNORMAL);
        }
        ImGui::EndPopup();
    }
}
static void RenderAchEntry(const AchFrame& frame,uint32_t achId, bool showPinButton) {
    auto defIt = frame.catalog->defs.find(achId);
    auto progIt = (*frame.progress).find(achId);

    // If no definition yet, show ID and loading indicator
    if (defIt == frame.catalog->defs.end()) {
        // Check name index for at least a name
        auto nameIt = frame.catalog->names.find(achId);
        if (nameIt != frame.catalog->names.end()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s (loading...)", nameIt->second.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Achievement %u (loading...)", achId);
        }
        return;
    }

    const AchDef& def = *defIt->second;
    bool done = progIt != (*frame.progress).end() && progIt->second.done;
    int current = (progIt != (*frame.progress).end()) ? progIt->second.current : 0;
    int max = def.max_count > 0 ? def.max_count : ((progIt != (*frame.progress).end()) ? progIt->second.max : 0);

    ImGui::PushID((int)achId);

    bool hasBits = !def.bits.empty() && !done;
    bool expanded = g_AchExpandedInList.count(achId) > 0;
    bool pinned = IsAchPinned(achId);
    bool inProgress = !done && current > 0 && max > 0;

    // ===== Compact row =====
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rowH = 30.0f;
    float availRowW = ImGui::GetContentRegionAvail().x;
    ImVec2 cmin = ImGui::GetCursorScreenPos();
    ImVec2 cmax(cmin.x + availRowW, cmin.y + rowH);

    // Shared quiet completion theme. Gold still identifies pinned state
    // through the independent pin action; done no longer adds green outlines.
    AlterEgo::CompletionTheme::DrawRow(dl, cmin, cmax, done);
    AlterEgo::CompletionTheme::DrawCompleteMark(
        dl, ImVec2(cmin.x + 10.0f, cmin.y + rowH * 0.5f), done);

    // Main row hit area excludes the pin button.
    const float rightReserve = (showPinButton ? 22.0f : 0.0f) + 8.0f;
    ImGui::InvisibleButton("##achrow", ImVec2(availRowW - rightReserve, rowH));
    bool rowHovered = ImGui::IsItemHovered();
    bool rowClicked = ImGui::IsItemClicked();

    // Hover does not draw a gold outline; borders mean completed only.

    AchievementNameContextMenu(def.name);

    if (rowClicked && hasBits) {
        if (expanded) g_AchExpandedInList.erase(achId);
        else g_AchExpandedInList.insert(achId);
    }

    // Title + AP pill + progress badge — drawn on top of the row
    float padX = 10.0f;
    float midY = cmin.y + rowH * 0.5f;

    // Right-side elements (progress badge + AP pill, right-to-left)
    float rightX = cmax.x - rightReserve;

    if (def.total_ap > 0) {
        char ap[16]; snprintf(ap, sizeof(ap), "%d AP", def.total_ap);
        ImVec2 sz = ImGui::CalcTextSize(ap);
        float pillW = sz.x + 10.0f;
        float pillH = 16.0f;
        ImVec2 pMin(rightX - pillW, midY - pillH * 0.5f);
        ImVec2 pMax(rightX, pMin.y + pillH);
        ImU32 pBg, pBorder, pText;
        if (done) {
            pBg = IM_COL32(35, 37, 41, 190); pBorder = IM_COL32(89, 93, 96, 140);
            pText = AlterEgo::CompletionTheme::kCompleteText;
        } else {
            pBg = IM_COL32(54, 38, 14, 200); pBorder = IM_COL32(200, 160, 90, 200);
            pText = IM_COL32(232, 196, 122, 255);
        }
        dl->AddRectFilled(pMin, pMax, pBg, 3.0f);
        dl->AddRect(pMin, pMax, pBorder, 3.0f, 0, 1.0f);
        dl->AddText(ImVec2(pMin.x + 5.0f, pMin.y + (pillH - sz.y) * 0.5f), pText, ap);
        rightX = pMin.x - 6.0f;
    }

    if (max > 1) {
        char progBuf[24];
        snprintf(progBuf, sizeof(progBuf), "%d / %d", done ? max : current, max);
        ImVec2 sz = ImGui::CalcTextSize(progBuf);
        float bdW = sz.x + 10.0f;
        float bdH = 16.0f;
        ImVec2 bMin(rightX - bdW, midY - bdH * 0.5f);
        ImVec2 bMax(rightX, bMin.y + bdH);
        ImU32 bBg, bBorder, bText;
        if (done) {
            bBg = IM_COL32(35, 37, 41, 190); bBorder = IM_COL32(89, 93, 96, 140);
            bText = AlterEgo::CompletionTheme::kCompleteText;
        } else {
            bBg = IM_COL32(0, 0, 0, 110); bBorder = IM_COL32(160, 145, 110, 170);
            bText = IM_COL32(220, 210, 180, 240);
        }
        dl->AddRectFilled(bMin, bMax, bBg, 3.0f);
        dl->AddRect(bMin, bMax, bBorder, 3.0f, 0, 1.0f);
        dl->AddText(ImVec2(bMin.x + 5.0f, bMin.y + (bdH - sz.y) * 0.5f), bText, progBuf);
        rightX = bMin.x - 6.0f;
    }

    // The title conveys remaining work; pin state is shown by the
    // independent gold pin button, not by overriding completion colors.
    // Always reserve the status-mark gutter. Text positions must not shift.
    float titleX = cmin.x + padX + 10.0f;
    float titleEndX = rightX - 8.0f;
    const ImU32 titleCol = AlterEgo::CompletionTheme::Text(done);
    ImVec2 tSz = ImGui::CalcTextSize(def.name.c_str());
    dl->PushClipRect(ImVec2(titleX, cmin.y), ImVec2(titleEndX, cmax.y), true);
    dl->AddText(ImVec2(titleX, midY - tSz.y * 0.5f - 1.0f), titleCol, def.name.c_str());
    dl->PopClipRect();

    // Gold only for genuinely partial progress; a full green strip on every
    // completed row would overpower the few remaining achievements.
    if (inProgress) {
        const float frac = ImMin(1.0f, (float)current / (float)max);
        const float stripY = cmax.y - 4.0f;
        const ImVec2 stripMin(cmin.x + 4.0f, stripY);
        const ImVec2 stripMax(cmax.x - 4.0f, stripY + 2.0f);
        dl->AddRectFilled(stripMin, stripMax,
            AlterEgo::CompletionTheme::kProgressTrack, 1.0f);
        dl->AddRectFilled(stripMin,
            ImVec2(stripMin.x + (stripMax.x - stripMin.x) * frac, stripMax.y),
            AlterEgo::CompletionTheme::kInProgressGold, 1.0f);
    }

    // Hover tooltip with requirement text (so the row stays one line)
    if (rowHovered) {
        std::string cleanReq = StripGW2Markup(def.requirement);
        AlterEgo::UI::DetailTooltip(def.name.c_str(),cleanReq.c_str());
    }

    // Position the pin button
    float btnX = cmax.x - 4.0f;
    if (showPinButton) {
        float pinW = 20.0f, pinH = 20.0f;
        btnX -= pinW;
        ImGui::SetCursorScreenPos(ImVec2(btnX, midY - pinH * 0.5f));
        if(AlterEgo::UI::PinButton("##pinbtn",pinned,"Pin to tracker","Unpin from tracker",pinH))
            ToggleAchPin(achId);
    }

    // Restore cursor to below the row
    ImGui::SetCursorScreenPos(ImVec2(cmin.x, cmax.y + 2.0f));

    // Expanded bits (sub-objectives) — shown when clicked
    if (hasBits && expanded) {
        ImGui::Indent(16.0f);
        for (size_t i = 0; i < def.bits.size(); i++) {
            bool bitDone = (progIt != (*frame.progress).end()) &&
                progIt->second.completed_bits.count((uint32_t)i) > 0;
            const AchBitDef& bit = def.bits[i];

            // Resolve label and detect type: Skin, sub-Achievement, or plain Text
            std::string label;
            uint32_t skinId = 0;
            uint32_t subAchId = 0;
            std::optional<Skinventory::SkinInfo> skinInfo;
            if (bit.type == "Skin" && bit.item_id > 0) {
                skinId = bit.item_id;
                skinInfo = Skinventory::SkinCache::GetSkin(skinId);
                if (skinInfo.has_value()) {
                    label = skinInfo->name;
                } else if (!bit.text.empty()) {
                    label = bit.text;
                } else {
                    label = "Skin #" + std::to_string(skinId);
                }
            } else if (bit.type == "Text" && !bit.text.empty()) {
                label = bit.text;
                // Check if this text matches an achievement name (sub-achievement in meta)
                subAchId = FindAchIdByName(frame,bit.text,def.name);
            } else {
                label = bit.text.empty() ? ("Step " + std::to_string(i + 1)) : bit.text;
            }

            ImGui::PushID((int)i);
            if (bitDone) {
                if (subAchId > 0) {
                    // Completed sub-achievement — still clickable to navigate
                    ImGui::TextColored(
                        ImGui::ColorConvertU32ToFloat4(AlterEgo::CompletionTheme::kCompleteMark), "[x]");
                    ImGui::SameLine(0.0f, 4.0f);
                    std::string selLabel = label + "###subach";
                    ImGui::PushStyleColor(ImGuiCol_Text, AlterEgo::CompletionTheme::TextColor(true));
                    if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                        NavigateToAchievement(frame,subAchId);
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Click to navigate to achievement");
                    }
                } else {
                    ImGui::TextColored(
                        ImGui::ColorConvertU32ToFloat4(AlterEgo::CompletionTheme::kCompleteMark), "[x]");
                    ImGui::SameLine(0.0f, 4.0f);
                    ImGui::TextColored(AlterEgo::CompletionTheme::TextColor(true), "%s", label.c_str());
                }
            } else if (skinId > 0 && skinInfo.has_value()) {
                // Clickable skin entry → navigate to Skinventory
                std::string selLabel = label + "###skinbit";
                if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                    g_SkinSelectedId = skinId;
                    RevealSkinInBrowser(skinId);
                    g_SkinSwitchToBrowser = true;
                    g_SkinScrollToSkin = true;
                    MarkNavigation(); g_SwitchToSkinventory = true;
                    Skinventory::WikiImage::RequestImage(skinId, skinInfo->name, skinInfo->weight_class);
                    Skinventory::Commerce::FetchPriceForSkin(skinId);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to view in Skins");
                }
            } else if (subAchId > 0) {
                // Clickable sub-achievement → navigate to it
                std::string selLabel = label + "###subach";
                if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                    NavigateToAchievement(frame,subAchId);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to navigate to achievement");
                }
            } else {
                ImGui::TextColored(AlterEgo::CompletionTheme::TextColor(false),
                                        "%s", label.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(16.0f);
    }

    // Scroll-to target: if this achievement is the navigation target, scroll here
    if (g_AchNavigateToId == achId) {
        ImGui::SetScrollHereY(0.3f);
        g_AchNavigateToId = 0;
    }

    ImGui::PopID();
    ImGui::Spacing();
}

// One account-wide tick shared by actual rendered consumers. Never use
// remembered tab IDs or an empty tracker as evidence of current demand.
static void PollAchievementDemand() {
    static auto lastCheck=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Achievements)&&
       !g_AchProgressFetching&&AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready&&
       now-lastCheck>=std::chrono::seconds(2)){
        lastCheck=now;
        if(IsAchCategoryRefreshDue(g_AchSelectedCatId))SendAchProgressQuery({},true);
    }
}

static void RenderAchievements() {
    g_AchBrowserRendered=true;
    PollAchievementDemand();
    // Cached groups remain visible immediately on startup. Refresh stale
    // groups/categories after a daily reset instead of discarding the file.
    // Back off failed fetches to avoid retrying on every rendered frame.
    {
        const int64_t resetEpoch = std::chrono::duration_cast<std::chrono::seconds>(
            CalcLastDailyReset(std::chrono::system_clock::now())
                .time_since_epoch()).count();
        const bool groupsStale = !g_AchGroupsFetched ||
            g_AchGroupsCacheFetchedAt < resetEpoch;
        static auto s_lastGroupRefreshAttempt = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (groupsStale && !g_AchGroupsFetching &&
            (s_lastGroupRefreshAttempt == std::chrono::steady_clock::time_point{} ||
             now - s_lastGroupRefreshAttempt > std::chrono::minutes(5))) {
            s_lastGroupRefreshAttempt = now;
            FetchAchGroups();
        }
    }
    if (!g_AchNameIndexFetching && AlterEgo::AchievementNames::Due(
            g_AchNameIndexVerifiedAt.load(), std::time(nullptr), g_AchNameIndexRetryAt.load(),
            AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::PublicCatalog))) {
        FetchAchNameIndex();
    }
    if (g_AchGroupsFetched && !g_AchActiveEventFetching &&
        std::time(nullptr)>=g_AchActiveEventNextCheck.load()) {
        FetchActiveSpecialEvent();
    }

    // Cache is restored globally on startup/account selection, regardless of
    // whether this tab is visible. Only schedule the new account's refresh here.
    if (g_AchNeedRequery && !g_AchProgressFetching) {
        g_AchNeedRequery = false;
        std::vector<uint32_t> pinnedCopy;
        {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            pinnedCopy = g_AchPinned;
        }
        if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Achievements) && !pinnedCopy.empty()) {
            SendAchProgressQuery(pinnedCopy, true);
        }
        // The active category is handled by the 30-minute per-category timer
        // below, after the pinned query finishes. Do not force a global reload.
    }

    // Consume only the current rendered browser's demand, never a worker continuation.
    if(g_AchCategoryDemand&&!g_AchProgressFetching){
        const auto demand=*g_AchCategoryDemand;
        const auto account=GetEffectiveAccountName();
        const auto credential=AlterEgo::NativeAccounts::FindLinked(account);
        uint64_t catalogRevision;
        {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);catalogRevision=g_AchGroupsGeneration;}
        if(!demand.Matches(account,credential.generation,g_AchDemandRevision,catalogRevision,g_AchSelectedCatId))
            g_AchCategoryDemand.reset();
        else{
            const auto ids=AchCategoryIds(demand.category);
            if(!AlterEgo::AchievementDefinitions::Pending(ids)){
                g_AchCategoryDemand.reset();
                if(!ids.empty()&&(!demand.automatic||IsAchCategoryRefreshDue(demand.category)))
                    SendAchProgressQuery(ids,demand.automatic,demand.category,&demand);
            }
        }
    }

    AlterEgo::UI::SearchField("achievements", "Search achievements...", g_AchSearchBuf);

    if (!g_AchGroupsFetched) return;
    if(AlterEgo::Search::Words(g_AchSearchBuf).empty()&&g_AchSelectedCatId){
        static auto lastDefinitionCheck=std::chrono::steady_clock::time_point{};
        const auto now=std::chrono::steady_clock::now();
        if(now-lastDefinitionCheck>=std::chrono::seconds(2)){
            lastDefinitionCheck=now;RequestCategoryDefinitions(g_AchSelectedCatId,false);
        }
    }else g_AchCatFetching=false;

    // A full native account snapshot needs one request, not one per category.
    if(g_AchNeedsInitialFullLoad&&!g_AchProgressFetching&&!g_AchCatFetching&&
       (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Achievements)||g_AchManualInitialScan)){
        static auto lastAttempt=std::chrono::steady_clock::time_point{};
        static std::string lastAccount;
        const auto account=GetAchievementCacheAccount();
        const auto now=std::chrono::steady_clock::now();
        if(!account.empty()&&(lastAccount!=account||lastAttempt==std::chrono::steady_clock::time_point{}||
            now-lastAttempt>=std::chrono::seconds(30))){
            lastAccount=account;lastAttempt=now;SendAchProgressQuery({},!g_AchManualInitialScan);
        }
    }

    const auto frame=CaptureAchFrame();

    // Search mode — cached results to avoid iterating 8000+ names every frame
    const auto achievementSearchWords=AlterEgo::Search::Words(g_AchSearchBuf);
    bool searchActive = !achievementSearchWords.empty();
    if (searchActive) {
        std::string query=AlterEgo::Search::Lower(g_AchSearchBuf);

        if (!g_AchNameIndexReady) {
            if (g_AchNameIndexFetching) RenderSpinner("Loading search index...");
            else ImGui::TextDisabled("Achievement names unavailable. Retry from the Achievements menu.");
            return;
        }

        // Refresh local search results when names or category visibility change.
        static uint64_t s_achSearchCatalogGen = 0, s_achSearchGroupsGen = 0;
        static std::string s_achSearchCache;
        static std::vector<uint32_t> s_achSearchResults;
        static bool s_achSearchTruncated = false;

        {
            if (s_achSearchCache != query || s_achSearchCatalogGen != frame.catalogRevision || s_achSearchGroupsGen != frame.groupsRevision) {
            s_achSearchCache = query;
            s_achSearchCatalogGen = frame.catalogRevision;
            s_achSearchGroupsGen = frame.groupsRevision;
            s_achSearchResults.clear();
            s_achSearchTruncated = false;

            for (const auto& [id, name] : frame.catalog->names) {
                if (AlterEgo::Search::Matches(name,achievementSearchWords)) {
                    // Skip achievements from hidden groups (e.g. Character Adventure Guide)
                    auto catIt = frame.groups->idToCategory.find(id);
                    if (catIt != frame.groups->idToCategory.end() && frame.groups->hiddenCats.count(catIt->second)) continue;
                    s_achSearchResults.push_back(id);
                    if (s_achSearchResults.size() >= 50) {
                        s_achSearchTruncated = true;
                        break;
                    }
                }
            }
            }
        }

        ImGui::BeginChild("AchSearchResults", ImVec2(0, 0), true);
        for (uint32_t id : s_achSearchResults) {
            // Show category hint
            auto catIt = frame.groups->idToCategory.find(id);
            if (catIt != frame.groups->idToCategory.end()) {
                auto catDefIt = frame.groups->categories.find(catIt->second);
                if (catDefIt != frame.groups->categories.end()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", catDefIt->second.name.c_str());
                    ImGui::SameLine();
                }
            }

            // If we have the full def, render it
            if (frame.catalog->defs.count(id)) {
                RenderAchEntry(frame,id,true);
            } else {
                auto nameIt = frame.catalog->names.find(id);
                ImGui::Text("%s", nameIt != frame.catalog->names.end() ? nameIt->second.c_str() : "?");
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
                ImGui::PushID((int)id);
                bool pinned = IsAchPinned(id);
                if (pinned) {
                    if (RenderActionButton("Unpin")) ToggleAchPin(id);
                } else {
                    if (RenderActionButton("Pin")) ToggleAchPin(id);
                }
                ImGui::PopID();
                ImGui::Spacing();
            }
        }
        if (s_achSearchTruncated) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "... and more (refine your search)");
        }
        if (s_achSearchResults.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No results");
        }
        ImGui::EndChild();
        return;
    }

    // Main layout: left tree + splitter + right list
    const AlterEgo::UI::SplitPanel split(g_AchTreeWidth,220.f);
    float availHeight=split.height;

    // Left panel: Group/Category tree
    ImGui::BeginChild("AchTree", ImVec2(split.left, availHeight), true);

    if (g_AchRestoreScroll) {
        ImGui::SetScrollY(g_AchTreeScrollY);
    }
    {

        // Compute (done, total, hasAny) for a category — cached per progress generation
        struct CatStats { int done; int total; bool hasAny; };
        static std::unordered_map<uint32_t, CatStats> s_catStats;
        static uint64_t s_catStatsGen = (uint64_t)-1, s_catGroupsGen = (uint64_t)-1;
        if (s_catStatsGen != frame.progressRevision || s_catGroupsGen != frame.groupsRevision) {
            s_catStats.clear();
            s_catStatsGen = frame.progressRevision;
            s_catGroupsGen = frame.groupsRevision;
        }
        auto getCatStats = [&](uint32_t catId) -> CatStats {
            auto it = s_catStats.find(catId);
            if (it != s_catStats.end()) return it->second;
            CatStats st{0, 0, false};
            auto catIt = frame.groups->categories.find(catId);
            if (catIt != frame.groups->categories.end()) {
                st.total = (int)catIt->second.achievements.size();
                for (uint32_t aid : catIt->second.achievements) {
                    auto pit = (*frame.progress).find(aid);
                    if (pit != (*frame.progress).end()) {
                        st.hasAny = true;
                        if (pit->second.done) st.done++;
                    }
                }
            }
            s_catStats[catId] = st;
            return st;
        };

        // Hide fully completed categories only when every achievement's
        // completion is known. Unknown/partially loaded categories stay visible.
        auto hideCompletedCategory = [&](uint32_t catId) {
            if (!g_AchHideCompletedCats) return false;
            const CatStats stats = getCatStats(catId);
            return stats.total > 0 && stats.done == stats.total;
        };

        // Render a category row with gold-bar selection, owned/total badge, dim if no progress
        auto renderCatRow = [&](const AchCategoryDef* cat, const std::string& groupId,
                                bool isSelected) -> bool {
            CatStats st = getCatStats(cat->id);
            bool complete = (st.done == st.total && st.total > 0 && st.hasAny);

            ImGui::PushID((int)cat->id);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 startPos = ImGui::GetCursorScreenPos();
            float availW = ImGui::GetContentRegionAvail().x;
            float rowH = ImGui::GetTextLineHeight() + 4.0f;

            // Active background + gold left bar
            if (isSelected) {
                ImVec2 bgMin = startPos;
                ImVec2 bgMax(startPos.x + availW, startPos.y + rowH);
                dl->AddRectFilled(bgMin, bgMax, IM_COL32(80, 64, 28, 70), 2.0f);
                dl->AddRectFilled(ImVec2(bgMin.x, bgMin.y + 2),
                                  ImVec2(bgMin.x + 2, bgMax.y - 2),
                                  IM_COL32(232, 196, 122, 230));
            }

            ImVec4 textCol = complete
                ? AlterEgo::CompletionTheme::TextColor(true)
                : isSelected ? ImVec4(0.95f, 0.85f, 0.55f, 1.0f)
                             : AlterEgo::CompletionTheme::TextColor(false);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.04f));
            bool clicked = ImGui::Selectable(cat->name.c_str(), isSelected);
            ImGui::PopStyleColor(3);

            // Count badge — only shown once progress data exists for at least one
            // achievement in the category (otherwise lazy-load means "0" would lie)
            if (st.total > 0 && st.hasAny) {
                char buf[24];
                snprintf(buf, sizeof(buf), "%d/%d", st.done, st.total);
                ImVec2 cSz = ImGui::CalcTextSize(buf);
                float cx = startPos.x + availW - cSz.x - 6.0f;
                float cy = startPos.y + (rowH - cSz.y) * 0.5f - 1.0f;
                ImU32 countCol = complete ? AlterEgo::CompletionTheme::kMetaText
                                : isSelected ? IM_COL32(232, 196, 122, 230)
                                             : IM_COL32(140, 132, 110, 220);
                dl->AddText(ImVec2(cx, cy), countCol, buf);
            }

            if (clicked && g_AchSelectedCatId != cat->id) {
                MarkNavigation();g_NavAchievementId=0;
                g_AchSelectedCatId = cat->id;
                g_AchSelectedGroupId = groupId;
                FetchAchCategoryDefs(cat->id, true);
            }
            ImGui::PopID();
            return true;
        };

        // Active event — shown at top of tree when a bonus event or festival is active
        if (!frame.activeMain.empty() || !frame.activeEvent.empty()) {
            static const std::string kEventGroupId = "__active_event__";
            bool forceOpenEvent = g_AchRestoreScroll && (g_AchSelectedGroupId == kEventGroupId);
            if (forceOpenEvent) ImGui::SetNextItemOpen(true);
            else if (!g_AchRestoreScroll) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

            std::string eventLabel = frame.activeEvent.empty() ? "Current Event" : frame.activeEvent;
            if (ImGui::TreeNode(eventLabel.c_str())) {
                if (frame.activeMain.empty()) {
                    // Recognised event with no tracked achievement category (e.g. a
                    // pure reward-boost event, or one whose category isn't live yet).
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextWrapped("Bonus rewards event - no tracked achievements.");
                    ImGui::PopStyleColor();
                }
                for (uint32_t catId : frame.activeMain) {
                    auto it = frame.groups->categories.find(catId);
                    if (it == frame.groups->categories.end() || it->second.name.empty() ||
                        hideCompletedCategory(catId)) continue;
                    renderCatRow(&it->second, kEventGroupId, g_AchSelectedCatId == catId);
                }
                ImGui::TreePop();
            }
        }

        // Build set of active daily cat IDs for quick lookup
        std::unordered_set<uint32_t> activeDailySet(frame.activeDaily.begin(), frame.activeDaily.end());

        for (const auto& group : frame.groups->groups) {
            if (group.name.empty()) continue;
            if (group.name == "Character Adventure Guide") continue;

            bool isDaily = (group.name == "Daily");

            // Sort categories by order, skipping hidden categories — and aggregate group stats
            std::vector<const AchCategoryDef*> sortedCats;
            int grpDone = 0, grpTotal = 0;
            bool grpHasAny = false;
            for (uint32_t catId : group.categories) {
                if (frame.groups->hiddenCats.count(catId)) continue;
                auto it = frame.groups->categories.find(catId);
                if (it != frame.groups->categories.end() && !it->second.name.empty() &&
                    !hideCompletedCategory(catId)) {
                    sortedCats.push_back(&it->second);
                    CatStats st = getCatStats(catId);
                    grpDone += st.done; grpTotal += st.total;
                    if (st.hasAny) grpHasAny = true;
                }
            }
            // A group with no remaining categories is not useful while the
            // filter is enabled. Keep Daily active-event categories in mind.
            bool hasVisibleActiveDaily = false;
            if (isDaily) {
                for (uint32_t activeId : frame.activeDaily) {
                    auto activeIt = frame.groups->categories.find(activeId);
                    if (activeIt != frame.groups->categories.end() &&
                        !activeIt->second.name.empty() &&
                        !hideCompletedCategory(activeId)) {
                        hasVisibleActiveDaily = true;
                        break;
                    }
                }
            }
            if (g_AchHideCompletedCats && sortedCats.empty() && !hasVisibleActiveDaily)
                continue;

            std::sort(sortedCats.begin(), sortedCats.end(),
                [](const AchCategoryDef* a, const AchCategoryDef* b) { return a->order < b->order; });

            // Compose group header with count badge
            std::string groupHeader = group.name;
            if (grpTotal > 0) {
                char buf[32];
                if (grpHasAny) snprintf(buf, sizeof(buf), "  (%d/%d)", grpDone, grpTotal);
                else           snprintf(buf, sizeof(buf), "  (%d)", grpTotal);
                groupHeader += buf;
            }

            // Auto-open the saved group. Use the group's stable id as the
            // TreeNode identity so changing counts in the label don't make
            // ImGui treat it as a new node and forget the open state.
            bool forceOpen = g_AchRestoreScroll && (group.id == g_AchSelectedGroupId);
            if (forceOpen) ImGui::SetNextItemOpen(true);
            const bool groupComplete = grpTotal > 0 && grpDone == grpTotal && grpHasAny;
            if (groupComplete)
                ImGui::PushStyleColor(ImGuiCol_Text, AlterEgo::CompletionTheme::TextColor(true));
            const bool groupOpen = ImGui::TreeNodeEx(group.id.c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupHeader.c_str());
            if (groupComplete) ImGui::PopStyleColor();
            if (groupOpen) {
                // For Daily group: show active event dailies first
                if (isDaily && !frame.activeDaily.empty()) {
                    for (uint32_t catId : frame.activeDaily) {
                        auto it = frame.groups->categories.find(catId);
                        if (it == frame.groups->categories.end() || it->second.name.empty() ||
                            hideCompletedCategory(catId)) continue;
                        renderCatRow(&it->second, group.id, g_AchSelectedCatId == catId);
                    }
                }

                for (const auto* cat : sortedCats) {
                    if (isDaily && activeDailySet.count(cat->id)) continue;
                    renderCatRow(cat, group.id, g_AchSelectedCatId == cat->id);
                }
                ImGui::TreePop();
            }
        }
    }
    g_AchTreeScrollY = ImGui::GetScrollY();
    ImGui::EndChild();

    if(split.Divider("##achtree_splitter",g_AchTreeWidth))SaveAchTrackerState();

    // Right panel: split into category list (top) + pinned section (bottom)
    ImGui::BeginChild("##AchRight", ImVec2(0, availHeight), false);
    {
        float rightWidth = ImGui::GetContentRegionAvail().x;
        bool hasPinned = !frame.pinned.empty();
        float splitterH = hasPinned ? 6.0f : 0.0f;

        // Clamp pinned height
        if (hasPinned) {
            float minPinned = 60.0f;
            float maxPinned = availHeight - 100.0f;
            g_AchPinnedHeight = (g_AchPinnedHeight < minPinned) ? minPinned : (g_AchPinnedHeight > maxPinned) ? maxPinned : g_AchPinnedHeight;
        }

        float innerAvail = ImGui::GetContentRegionAvail().y;
        float spacing = ImGui::GetStyle().ItemSpacing.y;
        float topHeight = hasPinned ? (innerAvail - g_AchPinnedHeight - splitterH - 2 * spacing) : innerAvail;

        // Top: Fixed category heading + scrollable list
        float catHeaderH = 0.0f;
        {
            if (g_AchSelectedCatId > 0) {
                auto catIt = frame.groups->categories.find(g_AchSelectedCatId);
                if (catIt != frame.groups->categories.end()) {
                    float startY = ImGui::GetCursorPosY();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.25f, 1.0f), "%s", catIt->second.name.c_str());

                    // Hide Complete chip — focuses the list on remaining work
                    {
                        bool active = g_AchHideCompletedCats;
                        const char* filterLabel = active ? "Show Complete##list" : "Hide Complete##list";
                        const float filterWidth = ImGui::CalcTextSize(filterLabel).x
                            + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - filterWidth - 6.0f);
                        if (RenderActionButton(filterLabel)) {
                            g_AchHideCompletedCats = !g_AchHideCompletedCats;
                            SaveAchTrackerState();
                        }
                    }

                    if (!catIt->second.description.empty()) {
                        std::string cleanDesc = StripGW2Markup(catIt->second.description);
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", cleanDesc.c_str());
                    }
                    ImGui::Separator();
                    catHeaderH = ImGui::GetCursorPosY() - startY;
                }
            }
        }

        ImGui::BeginChild("AchList", ImVec2(rightWidth, topHeight - catHeaderH), true);
        if (g_AchRestoreScroll) {
            ImGui::SetScrollY(g_AchListScrollY);
            g_AchRestoreScroll = false;
        }
        {

            if (g_AchSelectedCatId > 0) {
                auto catIt = frame.groups->categories.find(g_AchSelectedCatId);
                if (catIt != frame.groups->categories.end()) {
                    if (g_AchCatFetching) {
                        RenderSpinner("Loading...", ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
                    }

                    int visibleAchievements = 0;
                    for (uint32_t achId : catIt->second.achievements) {
                        if (g_AchHideCompletedCats) {
                            auto pit = (*frame.progress).find(achId);
                            if (pit != (*frame.progress).end() && pit->second.done) continue;
                        }
                        ++visibleAchievements;
                        RenderAchEntry(frame,achId,true);
                    }
                    if (g_AchHideCompletedCats && visibleAchievements == 0)
                        ImGui::TextDisabled("All achievements in this category completed.");
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select a category from the left panel");
            }
        }
        g_AchListScrollY = ImGui::GetScrollY();
        ImGui::EndChild();

        // Horizontal splitter + Pinned section
        if (hasPinned) {
            // Draggable horizontal splitter
            {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##achpinned_splitter", ImVec2(rightWidth, splitterH));
                bool hovered = ImGui::IsItemHovered();
                bool active = ImGui::IsItemActive();
                if (hovered || active)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                if (active) {
                    float delta = ImGui::GetIO().MouseDelta.y;
                    if (delta != 0.0f) {
                        g_AchPinnedHeight -= delta;
                        float minPinned = 60.0f;
                        float maxPinned = availHeight - 100.0f;
                        g_AchPinnedHeight = (g_AchPinnedHeight < minPinned) ? minPinned : (g_AchPinnedHeight > maxPinned) ? maxPinned : g_AchPinnedHeight;
                    }
                }
                if (ImGui::IsItemDeactivated()) {
                    SaveAchTrackerState();
                }
                ImU32 lineCol = (hovered || active)
                    ? IM_COL32(180, 160, 80, 180)
                    : IM_COL32(80, 75, 60, 100);
                float cy = pos.y + splitterH * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(pos.x, cy), ImVec2(pos.x + rightWidth, cy), lineCol, 2.0f);
            }

            // Bottom: Fixed pinned heading + scrollable pinned list
            float pinnedHeaderH = 0.0f;
            {
                float startY = ImGui::GetCursorPosY();
                ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.25f, 1.0f), "Pinned (%d)", (int)frame.pinned.size());
                ImGui::Separator();
                pinnedHeaderH = ImGui::GetCursorPosY() - startY;
            }
            ImGui::BeginChild("AchPinned", ImVec2(rightWidth, g_AchPinnedHeight - pinnedHeaderH), true);
            {
                int pinnedCount = (int)frame.pinned.size();
                int moveFrom = -1, moveTo = -1;
                for (int idx = 0; idx < pinnedCount; idx++) {
                    uint32_t id = frame.pinned[idx];
                    ImGui::PushID(idx + 90000);

                    // Up/down arrow buttons (drawn triangles)
                    float btnSize = ImGui::GetFrameHeight();
                    ImVec4 dimCol(0.5f, 0.5f, 0.5f, 0.3f);
                    ImVec4 normalCol(0.7f, 0.65f, 0.4f, 1.0f);

                    // Up button
                    bool canUp = idx > 0;
                    if (!canUp) ImGui::PushStyleColor(ImGuiCol_Text, dimCol);
                    if (ImGui::InvisibleButton("##up", ImVec2(btnSize, btnSize)) && canUp) {
                        moveFrom = idx; moveTo = idx - 1;
                    }
                    {
                        ImVec2 rmin = ImGui::GetItemRectMin();
                        ImVec2 rmax = ImGui::GetItemRectMax();
                        float cx = (rmin.x + rmax.x) * 0.5f;
                        float cy = (rmin.y + rmax.y) * 0.5f;
                        float hs = btnSize * 0.25f;
                        ImU32 col = canUp ? (ImGui::IsItemHovered() ? IM_COL32(255, 230, 120, 255) : IM_COL32(180, 165, 100, 255))
                                          : IM_COL32(128, 128, 128, 80);
                        ImGui::GetWindowDrawList()->AddTriangleFilled(
                            ImVec2(cx, cy - hs), ImVec2(cx - hs, cy + hs), ImVec2(cx + hs, cy + hs), col);
                    }
                    if (!canUp) ImGui::PopStyleColor();

                    ImGui::SameLine(0, 1);

                    // Down button
                    bool canDown = idx < pinnedCount - 1;
                    if (!canDown) ImGui::PushStyleColor(ImGuiCol_Text, dimCol);
                    if (ImGui::InvisibleButton("##dn", ImVec2(btnSize, btnSize)) && canDown) {
                        moveFrom = idx; moveTo = idx + 1;
                    }
                    {
                        ImVec2 rmin = ImGui::GetItemRectMin();
                        ImVec2 rmax = ImGui::GetItemRectMax();
                        float cx = (rmin.x + rmax.x) * 0.5f;
                        float cy = (rmin.y + rmax.y) * 0.5f;
                        float hs = btnSize * 0.25f;
                        ImU32 col = canDown ? (ImGui::IsItemHovered() ? IM_COL32(255, 230, 120, 255) : IM_COL32(180, 165, 100, 255))
                                            : IM_COL32(128, 128, 128, 80);
                        ImGui::GetWindowDrawList()->AddTriangleFilled(
                            ImVec2(cx, cy + hs), ImVec2(cx - hs, cy - hs), ImVec2(cx + hs, cy - hs), col);
                    }
                    if (!canDown) ImGui::PopStyleColor();

                    ImGui::SameLine(0, 4);

                    RenderAchEntry(frame,id,true);

                    ImGui::PopID();
                }
                // Apply move after loop
                if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
                    MoveAchPin(frame.pinned[moveFrom],frame.pinned[moveTo]);
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild(); // ##AchRight
}

// =========================================================================
// Achievement Tracker — Popout Window
// =========================================================================

// Cached display data for the popout — rebuilt only when inputs change
struct PopoutBitDisplay {
    std::string label;
    uint32_t skinId = 0;
    uint32_t subAchId = 0;
    std::string skinType;
    std::string skinWeightClass;
    std::string skinSubtype;
    std::string skinName;
    bool done = false;
};

struct PopoutAchDisplay {
    uint32_t achId = 0;
    std::string name;
    std::string requirement;
    std::string loadingName;
    bool hasDef = false;
    bool done = false;
    bool hasBits = false;
    int current = 0;
    int max = 0;
    std::vector<PopoutBitDisplay> bits;
};

static std::vector<PopoutAchDisplay> s_popoutCache;
static std::vector<uint32_t> s_popoutLastPinned;
static uint64_t s_popoutLastProgressGen = 0, s_popoutLastCatalogGen = 0;
static bool s_popoutCacheDirty = true;


static void InvalidatePopoutCache() { s_popoutCacheDirty = true; }

static void RebuildPopoutCache(const AchFrame& frame) {
    // Resolve skin labels and sub-achievements after releasing the domain lock.
    s_popoutCache.clear();
    s_popoutCache.reserve(frame.pinned.size());

    for (uint32_t achId : frame.pinned) {
        PopoutAchDisplay disp;
        disp.achId = achId;

        auto defIt = frame.catalog->defs.find(achId);
        auto progIt = (*frame.progress).find(achId);

        disp.done = (progIt != (*frame.progress).end()) && progIt->second.done;
        disp.current = (progIt != (*frame.progress).end()) ? progIt->second.current : 0;

        if (defIt != frame.catalog->defs.end()) {
            const AchDef& def = *defIt->second;
            disp.hasDef = true;
            disp.name = def.name;
            disp.requirement = StripGW2Markup(def.requirement);
            disp.max = def.max_count > 0 ? def.max_count : ((progIt != (*frame.progress).end()) ? progIt->second.max : 0);
            disp.hasBits = !def.bits.empty() && !disp.done;

            if (disp.hasBits) {
                disp.bits.reserve(def.bits.size());
                for (size_t i = 0; i < def.bits.size(); i++) {
                    PopoutBitDisplay bd;
                    bd.done = (progIt != (*frame.progress).end()) &&
                        progIt->second.completed_bits.count((uint32_t)i) > 0;

                    const AchBitDef& bit = def.bits[i];
                    if (bit.type == "Skin" && bit.item_id > 0) {
                        bd.skinId = bit.item_id;
                        auto skinInfo = Skinventory::SkinCache::GetSkin(bd.skinId);
                        if (skinInfo.has_value()) {
                            bd.label = skinInfo->name;
                            bd.skinType = skinInfo->type;
                            bd.skinWeightClass = skinInfo->weight_class;
                            bd.skinSubtype = skinInfo->subtype;
                            bd.skinName = skinInfo->name;
                        } else {
                            bd.label = !bit.text.empty() ? bit.text : "Skin #" + std::to_string(bd.skinId);
                        }
                    } else if (bit.type == "Text" && !bit.text.empty()) {
                        bd.label = bit.text;
                        bd.subAchId = FindAchIdByName(frame,bit.text,def.name);
                    } else {
                        bd.label = bit.text.empty() ? ("Step " + std::to_string(i + 1)) : bit.text;
                    }

                    disp.bits.push_back(std::move(bd));
                }
            }
        } else {
            disp.hasDef = false;
            auto nameIt = frame.catalog->names.find(achId);
            disp.loadingName = (nameIt != frame.catalog->names.end()) ? nameIt->second : "";
        }

        s_popoutCache.push_back(std::move(disp));
    }

    s_popoutLastPinned = frame.pinned;
    s_popoutLastProgressGen = frame.progressRevision;
    s_popoutLastCatalogGen = frame.catalogRevision;
    s_popoutCacheDirty = false;
}

static void RenderAchPopout() {
    static bool s_popoutHadDemand=false;
    if (!g_AchPopoutVisible || g_CurrentCharName.empty()) {
        s_popoutHadDemand=false;
        return;
    }

    const auto frame=CaptureAchFrame();
    if (s_popoutCacheDirty || frame.pinned != s_popoutLastPinned ||
        frame.progressRevision != s_popoutLastProgressGen || s_popoutLastCatalogGen != frame.catalogRevision) {
        RebuildPopoutCache(frame);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(250, 100), ImVec2(500, 800));
    const bool rendered=ImGui::Begin("Achievement Tracker", &g_AchPopoutVisible,
                     ImGuiWindowFlags_NoCollapse);
    const bool demand=rendered&&g_AchPopoutVisible&&!frame.pinned.empty();
    if(demand){
        static auto lastDefinitionCheck=std::chrono::steady_clock::time_point{};
        const auto now=std::chrono::steady_clock::now();
        if(!s_popoutHadDemand||now-lastDefinitionCheck>=std::chrono::seconds(2)){
            lastDefinitionCheck=now;FetchPinnedAchDefs();
        }
        PollAchievementDemand();
    }
    s_popoutHadDemand=demand;
    if (rendered) {

        // Toolbar
        ImGui::Checkbox("Show completed steps", &g_AchShowCompletedSteps);
        ImGui::SameLine();
        if (g_AchProgressFetching) {
            RenderSpinner("", ImVec4(0.95f, 0.85f, 0.55f, 1.0f));
        } else if (RenderChipButton("Refresh##pop", false)) {
            std::vector<uint32_t> pinnedCopy;
            {
                std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                pinnedCopy = g_AchPinned;
            }
            if (!pinnedCopy.empty()) {
                SendAchProgressQuery(pinnedCopy);
            }
        }
        ImGui::Separator();

        if (s_popoutCache.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No pinned achievements");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Pin achievements from the Achievements tab");
        }

        // Render from cache — no lock needed. Tighten vertical gap between rows.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
        for (int idx = 0; idx < (int)s_popoutCache.size(); idx++) {
            const auto& disp = s_popoutCache[idx];
            ImGui::PushID(idx + 80000);
            ImGui::BeginGroup();

            if (disp.hasDef) {
                // ===== Compact row (matches main list) =====
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float rowH = 28.0f;
                float availRowW = ImGui::GetContentRegionAvail().x;
                ImVec2 cmin = ImGui::GetCursorScreenPos();
                ImVec2 cmax(cmin.x + availRowW, cmin.y + rowH);

                bool inProgress = !disp.done && disp.current > 0 && disp.max > 0;
                ImU32 stateCol = disp.done ? IM_COL32(111, 204, 122, 255)
                              : inProgress ? IM_COL32(244, 196, 122, 230)
                                           : IM_COL32(232, 196, 122, 230);

                ImU32 bgCol = disp.done ? IM_COL32(28, 38, 28, 60) : IM_COL32(80, 64, 28, 50);
                dl->AddRectFilled(cmin, cmax, bgCol, 3.0f);
                dl->AddRect(cmin, cmax, IM_COL32(0, 0, 0, 130), 3.0f, 0, 1.0f);
                dl->AddRectFilled(ImVec2(cmin.x, cmin.y + 3),
                                  ImVec2(cmin.x + 3, cmax.y - 3), stateCol);

                bool expanded = g_AchExpandedInPopout[disp.achId];

                // Reserve right side for unpin button
                const float unpinW = 18.0f;
                float rowHitW = availRowW - unpinW - 6.0f;

                ImGui::InvisibleButton("##poprow", ImVec2(rowHitW, rowH));
                bool rowHovered = ImGui::IsItemHovered();
                bool rowClicked = ImGui::IsItemClicked();
                if(rowHovered&&disp.hasDef)AlterEgo::UI::DetailTooltip(disp.name.c_str(),disp.requirement.c_str());
                AchievementNameContextMenu(disp.name);
                if (rowHovered)
                    dl->AddRect(cmin, cmax, IM_COL32(197, 161, 85, 130), 3.0f, 0, 1.0f);
                if (rowClicked && disp.hasBits) {
                    g_AchExpandedInPopout[disp.achId] = !expanded;
                    SaveAchTrackerState();
                }

                float padX = 10.0f;
                float midY = cmin.y + rowH * 0.5f;

                // Progress badge on the right (before unpin)
                float rightX = cmax.x - unpinW - 6.0f;
                if (disp.max > 1) {
                    char progBuf[24];
                    snprintf(progBuf, sizeof(progBuf), "%d / %d",
                        disp.done ? disp.max : disp.current, disp.max);
                    ImVec2 sz = ImGui::CalcTextSize(progBuf);
                    float bdW = sz.x + 10.0f;
                    float bdH = 16.0f;
                    ImVec2 bMin(rightX - bdW, midY - bdH * 0.5f);
                    ImVec2 bMax(rightX, bMin.y + bdH);
                    ImU32 bBg, bBorder, bText;
                    if (disp.done) {
                        bBg = IM_COL32(40, 60, 40, 180); bBorder = IM_COL32(120, 200, 130, 200);
                        bText = IM_COL32(160, 220, 170, 255);
                    } else {
                        bBg = IM_COL32(0, 0, 0, 110); bBorder = IM_COL32(160, 145, 110, 170);
                        bText = IM_COL32(220, 210, 180, 240);
                    }
                    dl->AddRectFilled(bMin, bMax, bBg, 3.0f);
                    dl->AddRect(bMin, bMax, bBorder, 3.0f, 0, 1.0f);
                    dl->AddText(ImVec2(bMin.x + 5.0f, bMin.y + (bdH - sz.y) * 0.5f), bText, progBuf);
                    rightX = bMin.x - 6.0f;
                }

                // Title (clipped)
                float titleX = cmin.x + padX + 4.0f;
                float titleEndX = rightX - 6.0f;
                ImU32 titleCol = disp.done
                    ? IM_COL32(150, 200, 150, 255)
                    : IM_COL32(245, 220, 150, 255);
                ImVec2 tSz = ImGui::CalcTextSize(disp.name.c_str());
                dl->PushClipRect(ImVec2(titleX, cmin.y), ImVec2(titleEndX, cmax.y), true);
                dl->AddText(ImVec2(titleX, midY - tSz.y * 0.5f - 1.0f), titleCol, disp.name.c_str());
                dl->PopClipRect();

                // 2px progress strip at bottom
                if (disp.max > 0) {
                    float frac = disp.done ? 1.0f : (float)disp.current / (float)disp.max;
                    float stripY = cmax.y - 4.0f;
                    ImVec2 sMin(cmin.x + 4.0f, stripY);
                    ImVec2 sMax(cmax.x - 4.0f, stripY + 2.0f);
                    dl->AddRectFilled(sMin, sMax, IM_COL32(255, 255, 255, 18), 1.0f);
                    float fillEnd = sMin.x + (sMax.x - sMin.x) * frac;
                    if (fillEnd > sMin.x)
                        dl->AddRectFilled(sMin, ImVec2(fillEnd, sMax.y), stateCol, 1.0f);
                }

                // Shared tracker pin button
                {
                    float btnH = 18.0f;
                    ImGui::SetCursorScreenPos(ImVec2(cmax.x - unpinW - 4.0f, midY - btnH * 0.5f));
                    if(AlterEgo::UI::PinButton("##popunpin",true,"Pin to tracker","Unpin from tracker",btnH))
                        ToggleAchPin(disp.achId);
                }

                ImGui::SetCursorScreenPos(ImVec2(cmin.x, cmax.y + 2.0f));

                if (disp.hasBits && g_AchExpandedInPopout[disp.achId]) {
                    for (size_t i = 0; i < disp.bits.size(); i++) {
                        const auto& bd = disp.bits[i];
                        if (!g_AchShowCompletedSteps && bd.done) continue;

                        ImGui::PushID((int)i);
                        if (bd.done) {
                            if (bd.subAchId > 0) {
                                std::string selLabel = "    [x] " + bd.label + "###popsubach";
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.82f, 0.35f, 0.6f));
                                if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                                    NavigateToAchievement(frame,bd.subAchId);
                                }
                                ImGui::PopStyleColor();
                            } else {
                                ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.35f, 0.6f), "    [x] %s", bd.label.c_str());
                            }
                        } else if (bd.skinId > 0 && !bd.skinName.empty()) {
                            std::string selLabel = "    [ ] " + bd.label + "###popskinbit";
                            if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                                g_SkinSelectedId = bd.skinId;
                                RevealSkinInBrowser(bd.skinId);
                                g_SkinSwitchToBrowser = true;
                                g_SkinScrollToSkin = true;
                                MarkNavigation(); g_SwitchToSkinventory = true;
                                Skinventory::WikiImage::RequestImage(bd.skinId, bd.skinName, bd.skinWeightClass);
                                Skinventory::Commerce::FetchPriceForSkin(bd.skinId);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Click to view in Skins");
                            }
                        } else if (bd.subAchId > 0) {
                            std::string selLabel = "    [ ] " + bd.label + "###popsubach";
                            if (ImGui::Selectable(selLabel.c_str(), false, ImGuiSelectableFlags_AllowItemOverlap)) {
                                NavigateToAchievement(frame,bd.subAchId);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Click to navigate to achievement");
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "    [ ] %s", bd.label.c_str());
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Click to copy name");
                                if (ImGui::IsItemClicked()) {
                                    CopyToClipboard(bd.label);
                                }
                            }
                        }
                        ImGui::PopID();
                    }
                }
            } else {
                if (!disp.loadingName.empty()) {
                    ImGui::Text("%s (loading...)", disp.loadingName.c_str());
                } else {
                    ImGui::Text("Achievement %u (loading...)", disp.achId);
                }
            }
            ImGui::EndGroup();

            ImGui::PopID();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

static void RenderSkinventory() {
    // Keep OwnedSkins account in sync with dropdown selection
    Skinventory::OwnedSkins::SetAccountName(GetEffectiveAccountName());

    auto cacheStatus = Skinventory::SkinCache::GetStatus();

    if (cacheStatus == Skinventory::CacheStatus::Loading ||
        cacheStatus == Skinventory::CacheStatus::Empty) {
        std::string msg = Skinventory::SkinCache::GetStatusMessage();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", msg.c_str());
        return;
    }

    if (cacheStatus == Skinventory::CacheStatus::Error) {
        std::string msg = Skinventory::SkinCache::GetStatusMessage();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", msg.c_str());
        return;
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("##skin_tabs")) {
        ImGuiTabItemFlags browserFlags = 0;
        if (g_SkinSwitchToBrowser) {
            browserFlags = ImGuiTabItemFlags_SetSelected;
            g_SkinSwitchToBrowser = false;
        }
        if (ImGui::BeginTabItem("Browser", nullptr, browserFlags)) {
            if(g_SkinActiveTab!=0)MarkNavigation();
            g_SkinActiveTab = 0;
            ImGui::Spacing();
            const AlterEgo::UI::SplitPanel split(g_SkinListWidth,420.f,200.f,240.f);
            RenderSkinList(split.left);
            if(split.Divider("##skin_split",g_SkinListWidth))SaveSettings();
            RenderSkinDetailPanel();
            ImGui::EndTabItem();
        }
        const auto shoppingFlags=g_SkinSwitchToShopping?ImGuiTabItemFlags_SetSelected:0;
        g_SkinSwitchToShopping=false;
        if (ImGui::BeginTabItem("Shopping List",nullptr,shoppingFlags)) {
            if(g_SkinActiveTab!=1)MarkNavigation();
            g_SkinActiveTab = 1;
            ImGui::Spacing();
            AlterEgo::NativeAccounts::PollItemDemand(GetEffectiveAccountName());
            RenderSkinShoppingList();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}


// Read-only projection of the existing scheduler and exact request evidence.
struct CharacterFooterInspection {
    AlterEgo::RefreshStatus::Aggregate aggregate;
    AlterEgo::RefreshStatus::Kind kind=AlterEgo::RefreshStatus::Kind::NoData;
    std::string detail;
    bool busy=false;
};
static CharacterFooterInspection DescribeCharacterRefresh(const AlterEgo::Character& ch,AlterEgo::CharacterScopes::Mask demand,int64_t now){
    namespace A=AlterEgo::CharacterActivity;
    namespace S=AlterEgo::CharacterScopes;
    namespace R=AlterEgo::RefreshStatus;
    using W=A::WaitReason;
    CharacterFooterInspection out;
    const bool enabled=AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Characters);
    const bool linked=AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready;
    const auto access=AlterEgo::NativeAccounts::InspectCharacterAccess(ch.account_name,ch.name);
    const auto request=AlterEgo::GW2API::InspectCharacterRefresh(ch.account_name,ch.name,access.generation);
    const auto& work=request.work;out.busy=request.busy;
    std::array<A::Inspection,5> rows{};
    {
        std::lock_guard<std::mutex> lock(A::Mutex());
        for(auto s:S::Ordered)if(demand&s)rows[S::Index(s)]=A::State().Inspect(
            {ch.account_name,ch.name},S::Name(s),ch.scope_stamps[S::Index(s)],now,
            g_AutoRefreshOnSpecialization&&S::SpecSensitive(s));
    }
    const auto scopeLabel=[](S::Scope s)->const char*{
        switch(s){case S::Core:return "Character details";case S::Builds:return "Build tabs";
        case S::Tabs:return "Equipment tabs";case S::Physical:return "Physical equipment";
        default:return "Crafting";}
    };
    const auto& core=rows[S::Index(S::Core)];
    out.detail=(access.label.empty()?std::string{}:access.label+" / ")+ch.name+" - "+(core.active?"Active":"Not active");
    if(core.lastSeen>0)out.detail+="\nLast observed: "+R::Age(core.lastSeen,now);
    out.detail+="\nAutomatic character refresh: "+std::string(enabled?"Enabled":"Disabled");
    const auto scopeAccess=[&](S::Scope s){
        if(!linked)return R::Access::NoAPI;
        if(!access.linked||!access.verified)return R::Access::Unverified;
        if(!access.characters||((s==S::Builds||s==S::Tabs)&&!access.builds)||(s==S::Physical&&!access.inventories))
            return R::Access::Denied;
        return R::Access::Allowed;
    };
    int64_t logoutWait=0;
    for(auto s:S::Ordered)if(demand&s){
        const auto& row=rows[S::Index(s)];
        if(enabled&&scopeAccess(s)==R::Access::Allowed&&!row.fresh&&
           row.eligibility.reason==W::LogoutGrace&&row.eligibility.at>now)
            logoutWait=std::max(logoutWait,row.eligibility.at);
    }
    if(logoutWait)out.detail+="\nWaiting for API sync after logout: "+R::Duration(logoutWait-now);
    for(auto s:S::Ordered)if(demand&s){
        const auto& row=rows[S::Index(s)];
        R::Scope scope;scope.success=row.success;scope.attempt=row.attempt;scope.fresh=row.fresh;
        scope.automatic=enabled;scope.access=scopeAccess(s);scope.eligibility=row.eligibility;
        scope.error=row.error;scope.failed=!scope.error.empty();
        if((work.failed&s)&&work.finished>=scope.success){
            scope.failed=true;if(scope.error.empty())scope.error=work.detail.empty()?"Scope did not complete":work.detail;
        }
        if(work.requested&s){
            if(work.phase==R::Phase::Working)scope.phase=(work.active&s)?R::Phase::Working:R::Phase::Queued;
            else scope.phase=work.phase;
        }
        out.aggregate.Add(scope,now);
        out.detail+="\n"+std::string(scopeLabel(s))+": "+R::Describe(scope,now,logoutWait);
    }
    out.kind=out.aggregate.Status();
    if(out.kind==R::Kind::Working||out.kind==R::Kind::Queued){
        if(out.kind==R::Kind::Working&&!request.message.empty())out.detail+="\nRequest: "+request.message;
        else if(work.phase==R::Phase::Working)out.detail+="\nSelected scopes are pending in the current character request.";
    }else{
        if(work.finished>0&&now>=work.finished&&now-work.finished<4&&
           (work.verified&demand)==demand&&out.kind==R::Kind::Ready)out.kind=R::Kind::Complete;
        if(request.busy)out.detail+="\nAnother character or roster operation is in progress.";
        else if(linked)out.detail+="\nManual Refresh bypasses ordinary waiting periods.";
    }
    return out;
}

// Build the picker from the immutable, already verified roster/binding
// projection. Opening a selection control never initiates HTTP or SQL.
static void PopulateCharacterRefreshPicker(){
    using namespace AlterEgo;
    g_RefreshNames.clear();g_RefreshAddresses.clear();g_RefreshSelection.clear();
    std::set<CharacterOrder::Key> keys;
    std::map<CharacterOrder::Key,CharacterIdentity::Address> addresses;
    for(const auto& account:NativeAccounts::Linked()){
        if(!account.rosterKnown||!account.permissions.count("characters")||
           (!g_SelectedAccountFilter.empty()&&account.id!=g_SelectedAccountFilter))continue;
        for(const auto& [name,binding]:account.bindings)if(account.characters.count(name)){
            const CharacterOrder::Key key{account.id,name};
            keys.insert(key);addresses.emplace(key,CharacterIdentity::Address{account.id,name,binding,account.generation});
        }
    }
    const auto chars=GW2API::GetCharacters();
    const int ri=SelectedCharacterIndex(*chars);
    const auto current=ri>=0?CharacterAddressFor((*chars)[ri]):std::nullopt;
    for(const auto& key:OrderedCharacterKeys(keys)){
        const auto& address=addresses.at(key);
        g_RefreshNames.push_back(key.second);g_RefreshAddresses.push_back(address);
        const bool checked=current&&CharacterIdentity::SameAddress(*current,address);
        g_RefreshSelection.push_back(checked);
    }
}

// Roster-level command, independent of selected-character data refresh.
static void RenderCharacterRosterContext(){
    using namespace AlterEgo;
    const bool ready=GW2API::GetAccountStatus()==AccountStatus::Ready;
    const bool busy=GW2API::GetFetchStatus()==FetchStatus::InProgress||g_RefreshListFetching;
    const auto accounts=NativeAccounts::Linked();
    const bool permitted=std::any_of(accounts.begin(),accounts.end(),[](const auto& account){
        return (g_SelectedAccountFilter.empty()||account.id==g_SelectedAccountFilter)&&account.permissions.count("characters");
    });
    const char* reason=!ready?"Link or verify an API account in Nexus options.":
        busy?"A character or roster request is already in progress.":
        "No linked account with character permission in this scope.";
    if(UI::MenuAction("Refresh Character List",UI::MenuIcon::Regenerate,ready&&!busy&&permitted,reason,
        "Check the account roster for new, renamed or removed characters and verify their identities.")){
        g_RefreshListAccount=g_SelectedAccountFilter;
        g_RefreshListNotice.clear();g_RefreshListFinished=0;g_RefreshListFailed=false;
        g_RefreshListFetching=true;GW2API::RequestCharacterList(g_RefreshListAccount);
    }
}
static void RenderCharacterRosterBlankContext(){
    if(ImGui::BeginPopupContextWindow("##character_roster_actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){
        RenderCharacterRosterContext();ImGui::EndPopup();
    }
}

static void RenderCharactersStatusRow() {
    const auto charsSnapshot=AlterEgo::GW2API::GetCharacters();
    const auto& chars=*charsSnapshot;
    const int selectedIndex=SelectedCharacterIndex(chars);
    const auto* selected=selectedIndex>=0?&chars[selectedIndex]:nullptr;
    namespace S=AlterEgo::CharacterScopes;
    namespace R=AlterEgo::RefreshStatus;
    const auto demand=S::Visible(g_SelectedTab);
    const auto roster=AlterEgo::GW2API::InspectRosterRefresh();
    const auto now=AlterEgo::CharacterActivity::Now();
    if(g_RefreshListFetching&&roster.status!=AlterEgo::FetchStatus::InProgress){
        g_RefreshListFetching=false;g_RefreshListFinished=now;
        g_RefreshListFailed=roster.status!=AlterEgo::FetchStatus::Success;
        g_RefreshListNotice=roster.message;
    }
    // Keep an open selection stable; roster results never open the picker.
    const bool pickerPending=g_RefreshListFetching||ImGui::IsPopupOpen("Select Characters to Refresh");
    std::vector<AlterEgo::CharacterIdentity::Address> due;
    if(selected&&!pickerPending)if(const auto address=DueCharacterAddress(selected->account_name,selected->name,selected->scope_stamps,S::All))due.push_back(*address);
    const auto active=AlterEgo::CharacterActivity::ActiveKey();
    if(!pickerPending&&!active.first.empty()&&(g_SelectedAccountFilter.empty()||g_SelectedAccountFilter==active.first)&&
       (!selected||selected->name!=active.second||selected->account_name!=active.first)){
        S::Stamps stamps{};for(const auto& ch:chars)if(ch.account_name==active.first&&ch.name==active.second){stamps=ch.scope_stamps;break;}
        if(const auto address=DueCharacterAddress(active.first,active.second,stamps,S::All))due.push_back(*address);
    }
    if(!due.empty())AlterEgo::GW2API::RequestCharacterActivityRefreshAddresses(due);
    const bool ready=AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready;
    CharacterFooterInspection inspection;
    if(selected)inspection=DescribeCharacterRefresh(*selected,S::All,now);
    else{
        const auto fetch=AlterEgo::GW2API::GetFetchStatus();
        inspection.busy=fetch==AlterEgo::FetchStatus::InProgress;
        inspection.kind=!ready?R::Kind::NoAPI:inspection.busy?R::Kind::Working:
            fetch==AlterEgo::FetchStatus::Error?R::Kind::Failed:R::Kind::NoData;
        inspection.detail=ready?AlterEgo::GW2API::GetFetchStatusMessage():"Link an API key in Nexus options";
    }
    auto label=R::Label(inspection.kind,now,inspection.aggregate.retryAt);
    if(g_RefreshListFetching){
        label="Refreshing character list";inspection.kind=R::Kind::Working;
        inspection.detail+="\nCharacter list: "+roster.message;
    }else if(!g_RefreshListNotice.empty()){
        inspection.detail+="\nLast character list refresh ("+
            std::string(g_RefreshListAccount.empty()?"all eligible accounts":"selected account")+
            "): "+g_RefreshListNotice;
        if(!inspection.busy&&g_RefreshListFinished>0&&now>=g_RefreshListFinished&&now-g_RefreshListFinished<5){
            label=g_RefreshListFailed?"Character list failed":"Character list updated";
            inspection.kind=g_RefreshListFailed?R::Kind::Failed:R::Kind::Complete;
        }
    }
    const bool blocked=!ready||inspection.busy||g_RefreshListFetching;
    const auto address=selected?CharacterAddressFor(*selected):std::nullopt;
    const char* disabledReason=!ready?"Link or verify an API account in Nexus options.":
        g_RefreshListFetching?"The character list is being retrieved.":
        inspection.busy?"A character or roster request is already in progress.":
        !address?"Select a verified character. Use ... to select characters or right-click to refresh the list.":nullptr;
    bool more=false;
    if(AlterEgo::RefreshUI::Row("characters",inspection.aggregate,"selected character",
        AlterEgo::RefreshUI::StatusState(inspection.kind),inspection.detail,blocked||!address,&more,label.c_str(),true,now,disabledReason,&g_RefreshPopupAnchor,
        RenderCharacterRosterContext,!blocked,"Refresh the selected character's visible data.")){
        if(address){AlterEgo::GW2API::RequestCharacterRefreshAddresses({*address},demand);g_DetailsFetched=false;}
    }
    if(more&&!blocked&&!g_RefreshListFetching){
        PopulateCharacterRefreshPicker();
        g_RefreshPopupOpen=true;
    }
}

static void RenderCompareStatusRow() {
    const auto charsSnapshot=AlterEgo::GW2API::GetCharacters();
    const AlterEgo::Character* target=nullptr;
    for(const auto& ch:*charsSnapshot)if(MatchesSelection(g_LibCompareRenderedSelection,ch)){
        target=&ch;break;
    }
    namespace S=AlterEgo::CharacterScopes;
    namespace R=AlterEgo::RefreshStatus;
    if(target){
        if(const auto address=DueCharacterAddress(target->account_name,target->name,target->scope_stamps,S::All))
            AlterEgo::GW2API::RequestCharacterActivityRefreshAddresses({*address});
        AlterEgo::NativeAccounts::PollItemDemand(target->account_name);}
    const auto now=AlterEgo::CharacterActivity::Now();
    const bool ready=AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready;
    CharacterFooterInspection inspection;
    if(target)inspection=DescribeCharacterRefresh(*target,S::All,now);
    else{
        inspection.busy=AlterEgo::GW2API::GetFetchStatus()==AlterEgo::FetchStatus::InProgress;
        inspection.kind=ready?R::Kind::NoData:R::Kind::NoAPI;
        inspection.detail=ready?"No retrieved character selected":"Link or verify an API account in Nexus options.";
        if(inspection.busy)inspection.detail+="\nA character or roster operation is in progress.";
    }
    const auto label=target?R::Label(inspection.kind,now,inspection.aggregate.retryAt):ready?std::string("No character"):R::Label(inspection.kind);
    const char* disabledReason=!ready?"Link or verify an API account in Nexus options.":
        !target?"Select a retrieved character.":inspection.busy?"A character or roster request is already in progress.":nullptr;
    if(AlterEgo::RefreshUI::Row("compare",inspection.aggregate,"character build/equipment",
        AlterEgo::RefreshUI::StatusState(inspection.kind),inspection.detail,inspection.busy||!target||!ready,nullptr,label.c_str(),true,now,disabledReason)){
        if(target)if(const auto address=CharacterAddressFor(*target))AlterEgo::GW2API::RequestCharacterRefreshAddresses({*address},S::Compare);
    }
}

static void RenderSkinventoryStatusRow() {
    // Ownership admission and timing belong to OwnedSkins. This footer
    // supplies visible demand.
    static bool s_refreshPending = false;
    static uint64_t s_refreshStartingGeneration = 0;
    static auto s_successUntil = std::chrono::steady_clock::time_point{};
    static std::string s_statusAccount;
    const std::string visibleAccount = GetEffectiveAccountName();
    if (s_statusAccount != visibleAccount) {
        s_statusAccount = visibleAccount;
        s_refreshPending = false;
        s_successUntil = {};
    }
    Skinventory::OwnedSkins::SetAccountName(visibleAccount);
    bool querying = Skinventory::OwnedSkins::IsQuerying();
    bool canRefresh = Skinventory::OwnedSkins::IsAccountAvailable() && !querying;
    if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::SkinOwnership) &&
        canRefresh && Skinventory::OwnedSkins::RefreshDue()) {
        s_refreshPending = true;
        s_refreshStartingGeneration = Skinventory::OwnedSkins::GetGeneration();
        Skinventory::OwnedSkins::RequestOwnedSkinsViaApi(false);
    }
    querying = Skinventory::OwnedSkins::IsQuerying();
    if (s_refreshPending && !querying) {
        s_refreshPending = false;
        if (Skinventory::OwnedSkins::GetGeneration() != s_refreshStartingGeneration)
            s_successUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
    AlterEgo::RefreshUI::State state = AlterEgo::RefreshUI::State::Ready;
    const auto msg = Skinventory::OwnedSkins::GetStatusMessage();
    std::string detail = Skinventory::OwnedSkins::HasData()
        ? "Ready - " + std::to_string(Skinventory::OwnedSkins::GetOwnedCount()) + " owned"
        : "No ownership data - click Refresh";
    if (querying || msg.find("busy") != std::string::npos) {
        state = AlterEgo::RefreshUI::State::Working;
        detail = msg.empty() ? "Querying owned skins..." : msg;
    } else if (msg.find("denied") != std::string::npos ||
               msg.find("Failed") != std::string::npos ||
               msg.find("truncated") != std::string::npos) {
        state = AlterEgo::RefreshUI::State::Error;
        detail = msg;
    } else if (std::chrono::steady_clock::now() < s_successUntil) {
        state = AlterEgo::RefreshUI::State::Success;
        detail = "Refresh complete - " + std::to_string(Skinventory::OwnedSkins::GetOwnedCount()) + " owned";
    } else if (!Skinventory::OwnedSkins::IsAccountAvailable()) {
        detail = "Link an API key in Nexus options";
    }
    if (Skinventory::SkinCache::GetStatus() == Skinventory::CacheStatus::Updating) {
        state = AlterEgo::RefreshUI::State::Working;
        detail = Skinventory::SkinCache::GetStatusMessage();
    }
    if (AlterEgo::RefreshUI::Row("skinventory", Skinventory::OwnedSkins::GetLastSuccessTime(),
             "owned skins", state, detail, !canRefresh))
    {
        s_refreshPending=true;s_refreshStartingGeneration=Skinventory::OwnedSkins::GetGeneration();
        Skinventory::OwnedSkins::RequestOwnedSkinsViaApi();
    }
}

static void RenderAchievementsStatusRow() {
    const bool namesWorking = g_AchNameIndexFetching.load();
    const bool working = g_AchGroupsFetching || g_AchCatFetching || namesWorking ||
        g_AchProgressFetching;
    std::string detail, namesError, refreshError;
    std::chrono::steady_clock::time_point successUntil;
    int64_t verifiedAt=0;
    {
        std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
        detail = namesWorking ? g_AchNameIndexStatus : g_AchStatusMsg;
        namesError = g_AchNameIndexError;
        refreshError=g_AchRefreshError;successUntil=g_AchRefreshSuccessUntil;
        verifiedAt=g_AchProgressCacheSavedAt.load();
    }
    AlterEgo::RefreshUI::State state = AlterEgo::RefreshUI::State::Ready;
    if (working) {
        state = AlterEgo::RefreshUI::State::Working;
        if (detail.empty()) detail = "Querying achievement progress...";
    } else if (!refreshError.empty()) {
        state = AlterEgo::RefreshUI::State::Error;
        detail = refreshError;
    } else if (!namesError.empty()) {
        state = AlterEgo::RefreshUI::State::Error;
        detail = namesError;
    } else if (std::chrono::steady_clock::now() < successUntil) {
        state = AlterEgo::RefreshUI::State::Success;
        detail = "Refresh complete";
    } else {
        detail = g_AchGroupsFetched ? "Ready" : "Loading achievement categories...";
    }
    if (AlterEgo::RefreshUI::Row("achievements", (std::time_t)verifiedAt,
            "achievement progress", state, detail)) {
        {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            g_AchRefreshError.clear();
            ++g_AchRequestGeneration;
            g_AchProgressFetching = false; // invalidate a replaced request
            g_AchProgressQueryStartMs = 0;
            g_AchStatusMsg.clear();
        }
        // A manual refresh targets the selected category, not every category.
        // Only rebuild public group definitions if they have never been cached.
        if (!g_AchGroupsFetched) FetchAchGroups();
        if (g_AchNeedsInitialFullLoad) g_AchManualInitialScan = true;
        if (g_AchSelectedCatId > 0) {
            FetchAchCategoryDefs(g_AchSelectedCatId);
        } else {
            std::vector<uint32_t> pinnedCopy;
            {
                std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
                pinnedCopy = g_AchPinned;
            }
            SendAchProgressQuery(pinnedCopy);
        }
    }
}


static void PollAchievementCachePersistence() {
    const std::string account = GetAchievementCacheAccount();
    if (account != g_AchCachedAccount) {
        {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            g_AchSwitchingAccount=true;++g_AchRequestGeneration;}
        // Flush outgoing data before loading another account. Account switching
        // must never overwrite the previous account's progress file.
        if (g_AchProgressDirty && !g_AchCachedAccount.empty())
            SaveAchProgress(g_AchCachedAccount);
        if(!account.empty())LoadAchProgress(account);
        else {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);g_AchProgress.clear();g_AchOptimisticTime.clear();g_AchCachedAccount.clear();g_AchProgressCacheSavedAt=0;g_AchProgressLoadedSnapshotAt=0;g_AchProgressDirty=false;++g_AchProgressGen;}
        {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);g_AchSwitchingAccount=false;}

        // Responses/retries issued for the old account must not update the new
        // cache. Fresh queries can be scheduled for the selected account.
        g_AchProgressFetching = false;
        g_AchProgressQueryStartMs = 0;
        ++g_AchDemandRevision;g_AchCategoryDemand.reset();
    }

    // Runs even while the Achievements tab (or the whole addon window) is
    // closed, so API replies and alerts are persisted without opening the tab.
    if (g_AchProgressDirty && !g_AchCachedAccount.empty()) {
        bool due;
        {std::lock_guard<std::recursive_mutex> lock(g_AchMutex);due=std::chrono::steady_clock::now()-g_AchLastSave>=std::chrono::seconds(30);}
        if(due)SaveAchProgress(g_AchCachedAccount);
    }
}

// --- Main Render ---

// Pushes the configured default TTF for the whole frame (all windows + popouts).
// Pushes NOTHING for the Nexus-default face or a still-loading TTF (OOTB invariant).
struct DefaultFontScope {
    bool pushed = false;
    explicit DefaultFontScope(const AlterEgo::Font::Config& cfg) {
        ImFont* f = AlterEgo::FontManager::ResolveDefault(cfg);
        if (f) { ImGui::PushFont(f); pushed = true; }
    }
    ~DefaultFontScope() { if (pushed) ImGui::PopFont(); }
};

// Local gameplay observations run while the addon window is hidden. The
// Nexus gameplay flag avoids treating an old identity as active at selection.
static void ObserveCharacterActivity(){
    namespace A=AlterEgo::CharacterActivity;
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(now-last<std::chrono::seconds(1))return;
    last=now;
    const auto* nexus=APIDefs?static_cast<const NexusLinkData_t*>(APIDefs->DataLink_Get(DL_NEXUS_LINK)):nullptr;
    if(!nexus)return; // no reliable gameplay signal; do not invent activity
    const auto* identity=static_cast<const MumbleIdentity*>(APIDefs->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));
    std::string name;uint32_t spec=0;
    if(identity){
        char buf[20]={};memcpy(buf,identity->Name,19);name=buf;spec=identity->Specialization;
        for(unsigned char c:name)if(c!=' '&&c!='-'&&!isalpha(c)&&c<0x80){name.clear();break;}
    }
    if(nexus->IsGameplay&&!name.empty())AlterEgo::GW2API::SetCurrentAccountFromCharacter(name);
    const auto address=AlterEgo::NativeAccounts::ResolveObservedCharacter(name);
    {
        std::lock_guard<std::mutex> lock(A::Mutex());
        A::State().ObserveBound(address,spec,nexus->IsGameplay,A::Now());
    }
    const auto active=A::ActiveKey();
    // The live identity remains useful without an API key, but unverified
    // ownership must never create account-scoped login history.
    static auto lastIdentity=std::chrono::steady_clock::time_point{};
    if(nexus->IsGameplay&&!name.empty()){
        if(g_CurrentCharName!=name){g_CurrentCharName=name;g_LiveSpecialization=spec;}
        else if(spec)g_LiveSpecialization=spec;
        lastIdentity=now;
    }else if(active.first.empty()&&now-lastIdentity>=std::chrono::seconds(12)){
        g_CurrentCharName.clear();g_LiveSpecialization=0;
    }
    MigrateLegacyLoginHistory();
    SyncLoginHistory();
    SaveCharacterActivity();
}
static std::optional<AlterEgo::CharacterIdentity::Address> DueCharacterAddress(const std::string& account,const std::string& name,const AlterEgo::CharacterScopes::Stamps& stamps,AlterEgo::CharacterScopes::Mask demand){
    namespace A=AlterEgo::CharacterActivity;
    namespace S=AlterEgo::CharacterScopes;
    if(account.empty()||name.empty()||!AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Characters)||
       AlterEgo::GW2API::GetAccountStatus()!=AlterEgo::AccountStatus::Ready||
       AlterEgo::GW2API::GetFetchStatus()==AlterEgo::FetchStatus::InProgress)return std::nullopt;
    const auto access=AlterEgo::NativeAccounts::InspectCharacterAccess(account,name);
    if(!access.linked||!access.verified||AlterEgo::NativeAccounts::VerifiedAccountForCharacter(name)!=account)return std::nullopt;
    const auto address=AlterEgo::NativeAccounts::ResolveCharacterAddress(account,name);
    if(!address)return std::nullopt;
    std::lock_guard<std::mutex> lock(A::Mutex());
    if(!S::Due(A::State(),{account,name},stamps,demand&S::Permitted(access.characters,access.builds,access.inventories),
        A::Now(),g_AutoRefreshOnSpecialization))return std::nullopt;
    return address;
}

struct TabDropdownState {
    bool pinned = false;
    bool popupOpen = false;
    bool suppressHoverUntilLeave = false;
    bool requestOpen = false;
    bool requestClose = false;
    bool labelClicked = false;
    bool headerHovered = false;
    float hoverSeconds = 0.0f;
    float outsideSeconds = 0.0f;
    ImVec2 anchor{};
};

// This is the Library tab's proven interaction model, shared unchanged by
// the Achievements, Vault and Clears menus. The tab's label navigates; its
// chevron pins or unpins; hovering opens temporarily and dismisses on leave.
static void HandleTabDropdown(TabDropdownState& state, ImGuiTabBar* tabBar,
                              ImGuiID previousTab, ImGuiID currentTab,
                              ImVec2 tabMin, ImVec2 tabMax) {
    state.requestOpen = false;
    state.requestClose = false;
    state.labelClicked = false;
    state.anchor = ImVec2(tabMin.x, tabMax.y);

    const ImVec2 arrow(tabMax.x - 13.0f, (tabMin.y + tabMax.y) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddLine(ImVec2(arrow.x - 4.0f, arrow.y - 2.0f),
                  ImVec2(arrow.x, arrow.y + 2.0f), color, 1.6f);
    draw->AddLine(ImVec2(arrow.x, arrow.y + 2.0f),
                  ImVec2(arrow.x + 4.0f, arrow.y - 2.0f), color, 1.6f);

    if(g_LibEditMode){state.requestClose=state.popupOpen;return;}
    // IsItemHovered is blocked by active popups, especially for the active tab.
    state.headerHovered = ImGui::IsMouseHoveringRect(tabMin, tabMax, false);
    const bool clicked = state.headerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (!state.headerHovered) {
        state.hoverSeconds = 0.0f;
        state.suppressHoverUntilLeave = false;
    }
    if (clicked) {
        state.hoverSeconds = 0.0f;
        state.suppressHoverUntilLeave = true;
        if (ImGui::GetIO().MousePos.x >= tabMax.x - 26.0f) {
            if (state.pinned && state.popupOpen)
                state.requestClose = true;
            else {
                state.pinned = true;
                state.requestOpen = true;
                state.outsideSeconds = 0.0f;
            }
            // BeginTabItem may have queued a selection; a chevron never navigates.
            if (tabBar && previousTab && previousTab != currentTab)
                tabBar->NextSelectedTabId = previousTab;
        } else {
            state.labelClicked = true;
            if (tabBar) tabBar->NextSelectedTabId = currentTab;
        }
    } else if (state.headerHovered && !state.suppressHoverUntilLeave &&
               !state.popupOpen) {
        state.hoverSeconds += ImGui::GetIO().DeltaTime;
        if (state.hoverSeconds >= 1.0f) {
            state.hoverSeconds = 0.0f;
            state.pinned = false;
            state.outsideSeconds = 0.0f;
            state.requestOpen = true;
        }
    }
}

static bool TabDropdownMenuHovered() {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    return ImGui::IsMouseHoveringRect(pos,
        ImVec2(pos.x + size.x, pos.y + size.y), false);
}

static void FinishTabDropdown(TabDropdownState& state, bool actionSelected,
                              bool mouseOverMenu, bool mouseOverSubmenu = false) {
    if (state.labelClicked || state.requestClose || actionSelected) {
        state.pinned = false;
        state.outsideSeconds = 0.0f;
        ImGui::CloseCurrentPopup();
    } else if (!state.pinned) {
        // Same 250ms bridge as Library, also protecting nested flyouts.
        if (state.headerHovered || mouseOverMenu || mouseOverSubmenu) {
            state.outsideSeconds = 0.0f;
        } else {
            state.outsideSeconds += ImGui::GetIO().DeltaTime;
            if (state.outsideSeconds >= 0.25f) {
                state.outsideSeconds = 0.0f;
                state.suppressHoverUntilLeave = true;
                ImGui::CloseCurrentPopup();
            }
        }
    }
}

static void SyncTabDropdownOpen(TabDropdownState& state, const char* popup) {
    state.popupOpen = ImGui::IsPopupOpen(popup);
    if (!state.popupOpen) {
        state.pinned = false;
        state.outsideSeconds = 0.0f;
    }
}

#include "NavigationUI.h"

void AddonRender() {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    g_AchBrowserRendered=false;
    struct AchievementDemandFrameEnd{
        ~AchievementDemandFrameEnd(){
            if(!g_AchBrowserRendered){
                if(g_AchCategoryDemand){++g_AchDemandRevision;g_AchCategoryDemand.reset();}
                g_AchCatFetching=false;
            }
        }
    } achievementDemandFrameEnd;
    // Track actual rendered demand, including hidden-window early returns.
    // Ending demand only revokes manual retry permission; no retrieval here.
    struct VaultDemandFrame {
        bool visible=false;
        ~VaultDemandFrame(){
            static bool wasVisible=false;
            if(wasVisible&&!visible)EndVaultViewDemand();
            wasVisible=visible;
        }
    } vaultDemand;
    const auto toggles=g_UiToggles.Take();
    if(toggles&1){
        g_WindowVisible=!g_WindowVisible;
        if(APIDefs)APIDefs->Log(LOGL_INFO,"AlterEgo",g_WindowVisible?"Window shown":"Window hidden");
    }
    if(toggles&2){g_AchPopoutVisible=!g_AchPopoutVisible;SaveAchTrackerState();}
    DefaultFontScope _fontScope(g_FontConfig);
    // Process icon download queue every frame
    AlterEgo::IconManager::Tick();
    Skinventory::WikiImage::Tick();
    Skinventory::OwnedSkins::Tick();
    // Normalize legacy saved account names to the registry GUID. The active
    // character and the selected browsing account are independent.
    if(AlterEgo::NativeAccounts::Ready()){
        const auto accounts=AlterEgo::NativeAccounts::Linked();
        if(accounts.size()<=1)g_SelectedAccountFilter.clear();
        else{
            for(const auto& a:accounts)if(g_SelectedAccountFilter==a.name){g_SelectedAccountFilter=a.id;break;}
            if(!g_SelectedAccountFilter.empty()&&std::none_of(accounts.begin(),accounts.end(),[](const auto& a){return a.id==g_SelectedAccountFilter;}))g_SelectedAccountFilter.clear();
            static size_t priorCount=0;
            if(priorCount<2&&g_SelectedAccountFilter.empty())g_SelectedAccountFilter=AlterEgo::GW2API::GetCurrentAccountName();
            priorCount=accounts.size();
        }
    }
    ObserveCharacterActivity(); // local only, no character HTTP while hidden
    for(const auto& notice:AlterEgo::NativeAccounts::TakeRenameNotices())
        if(APIDefs&&notice.before!=notice.after){
            const auto message="Character renamed: "+notice.before+" -> "+notice.after+". Preferences preserved.";
            APIDefs->GUI_SendAlert(message.c_str());
        }
    // No host callback or inbox lock is held while decoding, fetching or drawing.
    for(const auto& input:g_ChatBuildInbox.Take())HandleChatBuildInput(input);
    // Explicit Pie clicks take precedence over automatic notifications.
    for(const auto& input:g_PieBuildInbox.Take())HandlePieBuildInput(input);
    PollAchievementCachePersistence(); // load/save disk progress, even when window is hidden
    SyncCompletionAccountCaches(); // selected-account Clears/Vault snapshots, even with UI hidden
    Skinventory::OwnedSkins::SetAccountName(GetEffectiveAccountName());

    // native API can drop a progress response (or an addon can be reloaded during a
    // query). Never leave the Achievements Refresh action stuck indefinitely.
    if (g_AchProgressFetching) {
        const long long started = g_AchProgressQueryStartMs.load();
        if (started > 0 && NowMs() - started > 120000) {
            std::lock_guard<std::recursive_mutex> lock(g_AchMutex);
            if(g_AchProgressFetching&&g_AchProgressQueryStartMs==started){
                ++g_AchRequestGeneration;
                g_AchProgressFetching = false;
                g_AchProgressQueryStartMs = 0;
                g_AchStatusMsg = "Achievement progress timed out. Click Refresh to retry.";
            }
        }
    }

    ThemeGuard themeGuard;

    // Render gear customize dialog (separate window, always checked)
    RenderGearCustomizeDialog();
    RenderNewBuildDialog();
    RenderSpecPickerDialog();
    RenderSaveToLibraryDialog();
    RenderFashionDialog();

    // Render chat build detection toast (always visible, even when main window is hidden)
    RenderBuildToast();

    // Achievement tracker popout (independent of main window)
    RenderAchPopout();

    g_NavInputMask.store(0);
    if (!g_WindowVisible) { CancelFastLoadUI();g_NavRequest.store(0); return; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Alter Ego", &g_WindowVisible,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse)) {
        CancelFastLoadUI();ImGui::End();
        return;
    }

    // native API status
    auto hoardStatus = AlterEgo::GW2API::GetAccountStatus();
    auto fetchStatus = AlterEgo::GW2API::GetFetchStatus();
    bool scanning = (fetchStatus == AlterEgo::FetchStatus::InProgress);
    bool hoardReady = (hoardStatus == AlterEgo::AccountStatus::Ready);

    // No separate addon is needed for character and inventory retrieval.
    if(!AlterEgo::NativeAccounts::Ready())RenderSpinner("Loading linked accounts...");
    else if(!AlterEgo::NativeAccounts::HasLinkedAccounts())
        ImGui::TextWrapped("Link your GW2 API key in Nexus > Alter Ego options. Saved Library data remains available.");

    // Global account scope, shown only for multiple linked accounts.
    static bool accountFallbackNavigation=false;
    RenderNavigationControls();
    BeginNavigationFrame();
        // Account selector shares the navigation header, never overlays main tabs.
        if (AlterEgo::GW2API::IsMultiAccount()) {
            const auto& accounts = AlterEgo::GW2API::GetAccounts();
            std::string currentLabel = "All Accounts";
            for (const auto& acct : accounts) {
                if (acct.name == g_SelectedAccountFilter) {
                    currentLabel = acct.display_name;
                    break;
                }
            }
            const float available=ImGui::GetContentRegionAvail().x;
            if(available<ImGui::GetFontSize()*9.f)ImGui::NewLine();
            const float comboWidth=std::min(180.f,std::max(1.f,ImGui::GetContentRegionAvail().x));
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowContentRegionMax().x-comboWidth));
            if (RenderThemedCombo("##AccountSelect", currentLabel.c_str(), comboWidth)) {
                std::string prevFilter = g_SelectedAccountFilter;
                if (AlterEgo::UI::SelectableOption("All Accounts", g_SelectedAccountFilter.empty())) {
                    g_SelectedAccountFilter.clear();
                    if ((g_MainTab >= 2 && g_MainTab <= 4) || g_MainTab == 6) {
                        g_ForceCharactersTab = true;accountFallbackNavigation=true;
                    }
                }
                for (const auto& acct : accounts) {
                    bool selected = (g_SelectedAccountFilter == acct.name);
                    ImGui::PushID(acct.name.c_str());
                    if (AlterEgo::UI::EntityOption(acct.display_name.c_str(), selected,[&]{CopyNameMenuItem(acct.display_name,acct.label.empty()?"Copy Account ID":"Copy Account Label",true);})) g_SelectedAccountFilter = acct.name;
                    ImGui::PopID();
                }
                if (g_SelectedAccountFilter != prevFilter && !g_SelectedAccountFilter.empty()) {
                    Skinventory::OwnedSkins::SetAccountName(g_SelectedAccountFilter);
                    g_ClearsNeedRequery = true;
                    g_VaultNeedRequery = true;
                    g_AchNeedRequery = true;
                }
                ImGui::EndCombo();
            }else if(!g_SelectedAccountFilter.empty()){
                ImGui::PushID(g_SelectedAccountFilter.c_str());AlterEgo::UI::EntityContext("##account_copy",[&]{for(const auto& acct:accounts)if(acct.name==g_SelectedAccountFilter){CopyNameMenuItem(acct.display_name,acct.label.empty()?"Copy Account ID":"Copy Account Label",true);break;}});ImGui::PopID();
            }
        }


    // Top-level tab bar: Characters | Build Library
    PollBuildSource();
    // The main tab layout is calculated by the first submitted tab. Add
    // six pixels to its gaps (three on either side of each group divider).
    const auto mainInnerSpacing=ImGui::GetStyle().ItemInnerSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,ImVec2(mainInnerSpacing.x+6.f,mainInnerSpacing.y));
    const bool mainTabBarOpen=ImGui::BeginTabBar("##main_tabs");
    if(!mainTabBarOpen)ImGui::PopStyleVar();
    if (mainTabBarOpen) {
        static TabDropdownState libraryMenu, vaultMenu, clearsMenu, achievementsMenu, skinMenu, inventoryMenu;

        ImGuiTabItemFlags charTabFlags = NavigationTabFlags(0);
        if (g_ForceCharactersTab) {
            charTabFlags = ImGuiTabItemFlags_SetSelected;
            g_ForceCharactersTab = false;
        }
        const bool charactersTabVisible = ImGui::BeginTabItem("Characters", nullptr, charTabFlags);
        ImGui::PopStyleVar(); // content and nested tabs keep their existing spacing
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Live and cached character data from your GW2 account");
        if (charactersTabVisible) {
            g_MainTab = 0;
            const std::string memoryAccount=AlterEgo::GW2API::GetCurrentAccountName();
            TickCharacterLoad();
            ImGui::BeginChild("##characters_page",
                ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);

            // Re-trigger detail fetch when character data finishes loading
            {
                static AlterEgo::FetchStatus lastFetchStatus = AlterEgo::FetchStatus::Idle;
                if (fetchStatus == AlterEgo::FetchStatus::Success && lastFetchStatus != AlterEgo::FetchStatus::Success) {
                    g_DetailsFetched = false;
                }
                lastFetchStatus = fetchStatus;
            }

            const auto charactersSnapshot = AlterEgo::GW2API::GetCharacters();
            const auto& characters = *charactersSnapshot;
            // Also check pending names from a list-only fetch (new user, no full data yet)
            const auto pendingNames = characters.empty()?OrderedCharacterNames(AlterEgo::GW2API::GetPendingCharNames()):AlterEgo::GW2API::GetPendingCharNames();

            // Auto-fetch character list when native API first becomes ready and we have no data
            // Wait for accounts data so we get characters from ALL accounts
            if (AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::Characters) &&
                !g_AutoCharListRequested && hoardReady && !scanning &&
                characters.empty() && pendingNames.empty() &&
                AlterEgo::GW2API::HasAccountsData()) {
                g_AutoCharListRequested = true;
                AlterEgo::GW2API::RequestCharacterList();
            }

            if (!characters.empty()) {
                EnsureCharDisplayOrder(charactersSnapshot);

                if(SelectedCharacterDisplayIndex()<0&&
                    (g_SelectedCharacter.Empty()||g_SelectedCharacter.preview||SelectionRetired(g_SelectedCharacter)||
                     (AlterEgo::NativeAccounts::Ready()&&!g_SelectedCharacter.ref.account.empty()&&
                      !AlterEgo::NativeAccounts::CharacterAccountLinked(g_SelectedCharacter.ref.account)))){
                    const int chosen=g_CharDisplayOrder.empty()?-1:0;
                    g_SelectedBuildTab=-1;SelectCharacterAt(chosen);g_DetailsFetched=false;
                }
                // Character list (left) + Detail panel (right)
                // Clamp width to reasonable bounds
                const AlterEgo::UI::SplitPanel split(g_CharListWidth,200.f);

                ImGui::BeginChild("CharList", ImVec2(split.left, 0), true);

                // Sort mode selector + direction toggle (inside the list child)
                {
                    float arrowBtnWidth = (g_CharSortMode != Sort_Custom) ? 24.0f : 0.0f;
                    float avail = ImGui::GetContentRegionAvail().x;
                    float comboWidth = avail - arrowBtnWidth - (arrowBtnWidth > 0 ? 4.0f : 0.0f);
                    ImGui::SetNextItemWidth(comboWidth);
                    const char* sortLabels[] = { "Custom", "Name", "Class", "Level", "Age", "Birthday", "Last Login" };
                    if (AlterEgo::UI::Combo("##sort", &g_CharSortMode, sortLabels, IM_ARRAYSIZE(sortLabels))) {
                        g_CharSortAscending = true; // reset direction on mode change
                        RebuildCharDisplayOrder(charactersSnapshot);
                        SaveCharSortConfig();
                    }
                    if (g_CharSortMode != Sort_Custom) {
                        ImGui::SameLine();
                        if (AlterEgo::UI::SortDirectionButton("##sortdir",g_CharSortAscending,arrowBtnWidth)) {
                            RebuildCharDisplayOrder(charactersSnapshot);
                            SaveCharSortConfig();
                        }

                    }
                    ImGui::Separator();
                }

                // Character search bar
                {
                    AlterEgo::UI::SearchField("characters", "Search characters...", g_CharSearchBuf);
                }

                // Collect item rects for insertion-line drag-and-drop
                struct CharItemRect { float yMin, yMax; int di; std::string acctName; };
                std::vector<CharItemRect> itemRects;
                itemRects.reserve(g_CharDisplayOrder.size());

                // Cache which character is currently being fetched
                std::string fetchingCharName = AlterEgo::GW2API::GetCurrentFetchCharName();

                // Prepare lowercase search string
                const auto charSearchWords=AlterEgo::Search::Words(g_CharSearchBuf);

                // Build account display name lookup for headers
                bool showAcctHeaders = AlterEgo::GW2API::IsMultiAccount();
                std::unordered_map<std::string, std::string> acctDisplayNames;
                if (showAcctHeaders) {
                    for (const auto& acct : AlterEgo::GW2API::GetAccounts())
                        acctDisplayNames[acct.name] = acct.display_name;
                }
                std::string lastAcctHeader;
                bool seenKnownLogin=false,shownUnknownLogin=false;

                for (int di = 0; di < (int)g_CharDisplayOrder.size(); di++) {
                    int realIdx = g_CharDisplayOrder[di];
                    if (realIdx < 0 || realIdx >= (int)characters.size()) continue;
                    const auto& ch = characters[realIdx];

                    // Apply account filter
                    if (!g_SelectedAccountFilter.empty() && ch.account_name != g_SelectedAccountFilter)
                        continue;

                    // Apply search filter
                    if(!AlterEgo::Search::MatchesFields(charSearchWords,{ch.name,ch.profession}))continue;

                    // Account group header
                    if (showAcctHeaders && ch.account_name != lastAcctHeader) {
                        lastAcctHeader = ch.account_name;
                        seenKnownLogin=false;shownUnknownLogin=false;
                        auto it = acctDisplayNames.find(ch.account_name);
                        const char* label = it != acctDisplayNames.end() ? it->second.c_str() : ch.account_name.c_str();
                        if (di > 0) ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "%s", label);
                        ImGui::Separator();
                    }

                    const auto lastLogin=CharacterLastLogin(ch);
                    if(g_CharSortMode==Sort_LastLogin){
                        if(lastLogin>0)seenKnownLogin=true;
                        else if(seenKnownLogin&&!shownUnknownLogin){
                            ImGui::Spacing();ImGui::Separator();ImGui::TextDisabled("No recorded login");
                            shownUnknownLogin=true;
                        }
                    }
                    const auto rowIdentity=ch.account_name+"\x1f"+ch.name;
                    ImGui::PushID(rowIdentity.c_str());

                    bool selected=MatchesSelection(g_SelectedCharacter,ch);
                    bool isLoggedIn = (!g_CurrentCharName.empty() && ch.name == g_CurrentCharName);
                    ImVec4 profColor = GetProfessionColor(ch.profession);

                    // Count extra lines for dynamic row height
                    int extraLines = 0;
                    if (!g_CompactCharList) {
                        if (g_ShowCraftingIcons && !ch.crafting.empty()) extraLines++;
                        if (g_ShowAge && !ch.created.empty()) extraLines++;
                        if (g_ShowPlaytime && ch.age > 0) extraLines++;
                        if (g_ShowLastLogin && lastLogin>0) extraLines++;
                        if (g_BirthdayMode != 2) {
                            int bdays = DaysUntilBirthday(ch.created);
                            bool showBday = (g_BirthdayMode == 0 && bdays >= 0) ||
                                            (g_BirthdayMode == 1 && bdays >= 0 && bdays <= 7);
                            if (showBday) extraLines++;
                        }
                    }
                    float lineH = ImGui::GetTextLineHeightWithSpacing();
                    float rowHeight = g_CompactCharList ? std::max(18.0f,lineH)
                        : (28.0f + extraLines * lineH);
                    // Show mismatch even when no library build happens to match.
                    const bool recognizeActive=!memoryAccount.empty()&&ch.account_name==memoryAccount&&ch.name==g_CurrentCharName;
                    if (recognizeActive) {
                        const auto& match = GetLoggedInSavedBuildMatch(ch);
                        if (match.show)rowHeight+=(1.f+static_cast<float>(match.warnings.size()))*lineH;
                        if (HasLiveSpecCacheMismatch(ch))
                            rowHeight += lineH;
                    }

                    // Profession-tinted selection highlight
                    ImVec4 selCol(profColor.x * 0.35f, profColor.y * 0.35f, profColor.z * 0.35f, 0.65f);
                    ImVec4 hovCol(profColor.x * 0.25f, profColor.y * 0.25f, profColor.z * 0.25f, 0.50f);
                    ImVec4 actCol(profColor.x * 0.40f, profColor.y * 0.40f, profColor.z * 0.40f, 0.75f);
                    ImGui::PushStyleColor(ImGuiCol_Header, selCol);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovCol);
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, actCol);

                    // Character entry
                    if (ImGui::Selectable("##char", selected, 0, ImVec2(0, rowHeight))) {
                        MarkNavigation();
                        SelectCharacterAt(di);
                        g_DetailsFetched = false;
                        g_SelectedEquipTab = 0;
                        g_SelectedBuildTab = -1;
                        // Refresh portrait detection for this character
                        if(const auto stem=AlterEgo::Storage::PortraitStem(ch.name);!stem.empty()){
                            s_portraitPathCache.erase(stem);s_portraitMissing.erase(stem);
                        }
                    }

                    // Context actions resolve against the right-clicked character, not
                    // the character whose details happen to be selected.
                    const bool characterHovered=ImGui::IsItemHovered();
                    if (ImGui::BeginPopupContextItem("##character_actions")) {
                        CopyNameMenuItem(ch.name);
                        RenderCharacterLoadMenu("Load Character",ch.name,ch.account_name);
                        if(const auto* loadout=CharacterContextLoadout(ch)){
                            ImGui::Separator();RenderCharacterLoadoutContext(ch,loadout,false);ImGui::Separator();
                        }
                        const int activeBuild = ActiveBuildIndex(ch);
                        const int activeEquip = ActiveEquipmentTab(ch);
                        const auto refreshAddress=CharacterAddressFor(ch);
                        const bool refreshBusy=AlterEgo::GW2API::GetFetchStatus()==AlterEgo::FetchStatus::InProgress;
                        const bool refreshReady=AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready;
                        if(AlterEgo::UI::MenuAction("Refresh Character",AlterEgo::UI::MenuIcon::Regenerate,
                            refreshReady&&!refreshBusy&&bool(refreshAddress),
                            !refreshReady?"Link or verify an API account in Nexus options.":
                            refreshBusy?"A character or roster request is already in progress.":"Character identity is not verified.",
                            "Retrieve the latest details, builds, equipment and crafting data for this character.")){
                            AlterEgo::GW2API::RequestCharacterRefreshAddresses({*refreshAddress});
                            if(MatchesSelection(g_SelectedCharacter,ch))g_DetailsFetched=false;
                        }
                        if (ImGui::MenuItem("Save to Library...", nullptr, false, activeBuild>=0||!ch.equipment.empty()))
                            OpenSaveLiveBuildDialog(ch,activeBuild>=0?ch.build_tabs[activeBuild]:AlterEgo::BuildTemplate{},
                                activeEquip,activeBuild>=0?-1:kSaveEntireEquipmentSet);
                        if (ImGui::BeginMenu("Share", activeBuild >= 0)) {
                            static AlterEgo::SavedBuild contextSnapshot;
                            static std::string contextKey;
                            const std::string key = ch.name + "#" +
                                std::to_string(activeBuild) + "#" + std::to_string(activeEquip);
                            if (ImGui::IsWindowAppearing() || contextKey != key) {
                                contextSnapshot = MakeLiveShareSnapshot(ch, activeBuild, activeEquip);
                                contextKey = key;
                            }
                            RenderBuildShareMenuItems(contextSnapshot, &ch);
                            ImGui::EndMenu();
                        }
                        ImGui::EndPopup();
                    }

                    if (characterHovered && !ImGui::IsPopupOpen("##character_actions")) {
                        // The sampled roster includes inactive same-account characters.
                        const std::string location=AlterEgo::GameMemory::Location(g_CurrentCharName,ch.name,memoryAccount,ch.account_name);
                        if (!location.empty()) AlterEgo::UI::Hint(("Location: "+location).c_str());
                    }

                    // Draw profession-colored left accent bar (non-compact only)
                    if (!g_CompactCharList) {
                        ImVec2 rMin = ImGui::GetItemRectMin();
                        ImVec2 rMax = ImGui::GetItemRectMax();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float barW = 3.0f;
                        ImU32 barCol = ImGui::ColorConvertFloat4ToU32(
                            ImVec4(profColor.x, profColor.y, profColor.z,
                                   selected ? 1.0f : (ImGui::IsItemHovered() ? 0.7f : 0.35f)));
                        dl->AddRectFilled(
                            ImVec2(rMin.x, rMin.y + 1), ImVec2(rMin.x + barW, rMax.y - 1),
                            barCol, 1.0f);
                        // Subtle glow on selected items
                        if (selected) {
                            ImU32 glowCol = ImGui::ColorConvertFloat4ToU32(
                                ImVec4(profColor.x, profColor.y, profColor.z, 0.08f));
                            dl->AddRectFilledMultiColor(
                                ImVec2(rMin.x + barW, rMin.y),
                                ImVec2(rMin.x + barW + 30.0f, rMax.y),
                                glowCol, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), glowCol);
                        }
                    }

                    // Record item rect for insertion line calculation
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();

                    itemRects.push_back({ rMin.y, rMax.y, di, ch.account_name });

                    // Drag source for Custom sort mode
                    if (g_CharSortMode == Sort_Custom) {
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            g_CharDragSelection=SelectCharacterIdentity(ch);
                            ImGui::SetDragDropPayload("CHAR_REORDER",rowIdentity.c_str(),rowIdentity.size()+1);
                            RenderProfessionCharacterName(ch.name, ch.profession);
                            ImGui::EndDragDropSource();
                        }
                    }

                    // Draw character details on top of the selectable, with an
                    // inset that keeps the profession icon clear of the accent.
                    // SetCursorScreenPos jumps back to the top of the card,
                    // so save the original layout position for the next row.
                    const ImVec2 nextCharacterPos = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(rMin.x + 8.0f, rMin.y));
                    ImGui::BeginGroup();
                    if (!g_CompactCharList) {
                        const float iconSz = 20.0f;
                        uint32_t profIconId = GetProfessionIconId(ch.profession);
                        const char* profIconUrl = GetProfessionIconUrl(ch.profession);
                        ImVec2 cpos = ImGui::GetCursorPos();
                        float yOff = (extraLines > 0 || recognizeActive) ? 2.0f : (rowHeight - iconSz) * 0.5f;
                        if (profIconId) {
                            if (!profIconUrl)
                                AlterEgo::GW2API::FetchProfessionInfoAsync(ch.profession);
                            auto* tex = AlterEgo::IconManager::GetIcon(profIconId);
                            ImGui::SetCursorPos(ImVec2(cpos.x, cpos.y + yOff));
                            if (tex && tex->Resource) {
                                ImGui::Image(tex->Resource, ImVec2(iconSz, iconSz));
                            } else {
                                AlterEgo::IconManager::RequestIcon(profIconId,
                                    profIconUrl ? profIconUrl : "");
                                ImGui::Dummy(ImVec2(iconSz, iconSz));
                            }
                        } else {
                            ImGui::SetCursorPos(ImVec2(cpos.x, cpos.y + yOff));
                            ImGui::Dummy(ImVec2(iconSz, iconSz));
                        }
                        ImGui::SameLine(0, 4);
                        float textYOff = (extraLines > 0 || recognizeActive) ? 2.0f : (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                        ImGui::SetCursorPosY(cpos.y + textYOff);
                    }
                    // First line: name + level + green dot
                    RenderProfessionCharacterName(ch.name, ch.profession);
                    ImGui::SameLine();
                    ImGui::TextColored(selected ? ImVec4(0.75f, 0.75f, 0.75f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Lv%d", ch.level);
                    if (isLoggedIn) {
                        ImGui::SameLine();
                        float dotY = ImGui::GetCursorScreenPos().y - 3.0f + ImGui::GetTextLineHeight() * 0.5f;
                        float dotX = ImGui::GetCursorScreenPos().x;
                        AlterEgo::UI::ActiveIndicator(ImGui::GetWindowDrawList(),
                            ImVec2(dotX + AlterEgo::UI::ActiveRadius, dotY));
                        ImGui::Dummy(ImVec2(8, 0));
                    }
                    // Spinning refresh icon for the character currently being fetched
                    if (!fetchingCharName.empty() && ch.name == fetchingCharName) {
                        ImGui::SameLine();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float iconR = 5.0f;
                        ImVec2 scrPos = ImGui::GetCursorScreenPos();
                        float cx = scrPos.x + iconR + 1.0f;
                        float cy = scrPos.y + ImGui::GetTextLineHeight() * 0.5f;
                        float angle = (float)ImGui::GetTime() * 3.0f; // spin speed
                        ImU32 col = IM_COL32(255, 220, 60, 220);
                        // Draw two arcs with arrowheads
                        for (int arc = 0; arc < 2; arc++) {
                            float arcStart = angle + arc * 3.14159f;
                            int segments = 8;
                            float arcSpan = 2.4f; // ~137 degrees per arc
                            for (int s = 0; s < segments; s++) {
                                float a0 = arcStart + (arcSpan * s / segments);
                                float a1 = arcStart + (arcSpan * (s + 1) / segments);
                                dl->AddLine(
                                    ImVec2(cx + cosf(a0) * iconR, cy + sinf(a0) * iconR),
                                    ImVec2(cx + cosf(a1) * iconR, cy + sinf(a1) * iconR),
                                    col, 1.5f);
                            }
                            // Arrowhead at end of arc
                            float aEnd = arcStart + arcSpan;
                            float tx = cosf(aEnd); // tangent direction (perpendicular to radius)
                            float ty = sinf(aEnd);
                            ImVec2 tip(cx + tx * iconR, cy + ty * iconR);
                            // Arrow points along the arc direction (tangent)
                            float tanX = -ty; // tangent = perpendicular to radial
                            float tanY = tx;
                            float arrSz = 3.0f;
                            // Two sides of the arrowhead
                            ImVec2 p1(tip.x - tanX * arrSz + tx * arrSz * 0.5f,
                                      tip.y - tanY * arrSz + ty * arrSz * 0.5f);
                            ImVec2 p2(tip.x - tanX * arrSz - tx * arrSz * 0.5f,
                                      tip.y - tanY * arrSz - ty * arrSz * 0.5f);
                            dl->AddTriangleFilled(tip, p1, p2, col);
                        }
                        ImGui::Dummy(ImVec2(iconR * 2.0f + 2.0f, 0));
                    }
                    if (recognizeActive) {
                        if (HasLiveSpecCacheMismatch(ch)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.34f, 1.0f),
                                "Live spec differs - refresh");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("MumbleLink reports %s, cached active build reports %s.",
                                    GetSpecNameForWarning(g_LiveSpecialization),
                                    GetSpecNameForWarning(ch.build_tabs[GetActiveBuildIndex(ch)].specializations[2].spec_id));
                        }
                        if(recognizeActive){
                            const auto& match=GetLoggedInSavedBuildMatch(ch);
                            if(match.show){
                                const auto color=match.full?ImVec4(.55f,.88f,.58f,1.f):
                                    match.id.empty()?ImVec4(.65f,.65f,.65f,1.f):
                                    AlterEgo::UI::WarningText;
                                const auto draw=[&](const std::string& label,ImVec4 tint){
                                    const float width=ImMax(0.f,rMax.x-ImGui::GetCursorScreenPos().x-6.f);
                                    ImGui::TextColored(tint,"%s",width>=ImGui::CalcTextSize("...").x?TruncateToWidth(label,width).c_str():label.c_str());
                                    if(ImGui::IsItemHovered())AlterEgo::UI::DetailTooltip("Configuration health",match.tooltip.c_str());
                                };
                                draw(match.full?"Matches - "+match.name:match.name,color);
                                for(const auto& warning:match.warnings)draw(warning,color);
                            }
                        }
                    }
                    // Extra lines (each on its own line with prefix)
                    if (!g_CompactCharList) {
                        ImGui::Indent(6.0f);
                        ImVec4 dimCol = selected ? ImVec4(0.8f, 0.8f, 0.8f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                        ImVec4 labelCol = selected ? ImVec4(0.7f, 0.7f, 0.7f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f);

                        // 1. Crafting icons (no prefix, inactive dimmed)
                        if (g_ShowCraftingIcons && !ch.crafting.empty()) {
                            const float craftIconSz = 14.0f;
                            for (size_t ci = 0; ci < ch.crafting.size(); ci++) {
                                if (ci > 0) ImGui::SameLine(0, 2);
                                const std::string& disc = ch.crafting[ci];
                                bool isActive = (ci < ch.crafting_active.size()) ? ch.crafting_active[ci] : true;
                                float iconAlpha = isActive ? 1.0f : 0.35f;
                                uint32_t cIconId = GetCraftingIconId(disc);
                                const char* cIconUrl = GetCraftingIconUrl(disc);
                                bool rendered = false;
                                if (cIconId && cIconUrl) {
                                    auto* tex = AlterEgo::IconManager::GetIcon(cIconId);
                                    if (tex && tex->Resource) {
                                        ImGui::Image(tex->Resource, ImVec2(craftIconSz, craftIconSz),
                                            ImVec2(0, 0), ImVec2(1, 1),
                                            ImVec4(1, 1, 1, iconAlpha));
                                        rendered = true;
                                    } else {
                                        AlterEgo::IconManager::RequestIcon(cIconId, cIconUrl);
                                    }
                                }
                                if (!rendered) ImGui::Dummy(ImVec2(craftIconSz, craftIconSz));
                                if (ImGui::IsItemHovered()) {
                                    ImGui::BeginTooltip();
                                    int lvl = (ci < ch.crafting_levels.size()) ? ch.crafting_levels[ci] : 0;
                                    ImGui::Text("%s %d%s", disc.c_str(), lvl,
                                        isActive ? "" : " (inactive)");
                                    ImGui::EndTooltip();
                                }
                            }
                        }

                        // 2. Age: (character creation age)
                        if (g_ShowAge && !ch.created.empty()) {
                            struct tm ctm = {};
                            if (sscanf(ch.created.c_str(), "%d-%d-%dT%d:%d:%d",
                                    &ctm.tm_year, &ctm.tm_mon, &ctm.tm_mday,
                                    &ctm.tm_hour, &ctm.tm_min, &ctm.tm_sec) == 6) {
                                ctm.tm_year -= 1900;
                                ctm.tm_mon -= 1;
                                time_t created_t = mktime(&ctm);
                                time_t now_t = std::time(nullptr);
                                int totalDays = (int)(difftime(now_t, created_t) / 86400.0);
                                ImGui::TextColored(labelCol, "Age:");
                                ImGui::SameLine();
                                if (totalDays >= 365)
                                    ImGui::TextColored(dimCol, "%dy %dd", totalDays / 365, totalDays % 365);
                                else
                                    ImGui::TextColored(dimCol, "%dd", totalDays);
                            }
                        }

                        // 3. Next Birthday: (right after Age)
                        if (g_BirthdayMode != 2) {
                            int bdays = DaysUntilBirthday(ch.created);
                            bool showBday = (g_BirthdayMode == 0 && bdays >= 0) ||
                                            (g_BirthdayMode == 1 && bdays >= 0 && bdays <= 7);
                            if (showBday) {
                                ImGui::TextColored(labelCol, "Next Birthday:");
                                ImGui::SameLine();
                                if (bdays == 0)
                                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "Today!");
                                else
                                    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 1.0f), "%dd", bdays);
                            }
                        }

                        // 4. Playtime:
                        if (g_ShowPlaytime && ch.age > 0) {
                            int hours = ch.age / 3600;
                            ImGui::TextColored(labelCol, "Playtime:");
                            ImGui::SameLine();
                            if (hours >= 24)
                                ImGui::TextColored(dimCol, "%dd %dh", hours / 24, hours % 24);
                            else
                                ImGui::TextColored(dimCol, "%dh", hours);
                        }

                        // 5. Last Login (from the shared gameplay activity history):
                        if (g_ShowLastLogin) {
                            if (lastLogin>0) {
                                time_t now_t = std::time(nullptr);
                                int elapsed = (int)difftime(now_t, (time_t)lastLogin);
                                std::string ago;
                                if (elapsed < 60) ago = "Just Now";
                                else if (elapsed < 3600) ago = std::to_string(elapsed / 60) + "m ago";
                                else if (elapsed < 86400) ago = std::to_string(elapsed / 3600) + "h ago";
                                else ago = std::to_string(elapsed / 86400) + "d ago";
                                ImGui::TextColored(labelCol, "Last Login:");
                                ImGui::SameLine();
                                ImGui::TextColored(dimCol, "%s", ago.c_str());
                            }
                        }
                        ImGui::Unindent(6.0f);
                    }
                    ImGui::EndGroup();
                    // Keep the next character below BOTH the selectable and
                    // the overlay content. Without restoring this position,
                    // short overlays cause later entries to overlap vertically.
                    const float overlayNextY = ImGui::GetCursorScreenPos().y;
                    ImGui::SetCursorScreenPos(ImVec2(nextCharacterPos.x,
                        ImMax(nextCharacterPos.y, overlayNextY)));

                    ImGui::PopStyleColor(3);

                    ImGui::PopID();
                }

                // Custom sort: draw insertion line and handle drop
                int dragIndex=-1;
                if(ResolveSelection(g_CharDragSelection))for(int di=0;di<(int)g_CharDisplayOrder.size();++di)
                    if(MatchesSelection(g_CharDragSelection,characters[g_CharDisplayOrder[di]])){dragIndex=di;break;}
                if(g_CharSortMode==Sort_Custom&&dragIndex>=0&&ImGui::GetDragDropPayload()!=nullptr&&
                    ImGui::GetDragDropPayload()->IsDataType("CHAR_REORDER")){
                    const std::string& srcAcct=g_CharDragSelection.ref.account;

                    // Find the range of itemRects indices belonging to the same account
                    int acctFirst = -1, acctLast = -1;
                    for (int i = 0; i < (int)itemRects.size(); i++) {
                        if (itemRects[i].acctName == srcAcct) {
                            if (acctFirst < 0) acctFirst = i;
                            acctLast = i;
                        }
                    }

                    float mouseY = ImGui::GetMousePos().y;
                    int insertIdx = -1;
                    float bestLineY = 0;

                    if (acctFirst >= 0) {
                        // Clamp insertion to within the account group
                        insertIdx = acctLast + 1; // default: end of account group
                        for (int i = acctFirst; i <= acctLast; i++) {
                            float midY = (itemRects[i].yMin + itemRects[i].yMax) * 0.5f;
                            if (mouseY < midY) {
                                insertIdx = i;
                                bestLineY = itemRects[i].yMin;
                                break;
                            }
                        }
                        if (insertIdx == acctLast + 1)
                            bestLineY = itemRects[acctLast].yMax;
                    }

                    // Draw insertion line
                    if (insertIdx >= 0) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float xMin = ImGui::GetWindowPos().x + 2;
                        float xMax = xMin + ImGui::GetWindowContentRegionMax().x - 4;
                        dl->AddLine(ImVec2(xMin, bestLineY), ImVec2(xMax, bestLineY),
                            IM_COL32(100, 180, 255, 220), 2.0f);
                    }

                    // Handle drop
                    if (ImGui::IsMouseReleased(0)) {
                        if (insertIdx >= 0) {
                            // Map itemRects indices back to display-order indices
                            int srcDi=dragIndex;
                            int dstDi = (insertIdx < (int)itemRects.size()) ? itemRects[insertIdx].di
                                : (itemRects[acctLast].di + 1);

                            if (srcDi != dstDi && srcDi >= 0 && srcDi < (int)g_CharDisplayOrder.size()) {
                                int movedIdx = g_CharDisplayOrder[srcDi];
                                g_CharDisplayOrder.erase(g_CharDisplayOrder.begin() + srcDi);
                                int finalPos = (srcDi < dstDi) ? dstDi - 1 : dstDi;
                                if (finalPos > (int)g_CharDisplayOrder.size())
                                    finalPos = (int)g_CharDisplayOrder.size();
                                g_CharDisplayOrder.insert(g_CharDisplayOrder.begin() + finalPos, movedIdx);
                                // Update custom order
                                std::vector<AlterEgo::CharacterIdentity::Address> ordered;
                                for(int idx:g_CharDisplayOrder)if(characters[idx].account_name==srcAcct)
                                    if(const auto address=CharacterAddressFor(characters[idx]))ordered.push_back(*address);
                                if(!AlterEgo::NativeAccounts::SaveCustomOrder(srcAcct,ordered)&&APIDefs)
                                    APIDefs->GUI_SendAlert("Could not save character order.");
                                RebuildCharDisplayOrder(charactersSnapshot);
                            }
                        }
                        g_CharDragSelection={};
                    }
                } else if (ImGui::IsMouseReleased(0)) {
                    g_CharDragSelection={};
                }

                RenderCharacterRosterBlankContext();
                ImGui::EndChild();

                if(split.Divider("##charlist_splitter",g_CharListWidth))SaveSettings();

                // Detail panel — resolve through display order
                const int selRealIdx=SelectedCharacterIndex(characters);

                ImGui::BeginChild("CharDetail", ImVec2(0, 0), true);
                if (selRealIdx >= 0 && selRealIdx < (int)characters.size()) {
                    const auto& ch = characters[selRealIdx];

                    // Fetch details on-demand when character is selected
                    if (!g_DetailsFetched) {
                        FetchDetailsForCharacter(ch);
                        g_DetailsFetched = true;
                    }

                    // Character header — avatar + name + meta (no duplicate emblem at right)
                    {
                        ImVec4 profColor = GetProfessionColor(ch.profession);
                        const float avatarSize = 46.0f;
                        const ImVec2 headerStart = ImGui::GetCursorScreenPos();
                        const float startY = ImGui::GetCursorPosY();
                        const float availW = ImGui::GetContentRegionAvail().x;
                        const float actionWidth = ImMax(ImGui::CalcTextSize("Save").x,ImGui::CalcTextSize("Share").x) +
                            2.0f * ImGui::GetStyle().FramePadding.x + 12.0f;
                        const bool actionsOnRight = availW >= avatarSize + actionWidth + 205.0f;

                        // Avatar (left)
                        ImGui::BeginGroup();
                        RenderCharacterAvatar(ch, avatarSize);
                        ImGui::EndGroup();
                        ImGui::SameLine(0, 10);

                        // Use the available space; the left avatar already
                        // identifies the profession and makes a second emblem redundant.
                        const float middleW = ImMax(1.0f, availW - avatarSize - 18.0f -
                            (actionsOnRight ? actionWidth + 10.0f : 0.0f));
                        ImGui::BeginGroup();
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + middleW);
                        ImGui::SetCursorPosY(startY + 2.0f);
                        RenderProfessionCharacterName(ch.name, ch.profession);
                        ImGui::PushID(ch.account_name.c_str());ImGui::PushID(ch.name.c_str());
                        AlterEgo::UI::EntityContext("##character_header_actions",[&]{
                            CopyNameMenuItem(ch.name);
                            if(FastLoadActionsEnabled()){ImGui::Separator();RenderCharacterLoadMenu("Load Character",ch.name,ch.account_name);}
                        });
                        ImGui::PopID();ImGui::PopID();

                        char meta[160];
                        std::string raceUp = ch.race;
                        std::string profUp = ch.profession;
                        for (auto& c : raceUp) c = (char)toupper((unsigned char)c);
                        for (auto& c : profUp) c = (char)toupper((unsigned char)c);
                        snprintf(meta, sizeof(meta), "LV %d  -  %s  -  %s",
                            ch.level, raceUp.c_str(), profUp.c_str());
                        ImGui::TextColored(ImVec4(0.55f, 0.50f, 0.40f, 1.0f), "%s", meta);
                        RenderCharacterHeaderMetadata(ch, middleW);
                        ImGui::PopTextWrapPos();
                        ImGui::EndGroup();

                        // Position the three actions at the top-right, as marked
                        // in the screenshot. In narrow layouts they fall below
                        // the name instead of overlapping or clipping it.
                        const float textBottom = ImGui::GetCursorScreenPos().y;
                        const float actionX = actionsOnRight
                            ? headerStart.x + availW - actionWidth : headerStart.x;
                        const float actionY = actionsOnRight
                            ? headerStart.y : ImMax(textBottom, headerStart.y + avatarSize) + 4.0f;
                        ImGui::SetCursorScreenPos(ImVec2(actionX, actionY));
                        RenderCharacterHeaderActions(ch, actionsOnRight
                            ? actionWidth : ImMin(actionWidth, availW));
                        const float actionsBottom = ImGui::GetCursorScreenPos().y;
                        ImGui::SetCursorScreenPos(ImVec2(headerStart.x,
                            ImMax(ImMax(textBottom, actionsBottom),
                                  headerStart.y + avatarSize) + 4.0f));
                    }

                    RenderCharacterLoadoutSelector(ch);
                    ImGui::Spacing();
                    ImGui::Separator();

                    // Resolve the requested physical build tab against the
                    // current verified identity, not a stale vector position.
                    int petBuildIndex=-1;
                    if(g_PetBuildNavigation){
                        const auto address=CharacterAddressFor(ch);
                        if(address&&address->Reference()==g_PetBuildNavigation->character&&
                            ch.scope_stamps[AlterEgo::CharacterScopes::Index(AlterEgo::CharacterScopes::Builds)]>0)
                            petBuildIndex=AlterEgo::PetOwnership::TabIndex(ch.build_tabs,g_PetBuildNavigation->tab);
                        if(petBuildIndex<0)g_PetBuildNavigation.reset();
                    }
                    // Tab bar: Equipment | Build | Tools | Ranger Pets
                    if (ImGui::BeginTabBar("##detail_tabs")) {
                        if (ImGui::BeginTabItem("Equipment",nullptr,NavigationDetailFlags(0))) {
                            if(g_SelectedTab!=0)MarkNavigation();g_SelectedTab = 0;
                            ImGui::BeginChild("EquipScroll", ImVec2(0, 0), false);
                            RenderEquipmentPanel(ch);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Build",nullptr,NavigationDetailFlags(1)|(petBuildIndex>=0?ImGuiTabItemFlags_SetSelected:0))) {
                            if(petBuildIndex>=0){
                                MarkNavigation();g_SelectedBuildTab=petBuildIndex;g_PetBuildNavigation.reset();
                            }
                            if(g_SelectedTab!=1)MarkNavigation();g_SelectedTab = 1;
                            ImGui::BeginChild("BuildScroll", ImVec2(0, 0), false);
                            RenderBuildPanel(ch);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Tools",nullptr,NavigationDetailFlags(2))) {
                            if(g_SelectedTab!=2)MarkNavigation();g_SelectedTab=2;
                            ImGui::BeginChild("ToolsScroll", ImVec2(0, 0), false);
                            RenderCharacterToolsPanel(ch);
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if(ch.profession=="Ranger"&&ImGui::BeginTabItem("Pets",nullptr,NavigationDetailFlags(3))){
                            if(g_SelectedTab!=3)MarkNavigation();g_SelectedTab=3;
                            ImGui::BeginChild("PetsScroll",ImVec2(0,0),false);
                            RenderCharacterPetsPanel(ch);
                            ImGui::EndChild();ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                } else {
                    if(!g_SelectedCharacter.Empty())ImGui::TextDisabled("Selected character temporarily unavailable.");
                    else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a character from the list.");
                }
                ImGui::EndChild();
            } else {
                // Empty state: no cached character data yet (new user)
                // Use same width as normal character list panel
                const AlterEgo::UI::SplitPanel split(g_CharListWidth,200.f);

                ImGui::BeginChild("CharListEmpty", ImVec2(split.left, 0), true);

                // Show pending character names grouped by account
                if (!pendingNames.empty() && AlterEgo::GW2API::IsMultiAccount()) {
                    ImGui::Separator();
                    const auto& accounts = AlterEgo::GW2API::GetAccounts();
                    // Sort so current account appears first
                    const std::string& currentAcct = AlterEgo::GW2API::GetCurrentAccountName();
                    std::vector<int> acctOrder(accounts.size());
                    std::iota(acctOrder.begin(), acctOrder.end(), 0);
                    std::stable_partition(acctOrder.begin(), acctOrder.end(),
                        [&](int i) { return accounts[i].name == currentAcct; });
                    for (int ai : acctOrder) {
                        const auto& acct = accounts[ai];
                        if (!g_SelectedAccountFilter.empty() && acct.name != g_SelectedAccountFilter)
                            continue;
                        // Collect chars for this account
                        bool hasChars = false;
                        for (size_t i = 0; i < pendingNames.size(); i++) {
                            if (AlterEgo::GW2API::GetAccountForCharacter(pendingNames[i]) == acct.name) {
                                if (!hasChars) {
                                    hasChars = true;
                                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.5f, 1.0f), "%s", acct.display_name.c_str());
                                }
                                ImGui::PushID((int)i);
                                ImGui::Selectable(pendingNames[i].c_str(), false, 0, ImVec2(0, 20.0f));
                                ImGui::PopID();
                            }
                        }
                    }
                } else if (!pendingNames.empty()) {
                    ImGui::Separator();
                    for (size_t i = 0; i < pendingNames.size(); i++) {
                        ImGui::PushID((int)i);
                        ImGui::Selectable(pendingNames[i].c_str(), false, 0, ImVec2(0, 20.0f));
                        ImGui::PopID();
                    }
                }

                RenderCharacterRosterBlankContext();
                ImGui::EndChild();

                // Detail panel placeholder (right side)
                if(split.Divider("##charlist_empty_splitter",g_CharListWidth))SaveSettings();
                ImGui::BeginChild("CharDetailEmpty", ImVec2(0, 0), true);
                if (!pendingNames.empty()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Click ... to select characters to refresh.");
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Waiting for character list...");
                }
                ImGui::EndChild();
            }

            ImGui::EndChild();
            RenderCharactersStatusRow();
            // Character refresh selection popup: OpenPopup and BeginPopup share
            // the same parent window scope after the content child has ended.
            if (g_RefreshPopupOpen) {
                ImGui::OpenPopup("Select Characters to Refresh");
                // Top-left of the footer button is the bottom-left pivot of
                // the picker; ImGui clamps it to the available viewport.
                ImGui::SetNextWindowPos(g_RefreshPopupAnchor,ImGuiCond_Appearing,ImVec2(0.f,1.f));
                g_RefreshPopupOpen = false;
            }
            if (ImGui::BeginPopup("Select Characters to Refresh")) {
                ImGui::Text("Select characters to refresh:");
                ImGui::Spacing();
                if(g_RefreshNames.empty()){
                    ImGui::TextWrapped("No verified characters are available. Right-click the character list or footer to refresh the character list.");
                    if(RenderActionButton("Close"))ImGui::CloseCurrentPopup();
                }else{

                if (RenderActionButton("All")) {
                    for (size_t i = 0; i < g_RefreshSelection.size(); i++)
                        g_RefreshSelection[i] = true;
                }
                ImGui::SameLine();
                if (RenderActionButton("None")) {
                    for (size_t i = 0; i < g_RefreshSelection.size(); i++)
                        g_RefreshSelection[i] = false;
                }
                ImGui::SameLine();
                if (RenderActionButton("Lv80")) {
                    const auto cachedCharsSnapshot = AlterEgo::GW2API::GetCharacters();
                    const auto& cachedChars = *cachedCharsSnapshot;
                    for (size_t i = 0; i < g_RefreshNames.size(); i++) {
                        g_RefreshSelection[i] = false;
                        for (const auto& cc : cachedChars) {
                            const auto address=CharacterAddressFor(cc);
                            if(address&&AlterEgo::CharacterIdentity::SameAddress(*address,g_RefreshAddresses[i])&&cc.level==80){
                                g_RefreshSelection[i] = true;
                                break;
                            }
                        }
                    }
                }

                ImGui::Separator();

                ImGui::BeginChild("##charCheckList", ImVec2(280, 300), true);
                const auto cachedCharsSnapshot = AlterEgo::GW2API::GetCharacters();
                const auto& cachedChars = *cachedCharsSnapshot;
                for (size_t i = 0; i < g_RefreshNames.size(); i++) {
                    ImGui::PushID((int)i);
                    bool checked = g_RefreshSelection[i];

                    const AlterEgo::Character* cached = nullptr;
                    for (const auto& cc : cachedChars) {
                        const auto address=CharacterAddressFor(cc);
                        if(address&&AlterEgo::CharacterIdentity::SameAddress(*address,g_RefreshAddresses[i])){cached=&cc;break;}
                    }

                    const auto current=AlterEgo::NativeAccounts::ResolveCharacterAddress(g_RefreshAddresses[i].Reference());
                    const bool valid=current&&AlterEgo::CharacterIdentity::SameAddress(*current,g_RefreshAddresses[i]);
                    if(!valid){checked=false;g_RefreshSelection[i]=false;}
                    if(ImGui::Checkbox("##cb",&checked)&&valid)g_RefreshSelection[i]=checked;
                    ImGui::SameLine();
                    if (cached) {
                        ImGui::TextColored(GetProfessionColor(cached->profession), "%s", g_RefreshNames[i].c_str());
                        NameOptionContext(g_RefreshNames[i]);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Lv%d", cached->level);
                    } else {
                        ImGui::Text("%s%s",g_RefreshNames[i].c_str(),valid?"":" (unavailable)");
                        NameOptionContext(g_RefreshNames[i]);
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();

                int selCount = 0;
                for (bool b : g_RefreshSelection) { if (b) selCount++; }

                ImGui::Spacing();
                char btnLabel[64];
                snprintf(btnLabel, sizeof(btnLabel), "Refresh Selected (%d)", selCount);
                const bool busy=AlterEgo::GW2API::GetFetchStatus()==AlterEgo::FetchStatus::InProgress;
                const bool ready=AlterEgo::GW2API::GetAccountStatus()==AlterEgo::AccountStatus::Ready;
                const bool canRefresh=selCount>0&&!busy&&ready&&!g_RefreshListFetching;
                const char* why=!ready?"Link or verify an API account in Nexus options.":
                    (busy||g_RefreshListFetching)?"A character or roster request is already in progress.":"Select at least one verified character.";
                if (AlterEgo::UI::AvailableActionButton(btnLabel,AlterEgo::UI::ButtonRole::Standard,canRefresh,why)) {
                    std::vector<AlterEgo::CharacterIdentity::Address> selected;
                    for(size_t i=0;i<g_RefreshAddresses.size();++i)if(g_RefreshSelection[i])
                        selected.push_back(g_RefreshAddresses[i]);
                    std::stable_partition(selected.begin(),selected.end(),[&](const auto& address){
                        return g_SelectedCharacter.Matches(address);
                    });
                    AlterEgo::GW2API::RequestCharacterRefreshAddresses(selected);
                    g_DetailsFetched = false;
                    g_RefreshPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (RenderActionButton("Cancel")) {
                    g_RefreshPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                }

                ImGui::EndPopup();
            }

            ImGui::EndTabItem();
            ProcessCharacterLoad();
        }


        // Library, Vault, Clears and Achievements share the same tab dropdown.
        ImGuiTabBar* libraryTabBar = GImGui->CurrentTabBar;
        const ImGuiID previousSelectedTab = libraryTabBar ? libraryTabBar->SelectedTabId : 0;
        ImGuiTabItemFlags libraryFlags = NavigationTabFlags(1);
        if(g_LibEditMode){g_LibraryCollection=0;libraryFlags|=ImGuiTabItemFlags_SetSelected;}
        if (g_SwitchToLibrary) {
            libraryFlags |= ImGuiTabItemFlags_SetSelected;
            g_SwitchToLibrary = false;
        }
        const bool libraryVisible = ImGui::BeginTabItem("Library    ##ae_library_dropdown", nullptr, libraryFlags);
        const ImGuiTabItem* libItem = (libraryTabBar && libraryTabBar->LastTabItemIdx >= 0)
            ? &libraryTabBar->Tabs[libraryTabBar->LastTabItemIdx] : nullptr;
        const ImGuiID libraryTabId = libItem ? libItem->ID : 0;
        HandleTabDropdown(libraryMenu, libraryTabBar, previousSelectedTab,
            libraryTabId, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const bool libraryDirectNavigationClick = libraryMenu.labelClicked;
        if (libraryDirectNavigationClick) NavigateToLibraryCollection(0);
        if (libraryVisible) {
            g_MainTab = 1;
            if (g_LibraryCollection == 0) RenderBuildLibrary();
            else if (g_LibraryCollection == 1) RenderFashionLibrary();
            else if(g_LibraryCollection==2) { RenderComponentLibrary(); ProcessComponentUICommand(); }
            else { RenderEquipmentSetLibrary(); ProcessComponentUICommand(); }
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags compareTabFlags = NavigationTabFlags(5);
        if (g_SwitchToCompare) {
            compareTabFlags = ImGuiTabItemFlags_SetSelected;
            g_SwitchToCompare = false;
        }
        if (ImGui::BeginTabItem("Compare", nullptr, compareTabFlags)) {
            g_MainTab = 5; // Do not renumber existing session tab IDs.
            TickCharacterLoad();
            ImGui::BeginChild("##compare_page", ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);
            RenderCompareTab();
            ImGui::EndChild();
            RenderCompareStatusRow();
            ImGui::EndTabItem();
            ProcessCharacterLoad();
        }

        auto inventoryFlags=NavigationTabFlags(7);
        if(g_SwitchToInventory){inventoryFlags|=ImGuiTabItemFlags_SetSelected;g_SwitchToInventory=false;}
        ImGuiTabBar* inventoryBar=GImGui->CurrentTabBar;
        const ImGuiID beforeInventory=inventoryBar?inventoryBar->SelectedTabId:0;
        const bool inventoryVisible=ImGui::BeginTabItem("Inventory    ##ae_inventory_dropdown",nullptr,inventoryFlags);
        const ImGuiID inventoryId=inventoryBar&&inventoryBar->LastTabItemIdx>=0?inventoryBar->Tabs[inventoryBar->LastTabItemIdx].ID:0;
        HandleTabDropdown(inventoryMenu,inventoryBar,beforeInventory,inventoryId,ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
        if(inventoryMenu.labelClicked){
            MarkNavigation();AlterEgo::NativeAccounts::SetInventorySubview(0);g_SwitchToInventory=true;
        }
        if(inventoryVisible){
            g_MainTab=7;
            ImGui::BeginChild("##inventory_page",ImVec2(0,-AlterEgo::RefreshUI::RowHeight()),false);
            TickCharacterLoad();
            AlterEgo::NativeAccounts::RenderInventory(g_SelectedAccountFilter,SaveSettings,g_InventoryListWidth);
            ImGui::EndChild();
            AlterEgo::NativeAccounts::RenderInventoryStatusRow(g_SelectedAccountFilter);
            ImGui::EndTabItem();
            ProcessCharacterLoad();
        }

        // Disable account-scoped tabs when viewing "All Accounts"
        bool acctTabsDisabled = AlterEgo::GW2API::IsMultiAccount() && g_SelectedAccountFilter.empty();
        const char* acctTabTooltip = "Select a specific account to use this tab.";

        ImGuiTabItemFlags skinTabFlags = NavigationTabFlags(2);
        if (g_SwitchToSkinventory && !acctTabsDisabled) {
            skinTabFlags = ImGuiTabItemFlags_SetSelected;
            g_SwitchToSkinventory = false;
        }
        if (acctTabsDisabled) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
            ImGui::TabItemButton("Skins", ImGuiTabItemFlags_NoReorder);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered())
                AlterEgo::UI::Hint(acctTabTooltip);
        } else if (g_SkinInitialized) {
            ImGuiTabBar* skinBar=GImGui->CurrentTabBar;
            ImGuiID beforeSkin=skinBar?skinBar->SelectedTabId:0;
            const bool skinVisible=ImGui::BeginTabItem("Skins    ##ae_skin_dropdown",nullptr,skinTabFlags);
            const ImGuiID skinId=skinBar&&skinBar->LastTabItemIdx>=0?skinBar->Tabs[skinBar->LastTabItemIdx].ID:0;
            HandleTabDropdown(skinMenu,skinBar,beforeSkin,skinId,ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
            if(skinVisible) {
            g_MainTab = 2;
            ImGui::BeginChild("##skinventory_page", ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);
            RenderSkinventory();
            ImGui::EndChild();
            RenderSkinventoryStatusRow();
            ImGui::EndTabItem();
            }
        }

        if (acctTabsDisabled) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
            ImGui::TabItemButton("Wizard's Vault", ImGuiTabItemFlags_NoReorder);
            ImGui::TabItemButton("Clears", ImGuiTabItemFlags_NoReorder);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered())
                AlterEgo::UI::Hint(acctTabTooltip);
        } else {
            ImGuiTabBar* accountTabBar = GImGui->CurrentTabBar;
            ImGuiID beforeVault = accountTabBar ? accountTabBar->SelectedTabId : 0;
            const bool vaultVisible = ImGui::BeginTabItem("Wizard's Vault    ##ae_vault_dropdown",nullptr,NavigationTabFlags(3));
            const ImGuiID vaultId = accountTabBar && accountTabBar->LastTabItemIdx >= 0
                ? accountTabBar->Tabs[accountTabBar->LastTabItemIdx].ID : 0;
            HandleTabDropdown(vaultMenu, accountTabBar, beforeVault, vaultId,
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (vaultVisible) {
                vaultDemand.visible=g_WindowVisible;
                g_MainTab = 3; // Keep original session tab ID.
                ImGui::BeginChild("##vault_page", ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);
                RenderVault();
                ImGui::EndChild();
                RenderVaultStatusRow();
                ImGui::EndTabItem();
            }
            ImGuiID beforeClears = accountTabBar ? accountTabBar->SelectedTabId : 0;
            const bool clearsVisible = ImGui::BeginTabItem("Clears    ##ae_clears_dropdown",nullptr,NavigationTabFlags(6));
            const ImGuiID clearsId = accountTabBar && accountTabBar->LastTabItemIdx >= 0
                ? accountTabBar->Tabs[accountTabBar->LastTabItemIdx].ID : 0;
            HandleTabDropdown(clearsMenu, accountTabBar, beforeClears, clearsId,
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (clearsVisible) {
                g_MainTab = 6; // New session ID; do not renumber older tabs.
                ImGui::BeginChild("##clears_page", ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);
                RenderClears();
                ImGui::EndChild();
                RenderClearsStatusRow();
                ImGui::EndTabItem();
            }
        }

        if (acctTabsDisabled) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
            ImGui::TabItemButton("Achievements", ImGuiTabItemFlags_NoReorder);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered())
                AlterEgo::UI::Hint(acctTabTooltip);
        } else {
            ImGuiTabBar* achTabBar = GImGui->CurrentTabBar;
            const ImGuiID beforeAchievements = achTabBar ? achTabBar->SelectedTabId : 0;
            const bool achievementsVisible = ImGui::BeginTabItem("Achievements    ##ae_achievements_dropdown",nullptr,NavigationTabFlags(4));
            const ImGuiID achievementsId = achTabBar && achTabBar->LastTabItemIdx >= 0
                ? achTabBar->Tabs[achTabBar->LastTabItemIdx].ID : 0;
            HandleTabDropdown(achievementsMenu, achTabBar, beforeAchievements,
                achievementsId, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (achievementsVisible) {
            g_MainTab = 4;
            ImGui::BeginChild("##achievements_page", ImVec2(0, -AlterEgo::RefreshUI::RowHeight()), false);
            RenderAchievements();
            ImGui::EndChild();
            RenderAchievementsStatusRow();
            ImGui::EndTabItem();
            }
        }

        // Group boundaries are drawn in existing tab gaps, never as tabs.
        // The Vault ID differs in All Accounts mode (nonselecting tab button).
        const ImGuiID vaultGroupId=ImGui::GetID(acctTabsDisabled?"Wizard's Vault":"Wizard's Vault    ##ae_vault_dropdown");
        AlterEgo::UI::TabGroupDividerBefore(GImGui->CurrentTabBar,inventoryId);
        AlterEgo::UI::TabGroupDividerBefore(GImGui->CurrentTabBar,vaultGroupId);

        // ImGui restores the last selected main tab across sessions. Track
        // actual navigation instead of comparing against the saved tab ID.
        // Trigger after rendering so reset and account-switch checks run first.
        static int s_previousVisibleMainTab = -1;
        if (g_MainTab != s_previousVisibleMainTab) {
            if(s_previousVisibleMainTab>=0&&!g_NavRestore&&!accountFallbackNavigation)MarkNavigation();
            if(g_MainTab==0)accountFallbackNavigation=false;
            if (g_MainTab == 3) RefreshVaultOnEntry();
            else if (g_MainTab == 6) RefreshClearsOnEntry();
            s_previousVisibleMainTab = g_MainTab;
        }
        ImGui::EndTabBar();
        // Live identity discovery is visible-window demand, independent of
        // which page is open. Retain the existing opt-in and retry gate.
        if(g_MainTab==7&&AlterEgo::NativeAccounts::InventorySubview()==0)
            AlterEgo::NativeAccounts::PollObservedIdentity(g_CurrentCharName,true);
        AlterEgo::NativeAccounts::PollObservedIdentity(g_CurrentCharName);

        if(inventoryMenu.requestOpen)ImGui::OpenPopup("Inventory menu##ae_main");
        ImGui::SetNextWindowPos(inventoryMenu.anchor,ImGuiCond_Appearing);
        if(ImGui::BeginPopup("Inventory menu##ae_main")){
            bool chosen=false,subHovered=false;const bool hovered=TabDropdownMenuHovered();
            namespace Inventory=AlterEgo::NativeAccounts;
            const auto select=[&](int sub){MarkNavigation();Inventory::SetInventorySubview(sub);g_SwitchToInventory=true;chosen=true;};
            const auto flyout=AlterEgo::UI::TabFlyoutRow("Items##ae_inventory_items",
                g_MainTab==7&&Inventory::InventorySubview()==0,"##ae_inventory_view_arrow","Inventory view##ae_main");
            if(flyout.clicked){select(0);ImGui::CloseCurrentPopup();}
            ImGui::SetNextWindowPos(flyout.anchor,ImGuiCond_Appearing);
            if(ImGui::BeginPopup("Inventory view##ae_main")){
                subHovered=TabDropdownMenuHovered();
                if(ImGui::MenuItem("View as List",nullptr,!Inventory::InventoryGrouped())){
                    select(0);Inventory::SetInventoryGrouped(false);SaveSettings();ImGui::CloseCurrentPopup();
                }
                if(ImGui::MenuItem("View as Groups",nullptr,Inventory::InventoryGrouped())){
                    select(0);Inventory::SetInventoryGrouped(true);SaveSettings();ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if(ImGui::MenuItem("Wallet",nullptr,g_MainTab==7&&Inventory::InventorySubview()==1))select(1);
            if(ImGui::MenuItem("Guild Bank",nullptr,g_MainTab==7&&Inventory::InventorySubview()==2))select(2);
            FinishTabDropdown(inventoryMenu,chosen,hovered,subHovered);ImGui::EndPopup();
        }
        SyncTabDropdownOpen(inventoryMenu,"Inventory menu##ae_main");

        // Render OpenPopup and BeginPopup together in the stable main-window
        // scope, *after* EndTabBar. This works when Library itself is selected.
        if (libraryMenu.requestOpen)
            ImGui::OpenPopup("Library menu##ae_main");
        ImGui::SetNextWindowPos(libraryMenu.anchor, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("Library menu##ae_main")) {
            bool libraryActionSelected = false;
            bool libraryMouseOverSubmenu = false;
            const ImVec2 menuPos = ImGui::GetWindowPos();
            const ImVec2 menuSize = ImGui::GetWindowSize();
            const bool libraryMouseOverMenu = ImGui::IsMouseHoveringRect(
                menuPos, ImVec2(menuPos.x + menuSize.x, menuPos.y + menuSize.y), false);
            ImGui::TextDisabled("COLLECTIONS");
            // Only the actually visible collection is checked. Merely opening
            // this dropdown from another tab does not select either collection.
            const bool onBuilds = g_MainTab == 1 && g_LibraryCollection == 0;
            const bool onFashion = g_MainTab == 1 && g_LibraryCollection == 1;
            const bool onComponents = g_MainTab == 1 && g_LibraryCollection == 2;

            // The Builds label is a navigation target; its separate, right-
            // hand arrow opens a collection-specific flyout. Do not turn the
            // entire row into a submenu, or direct navigation stops working.
            const auto buildsFlyout=AlterEgo::UI::TabFlyoutRow("Loadouts##ae_library_collection",
                onBuilds,"##ae_builds_actions","Builds actions##ae_library");
            const bool buildsClicked=buildsFlyout.clicked;
            if (buildsClicked) {
                libraryActionSelected = true;
                NavigateToLibraryCollection(0);
                if (libraryTabBar) libraryTabBar->NextSelectedTabId = libraryTabId;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SetNextWindowPos(buildsFlyout.anchor,ImGuiCond_Appearing);
            if (ImGui::BeginPopup("Builds actions##ae_library")) {
                const ImVec2 subPos = ImGui::GetWindowPos();
                libraryMouseOverSubmenu = ImGui::IsMouseHoveringRect(subPos,
                    ImVec2(subPos.x + ImGui::GetWindowSize().x,
                           subPos.y + ImGui::GetWindowSize().y), false);
                if (ImGui::MenuItem("New",nullptr,false,!LibraryDraftOpen())) {
                    g_NewBuildModes = {AlterEgo::GameMode::PvE};
                    g_NewBuildDialogOpen = true;
                    libraryActionSelected = true;
                    // The parent menu should also close when launching New.
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (ImGui::MenuItem("Components", nullptr, onComponents,!g_LibEditMode)) {
                libraryActionSelected = true;
                NavigateToLibraryCollection(2);
                if (libraryTabBar) libraryTabBar->NextSelectedTabId = libraryTabId;
            }
            if(ImGui::MenuItem("Equipment Sets",nullptr,g_MainTab==1&&g_LibraryCollection==3,!g_LibEditMode)) {
                libraryActionSelected=true;
                NavigateToLibraryCollection(3);
                if(libraryTabBar)libraryTabBar->NextSelectedTabId=libraryTabId;
            }
            if (ImGui::MenuItem("Fashion", nullptr, onFashion,!g_LibEditMode)) {
                libraryActionSelected = true;
                NavigateToLibraryCollection(1);
                if (libraryTabBar) libraryTabBar->NextSelectedTabId = libraryTabId;
            }

            ImGui::Separator();
            ImGui::TextDisabled("TEMPLATE IMPORT");
            if (ImGui::MenuItem("Import from Link / URL",nullptr,false,!LibraryDraftOpen())) {
                libraryActionSelected = true;
                g_LibShowImport = true;
                g_LibImportBuf[0] = '\0';
                g_LibImportName[0] = '\0';
                g_LibImportModes = {AlterEgo::GameMode::PvE};
                g_LibImportError.clear();
                g_LibImportPending = false;
                g_LibImportPendingProfession.clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", AlterEgo::BuildRelay::ImportTooltip());

            ImGui::Separator();
            ImGui::TextDisabled("LIBRARIES");
            if (ImGui::MenuItem("Import")) {
                libraryActionSelected = true;
                BeginLibraryBackupImport();
            }
            if (ImGui::MenuItem("Export")) {
                libraryActionSelected = true;
                BeginLibraryBackupExport();
            }
            FinishTabDropdown(libraryMenu, libraryActionSelected,
                libraryMouseOverMenu, libraryMouseOverSubmenu);
            ImGui::EndPopup();
        }
        SyncTabDropdownOpen(libraryMenu, "Library menu##ae_main");

        if (!acctTabsDisabled) {
            if(g_SkinInitialized) {
                if(skinMenu.requestOpen)ImGui::OpenPopup("Skinventory menu##ae_main");
                ImGui::SetNextWindowPos(skinMenu.anchor,ImGuiCond_Appearing);
                if(ImGui::BeginPopup("Skinventory menu##ae_main")) {
                    bool chosen=false;
                    const bool hovered=TabDropdownMenuHovered();
                    if(ImGui::MenuItem("View as Grid",nullptr,g_SkinViewMode==1)) {
                        g_SkinViewMode=g_SkinViewMode==1?0:1;SaveSession();chosen=true;
                    }
                    FinishTabDropdown(skinMenu,chosen,hovered);
                    ImGui::EndPopup();
                }
                SyncTabDropdownOpen(skinMenu,"Skinventory menu##ae_main");
            }
            if (vaultMenu.requestOpen) ImGui::OpenPopup("Vault menu##ae_main");
            ImGui::SetNextWindowPos(vaultMenu.anchor, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("Vault menu##ae_main")) {
                bool chosen = false;
                const bool hovered = TabDropdownMenuHovered();
                if (ImGui::MenuItem("Hide Complete", nullptr, VaultHideComplete())) {
                    ToggleVaultHideComplete();
                    chosen = true;
                }
                if (ImGui::MenuItem("Show Objective Assistance", nullptr, ShowVaultAssistance())) {
                    ToggleVaultAssistance();
                    chosen = true;
                }
                FinishTabDropdown(vaultMenu, chosen, hovered);
                ImGui::EndPopup();
            }
            SyncTabDropdownOpen(vaultMenu, "Vault menu##ae_main");

            if (clearsMenu.requestOpen) ImGui::OpenPopup("Clears menu##ae_main");
            ImGui::SetNextWindowPos(clearsMenu.anchor, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("Clears menu##ae_main")) {
                bool chosen = false;
                const bool hovered = TabDropdownMenuHovered();
                if (ImGui::MenuItem("Hide Complete", nullptr, ClearsHideComplete())) {
                    ToggleClearsHideComplete();
                    chosen = true;
                }
                if (ImGui::MenuItem("Show Fractal Next Resets", nullptr, ShowFractalNextResets())) {
                    ToggleFractalNextResets();
                    chosen = true;
                }
                FinishTabDropdown(clearsMenu, chosen, hovered);
                ImGui::EndPopup();
            }
            SyncTabDropdownOpen(clearsMenu, "Clears menu##ae_main");

            if (achievementsMenu.requestOpen) ImGui::OpenPopup("Achievements menu##ae_main");
            ImGui::SetNextWindowPos(achievementsMenu.anchor, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("Achievements menu##ae_main")) {
                bool chosen = false;
                const bool hovered = TabDropdownMenuHovered();
                if (ImGui::MenuItem("Show Popout Tracker", nullptr, g_AchPopoutVisible)) {
                    g_AchPopoutVisible = !g_AchPopoutVisible;
                    SaveAchTrackerState();
                    chosen = true;
                }
                if (ImGui::MenuItem("Hide Complete", nullptr, g_AchHideCompletedCats)) {
                    g_AchHideCompletedCats = !g_AchHideCompletedCats;
                    SaveAchTrackerState();
                    chosen = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Refresh Achievement Definitions", nullptr, false, !g_AchNameIndexFetching.load())) {
                    FetchAchNameIndex(true);
                    chosen = true;
                }
                FinishTabDropdown(achievementsMenu, chosen, hovered);
                ImGui::EndPopup();
            }
            SyncTabDropdownOpen(achievementsMenu, "Achievements menu##ae_main");
        }

    }
    // Import/export dialogs are available from every top-level tab.
    RenderLibraryImportDialog();
    RenderLibraryBackupUI();
    if constexpr (AlterEgo::BuildRelay::Enabled) RenderShareRelayPopups();
    RenderReferenceEditor();
    RenderLoadoutSaveReview();
    ProcessComponentUICommand();
    EndNavigationFrame();
    if(g_WindowVisible)RenderFastLoadConfirmation();else CancelFastLoadUI();

    ImGui::End();
    if (g_EditSavePending) {
        g_EditSavePending = false;
        if (g_LibEditMode && g_EditRefsReady && AlterEgo::GW2API::SaveUnifiedLoadout(g_EditDraft,g_EditSetId,g_EditFashionId,g_EditRefs,g_EditSharedKinds,g_EditDraftRevision,g_EditInheritedRefs,g_EditSharedReferenceKinds)) {
            g_EditDraftId.clear();
            g_LibEditMode = false; g_EditDirty = false;
            g_EditIsNewBuild = false;
            g_GearDialogOpen = false;
        } else {
            g_EditError="Could not save Loadout. Check the library revision or component compatibility. Draft retained.";
            if(APIDefs)APIDefs->GUI_SendAlert(g_EditError.c_str());
        }
    }
}

// --- Options/Settings Render ---

// List *.ttf filenames in <data>/fonts (created if missing). Returns bare filenames.
static std::vector<std::string> ScanFontFolder(std::string& outDir) {
    outDir = AlterEgo::GW2API::GetDataDirectory() + "/fonts";
    std::error_code ec; std::filesystem::create_directories(outDir, ec);
    std::vector<std::string> out;
    for (auto& e : std::filesystem::directory_iterator(outDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        std::string lower = name;
        for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
        // Skip the bundled face's cache file — it's already listed as "Inter (bundled)".
        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".ttf")
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static void RenderStorageDiagnostics() {
    using namespace AlterEgo;
    static Storage::CacheDiagnostics snapshot;
    static bool inspected=false;
    RenderSectionHeader("Storage & Cache", UI::SectionGold);
    if (RenderActionButton("Inspect Storage##ae_cache_diagnostics")) {
        snapshot=Storage::InspectStorage(GW2API::GetDataDirectory());
        inspected=true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        UI::TooltipText("Inspect Storage",
            "Inspect local storage without making API requests. Saved Loadouts and preferences are separate from rebuildable cache data.");
        ImGui::EndTooltip();
    }
    if(!g_ActivityLoadValid)
        ImGui::TextWrapped("Login history could not be loaded. Its original file is preserved; recover it with the addon unloaded.");
    if(g_StorageMaintenanceResult.attempted)
        ImGui::TextWrapped("%s",g_StorageMaintenanceResult.message.c_str());
    if(!g_StorageMaintenanceMessage.empty())
        ImGui::TextWrapped("%s",g_StorageMaintenanceMessage.c_str());
    if(g_StorageMaintenancePending) {
        ImGui::TextDisabled("Cache maintenance scheduled for next addon startup.");
    } else {
        static int selected=0;
        const char* labels[]={"Rebuild all cache (quarantine)","Clear downloaded artwork",
            "Rebuild characters","Rebuild account progress","Rebuild achievement catalog",
            "Rebuild public catalogs and prices","Rebuild inventory, wallet and guild bank"};
        const char* rebuildLabel="Rebuild##ae_cache_operation";
        const float avail=std::max(1.0f,ImGui::GetContentRegionAvail().x);
        const float reserve=UI::CompactButtonWidth(rebuildLabel)+ImGui::GetStyle().ItemSpacing.x;
        const bool sameRow=avail>=reserve+ImGui::GetFontSize()*6.0f;
        ImVec2 pad=ImGui::GetStyle().FramePadding;
        pad.y=std::max(pad.y,(UI::ActionButtonHeight()-ImGui::GetFontSize())*0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,pad);
        const bool comboOpen=UI::BeginCombo("##ae_cache_operation",labels[selected],0,
            sameRow?avail-reserve:avail);
        ImGui::PopStyleVar();
        if(comboOpen) {
            for(size_t i=0;i<Storage::kCacheOperations.size();++i)
                if(UI::SelectableOption(labels[i],selected==static_cast<int>(i)))selected=static_cast<int>(i);
            ImGui::EndCombo();
        }
        if(sameRow)ImGui::SameLine();
        if(RenderActionButton(rebuildLabel))
            ImGui::OpenPopup("Confirm cache maintenance##ae_cache_confirm");
        if(ImGui::IsItemHovered())AlterEgo::UI::Hint("Rebuild the selected cache at next addon startup.");
        if(ImGui::BeginPopupModal("Confirm cache maintenance##ae_cache_confirm",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s",labels[selected]);
            ImGui::TextWrapped("Runs at next addon startup, before cache readers and workers. Saved library, settings, pins, login history and backups are untouched.");
            ImGui::TextWrapped("Moved files are quarantined, not deleted. Missing data is rebuilt by normal refreshes.");
            if(UI::Button("Schedule",ImVec2(0,0),UI::ButtonRole::Destructive)) {
                std::string error;
                g_StorageMaintenancePending=Storage::ScheduleMaintenance(GW2API::GetDataDirectory(),
                    Storage::kCacheOperations[static_cast<size_t>(selected)],error);
                g_StorageMaintenanceMessage=g_StorageMaintenancePending?
                    "Scheduled. Restart the game to apply.":error;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(RenderActionButton("Cancel##ae_cache_confirm"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if(ImGui::TreeNode("API Activity##ae_api_activity")) {
        using namespace ApiActivity;
        static Snapshot activity;
        static int64_t sampled=-1;
        static size_t apiWaiting=0,wikiWaiting=0;
        const auto now=Now();
        if(sampled!=now){
            activity=Session().Read(now);sampled=now;
            apiWaiting=ArenaNet::Waiting();wikiWaiting=Skinventory::HttpClient::WikiWaiting();
        }
        const char* sources[]={"ArenaNet API","Wiki","ArenaNet artwork"};
        for(size_t source=0;source<SourceCount;++source){
            Totals totals;
            for(size_t i=0;i<Count;++i)if(Source(static_cast<Endpoint>(i))==source){
                const auto& t=activity.endpoints[i];
                totals.started+=t.started;totals.success+=t.success;totals.failed+=t.failed;
                totals.limited+=t.limited;totals.active+=t.active;totals.cancelled+=t.cancelled;
            }
            ImGui::Text("%s: %llu / %llu / %llu",sources[source],
                (unsigned long long)activity.last10[source],(unsigned long long)activity.last60[source],
                (unsigned long long)totals.started);
            ImGui::TextWrapped("  OK %llu | failed %llu (429: %llu) | cancelled %llu | active %llu | waiting %llu | peak %llu/s",
                (unsigned long long)totals.success,(unsigned long long)totals.failed,
                (unsigned long long)totals.limited,(unsigned long long)totals.cancelled,(unsigned long long)totals.active,
                (unsigned long long)(source==0?apiWaiting:source==1?wikiWaiting:0),(unsigned long long)activity.peak[source]);
            const auto& reasons=activity.reasons[source];
            ImGui::TextDisabled("  Manual %llu | objective %llu | visible %llu | background %llu | other %llu",
                (unsigned long long)reasons[static_cast<size_t>(Reason::Manual)],
                (unsigned long long)reasons[static_cast<size_t>(Reason::Objective)],
                (unsigned long long)reasons[static_cast<size_t>(Reason::Visible)],
                (unsigned long long)reasons[static_cast<size_t>(Reason::Background)],
                (unsigned long long)reasons[static_cast<size_t>(Reason::Unclassified)]);
        }
        ImGui::TextDisabled("Requests: last 10 min / hour / session");
        ImGui::Text("Achievement broker: %llu cache hits, %llu coalesced",
            (unsigned long long)activity.sharedHits,(unsigned long long)activity.sharedCoalesced);
        ImGui::TextWrapped("Metadata IDs: %llu claimed (%llu manual), %llu coalesced, %llu cooldown suppressed",
            (unsigned long long)activity.metadataOwned,(unsigned long long)activity.metadataManual,
            (unsigned long long)activity.metadataCoalesced,(unsigned long long)activity.metadataCooldown);
        if(ImGui::TreeNode("Endpoint groups##ae_api_endpoints")){
            for(size_t i=0;i<Count;++i){
                const auto& t=activity.endpoints[i];if(!t.started)continue;
                ImGui::TextWrapped("%s: %llu requests, %llu OK, %llu failed (%llu 429), %llu cancelled",
                    Labels[i],(unsigned long long)t.started,(unsigned long long)t.success,
                    (unsigned long long)t.failed,(unsigned long long)t.limited,(unsigned long long)t.cancelled);
            }
            ImGui::TreePop();
        }
        ImGui::TextDisabled("Local session only; HTTP success is not dataset validation.");
        ImGui::TreePop();
    }
    if(!inspected)return;
    auto database=[](const char* label,const Storage::DatabaseDiagnostic& info) {
        ImGui::Text("%s: %.2f MiB | schema %lld",label,
            static_cast<double>(info.bytes)/(1024.0*1024.0),static_cast<long long>(info.schema));
        ImGui::TextWrapped("%s",info.status.c_str());
    };
    database("Saved library",snapshot.library);
    database("Account registry",snapshot.accounts);
    database("Cache database",snapshot.cache);
    ImGui::Text("Preferences, library auxiliary files and custom assets: %.2f MiB%s",
        static_cast<double>(snapshot.durableFileBytes)/(1024.0*1024.0),snapshot.durableFilesComplete?"":" (partial)");
    ImGui::Text("Migration, recovery and other backups: %.2f MiB%s",
        static_cast<double>(snapshot.backupBytes)/(1024.0*1024.0),snapshot.backupsComplete?"":" (partial)");
    ImGui::Text("Downloaded artwork: %.2f MiB%s",
        static_cast<double>(snapshot.artworkBytes)/(1024.0*1024.0),
        snapshot.artworkComplete?"":" (partial)");
    if(ImGui::TreeNode("Dataset details##ae_cache_datasets")) {
        for(const auto& data:snapshot.datasets) {
            ImGui::Text("%s: %lld",data.label.c_str(),static_cast<long long>(data.records));
            if(data.refreshedAt>0) {
                const auto age=std::max<int64_t>(0,snapshot.inspectedAt-data.refreshedAt);
                ImGui::SameLine();
                if(age<3600)ImGui::TextDisabled("%lldm ago",static_cast<long long>(age/60));
                else if(age<86400)ImGui::TextDisabled("%lldh ago",static_cast<long long>(age/3600));
                else ImGui::TextDisabled("%lldd ago",static_cast<long long>(age/86400));
            }
        }
        ImGui::TreePop();
    }
    ImGui::TextDisabled("Snapshot as of last inspection; counts cover all cached accounts.");
    ImGui::TextDisabled("Quarantined files remain under backups/cache-quarantine until removed manually.");
}

void AddonOptions() {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    ThemeGuard themeGuard;
    ImGui::Text("Alter Ego");
    ImGui::TextDisabled("%s | File version %s", AlterEgo::BuildInfo::Channel(), AlterEgo::BuildInfo::FileVersion());
    if (ImGui::TreeNode("Build information##ae_build_info")) {
        ImGui::Text("Product version: %s", AlterEgo::BuildInfo::ProductVersion());
        ImGui::TextWrapped("Commit: %s", AlterEgo::BuildInfo::Commit());
        ImGui::TextWrapped("CI build: %s | Attempt: %s | Run: %s",
            AlterEgo::BuildInfo::RunNumber(), AlterEgo::BuildInfo::RunAttempt(), AlterEgo::BuildInfo::RunId());
        ImGui::TextDisabled("CI values of 0 indicate a local build.");
        ImGui::TreePop();
    }

    AlterEgo::NativeAccounts::RenderOptions();
    ImGui::Spacing();
    ImGui::Spacing();
    RenderSectionHeader("Automatic API Refresh", AlterEgo::UI::SectionGold);
    if (ImGui::Checkbox("Automatically refresh API data##ae_auto", &g_AutoRefreshEnabled)) {
        AlterEgo::AutoRefreshPolicy::SetEnabled(g_AutoRefreshEnabled);
        if (g_AutoRefreshEnabled) {
            // Account data is refreshed by its visible consumer, not the
            // remembered tab in settings. Keep existing public-catalog policy.
            if (AlterEgo::AutoRefreshPolicy::Included(AlterEgo::AutoRefreshPolicy::Dataset::PublicCatalog) && g_SkinInitialized && Skinventory::SkinCache::GetStatus() == Skinventory::CacheStatus::Ready)
                Skinventory::SkinCache::UpdateCacheAsync();
            AlterEgo::GW2API::FetchAllItemStatsAsync();
            AlterEgo::GW2API::FetchDyeColorsAsync();
            Skinventory::Commerce::RefreshUpgradeCatalogAsync(false);
            AlterEgo::PublicCatalog::RefreshVault(AlterEgo::GW2API::GetDataDirectory(),false);
            AlterEgo::WikiEvents::Refresh(AlterEgo::GW2API::GetDataDirectory());
        }
        SaveSettings();
    }
    ImGui::Indent(16.0f);
    if (!g_AutoRefreshEnabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    ImGui::TextDisabled("Characters, Bank, Shared inventory and Materials: 10 minutes while relevant views are open.");
    ImGui::TextDisabled("Legendary Armory: 30 minutes; login alone does not fetch.");
    if (ImGui::Checkbox("When specialization changes##ae_auto_spec", &g_AutoRefreshOnSpecialization)){
        AlterEgo::CharacterActivity::SpecSignal()=g_AutoRefreshOnSpecialization;SaveSettings();
    }
    int objectiveMinutes=AlterEgo::RefreshEngine::ObjectiveMinutes();
    ImGui::SetNextItemWidth(100.0f);
    if(ImGui::InputInt("Objective tracking (minutes)##ae_objective_interval",&objectiveMinutes,1,5)){
        AlterEgo::RefreshEngine::SetObjectiveMinutes(objectiveMinutes);SaveSettings();
    }
    if(ImGui::IsItemHovered())AlterEgo::UI::Hint("Pinned achievements, Clears and daily/weekly Wizard\'s Vault. General browsing and Special: 30 minutes.");
    if (!g_AutoRefreshEnabled) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
    ImGui::Unindent(16.0f);
    if(ImGui::TreeNode("Automatic refresh datasets")){
        using namespace AlterEgo::AutoRefreshPolicy;
        ImGui::TextDisabled("Excluded datasets can still be refreshed manually.");
        for(uint32_t i=0;i<static_cast<uint32_t>(Dataset::Count);++i){
            const auto dataset=static_cast<Dataset>(i);
            if(i==0||i==1||i==3||i==4||i==6){
                ImGui::Spacing();ImGui::TextDisabled("%s",i==0?"Character data":i==1?"Account ownership and achievements":i==3?"Public catalogs":i==4?"Completion trackers":"Inventory");
            }
            bool included=Included(dataset);
            if(ImGui::Checkbox(Labels[i],&included)){
                SetIncluded(dataset,included);
                if(included&&dataset==Dataset::PublicCatalog){
                    Skinventory::SkinCache::UpdateCacheAsync();
                    AlterEgo::GW2API::FetchAllItemStatsAsync();
                    AlterEgo::GW2API::FetchDyeColorsAsync();
                    Skinventory::Commerce::RefreshUpgradeCatalogAsync(false);
                    AlterEgo::PublicCatalog::RefreshVault(AlterEgo::GW2API::GetDataDirectory(),false);
                    AlterEgo::WikiEvents::Refresh(AlterEgo::GW2API::GetDataDirectory());
                }
                SaveSettings();
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::Checkbox("Warn when live specialization differs from retrieved build##ae_warn_spec",
                        &g_WarnStaleSpecialization))
        SaveSettings();
    ImGui::TextDisabled("Trading Post prices: 60 minutes, selected item demand only; manual refresh bypasses TTL.");
    ImGui::TextDisabled("Applies to automatic API refreshes across all tabs.");
    ImGui::TextDisabled("Manual Refresh bypasses ordinary freshness/cooldown; offline automatic refresh waits 10 minutes.");
    ImGui::TextDisabled("The GW2 API may still return cached equipment/build data.");
    RenderSectionHeader("Public reference catalogs", AlterEgo::UI::SectionGold);
    ImGui::TextDisabled("Item/skin discovery, upgrades, Vault, companions, stats, professions: 7 days. Dyes: 30 days.\nDemanded skills, traits, specializations and items: 7 days. Retry: 24h.");
    const auto publicData=AlterEgo::PublicCatalog::Get();
    ImGui::Text("Equipment upgrades: %zu",publicData->entries.size());
    if(publicData->upgrades)ImGui::TextDisabled("Last refreshed: %lld day(s) ago",static_cast<long long>(std::max<int64_t>(0,AlterEgo::PublicCatalog::Now()-publicData->upgrades)/86400));
    if(RenderActionButton("Refresh equipment catalogs")) Skinventory::Commerce::RefreshUpgradeCatalogAsync(true);
    ImGui::TextDisabled("%s",Skinventory::Commerce::GetFetchStatus().c_str());
    ImGui::Text("Wizard's Vault objective definitions: %zu",publicData->objectives.size());
    if(publicData->vault)ImGui::TextDisabled("Last refreshed: %lld day(s) ago",static_cast<long long>(std::max<int64_t>(0,AlterEgo::PublicCatalog::Now()-publicData->vault)/86400));
    if(RenderActionButton("Refresh Wizard's Vault definitions")) AlterEgo::PublicCatalog::RefreshVault(AlterEgo::GW2API::GetDataDirectory(),true);
    ImGui::TextDisabled("%s",AlterEgo::PublicCatalog::VaultStatus().c_str());
    auto referenceAction=[&](const char* label,const char* dataset,auto refresh){
        auto it=publicData->freshness.find(dataset);
        if(it!=publicData->freshness.end()&&it->second.first)
            ImGui::TextDisabled("Last refreshed: %lld day(s) ago",static_cast<long long>(std::max<int64_t>(0,AlterEgo::PublicCatalog::Now()-it->second.first)/86400));
        if(RenderActionButton(label))refresh();
        const auto status=AlterEgo::PublicCatalog::DatasetStatus(dataset);
        if(!status.empty())ImGui::TextDisabled("%s",status.c_str());
    };
    referenceAction("Discover new items","public_item_discovery",[]{Skinventory::Commerce::RefreshUpgradeCatalogAsync(true,true);});
    referenceAction("Refresh skin catalog","public_skins",[]{Skinventory::SkinCache::UpdateCacheAsync(true);});
    referenceAction("Refresh item stats","public_stats",[]{AlterEgo::GW2API::FetchAllItemStatsAsync(true);});
    referenceAction("Refresh dyes","public_dyes",[]{AlterEgo::GW2API::FetchDyeColorsAsync(true);});
    referenceAction("Refresh ranger pets","public_pets",[]{AlterEgo::PublicCatalog::RefreshPets(AlterEgo::GW2API::GetDataDirectory(),true);});
    referenceAction("Refresh revenant legends","public_legends",[]{AlterEgo::PublicCatalog::RefreshLegends(AlterEgo::GW2API::GetDataDirectory(),true);});
    if(RenderActionButton("Refresh profession metadata"))AlterEgo::GW2API::RefreshProfessionMetadataAsync();
    ImGui::TextDisabled("%s",AlterEgo::GW2API::ProfessionMetadataStatus().c_str());
    ImGui::Spacing();
    RenderSectionHeader("Wiki reference schedules", AlterEgo::UI::SectionGold);
    ImGui::TextDisabled("Build assets: 24 hours. Runtime event timers: 30 days; retry: 24h.");
    const auto wikiTimers=AlterEgo::WikiEvents::Get();
    ImGui::Text("Event timer phases: %zu (%s)",wikiTimers->timers.size(),wikiTimers->embedded?"embedded baseline":"runtime cache");
    ImGui::TextDisabled("Last verified: %lld day(s) ago",static_cast<long long>(std::max<int64_t>(0,AlterEgo::PublicCatalog::Now()-wikiTimers->verified)/86400));
    if(RenderActionButton("Refresh Wiki event timers")) AlterEgo::WikiEvents::Refresh(AlterEgo::GW2API::GetDataDirectory(),true);
    const auto wikiStatus=AlterEgo::WikiEvents::Status();
    if(!wikiStatus.empty())ImGui::TextDisabled("%s",wikiStatus.c_str());
    ImGui::Spacing();
    RenderSectionHeader("UI Settings", AlterEgo::UI::SectionGold);
    {
        using AlterEgo::FastLoadUI::Mode;
        const char* labels[]={"Disabled","Enabled","Enabled with confirmation"};
        const Mode modes[]={Mode::Disabled,Mode::Enabled,Mode::Confirm};
        const int selected=g_FastLoadMode==Mode::Disabled?0:g_FastLoadMode==Mode::Confirm?2:1;
        ImGui::TextUnformatted("Fast Load");
        if(RenderThemedCombo("##ae_fast_load_mode",labels[selected],230.f)){
            for(int i=0;i<3;++i)if(AlterEgo::UI::SelectableOption(labels[i],selected==i)&&i!=selected){
                g_FastLoadMode=modes[i];CancelFastLoadUI();SaveSettings();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Controls load actions only; character location sampling remains available.");
    }
    if (ImGui::Checkbox("Show Quick Access icon", &g_ShowQAIcon)) {
        if (g_ShowQAIcon) {
            APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON_HOVER, "KB_ALTER_EGO_TOGGLE", "Alter Ego");
        } else {
            APIDefs->QuickAccess_Remove(QA_ID);
        }
        SaveSettings();
    }
    ImGui::Spacing();
    RenderSectionHeader("Chat Build Detection", AlterEgo::UI::SectionGold);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Requires 'Events: Chat' addon from the Nexus library.");
    if (ImGui::Checkbox("Detect build links in chat", &g_ChatBuildDetection)) {
        g_ChatDetectionEnabled.store(g_ChatBuildDetection);
        SaveSettings();
    }
    if (g_ChatBuildDetection) {
        ImGui::Indent(16.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Drag the toast notification to reposition it.");
        if (RenderChipButton("Reset Toast Position", false)) {
            g_ToastPosX = -1.0f;
            g_ToastPosY = 100.0f;
            g_ToastPosInitialized = false;
            SaveSettings();
        }
        ImGui::Unindent(16.0f);
    }
    ImGui::Spacing();
    RenderSectionHeader("Character List", AlterEgo::UI::SectionGold);
    bool expanded = !g_CompactCharList;
    if (ImGui::Checkbox("Expanded mode", &expanded)) {
        g_CompactCharList = !expanded;
        SaveSettings();
    }
    ImGui::TextColored(ImVec4(0.55f, 0.53f, 0.45f, 1.0f),
        "Columns shown in expanded mode:");
    ImGui::Indent(16.0f);
    if (g_CompactCharList) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
    }
    if (ImGui::Checkbox("Crafting Profession Icons", &g_ShowCraftingIcons)) {
        SaveSettings();
    }
    if (ImGui::Checkbox("Age", &g_ShowAge)) {
        SaveSettings();
    }
    if (ImGui::Checkbox("Playtime", &g_ShowPlaytime)) {
        SaveSettings();
    }
    if (ImGui::Checkbox("Last Login", &g_ShowLastLogin)) {
        SaveSettings();
    }
    ImGui::Text("Next Birthday");
    ImGui::SameLine();
    if (ImGui::RadioButton("Always", g_BirthdayMode == 0)) {
        g_BirthdayMode = 0;
        SaveSettings();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("A week out", g_BirthdayMode == 1)) {
        g_BirthdayMode = 1;
        SaveSettings();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Never", g_BirthdayMode == 2)) {
        g_BirthdayMode = 2;
        SaveSettings();
    }
    if (g_CompactCharList) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
    ImGui::Unindent(16.0f);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.85f, 0.78f, 0.55f, 1.0f), "Font");

    std::string fontsDir;
    std::vector<std::string> ttfs = ScanFontFolder(fontsDir);

    // Current selection label.
    std::string current;
    if (g_FontConfig.face == AlterEgo::Font::Face::NexusDefault) current = "Nexus default";
    else {
        current = g_FontConfig.customPath;
        auto slash = current.find_last_of("/\\");
        if (slash != std::string::npos) current = current.substr(slash + 1);
    }

    ImGui::SetNextItemWidth(220.0f);
    if (AlterEgo::UI::BeginCombo("##fontface", current.c_str())) {
        if (ImGui::Selectable("Nexus default", g_FontConfig.face == AlterEgo::Font::Face::NexusDefault)) {
            g_FontConfig.face = AlterEgo::Font::Face::NexusDefault;
            g_FontConfig.customPath.clear();
            SaveSettings();
        }
        for (const auto& name : ttfs) {
            std::string full = fontsDir + "/" + name;
            bool sel = (g_FontConfig.face == AlterEgo::Font::Face::Custom && g_FontConfig.customPath == full);
            if (ImGui::Selectable(name.c_str(), sel)) {
                g_FontConfig.face = AlterEgo::Font::Face::Custom;
                g_FontConfig.customPath = full;
                SaveSettings();
            }
        }
        ImGui::EndCombo();
    }

    // Size slider only for TTF faces (the Nexus-default face is unscaled).
    if (g_FontConfig.face != AlterEgo::Font::Face::NexusDefault) {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Size (px)", &g_FontConfig.px, 10.0f, 32.0f, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveSettings();
    }

    ImGui::TextColored(ImVec4(0.55f, 0.53f, 0.45f, 1.0f),
                       "Drop your own .ttf files in: %s", fontsDir.c_str());
    ImGui::Spacing();
    RenderStorageDiagnostics();
}

// --- Export: GetAddonDef ---

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature = 0xA17E3E90;  // Unique ID for Alter Ego
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "Alter Ego";
    AddonDef.Version.Major = V_MAJOR;
    AddonDef.Version.Minor = V_MINOR;
    AddonDef.Version.Build = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author = "PieOrCake.7635";
    AddonDef.Description = "Manage characters, builds, skins, clears, achievements. Native account API key required for authenticated data. Optional - Events:Chat and Events:Alerts";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = AF_None;
    AddonDef.Provider = UP_GitHub;
    AddonDef.UpdateLink = "https://github.com/PieOrCake/alter_ego";

    return &AddonDef;
}
