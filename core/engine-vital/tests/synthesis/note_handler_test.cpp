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

#include "note_handler_test.h"
#include "sound_engine.h"

void NoteHandlerTest::processAndExpectFinite(sbvital::SoundEngine* engine) {
  engine->process(sbvital::kMaxBufferSize);

  sbvital::Output* output = engine->output();
  expect(sbvital::utils::isFinite(output->buffer, output->buffer_size));
}

void NoteHandlerTest::processAndExpectQuiet(sbvital::SoundEngine* engine) {
  engine->process(sbvital::kMaxBufferSize);

  sbvital::Output* output = engine->output();
  expect(sbvital::utils::rms(reinterpret_cast<float*>(output->buffer), output->buffer_size) < 0.001f);
}

void NoteHandlerTest::runTest() {
  sbvital::SoundEngine engine;
  engine.getControls()["env_1_release"]->set(0.0f);

  beginTest("No Notes");
  processAndExpectQuiet(&engine);

  beginTest("One Note On");
  engine.noteOn(60, 1.0f, 10, 0);
  processAndExpectFinite(&engine);
  processAndExpectFinite(&engine);

  beginTest("One Note Off");
  engine.noteOff(60, 20, 0, 0);
  processAndExpectFinite(&engine);
  processAndExpectQuiet(&engine);

  beginTest("Three Notes On");
  engine.noteOn(61, 1.0f, 10, 0);
  engine.noteOn(62, 1.0f, sbvital::kMaxBufferSize - 1, 0);
  engine.noteOn(63, 1.0f, sbvital::kMaxBufferSize - 1, 0);
  processAndExpectFinite(&engine);
  processAndExpectFinite(&engine);

  beginTest("Three Notes Off");
  engine.noteOff(61, 0, 0, 0);
  engine.noteOff(62, 0, 0, 0);
  engine.noteOff(63, 0, 0, 0);
  processAndExpectFinite(&engine);
  processAndExpectQuiet(&engine);

  beginTest("Four Notes On");
  engine.noteOn(61, 1.0f, 0, 0);
  engine.noteOn(62, 1.0f, 0, 0);
  engine.noteOn(63, 1.0f, 0, 0);
  engine.noteOn(64, 1.0f, 0, 0);
  processAndExpectFinite(&engine);
  processAndExpectFinite(&engine);

  beginTest("Four Notes Off");
  engine.noteOff(64, 0, 0, 0);
  engine.noteOff(61, 0, 0, 0);
  engine.noteOff(62, 0, 0, 0);
  engine.noteOff(63, 0, 0, 0);
  processAndExpectFinite(&engine);
  processAndExpectQuiet(&engine);
}

SBVITAL_REGISTER_UNIT_TEST(NoteHandlerTest, note_handler_test)