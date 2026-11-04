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

#include "wavetable_creator.h"
#include "line_generator.h"
#include "synth_constants.h"
#include "wave_frame.h"
#include "wave_line_source.h"
#include "wave_source.h"
#include "wavetable.h"

namespace {
  int getFirstNonZeroSample(const float* audio_buffer, int num_samples) {
    for (int i = 0; i < num_samples; ++i) {
      if (audio_buffer[i])
        return i;
    }
    return 0;
  }
}

int WavetableCreator::getGroupIndex(WavetableGroup* group) {
  for (int i = 0; i < groups_.size(); ++i) {
    if (groups_[i].get() == group)
      return i;
  }
  return -1;
}

void WavetableCreator::moveUp(int index) {
  if (index <= 0)
    return;
  
  groups_[index].swap(groups_[index - 1]);
}

void WavetableCreator::moveDown(int index) {
  if (index < 0 || index >= groups_.size() - 1)
    return;

  groups_[index].swap(groups_[index + 1]);
}

void WavetableCreator::removeGroup(int index) {
  if (index < 0 || index >= groups_.size())
    return;

  std::unique_ptr<WavetableGroup> group = std::move(groups_[index]);
  groups_.erase(groups_.begin() + index);
}

float WavetableCreator::render(int position) {
  compute_frame_combine_.clear();
  compute_frame_combine_.index = position;
  compute_frame_.index = position;

  for (auto& group : groups_) {
    group->render(&compute_frame_, position);
    compute_frame_combine_.addFrom(&compute_frame_);
  }

  if (groups_.size() > 1)
    compute_frame_combine_.multiply(1.0f / groups_.size());

  if (remove_all_dc_)
    compute_frame_combine_.removedDc();

  float max_value = 0.0f;
  float min_value = 0.0f;
  for (int i = 0; i < sbvital::WaveFrame::kWaveformSize; ++i) {
    max_value = std::max(compute_frame_combine_.time_domain[i], max_value);
    min_value = std::min(compute_frame_combine_.time_domain[i], min_value);
  }

  wavetable_->loadWaveFrame(&compute_frame_combine_);
  return max_value - min_value;
}

void WavetableCreator::render() {
  int last_waveframe = 0;
  bool shepard = groups_.size() > 0;
  for (auto& group : groups_) {
    group->prerender();
    last_waveframe = std::max(last_waveframe, group->getLastKeyframePosition());
    shepard = shepard && group->isShepardTone();
  }
  
  wavetable_->setNumFrames(last_waveframe + 1);
  wavetable_->setShepardTable(shepard);
  float max_span = 0.0f;
  for (int i = 0; i < last_waveframe + 1; ++i)
    max_span = std::max(render(i), max_span);
  wavetable_->setFrequencyRatio(compute_frame_.frequency_ratio);
  wavetable_->setSampleRate(compute_frame_.sample_rate);

  postRender(max_span);
}

void WavetableCreator::postRender(float max_span) {
  if (full_normalize_)
    wavetable_->postProcess(max_span);
  else
    wavetable_->postProcess(0.0f);
}

void WavetableCreator::renderToBuffer(float* buffer, int num_frames, int frame_size) {
  int total_samples = num_frames * frame_size;
  for (int i = 0; i < num_frames; ++i) {
    float position = (1.0f * i * sbvital::kNumOscillatorWaveFrames) / num_frames;
    compute_frame_combine_.clear();
    compute_frame_combine_.index = position;
    compute_frame_.index = position;

    for (auto& group : groups_) {
      group->render(&compute_frame_, position);
      compute_frame_combine_.addFrom(&compute_frame_);
    }

    float* output_buffer = buffer + (i * frame_size);

    if (frame_size != sbvital::WaveFrame::kWaveformSize)
      VITAL_ASSERT(false); // TODO: support different waveframe size.
    else {
      for (int s = 0; s < sbvital::WaveFrame::kWaveformSize; ++s)
        output_buffer[s] = compute_frame_combine_.time_domain[s];
    }
  }

  float max_value = 1.0f;
  for (int i = 0; i < total_samples; ++i)
    max_value = std::max(max_value, fabsf(buffer[i]));

  float scale = 1.0f / max_value;
  for (int i = 0; i < total_samples; ++i)
    buffer[i] *= scale;
}

