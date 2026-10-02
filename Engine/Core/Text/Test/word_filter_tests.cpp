#include "Engine/Core/Text/word_filter.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {
int failures = 0;

void expect(bool condition, std::string_view what) {
    if (condition) return;
    ++failures;
    std::printf("FAIL: %.*s\n", static_cast<int>(what.size()), what.data());
}

void bad(std::string_view text) { expect(dingosdk::text::contains_bad_words(text), std::string("bad: ") + std::string(text)); }
void clean(std::string_view text) { expect(!dingosdk::text::contains_bad_words(text), std::string("clean: ") + std::string(text)); }
void masks(std::string_view text, std::string_view expected) {
    const auto masked = dingosdk::text::mask_bad_words(text);
    expect(masked == expected, std::string("mask: \"") + std::string(text) + "\" -> \"" + masked + "\", wanted \"" +
                                   std::string(expected) + "\"");
}
} // namespace

int main() {
    // Listed words, any case, whole or inside longer words.
    bad("fuck");
    bad("FUCK this server");
    bad("Fuckface Skaters");
    bad("motherfuckers only");
    bad("Shithead's park");
    bad("big ass ramps");
    bad("Bitch Please");
    // Look-alikes and spelled-out letters.
    bad("sh1t");
    bad("@$$ hats");
    bad("f u c k");
    bad("F.U.C.K. yeah");
    bad("b17ch");
    // Ordinary words that contain a listed word, and server names people use.
    clean("Hello World");
    clean("Shell Shock Skatepark");
    clean("Classic Skate Session");
    clean("Pass the Grass");
    clean("Scunthorpe Skaters");
    clean("Cocktail Hour");
    clean("Peacock Plaza");
    clean("Dickies Team Session");
    clean("Arsenal FC fans");
    clean("Sparse Parsec");
    clean("Saturday Night Session");
    clean("Reputation Skate");
    clean("San Vansterdam 24/7");
    clean("Titanic Ledges");
    clean("Cumulus Bowl");
    clean("Essex Street League");
    clean("Pakistan Plaza");
    clean("Scrapyard DIY");
    clean("ReSkate server");
    clean("");
    // Masking keeps everything else and the length.
    masks("what the fuck dude", "what the **** dude");
    masks("shit happens", "**** happens");
    masks("sh1t", "****");
    masks("f u c k", "* * * *");
    masks("Hello friends", "Hello friends");
    masks("fuckface", "****face");
    if (failures == 0) std::printf("word filter: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
