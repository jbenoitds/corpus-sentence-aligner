// SPDX-License-Identifier: MIT
#include "csa/segmenter.hpp"
#include "minitest.hpp"

using namespace csa;
using V = std::vector<std::string>;

TEST(seg_en_basic) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("Hello there. How are you? Fine! Thanks."), (V{"Hello there.", "How are you?", "Fine!", "Thanks."}));
}

TEST(seg_en_abbreviations) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("Dr. Smith met Mrs. Jones. They talked.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("Use a timer, e.g. A kitchen timer. Then rest.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("Smith et al. (2004) agree. We do too.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("See p. 12 and Fig. 3 for details. Next.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("J. R. R. Tolkien wrote it. It is long.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("Made in the U.S. by hand. Done.").size(), static_cast<std::size_t>(2));
}

TEST(seg_en_nonblocking_etc) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("Bring pens, paper, etc. We start at 9 a.m. sharp.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split("Bring pens, paper, etc. and food.").size(), static_cast<std::size_t>(1));
}

TEST(seg_numeric_abbreviation) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("Call No. 5 now. The answer was no. Then we left.").size(), static_cast<std::size_t>(3));
}

TEST(seg_list_numbering_and_decimals) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("1. Introduction to sleep.").size(), static_cast<std::size_t>(1));
    CHECK_EQ(s.split("IV. Results are here.").size(), static_cast<std::size_t>(1));
    CHECK_EQ(s.split("It takes 3.5 hours. Then it ends.").size(), static_cast<std::size_t>(2));
}

TEST(seg_lowercase_does_not_split) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("The version is 2.0 and the site is www.example.com today.").size(), static_cast<std::size_t>(1));
    CHECK_EQ(s.split("Wait... what happened?").size(), static_cast<std::size_t>(1));
    CHECK_EQ(s.split("Wait... Nothing happened.").size(), static_cast<std::size_t>(2));
}

TEST(seg_quotes_and_brackets) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split(u8"He said “stop.” Then he left."),
             (V{u8"He said “stop.”", "Then he left."}));
    CHECK_EQ(s.split("(This is aside.) Main text."), (V{"(This is aside.)", "Main text."}));
}

TEST(seg_footnote_number_glued) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("About 39% report insomnia.2 Another study agrees."),
             (V{"About 39% report insomnia.2", "Another study agrees."}));
}

TEST(seg_fr_typography) {
    SentenceSplitter s("fr");
    // No-break space before ? and !
    CHECK_EQ(s.split(u8"Que se passe-t-il ? Rien ! Bonne nouvelle."),
             (V{u8"Que se passe-t-il ?", u8"Rien !", "Bonne nouvelle."}));
    // Closing guillemet after the period, with a space before it.
    CHECK_EQ(s.split(u8"Il a dit « Arrêtez. » Puis il est parti."),
             (V{u8"Il a dit « Arrêtez. »", u8"Puis il est parti."}));
    // Plain space before ? (common in Word documents)
    CHECK_EQ(s.split(u8"Vous dormez bien ? Oui.").size(), static_cast<std::size_t>(2));
}

TEST(seg_fr_abbreviations) {
    SentenceSplitter s("fr");
    CHECK_EQ(s.split(u8"M. Tremblay et Mme Roy sont arrivés. Ils parlent.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split(u8"Faites une activité, p. ex. Lire un livre. Ensuite, dormez.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split(u8"Voir la p. 12 et le no. 4 du questionnaire. Merci.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split(u8"Il faut des crayons, du papier, etc. Nous commençons demain.").size(), static_cast<std::size_t>(2));
    CHECK_EQ(s.split(u8"C.-à-d. Rien de plus. Fin.").size(), static_cast<std::size_t>(2));
}

TEST(seg_fr_verbs_not_abbreviations) {
    // "ont" and "mer" must still end sentences in French.
    SentenceSplitter s("fr");
    CHECK_EQ(s.split(u8"Ils ont. Elle est partie en mer. Nous restons.").size(), static_cast<std::size_t>(3));
}

TEST(seg_custom_abbreviation) {
    SentenceSplitter s("en");
    CHECK_EQ(s.split("Ask Assoc. Professor Lee. Done.").size(), static_cast<std::size_t>(3));
    s.add_abbreviation("Assoc.");
    CHECK_EQ(s.split("Ask Assoc. Professor Lee. Done.").size(), static_cast<std::size_t>(2));
}

TEST(seg_empty_and_whitespace) {
    SentenceSplitter s("en");
    CHECK(s.split("").empty());
    CHECK(s.split("   ").empty());
    CHECK_EQ(s.split("No final punctuation"), (V{"No final punctuation"}));
}