void WavetableCreator::init() {
  clear();
  loadDefaultCreator();
  render();
}

void WavetableCreator::clear() {
  groups_.clear();
  remove_all_dc_ = true;
  full_normalize_ = true;
}

void WavetableCreator::loadDefaultCreator() {
  wavetable_->setName("Init");
  WavetableGroup* new_group = new WavetableGroup();
  new_group->loadDefaultGroup();
  addGroup(new_group);
}

void WavetableCreator::initPredefinedWaves() {
  clear();

  WavetableGroup* new_group = new WavetableGroup();
  WaveSource* wave_source = new WaveSource();

  int num_shapes = sbvital::PredefinedWaveFrames::kNumShapes;
  for (int i = 0; i < num_shapes; ++i) {
    int position = (sbvital::kNumOscillatorWaveFrames * i) / num_shapes;
    wave_source->insertNewKeyframe(position);
    WaveSourceKeyframe* keyframe = wave_source->getKeyframe(i);

    sbvital::PredefinedWaveFrames::Shape shape = static_cast<sbvital::PredefinedWaveFrames::Shape>(i);
    keyframe->wave_frame()->copy(sbvital::PredefinedWaveFrames::getWaveFrame(shape));
  }
  wave_source->setInterpolationStyle(WaveSource::kNone);
  full_normalize_ = false;
  remove_all_dc_ = false;

  new_group->addComponent(wave_source);
  addGroup(new_group);
  render();
}

void WavetableCreator::initFromAudioFile(const float* audio_buffer, int num_samples, int sample_rate,
                                         AudioFileLoadStyle load_style, FileSource::FadeStyle fade_style) {
  int beginning_sample = getFirstNonZeroSample(audio_buffer, num_samples);
  int shortened_num_samples = num_samples - beginning_sample;
  if (load_style == kVocoded)
    initFromVocodedAudioFile(audio_buffer + beginning_sample, shortened_num_samples, sample_rate, false);
  else if (load_style == kTtwt)
    initFromVocodedAudioFile(audio_buffer + beginning_sample, shortened_num_samples, sample_rate, true);
  else if (load_style == kPitched)
    initFromPitchedAudioFile(audio_buffer + beginning_sample, shortened_num_samples, sample_rate);
  else
    initFromSplicedAudioFile(audio_buffer, num_samples, sample_rate, fade_style);
}

void WavetableCreator::initFromSplicedAudioFile(const float* audio_buffer, int num_samples, int sample_rate,
                                                FileSource::FadeStyle fade_style) {
  clear();

  WavetableGroup* new_group = new WavetableGroup();
  FileSource* file_source = new FileSource();

  file_source->loadBuffer(audio_buffer, num_samples, sample_rate);
  file_source->setFadeStyle(fade_style);
  file_source->setPhaseStyle(FileSource::PhaseStyle::kNone);
  file_source->insertNewKeyframe(0);
  file_source->detectWaveEditTable();

  double window_size = file_source->getWindowSize();
  if (fade_style == FileSource::kNoInterpolate) {
    int num_cycles = std::max<int>(1, num_samples / window_size);
    int buffer_frames = sbvital::kNumOscillatorWaveFrames / num_cycles;
    file_source->insertNewKeyframe(std::max(0, sbvital::kNumOscillatorWaveFrames - 1 - buffer_frames));
  }
  else
    file_source->insertNewKeyframe(sbvital::kNumOscillatorWaveFrames - 1);

  file_source->getKeyframe(0)->setStartPosition(0);
  int last_sample_position = num_samples - window_size;
  last_sample_position = std::min<int>(file_source->getKeyframe(1)->position() * window_size, last_sample_position);
  file_source->getKeyframe(1)->setStartPosition(std::max(0, last_sample_position));

  new_group->addComponent(file_source);
  addGroup(new_group);
  render();
}

