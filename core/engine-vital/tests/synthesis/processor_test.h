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

#pragma once

#include "unit_test_shim.h"
#include <set>

namespace sbvital {
  class Processor;
} // namespace sbvital

class ProcessorTest : public UnitTest {
  public:
    ProcessorTest(std::string name) : UnitTest(name, "Processor") { }
    void runInputBoundsTest(sbvital::Processor* processor);
    void runInputBoundsTest(sbvital::Processor* processor, std::set<int> leave_inputs, std::set<int> ignore_outputs);
    void processAndCheckFinite(sbvital::Processor* processor, const std::set<int>& ignore_outputs);
};

