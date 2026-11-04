/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */
// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026: JUCE UnitTest -> tests/unit_test_shim.h, namespace sbvital

#include "poly_utils_test.h"
#include "poly_utils.h"

#define EPSILON 0.0000001f

void PolyUtilsTest::runTest() {
  beginTest("Swap Stereo");
  sbvital::poly_float val;
  for (int i = 0; i < sbvital::poly_float::kSize; ++i)
    val.set(i, i);

  const sbvital::poly_float test_value = val;

  sbvital::poly_float swap_stereo = sbvital::utils::swapStereo(test_value);
  for (int i = 0; i < sbvital::poly_float::kSize; i += 2) {
    expect(swap_stereo[i] == i + 1);
    expect(swap_stereo[i + 1] == i);
  }

  beginTest("Swap Voices");
  sbvital::poly_float swap_voices = sbvital::utils::swapVoices(test_value);
  for (int i = 0; i < sbvital::poly_float::kSize / 2; ++i) {
    expect(swap_voices[i] == i + sbvital::poly_float::kSize / 2);
    expect(swap_voices[i + sbvital::poly_float::kSize / 2] == i);
  }

  beginTest("Reverse");
  sbvital::poly_float reverse = sbvital::utils::reverse(test_value);
  for (int i = 0; i < sbvital::poly_float::kSize; ++i)
    expect(reverse[i] == sbvital::poly_float::kSize - 1 - i);

  beginTest("Mid Side Encoding");
  sbvital::poly_float encode_mid_side = sbvital::utils::encodeMidSide(test_value);
  sbvital::poly_float decode_mid_side = sbvital::utils::decodeMidSide(encode_mid_side);
  for (int i = 0; i < sbvital::poly_float::kSize; i += 2)
    expectWithinAbsoluteError<sbvital::mono_float>(test_value[i], decode_mid_side[i], EPSILON);

  beginTest("Mask Load");
  sbvital::poly_float one(-1.0f, 2.0f, 1.0f, 10.0f);
  sbvital::poly_float two(3.0f, 1.0f, -20.0f, 50.0f);
  sbvital::poly_float combine = sbvital::utils::maskLoad(one, two, sbvital::poly_float::greaterThan(two, one));
  expect(combine[0] == 3.0f);
  expect(combine[1] == 2.0f);
  expect(combine[2] == 1.0f);
  expect(combine[3] == 50.0f);

  sbvital::poly_int int_one(-1, 2, 1, 10);
  sbvital::poly_int int_two(3, 1, -20, 50);
  sbvital::poly_int int_combine = sbvital::utils::maskLoad(int_one, int_two, sbvital::poly_int::greaterThan(int_two, int_one));
  expect(int_combine[0] == (unsigned int)-1);
  expect(int_combine[1] == 2);
  expect(int_combine[2] == (unsigned int)-20);
  expect(int_combine[3] == 50);
}

SBVITAL_REGISTER_UNIT_TEST(PolyUtilsTest, poly_utils_test)