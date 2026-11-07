#pragma once

namespace linweb {

// Where a frame spends its time. The rest of the frame (the overlay itself, the pacing) is shown as "Other".
enum class ProfSection { Script, Style, Tree, Layout, Anim, Render, Swap, Count };

// The visual profiler (F3 or --profiler): a panel drawn over the page with the last frames as stacked bars, one colour
// per stage, and the average of each stage. Everything is measured on the CPU side; "Swap" is the buffer swap plus the
// event poll, which is where the wait for the GPU (or the display) shows up.
class Profiler {
public:
    static void add(ProfSection section, double ms);                  // time spent in a stage of the current frame
    static void note_rebuild(int layout_calls, int layout_hits, bool full);
    static void end_frame();                                          // once per loop turn, after swapping buffers
    static void draw();                                               // the panel, when it is turned on
};

// Adds the time between its construction and destruction to a stage.
class ProfScope {
public:
    explicit ProfScope(ProfSection section);
    ~ProfScope();
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;

private:
    ProfSection section_;
    double start_;
};

} // namespace linweb
