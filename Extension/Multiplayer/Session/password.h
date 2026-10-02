#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
namespace dingosdk::multiplayer {
using PasswordKey = std::array<std::uint8_t, 32>;
std::optional<PasswordKey> password_key(std::string_view password, std::uint64_t session);
PasswordKey password_proof(const PasswordKey &, std::uint64_t session, std::uint64_t map, std::uint64_t host,
                           std::uint64_t guest, std::uint64_t host_epoch, std::uint64_t guest_epoch,
                           std::uint64_t challenge);
bool proof_matches(const PasswordKey &, const PasswordKey &) noexcept;
void erase_password(std::string &) noexcept;
void erase_key(std::optional<PasswordKey> &) noexcept;
} // namespace dingosdk::multiplayer
