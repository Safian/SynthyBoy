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

#include "tuning.h"

#include "utils.h"

#include <cctype>
#include <locale>
#include <sstream>

namespace {
  constexpr int kDefaultMidiReference = 60;
  constexpr char kScalaKbmComment = '!';
  constexpr char kTunComment = ';';

  enum ScalaReadingState {
    kDescription,
    kScaleLength,
    kScaleRatios
  };

  enum KbmPositions {
    kMapSizePosition,
    kStartMidiMapPosition,
    kEndMidiMapPosition,
    kMidiMapMiddlePosition,
    kReferenceNotePosition,
    kReferenceFrequencyPosition,
    kScaleDegreePosition,
  };

  enum TunReadingState {
    kScanningForSection,
    kTuning,
    kExactTuning
  };

  // Minimal std::string equivalents of the string calls the parsers used.
  std::string trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
      ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
      --end;
    return s.substr(begin, end - begin);
  }

  std::string toLower(std::string s) {
    for (char& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  std::vector<std::string> splitTokens(const std::string& source, const char* delimiters) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : source) {
      if (std::strchr(delimiters, c) != nullptr) {
        if (!current.empty())
          tokens.push_back(current);
        current.clear();
      }
      else
        current.push_back(c);
    }
    if (!current.empty())
      tokens.push_back(current);
    return tokens;
  }

  std::string tokenAt(const std::vector<std::string>& tokens, size_t index) {
    return index < tokens.size() ? tokens[index] : std::string();
  }

  int getIntValue(const std::string& s) {
    std::istringstream stream(s);
    stream.imbue(std::locale::classic());
    int value = 0;
    stream >> value;
    return value;
  }

  float getFloatValue(const std::string& s) {
    std::istringstream stream(s);
    stream.imbue(std::locale::classic());
    float value = 0.0f;
    stream >> value;
    return std::isfinite(value) ? value : 0.0f;
  }

  std::string upToFirstOccurrenceOf(const std::string& s, char c) {
    size_t position = s.find(c);
    return position == std::string::npos ? s : s.substr(0, position);
  }

  std::string fromLastOccurrenceOf(const std::string& s, char c) {
    size_t position = s.rfind(c);
    return position == std::string::npos ? s : s.substr(position + 1);
  }

  std::string extractFirstToken(const std::string& source) {
    return tokenAt(splitTokens(source, " \t\r\n"), 0);
  }

  float readCentsToTranspose(const std::string& cents) {
    return getFloatValue(cents) / sbvital::kCentsPerNote;
  }

  float readRatioToTranspose(const std::string& ratio) {
    std::vector<std::string> tokens = splitTokens(ratio, "/");
    float value = getIntValue(tokenAt(tokens, 0));

    if (tokens.size() == 2)
      value /= getIntValue(tokens[1]);

    return sbvital::utils::ratioToMidiTranspose(value);
  }

  std::string readTunSection(const std::string& line) {
    return toLower(line.substr(1, line.size() - 2));
  }

  bool isBaseFrequencyAssignment(const std::string& line) {
    return trim(toLower(upToFirstOccurrenceOf(line, '='))) == "basefreq";
  }

  int getNoteAssignmentIndex(const std::string& line) {
    std::string variable = upToFirstOccurrenceOf(line, '=');
    std::vector<std::string> tokens = splitTokens(variable, " \t\r\n");
    if (tokens.size() <= 1 || toLower(tokens[0]) != "note")
      return -1;
    int index = getIntValue(tokens[1]);
    if (index < 0 || index >= sbvital::kMidiSize)
      return -1;
    return index;
  }

  float getAssignmentValue(const std::string& line) {
    std::string value = trim(fromLastOccurrenceOf(line, '='));
    return getFloatValue(value);
  }
}

