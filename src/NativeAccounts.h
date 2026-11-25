#pragma once
#include <string>
namespace AlterEgo::NativeAccounts {
void Initialize(const std::string& root);
void Shutdown();
void RenderOptions();
bool HasLinkedAccounts();
std::string VerifiedAccountForCharacter(const std::string& character);
void RenderInventory(const std::string& activeCharacter);
}
