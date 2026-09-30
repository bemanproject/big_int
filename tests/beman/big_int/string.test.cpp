// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using namespace beman::big_int;

// to_string and its wide twin to_wstring both render through the same to_chars
// path; to_wstring additionally widens the narrow ASCII output. The ToWString
// fixed-value checks below are therefore the L"..." mirrors of the ToString
// ones, and the round-trips at the end cover the large-input kernels for both.

[[nodiscard]] big_int parse(const std::string_view str, const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2 && base <= 36);
    big_int result;
    const auto [p, ec] = from_chars(str.data(), str.data() + str.size(), result, base);
    if (ec != std::errc{}) {
        throw std::runtime_error("from_chars did not succeed.");
    }
    if (p != str.data() + str.size()) {
        throw std::runtime_error("from_chars parsed only part of the string.");
    }
    return result;
}

// Widens an ASCII narrow string to its wchar_t equivalent for the round-trips.
[[nodiscard]] std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

[[nodiscard]] std::string random_digit_string(const std::size_t len, const int base, const std::uint64_t seed) {
    static constexpr char              alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::mt19937_64                    rng{seed};
    std::uniform_int_distribution<int> dist(0, base - 1);
    std::string                        s(len, '0');
    for (char& c : s) {
        c = alphabet[dist(rng)];
    }
    if (s.front() == '0') {
        s.front() = alphabet[1]; // no leading zero, so the string round-trips exactly
    }
    return s;
}

// clang-format off
TEST(ToString, EveryBase_255) {
    constexpr big_int value = 255;
    EXPECT_EQ(to_string(value, 2), "11111111");
    EXPECT_EQ(to_string(value, 3), "100110");
    EXPECT_EQ(to_string(value, 4), "3333");
    EXPECT_EQ(to_string(value, 5), "2010");
    EXPECT_EQ(to_string(value, 6), "1103");
    EXPECT_EQ(to_string(value, 7), "513");
    EXPECT_EQ(to_string(value, 8), "377");
    EXPECT_EQ(to_string(value, 9), "313");
    EXPECT_EQ(to_string(value, 10), "255");
    EXPECT_EQ(to_string(value, 11), "212");
    EXPECT_EQ(to_string(value, 12), "193");
    EXPECT_EQ(to_string(value, 13), "168");
    EXPECT_EQ(to_string(value, 14), "143");
    EXPECT_EQ(to_string(value, 15), "120");
    EXPECT_EQ(to_string(value, 16), "ff");
    EXPECT_EQ(to_string(value, 17), "f0");
    EXPECT_EQ(to_string(value, 18), "e3");
    EXPECT_EQ(to_string(value, 19), "d8");
    EXPECT_EQ(to_string(value, 20), "cf");
    EXPECT_EQ(to_string(value, 21), "c3");
    EXPECT_EQ(to_string(value, 22), "bd");
    EXPECT_EQ(to_string(value, 23), "b2");
    EXPECT_EQ(to_string(value, 24), "af");
    EXPECT_EQ(to_string(value, 25), "a5");
    EXPECT_EQ(to_string(value, 26), "9l");
    EXPECT_EQ(to_string(value, 27), "9c");
    EXPECT_EQ(to_string(value, 28), "93");
    EXPECT_EQ(to_string(value, 29), "8n");
    EXPECT_EQ(to_string(value, 30), "8f");
    EXPECT_EQ(to_string(value, 31), "87");
    EXPECT_EQ(to_string(value, 32), "7v");
    EXPECT_EQ(to_string(value, 33), "7o");
    EXPECT_EQ(to_string(value, 34), "7h");
    EXPECT_EQ(to_string(value, 35), "7a");
    EXPECT_EQ(to_string(value, 36), "73");
}

TEST(ToString, EveryBase_m255) {
    constexpr big_int value = -255;
    EXPECT_EQ(to_string(value, 2), "-11111111");
    EXPECT_EQ(to_string(value, 3), "-100110");
    EXPECT_EQ(to_string(value, 4), "-3333");
    EXPECT_EQ(to_string(value, 5), "-2010");
    EXPECT_EQ(to_string(value, 6), "-1103");
    EXPECT_EQ(to_string(value, 7), "-513");
    EXPECT_EQ(to_string(value, 8), "-377");
    EXPECT_EQ(to_string(value, 9), "-313");
    EXPECT_EQ(to_string(value, 10), "-255");
    EXPECT_EQ(to_string(value, 11), "-212");
    EXPECT_EQ(to_string(value, 12), "-193");
    EXPECT_EQ(to_string(value, 13), "-168");
    EXPECT_EQ(to_string(value, 14), "-143");
    EXPECT_EQ(to_string(value, 15), "-120");
    EXPECT_EQ(to_string(value, 16), "-ff");
    EXPECT_EQ(to_string(value, 17), "-f0");
    EXPECT_EQ(to_string(value, 18), "-e3");
    EXPECT_EQ(to_string(value, 19), "-d8");
    EXPECT_EQ(to_string(value, 20), "-cf");
    EXPECT_EQ(to_string(value, 21), "-c3");
    EXPECT_EQ(to_string(value, 22), "-bd");
    EXPECT_EQ(to_string(value, 23), "-b2");
    EXPECT_EQ(to_string(value, 24), "-af");
    EXPECT_EQ(to_string(value, 25), "-a5");
    EXPECT_EQ(to_string(value, 26), "-9l");
    EXPECT_EQ(to_string(value, 27), "-9c");
    EXPECT_EQ(to_string(value, 28), "-93");
    EXPECT_EQ(to_string(value, 29), "-8n");
    EXPECT_EQ(to_string(value, 30), "-8f");
    EXPECT_EQ(to_string(value, 31), "-87");
    EXPECT_EQ(to_string(value, 32), "-7v");
    EXPECT_EQ(to_string(value, 33), "-7o");
    EXPECT_EQ(to_string(value, 34), "-7h");
    EXPECT_EQ(to_string(value, 35), "-7a");
    EXPECT_EQ(to_string(value, 36), "-73");
}

