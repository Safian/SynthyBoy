# WP-01 report - the content pipeline in the shells

Branch `work-wp01` (cut from develop 0869949). Spec: 13-new-keys.md section 4.25, assumptions A309 .. A324. Morning questions 48 .. 53. 0 keys, 0 capability bits, `kMaxParams` untouched, the 112 factory presets unchanged (`scripts/gen-factory-presets.py --check`: up to date).

## What was built

| Item | Where | Test |
|------|-------|------|
| a. user content library + store in both shells | `core/include/synthyboy/user_content.h`, `core/src/user_content.cpp` (`UserContentStore`: `<userDir>/content/{wavetables,samples,impulses,embedded}`, installed packs `<userDir>/packs`, SHA-256 verified, content addressed); JUCE shell `PluginProcessor.cpp` constructor (`setContentStore` before any content, null in the destructor, every document load decodes through it, `contentRevision`); native shell `shell-clap/synthyboy_clap.cpp` | `core/tests/test_user_content.cpp` `content_store_writes_lists_reads_and_removes_with_sha_references`, `state_keeps_the_references_and_the_missing_files_logic_resolves_them` |
| b. wavetable editor to the engine | `WavetableEditorSheet.cpp:2321` Save Table, Embed in Preset (embedded pack), `:2294` export through a save dialog, `:2345` real importers (audio file, image file, wavetable `.wav` / `.vitaltable`), `:2466` `open` (starts on the stored file), `WavetablePanel.cpp` (real table in the view), `ContentIO.cpp:255` `saveWavetable`; Missing Files dialog `ContentSheets.cpp` + `MissingFilesModel` (`ContentIO.cpp:481`) + `Editor.cpp:307` | `content_saved_table_survives_a_state_reload_and_differs_from_the_saw_fallback`, `content_wavetable_editor_imports_real_files_saves_and_exports`, `content_missing_files_dialog_logic_locate_remove_and_hash_check` |
| c. sample load, real waveform, root note, Auto slices, Snap, Trim, marker animation | `OscWaveView.cpp` (`loadSampleFile` :255, `trimSample` :283, `snappedMarker` :353, `autoSliceMarkers` :370, `modulationBands` :388, `paintSample` :1204, `paintUserTable` :970); core `SamplePreview`, `snapMarker`, `trimSampleWav`, `probeSampleWav` | `content_sample_load_plays_the_content_sample_at_the_key_pitch_through_the_shell` (WAV and FLAC, root note from the name, pitch 440 / 880 Hz, snapshots differ, slices = engine rule, snap modes, trim, remove), core `root_note_comes_from_the_file_name_or_the_wav_chunk`, `sample_preview_slices_follow_the_engine_rule_and_the_markers_snap` |
| d. Convolve IR | `FxConvolvePanel.cpp:187` (Load IR... opens a file chooser, IR envelope drawn from the file, Remove), `ContentIO.cpp:294` | `content_convolve_impulse_response_makes_a_tail` (no tail without IR, tail after the key is released with it, back to bypass after Remove) |
| e. Vital import in the plugin | `core/vital-import/plugin_import.cpp` + `vital_plugin_import.h` (`.vital`, `.vitalbank`, `.vitaltable`, `.vitallfo`, `.lfoshape.json`), `ContentIO.cpp:358` `importVitalFile`, `Editor.cpp:327` menu entry, `ContentSheets.cpp` report sheet, `LfoShapeEditor.cpp` file loader | core `vital_import_inside_the_plugin_with_synthetic_fixtures`, `content_vital_import_inside_the_plugin_with_the_report_sheet` (synthetic fixtures of `core/vital-import/tests`, no real preset in git) |
| f. user library service | `core/include/synthyboy/user_library.h`, `library_items.h`; `UiHost::userLibrary`; `LibraryMenu.{h,cpp}`; LFO shape presets + favourites (`LfoShapeEditor.cpp:282`), formula presets / own formulas / examples (`WavetableEditorSheet.cpp:856`), FX rack presets (`FxRackView.h:327`), `voice_control` category for WP-07 | core `user_library_round_trips_items_favourites_and_refusals`, `library_item_formats_formula_examples_fx_rack_and_lfo_shape`, shell `content_user_library_service_lfo_shapes_formulas_and_fx_racks` |
| g. CLAP / mobile (Q39 default) | `shell-clap/synthyboy_content_ext.h` (`synthyboy.content`: count, get, import_file, attach, remove, import_vital), implemented in `synthyboy_clap.cpp`; JUCE shell uses `juce::FileChooser` via `UiHost::requestFile` | `core/tests/test_clap_content.cpp` (import by path, list, attach -> the sample plays at the key pitch, remove -> silence, errors) |

UiHost additions (both shells and the test hosts through defaults): `contentLibrary()`, `userLibrary()`, `userDirectory()`, `requestFile()` + `canRequestFile()`, `contentRevision()`; the processor takes `setFileRequestHandler` for tests.

## Verification

* Build: `ninja -C build-wp01` (all 1025 steps, VST3 + CLAP included); zero compiler warnings (only the linker's "ignoring duplicate libraries", which the test targets already produced).
* Affected suites: `synthyboy_test_user_content` 8 cases / 206 checks, `synthyboy_test_clap_content` 1 / 33, `synthyboy_test_ui_content` 7 / 202, all green; the existing UI suites (ui_render, ui_gaps, ui_panels, ui_lfo, ui_shellfeat, ...) green.
* `ctest -j8`: `100% tests passed out of 64` (`synthyboy_test_threads` passed in the full run).

## Exact remainder

* S07-06 (own sample for the Noise source): not built, it needs WP-04 (`noise.type`); the flow `loadSampleFile` is target-agnostic (A324, morning question 51).
* Marker animation shows the modulation range band, not the live marker (needs the WP-17 modulation read-out; A315, morning question 49).
* The JUCE-wrapped CLAP build does not expose `synthyboy.content` (the wrapper has no hook); the native shell does (morning question 50).
* `Embed in Preset` writes the embedded pack, not bytes into the document (A310, morning question 48).
* FX rack presets cover the whole chain, not a single bus (A320, morning question 53).
* The wavetable editor of oscillators 2 and 3 opens from the oscillator view's Edit button; the Wavetable panel stays the osc 1 panel (A323).
* Conflicts to expect at the merge: `PluginProcessor.cpp/.h` and `UiHost.h` (WP-02), `OscWaveView.cpp` (WP-11 / 12 / 13), `Editor.cpp` menu ids 60 / 61, `core/CMakeLists.txt` (three source lines and the `synthyboy_sampleosc` link), the top-level `CMakeLists.txt` (new test target) and the assumption / question numbers.
