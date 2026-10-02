// Reviewed native contract for supported_build::game_sha256 (September 29 2026).
// RVA-only code, pdata and unwind bytes are ASLR invariant. This file is a
// production compatibility guard, not an extracted asset or runtime dump.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dingosdk::game::build::v20260929::morph_memory_pool {
template<std::size_t N> consteval auto hex_bytes(const char (&hex)[N]) {
    static_assert(N % 2 == 1);
    std::array<std::uint8_t, (N - 1) / 2> result{};
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw "Invalid morph contract hex";
    };
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    return result;
}

// SHA256 9d5371d7d80da2418e82d14fdc28bf77e0d1c26c03ea34a9249e8b4759f2710d
inline constexpr auto constructor = hex_bytes(
    "48895c2408574883ec30488d05670c7804488bf9488901488d415848c7410800000000c5fc1002c5fc11411048894150"
    "ba0100000048894138448bc2488941304883c0404889414048c74148000000004881c198000000c5f877e8616527024c"
    "8bcf48c7442420000000004c8d05ee810000488d15c7810000488d8f98000000e85ba427024533c0488d8f98000000ba"
    "01000000e827c927024c8b4718488d8f98000000ba07000000e812c92702488b5c2440488bc74883c4305fc3");
// SHA256 255be1a04afcfe7c13c7a2f7e327246e06d702a914c8255c889b101daf7dd580
inline constexpr auto constructor_pdata_0 = hex_bytes(
    "8015b2013c16b20160b1ab06");
// SHA256 95a10c6d2d751622a4535e1f8c8bdca6a784f9865cf0e1039457f8b7b56c3dae
inline constexpr auto constructor_unwind_0 = hex_bytes(
    "010a04000a3408000a520670");
// SHA256 607fea135c302789e7f2deae98ebdd6d31f38e5a44c945694896bc8ed3d300c8
inline constexpr auto morph_initializer = hex_bytes(
    "48895c2408574881eca0000000488b1d74a59705488d05cd2b790448890546589a05488d05d72b790448890548589a05"
    "488d05e12b79044889054a589a0548890553589a05e8d6106ffe33ff84c00f8599010000488b054d5e9a054885c0751a"
    "488b0d194897054c8d053a5e9a05488d1553539a05e8c68adcff486348284c8d15f7f45504488d15d42a790448890d3d"
    "589a054889150e589a054533c94c89151c589a054533c0893d03589a05c705fd579a050200000048893dfa579a054889"
    "3d03589a054863402048891508589a05498bd248894c2420488d4c2450893dfd579a05c705f7579a050200000048893d"
    "f4579a054c8915f5579a0548893df6579a05488905f7579a05e8028ea8ff488b05bb579a05b920150000488b15f7887e"
    "05488b1d60a497054889442468c644247001e859f4a6ff4885c074194c8bc3488d542450488bc8e8248da8ff48890575"
    "579a05eb0748893d6c579a0548393d95579a057571488b0594579a05488d4c2450488b1578579a054533c94533c04889"
    "442420e8888da8ff488b0571579a05b920150000488b157d887e05488b1de6a397054889442468c644247001e8dff3a6"
    "ff4885c074194c8bc3488d542450488bc8e8aa8ca8ff4889052b579a05eb0748893d22579a05c605c3569a0501488d1d"
    "f4569a05488d05bd569a05eb03488bc348891d69569a0548891d82569a05488b1da35c9a0548890564569a054889057d"
    "569a054885db751d488b0d614697054c8d05825c9a05488d159b519a05e80e89dcff488bd8488b0d34569a05488b151d"
    "569a0548894c243048c74424380000010048634b3448894c2440b9700c0000c644244801e827f3a6ff4885c0740f488d"
    "542430488bc8e8a52e0100eb03488bc7488b15d9559a05b9700c000048890595569a05488b05d6559a05488944243048"
    "63432c4889442440c644244801e8def2a6ff4885c07425488d542430488bc8e85c2e010048890555569a05488b9c24b0"
    "0000004881c4a00000005fc3488b9c24b000000048893d35569a054881c4a00000005fc3");
// SHA256 33d299717f4325f4df48939844e7c9a58b871e085d43ee6c8e515846d718f903
inline constexpr auto morph_initializer_pdata_0 = hex_bytes(
    "60e4b00154e7b001fcc5ac06");
// SHA256 0805341631737f6e131b8ceadb7b0046158c75a5a77488732dd00253aff37e9f
inline constexpr auto morph_initializer_unwind_0 = hex_bytes(
    "010d05000d3416000d01140006700000");