int Tuning::noteToMidiKey(const std::string& note_text) {
  constexpr int kNotesInScale = 7;
  constexpr int kOctaveStart = -1;
  constexpr int kScale[kNotesInScale] = { -3, -1, 0, 2, 4, 5, 7 };

  std::string text = toLower(note_text);
  text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
  if (note_text.size() < 2)
    return -1;

  char note_in_scale = text[0] - 'a';
  if (note_in_scale < 0 || note_in_scale >= kNotesInScale)
    return -1;

  int offset = kScale[note_in_scale];
  text = text.substr(1);
  if (text[0] == '#') {
    text = text.substr(1);
    offset++;
  }
  else if (text[0] == 'b') {
    text = text.substr(1);
    offset--;
  }

  if (text.size() == 0)
    return -1;

  bool negative = false;
  if (text[0] == '-') {
    text = text.substr(1);
    negative = true;
    if (text.size() == 0)
      return -1;
  }
  int octave = text[0] - '0';
  if (negative)
    octave = -octave;
  octave = octave - kOctaveStart;
  return sbvital::kNotesPerOctave * octave + offset;
}

void Tuning::loadScalaFile(const std::vector<std::string>& scala_lines) {
  ScalaReadingState state = kDescription;

  int scale_length = 1;
  std::vector<float> scale;
  scale.push_back(0.0f);

  for (const std::string& line : scala_lines) {
    std::string trimmed_line = trim(line);
    if (trimmed_line.size() > 0 && trimmed_line[0] == kScalaKbmComment)
      continue;

    if (scale.size() >= scale_length + 1)
      break;

    switch (state) {
      case kDescription:
        state = kScaleLength;
        break;
      case kScaleLength:
        scale_length = getIntValue(extractFirstToken(trimmed_line));
        state = kScaleRatios;
        break;
      case kScaleRatios: {
        std::string tuning = extractFirstToken(trimmed_line);
        if (tuning.find('.') != std::string::npos)
          scale.push_back(readCentsToTranspose(tuning));
        else
          scale.push_back(readRatioToTranspose(tuning));
        break;
      }
    }
  }

  keyboard_mapping_.clear();
  for (int i = 0; i < scale.size() - 1; ++i)
    keyboard_mapping_.push_back(i);
  scale_start_midi_note_ = kDefaultMidiReference;
  reference_midi_note_ = 0;

  loadScale(scale);
  default_ = false;
}

void Tuning::loadKeyboardMapFile(const std::vector<std::string>& lines) {
  static constexpr int kHeaderSize = 7;

  float header_data[kHeaderSize];
  memset(header_data, 0, kHeaderSize * sizeof(float));
  int header_position = 0;
  int map_size = 0;
  int last_scale_value = 0;
  keyboard_mapping_.clear();

  for (const std::string& line : lines) {
    std::string trimmed_line = trim(line);
    if (trimmed_line.size() > 0 && trimmed_line[0] == kScalaKbmComment)
      continue;

    if (header_position >= kHeaderSize) {
      std::string token = extractFirstToken(trimmed_line);
      if (toLower(token)[0] != 'x')
        last_scale_value = getIntValue(token);
     
      keyboard_mapping_.push_back(last_scale_value);

      if (keyboard_mapping_.size() >= map_size)
        break;
    }
    else {
      header_data[header_position] = getFloatValue(extractFirstToken(trimmed_line));
      if (header_position == kMapSizePosition)
        map_size = header_data[header_position];
      header_position++;
    }
  }

  setStartMidiNote(header_data[kMidiMapMiddlePosition]);
  setReferenceNoteFrequency(header_data[kReferenceNotePosition], header_data[kReferenceFrequencyPosition]);
  loadScale(scale_);
}

void Tuning::loadTunFile(const std::vector<std::string>& lines) {
  keyboard_mapping_.clear();

  TunReadingState state = kScanningForSection;

  int last_read_note = 0;
  float base_frequency = sbvital::kMidi0Frequency;
  std::vector<float> scale;
  for (int i = 0; i < sbvital::kMidiSize; ++i)
    scale.push_back(i);

  for (const std::string& line : lines) {
    std::string trimmed_line = trim(line);
    if (trimmed_line.size() == 0 || trimmed_line[0] == kTunComment)
      continue;

    if (trimmed_line[0] == '[') {
      std::string section = readTunSection(trimmed_line);
      if (section == "tuning")
        state = kTuning;
      else if (section == "exact tuning")
        state = kExactTuning;
      else
        state = kScanningForSection;
    }
    else if (state == kTuning || state == kExactTuning) {
      if (isBaseFrequencyAssignment(trimmed_line))
        base_frequency = getAssignmentValue(trimmed_line);
      else {
        int index = getNoteAssignmentIndex(trimmed_line);
        last_read_note = std::max(last_read_note, index);
        if (index >= 0)
          scale[index] = getAssignmentValue(trimmed_line) / sbvital::kCentsPerNote;
      }
    }
  }

  scale.resize(last_read_note + 1);
  
  loadScale(scale);
  setStartMidiNote(0);
  setReferenceFrequency(base_frequency);
}

