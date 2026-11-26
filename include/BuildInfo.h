#pragma once

namespace AlterEgo::BuildInfo {
const char* FileVersion() noexcept;
const char* ProductVersion() noexcept;
const char* Commit() noexcept;
const char* Channel() noexcept;
const char* RunNumber() noexcept;
const char* RunAttempt() noexcept;
const char* RunId() noexcept;
}