// SHA256 b0d0e53c6708473c5c8aaaef4e3d8ed0a497990f86a70011d80d572beb784cf9
inline constexpr auto pool_allocate = hex_bytes(
    "40535556574154415641574883ec304d8be14d8bf84c8bf2488be90f1f440000488b4508a83f751b0f1f840000000000"
    "488bd04883ca3ff0480fb155087418a83f74ed41b801000000488d4d08488bd0e87b38a6ffebc94c8b4518488d8d9800"
    "00004533c9c744242004000000498bd7e88b852702488bf84885c07525488bcde81bfbffff4c8b4518488d8d98000000"
    "4533c9897c2420498bd7e861852702488bf848c7c2c1fffffff0480fc155084883c2c148f7c280ffffff7409488d4d08"
    "e84b73a6ff4d8bcf4c8bc7488bd5498bcee8eab53102498bd4498bcee8af5c6dfe498bc64883c430415f415e415c5f5e"
    "5d5bc3");
// SHA256 bec574e7df6885128b3496a7f3996084aae42ba123a546d9026f0e411eee822d
inline constexpr auto pool_allocate_pdata_0 = hex_bytes(
    "a038b2019339b201c4daab06");
// SHA256 eb3d1b62e666c6777ab6187669de7911588d02064dc5bd980db5d82be559a7f5
inline constexpr auto pool_allocate_unwind_0 = hex_bytes(
    "010f08000f520bf009e007c00570046003500230");
// SHA256 82090f22003a26a5baa782117422d2b7d01692808a90ccb4ed3a3a6533f856f7
inline constexpr auto pool_grow = hex_bytes(
    "4053555741554883ec68488b4120488bf948894424384533ed488b411848894424400fb64128488b4938488be9884424"
    "48482b6f30488d05b4ed770448c1fd03c74424303000000066c744244a01004c896c2458c6442449014889442450483b"
    "4f407310488d4108488947384c8929e9130100004889b424980000004c89a424a00000004c89b424a80000004c897c24"
    "6085ed741f81fdffffff7f73108d346d0000000085f675114d8bfdeb2cbeffffffffeb05be010000008bd6488d4f4848"
    "c1e2034533c941b80800000044896c2420e80a1ebf004c8bf88bcd4d8bc74d892ccf4c8b4f38488b4f30493bc9741790"
    "488b114c89294883c1084989104983c008493bc975ea4c8b77384d8d6008488b5f30493bde7417488b0b4885c9740648"
    "8b01ff50084883c308493bde75e9488b57304c8bb424a80000004885d27421483b5750488d4f4874174c8b47404c2bc2"
    "49c1f803458bc049c1e003e8c01dbf008bc6488bb424980000004c897f304c8967384c8ba424a0000000498d0cc74c8b"
    "7c246048894f40488b0d4258bc05488b5f38488b01ff50604c8d43f8488bc8488d5424304c8b0841ff91880000004c8b"
    "4720488d8f9800000089ac24900000004533c94489ac2494000000488b84249000000048c1e8208bd548c1e220480bd0"
    "4c896c242048b80000000000000080480bd0e8894b27024883c468415d5f5d5bc3");
// SHA256 1c0c2e6a97b2eaa9f4277e4648cb8abaae51541b07b177b6476cd4c8ad80bcb3
inline constexpr auto pool_grow_pdata_0 = hex_bytes(
    "4034b201b434b201b855c606");
// SHA256 38605cfc6c0a2eae2275b9f0d12567636ffb9468d1da3e3d3f7489b0cc42b396
inline constexpr auto pool_grow_unwind_0 = hex_bytes(
    "010a05000ac206d00470035002300000");
// SHA256 c9e7811d183bf2c809fd4bda01f0ed59e8aa1ea13f8e25bf08700b9c43f85111
inline constexpr auto pool_grow_pdata_1 = hex_bytes(
    "b434b201c434b201c855c606");
// SHA256 bf698f75148e8b8d93cbee934ca4e804af2b8975b04e04bf4a1428d6f35f49d2
inline constexpr auto pool_grow_unwind_1 = hex_bytes(
    "2110040010c41400086413004034b201b434b201b855c606");
// SHA256 3e4df33dc716181ae3129bc7911ccd84ce6015430f4a3904ed2ed0c4fdb45f90
inline constexpr auto pool_grow_pdata_2 = hex_bytes(
    "c434b2017f35b201e055c606");
// SHA256 e16c924644dc1dc90eb3c68bc8a8e63ad65b1205c98cef8ea1cc7c91993ba4ac
inline constexpr auto pool_grow_unwind_2 = hex_bytes(
    "210d04000df40c0008e41500b434b201c434b201c855c606");
// SHA256 640c455ecd23d4077cab2e987610fe1300b6bb2fb999527e2f673b48a6b632f2
inline constexpr auto pool_grow_pdata_3 = hex_bytes(
    "7f35b201c735b201f855c606");
// SHA256 ec4f597441944f311d301422061c2555e58e09b218f06ebc005204950a947e43
inline constexpr auto pool_grow_unwind_3 = hex_bytes(
    "2100020000f40c00b434b201c434b201c855c606");