Tuning::Tuning() : default_(true) {
  scale_start_midi_note_ = kDefaultMidiReference;
  reference_midi_note_ = 0;

  setDefaultTuning();
}

void Tuning::loadScale(std::vector<float> scale) {
  scale_ = scale;
  if (scale.size() <= 1) {
    setConstantTuning(kDefaultMidiReference);
    return;
  }

  int scale_size = static_cast<int>(scale.size() - 1);
  int mapping_size = scale_size;
  if (keyboard_mapping_.size())
    mapping_size = static_cast<int>(keyboard_mapping_.size());

  float octave_offset = scale[scale_size];
  int start_octave = -kTuningCenter / mapping_size - 1;
  int mapping_position = -kTuningCenter - start_octave * mapping_size;

  float current_offset = start_octave * octave_offset;
  for (int i = 0; i < kTuningSize; ++i) {
    if (mapping_position >= mapping_size) {
      current_offset += octave_offset;
      mapping_position = 0;
    }

    int note_in_scale = mapping_position;
    if (keyboard_mapping_.size())
      note_in_scale = keyboard_mapping_[mapping_position];

    tuning_[i] = current_offset + scale[note_in_scale];
    mapping_position++;
  }
}

void Tuning::setConstantTuning(float note) {
  for (int i = 0; i < kTuningSize; ++i)
    tuning_[i] = note;
}

void Tuning::setDefaultTuning() {
  for (int i = 0; i < kTuningSize; ++i)
    tuning_[i] = i - kTuningCenter;

  scale_.clear();
  for (int i = 0; i <= sbvital::kNotesPerOctave; ++i)
    scale_.push_back(i);

  keyboard_mapping_.clear();

  default_ = true;
  tuning_name_ = "";
  mapping_name_ = "";
}

sbvital::mono_float Tuning::convertMidiNote(int note) const {
  int scale_offset = note - scale_start_midi_note_;
  return tuning_[kTuningCenter + scale_offset] + scale_start_midi_note_ + reference_midi_note_;
}

void Tuning::setReferenceFrequency(float frequency) {
  setReferenceNoteFrequency(0, frequency);
}

void Tuning::setReferenceNoteFrequency(int midi_note, float frequency) {
  reference_midi_note_ = sbvital::utils::frequencyToMidiNote(frequency) - midi_note;
}

void Tuning::setReferenceRatio(float ratio) {
  reference_midi_note_ = sbvital::utils::ratioToMidiTranspose(ratio);
}

json Tuning::stateToJson() const {
  json data;
  data["scale_start_midi_note"] = scale_start_midi_note_;
  data["reference_midi_note"] = reference_midi_note_;
  data["tuning_name"] = tuning_name_;
  data["mapping_name"] = mapping_name_;
  data["default"] = default_;

  json scale_data;
  for (float scale_value : scale_)
    scale_data.push_back(scale_value);
  data["scale"] = scale_data;

  if (keyboard_mapping_.size()) {
    json mapping_data;
    for (int mapping_value : keyboard_mapping_)
      mapping_data.push_back(mapping_value);
    data["mapping"] = mapping_data;
  }

  return data;
}

void Tuning::jsonToState(const json& data) {
  scale_start_midi_note_ = data["scale_start_midi_note"];
  reference_midi_note_ = data["reference_midi_note"];
  std::string tuning_name = data["tuning_name"];
  tuning_name_ = tuning_name;
  std::string mapping_name = data["mapping_name"];
  mapping_name_ = mapping_name;
  
  if (data.count("default"))
    default_ = data["default"];

  json scale_data = data["scale"];
  scale_.clear();
  for (json& value : scale_data) {
    float scale_value = value;
    scale_.push_back(scale_value);
  }

  keyboard_mapping_.clear();
  if (data.count("mapping")) {
    json mapping_data = data["mapping"];
    for (json& value : mapping_data) {
      int keyboard_value = value;
      keyboard_mapping_.push_back(keyboard_value);
    }
  }

  loadScale(scale_);
}
