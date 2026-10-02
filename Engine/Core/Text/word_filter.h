#pragma once
#include <string>
#include <string_view>

// The bad-word filter: the list in bad_words.txt (built in), matched against text after
// folding case and look-alike characters, so "Sh1t", "@$$" and "f u c k" count too.
//
// - Words of three letters or fewer, and a few longer ones that are common inside ordinary
//   words ("hell" in hello, "arse" in parse), only match a whole word.
// - Every other word also matches inside a longer one ("fuckface"), unless an ordinary word
//   covers it there ("Scunthorpe", "cocktail").
//
// Used for dedicated server names (the server refuses to list one, the client hides it) and,
// as a player option, to mask chat.
namespace dingosdk::text {
bool contains_bad_words(std::string_view text);

// `text` with each letter of every bad word replaced by '*'; same length, other text untouched.
std::string mask_bad_words(std::string_view text);
} // namespace dingosdk::text