// SHA256 15353f67825a571676015f7940054e1c25ce0aeae9ecfb06285cc1eb34d380ea
inline constexpr auto pool_grow_pdata_4 = hex_bytes(
    "c735b2014136b2010c56c606");
// SHA256 18eed2b26ab659cfce6633d49c6818a9fba7bfc7c56d340924c23e259b1bc9e1
inline constexpr auto pool_grow_unwind_4 = hex_bytes(
    "210000004034b201b434b201b855c606");
// SHA256 bac5adde31da1384fc6b409a92d261a7112fb4c46d0824eb78b078cc54c08129
inline constexpr auto pool_map_heap_offset = hex_bytes(
    "40535556574883ec28498be8488bfa488bf1488b460848a980ffffff75660fb6d080e23f80fa3e735b488d4801f0480f"
    "b14e0875e1488b4630488bcd48c1e92048c7c2ffffffff0fbaf11f488b0cc88bc54889470848890ff0480fc1560848ff"
    "caf6c23f751248f7c280ffffff7409488d4e08e8483aa6ff488bc74883c4285f5e5d5bc34533c0488d4e08488bd0e8ed"
    "fea5ffe97affffff");
// SHA256 a45a521d57a2ff5732f925598412e0ab324d4e138c224bfecce8b955ce76acbf
inline constexpr auto pool_map_heap_offset_pdata_0 = hex_bytes(
    "f071b2018872b20120b6ab06");
// SHA256 f28ccee9e54971753b66d1ea62e19a750e5b147360135aca2e5cffe13c1c86eb
inline constexpr auto pool_map_heap_offset_unwind_0 = hex_bytes(
    "01090500094205700460035002300000");
// SHA256 4896d783b760dae884dfc92ca9df2a19f00420d05a6fd73b6e44d0ca1a2d5b90
inline constexpr auto temporary_field_name = hex_bytes(
    "54656d7052656e6465724d656d6f7279506f6f6c457874656e6453697a6500");
// SHA256 6705479ca5b564ada2ee65fc0f3fa22b5a5e0ce0014c88ccb697eca29df61f15
inline constexpr auto pool_debug_name = hex_bytes(
    "44696e676f4d6f7270684d656d6f7279506f6f6c00");
// SHA256 73eb8b851694db818e5f16ae4ce8afedba597e738d78c82e7cdb5d3923e71dfd
inline constexpr auto settings_constructor = hex_bytes(
    "40534883ec20488bd9e8528f0004488d05fbef7a04c7432000008002488903488bc3c7432400008000c7432800008002"
    "c7432c00000001c7433000008000c743340000a0004883c4205bc3");

struct Contract { std::uintptr_t rva; std::span<const std::uint8_t> bytes; };
inline constexpr Contract contracts[]{
    {0x01b21580, constructor},
    {0x08d926c4, constructor_pdata_0},
    {0x06abb160, constructor_unwind_0},
    {0x01b0e460, morph_initializer},
    {0x08d91260, morph_initializer_pdata_0},
    {0x06acc5fc, morph_initializer_unwind_0},
    {0x01b238a0, pool_allocate},
    {0x08d929dc, pool_allocate_pdata_0},
    {0x06abdac4, pool_allocate_unwind_0},
    {0x01b23440, pool_grow},
    {0x08d92970, pool_grow_pdata_0},
    {0x06c655b8, pool_grow_unwind_0},
    {0x08d9297c, pool_grow_pdata_1},
    {0x06c655c8, pool_grow_unwind_1},
    {0x08d92988, pool_grow_pdata_2},
    {0x06c655e0, pool_grow_unwind_2},
    {0x08d92994, pool_grow_pdata_3},
    {0x06c655f8, pool_grow_unwind_3},
    {0x08d929a0, pool_grow_pdata_4},
    {0x06c6560c, pool_grow_unwind_4},
    {0x01b271f0, pool_map_heap_offset},
    {0x08d92cc4, pool_map_heap_offset_pdata_0},
    {0x06abb620, pool_map_heap_offset_unwind_0},
    {0x0629fcc0, temporary_field_name},
    {0x062a2230, pool_debug_name},
    {0x01af1dc0, settings_constructor},
};
inline constexpr std::size_t max_contract_size = [] {
    std::size_t size = 0;
    for (const auto& contract : contracts) size = contract.bytes.size() > size ? contract.bytes.size() : size;
    return size;
}();

// Morph heap constructor hooked to raise the render heap capacity.
inline constexpr std::uintptr_t constructor_rva = 0x01b21580;
// Return addresses of the temporary and permanent morph heap constructions.
inline constexpr std::uintptr_t temporary_return = 0x01b0e724;
inline constexpr std::uintptr_t permanent_return = 0x01b0e6db;
// Globals holding the temporary and permanent morph pools (null until built).
inline constexpr std::uintptr_t temporary_pool = 0x074b3d80;
inline constexpr std::uintptr_t permanent_pool = 0x074b3d88;
} // namespace dingosdk::game::build::v20260929::morph_memory_pool

