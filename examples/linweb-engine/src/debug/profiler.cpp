#include "debug/profiler.h"
#include <algorithm>
#include <cstdio>
#include "app/app_state.h"
#include "platform/platform_config.h"
#include "renderer/renderer.h"

namespace linweb {

namespace {

constexpr int SECTIONS = static_cast<int>(ProfSection::Count);
constexpr int HISTORY = 90;               // frames kept, one bar each
constexpr int AVERAGE_FRAMES = 30;        // frames the figures of the list average
constexpr float BAR_W = 4.0f;
constexpr float GRAPH_H = 64.0f;
constexpr float PANEL_W = HISTORY * BAR_W + 20.0f;
constexpr float MIN_SCALE_MS = 33.4f;     // the graph never zooms in past two frames at 60 Hz

struct Frame {
    double stage[SECTIONS] = {};
    double total = 0.0;
    int layout_calls = 0;
    int layout_hits = 0;
    bool full = false;
};

struct StageStyle { const char* name; float r, g, b; };
const StageStyle STAGES[SECTIONS] = {
    {"Script + input", 0.65f, 0.55f, 1.00f},
    {"Style",          0.30f, 0.80f, 1.00f},
    {"Layout tree",    0.30f, 0.90f, 0.60f},
    {"Layout",         1.00f, 0.85f, 0.30f},
    {"Animations",     1.00f, 0.60f, 0.25f},
    {"Render",   1.00f, 0.40f, 0.45f},
    {"Swap / GPU wait",0.90f, 0.45f, 0.90f},
};
const StageStyle OTHER = {"Other", 0.55f, 0.58f, 0.62f};

Frame g_history[HISTORY];
int g_head = 0;                           // next slot to write
int g_count = 0;
Frame g_current;
double g_last_end = 0.0;
double g_fps_window_start = 0.0;
int g_fps_frames = 0;
double g_fps = 0.0;

double other_ms(const Frame& f)
{
    double sum = 0.0;
    for (double s : f.stage) sum += s;
    return std::max(0.0, f.total - sum);
}

const Frame& recent(int back) { return g_history[(g_head - 1 - back + HISTORY * 2) % HISTORY]; }

} // namespace

ProfScope::ProfScope(ProfSection section) : section_(section), start_(get_platform_time()) {}
ProfScope::~ProfScope() { Profiler::add(section_, (get_platform_time() - start_) * 1000.0); }

void Profiler::add(ProfSection section, double ms)
{
    g_current.stage[static_cast<int>(section)] += ms;
}

void Profiler::note_rebuild(int layout_calls, int layout_hits, bool full)
{
    g_current.layout_calls += layout_calls;
    g_current.layout_hits += layout_hits;
    g_current.full = g_current.full || full;
}

void Profiler::end_frame()
{
    const double now = get_platform_time();
    if (g_last_end > 0.0) {
        g_current.total = (now - g_last_end) * 1000.0;
        g_history[g_head] = g_current;
        g_head = (g_head + 1) % HISTORY;
        g_count = std::min(g_count + 1, HISTORY);
    } else {
        g_fps_window_start = now;
    }
    g_last_end = now;
    g_current = Frame();

    ++g_fps_frames;
    if (now - g_fps_window_start >= 0.5) {
        g_fps = g_fps_frames / (now - g_fps_window_start);
        g_fps_window_start = now;
        g_fps_frames = 0;
    }
}

void Profiler::draw()
{
    if (!g_show_profiler || g_count == 0) return;

    const int avg_n = std::min(g_count, AVERAGE_FRAMES);
    double avg[SECTIONS] = {};
    double avg_other = 0.0, avg_total = 0.0, worst = 0.0;
    int calls = 0, hits = 0;
    for (int i = 0; i < avg_n; ++i) {
        const Frame& f = recent(i);
        for (int s = 0; s < SECTIONS; ++s) avg[s] += f.stage[s] / avg_n;
        avg_other += other_ms(f) / avg_n;
        avg_total += f.total / avg_n;
        calls += f.layout_calls;
        hits += f.layout_hits;
    }
    double scale = MIN_SCALE_MS;
    for (int i = 0; i < g_count; ++i) {
        worst = std::max(worst, recent(i).total);
    }
    scale = std::max(scale, std::min(worst, 200.0));

    const float vw = static_cast<float>(Renderer::get_viewport_width());
    const float x = std::max(6.0f, vw - PANEL_W - 6.0f);
    float y = 6.0f;
    const float rows_h = (SECTIONS + 1) * 16.0f;
    const float panel_h = 26.0f + GRAPH_H + 10.0f + rows_h + 30.0f;

    Renderer::draw_rect(x, y, PANEL_W, panel_h, 0.04f, 0.05f, 0.07f, 0.86f, 6.0f);

    char text[96];
    snprintf(text, sizeof(text), "%.0f FPS   %.1f ms avg   %.1f ms worst", g_fps, avg_total, worst);
    Renderer::draw_text(text, x + 10.0f, y + 18.0f, 13.0f, 0.55f, 1.0f, 0.55f, 1.0f, true, 0.0f, 0.0f, false);

    // The graph: the oldest frame on the left; one bar per frame, stacked by stage
    const float gx = x + 10.0f;
    const float gy = y + 26.0f;
    Renderer::draw_rect(gx, gy, HISTORY * BAR_W, GRAPH_H, 1.0f, 1.0f, 1.0f, 0.06f);
    const float base = gy + GRAPH_H;
    for (int i = 0; i < g_count; ++i) {
        const Frame& f = recent(g_count - 1 - i);
        const float bx = gx + (HISTORY - g_count + i) * BAR_W;
        float top = base;
        auto segment = [&](double ms, const StageStyle& st) {
            const float h = static_cast<float>(ms / scale) * GRAPH_H;
            if (h < 0.4f) return;
            const float clipped = std::min(h, top - gy);
            if (clipped <= 0.0f) return;
            top -= clipped;
            Renderer::draw_rect(bx, top, BAR_W - 1.0f, clipped, st.r, st.g, st.b, 1.0f);
        };
        for (int s = 0; s < SECTIONS; ++s) segment(f.stage[s], STAGES[s]);
        segment(other_ms(f), OTHER);
    }
    // 60 Hz and 30 Hz budget lines
    for (double budget : {1000.0 / 60.0, 1000.0 / 30.0}) {
        if (budget >= scale) continue;
        const float ly = base - static_cast<float>(budget / scale) * GRAPH_H;
        Renderer::draw_rect(gx, ly, HISTORY * BAR_W, 1.0f, 1.0f, 1.0f, 1.0f, 0.35f);
    }
    snprintf(text, sizeof(text), "%.0f ms", scale);
    Renderer::draw_text(text, gx + 2.0f, gy + 10.0f, 10.0f, 1.0f, 1.0f, 1.0f, 0.6f, false, 0.0f, 0.0f, false);

    // The list: each stage with its average and a bar relative to the frame
    y = gy + GRAPH_H + 10.0f;
    const float bar_x = x + 130.0f;
    const float bar_w = PANEL_W - 130.0f - 76.0f;
    auto row = [&](const StageStyle& st, double ms) {
        Renderer::draw_rect(x + 10.0f, y + 3.0f, 8.0f, 8.0f, st.r, st.g, st.b, 1.0f, 2.0f);
        Renderer::draw_text(st.name, x + 24.0f, y + 12.0f, 11.0f, 0.85f, 0.88f, 0.92f, 1.0f, false, 0.0f, 0.0f, false);
        const float w = avg_total > 0.0 ? static_cast<float>(std::min(1.0, ms / avg_total)) * bar_w : 0.0f;
        Renderer::draw_rect(bar_x, y + 4.0f, bar_w, 6.0f, 1.0f, 1.0f, 1.0f, 0.08f);
        if (w > 0.5f) Renderer::draw_rect(bar_x, y + 4.0f, w, 6.0f, st.r, st.g, st.b, 1.0f);
        snprintf(text, sizeof(text), "%6.2f ms", ms);
        Renderer::draw_text(text, bar_x + bar_w + 6.0f, y + 12.0f, 11.0f, 0.85f, 0.88f, 0.92f, 1.0f, false, 0.0f, 0.0f, false);
        y += 16.0f;
    };
    for (int s = 0; s < SECTIONS; ++s) row(STAGES[s], avg[s]);
    row(OTHER, avg_other);

    const int rebuilds = [&] { int n = 0; for (int i = 0; i < avg_n; ++i) n += recent(i).full ? 1 : 0; return n; }();
    snprintf(text, sizeof(text), "rebuilds %d/%d   layout calls %d, cached %d", rebuilds, avg_n, calls / avg_n, hits / avg_n);
    Renderer::draw_text(text, x + 10.0f, y + 16.0f, 10.0f, 0.65f, 0.7f, 0.75f, 1.0f, false, 0.0f, 0.0f, false);
}

} // namespace linweb
