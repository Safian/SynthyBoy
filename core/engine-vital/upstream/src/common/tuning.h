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
// Modified for SynthyBoy (AGPL-3.0-or-later combined work), 2026

#pragma once

#include "vital_compat.h"

#include "common.h"
#include "json/json.h"

using json = nlohmann::json;

class Tuning {
  public:
    static constexpr int kTuningSize = 2 * sbvital::kMidiSize;
    static constexpr int kTuningCenter = sbvital::kMidiSize;
    static int noteToMidiKey(const std::string& note);

    Tuning();

    void loadScale(std::vector<float> scale);
    void setConstantTuning(float note);
    void setDefaultTuning();
    sbvital::mono_float convertMidiNote(int note) const;
    void setStartMidiNote(int start_midi_note) { scale_start_midi_note_ = start_midi_note; }
    void setReferenceNote(int reference_note) { reference_midi_note_ = reference_note; }
    void setReferenceFrequency(float frequency);
    void setReferenceNoteFrequency(int midi_note, float frequency);
    void setReferenceRatio(float ratio);
    std::string getName() const { 
      if (mapping_name_.size() == 0)
        return tuning_name_;
      if (tuning_name_.size() == 0)
        return mapping_name_;
      return tuning_name_ + " / " + mapping_name_;
    }

    void setName(const std::string& name) {
      mapping_name_ = "";
      tuning_name_ = name;
    }
    bool isDefault() const { return default_; }

    json stateToJson() const;
    void jsonToState(const json& data);
    // The text of the files, line by line (reading the files is the caller's job).
    void loadScalaFile(const std::vector<std::string>& scala_lines);
    void loadKeyboardMapFile(const std::vector<std::string>& kbm_lines);
    void loadTunFile(const std::vector<std::string>& tun_lines);

  private:

    int scale_start_midi_note_;
    float reference_midi_note_;
    std::vector<float> scale_;
    std::vector<int> keyboard_mapping_;
    sbvital::mono_float tuning_[kTuningSize];
    std::string tuning_name_;
    std::string mapping_name_;
    bool default_;

    JUCE_LEAK_DETECTOR(Tuning)
};

