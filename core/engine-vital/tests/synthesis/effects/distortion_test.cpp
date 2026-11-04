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

#include "distortion_test.h"
#include "distortion.h"

void DistortionTest::runTest() {
  sbvital::Distortion distortion;
  std::set<int> ignored_outputs;
  ignored_outputs.insert(sbvital::Distortion::kDriveOut);
  runInputBoundsTest(&distortion, std::set<int>(), ignored_outputs);
}

SBVITAL_REGISTER_UNIT_TEST(DistortionTest, distortion_test)