TEST(ToString, EveryBase_0x1p64) {
    const big_int value = 1_n << 64;
    EXPECT_EQ(to_string(value, 2), "10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "11112220022122120101211020120210210211221");
    EXPECT_EQ(to_string(value, 4), "100000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "2214220303114400424121122431");
    EXPECT_EQ(to_string(value, 6), "3520522010102100444244424");
    EXPECT_EQ(to_string(value, 7), "45012021522523134134602");
    EXPECT_EQ(to_string(value, 8), "2000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "145808576354216723757");
    EXPECT_EQ(to_string(value, 10), "18446744073709551616");
    EXPECT_EQ(to_string(value, 11), "335500516a429071285");
    EXPECT_EQ(to_string(value, 12), "839365134a2a240714");
    EXPECT_EQ(to_string(value, 13), "219505a9511a867b73");
    EXPECT_EQ(to_string(value, 14), "8681049adb03db172");
    EXPECT_EQ(to_string(value, 15), "2c1d56b648c6cd111");
    EXPECT_EQ(to_string(value, 16), "10000000000000000");
    EXPECT_EQ(to_string(value, 17), "67979g60f5428011");
    EXPECT_EQ(to_string(value, 18), "2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_string(value, 19), "141c8786h1ccaagh");
    EXPECT_EQ(to_string(value, 20), "b53bjh07be4dj0g");
    EXPECT_EQ(to_string(value, 21), "5e8g4ggg7g56dig");
    EXPECT_EQ(to_string(value, 22), "2l4lf104353j8kg");
    EXPECT_EQ(to_string(value, 23), "1ddh88h2782i516");
    EXPECT_EQ(to_string(value, 24), "l12ee5fn0ji1ig");
    EXPECT_EQ(to_string(value, 25), "c9c336o0mlb7eg");
    EXPECT_EQ(to_string(value, 26), "7b7n2pcniokcgg");
    EXPECT_EQ(to_string(value, 27), "4eo8hfam6fllmp");
    EXPECT_EQ(to_string(value, 28), "2nc6j26l66rhog");
    EXPECT_EQ(to_string(value, 29), "1n3rsh11f098ro");
    EXPECT_EQ(to_string(value, 30), "14l9lkmo30o40g");
    EXPECT_EQ(to_string(value, 31), "nd075ib45k86g");
    EXPECT_EQ(to_string(value, 32), "g000000000000");
    EXPECT_EQ(to_string(value, 33), "b1w8p7j5q9r6g");
    EXPECT_EQ(to_string(value, 34), "7orp63sh4dphi");
    EXPECT_EQ(to_string(value, 35), "5g24a25twkwfg");
    EXPECT_EQ(to_string(value, 36), "3w5e11264sgsg");
}

TEST(ToString, EveryBase_m0x1p64) {
    const big_int value = -(1_n << 64);
    EXPECT_EQ(to_string(value, 2), "-10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "-11112220022122120101211020120210210211221");
    EXPECT_EQ(to_string(value, 4), "-100000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "-2214220303114400424121122431");
    EXPECT_EQ(to_string(value, 6), "-3520522010102100444244424");
    EXPECT_EQ(to_string(value, 7), "-45012021522523134134602");
    EXPECT_EQ(to_string(value, 8), "-2000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "-145808576354216723757");
    EXPECT_EQ(to_string(value, 10), "-18446744073709551616");
    EXPECT_EQ(to_string(value, 11), "-335500516a429071285");
    EXPECT_EQ(to_string(value, 12), "-839365134a2a240714");
    EXPECT_EQ(to_string(value, 13), "-219505a9511a867b73");
    EXPECT_EQ(to_string(value, 14), "-8681049adb03db172");
    EXPECT_EQ(to_string(value, 15), "-2c1d56b648c6cd111");
    EXPECT_EQ(to_string(value, 16), "-10000000000000000");
    EXPECT_EQ(to_string(value, 17), "-67979g60f5428011");
    EXPECT_EQ(to_string(value, 18), "-2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_string(value, 19), "-141c8786h1ccaagh");
    EXPECT_EQ(to_string(value, 20), "-b53bjh07be4dj0g");
    EXPECT_EQ(to_string(value, 21), "-5e8g4ggg7g56dig");
    EXPECT_EQ(to_string(value, 22), "-2l4lf104353j8kg");
    EXPECT_EQ(to_string(value, 23), "-1ddh88h2782i516");
    EXPECT_EQ(to_string(value, 24), "-l12ee5fn0ji1ig");
    EXPECT_EQ(to_string(value, 25), "-c9c336o0mlb7eg");
    EXPECT_EQ(to_string(value, 26), "-7b7n2pcniokcgg");
    EXPECT_EQ(to_string(value, 27), "-4eo8hfam6fllmp");
    EXPECT_EQ(to_string(value, 28), "-2nc6j26l66rhog");
    EXPECT_EQ(to_string(value, 29), "-1n3rsh11f098ro");
    EXPECT_EQ(to_string(value, 30), "-14l9lkmo30o40g");
    EXPECT_EQ(to_string(value, 31), "-nd075ib45k86g");
    EXPECT_EQ(to_string(value, 32), "-g000000000000");
    EXPECT_EQ(to_string(value, 33), "-b1w8p7j5q9r6g");
    EXPECT_EQ(to_string(value, 34), "-7orp63sh4dphi");
    EXPECT_EQ(to_string(value, 35), "-5g24a25twkwfg");
    EXPECT_EQ(to_string(value, 36), "-3w5e11264sgsg");
}

TEST(ToString, EveryBase_0x1p127m1) {
    const big_int value = (1_n << 127) - 1;
    EXPECT_EQ(to_string(value, 2), "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
    EXPECT_EQ(to_string(value, 3), "101100201022001010121000102002120122110122221010202000122201220121120010200022001");
    EXPECT_EQ(to_string(value, 4), "1333333333333333333333333333333333333333333333333333333333333333");
    EXPECT_EQ(to_string(value, 5), "3013030220323124042102424341431241221233040112312340402");
    EXPECT_EQ(to_string(value, 6), "11324454543055553250455021551551121442554522203131");
    EXPECT_EQ(to_string(value, 7), "1406241064412313155000336513424310163013142501");
    EXPECT_EQ(to_string(value, 8), "1777777777777777777777777777777777777777777");
    EXPECT_EQ(to_string(value, 9), "11321261117012076573587122018656546120261");
    EXPECT_EQ(to_string(value, 10), "170141183460469231731687303715884105727");
    EXPECT_EQ(to_string(value, 11), "555a8020989a11327710815513a946a188726");
    EXPECT_EQ(to_string(value, 12), "2a695925806818735399a37a20a31b3534a7");
    EXPECT_EQ(to_string(value, 13), "2373464c8a3cb25ba2b7c6382b2963bb71a");
    EXPECT_EQ(to_string(value, 14), "27c22d5b9734a1517bb1dc612904a79d71");
    EXPECT_EQ(to_string(value, 15), "3e2480b3404d8bb9bca3084369ba3e187");
    EXPECT_EQ(to_string(value, 16), "7fffffffffffffffffffffffffffffff");
    EXPECT_EQ(to_string(value, 17), "13d03cge4242f3e39f9dga60476a8098");
    EXPECT_EQ(to_string(value, 18), "3d51ddf66g5befc8e19d2607hc26e31");
    EXPECT_EQ(to_string(value, 19), "e09c09h6a4eihac8fchc875gf4di41");
    EXPECT_EQ(to_string(value, 20), "337d04g0ec2d918ac3j85180dfd467");
    EXPECT_EQ(to_string(value, 21), "g3b663ge01jk6cica417i3a75c601");
    EXPECT_EQ(to_string(value, 22), "48f5dj8i8eli87ecigb8g6egjhchh");
    EXPECT_EQ(to_string(value, 23), "162g6gam6d49ik37jk2mdcl41aj6h");
    EXPECT_EQ(to_string(value, 24), "95b794mjicl2m0cbfjnjnd2hdm57");
    EXPECT_EQ(to_string(value, 25), "31ffc3d7km5eej9ge7bdfk6d7j42");
    EXPECT_EQ(to_string(value, 26), "11gf2k68m8of9j9agmk61a6g4i0n");
    EXPECT_EQ(to_string(value, 27), "a9j813g0b2fhchp3k0hjogf3i81");
    EXPECT_EQ(to_string(value, 28), "40j3ek89l5h69q3q6jh2khn6dof");
    EXPECT_EQ(to_string(value, 29), "1hp3d3elmjg86isj584dklf2djq");
    EXPECT_EQ(to_string(value, 30), "k2chs8jacc5g718abkqgokq447");
    EXPECT_EQ(to_string(value, 31), "8q7cd4uoh31nng3aqs4edr0p73");
    EXPECT_EQ(to_string(value, 32), "3vvvvvvvvvvvvvvvvvvvvvvvvv");
    EXPECT_EQ(to_string(value, 33), "1s5acer890k5h07uh412q2mo0s");
    EXPECT_EQ(to_string(value, 34), "ttq3btpo06a5neskugckddplp");
    EXPECT_EQ(to_string(value, 35), "evh2xn5qudpcldl13shtyuixm");
    EXPECT_EQ(to_string(value, 36), "7ksyyizzkutudzbv8aqztecjj");
}

TEST(ToString, EveryBase_0x1p127) {
    const big_int value = 1_n << 127;
    EXPECT_EQ(to_string(value, 2), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "101100201022001010121000102002120122110122221010202000122201220121120010200022002");
    EXPECT_EQ(to_string(value, 4), "2000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "3013030220323124042102424341431241221233040112312340403");
    EXPECT_EQ(to_string(value, 6), "11324454543055553250455021551551121442554522203132");
    EXPECT_EQ(to_string(value, 7), "1406241064412313155000336513424310163013142502");
    EXPECT_EQ(to_string(value, 8), "2000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "11321261117012076573587122018656546120262");
    EXPECT_EQ(to_string(value, 10), "170141183460469231731687303715884105728");
    EXPECT_EQ(to_string(value, 11), "555a8020989a11327710815513a946a188727");
    EXPECT_EQ(to_string(value, 12), "2a695925806818735399a37a20a31b3534a8");
    EXPECT_EQ(to_string(value, 13), "2373464c8a3cb25ba2b7c6382b2963bb71b");
    EXPECT_EQ(to_string(value, 14), "27c22d5b9734a1517bb1dc612904a79d72");
    EXPECT_EQ(to_string(value, 15), "3e2480b3404d8bb9bca3084369ba3e188");
    EXPECT_EQ(to_string(value, 16), "80000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 17), "13d03cge4242f3e39f9dga60476a8099");
    EXPECT_EQ(to_string(value, 18), "3d51ddf66g5befc8e19d2607hc26e32");
    EXPECT_EQ(to_string(value, 19), "e09c09h6a4eihac8fchc875gf4di42");
    EXPECT_EQ(to_string(value, 20), "337d04g0ec2d918ac3j85180dfd468");
    EXPECT_EQ(to_string(value, 21), "g3b663ge01jk6cica417i3a75c602");
    EXPECT_EQ(to_string(value, 22), "48f5dj8i8eli87ecigb8g6egjhchi");
    EXPECT_EQ(to_string(value, 23), "162g6gam6d49ik37jk2mdcl41aj6i");
    EXPECT_EQ(to_string(value, 24), "95b794mjicl2m0cbfjnjnd2hdm58");
    EXPECT_EQ(to_string(value, 25), "31ffc3d7km5eej9ge7bdfk6d7j43");
    EXPECT_EQ(to_string(value, 26), "11gf2k68m8of9j9agmk61a6g4i0o");
    EXPECT_EQ(to_string(value, 27), "a9j813g0b2fhchp3k0hjogf3i82");
    EXPECT_EQ(to_string(value, 28), "40j3ek89l5h69q3q6jh2khn6dog");
    EXPECT_EQ(to_string(value, 29), "1hp3d3elmjg86isj584dklf2djr");
    EXPECT_EQ(to_string(value, 30), "k2chs8jacc5g718abkqgokq448");
    EXPECT_EQ(to_string(value, 31), "8q7cd4uoh31nng3aqs4edr0p74");
    EXPECT_EQ(to_string(value, 32), "40000000000000000000000000");
    EXPECT_EQ(to_string(value, 33), "1s5acer890k5h07uh412q2mo0t");
    EXPECT_EQ(to_string(value, 34), "ttq3btpo06a5neskugckddplq");
    EXPECT_EQ(to_string(value, 35), "evh2xn5qudpcldl13shtyuixn");
    EXPECT_EQ(to_string(value, 36), "7ksyyizzkutudzbv8aqztecjk");
}

TEST(ToString, EveryBase_0x1p200) {
    const big_int value = 1_n << 200;
    EXPECT_EQ(to_string(value, 2), "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "1020010020011122222010012002211022002220100012201102121001102021011020010001110010111202101022110112000001121101020022221002011");
    EXPECT_EQ(to_string(value, 4), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "111020132102420112021343001342032333120043341314112104422342034202402044234211314121001");
    EXPECT_EQ(to_string(value, 6), "153532332401011220403425141024425043540104001011245043050435125240351024502304");
    EXPECT_EQ(to_string(value, 7), "141246066533632643213232344050606053061443446006544361632102630555343054");
    EXPECT_EQ(to_string(value, 8), "4000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "1203204588105084262810181377042234203043114671273460047336287064");
    EXPECT_EQ(to_string(value, 10), "1606938044258990275541962092341162602522202993782792835301376");
    EXPECT_EQ(to_string(value, 11), "702a695236a26a175662a9a2048793aa12225aa884798921aa43640211");
    EXPECT_EQ(to_string(value, 12), "711a44b68618019a2aa233ab1b3a354234329741725b37749147a994");
    EXPECT_EQ(to_string(value, 13), "118c655a2aa24b3c3c25c8249269882811a1367a2b7c5b60b930499");
    EXPECT_EQ(to_string(value, 14), "409840b05276d868d7a284750dd66851c40284971c8d464973c64");
    EXPECT_EQ(to_string(value, 15), "1a306ea66c3a28ae9e2acb154bda45d3d33bcc419cb2a50c1d51");
    EXPECT_EQ(to_string(value, 16), "100000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 17), "dg16f38eebf846a55ce10e44gf26g5fb8eg148ad8b5g84f11");
    EXPECT_EQ(to_string(value, 18), "g2ceg0g4b8aa15g1hh662aacbhbd5hab7287a54c7b3d8dc4");
    EXPECT_EQ(to_string(value, 19), "15359dfh0ccef9hgch451hg8fdb3c1e569cce46a98c2efi4");
    EXPECT_EQ(to_string(value, 20), "25d8f83ed4e8idac0b962jghhebge99jd0f5bc06i10cd8g");
    EXPECT_EQ(to_string(value, 21), "51fdh8f51670kddg476b2ih0ai1j9a55j48dk27be7c0i4");
    EXPECT_EQ(to_string(value, 22), "dh5ih81f0403j656bd7i9ch363icjeb78gi7f4cki2h0c");
    EXPECT_EQ(to_string(value, 23), "1lj985917fa09i739cl96kl9gc1978d7kbelg1dkcfde4");
    EXPECT_EQ(to_string(value, 24), "74f57n8nnacman4gga8nb9dbemn4ifbe2bcfcjh0bkag");
    EXPECT_EQ(to_string(value, 25), "1621h5ea6abjf1jahiga4ilgl75ocjajae24mjb89751");
    EXPECT_EQ(to_string(value, 26), "5pg9dj501bcj73ompgoo20j8215ldjpmf56eo95mkhm");
    EXPECT_EQ(to_string(value, 27), "16364hq352m82o95jbg1b74631c3dka8ce01ga68p24");
    EXPECT_EQ(to_string(value, 28), "7clja95f6j5hiaikcg50ib2433rrikejonpi0896h4");
    EXPECT_EQ(to_string(value, 29), "1m8gen8rkhnnhs0a3qf5fajn361kf4ae06pmrf91pg");
    EXPECT_EQ(to_string(value, 30), "d6fm5miet05hlefp0n0d9chmptojk99cf225jf72g");
    EXPECT_EQ(to_string(value, 31), "3hboprqof3jja07q3c5qmnja0tuc0chm0fffbg691");
    EXPECT_EQ(to_string(value, 32), "10000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 33), "9l10o238v9in0ega68m2g7rv989opaac2s0bajq1");
    EXPECT_EQ(to_string(value, 34), "309k9mmbxji2j3ufnp7tpu8gi73a8tj2u823nxhi");
    EXPECT_EQ(to_string(value, 35), "xysp95s4kpn5qxodmbh36dg7i7pnpt8lg7ur2jb");
    EXPECT_EQ(to_string(value, 36), "bnklg118comha6gqury14067gur54n8won6guf4");
}

TEST(ToString, EveryBase_1234567890123456789012345678901234567890112233445566778899) {
    const big_int value = 1234567890123456789012345678901234567890112233445566778899_n;
    EXPECT_EQ(to_string(value, 2), "1100100101100101111101011111010010101011010111010110000000010101011010001111000011110010000101101011001110110000010011101110011110001010001000010010010011111010010110111101010000111000010011");
    EXPECT_EQ(to_string(value, 3), "200112211110102021212200222012120002011210101222201200002000101122212221111212022122211110201211211110122020212122000000");
    EXPECT_EQ(to_string(value, 4), "30211211331133102223113112000111122033003302011223032300103232132022020102103322112331100320103");
    EXPECT_EQ(to_string(value, 5), "2443030312003032423221132100412422141210014133224140342114014311130233010043411044");
    EXPECT_EQ(to_string(value, 6), "15334321411452312252221453151303140525001150011340242051412445054523101043");
    EXPECT_EQ(to_string(value, 7), "26440656114205151522424022616202405154534130314565642632315310540442");
    EXPECT_EQ(to_string(value, 8), "1445457537225327260025321703620553166023563612102223722675207023");
    EXPECT_EQ(to_string(value, 9), "615743367780865502153358650060348787455278743654743566778000");
    EXPECT_EQ(to_string(value, 10), "1234567890123456789012345678901234567890112233445566778899");
    EXPECT_EQ(to_string(value, 11), "72017331781188537226214691a6861169a4a5a34a5556533343982");
    EXPECT_EQ(to_string(value, 12), "95072955358a5a5958917776724801051a7700981b01076971783");
    EXPECT_EQ(to_string(value, 13), "1ba4268b1cba8618c75baba39769b2424353a8783013593510b9");
    EXPECT_EQ(to_string(value, 14), "877010525452793a66c39bb601492084683853c6222b250d59");
    EXPECT_EQ(to_string(value, 15), "455312ee1037c1d82b7821ba032748dadb6411716c18b1d69");
    EXPECT_EQ(to_string(value, 16), "32597d7d2ad758055a3c3c85acec13b9e288493e96f50e13");
    EXPECT_EQ(to_string(value, 17), "31aeef86cef56442g4220366g988a26bb8f760e3078b9d1");
    EXPECT_EQ(to_string(value, 18), "406a0h99fh5dfa2d0ahd0036d8g8bf8h2d2dg39c62d6d9");
    EXPECT_EQ(to_string(value, 19), "6d7525g4h9ea7g0a98g9afa5g3i40d07f41507ac247h1");
    EXPECT_EQ(to_string(value, 20), "e0e35i40eg2286j8hg32bf8jb3f44aegicfij6jc774j");
    EXPECT_EQ(to_string(value, 21), "1f3a7ac32bec3fh1dei96hkfbhj2i4ck71f454g516f9");
    EXPECT_EQ(to_string(value, 22), "52gjd917fkl2la9e68ac141jc2fb0330h99h0k0l29d");
    EXPECT_EQ(to_string(value, 23), "i543gdle859f40efkheafjm1870d97bl1id8dghdmi");
    EXPECT_EQ(to_string(value, 24), "349bd97fh5ig7dc36d7817dhbe7mh8e40b74a14gm3");
    EXPECT_EQ(to_string(value, 25), "en3370fhmhb8b0lec9751liclkjb91n682i10nl5o");
    EXPECT_EQ(to_string(value, 26), "32lckgdd55kpdmhii2nhjebd41426i262d1449mp9");
    EXPECT_EQ(to_string(value, 27), "iemcb7niq5f24laqji20ahnpdn8hmcjmmch6nh00");
    EXPECT_EQ(to_string(value, 28), "4dl0bb0blfaoipc8f7elhr5d7p0n28iob4rhmo9n");
    EXPECT_EQ(to_string(value, 29), "14446650lm8disc37ohldq5f36dn51dbqrjmclll");
    EXPECT_EQ(to_string(value, 30), "9457qs5cc4cjgsslml19i074rnl82ohein77m39");
    EXPECT_EQ(to_string(value, 31), "2jfat1a95mrgd7ej9ducbir25ouseir6e53el5p");
    EXPECT_EQ(to_string(value, 32), "p5ivbt5bblg1aq7gu8bb7c2esu52297qbfa3gj");
    EXPECT_EQ(to_string(value, 33), "822lnwm95hb4bj95ulwgpdln3q8s81okhbjn2o");
    EXPECT_EQ(to_string(value, 34), "2msi9cfaccqunvoom5m362coqrxcmttlqr3jf1");
    EXPECT_EQ(to_string(value, 35), "vyrcqwj3tp1erk7nkx7xw4xme63wchx6ojh99");
    EXPECT_EQ(to_string(value, 36), "blrdpawjeweaxb93a5h07u19ogcvpgt5tf66r");
}

TEST(ToWString, EveryBase_255) {
    constexpr big_int value = 255;
    EXPECT_EQ(to_wstring(value, 2), L"11111111");
    EXPECT_EQ(to_wstring(value, 3), L"100110");
    EXPECT_EQ(to_wstring(value, 4), L"3333");
    EXPECT_EQ(to_wstring(value, 5), L"2010");
    EXPECT_EQ(to_wstring(value, 6), L"1103");
    EXPECT_EQ(to_wstring(value, 7), L"513");
    EXPECT_EQ(to_wstring(value, 8), L"377");
    EXPECT_EQ(to_wstring(value, 9), L"313");
    EXPECT_EQ(to_wstring(value, 10), L"255");
    EXPECT_EQ(to_wstring(value, 11), L"212");
    EXPECT_EQ(to_wstring(value, 12), L"193");
    EXPECT_EQ(to_wstring(value, 13), L"168");
    EXPECT_EQ(to_wstring(value, 14), L"143");
    EXPECT_EQ(to_wstring(value, 15), L"120");
    EXPECT_EQ(to_wstring(value, 16), L"ff");
    EXPECT_EQ(to_wstring(value, 17), L"f0");
    EXPECT_EQ(to_wstring(value, 18), L"e3");
    EXPECT_EQ(to_wstring(value, 19), L"d8");
    EXPECT_EQ(to_wstring(value, 20), L"cf");
    EXPECT_EQ(to_wstring(value, 21), L"c3");
    EXPECT_EQ(to_wstring(value, 22), L"bd");
    EXPECT_EQ(to_wstring(value, 23), L"b2");
    EXPECT_EQ(to_wstring(value, 24), L"af");
    EXPECT_EQ(to_wstring(value, 25), L"a5");
    EXPECT_EQ(to_wstring(value, 26), L"9l");
    EXPECT_EQ(to_wstring(value, 27), L"9c");
    EXPECT_EQ(to_wstring(value, 28), L"93");
    EXPECT_EQ(to_wstring(value, 29), L"8n");
    EXPECT_EQ(to_wstring(value, 30), L"8f");
    EXPECT_EQ(to_wstring(value, 31), L"87");
    EXPECT_EQ(to_wstring(value, 32), L"7v");
    EXPECT_EQ(to_wstring(value, 33), L"7o");
    EXPECT_EQ(to_wstring(value, 34), L"7h");
    EXPECT_EQ(to_wstring(value, 35), L"7a");
    EXPECT_EQ(to_wstring(value, 36), L"73");
}

TEST(ToWString, EveryBase_m255) {
    constexpr big_int value = -255;
    EXPECT_EQ(to_wstring(value, 2), L"-11111111");
    EXPECT_EQ(to_wstring(value, 3), L"-100110");
    EXPECT_EQ(to_wstring(value, 4), L"-3333");
    EXPECT_EQ(to_wstring(value, 5), L"-2010");
    EXPECT_EQ(to_wstring(value, 6), L"-1103");
    EXPECT_EQ(to_wstring(value, 7), L"-513");
    EXPECT_EQ(to_wstring(value, 8), L"-377");
    EXPECT_EQ(to_wstring(value, 9), L"-313");
    EXPECT_EQ(to_wstring(value, 10), L"-255");
    EXPECT_EQ(to_wstring(value, 11), L"-212");
    EXPECT_EQ(to_wstring(value, 12), L"-193");
    EXPECT_EQ(to_wstring(value, 13), L"-168");
    EXPECT_EQ(to_wstring(value, 14), L"-143");
    EXPECT_EQ(to_wstring(value, 15), L"-120");
    EXPECT_EQ(to_wstring(value, 16), L"-ff");
    EXPECT_EQ(to_wstring(value, 17), L"-f0");
    EXPECT_EQ(to_wstring(value, 18), L"-e3");
    EXPECT_EQ(to_wstring(value, 19), L"-d8");
    EXPECT_EQ(to_wstring(value, 20), L"-cf");
    EXPECT_EQ(to_wstring(value, 21), L"-c3");
    EXPECT_EQ(to_wstring(value, 22), L"-bd");
    EXPECT_EQ(to_wstring(value, 23), L"-b2");
    EXPECT_EQ(to_wstring(value, 24), L"-af");
    EXPECT_EQ(to_wstring(value, 25), L"-a5");
    EXPECT_EQ(to_wstring(value, 26), L"-9l");
    EXPECT_EQ(to_wstring(value, 27), L"-9c");
    EXPECT_EQ(to_wstring(value, 28), L"-93");
    EXPECT_EQ(to_wstring(value, 29), L"-8n");
    EXPECT_EQ(to_wstring(value, 30), L"-8f");
    EXPECT_EQ(to_wstring(value, 31), L"-87");
    EXPECT_EQ(to_wstring(value, 32), L"-7v");
    EXPECT_EQ(to_wstring(value, 33), L"-7o");
    EXPECT_EQ(to_wstring(value, 34), L"-7h");
    EXPECT_EQ(to_wstring(value, 35), L"-7a");
    EXPECT_EQ(to_wstring(value, 36), L"-73");
}

TEST(ToWString, EveryBase_0x1p64) {
    const big_int value = 1_n << 64;
    EXPECT_EQ(to_wstring(value, 2), L"10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 3), L"11112220022122120101211020120210210211221");
    EXPECT_EQ(to_wstring(value, 4), L"100000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 5), L"2214220303114400424121122431");
    EXPECT_EQ(to_wstring(value, 6), L"3520522010102100444244424");
    EXPECT_EQ(to_wstring(value, 7), L"45012021522523134134602");
    EXPECT_EQ(to_wstring(value, 8), L"2000000000000000000000");
    EXPECT_EQ(to_wstring(value, 9), L"145808576354216723757");
    EXPECT_EQ(to_wstring(value, 10), L"18446744073709551616");
    EXPECT_EQ(to_wstring(value, 11), L"335500516a429071285");
    EXPECT_EQ(to_wstring(value, 12), L"839365134a2a240714");
    EXPECT_EQ(to_wstring(value, 13), L"219505a9511a867b73");
    EXPECT_EQ(to_wstring(value, 14), L"8681049adb03db172");
    EXPECT_EQ(to_wstring(value, 15), L"2c1d56b648c6cd111");
    EXPECT_EQ(to_wstring(value, 16), L"10000000000000000");
    EXPECT_EQ(to_wstring(value, 17), L"67979g60f5428011");
    EXPECT_EQ(to_wstring(value, 18), L"2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_wstring(value, 19), L"141c8786h1ccaagh");
    EXPECT_EQ(to_wstring(value, 20), L"b53bjh07be4dj0g");
    EXPECT_EQ(to_wstring(value, 21), L"5e8g4ggg7g56dig");
    EXPECT_EQ(to_wstring(value, 22), L"2l4lf104353j8kg");
    EXPECT_EQ(to_wstring(value, 23), L"1ddh88h2782i516");
    EXPECT_EQ(to_wstring(value, 24), L"l12ee5fn0ji1ig");
    EXPECT_EQ(to_wstring(value, 25), L"c9c336o0mlb7eg");
    EXPECT_EQ(to_wstring(value, 26), L"7b7n2pcniokcgg");
    EXPECT_EQ(to_wstring(value, 27), L"4eo8hfam6fllmp");
    EXPECT_EQ(to_wstring(value, 28), L"2nc6j26l66rhog");
    EXPECT_EQ(to_wstring(value, 29), L"1n3rsh11f098ro");
    EXPECT_EQ(to_wstring(value, 30), L"14l9lkmo30o40g");
    EXPECT_EQ(to_wstring(value, 31), L"nd075ib45k86g");
    EXPECT_EQ(to_wstring(value, 32), L"g000000000000");
    EXPECT_EQ(to_wstring(value, 33), L"b1w8p7j5q9r6g");
    EXPECT_EQ(to_wstring(value, 34), L"7orp63sh4dphi");
    EXPECT_EQ(to_wstring(value, 35), L"5g24a25twkwfg");
    EXPECT_EQ(to_wstring(value, 36), L"3w5e11264sgsg");
}

TEST(ToWString, EveryBase_m0x1p64) {
    const big_int value = -(1_n << 64);
    EXPECT_EQ(to_wstring(value, 2), L"-10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 3), L"-11112220022122120101211020120210210211221");
    EXPECT_EQ(to_wstring(value, 4), L"-100000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 5), L"-2214220303114400424121122431");
    EXPECT_EQ(to_wstring(value, 6), L"-3520522010102100444244424");
    EXPECT_EQ(to_wstring(value, 7), L"-45012021522523134134602");
    EXPECT_EQ(to_wstring(value, 8), L"-2000000000000000000000");
    EXPECT_EQ(to_wstring(value, 9), L"-145808576354216723757");
    EXPECT_EQ(to_wstring(value, 10), L"-18446744073709551616");
    EXPECT_EQ(to_wstring(value, 11), L"-335500516a429071285");
    EXPECT_EQ(to_wstring(value, 12), L"-839365134a2a240714");
    EXPECT_EQ(to_wstring(value, 13), L"-219505a9511a867b73");
    EXPECT_EQ(to_wstring(value, 14), L"-8681049adb03db172");
    EXPECT_EQ(to_wstring(value, 15), L"-2c1d56b648c6cd111");
    EXPECT_EQ(to_wstring(value, 16), L"-10000000000000000");
    EXPECT_EQ(to_wstring(value, 17), L"-67979g60f5428011");
    EXPECT_EQ(to_wstring(value, 18), L"-2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_wstring(value, 19), L"-141c8786h1ccaagh");
    EXPECT_EQ(to_wstring(value, 20), L"-b53bjh07be4dj0g");
    EXPECT_EQ(to_wstring(value, 21), L"-5e8g4ggg7g56dig");
    EXPECT_EQ(to_wstring(value, 22), L"-2l4lf104353j8kg");
    EXPECT_EQ(to_wstring(value, 23), L"-1ddh88h2782i516");
    EXPECT_EQ(to_wstring(value, 24), L"-l12ee5fn0ji1ig");
    EXPECT_EQ(to_wstring(value, 25), L"-c9c336o0mlb7eg");
    EXPECT_EQ(to_wstring(value, 26), L"-7b7n2pcniokcgg");
    EXPECT_EQ(to_wstring(value, 27), L"-4eo8hfam6fllmp");
    EXPECT_EQ(to_wstring(value, 28), L"-2nc6j26l66rhog");
    EXPECT_EQ(to_wstring(value, 29), L"-1n3rsh11f098ro");
    EXPECT_EQ(to_wstring(value, 30), L"-14l9lkmo30o40g");
    EXPECT_EQ(to_wstring(value, 31), L"-nd075ib45k86g");
    EXPECT_EQ(to_wstring(value, 32), L"-g000000000000");
    EXPECT_EQ(to_wstring(value, 33), L"-b1w8p7j5q9r6g");
    EXPECT_EQ(to_wstring(value, 34), L"-7orp63sh4dphi");
    EXPECT_EQ(to_wstring(value, 35), L"-5g24a25twkwfg");
    EXPECT_EQ(to_wstring(value, 36), L"-3w5e11264sgsg");
}

TEST(ToWString, EveryBase_0x1p127m1) {
    const big_int value = (1_n << 127) - 1;
    EXPECT_EQ(to_wstring(value, 2), L"1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
    EXPECT_EQ(to_wstring(value, 3), L"101100201022001010121000102002120122110122221010202000122201220121120010200022001");
    EXPECT_EQ(to_wstring(value, 4), L"1333333333333333333333333333333333333333333333333333333333333333");
    EXPECT_EQ(to_wstring(value, 5), L"3013030220323124042102424341431241221233040112312340402");
    EXPECT_EQ(to_wstring(value, 6), L"11324454543055553250455021551551121442554522203131");
    EXPECT_EQ(to_wstring(value, 7), L"1406241064412313155000336513424310163013142501");
    EXPECT_EQ(to_wstring(value, 8), L"1777777777777777777777777777777777777777777");
    EXPECT_EQ(to_wstring(value, 9), L"11321261117012076573587122018656546120261");
    EXPECT_EQ(to_wstring(value, 10), L"170141183460469231731687303715884105727");
    EXPECT_EQ(to_wstring(value, 11), L"555a8020989a11327710815513a946a188726");
    EXPECT_EQ(to_wstring(value, 12), L"2a695925806818735399a37a20a31b3534a7");
    EXPECT_EQ(to_wstring(value, 13), L"2373464c8a3cb25ba2b7c6382b2963bb71a");
    EXPECT_EQ(to_wstring(value, 14), L"27c22d5b9734a1517bb1dc612904a79d71");
    EXPECT_EQ(to_wstring(value, 15), L"3e2480b3404d8bb9bca3084369ba3e187");
    EXPECT_EQ(to_wstring(value, 16), L"7fffffffffffffffffffffffffffffff");
    EXPECT_EQ(to_wstring(value, 17), L"13d03cge4242f3e39f9dga60476a8098");
    EXPECT_EQ(to_wstring(value, 18), L"3d51ddf66g5befc8e19d2607hc26e31");
    EXPECT_EQ(to_wstring(value, 19), L"e09c09h6a4eihac8fchc875gf4di41");
    EXPECT_EQ(to_wstring(value, 20), L"337d04g0ec2d918ac3j85180dfd467");
    EXPECT_EQ(to_wstring(value, 21), L"g3b663ge01jk6cica417i3a75c601");
    EXPECT_EQ(to_wstring(value, 22), L"48f5dj8i8eli87ecigb8g6egjhchh");
    EXPECT_EQ(to_wstring(value, 23), L"162g6gam6d49ik37jk2mdcl41aj6h");
    EXPECT_EQ(to_wstring(value, 24), L"95b794mjicl2m0cbfjnjnd2hdm57");
    EXPECT_EQ(to_wstring(value, 25), L"31ffc3d7km5eej9ge7bdfk6d7j42");
    EXPECT_EQ(to_wstring(value, 26), L"11gf2k68m8of9j9agmk61a6g4i0n");
    EXPECT_EQ(to_wstring(value, 27), L"a9j813g0b2fhchp3k0hjogf3i81");
    EXPECT_EQ(to_wstring(value, 28), L"40j3ek89l5h69q3q6jh2khn6dof");
    EXPECT_EQ(to_wstring(value, 29), L"1hp3d3elmjg86isj584dklf2djq");
    EXPECT_EQ(to_wstring(value, 30), L"k2chs8jacc5g718abkqgokq447");
    EXPECT_EQ(to_wstring(value, 31), L"8q7cd4uoh31nng3aqs4edr0p73");
    EXPECT_EQ(to_wstring(value, 32), L"3vvvvvvvvvvvvvvvvvvvvvvvvv");
    EXPECT_EQ(to_wstring(value, 33), L"1s5acer890k5h07uh412q2mo0s");
    EXPECT_EQ(to_wstring(value, 34), L"ttq3btpo06a5neskugckddplp");
    EXPECT_EQ(to_wstring(value, 35), L"evh2xn5qudpcldl13shtyuixm");
    EXPECT_EQ(to_wstring(value, 36), L"7ksyyizzkutudzbv8aqztecjj");
}

TEST(ToWString, EveryBase_0x1p127) {
    const big_int value = 1_n << 127;
    EXPECT_EQ(to_wstring(value, 2), L"10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 3), L"101100201022001010121000102002120122110122221010202000122201220121120010200022002");
    EXPECT_EQ(to_wstring(value, 4), L"2000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 5), L"3013030220323124042102424341431241221233040112312340403");
    EXPECT_EQ(to_wstring(value, 6), L"11324454543055553250455021551551121442554522203132");
    EXPECT_EQ(to_wstring(value, 7), L"1406241064412313155000336513424310163013142502");
    EXPECT_EQ(to_wstring(value, 8), L"2000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 9), L"11321261117012076573587122018656546120262");
    EXPECT_EQ(to_wstring(value, 10), L"170141183460469231731687303715884105728");
    EXPECT_EQ(to_wstring(value, 11), L"555a8020989a11327710815513a946a188727");
    EXPECT_EQ(to_wstring(value, 12), L"2a695925806818735399a37a20a31b3534a8");
    EXPECT_EQ(to_wstring(value, 13), L"2373464c8a3cb25ba2b7c6382b2963bb71b");
    EXPECT_EQ(to_wstring(value, 14), L"27c22d5b9734a1517bb1dc612904a79d72");
    EXPECT_EQ(to_wstring(value, 15), L"3e2480b3404d8bb9bca3084369ba3e188");
    EXPECT_EQ(to_wstring(value, 16), L"80000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 17), L"13d03cge4242f3e39f9dga60476a8099");
    EXPECT_EQ(to_wstring(value, 18), L"3d51ddf66g5befc8e19d2607hc26e32");
    EXPECT_EQ(to_wstring(value, 19), L"e09c09h6a4eihac8fchc875gf4di42");
    EXPECT_EQ(to_wstring(value, 20), L"337d04g0ec2d918ac3j85180dfd468");
    EXPECT_EQ(to_wstring(value, 21), L"g3b663ge01jk6cica417i3a75c602");
    EXPECT_EQ(to_wstring(value, 22), L"48f5dj8i8eli87ecigb8g6egjhchi");
    EXPECT_EQ(to_wstring(value, 23), L"162g6gam6d49ik37jk2mdcl41aj6i");
    EXPECT_EQ(to_wstring(value, 24), L"95b794mjicl2m0cbfjnjnd2hdm58");
    EXPECT_EQ(to_wstring(value, 25), L"31ffc3d7km5eej9ge7bdfk6d7j43");
    EXPECT_EQ(to_wstring(value, 26), L"11gf2k68m8of9j9agmk61a6g4i0o");
    EXPECT_EQ(to_wstring(value, 27), L"a9j813g0b2fhchp3k0hjogf3i82");
    EXPECT_EQ(to_wstring(value, 28), L"40j3ek89l5h69q3q6jh2khn6dog");
    EXPECT_EQ(to_wstring(value, 29), L"1hp3d3elmjg86isj584dklf2djr");
    EXPECT_EQ(to_wstring(value, 30), L"k2chs8jacc5g718abkqgokq448");
    EXPECT_EQ(to_wstring(value, 31), L"8q7cd4uoh31nng3aqs4edr0p74");
    EXPECT_EQ(to_wstring(value, 32), L"40000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 33), L"1s5acer890k5h07uh412q2mo0t");
    EXPECT_EQ(to_wstring(value, 34), L"ttq3btpo06a5neskugckddplq");
    EXPECT_EQ(to_wstring(value, 35), L"evh2xn5qudpcldl13shtyuixn");
    EXPECT_EQ(to_wstring(value, 36), L"7ksyyizzkutudzbv8aqztecjk");
}

TEST(ToWString, EveryBase_0x1p200) {
    const big_int value = 1_n << 200;
    EXPECT_EQ(to_wstring(value, 2), L"100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 3), L"1020010020011122222010012002211022002220100012201102121001102021011020010001110010111202101022110112000001121101020022221002011");
    EXPECT_EQ(to_wstring(value, 4), L"10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 5), L"111020132102420112021343001342032333120043341314112104422342034202402044234211314121001");
    EXPECT_EQ(to_wstring(value, 6), L"153532332401011220403425141024425043540104001011245043050435125240351024502304");
    EXPECT_EQ(to_wstring(value, 7), L"141246066533632643213232344050606053061443446006544361632102630555343054");
    EXPECT_EQ(to_wstring(value, 8), L"4000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 9), L"1203204588105084262810181377042234203043114671273460047336287064");
    EXPECT_EQ(to_wstring(value, 10), L"1606938044258990275541962092341162602522202993782792835301376");
    EXPECT_EQ(to_wstring(value, 11), L"702a695236a26a175662a9a2048793aa12225aa884798921aa43640211");
    EXPECT_EQ(to_wstring(value, 12), L"711a44b68618019a2aa233ab1b3a354234329741725b37749147a994");
    EXPECT_EQ(to_wstring(value, 13), L"118c655a2aa24b3c3c25c8249269882811a1367a2b7c5b60b930499");
    EXPECT_EQ(to_wstring(value, 14), L"409840b05276d868d7a284750dd66851c40284971c8d464973c64");
    EXPECT_EQ(to_wstring(value, 15), L"1a306ea66c3a28ae9e2acb154bda45d3d33bcc419cb2a50c1d51");
    EXPECT_EQ(to_wstring(value, 16), L"100000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 17), L"dg16f38eebf846a55ce10e44gf26g5fb8eg148ad8b5g84f11");
    EXPECT_EQ(to_wstring(value, 18), L"g2ceg0g4b8aa15g1hh662aacbhbd5hab7287a54c7b3d8dc4");
    EXPECT_EQ(to_wstring(value, 19), L"15359dfh0ccef9hgch451hg8fdb3c1e569cce46a98c2efi4");
    EXPECT_EQ(to_wstring(value, 20), L"25d8f83ed4e8idac0b962jghhebge99jd0f5bc06i10cd8g");
    EXPECT_EQ(to_wstring(value, 21), L"51fdh8f51670kddg476b2ih0ai1j9a55j48dk27be7c0i4");
    EXPECT_EQ(to_wstring(value, 22), L"dh5ih81f0403j656bd7i9ch363icjeb78gi7f4cki2h0c");
    EXPECT_EQ(to_wstring(value, 23), L"1lj985917fa09i739cl96kl9gc1978d7kbelg1dkcfde4");
    EXPECT_EQ(to_wstring(value, 24), L"74f57n8nnacman4gga8nb9dbemn4ifbe2bcfcjh0bkag");
    EXPECT_EQ(to_wstring(value, 25), L"1621h5ea6abjf1jahiga4ilgl75ocjajae24mjb89751");
    EXPECT_EQ(to_wstring(value, 26), L"5pg9dj501bcj73ompgoo20j8215ldjpmf56eo95mkhm");
    EXPECT_EQ(to_wstring(value, 27), L"16364hq352m82o95jbg1b74631c3dka8ce01ga68p24");
    EXPECT_EQ(to_wstring(value, 28), L"7clja95f6j5hiaikcg50ib2433rrikejonpi0896h4");
    EXPECT_EQ(to_wstring(value, 29), L"1m8gen8rkhnnhs0a3qf5fajn361kf4ae06pmrf91pg");
    EXPECT_EQ(to_wstring(value, 30), L"d6fm5miet05hlefp0n0d9chmptojk99cf225jf72g");
    EXPECT_EQ(to_wstring(value, 31), L"3hboprqof3jja07q3c5qmnja0tuc0chm0fffbg691");
    EXPECT_EQ(to_wstring(value, 32), L"10000000000000000000000000000000000000000");
    EXPECT_EQ(to_wstring(value, 33), L"9l10o238v9in0ega68m2g7rv989opaac2s0bajq1");
    EXPECT_EQ(to_wstring(value, 34), L"309k9mmbxji2j3ufnp7tpu8gi73a8tj2u823nxhi");
    EXPECT_EQ(to_wstring(value, 35), L"xysp95s4kpn5qxodmbh36dg7i7pnpt8lg7ur2jb");
    EXPECT_EQ(to_wstring(value, 36), L"bnklg118comha6gqury14067gur54n8won6guf4");
}

TEST(ToWString, EveryBase_1234567890123456789012345678901234567890112233445566778899) {
    const big_int value = 1234567890123456789012345678901234567890112233445566778899_n;
    EXPECT_EQ(to_wstring(value, 2), L"1100100101100101111101011111010010101011010111010110000000010101011010001111000011110010000101101011001110110000010011101110011110001010001000010010010011111010010110111101010000111000010011");
    EXPECT_EQ(to_wstring(value, 3), L"200112211110102021212200222012120002011210101222201200002000101122212221111212022122211110201211211110122020212122000000");
    EXPECT_EQ(to_wstring(value, 4), L"30211211331133102223113112000111122033003302011223032300103232132022020102103322112331100320103");
    EXPECT_EQ(to_wstring(value, 5), L"2443030312003032423221132100412422141210014133224140342114014311130233010043411044");
    EXPECT_EQ(to_wstring(value, 6), L"15334321411452312252221453151303140525001150011340242051412445054523101043");
    EXPECT_EQ(to_wstring(value, 7), L"26440656114205151522424022616202405154534130314565642632315310540442");
    EXPECT_EQ(to_wstring(value, 8), L"1445457537225327260025321703620553166023563612102223722675207023");
    EXPECT_EQ(to_wstring(value, 9), L"615743367780865502153358650060348787455278743654743566778000");
    EXPECT_EQ(to_wstring(value, 10), L"1234567890123456789012345678901234567890112233445566778899");
    EXPECT_EQ(to_wstring(value, 11), L"72017331781188537226214691a6861169a4a5a34a5556533343982");
    EXPECT_EQ(to_wstring(value, 12), L"95072955358a5a5958917776724801051a7700981b01076971783");
    EXPECT_EQ(to_wstring(value, 13), L"1ba4268b1cba8618c75baba39769b2424353a8783013593510b9");
    EXPECT_EQ(to_wstring(value, 14), L"877010525452793a66c39bb601492084683853c6222b250d59");
    EXPECT_EQ(to_wstring(value, 15), L"455312ee1037c1d82b7821ba032748dadb6411716c18b1d69");
    EXPECT_EQ(to_wstring(value, 16), L"32597d7d2ad758055a3c3c85acec13b9e288493e96f50e13");
    EXPECT_EQ(to_wstring(value, 17), L"31aeef86cef56442g4220366g988a26bb8f760e3078b9d1");
    EXPECT_EQ(to_wstring(value, 18), L"406a0h99fh5dfa2d0ahd0036d8g8bf8h2d2dg39c62d6d9");
    EXPECT_EQ(to_wstring(value, 19), L"6d7525g4h9ea7g0a98g9afa5g3i40d07f41507ac247h1");
    EXPECT_EQ(to_wstring(value, 20), L"e0e35i40eg2286j8hg32bf8jb3f44aegicfij6jc774j");
    EXPECT_EQ(to_wstring(value, 21), L"1f3a7ac32bec3fh1dei96hkfbhj2i4ck71f454g516f9");
    EXPECT_EQ(to_wstring(value, 22), L"52gjd917fkl2la9e68ac141jc2fb0330h99h0k0l29d");
    EXPECT_EQ(to_wstring(value, 23), L"i543gdle859f40efkheafjm1870d97bl1id8dghdmi");
    EXPECT_EQ(to_wstring(value, 24), L"349bd97fh5ig7dc36d7817dhbe7mh8e40b74a14gm3");
    EXPECT_EQ(to_wstring(value, 25), L"en3370fhmhb8b0lec9751liclkjb91n682i10nl5o");
    EXPECT_EQ(to_wstring(value, 26), L"32lckgdd55kpdmhii2nhjebd41426i262d1449mp9");
    EXPECT_EQ(to_wstring(value, 27), L"iemcb7niq5f24laqji20ahnpdn8hmcjmmch6nh00");
    EXPECT_EQ(to_wstring(value, 28), L"4dl0bb0blfaoipc8f7elhr5d7p0n28iob4rhmo9n");
    EXPECT_EQ(to_wstring(value, 29), L"14446650lm8disc37ohldq5f36dn51dbqrjmclll");
    EXPECT_EQ(to_wstring(value, 30), L"9457qs5cc4cjgsslml19i074rnl82ohein77m39");
    EXPECT_EQ(to_wstring(value, 31), L"2jfat1a95mrgd7ej9ducbir25ouseir6e53el5p");
    EXPECT_EQ(to_wstring(value, 32), L"p5ivbt5bblg1aq7gu8bb7c2esu52297qbfa3gj");
    EXPECT_EQ(to_wstring(value, 33), L"822lnwm95hb4bj95ulwgpdln3q8s81okhbjn2o");
    EXPECT_EQ(to_wstring(value, 34), L"2msi9cfaccqunvoom5m362coqrxcmttlqr3jf1");
    EXPECT_EQ(to_wstring(value, 35), L"vyrcqwj3tp1erk7nkx7xw4xme63wchx6ojh99");
    EXPECT_EQ(to_wstring(value, 36), L"blrdpawjeweaxb93a5h07u19ogcvpgt5tf66r");
}
// clang-format on

TEST(ToString, Zero) {
    constexpr big_int value = 0;
    EXPECT_EQ(to_string(value), "0");
    for (int base = 2; base <= 36; ++base) {
        EXPECT_EQ(to_string(value, base), "0") << "base=" << base;
    }
}

TEST(ToWString, Zero) {
    constexpr big_int value = 0;
    EXPECT_EQ(to_wstring(value), L"0");
    for (int base = 2; base <= 36; ++base) {
        EXPECT_EQ(to_wstring(value, base), L"0") << "base=" << base;
    }
}

TEST(ToString, DefaultBaseIsTen) {
    EXPECT_EQ(to_string(big_int{255}), "255");
    EXPECT_EQ(to_string(big_int{-255}), "-255");
    EXPECT_EQ(to_string(1_n << 64), "18446744073709551616");
}

TEST(ToWString, DefaultBaseIsTen) {
    EXPECT_EQ(to_wstring(big_int{255}), L"255");
    EXPECT_EQ(to_wstring(big_int{-255}), L"-255");
    EXPECT_EQ(to_wstring(1_n << 64), L"18446744073709551616");
}

// Large-input coverage for the sub-quadratic to_chars path that backs both
// to_string and to_wstring. The fast kernel only engages above the per-arch
// gate (more than fast_output_basecase_chunks chunks: ~300 base-10 digits on
// every configuration), so the fixed-value tests above exercise only the inline
// fallback. These round-trips push past
// both gates across several bases, validating the digit transcode, the
// result/scratch sizing, sign handling, and (for to_wstring) the widening.
TEST(ToString, FastPathRoundTrip) {
    for (const int base : {3, 7, 10, 26, 36}) {
        for (const std::size_t len : {std::size_t{2000}, std::size_t{25000}, std::size_t{60000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 1000003u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            const big_int v = parse(s, base);
            EXPECT_EQ(to_string(v, base), s) << "base=" << base << " len=" << len;

            const std::string ns = "-" + s;
            const big_int     nv = parse(ns, base);
            EXPECT_EQ(to_string(nv, base), ns) << "negative base=" << base << " len=" << len;
        }
    }
}

TEST(ToWString, FastPathRoundTrip) {
    for (const int base : {3, 7, 10, 26, 36}) {
        for (const std::size_t len : {std::size_t{2000}, std::size_t{25000}, std::size_t{60000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 1000003u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            const big_int v = parse(s, base);
            EXPECT_EQ(to_wstring(v, base), widen(s)) << "base=" << base << " len=" << len;

            const std::string ns = "-" + s;
            const big_int     nv = parse(ns, base);
            EXPECT_EQ(to_wstring(nv, base), widen(ns)) << "negative base=" << base << " len=" << len;
        }
    }
}

// Large power-of-two round-trips: the fixed-value tests above top out at ~200
// digits, so this exercises the multi-limb bit-packing chunk loops in both po2
// from_chars branches (2/16 = max_pow==0, 8/32 = general is_pow_2) plus the
// short top block, against the direct-shift to_chars side.
TEST(ToString, Po2RoundTrip) {
    for (const int base : {2, 8, 16, 32}) {
        for (const std::size_t len : {std::size_t{200}, std::size_t{5000}, std::size_t{100000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 7919u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            EXPECT_EQ(to_string(parse(s, base), base), s) << "base=" << base << " len=" << len;
            const std::string ns = "-" + s;
            EXPECT_EQ(to_string(parse(ns, base), base), ns) << "negative base=" << base << " len=" << len;
        }
    }
}

TEST(ToWString, Po2RoundTrip) {
    for (const int base : {2, 8, 16, 32}) {
        for (const std::size_t len : {std::size_t{200}, std::size_t{5000}, std::size_t{100000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 7919u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            EXPECT_EQ(to_wstring(parse(s, base), base), widen(s)) << "base=" << base << " len=" << len;
            const std::string ns = "-" + s;
            EXPECT_EQ(to_wstring(parse(ns, base), base), widen(ns)) << "negative base=" << base << " len=" << len;
        }
    }
}

// ---------------------------------------------------------------------------
// The rvalue to_string / to_wstring overloads. Both render through the to_chars
// overload that consumes its operand, so a value the caller hands over is
// divided down in place rather than copied first. The text must be identical
// either way, which is what these checks pin down.
// ---------------------------------------------------------------------------

// Pointers whose target types can only be one overload each. If an rvalue
// overload went missing, the const-reference one could not initialize the
// corresponding pointer, so these four initializations alone are a compile-time
// regression net. Selection then follows from overload resolution: a non-const
// rvalue argument always prefers the rvalue-reference parameter.
using to_string_rvalue_t                        = std::string (*)(big_int&&, int);
using to_string_lvalue_t                        = std::string (*)(const big_int&, int);
using to_wstring_rvalue_t                       = std::wstring (*)(big_int&&, int);
using to_wstring_lvalue_t                       = std::wstring (*)(const big_int&, int);
constexpr to_string_rvalue_t  to_string_rvalue  = &to_string;
constexpr to_string_lvalue_t  to_string_lvalue  = &to_string;
constexpr to_wstring_rvalue_t to_wstring_rvalue = &to_wstring;
constexpr to_wstring_lvalue_t to_wstring_lvalue = &to_wstring;

// Spans every to_chars dispatch reachable from to_string: zero, the single-limb
// delegation, the two-limb boundary, magnitudes deep enough for the repeated
// division ladder, and both signs of each.
[[nodiscard]] std::vector<big_int> rvalue_sample_values() {
    std::vector<big_int> values{
        0,
        1,
        -1,
        255,
        -255,
        big_int{std::numeric_limits<std::uint64_t>::max()},
        1_n << 64,
        (1_n << 128) - 1,
        1_n << 128,
        -(1_n << 128),
        1_n << 200,
        -((1_n << 200) + 12345),
        1_n << 1000,
        -(1_n << 1000),
    };
    values.push_back(parse("1234567890123456789012345678901234567890112233445566778899", 10));
    values.push_back(-values.back());
    return values;
}

TEST(ToStringRvalue, BothOverloadsAreCallable) {
    const big_int value = 1_n << 200;
    EXPECT_EQ(to_string_rvalue(big_int{value}, 10), to_string_lvalue(value, 10));
    EXPECT_EQ(to_wstring_rvalue(big_int{value}, 10), to_wstring_lvalue(value, 10));
    EXPECT_EQ(to_string_rvalue(big_int{value}, 10), "1606938044258990275541962092341162602522202993782792835301376");
}

TEST(ToStringRvalue, MatchesLvalueInEveryBase) {
    for (const big_int& value : rvalue_sample_values()) {
        for (int base = 2; base <= 36; ++base) {
            const std::string expected = to_string(value, base);
            EXPECT_EQ(to_string(big_int{value}, base), expected) << "base=" << base;
            EXPECT_EQ(to_wstring(big_int{value}, base), widen(expected)) << "base=" << base;
        }
    }
}

TEST(ToStringRvalue, LvalueOverloadLeavesArgumentUnchanged) {
    for (const big_int& original : rvalue_sample_values()) {
        big_int value = original;
        for (int base = 2; base <= 36; ++base) {
            const std::string narrow = to_string(value, base);
            EXPECT_EQ(value, original) << "base=" << base;
            const std::wstring wide = to_wstring(value, base);
            EXPECT_EQ(value, original) << "base=" << base;
            EXPECT_EQ(wide, widen(narrow)) << "base=" << base;
        }
    }
}

TEST(ToStringRvalue, DefaultBaseIsTen) {
    EXPECT_EQ(to_string(big_int{255}), "255");
    EXPECT_EQ(to_string(big_int{-255}), "-255");
    EXPECT_EQ(to_string(1_n << 64), "18446744073709551616");
    EXPECT_EQ(to_wstring(big_int{255}), L"255");
    EXPECT_EQ(to_wstring(big_int{-255}), L"-255");
    EXPECT_EQ(to_wstring(1_n << 64), L"18446744073709551616");
}

TEST(ToStringRvalue, Zero) {
    for (int base = 2; base <= 36; ++base) {
        EXPECT_EQ(to_string(big_int{0}, base), "0") << "base=" << base;
        EXPECT_EQ(to_wstring(big_int{0}, base), L"0") << "base=" << base;
    }
}

// Whatever state the call leaves behind, assignment restores a usable value.
TEST(ToStringRvalue, HandedOverValueRemainsUsable) {
    for (const int base : {10, 16}) {
        big_int handed_over = 1_n << 128;
        EXPECT_EQ(to_string(std::move(handed_over), base), to_string(1_n << 128, base)) << "base=" << base;

        handed_over = 42;
        EXPECT_EQ(handed_over, big_int{42});
        EXPECT_EQ(to_string(handed_over), "42");
    }
}

// The same large-input coverage as the lvalue round-trips above, driven through
// the consuming overloads: past both the sub-quadratic kernel's gate and, for
// the power-of-two bases, the direct bit-packing path.
TEST(ToStringRvalue, FastPathRoundTrip) {
    for (const int base : {3, 7, 10, 26, 36}) {
        for (const std::size_t len : {std::size_t{2000}, std::size_t{25000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 1000003u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            EXPECT_EQ(to_string(parse(s, base), base), s) << "base=" << base << " len=" << len;
            EXPECT_EQ(to_wstring(parse(s, base), base), widen(s)) << "base=" << base << " len=" << len;

            const std::string ns = "-" + s;
            EXPECT_EQ(to_string(parse(ns, base), base), ns) << "negative base=" << base << " len=" << len;
            EXPECT_EQ(to_wstring(parse(ns, base), base), widen(ns)) << "negative base=" << base << " len=" << len;
        }
    }
}

TEST(ToStringRvalue, Po2RoundTrip) {
    for (const int base : {2, 8, 16, 32}) {
        for (const std::size_t len : {std::size_t{200}, std::size_t{5000}}) {
            const std::uint64_t seed = static_cast<std::uint64_t>(base) * 7919u + static_cast<std::uint64_t>(len);
            const std::string   s    = random_digit_string(len, base, seed);

            EXPECT_EQ(to_string(parse(s, base), base), s) << "base=" << base << " len=" << len;
            EXPECT_EQ(to_wstring(parse(s, base), base), widen(s)) << "base=" << base << " len=" << len;
        }
    }
}

// The consuming path mutates its operand, which is exactly the kind of thing a
// constant evaluator rejects when it is done to something it should not be.
TEST(ToStringRvalue, ConstantEvaluation) {
    static_assert(to_string(big_int{255}, 16) == "ff");
    static_assert(to_string(big_int{-255}, 16) == "-ff");
    static_assert(to_string(big_int{0}) == "0");
    static_assert(to_string(1_n << 200) == "1606938044258990275541962092341162602522202993782792835301376");
    static_assert(to_wstring(big_int{255}, 16) == L"ff");
    static_assert(to_wstring(big_int{-255}, 16) == L"-ff");
    static_assert(to_wstring(1_n << 200) == L"1606938044258990275541962092341162602522202993782792835301376");
}

} // namespace
