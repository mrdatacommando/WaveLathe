// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace wavelathe
{
// One place the build's version lives, shown beside the name in the header so
// it is obvious which build is running. Bump it with every change that reaches
// the user - minor for a new feature, patch for a fix or a tidy-up.
//
// Not quite one place any more: project(WaveLathe VERSION ...) in CMakeLists.txt
// carries the same number, because that is what a DAW reads off the plugin and
// this header is what the standalone shows in its own header. Move both.
//
//   0.49.0 WaveLathe is open source, under the GNU Affero General Public
//          License, version 3 or later. It is free and it stays free: a
//          version released under it cannot be taken back, and anything made
//          from one carries the same licence.
//          LICENSE holds the licence text. THIRD_PARTY_NOTICES.txt holds the
//          licence of every bundled component in full, each copied unaltered
//          out of JUCE 9.0.1. The release package carries both files, and
//          says where the source is.
//          The newest package lives in the repository too, in Releases,
//          with the README's download link pointing straight at it: one click
//          for somebody who wants a copy to run and has never used git.
//          make-release.ps1 swaps the zip and rewrites the link.
//          Every source file starts with a copyright line and an SPDX tag,
//          and the plugins' website field is the repository.
//          Questions and bug reports go to the repository's issues. A crash
//          dump has to be zipped before GitHub will take it, and every place
//          that asks for one says so.
//          Nothing about the sound, the files or the parameters changed.
//   0.48.2 Dial readouts that drop their decimal above ten now drop it. They
//          asked JUCE for zero decimal places, which JUCE reads as "default
//          format" - six significant figures - so the Master page's threshold
//          read -10.5827dB, found in a screenshot for the guide. Seven
//          readouts shared it (the mastering and bus dB dials, both EQs,
//          Cutoff above 10k, times over ten seconds, Sub Wave); all go through
//          ui::toFixed now.
//   0.48.1 Two messages that were wrong, found the first time 0.48.0 was
//          looked at in the app. Save Project said "Synth settings only - the
//          sequencer is empty" whenever there were no synth notes: wrong for a
//          project, which is every page, and blind to the beat, so a project
//          that was only drums was reported as empty. It now says what the
//          file carries on top of every page's settings - notes, drum hits,
//          lanes, samples, a sampled wavetable - singular when there is one.
//          And cancelling the preset browser put the patch back twice: Cancel
//          restored it and closed the window, and the browser's destructor,
//          there for the title bar's close box, restored it again. Once now,
//          and a second press before the window goes is ignored.
//   0.48.0 A synth preset is the Synth page's sound, and a project is
//          everything - and the menu says which is which.
//          Save Synth Preset, Load Synth Preset, Browse Synth Presets and
//          Init Synth Patch, under a Synth Presets heading. Each does exactly
//          what its name says: the oscillators, filter, envelopes, modulation,
//          the Synth page's own effects, the arpeggiator, Play in Key and any
//          sampled wavetable. Nothing else, either way round.
//          A project saves and loads everything on every page - the sound, the
//          tempo, the pattern and automation, the whole kit with its samples,
//          and both halves of the Master page.
//          Three things were wrong with the old line. Save Preset saved the
//          WHOLE project - pattern, kit, samples, Master page - from before
//          projects had their own menu item; the same session is now 178 KB as
//          a project and 1 KB as a synth preset. Loading a preset brought the
//          tempo and the mastering chain with it, so choosing a lead sound
//          moved the transport and reset half the Master page. And Quick Match
//          and Deep Match applied their result as a whole project: an estimate
//          or a factory patch, with an untouched kit, a dry bus and mastering
//          off - so matching a sound reset the drums and the Master page
//          underneath it.
//          The "this preset has a pattern in it" question is gone with them. A
//          synth preset carries no pattern; somebody who wants the pattern
//          saves a project.
//          Play in Key stays with the sound, as a judgement: the factory
//          presets are voiced with it, and Chord Pulse is its chord mode.
//          One enum, FxpPreset::ApplyScope, where there were two booleans.
//   0.47.2 A bypassed mastering chain is as late as it says it is.
//          The chain tells the host it costs 72 samples at 44.1 kHz - the
//          limiter's lookahead plus the oversampler's filters - and the host
//          compensates by that whether mastering is on or off. Bypassed, the
//          chain handed the audio straight back instead, so a host played the
//          whole track about a millisecond and a half EARLY with mastering
//          off - which is the default, so that was everybody.
//          Bypass now plays the input exactly that late, still bit for bit.
//          The number the host was given is true in both states, and
//          switching mastering on and off no longer shifts the track in time.
//          The bypass line is fed on every block, working or not, so switching
//          bypass on plays on from the last samples in rather than replaying
//          whatever was left from the last time it was off.
//          How it hid: the bypass test demanded the input back UNdelayed, and
//          the latency test checked only that the reported number held still.
//          Nothing checked the audio kept to it. A test does now - bypassed
//          and working chains, one sine, lining up to -102.5 dBFS.
//          0.47.1's display fix is undone as a result: the Master page takes
//          the lookahead out whether the chain is working or not, because now
//          it is always there to take out.
//          Shared with WaveLatheMaster, which gets the same fix. The standalone
//          gains the same millisecond and a half with mastering off, which is
//          below anything a player would feel.
//   0.47.1 The Master page's picture lines up with its own ghost.
//          Found in the first look at 0.47.0: with the bus dry and mastering
//          off - the defaults, so what everybody sees first - the MASTER trace
//          sat a quarter of a cycle away from the grey note it was drawn over, where
//          the two should have been one line. The display took the mastering
//          chain's lookahead back out of what came through it. But a bypassed
//          chain hands the audio back untouched and UNdelayed while still
//          reporting the lookahead, so the correction shifted it by samples it
//          never had. It is only taken out now while the chain is working.
//          Checked in the same round and needing nothing: the bus layout,
//          stages 1-7 and 8-11, a bus reverb's tail on the envelope, the BUS
//          EQ curve on the spectrum, SYNTH EQ and WAVEFORM coming back on the
//          Synth page, and undo stepping back through two bus dials by name.
//   0.47.0 The Master page has bus effects: the Synth page's seven stages a
//          second time, over synth AND drums together, before mastering.
//          Distortion, Chorus, Phaser, Delay, Reverb, EQ and a Gain in dB,
//          numbered 1-7 across the top of the page with the mastering chain
//          continuing 8-11 below them, so the order the signal takes reads
//          straight off it. A reverb here puts the kit and the bassline in one
//          room, which neither of their own effects can do.
//          The same MasterEffects class, now reading an EffectSettings struct
//          rather than SynthParameters, so one chain serves two sets of dials.
//          The Synth page's path is byte-identical - its new Gain stage is
//          pinned at unity and skipped, and BankAuditTest fails seven factory
//          presets if it is not.
//          Untouched, the bus is an exact passthrough: dry, flat, unity, and
//          every stage skips itself. So every older project sounds exactly as
//          it did. Saved as preset v25 with the PROJECT, not the patch - a
//          patch rewriting the room the drums are in is the surprise the drum
//          rack is already gated against. Not host-visible, so not MIDI
//          learnable either; learn is keyed by the same registry id.
//          Its tail is added to what the host is told, so a bounce waits for
//          the bus reverb instead of cutting it off.
//          The displays follow the page: WAVEFORM and ENVELOPE become MASTER,
//          the patch's C3 rendered through private copies of both chains, over
//          a grey ghost of it going in. One scale for the pair, so a gain
//          change or a limiter's squash shows; the lookahead is taken back out
//          so before and after line up. The spectrum's EQ curve is the bus EQ,
//          and says so.
//   0.46.1 The note under each drum strip's name can actually be read.
//          0.45.0 drew it from DrumPanel::paint(), which in JUCE is BEHIND
//          the children - and the Drum Kit box is a child, filling the whole
//          mixer with the panel colour at 62% opacity. So every note sat under
//          that fill. Found in the running app, not by the suite: the brightest
//          pixel of "C1 (36)" was (58,64,75) on a (25,28,35) ground, about
//          1.6:1, where a dial name reaches (139,147,166).
//          The first fix was the wrong one and is worth recording as such:
//          brightening the text and dropping its alpha got it to (70,76,88),
//          which is textDim seen through 62% of panel to the unit - the number
//          that proved where the fill was. Drawn from paintOverChildren now,
//          the note measures (139,147,166), identical to the "Level" label
//          under it. The row holds no control, so drawing over the children
//          there covers nothing, and the unused-voice wash is laid after it so
//          a dimmed strip's note dims with it.
//          The 0.46.0 lane filter and the 0.45.0 transpose were checked in the
//          app in the same round and needed nothing.
//   0.46.0 The automation strip shows the lanes for the grid that is up.
//          Synth lanes under the note grid, drum lanes under the drum grid,
//          and a Show all button that brings back everything. The plan named
//          this choice before any drum dial was automatable and it was never
//          made, so it fell out as "show everything" - harmless at 48 drum
//          dials, a strip of mixed lanes at 108.
//          The button counts what the filter hides - "Show all (3 hidden)" -
//          and an empty strip over hidden lanes says so, because the plan's
//          own objection to filtering is that a lane you cannot see is a lane
//          you forget is recording.
//          Clear removes what is SHOWN. It says "Clear" when filtered and
//          "Clear All" only when all is in view, and the display reports how
//          many lanes of the other kind it kept. A Clear that reached hidden
//          lanes would delete automation nobody was looking at.
//          Add Lane offers the current grid's dials, so a new lane cannot
//          vanish the moment it is made. The strip keeps its height across a
//          view change, so switching grids does not reflow the page.
//          The strip, Clear and SequencerTest all ask LaneFilter.h, so what is
//          drawn and what is cleared cannot disagree.
//   0.45.0 The kit's trigger notes can be moved, by the octave or by the pad.
//          Options > Drum Notes shifts the whole General MIDI map an octave
//          at a time, three either way, and names the note the kick lands on.
//          Right-click a strip > Learn trigger note arms that slot, and the
//          next pad hit on the drum channel becomes its note.
//          Each strip prints the note that plays it, as name AND number -
//          "C1 (36)". The number because the name is not enough: every DAW
//          agrees this note is 36 and they do not agree what to call it.
//          WaveLathe and Ableton say C1, FL Studio and Reaper say C2 for the
//          same note, and without the number that mismatch has nothing to
//          explain it.
//          A learned note is ABSOLUTE - it was learned by playing a pad, so
//          the transpose leaves it alone. A taught slot comes OFF the GM map,
//          or it would answer two notes. One note reaches one drum: teaching
//          a note another slot holds takes it off that slot, the way the CC
//          map has always settled it. The learn eats the note rather than
//          also playing it.
//          GM still FOLDS for untouched slots, which is why this is an
//          override layer and not a table of twelve notes: its three low toms
//          all reach LoTom, and a slot cut down to one note would stop
//          answering the other two.
//          Kept with the application beside the drum channel and the CC map,
//          not with the project - all three describe the desk rather than the
//          song, and a map in a project file would mean opening somebody
//          else's kit moved your pads. No preset format change.
//   0.44.0 Every dial in the kit can be MIDI learned and automated.
//          All nine controls on all twelve voices, where four of the nine
//          were reachable before. The parameter cap went from 128 to 192 to
//          fit them: the synth's 63 plus the kit's 108 is 171.
//          The four that were missing were chosen with an argument about
//          automation lanes - a selector is set once while building a kit -
//          and that argument was answering the wrong question. Wanting a hand
//          on Tune while the take runs is not the same wish as wanting a Tune
//          curve drawn under it, and the two share an id space.
//          THIS BREAKS SAVED DRUM AUTOMATION AND MIDI ASSIGNMENTS. Drum ids
//          were voice x 4 + offset and are now voice x 9 + control, so Snare
//          Level moved from 67 to 72. A lane saved on Snare Level plays into
//          Snare Pan now. The synth's sixty-three did not move. Taken
//          deliberately rather than appending the new sixty after the old
//          forty-eight, which would have split a voice's controls across two
//          ranges for good.
//          The send selectors are stepped parameters now, so a DAW shows
//          "Reverb 1" rather than "1.00" and cannot draw a ramp through
//          positions the engine rounds away.
//          Two parallel lists died with the distinction: the panel's second
//          table of ranges for the dials with no registry row, and the
//          offset function that mapped registry order onto storage order.
//   0.43.0 The computer keyboard plays the kit on the Drums page.
//          Q W E R T Y U I O P [ ] strike the twelve slots, left to right,
//          under twelve strips that read left to right. Hold Shift for an
//          accent - the kit keeps a quarter of its range back for accented
//          hits and a computer key has no velocity to reach it with, so
//          without that the loudest thing playable from the keyboard would be
//          three quarters of the loudest thing the kit can do.
//          The down edge only, because a drum is struck rather than held: a
//          leant-on key is one hit and not a machine-gun at the polling rate.
//          Anything the note keys were holding is let go of AT the page
//          change rather than at the next tick. The two sets overlap - W, E,
//          T, Y, U, O and P are notes on one page and drums on the other - so
//          a key held across the change would have its release read by the
//          wrong handler and the note would hang.
//          auditionDrumVoice takes an accent now, and the struck and accented
//          slots share ONE atomic word rather than two. Two would have to be
//          exchanged one after the other, and a hit landing between the two
//          exchanges would have its accent read against the next hit.
//   0.42.0 The kit has its own MIDI channel, its own page, and an Attack
//          that means the same thing wherever Decay sits.
//          A Drum Channel beside the Keyboard Channel in Options, off by
//          default. Notes on it strike the kit by the General MIDI map - C1
//          kick, D1 snare, F#1 closed hat - and never reach the synth; notes
//          on the keyboard's channel never reach the kit. Setting both to one
//          number is a split rather than a layer, which is the only reading
//          that does not put a bass note under every kick. Hits carry their
//          sample offset, so a pad lands where it was played rather than at
//          the top of the block.
//          On the Drums page the keyboard and the arpeggiator are dimmed -
//          neither plays the kit - and the Waveform and Envelope at the top
//          show the drum being worked on rather than the patch. Turn Decay on
//          the crash and the envelope in the corner is the crash's.
//          Attack is a quarter of however long the drum CURRENTLY rings, so
//          Attack and Decay sit at 20:80 of the hit. It used to be a
//          hand-tuned column of sixteen numbers measured against each voice's
//          nominal length, which did not follow the Decay dial at all: a kick
//          at Decay minimum rings for 140 ms and could still be given a
//          120 ms attack - 86% of the sound - while the same dial position at
//          Decay maximum was 9%. All sixteen of those numbers turned out to be
//          27-33% of their own decay, so the column was a ratio nobody had
//          written down. The curve is exponential rather than squared, which
//          puts half the travel inside the first eighth of the range where a
//          drum's useful attacks are, and still reaches exactly zero.
//   0.41.0 Eleven drums and a blank, and a slot can be any of them.
//          Checked against the machine this kit is modelled on: an 808, and
//          the RD-8 that clones it, has ELEVEN instrument channels and gets a
//          whole genre out of them. This had sixteen rows, and the four extra
//          were width and height spent on a grid nobody was filling. The kit
//          is twelve now - the 808's eleven, which slots 0-10 already were,
//          and one blank for a sample.
//          Nothing was lost by it, because which DRUM a slot makes is a
//          choice now rather than its position. That is the 808's own Tom /
//          Conga toggle generalised: on the hardware one channel is a low tom
//          or a low conga depending on a switch, because a tom and a conga
//          are not worth a channel each. Every slot has that switch here and
//          it reaches all sixteen drums - Clave, Maraca and the two Percs
//          included, which came off the kit and not out of it. Right-click a
//          strip to pick one, or to load a sample, or to empty it.
//          The order of the eleven is unchanged, deliberately. Matching the
//          hardware's front panel would have read better left to right and
//          would have changed what every saved pattern and every automation
//          lane means - a bar would play its claps as toms.
//          The choke follows the drum rather than the row, so a closed hat
//          cuts an open one wherever the two have been put, and the hat rows
//          choke nothing once the hats are moved off them.
//          Forty-eight host parameters rather than sixty-four, so the
//          registry is 111 of 128 where it was 127. Seventeen ids spare where
//          there was one.
//          Preset format v24, and the first version where a block CHANGES
//          SHAPE rather than being appended: the v19-v22 blocks were sixteen
//          voices and are twelve now. The old count is frozen beside the old
//          control lists, so a file from v19 on is still read at the size it
//          was written. A project carrying hits or samples on a row that has
//          gone loads anyway, and the display names what was dropped rather
//          than losing it in silence.
//          Opening a project from a file restored the pattern and the mixer
//          and none of the samples - the same gap undo had, in the same
//          place, for the same reason: two paths restore a whole project and
//          only the host's did the slots. Fixed with it.
//   0.40.0 Samples are chosen by ear, and every voice has a lamp.
//          A sample library is a folder of names that all describe the same
//          drum, and the native file dialog cannot say what is highlighted -
//          so picking one meant loading it, listening, and opening the dialog
//          again for the next. The browser previews as you move: the sample
//          really goes onto the pad, so what you hear is that voice with its
//          Level, Tune, Attack, Decay, Pan and both sends, in the groove if
//          the pattern is running. Load keeps it, Escape puts back what the
//          slot held, and the whole search leaves ONE entry in the undo
//          history rather than one per file arrowed past. The same bargain
//          the preset browser already made.
//          JUCE will host a preview inside the native dialog on Windows, but
//          only by falling back to the pre-Vista one - no places sidebar, no
//          search, no recent folders. That is the wrong trade for somebody
//          working through a library, so this uses JUCE's own browser.
//          Previewing per keypress broke the bargain the sample slots were
//          built on. Every sample ever loaded was kept for the life of the
//          plugin, which is fair when loading one is a deliberate act and is
//          a leak when arrowing down a folder loads thirty. They are now
//          held for a minute after being displaced - longer than the longest
//          possible hit, which is ten seconds of recording at the slowest
//          Tune plus six of reverb tail - and then let go. A static_assert
//          holds that arithmetic to the constants it is derived from.
//          The lamp beside each Level dial is white while the voice sounds
//          and red when it passes full scale. Measured after both sends,
//          which is the only reading worth having: a hit at a sensible level
//          driven into the distortion clips there rather than in the voice.
//          Clipping latches for a second, because one sample over at thirty
//          frames a second is nothing anybody would see.
//          Undo puts the sample slots back, which it did not before. Two
//          paths restore a whole state - opening a project, and undo, redo
//          and crash recovery - and only the first touched the slots, so
//          undoing a sample load said "Undid kick sample" and left the sample
//          on the pad. The snapshots had always carried them; nothing applied
//          them. Both paths go through one function now.
//   0.39.0 There is a mastering stage now, and it is also a plugin of its
//          own.
//          The reason for starting was aliasing rather than any of the things
//          that got built. Two waveshapers here run tanh at the base sample
//          rate, and tanh makes harmonics without limit: everything above
//          Nyquist folds back into the audible band as inharmonic content,
//          worst exactly when the drive is pushed hardest, which is when
//          somebody is reaching for it wanting warmth. The new saturation
//          runs at four times the rate. On a 5 kHz tone at drive 0.9,
//          measured at 9.1 kHz where the seventh harmonic folds to and where
//          no harmonic of 5 kHz can be: -19.95 dB before, -103.20 dB after.
//          Polyphase IIR rather than the linear-phase FIR, which looks wrong
//          for a mastering stage until you ask what is being mastered. Linear
//          phase rings BEFORE a transient, which no analogue circuit has ever
//          done, and this exists to sound more analogue rather than less.
//          The compressor keeps its envelope in decibels rather than as a
//          gain multiplier, so a release time sounds the same at three
//          decibels of reduction and at fifteen. Stereo-linked, because two
//          independent detectors pull the image towards whichever channel is
//          quieter. Fixed six-decibel knee rather than a dial, on the
//          reasoning 0.38.6 used about the clock smoothing switch.
//          The limiter looks ahead rather than clipping. A clipper holds a
//          ceiling by generating exactly the harmonics the stage above it
//          just spent four times the CPU preventing. Its ceiling is a
//          structural guarantee: the target is the worst requirement anywhere
//          in the visible window and the gain may fall by at most one over
//          the lookahead per sample, so there is always exactly enough
//          runway. Driven 18 dB too hard with single-sample transients, the
//          worst output sample sits 0.000001 dB above it.
//          Off by default inside the synth. Sixty-five factory presets were
//          voiced without any of this, and a stage that engaged itself would
//          have changed every one of them the first time this version opened
//          an old patch. Preset format goes to v16; a v15 file keeps the
//          defaults, which have it switched off.
//          The twelve new dials are saved with a patch and are not
//          automatable. The registry is capped at 64 by the atomic word the
//          sequencer reads and has one slot left; twelve would need twelve.
//          That slot is deliberately still free, because ids are appended and
//          never inserted, so a registered parameter cannot be withdrawn
//          without breaking every automation lane saved after it.
//          Which is most of why WaveLathe Master exists as well as the page:
//          the same chain and the same panel as a separate effect plugin, on
//          a mix bus, catching whatever is routed into it - including
//          instruments nobody here wrote, which is the half a stage inside an
//          instrument can never do - with twelve automatable parameters
//          against a fresh sixty-four. It starts switched on, because
//          somebody who has put an effect on a bus has already said what they
//          want.
//          Latency is reported from both, and reported whether the stage is
//          on or not. A number that appeared when a control moved would make
//          a host resynchronise mid-performance.
//          Also: projects had been saving correctly since the rename from
//          WaveForge and then being filtered out of the dialog that opens
//          them. The extension moved to .wlp and the chooser's wildcard did
//          not. Written to disk exactly as asked and then hidden, which from
//          the outside is indistinguishable from saving not working, and
//          arguably worse - the work was there the whole time with no way to
//          reach it. The wildcard is built from the extension now.
//
//   0.38.8 The C++ runtime is inside the binary now, so whether WaveLathe
//          starts on somebody else's machine no longer depends on what else
//          they happen to have installed.
//          It imported MSVCP140.dll, VCRUNTIME140.dll and VCRUNTIME140_1.dll.
//          Those are not part of Windows. They arrive with Microsoft's Visual
//          C++ redistributable, which lands on a machine because a game or an
//          office suite or a graphics driver put it there. Most machines have
//          one, and that is exactly what makes it worth removing rather than
//          writing down: it works on every machine anybody thinks to try and
//          fails on the one that happens to be clean.
//          A copy was run on a second PC and opened, made a sound, loaded a
//          preset and played from the keyboard. Worth having - but what that
//          established is that the machine had the redistributable, not that
//          nothing needed one. A pass for a reason you did not test is not a
//          pass for the thing you meant to test.
//          The plugin was the half that mattered. The standalone at least puts
//          the missing DLL in a dialog somebody can read out. A host that
//          cannot load a module drops it from the scan, so WaveLathe would not
//          be in the list at all, with nothing anywhere saying why, and the
//          person it happened to would conclude it does not work.
//          Safe because nothing crosses between two runtimes: the plugin
//          reaches the host through VST3's virtual interfaces, which carry
//          plain types, and JUCE and the VST3 SDK compile into these binaries
//          rather than sit beside them. Set before JUCE is fetched for that
//          reason, alongside /Zi, which is there for the same one.
//          The standalone grows by 382K and the plugin by 566K. Both now import
//          nothing but Windows' own DLLs - the Universal CRT went static with
//          the rest, so the api-ms-win-crt stubs have gone as well.
//   0.38.7 A pulse lost on the way no longer matters, which it very much did.
//          This came out of numbers from a Force that did not match the test.
//          The test said a tenth of a BPM at every tempo. What came back was 90
//          exactly, 174 reading a steady 173.7, and 120 wandering over nearly
//          two. Steady where it should have been, wandering where it should not
//          - and the wandering got worse the faster the clock ran, which is a
//          shape, not noise.
//          So the test was taught to lose the odd pulse, the way a real link
//          does. Losing one in a hundred threw the reading around by ten BPM at
//          120 and fifteen at 174, and left 90 untouched: more pulses a second
//          is more chances to lose one. The shape matched.
//          The cause was counting arrivals. Each pulse was numbered by the
//          order it turned up in, so the one after a lost pulse was called the
//          next when it was really the one after next, and the line got fitted
//          across a gap twice as wide as it was told. Numbered by spacing
//          instead - how many pulses should have arrived in that time - a lost
//          one leaves a hole in the numbering and the fit does not notice.
//          Losing one in a hundred now reads identically to losing none, at
//          every tempo tried.
//          Tolerating that opens a hole worth naming, because it is the kind
//          that hides: halve the tempo on a sender and every gap doubles, which
//          is indistinguishable from losing every other pulse. Read as drops,
//          the fit would agree with itself at the old tempo and never notice.
//          A link drops one occasionally; it does not drop every other one, so
//          three in a row at a multiple of the spacing is the sender having
//          slowed. 140 to 70 is tested, and lands on 70.
//          The steady 173.7 at 174 was probably never ours. It does not move,
//          and a reading that is wrong by the same amount forever is not a
//          reading that is wobbling - grooveboxes make clock by dividing a
//          timer, and the rate they can hit is not always the number on the
//          front. Nothing here would fix that, and nothing here should try.
//   0.38.6 The clock window works out for itself how long it should be, which
//          is better on both counts than a switch asking somebody else.
//          The suggestion was an option - smooth the clock over one to three
//          seconds, on by default. The lever was the right one. A longer window
//          is exactly what buys stillness, and error falls off faster than the
//          span grows, so two seconds takes half a BPM of movement down to a
//          tenth of one.
//          What was wrong with it was the switch. On by default means almost
//          nobody ever sees the other position, so the off setting exists for
//          the one case - following a sender that changes tempo - which the
//          filter can notice by itself. And it would have been a dead control
//          in a plugin, where the host hands over its tempo exactly and none of
//          this applies at all. There is already one menu item in that state
//          and it took until 0.38.1 to admit it.
//          So: two windows over the same pulses. The long one, two seconds, is
//          what the tempo is read from. The short one, three tenths, is watched
//          and never used. Noise cannot hold a steady offset between them - it
//          disagrees and then agrees again - while a tempo change disagrees at
//          once and keeps on disagreeing. Three pulses running outside one and
//          a half percent is the sender having moved, and two seconds of
//          averaging built at the old tempo is then worse than none.
//          It collapses to the freshest eight pulses rather than to the short
//          window, which is the part that was wrong on the first attempt: three
//          tenths of a second still holds pulses from before the change, so
//          rebuilding from it landed halfway and crawled the rest. A change
//          read 132.7 that way and reads 139.6 this way.
//          Measured across four tempos with three milliseconds of jitter: 0.09,
//          0.09, 0.10 and 0.09 BPM of movement at 90, 120, 145 and 174. It was
//          five at 120 and nine at 145 before any of this started. And a change
//          of tempo now lands inside half a second, where the shorter window
//          took a whole one - steadier and quicker, rather than one traded for
//          the other, which is what the switch would have been choosing between.
//   0.38.5 The clock window is a length of time rather than a number of pulses,
//          which is what was left of the wobble.
//          0.38.4 fitted the tempo across twenty-five pulses. Twenty-five of
//          anything is a different amount of time at every tempo: a third of a
//          second at 174 BPM against two thirds at 90. The accuracy of a line
//          follows the span it is drawn across, so the reading held twice as
//          steady slow as fast - which is exactly what came back from a Force
//          at both ends, about a BPM either way at 90 and two at 174.
//          Three quarters of a second now, whatever the tempo, with the number
//          of pulses falling out of that instead of being fixed. Measured
//          across four tempos with three milliseconds of jitter: 0.35, 0.48,
//          0.67 and 0.58 BPM of movement at 90, 120, 145 and 174. Flat, where
//          before it tripled between 90 and 145.
//          174 is in the test now because that is where it was heard. A tempo
//          somebody actually reported is worth more as a test case than three
//          round numbers.
//   0.38.4 A tempo taken from MIDI clock that holds still. It never has, and
//          the readout added in 0.38.3 is what finally showed it.
//          A steady hardware clock made the reading jump by five BPM at 120 and
//          nine at 145. The arithmetic was never wrong - fed a clean clock it
//          returned the tempo exactly - and setting the local BPM to match
//          changed nothing, because once a clock is arriving the local value is
//          not consulted at all.
//          The positions it was reading are not when the clock arrived. JUCE's
//          MidiMessageCollector measures the wall-clock gap between audio
//          callbacks and rescales every message into the block to make it fit,
//          so each position carries a few milliseconds of whatever the
//          scheduler was doing. Against the 20.8ms pulse of 24-PPQN clock that
//          is a tenth of the interval, and a tempo read off one interval
//          inherits every bit of it.
//          So it is no longer read off one interval. Twenty-five pulse
//          positions are kept - twenty-four is a quarter note, so that is one
//          beat of them - and a straight line is fitted through the lot by
//          least squares. A line rather than the distance from the first to the
//          last: both average the noise down, but a line uses all twenty-five
//          points instead of two, so one badly placed pulse cannot drag the
//          answer along with it.
//          Measured, with three milliseconds of jitter either way, which is
//          ordinary scheduling noise on Windows. At 120 BPM the reading moved
//          5.19 before and 0.99 after; at 145, 8.77 before and 1.42 after. A
//          clean clock now reads its own tempo to the second decimal.
//          It still moves when the sender does: 120 to 140 arrives inside a
//          second, and that is checked too, because a window long enough to
//          ignore noise is long enough to ignore a real change and only a test
//          knows which of the two it is doing.
//          None of this had ever been tested. TempoLinkTest has always
//          exercised MusicalClock, the beat clock the arpeggiator and the
//          sequencer run on. TempoClock, which is what turns pulses into a
//          tempo, had nothing on it at all and shipped wrong from 0.3.0. The
//          wobble was driving the arpeggiator and the synced delay the whole
//          time; the dial just showed the local number, so nothing on screen
//          ever disagreed.
//   0.38.3 Tempo comes from the host. It never had, which meant everything
//          that syncs was in time with nothing.
//          There was no call to getPlayHead anywhere in the synth. The
//          arpeggiator, the sequencer, the synced delay and the synced LFOs all
//          ran at whatever the BPM dial said while the song ran at its own
//          speed, so a patch saved at 154 played its arpeggio at 154 inside a
//          project at 120. Found by moving the dial in Live and noticing the
//          transport did not care - which is right on its own, since a plugin
//          has no business setting the host's tempo. The missing half was the
//          other direction.
//          MIDI clock still drives the standalone, and the host outranks it in
//          a plugin. Clock is how hardware drives this thing when it is on its
//          own; a plugin is already sitting inside the thing driving it.
//          The readout tells the truth now, which it had not been doing even
//          for MIDI clock. getBpm has returned the clock's tempo since 0.3.0
//          without ever writing it back, so the dial went on showing the local
//          number while the synth played at the external one. The host case
//          would have been a second copy of the same lie.
//          A dial carries 128 positions across 20 to 300 BPM, which is a step
//          of better than two: fine for choosing a tempo, useless for reporting
//          one, and a host at 120.00 lands on 119. So the pointer follows
//          approximately, as a pointer must, and the number under it is exact
//          and marked ext.
//          The local BPM is left alone while that happens. It is what the patch
//          plays at back in the standalone, and a tempo borrowed from a host
//          for an afternoon has no business overwriting it.
//   0.38.2 The factory bank is the host's preset menu too.
//          getNumPrograms had returned 1 since the first build, which is what a
//          plugin with nothing to offer says, so a DAW's preset menu sat empty
//          while sixty-five sounds waited inside - reachable only from
//          WaveLathe's own browser, which a host cannot see into. A host counts
//          programs to decide whether to draw that menu at all.
//          Names carry their category: "Bass: Sub Rumble" rather than "Sub
//          Rumble". The bank is already in category order, so the prefix sorts
//          nothing - it labels the runs that were there anyway, and turns one
//          flat list of sixty-five into seven readable groups.
//          A program change brings the sound and nothing else. Not the pattern,
//          because choosing a preset is not the same act as replacing the bar
//          you are writing - the line drawn in 0.24.0, honoured now at the
//          host's door as well as the browser's. And not the tempo: in a host
//          the transport belongs to the host, and a preset that reached in and
//          moved it would be a preset nobody would dare audition. That is a new
//          argument to applyToSynthParameters rather than a change to what the
//          standalone does, where the patch IS the session and a preset that
//          plays at the wrong speed is the wrong preset.
//          The host is told afterwards, or it writes its own sixty-three values
//          back over the patch that just arrived - the failure 0.36.2 fixed for
//          the editor's preset loads, arriving through a different door. Posted
//          through an async update rather than done on the spot, because VST3
//          carries programs as an automatable parameter and a program change
//          can therefore land inside a block, on the audio thread.
//          The panel notices the same way it notices a MIDI bind: by watching a
//          counter on the tick rather than being told, since the thread the
//          change arrived on may not touch a component.
//   0.38.1 Licence and contact details for the beta. Both were replaced in
//          0.49.0, when WaveLathe became open source.
//   0.38.0 WaveForge is WaveLathe. Every entry below this one was written
//          under the old name and still says it.
//          The name was taken, several times over and in this exact market: a
//          modular synthesiser for three platforms, a DAW on the Microsoft
//          Store, and a record label going since 1998 that styles itself
//          Waveforge(tm). Nothing had shipped yet, which made this the last
//          moment the change was free. A VST3 is identified by its plugin name
//          and its two four-character codes, so renaming after a release would
//          have left every saved session in every host hunting for a plugin
//          that no longer existed.
//          A lathe shapes by spinning the work against a fixed tool, which is
//          what a wavetable oscillator does to a single cycle. The same kind
//          of name as the old one, without standing in Sound Forge's shadow.
//          The maker is MrDataCommando rather than the product's own name, so
//          a host lists WaveLathe by MrDataCommando instead of WaveLathe by
//          WaveLathe. Manufacturer code MDCo, plugin code Wlt1, bundle id
//          com.mrdatacommando.wavelathe.
//          Projects are .wlp rather than .wfo. The format is unchanged, so an
//          old project still opens once its name is changed.
//          Settings move to %APPDATA%\WaveLathe. An existing MIDI map is not
//          lost, but it is not found either, until the old file is copied over.
//   0.37.2 What a host reads off the plugin, and a version history anyone can
//          read.
//          A DAW shows more than a name. It has room for who made it, what it
//          is, and what kind of thing it is, and WaveForge had been handing it
//          a name and blanks. There is now a copyright line, a one-line
//          description, an explicit bundle id, and Synth alongside the
//          Instrument that IS_SYNTH already implied - so a host that files
//          instruments by subcategory puts this one with the synthesisers.
//          Website and email are left commented out rather than filled with
//          something plausible. An empty field is better than a dead link
//          baked into every copy that ships.
//          JUCE_DISPLAY_SPLASH_SCREEN is gone. JUCE 9 removed the splash
//          screen altogether and warns that the flag is ignored, so the line
//          had been doing nothing except looking like it was doing something.
//          The version history is a file now. It has lived in this header
//          since 0.1.0, which means it has never been visible to anybody
//          without the source - and the source is not what ships. CHANGELOG.md
//          is generated from these comments rather than written a second time,
//          so there is still only one copy of this and this is still it.
//   0.37.1 The version a DAW reads, and symbols to read a crash with.
//          The plugin had been reporting 0.1.0 since the end of August while
//          the panel said 0.37.0. Neither number was wrong where it was
//          written - CMakeLists.txt carried the right one, and JUCE generates
//          the Windows version resource out of it. Once. The rule that does it
//          declares no inputs, so it ran on the first configure of the build
//          tree and never again, and every bump since had stopped at a
//          generated file nobody ever opens. The missing dependency is named
//          now, so the number moves when the version does.
//          Release builds emit a .pdb. The one real crash this project has had
//          left a dump that could say no more than WaveForge.exe+0x15e61a,
//          because a Release link had never been asked for symbols. Placing it
//          meant lifting the faulting instruction bytes out of the dump and
//          searching all 438 object files for them - to find a heap overflow
//          in the chorus LFO, which the dump had been describing since the
//          thirteenth in the only language left to it.
//          /Zi adds the file and changes no code. /DEBUG, which is what makes
//          the linker write one, switches two Release optimisations off as a
//          side effect, so both are asked for again by name and the binary is
//          the same binary it was.
//   0.37.0 Two things that only a plugin could get wrong, and that the
//          standalone had hidden since the first build.
//          The tail is no longer zero. getTailLengthSeconds is how long a host
//          keeps rendering after the last note lets go, and it is what Live
//          reads when you bounce, freeze or export a track. It had said 0.0
//          since 0.1.0, so every export cut the release, the delay and the
//          reverb off dead - silently, and only on export, which is the worst
//          way for a thing to be wrong.
//          It is worked out from the three things that actually ring: the amp
//          envelope's release, the delay, and the reverb. Chorus and phaser
//          colour what is there rather than outliving it; the filter and EQ
//          shape without holding.
//          Measured, not estimated. It lives beside the master chain rather
//          than on the processor so MasterFxTest can render an impulse through
//          the real effects, find where it falls sixty decibels down, and fail
//          if the promise does not cover it. The reverb rings 0.4 seconds at
//          its smallest and 2.15 at its largest; it is promised 1 and 4.5,
//          because falling short cuts audio while being long costs a moment of
//          export. The first attempt promised 8 seconds at the top, which the
//          measurement showed was four times what the reverb actually needed.
//          The delay is capped on purpose, and the cap is stated as a test so
//          it reads as a decision rather than a bug. At the top of the feedback
//          dial a repeat loses five percent, so it takes 134 of them to fall
//          out of hearing - over three minutes at a second and a half apiece.
//          True, and an export nobody would sit through.
//          Crash recovery is standalone-only now. It exists because there is no
//          host to save the session, which is exactly what a host does - so in
//          a plugin it was redundant. Worse: the path is one fixed file in the
//          temp directory, so two instances on two tracks autosaved over each
//          other every five minutes, and closing either deleted the file the
//          other was relying on. Then the next standalone launch would have
//          offered to recover whichever plugin instance wrote last.
//   0.36.3 Four parameters tell the host they have positions rather than a
//          range, and say what those positions are called. Live had been
//          showing Filter Type as 3.00 and Sub Oct as 1.50, and drawing
//          automation curves through values that could never sound.
//          Filter Type, Sub Oct, Voices and Arp Octaves are choices wearing a
//          dial. The engine has always rounded them at the point of use, so
//          nothing was ever WRONG - 1.5 genuinely sounded as -2 oct and the
//          panel said so - but a host told nothing draws a ramp where there are
//          five switches, and a lane you cannot land on a value is a lane you
//          cannot use.
//          Which four was worked out by asking the engine rather than by
//          judgement: they are exactly the registry parameters it reads through
//          std::round.
//          Named, where a number says nothing. An automation lane reading
//          LP 24 is worth more than one reading 1.00, and the names can be
//          typed back in - checked before the number, because "LP 12" read as a
//          number is zero, which is also a real position, so getting that order
//          wrong would look right.
//          The names live in the registry now, not in the editor. There were
//          about to be two copies of them - one for the dial and one for the
//          host - and two copies of a list like that stay in step until the
//          first time somebody adds a filter type.
//          The step a host shows is the step the ENGINE would round to, so a
//          value between two positions never displays as a setting the synth
//          would not play.
//          23 more checks in HostParameterTest, confirmed to fail against a
//          break in each of the two paths.
//   0.36.2 Loading a preset tells the host about it. 0.36.0 said it did and it
//          did not: the notification went into applyProject, above a comment
//          claiming that was where every path loading a patch ended up. Five
//          more are in the editor and reach the parameters directly - the
//          preset browser, Load Preset File, Quick Match, Deep Match and the
//          project restore - and none of them told anyone.
//          What that looked like: load Distorted 808 in Live, and Live's panel
//          went on showing the previous patch's Cutoff, Drive and Sustain. Not
//          merely stale. The host believes those numbers, so the next time it
//          touches one it writes the old value back over the patch you just
//          loaded, and the patch appears to revert on its own.
//          Found by testing it rather than by reading it, which is the only way
//          this one was ever going to be found: the code says clearly that it
//          notifies, and the sentence above it says clearly that everything
//          comes through there, and both were wrong together.
//          Fixed as one named step rather than five calls. A patch arriving now
//          goes through adoptLoadedPatch - put it on the dials, tell the host -
//          because five call sites each remembering to do both is precisely the
//          arrangement that failed. Deliberately NOT folded into
//          refreshKnobsFromParameters, which is also how the editor catches up
//          when its window opens: telling a host that sixty-three values
//          changed every time someone looks at the panel would write sixty-three
//          automation points into an armed take for nothing.
//   0.36.1 A dial follows its parameter whoever moved it, which now includes
//          the host. Automating Sustain from Live changed the sound and left
//          the dial sitting where it was.
//          followAutomation had been asking WHICH of two things was driving a
//          parameter - a recorded lane, or a learned controller - and answering
//          "neither" for a DAW, because when that code was written there was no
//          DAW to be a third answer. Publishing the parameters in 0.36.0 made
//          one, and did not come back here.
//          The same bug as 0.27.1 with a different driver, and the comment
//          explaining 0.27.1 was sitting directly above the code that had it:
//          a stale dial is not cosmetic, because the next touch of it snaps the
//          sound back to where the dial was and undoes everything the host had
//          done.
//          It asks the parameter now instead of enumerating what might have
//          written it, so the next way of writing one is followed without
//          anybody remembering to come back here a third time.
//          Which means every dial is asked to follow on every frame, so
//          setRealValue stops when the answer has not changed - otherwise that
//          is sixty-three repaints a frame for a panel standing perfectly
//          still. It repainted unconditionally before and always had; nothing
//          had ever called it often enough for that to show.
//          No test. This is editor code and the suites do not build the
//          interface - the same gap the MIDI monitor's wiring had, and found
//          the same way, by someone using it in a host.
//   0.36.0 Every dial is a parameter the host can see. Sixty-three of them,
//          automatable from a DAW, listed by Live's Configure, and mappable to a
//          controller through the host's own MIDI mapping as well as ours.
//          The ParameterRegistry was already the manifest. It was built so the
//          sequencer and MIDI Learn could address a dial from the audio thread,
//          long before there was a host to tell - name, range, curve and an
//          append-only id, which is exactly the discipline VST3 parameter ids
//          demand. So the host's parameter is a WINDOW onto the engine's own
//          atomic rather than a second copy of it: reads read what the engine
//          reads, writes write what it writes. Nothing is mirrored, so nothing
//          can drift, which is the failure this design exists to make
//          impossible.
//          Named, not numbered. VST3 identifies a parameter by hashing the
//          string, so a saved Live set keeps pointing at Cutoff whatever happens
//          to the ordering underneath.
//          A dial now tells the host when it moves, and that is not optional
//          decoration: Live's manual is explicit that adjusting a control in the
//          plugin's own window while in mapping mode is what CREATES the entry
//          in Live's panel. A published parameter that never reports is
//          invisible to Configure, to automation recording, and to the temporary
//          entries Live puts in its envelope choosers.
//          Told without being written back. The dial has already set its real
//          value through its own range, which is skewed for the finger and is
//          not the registry's log-or-linear one - so going out through
//          normalised and back in would land a little away from what was just
//          set, and a dial would creep every time it was touched.
//          Gestures are paired, including where there was no pair to be had. A
//          drag ends when the mouse comes up; the wheel and the arrow keys have
//          no such moment, and a host told a gesture began and never told it
//          ended believes the control is still under your hand and holds its own
//          automation off it for good. Those now end when the dial settles, on
//          the same 900ms that already governs the readout.
//          Loading a preset tells the host all sixty-three. A DAW that was not
//          told still believes its own values, and writes them back over the
//          patch you just loaded the next time it touches one. Ours move nearly
//          every parameter at once, so that is not a corner case, it is every
//          load - and it is done where every load path already converges rather
//          than at each of them.
//          Who wins when the host and the step sequencer both automate a dial:
//          the host, for a moment, on exactly the terms a learned controller
//          already gets. No new rule - the question the lanes have always asked
//          is "is something other than the lane in charge of this right now",
//          and a DAW automating it is one more way of being that. A host write
//          sets a bit from whichever thread the DAW chose and the audio thread
//          turns it into the existing hold countdown, because a bit set without
//          a countdown to run it down would never clear and would lock the lane
//          out of that dial for the rest of the session.
//          Fourteenth test suite: HostParameterTest, 24 checks - the window, the
//          ends and middle of a logarithmic range, defaults read off a freshly
//          built engine rather than written down twice, text in both directions,
//          and which direction of travel announces itself. Confirmed to fail
//          against two deliberate breaks.
//   0.35.2 MIDI Learn ignores controllers 120 to 127. Those are Channel Mode
//          messages - All Sound Off, Reset All Controllers, All Notes Off and
//          the mode selects - which are instructions to the instrument rather
//          than the position of anything, and no hardware sends one from a knob.
//          A host does, constantly. Live emits All Notes Off every time the
//          transport stops, which the new monitor caught arriving on the readout
//          as CC 123 ch1 = 0, sitting beside a real controller. So arming a
//          learn in a host and then pressing stop would have bound that dial to
//          All Notes Off - after which it snapped to zero on every stop, with
//          nothing on screen saying why.
//          Refused BEFORE the learn is taken, which is the whole subtlety: the
//          arming is claimed with an exchange at the top of handleController, so
//          a reserved controller reaching that line takes the armed dial with
//          it, and one refused any later silently disarms a learn still waiting
//          for the knob you actually meant to move. Both are tested.
//          Not in the map at all rather than in it and inert: assigning one
//          directly does nothing, and a map saved by an older build drops them
//          on the way back in. An assignment the dialog lists but which can
//          never fire is worse than no assignment.
//          The synth still receives them. This map never touches the buffer,
//          and juce::Synthesiser reads All Notes Off from it exactly as before.
//          Only findable in a host, and only because the monitor was there to
//          find it with - which is the first thing stage 01 has paid for.
//          13 more checks in MidiMapTest, confirmed to fail without the guard.
//   0.35.1 Full screen is gone from the panel when WaveForge is a plugin, which
//          is what it should always have been: the window belongs to the host
//          there, and a control of ours that reaches into it is acting on
//          something we do not own.
//          It did not merely fail. The button asked for a DocumentWindow, did
//          not find one, and fell back to drawing itself in the header - the
//          path written for the moment full screen removes the title bar. So it
//          was visible and clickable in Live. And setFullScreen only refused
//          when there was no window AT ALL, so the click sailed past the guard
//          and handed LIVE'S OWN WINDOW to kiosk mode.
//          Three places were asking the same question three different ways and
//          had drifted apart: the menu asked for a ResizableWindow and was
//          right, the button asked for a DocumentWindow and had a fallback that
//          hid the answer, and setFullScreen asked for neither. One ownWindow()
//          now, and exactly one cast in the file.
//          Escape and F11 go through the same guard, since they reach it
//          without passing either the button or the menu.
//          The header keeps the space it reserved. That region is empty between
//          the tabs and the wordmark, so nothing moves when the button is not
//          there - and the standalone is untouched, full screen and all.
//   0.35.0 WaveForge builds as a VST3 as well as a standalone. One word in
//          CMakeLists - JUCE takes the plugin category from IS_SYNTH, which was
//          already declared, so it registers as an instrument rather than an
//          effect without being told. The built module says so: Instrument,
//          Synth. Nothing else about the processor had to change; the bus
//          layout and the save/load of state were already right.
//          It is NOT installed anywhere. Point the host's VST3 custom folder at
//          build/WaveForge_artefacts/Release/VST3/ instead, so a rebuild is
//          picked up in place rather than needing a copy between every change.
//          Ableton will not accept a folder that is also its VST3 system folder
//          or its VST2 one.
//          The version a host reads is not the one in this header. It comes
//          from CMakeLists, where it had said 0.1.0 since the first commit and
//          nothing had ever shown it. Both now say the same thing, and the note
//          above says to move both.
//          What this stage does NOT answer is whether a MIDI controller reaches
//          it. VST3 has no controller message; JUCE rebuilds one from hidden
//          parameters, and whether a given host fills those in is the thing
//          being tested. Notes first: if those do not arrive either, the
//          problem is routing rather than the CC mechanism, which is a much
//          easier thing to be wrong about.
//   0.34.0 A MIDI monitor, under Options. Switch it on and every message that
//          reaches the synth is listed on the readout - CC 74 ch1 = 63 - and
//          switching it off prints a count of what came: notes 24, CC 0.
//          Built for the VST3 port, and the count is the point of it. VST3 has
//          no MIDI controller message at all; a host that never delivers one
//          looks from inside the synth exactly like a controller nobody turned,
//          and "the dial didn't move" is a shrug rather than a result. The
//          count turns it into a number, and separates the two failures that
//          matter - CC does not arrive here, versus nothing arrives here -
//          which need completely different fixes.
//          Read before anything of ours touches the buffer, so what it reports
//          is what the HOST sent rather than what survived our own filtering.
//          That is not pedantry: a keyboard narrowed to one channel drops notes
//          a few lines later, and a monitor that could not see them would have
//          reported the host as silent for a setting of ours. It says so on the
//          way in when that setting is not omni. The keys on screen and the
//          letter keys are not listed either, for the same reason: they are
//          ours, not something a host sent. That is the first thing anyone
//          tries, so switching the monitor on says so.
//          One 64-bit word per message across the thread boundary rather than a
//          queue, so the whole message is taken at once - a controller number
//          paired with the previous value would print a message that never
//          happened. A sweep of one knob rewrites one line; a different control
//          starts a new one, because the test this exists for asks for two.
//          Nothing about it is saved. A monitor still on from last week would
//          fill the readout with traffic while you were reading something else.
//          Proved before being trusted, which is the whole discipline here: a
//          probe that has never been right about a known case is not evidence
//          about an unknown one. MidiMapTest gained 36 checks covering the
//          classification, the packing at every field's edge, and the counting
//          - and they were confirmed to fail against two deliberate breaks, a
//          seven-bit value field and a note-off read as a note-on.
//   0.33.4 Clearing automation while REC is still armed makes it stay cleared.
//          The lanes went, and then came straight back - which read as the
//          interface refreshing them back in, but they were real: the data had
//          them too.
//          The latch is the memory of "this dial was moved during this
//          recording pass", and it deliberately outlives letting go of the dial
//          - that is what stopped a pass made of several adjustments playing
//          back as new value, old value, new value. After a clear it was a
//          memory of something that had just been thrown away, and the next
//          tick acted on it: the lane was recreated, filled flat with wherever
//          the dial was sitting. That is why the automation looked reset and
//          the entry did not - the values WERE all the same, and the entry was
//          genuinely there.
//          Clearing a lane now ends the recording pass for that parameter as
//          well. It does not disarm REC: moving something after a clear is a
//          new statement and records normally.
//          SequencerTest covers it, and fails three assertions without the fix
//          - including the one the report was about, that they stay cleared
//          while the pass runs on.
//   0.33.3 The letter keys stop playing notes while you are typing somewhere
//          else in the app. Typing a file name into the save dialog played a
//          tune, and the letters that played it were picking items out of the
//          Options menu on their way past.
//          The note keys are read from the GLOBAL key state rather than
//          delivered as events - deliberately, so that clicking a dial does not
//          stop them working - which means nothing intercepts them on the way
//          in and every rule about when they should not sound has to be written
//          out by hand. There was a rule for another application having focus
//          and a rule for a text field having it, and nothing in between: a
//          menu, an alert, a file chooser and the preset browser are all
//          separate WINDOWS of this same process, so the foreground-process
//          check waved every one of them through.
//          All four are modal in JUCE's sense, whether they are its own windows
//          or the system's, so one question now covers them: anything modal is
//          up, or anything focused that is not part of this panel, and the keys
//          are its business rather than the keyboard's.
//          Nothing focused at all is still fine, because that is the ORDINARY
//          state here - blocking on it would block almost always.
//   0.33.2 The window opens at the size of the screen it opens ON. It had been
//          sized from the main display whatever screen it was shown on, which
//          on a second monitor meant 1922 pixels of window on a 1680 pixel
//          display, with the remainder hanging over the edge onto the other
//          one - and the right-hand third of the panel off the side of it.
//          The editor cannot know better on its own: it sizes itself in its
//          constructor, and at that point there is no window yet to have been
//          put anywhere, so the main display is the only display it can ask
//          about. The window is refitted on the first tick instead, once there
//          is one and it has been placed - to the user area of whichever screen
//          holds the largest part of it, so a window straddling two gets the
//          one it is mostly on.
//          Once, at startup. A window that resized itself every time it was
//          dragged over a monitor edge would be a window you could not drag
//          over a monitor edge.
//   0.33.1 Full screen moved into the title bar, where the window's own
//          controls are. It had been sitting alone beside the tabs, which made
//          a control that acts on the WINDOW look like one that acts on the
//          synth. It is placed by asking the window which buttons it has and
//          taking the cell to the left of them, rather than by counting on
//          there being two - the wrapper decides that, and a hard-coded guess
//          would put this one through the middle of a third.
//          The window gained the expand button it never had. The standalone
//          wrapper asks for a minimise and a close and stops there, which left
//          a resizable window with no way to fill the screen from its own
//          corner.
//          It comes back to the header while full screen is on. That is not a
//          second home for it: full screen removes the title bar, and the way
//          out of full screen cannot be on the bar that full screen just took
//          away. Escape and F11 still work, and the readout still says so.
//          Placed from the tick rather than from resized(): DocumentWindow lays
//          its own buttons out AFTER it has resized the content component, so
//          anything measuring them from in there measures where they used to
//          be, and the button lags a frame behind every resize.
//          No frame round it at rest, because the buttons it now sits among are
//          bare glyphs on the bar and a boxed one would read as something that
//          had landed there rather than as one of them.
//   0.33.0 A pass over the front panel, and full screen.
//          The transport moved out of the keyboard row and up beside the
//          readout, at the readout's own height rather than half of it. Those
//          two are read together - you arm a take and then watch the screen say
//          what it recorded - and Play and REC are the controls reached for
//          most often, which is not what a pair of badges floating over the
//          keys was saying about them. They are drawn in the readout's bezel
//          now, so the pair reads as one panel with a screen on it.
//          The keyboard is five octaves. On a wide screen it had been spreading
//          twelve across the row, the top ones drawn past note 127 as blank
//          board, and the real ones had keys too narrow to hit singly. The keys
//          keep the whole row and take the width back instead: 34 pixels a key
//          on a 1920 screen rather than 16. Oct - and Oct + walk the five up and
//          down as before, and now sit on the keyboard's own line with Hold
//          note, where what they do can be seen happening.
//          Octave shifts now move by how far the keyboard ACTUALLY went rather
//          than by how far they were asked to. Each of the three things that
//          follow the keys - the view, the letter keys, the auditioned note -
//          used to clamp itself, so at either end they came apart and the
//          letter keys played an octave the keyboard was not showing.
//          The CLIP light lets go after twenty seconds. It was latched until
//          clicked, which is right for catching a clip you would otherwise miss
//          and wrong for everything after that: a light still on from a patch
//          you left twenty minutes ago is not a warning, it is furniture, and
//          the next real clip arrives at a light already lit and says nothing.
//          Every new clip restarts the clock, so a patch that keeps going over
//          stays lit the whole time it does. A click still clears it now.
//          Tone is called EQ, which is what everything else calls it.
//          Full screen is a button in the header, drawn as the corner brackets
//          everything uses for it, and it is real full screen now - kiosk mode,
//          the whole display, no title bar or taskbar over the synth - not the
//          window maximise that the title bar already offered. Escape and F11
//          both leave it, and the button stays where it is, because the way out
//          of full screen cannot be on the bar that full screen just took away.
//   0.32.1 A headroom pass over the whole bank. Every preset is levelled to
//          about -6 dBFS at the loudest of two test notes, measured rather than
//          guessed: BankAuditTest now prints the gain each patch would need to
//          arrive where the rest of the bank does, so this was a matter of
//          reading a column instead of listening to sixty-five patches.
//          The bank spanned 31 dB and now spans 6. Several presets had been
//          arriving within a decibel of full scale, which left nothing to play
//          a chord over, let alone a second instrument.
//          Two were quiet for a reason no gain could fix. Choir Pad was a
//          TRIANGLE through a bandpass at 1.5 kHz - a triangle's harmonics have
//          long died away by then, so almost nothing reached the output and it
//          sat twelve decibels under everything else, quiet enough to read as
//          broken. It has a richer source now, which is how a formant filter is
//          supposed to earn its keep, and it came up fifteen decibels.
//          Effects are exempt from the loudness floor, which is not a fudge: a
//          riser is laid UNDER something and holding it to an instrument's
//          level would make it the loudest thing in the bar. The ceiling still
//          applies to them, because nothing may clip.
//          Distorted 808 is a little cleaner than it was. Its gain sits before
//          the master clipper, so levelling it takes signal off the clipper -
//          and raising the distortion to compensate does not work, because past
//          about 0.7 the clipper holds its own output whatever arrives and the
//          gain stops being able to move it at all. The patch used to sit at
//          -1 dB because it was running into the ceiling, and that was part of
//          the sound. You cannot give something headroom and keep the character
//          it got from not having any.
//   0.32.0 Twenty-three more presets, and an audit that plays all of them.
//          The bank was written before the synth grew glide, mono and legato,
//          the chorus and phaser, the tone controls and the velocity routings -
//          and touched none of them. A bank that never reaches half the panel
//          makes that half look optional, so the new ones lean on it: a sliding
//          sub and a 303 line that are mono and legato because that is what
//          those sounds ARE, a Juno-style pad whose width is entirely its
//          chorus, an electric piano whose bark comes from velocity moving the
//          wavetable, a reed organ drifting through a phaser.
//          Keys went from four presets to eight, which was the thinnest
//          category by half.
//          BankAuditTest plays every preset at two notes and measures it. Not
//          whether it sounds good - nothing can judge that - but whether it
//          makes a sound at all, whether it makes one at both pitches, and
//          whether it leaves room to play anything over it.
//          It found seven presets arriving past full scale, four of which had
//          been in the bank all along: Screamer, Wide Stack, Dark Drone and
//          Glass Bell were clipping before anyone touched a dial, and nothing
//          had ever looked. Their output gain is trimmed; their character is
//          untouched. The bank now spans 20 dB rather than 31.
//          Thirteenth test suite.
//   0.31.0 A spectrum, with the EQ drawn on it. OUT now has two views sharing
//          the same space - they want the same width and are never read at
//          once - and the meter beside them serves either. SPECTRUM shows the
//          signal by frequency, log scaled, because an octave is an octave
//          wherever it sits and a linear axis spends four fifths of its width
//          on the top two octaves where almost nothing needs deciding.
//          Over it, in the warm colour, is what the EQ is actually doing:
//          built from the same coefficients the audio path builds, asked for
//          their own magnitude, at the rate the engine is really running. The
//          three band frequencies are named constants now rather than numbers
//          written into updateEqualiser, because a curve drawn from a second
//          copy of them is a curve that drifts the first time one changes.
//          On its own scale, centred, rather than sharing the spectrum's: one
//          is how loud the signal is and the other is what is being done to it,
//          and forcing both onto one axis leaves the EQ either invisible or
//          pinned to the ceiling. Dimmed when flat, so a flat EQ does not look
//          like a setting.
//          Each pixel asks for the loudest bin in the BAND it covers rather
//          than the one at its centre: at the top of the range dozens of bins
//          share a pixel, and sampling the middle one walks past the peak
//          beside it. Scaled so a full-scale sine reads 0 dBFS, which is what
//          lets these decibels be compared with the meter's.
//          Twelfth test suite: SpectrumTest, 22 checks - where a tone lands,
//          what it reads, that two tones stay apart, and that the drawn EQ is
//          the applied EQ.
//   0.30.0 An output tap, and a live scope and meter beside the waveform. The
//          two pictures at the top were both offline: a C3 note rendered through
//          the engine, telling you about the patch and never moving while you
//          played. Nothing anywhere looked at what actually left the plugin.
//          The tap is a lock-free ring written at the very end of the block -
//          after the effects and the output gain - so what it holds is what the
//          interface receives rather than an earlier and more flattering version
//          of it. One writer, one reader, a published write position, and a
//          third of a second of history so a reader at screen rate is never
//          racing the audio thread.
//          OUT shows the waveform's shape, triggered on a rising zero crossing
//          so a steady tone stands still instead of sliding, and drawn min-to-
//          max per column because at these rates one sample per column draws
//          whichever alias the spacing produces rather than the signal. The
//          trace turns orange at full scale.
//          The meter beside it answers the other half: clipping is two
//          questions, and the scope says what the waveform is DOING while the
//          bars say how far over it went. Peak-hold, because a peak that
//          vanished in a sixtieth of a second is a peak you never saw, and a
//          latched CLIP light cleared by clicking, because one that cleared
//          itself would let you miss the thing it exists to report. In a float
//          plugin nothing is clipped here - it is clipped later by the converter
//          - so CLIP means "over 0 dBFS", while there is still something to do
//          about it.
//          The strip keeps its height and gives up its width, which was the
//          actual complaint: one picture stretched across the panel. Offline
//          views left, live view right, and the live one says so.
//          Eleventh test suite: OutputTapTest, 31 checks.
//   0.29.0 A project remembers the keyboard channel too, and offers it with
//          the assignments as one thing. They are one thing: which MIDI reaches
//          this synth. Two dialogs in a row for two halves of the same answer
//          would be worse than either, so the question now names whichever of
//          them actually differs and changes only those.
//          Absent is not the same as different, and the format says which. A
//          project saved before v15 records -1 rather than 0, so opening an
//          older one cannot quietly widen a keyboard that was deliberately
//          narrowed - while someone who chose omni saved a real 0 and gets it
//          back. Same rule the map already followed: an empty map is left
//          alone, not applied.
//          Undo, redo and crash recovery restore both without asking, as
//          before. Preset format v15.
//   0.28.3 Recording latches instead of letting go. A control was released the
//          moment it stopped moving, which is right when auditioning a sound
//          and wrong when recording one: it handed the parameter straight back
//          to its lane between one adjustment and the next, so a pass made of
//          several played back as the new value, then the old value, then the
//          new value again. That is the jerkiness.
//          Moving something once with REC armed is now a statement about the
//          whole pass: it stays yours until the pass ends, and the steps the
//          playhead crosses afterwards are recorded at the value you left it
//          at. A pass ends when REC is switched off or the transport stops,
//          which is what keeps one pass from claiming half the panel for the
//          next one.
//          A third bit set rather than a longer hold, because the two are
//          different claims: a hold answers "is this moving right now", the
//          latch answers "did this get moved during the take". The 0.9-second
//          controller hold stays exactly as it was, and still does its own job
//          outside recording.
//   0.28.2 Opening a project loads its MIDI assignments, and asks first. They
//          were saved correctly all along and never read back: the only code
//          that applied them was the host's session-restore path, so in the
//          standalone - where projects are opened from the File menu - the map
//          in the file was written, stored and ignored.
//          Asked rather than applied, unlike the pattern. The pattern belongs
//          to the project; the assignments belong to the desk the project is
//          being opened ON, which may not be the desk it was saved at, and
//          someone else's controller layout is not an improvement on your own.
//          Silent when the project carries none, and silent when it carries
//          exactly what is already there - a question whose two answers do the
//          same thing is a click for nothing.
//          Undo, redo and crash recovery still restore it without asking:
//          those put back a state that was yours a moment ago rather than
//          adopting one from elsewhere, and a question there would be asking
//          whether you meant your own last move.
//   0.28.1 REC records a MIDI controller, not only the mouse. A learned
//          control writes its parameter on the audio thread, which the held-a-
//          dial set knew nothing about - so REC sat there recording nothing
//          while the sound audibly changed.
//          A control sends no "let go" message: a knob you stop turning simply
//          stops sending. So being driven is a countdown refilled by every
//          controller message and run down by each block, set to the same 0.9
//          seconds a dial stays lit after the mouse releases it - a sweep from
//          hardware and a sweep from the mouse are now held for the same
//          moment.
//          Kept in a SECOND bit set rather than the existing one, because the
//          two are written by different threads: the editor reports a hand on
//          the dial every frame from the message thread, the audio thread
//          reports the controller. Sharing one set would have had each of them
//          switching the other off, and a sweep would have been recorded only
//          on the frames they happened to agree. Everything downstream reads
//          them together, which is what it always wanted anyway - the question
//          was never "was it the mouse", it was "is something other than the
//          lane in charge of this right now".
//   0.28.0 The keyboard can be narrowed to one MIDI channel. Options >
//          Keyboard Channel: Omni, or 1 to 16. A controller whose knobs you
//          want and whose keys you do not is now a setting rather than a
//          reason to unplug it.
//          Notes and only notes are filtered. Controllers pass on every
//          channel whatever this says, so narrowing the keyboard to silence a
//          device's keys does not also disconnect its knobs - which is the
//          entire reason for narrowing it.
//          Note-offs are dropped alongside note-ons rather than let through,
//          because a release whose note-on was filtered tells the voices to let
//          go of something they were never holding. Changing the setting lets
//          go of everything first: what is held arrived on the old channel, and
//          its release would arrive there too and be dropped, leaving the note
//          sounding for good.
//          Filtered before anything reads the buffer - keyboard state, latch,
//          harmony, arpeggiator - so a dropped note is dropped everywhere
//          rather than leaving one of them holding a key.
//          Saved with the application rather than in a preset, like the MIDI
//          map: it describes what is plugged in, not what the patch sounds
//          like. 12 more checks in MidiMapTest.
//   0.27.1 A way out of learn mode, and dials that move when the controller
//          does. The layer covers the whole panel - which is the point of it -
//          and that included the Options button that switched learn on, so once
//          in there was no way back out. Escape leaves it now, and there is a
//          Done button on the layer's own footer, because a mode you can only
//          leave by knowing a keystroke is still a trap.
//          A learned controller wrote its parameter on the audio thread while
//          the dial on screen stayed where it was, so nothing appeared to
//          happen. Worse than cosmetic: the next touch of that dial would snap
//          the sound back to the stale position and undo everything the
//          hardware had done. A mapped dial now follows its parameter the same
//          way an automated one follows its lane.
//   0.27.0 MIDI learn, end to end. Options > MIDI Learn puts a layer over the
//          panel: every dial wears a badge saying what drives it - a controller
//          number, a dash for nothing, or MOVE IT for the one waiting. Click a
//          dial, move the control you want, done. Right-click a dial clears it,
//          clicking off a dial cancels, and the strip along the bottom says
//          what the pointer is over and what will happen if you click it.
//          A layer rather than a badge component per dial, because it also has
//          to swallow the click - learning a dial must not also turn it - and
//          because sixty-three overlays would be sixty-three things to keep in
//          step with a layout that already moves them.
//          Options > MIDI Assignments lists everything in one place, by
//          controller number. That is the question that comes up after ten
//          minutes of assigning - "what did I put on CC 21" - and it is the one
//          hovering dials cannot answer, because the dial you want is the one
//          you have forgotten.
//          Assignments persist. They live with the application rather than in a
//          patch, because they describe the hardware on the desk and not the
//          sound - a preset that rearranged your controller would be a preset
//          you could not trust to load. A saved project carries its own copy
//          and applies it, since a project IS the session; an empty one is left
//          alone so an older project cannot wipe the desk in front of you.
//          Preset format v14.
//   0.26.0 The MIDI map, and controllers that actually reach the synth. A
//          learned controller now drives its parameter from the audio thread,
//          so it keeps working with the plugin window shut - which is how a
//          plugin is normally used, and the reason the map lives in the
//          processor rather than the editor.
//          Keyed by controller number, which is what makes reassignment free:
//          pointing CC 74 at a second dial is one store into slot 74, and the
//          dial it used to move is simply no longer pointed at. The reverse is
//          allowed - two controllers may drive one dial - because that is a
//          fader and a knob reaching the same thing, not a conflict.
//          Learning applies the incoming value as it binds, so the dial jumps
//          to where the hardware already is: seeing it move is how you know it
//          took. Channel is ignored, and a wheel learned to a dial keeps its
//          modulation duty as well, since setting a parameter and being a
//          modulation source are different jobs.
//          Saved as names rather than numbers, so a map cannot be repointed at
//          the wrong dial by a renumbered registry. Nothing yet reads or writes
//          those saved maps; that is next, along with the interface.
//          Tenth test suite: MidiMapTest, 30 checks.
//   0.25.0 The parameter registry holds sixty-four instead of thirty-two, so
//          every dial on the panel has an id. Thirty-one that were reachable
//          by hand but not by name - glide, the mod envelope, delay time,
//          reverb size, chorus, phaser, the EQ, the velocity and wheel
//          amounts, accent, and three arpeggiator dials - can now be automated
//          and, next, learned to a MIDI controller. Groundwork for MIDI learn,
//          which has to address a parameter from the audio thread because the
//          plugin window is usually shut.
//          Three dials stay out on purpose rather than for want of room: BPM,
//          because a lane whose own playback rate comes from the tempo should
//          not be driving the tempo; Arp Link, because it rescales the arp rate
//          dial beside it; and Arp Rate, because it means two different
//          parameters depending on whether the arpeggiator is synced.
//          Ids are append-only and now have a test that says so - the first
//          thirty-two are pinned by name, because they are what every saved
//          automation lane means by 0 to 31.
//          The lane chooser is grouped by section, since sixty-three entries in
//          one column is a list you read rather than a menu you pick from.
//   0.24.1 The pattern question did the opposite of what its buttons said.
//          With two buttons JUCE numbers the FIRST one 1 and the second 0 -
//          the Return/Escape pairing, not the reading order - so listing Keep
//          first made Keep the load and Load the keep, and left escape doing
//          the destructive one. The order is swapped: dismissing the box keeps
//          your pattern, which is what it always claimed to do.
//   0.24.0 A patch and a pattern are two different things to load. WaveForge's
//          own presets are synth patches now and never touch the sequencer -
//          choosing a sound is not an act that should resize your bar. A preset
//          file you saved still carries its pattern, and loading one loads the
//          sound first and then asks whether to take the pattern as well, with
//          Keep as the answer you get by pressing escape.
//          The question waits for Load rather than firing while you browse:
//          moving through the list only ever auditions the sound, so arrowing
//          down a folder of your own presets does not put a dialog between you
//          and the next one. Projects and undo are unchanged - those restore
//          everything, which is what they are for.
//   0.23.13 Browsing the factory bank no longer resizes your pattern. Loading a
//          patch applied five sequencer settings - length, rate, root note,
//          record and quantise - from the preset even when the preset carried
//          no pattern to go with them, so a 32-step bar came back 64 steps long
//          at a rate and root note that were never yours. The pattern itself
//          was already protected; the settings that govern it were not, which
//          is the worse half to restore. They now travel with the pattern or
//          not at all, so a saved project still comes back whole.
//   0.23.12 The slide tooltip says where Mono and Legato are - Oscillator 1,
//          on the Synth page, beside the Glide dial - rather than only naming
//          them. It also says it in both states: the version that only warned
//          you when the switches were off went quiet the moment they were on,
//          which is exactly when you stop being told what the row depends on.
//   0.23.11 The tooltip actually uses the synth's look and feel. It was drawn
//          bold and centred whatever the look and feel said, because the
//          tooltip window is built with no parent - which puts it on the
//          desktop, not among this editor's children, so setLookAndFeel on the
//          editor never reached it and JUCE's default kept drawing it. Told
//          directly, the way the popup menus here already are. Plain weight,
//          ranged left, and wrapped only where the line runs out.
//   0.23.10 Footer tooltips reach the labels. Every row rect is trimmed of the
//          gutter, because that is where the cells are drawn and where clicks
//          land - so the hover test said "not this row" for the one part of a
//          row anyone points at to ask what it is. The [i] beside SLIDE was
//          itself the least hoverable thing on the panel.
//   0.23.9 Accent, and the footer explains itself. A step can be marked ACCENT
//          and plays at the pattern's accent level instead of its own velocity,
//          so one dial retunes every accent at once rather than each being a
//          velocity bar dragged to the right height.
//          Accent is expressed as velocity and nothing else, because velocity
//          is the only per-note channel between the sequencer and the synth -
//          anything else would be a setting every voice reads at once, which is
//          the shape of every voice bug this synth has had. Everything an
//          accent does therefore comes from the existing velocity routing:
//          louder, brighter, further through the wavetable, and now more
//          resonant, which is the new Vel>Res dial. Turn Vel>Cut and Vel>Res up
//          or an accent is only a loud note.
//          Rolling over any row in the sequencer footer now says what it does.
//          Slide carries an [i] beside its label because it is the one row that
//          depends on settings kept elsewhere - it needs mono and legato - and
//          the marker brightens when they are off. Lock says what it protects
//          and when to reach for it, which nothing had ever explained.
//          Preset format v13. It also restores the size check for v12 files,
//          which was missed when slide went in.
//   0.23.8 The slide row, so ties can actually be set. Steps gained the ability
//          to tie into the next one in 0.23.7 with no way to switch it on; the
//          sequencer footer now has a SLIDE row between OCTAVE and LOCK, and a
//          click on a step toggles its tie.
//          Drawn as a mark running from the step across to the next one rather
//          than as a filled cell, because that is what a tie is - a property of
//          the join between two steps, not of either of them. A filled cell
//          would read as belonging to the step you clicked.
//          A tie needs mono and legato to do anything, and rather than hide
//          that or switch them on behind your back, a tie that cannot sound
//          draws hollow. Nothing is flagged before a tie exists, because until
//          then there is nothing to say.
//          Generate still clears unlocked steps outright, so a regenerated
//          pattern comes back without ties and locked steps keep theirs.
//   0.23.7 Steps can be tied to the one after them, so a sequenced line slides
//          from note to note instead of re-articulating each one. The engine
//          half only; there is no way to switch it on from the UI yet, and
//          patterns saved now carry the flag (preset format v12 - older files
//          load with no ties, as they had none).
//          A tie is not a glide setting, it is an ORDER: the next step's
//          note-on has to reach the voice while the previous note is still
//          held, so they go out on-then-off in the same instant rather than
//          off-then-on. Release first and the line ends and a new one begins.
//          Worth being precise about what that buys, because the first version
//          of the test claimed the wrong thing and passed: releasing first
//          still slides, since 0.23.3 has a fresh voice pick up from whatever
//          most recently stopped. What it cannot avoid is being a fresh voice -
//          a new attack, the old note decaying beside it. Measured through a
//          real Synthesiser, a tie holds ONE voice where releasing first starts
//          two, and one voice with no retrigger is what a slide means.
//          A tie that reaches a step a chance roll skipped still lets its note
//          go, and stopping mid-tie releases it - both measured, because a
//          sequencer hanging on a note is the worst way it can fail. With mono
//          or legato off there is nothing to bend, so the step plays straight
//          rather than stacking two notes into a chord.
//   0.23.6 A match no longer comes back wider than the sample. Fitted to a
//          plucked note with no unison at all, the search was returning 9.5
//          cents of detune - audible, and nothing to do with the sound it was
//          given. There was already a nudge toward zero and it was not enough,
//          because a prior competes with every other pull on the same vector.
//          Each unison setting is now offered at zero once, after the search,
//          and left there unless putting it back actually improves the fit.
//          The threshold comes from measurement rather than taste: taking the
//          detune away costs 35% to 129% on a sample that really is detuned and
//          6% on one that is not, so 15% sits in the middle of a five-fold gap.
//          Measured at 0.0 cents invented afterwards, with a target at 20 cents
//          still recovering 10.2 - and the probe now checks both of those,
//          because a pass that stripped the detune always would have scored
//          perfectly on the first test and been plainly wrong.
//   0.23.5 A note you let go no longer chases the next one. In mono with
//          legato, the sounding voice follows whichever key is held newest -
//          but that rule was applied to every voice still making sound,
//          including ones whose key was already up and which were only
//          finishing their release. Play a note, let it go, play another
//          before the first has faded, and the first note's tail slid up after
//          the second: leaving C3 at 130.8 Hz and reading 253.8 Hz six blocks
//          later, an octave of bend on a note nobody was holding. Not an edge
//          case either - on any patch with a tail it happened on every note
//          played faster than its own release. Holding a key and pressing
//          another never showed it, because then only one voice is ever
//          sounding; it takes the gap. A released note now keeps its own pitch
//          while it fades, and the line itself slides exactly as before.
//   0.23.4 Mono glide, and a correction. The 0.23.2 note said mono portamento
//          was untouched "there being only one voice" - that was wrong. Mono
//          rewrites the MIDI so one note SOUNDS at a time; it does not reduce
//          the voice count, and with legato off the note-off and note-on land
//          on the same sample, so the new note always finds a voice with no
//          history. Mono was the worst case, not the exempt one.
//          Fixed here as well: letting go of the newer key to fall back to one
//          still held did not slide. Before starting a note the Synthesiser
//          stops any voice already playing it, and that older note is often
//          still finishing its release - so a stale voice was republishing its
//          own pitch over the one the line had just left, and the fall-back
//          arrived with nothing to slide from. Only the first stop counts now.
//          Mono lines, mono fall-backs and legato pitch changes are all
//          measured through MonoVoice and a real Synthesiser rather than
//          assumed - which is how both of the above were found.
//   0.23.3 Glide in poly, finished properly. 0.23.2 gave each voice its own
//          glide origin, which fixed chords but quietly broke lines: the
//          allocator hands each note to the first FREE voice and a voice
//          stays busy through its release tail, so a line played faster than
//          its own release walks along the voice list onto voices with no
//          history. Measured, such a line glided on none of its notes -
//          alternating two pitches parked one on each of two voices, neither
//          ever changing note. A voice with no history of its own now picks
//          up from the note that most recently STOPPED, which a line always
//          has and a chord never does, so lines glide again and chords still
//          do not smear. Both cases are measured through a real Synthesiser
//          rather than assumed.
//   0.23.2 Glide no longer smears a chord. The pitch a slide starts from was
//          kept in one place shared by every voice: each voice read it as its
//          note began and then overwrote it, so playing C-E-G, the E slid up
//          from the C and the G slid up from the E. A chord arrived from the
//          bottom up instead of each note sliding from its own previous pitch.
//          Each voice now remembers where it was itself, which leaves mono
//          portamento - what glide is mostly used for - exactly as it was, and
//          means a voice that has not sounded yet starts on pitch rather than
//          inheriting a stranger. GlideTest is new and fails against the old
//          behaviour on precisely the two cases it should.
//   0.23.1 A match no longer hands back settings nobody chose. Around twenty of
//          the forty-nine searched settings do nothing for any given sample,
//          because whatever they belong to is switched off - a second
//          oscillator's tuning at zero level, a reverb's size at zero mix - so
//          the search left them wherever they landed. Silent while matching,
//          and audible the moment the parent was turned up: raising the second
//          oscillator after a Deep Match found it seventeen semitones sharp.
//          Those are now put back to sane values once the search is done, which
//          cannot change the sound, and the test proves it by checking the
//          distance is identical before and after.
//   0.23.0 Deep Match fits several notes of the sample at once instead of one.
//          Fitted to a single note the search could put the filter in the wrong
//          place and bend the tone controls to cancel it - right on that note,
//          wrong across the keyboard - and no penalty on the settings reliably
//          stopped it. Shown three pitches it cannot, because a filter cuts the
//          harmonic series somewhere different at every fundamental. Measured
//          at a pitch the search had never seen, three pitches beat one on all
//          four seeds tried, and beat one pitch given three times the budget,
//          which recovered the filter WORSE than the short run because it only
//          overfitted harder. The budget is shared between the notes rather
//          than multiplied by them, so a Deep Match takes about as long as it
//          did before.
//   0.22.0 Quick Match now plays the whole factory bank against the sample and
//          keeps whichever comes closest, the estimate included. The estimate
//          alone was being beaten by nearly every shipped preset - on a test
//          phrase it read the filter at 398 Hz against a real 3000 Hz and
//          ranked 39th of 43. MatchTest gained two probes that sweep one
//          setting at a time across its range, which established that the
//          objective can see the chorus perfectly well; what loses it is the
//          voice being fitted dry first, after which the real chorus no longer
//          fits the voice that stood in for it.
//   0.21.0 Sound matching made repeatable, and the factory bank put to work.
//          The offline renderer now seeds its noise and snaps the master
//          chain's smoothers, so the same patch measures the same every time -
//          without that, a candidate's score depended on which worker thread
//          rendered it and what it had rendered before. The shipped presets are
//          auditioned against the reference before the search starts: the
//          nearest one is reported, and the best of each category joins the
//          starting population, where musically coherent patches give the
//          search somewhere real to step from.
//   0.20.0 Sound matching is fitted in stages - the voice, then what moves
//          it, then the master chain - so an equaliser can no longer be used
//          to cancel a wrong filter. Settings jammed against their limits are
//          discouraged.
//   0.19.1 Pitch detection stopped at 500 Hz, so any reference note above B4
//          came back an octave or more low and matching rendered against the
//          wrong pitch. It now covers the range of a piano.
//   0.19.0 Sound matching can now reach the chorus, phaser and tone controls,
//          and its parameter table drives the search directly instead of
//          three hand-kept lists. Master effects split long buffers, which
//          is what stopped the chorus working outside live playback.
//   0.18.3 A slow dial adjustment was being recorded as several undo steps,
//          so undoing it went back only part of the way. A gesture is now
//          the mouse going down and up, however long it lingers between.
//   0.18.2 One Ctrl+Z was stepping back two entries: undo was handled both
//          by the key handler and by the timer that polls for it.
//   0.18.1 Options menu entries ran each other's actions - Audio settings
//          triggered the recovery, Undo opened a save dialog - because three
//          new ids renumbered six existing ones. The menu is now dispatched
//          by name. Turning a dial is undoable too.
//   0.18.0 Undo and redo over the whole project, kept as snapshots rather
//          than reversible commands, with Ctrl+Z and Ctrl+Shift+Z. The
//          project is written to a temporary file every five minutes and
//          offered back if the last run ended without tidying up.
//   0.17.1 A note drawn with the mouse is a whole step again - the change to
//          tick lengths had made it a sixteenth of one. Pattern lengths over
//          sixteen steps snap to whole bars.
//   0.17.0 Notes can be as short as a sixteenth of a step - lengths are kept
//          in ticks now, not whole steps - and Generate gives them lengths
//          from the gaps between them. The sequencer opens at 64 steps.
//          Wide dials put the name and value beside the dial so it can grow.
//   0.16.0 Velocity, mod wheel and aftertouch are modulation sources, not
//          just loudness: they open the filter and move the wavetable, so an
//          accented step is brighter rather than only louder. Transport
//          centred over the keyboard, dial readouts trimmed short.
//   0.15.0 Every dial reads its value in the gap its arc leaves at the
//          bottom, always visible rather than only while turning. The
//          transport is bigger and boxed off as a control of its own.
//   0.14.0 Play/Stop and REC as icon buttons by the keyboard, reachable from
//          either page. The note grid scrolls up and down as well as across,
//          so a note written outside the window can be found.
//   0.13.0 Chance and octave rows on the piano roll, so a pattern can vary
//          instead of repeating. Glide, mono and legato for lines that join
//          up. Chorus, phaser and a three-band tone control on the master
//          chain. Preset format v9 carries all of it.
//   0.12.1 Header rearranged: Options and the page tabs on the left, wordmark
//          on the right, and the page switch is a proper pair of tabs rather
//          than one button that renamed itself.
//   0.12.0 Projects are files of their own: Save Project / Load Project write
//          .wfo, carrying the synth, the sequencer pattern and the automation.
//          Audio / MIDI Settings moved into the one Options menu and the
//          wrapper's second Options button retired.
//   0.11.0 One musical clock behind the arpeggiator and the sequencer, so
//          their rates link rather than merely agree. Arpeggiator rate is now
//          a note division and a Link dial ties it to the sequencer's, and the
//          whole project - synth, pattern and automation - is saved, to a file
//          and between sessions (preset format v8).
//   0.10.0 Notes of any length up to 64 steps, zoom and scroll on the
//          timeline, played notes captured onto the grid with optional
//          quantise, and preset browsing that only previews until you Load.
//   0.9.0  Sequencer rebuilt as a piano roll - pitch rows, a key strip, a
//          velocity footer, and collapsible automation lanes sharing the
//          same time axis that can be drawn as well as recorded.
//   0.8.0  Step sequencer with its own page, scale-degree steps, Euclidean
//          generate, and dial automation recorded onto the same timeline.
//          Hold now latches every note played, not just the last.
//   0.7.1  Computer keyboard plays notes wherever focus is and follows the
//          octave buttons; arrow keys audition presets in the browser.
//   0.7.0  Scale and key lock, chord helper, factory preset bank with a
//          browser, and preset format v7 - arpeggiator, tempo and key
//          settings now travel with the patch.
//   0.6.0  All actions gathered under one Options menu; folder settings moved
//          into it; version shown in the header.
//   0.5.0  Arpeggiator rhythm patterns (25) on their own axis, swing, and
//          notches on dials with only a few settings.
//   0.4.0  Arpeggiator with 24 note-order modes and a grouped picker.
//   0.3.0  Tempo clock with MIDI-clock slaving, per-feature sync switches,
//          remembered folders, full-screen view.
//   0.2.0  Second oscillator, sub and noise, multi-mode filter, mod envelope
//          and second LFO, master effects.
//   0.1.0  Wavetable engine, preset format, sample matching.
namespace version
{
constexpr int major = 0;
constexpr int minor = 49;
constexpr int patch = 0;

inline juce::String string()
{
    return "v" + juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
}
} // namespace version
} // namespace wavelathe
