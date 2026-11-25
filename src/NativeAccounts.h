#pragma once
#include <string>
namespace AlterEgo::NativeAccounts {
void Initialize(const std::string& root);
void Shutdown();
void RenderOptions();
void RenderInventory(const std::string& activeCharacter);
}