void WavetableCreator::initFromVocodedAudioFile(const float* audio_buffer, int num_samples,
                                                int sample_rate, bool ttwt) {
  static constexpr float kMaxTTWTPeriod = .02f;
  clear();

  WavetableGroup* new_group = new WavetableGroup();
  FileSource* file_source = new FileSource();

  file_source->loadBuffer(audio_buffer, num_samples, sample_rate);
  if (ttwt)
    file_source->detectPitch(kMaxTTWTPeriod * sample_rate);
  else
    file_source->detectPitch();

  file_source->setFadeStyle(FileSource::FadeStyle::kWaveBlend);
  file_source->setPhaseStyle(FileSource::PhaseStyle::kVocode);
  file_source->insertNewKeyframe(0);
  file_source->insertNewKeyframe(sbvital::kNumOscillatorWaveFrames - 1);
  file_source->getKeyframe(0)->setStartPosition(0);
  int samples_needed = file_source->getKeyframe(1)->getSamplesNeeded();
  file_source->getKeyframe(1)->setStartPosition(num_samples - samples_needed);

  new_group->addComponent(file_source);
  addGroup(new_group);
  render();
}

void WavetableCreator::initFromPitchedAudioFile(const float* audio_buffer, int num_samples, int sample_rate) {
  clear();

  WavetableGroup* new_group = new WavetableGroup();
  FileSource* file_source = new FileSource();

  file_source->loadBuffer(audio_buffer, num_samples, sample_rate);
  file_source->detectPitch();
  file_source->setFadeStyle(FileSource::FadeStyle::kWaveBlend);
  file_source->insertNewKeyframe(0);
  file_source->insertNewKeyframe(sbvital::kNumOscillatorWaveFrames - 1);
  file_source->getKeyframe(0)->setStartPosition(0);
  int samples_needed = file_source->getKeyframe(1)->getSamplesNeeded();
  file_source->getKeyframe(1)->setStartPosition(num_samples - samples_needed);

  new_group->addComponent(file_source);
  addGroup(new_group);
  render();
}

void WavetableCreator::initFromLineGenerator(LineGenerator* line_generator) {
  clear();

  wavetable_->setName(line_generator->getName());
  WavetableGroup* new_group = new WavetableGroup();

  WaveLineSource* line_source = new WaveLineSource();
  line_source->insertNewKeyframe(0);
  WaveLineSource::WaveLineSourceKeyframe* keyframe = line_source->getKeyframe(0);
  keyframe->getLineGenerator()->jsonToState(line_generator->stateToJson());

  new_group->addComponent(line_source);
  addGroup(new_group);
  render();
}

bool WavetableCreator::isValidJson(json data) {
  if (LineGenerator::isValidJson(data))
    return true;

  if (!data.count("version") || !data.count("groups") || !data.count("name"))
    return false;

  json groups_data = data["groups"];
  return groups_data.is_array();
}

json WavetableCreator::updateJson(json data) {
  // SynthyBoy: the migrations of Vital's pre-1.0 wavetable files (they used LoadSave and JUCE's Base64) are not carried
  // over; only files written by this code are read, see docs/UPSTREAM.md.
  return data;
}

json WavetableCreator::stateToJson() {
  json json_groups;
  for (auto& group : groups_)
    json_groups.push_back(group->stateToJson());

  return {
    { "groups", json_groups },
    { "name", wavetable_->getName() },
    { "author", wavetable_->getAuthor() },
    { "version", ProjectInfo::versionString },
    { "remove_all_dc", remove_all_dc_ },
    { "full_normalize", full_normalize_ },
  };
}

void WavetableCreator::jsonToState(json data) {
  if (LineGenerator::isValidJson(data)) {
    LineGenerator generator(sbvital::WaveFrame::kWaveformSize);
    generator.jsonToState(data);
    initFromLineGenerator(&generator);
    return;
  }

  clear();
  data = updateJson(data);

  std::string name = "";
  if (data.count("name"))
    name = data["name"].get<std::string>();
  wavetable_->setName(name);

  std::string author = "";
  if (data.count("author"))
    author = data["author"].get<std::string>();
  wavetable_->setAuthor(author);

  if (data.count("remove_all_dc"))
    remove_all_dc_ = data["remove_all_dc"];
  if (data.count("full_normalize"))
    full_normalize_ = data["full_normalize"];
  else
    full_normalize_ = false;

  json json_groups = data["groups"];
  for (const json& json_group : json_groups) {
    WavetableGroup* new_group = new WavetableGroup();
    new_group->jsonToState(json_group);
    addGroup(new_group);
  }

  render();
}
