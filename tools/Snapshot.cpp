// Renders the plugin's panel to PNG files without a host. Development only.
//
//   ContrailSnapshot <out-dir> [preset-number] [seconds] [fps] [scale] [param=value ...]
//
// Any param=value arguments set parameters after the preset loads, in their own
// units (for example format=4 bypass=1 wow=1.5). With BENCH=1 in the
// environment it skips writing files and prints the average time per frame.
//
// With seconds = 0 it writes one still (panel.png). Otherwise it writes numbered
// frames (frame-0000.png, ...) covering that many seconds of animation. With
// CHANGE="1.0 format=4" in the environment it sets that parameter 1 s in, to film a
// crossfade.

#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <cstdlib>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 1;
    const double seconds = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 0.0;
    const double fps = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 20.0;
    const float scale = argc > 5 ? juce::String (argv[5]).getFloatValue() : 1.0f;
    outDir.createDirectory();

    contrail::ContrailProcessor processor;
    if (preset > 0)
        processor.getPresets().loadPreset (preset - 1);
    for (int i = 6; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (auto* p = processor.getState().getParameter (arg.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* contrailEditor = dynamic_cast<contrail::ContrailEditor*> (editor.get());

    auto write = [&] (const juce::String& name)
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        if (std::getenv ("BENCH")) return;
        juce::FileOutputStream out (outDir.getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };

    if (seconds <= 0.0)
    {
        contrailEditor->renderFrame (2.4, true);
        write ("panel.png");
        return 0;
    }

    // An optional parameter change part way through.
    const juce::String change (std::getenv ("CHANGE") != nullptr ? std::getenv ("CHANGE") : "");
    const double changeAt = change.isNotEmpty() ? change.upToFirstOccurrenceOf (" ", false, false).getDoubleValue() : -1.0;
    const auto changeArg = change.fromFirstOccurrenceOf (" ", false, false);
    bool changed = false;

    const int frames = juce::roundToInt (seconds * fps);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < frames; ++i)
    {
        if (! changed && changeAt >= 0.0 && static_cast<double> (i) / fps >= changeAt)
        {
            changed = true;
            if (auto* p = processor.getState().getParameter (changeArg.upToFirstOccurrenceOf ("=", false, false)))
                p->setValueNotifyingHost (p->convertTo0to1 (changeArg.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20); // let the attachments hear it
        }
        contrailEditor->renderFrame (static_cast<double> (i) / fps, true);
        write ("frame-" + juce::String (i).paddedLeft ('0', 4) + ".png");
    }
    std::printf ("%.2f ms/frame\n", (juce::Time::getMillisecondCounterHiRes() - t0) / frames);
    return 0;
}
