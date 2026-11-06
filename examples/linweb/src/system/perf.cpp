#include "system/perf.h"
#include "renderer/renderer.h"
#include "layout/layout_engine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace linweb {
namespace perf {

namespace {

const double kSmoothing = 0.2;    // weight of the newest frame in the moving averages
const int kPhases = 5;            // script, style, layout, paint, swap
const int kHistory = 90;          // frames kept, one bar each
const int kAverageFrames = 30;    // frames the list averages
const float kBarW = 4.0f;
const float kGraphH = 64.0f;
const float kPanelW = kHistory * kBarW + 20.0f;
const double kMinScaleMs = 33.4;  // the graph does not zoom in past two frames at 60 Hz

struct Frame {
    double phase[kPhases] = {};
    double total = 0.0;
    int layout_calls = 0;
    int layout_hits = 0;
    bool full = false;
};

struct Phase { const char* name; float r, g, b; };
const Phase kPhase[kPhases] = {
    {"Script + input",  0.65f, 0.55f, 1.00f},
    {"Style",           0.30f, 0.80f, 1.00f},
    {"Layout",          1.00f, 0.85f, 0.30f},
    {"Paint",           1.00f, 0.40f, 0.45f},
    {"Swap / GPU wait", 0.90f, 0.45f, 0.90f},
};
const Phase kOther = {"Other", 0.55f, 0.58f, 0.62f};

Stats g_stats;
double g_window_start = -1.0;     // start of the current one-second window
int g_window_frames = 0;
bool g_visible = false;
Frame g_history[kHistory];
int g_head = 0;                   // next slot to write
int g_count = 0;

// The profile log, for reading the numbers of a machine without a screen to look at (steward-httpd serves it at
// /profile.log): a line for each slow frame and a summary every kLogSummaryS seconds. It is cut when it passes kLogMax.
const char* kLogPath = "/tmp/linweb-profile.log";   // LINWEB_PROFILE_LOG replaces it
const long kLogMax = 512 * 1024;
const double kSlowFrameMs = 50.0;
const int kManyLayoutCalls = 2000;
const double kLogSummaryS = 5.0;
FILE* g_log = nullptr;
bool g_log_tried = false;
long g_log_bytes = 0;
double g_log_summary_start = -1.0;
int g_log_frames = 0;
double g_log_frame_sum = 0.0, g_log_frame_max = 0.0;
int g_log_calls_max = 0;

void log_write(const char* text) {
    if (!g_log_tried) {
        g_log_tried = true;
        const char* path = std::getenv("LINWEB_PROFILE_LOG");
        g_log = std::fopen(path && *path ? path : kLogPath, "w");
    }
    if (!g_log) return;
    if (g_log_bytes > kLogMax) {
        std::rewind(g_log);
        g_log_bytes = 0;
    }
    g_log_bytes += std::fprintf(g_log, "%s\n", text);
    std::fflush(g_log);
}

void log_frame(double script_ms, double style_ms, double layout_ms, double paint_ms, double swap_ms, double frame_ms, double now_s,
               int layout_calls, int layout_hits, bool full_rebuild) {
    char line[256];
    if (frame_ms >= kSlowFrameMs || layout_calls >= kManyLayoutCalls) {
        std::snprintf(line, sizeof(line),
                      "[%.3f] slow frame %.1f ms: script %.1f style %.1f layout %.1f paint %.1f swap %.1f | layout calls %d cached %d rebuild %d",
                      now_s, frame_ms, script_ms, style_ms, layout_ms, paint_ms, swap_ms, layout_calls, layout_hits, full_rebuild ? 1 : 0);
        log_write(line);
        const std::string breakdown = layout_call_breakdown(10);
        if (!breakdown.empty()) log_write(("    layout calls by box: " + breakdown).c_str());
    }
    if (g_log_summary_start < 0.0) g_log_summary_start = now_s;
    ++g_log_frames;
    g_log_frame_sum += frame_ms;
    g_log_frame_max = std::max(g_log_frame_max, frame_ms);
    g_log_calls_max = std::max(g_log_calls_max, layout_calls);
    if (now_s - g_log_summary_start >= kLogSummaryS) {
        std::snprintf(line, sizeof(line), "[%.3f] summary: %.1f fps, frame avg %.1f max %.1f ms, layout calls max %d",
                      now_s, g_log_frames / (now_s - g_log_summary_start), g_log_frame_sum / g_log_frames, g_log_frame_max, g_log_calls_max);
        log_write(line);
        g_log_summary_start = now_s;
        g_log_frames = 0;
        g_log_frame_sum = g_log_frame_max = 0.0;
        g_log_calls_max = 0;
    }
}

double smooth(double average, double value) {
    return average == 0.0 ? value : average + kSmoothing * (value - average);
}

const Frame& recent(int back) { return g_history[(g_head - 1 - back + kHistory * 2) % kHistory]; }

double other_ms(const Frame& f) {
    double sum = 0.0;
    for (double p : f.phase) sum += p;
    return std::max(0.0, f.total - sum);
}

} // namespace

void record(double script_ms, double style_ms, double layout_ms, double paint_ms, double swap_ms, double frame_ms, double now_s,
            int layout_calls, int layout_hits, bool full_rebuild) {
    g_stats.script_ms = smooth(g_stats.script_ms, script_ms);
    g_stats.style_ms = smooth(g_stats.style_ms, style_ms);
    g_stats.layout_ms = smooth(g_stats.layout_ms, layout_ms);
    g_stats.paint_ms = smooth(g_stats.paint_ms, paint_ms);
    g_stats.swap_ms = smooth(g_stats.swap_ms, swap_ms);
    g_stats.frame_ms = smooth(g_stats.frame_ms, frame_ms);

    Frame& f = g_history[g_head];
    f.phase[0] = script_ms;
    f.phase[1] = style_ms;
    f.phase[2] = layout_ms;
    f.phase[3] = paint_ms;
    f.phase[4] = swap_ms;
    f.total = frame_ms;
    f.layout_calls = layout_calls;
    f.layout_hits = layout_hits;
    f.full = full_rebuild;
    g_head = (g_head + 1) % kHistory;
    g_count = std::min(g_count + 1, kHistory);
    log_frame(script_ms, style_ms, layout_ms, paint_ms, swap_ms, frame_ms, now_s, layout_calls, layout_hits, full_rebuild);

    if (g_window_start < 0.0) g_window_start = now_s;
    ++g_window_frames;
    if (now_s - g_window_start >= 1.0) {
        g_stats.fps = g_window_frames / (now_s - g_window_start);
        g_window_start = now_s;
        g_window_frames = 0;
    }
}

Stats stats() {
    return g_stats;
}

bool visible() {
    return g_visible;
}

void toggle() {
    g_visible = !g_visible;
}

void draw_overlay(int framebuffer_width) {
    if (!g_visible || g_count == 0) return;

    const int n = std::min(g_count, kAverageFrames);
    double avg[kPhases] = {};
    double avg_other = 0.0, avg_total = 0.0, worst = 0.0;
    int calls = 0, hits = 0, rebuilds = 0;
    for (int i = 0; i < n; ++i) {
        const Frame& f = recent(i);
        for (int p = 0; p < kPhases; ++p) avg[p] += f.phase[p] / n;
        avg_other += other_ms(f) / n;
        avg_total += f.total / n;
        calls += f.layout_calls;
        hits += f.layout_hits;
        rebuilds += f.full ? 1 : 0;
    }
    for (int i = 0; i < g_count; ++i) worst = std::max(worst, recent(i).total);
    const double scale = std::max(kMinScaleMs, std::min(worst, 200.0));

    const float x = std::max(6.0f, static_cast<float>(framebuffer_width) - kPanelW - 6.0f);
    const float y = 6.0f;
    const float panel_h = 26.0f + kGraphH + 10.0f + (kPhases + 1) * 16.0f + 30.0f;
    Renderer::draw_rect(x, y, kPanelW, panel_h, 0.04f, 0.05f, 0.07f, 0.86f, 6.0f);

    char text[96];
    std::snprintf(text, sizeof(text), "%.0f FPS   %.1f ms avg   %.1f ms worst", g_stats.fps, avg_total, worst);
    Renderer::draw_text(text, x + 10.0f, y + 18.0f, 13.0f, 0.55f, 1.0f, 0.55f, 1.0f, true);

    // The graph: the oldest frame on the left, one bar per frame stacked by phase
    const float gx = x + 10.0f;
    const float gy = y + 26.0f;
    const float base = gy + kGraphH;
    Renderer::draw_rect(gx, gy, kHistory * kBarW, kGraphH, 1.0f, 1.0f, 1.0f, 0.06f);
    for (int i = 0; i < g_count; ++i) {
        const Frame& f = recent(g_count - 1 - i);
        const float bx = gx + (kHistory - g_count + i) * kBarW;
        float top = base;
        auto segment = [&](double ms, const Phase& ph) {
            const float h = std::min(static_cast<float>(ms / scale) * kGraphH, top - gy);
            if (h < 0.4f) return;
            top -= h;
            Renderer::draw_rect(bx, top, kBarW - 1.0f, h, ph.r, ph.g, ph.b, 1.0f);
        };
        for (int p = 0; p < kPhases; ++p) segment(f.phase[p], kPhase[p]);
        segment(other_ms(f), kOther);
    }
    for (double budget : {1000.0 / 60.0, 1000.0 / 30.0}) {   // 60 Hz and 30 Hz budgets
        if (budget >= scale) continue;
        Renderer::draw_rect(gx, base - static_cast<float>(budget / scale) * kGraphH, kHistory * kBarW, 1.0f, 1.0f, 1.0f, 1.0f, 0.35f);
    }
    std::snprintf(text, sizeof(text), "%.0f ms", scale);
    Renderer::draw_text(text, gx + 2.0f, gy + 10.0f, 10.0f, 1.0f, 1.0f, 1.0f, 0.6f);

    // The list: each phase with its average and a bar relative to the frame
    float row_y = base + 10.0f;
    const float bar_x = x + 130.0f;
    const float bar_w = kPanelW - 130.0f - 76.0f;
    auto row = [&](const Phase& ph, double ms) {
        Renderer::draw_rect(x + 10.0f, row_y + 3.0f, 8.0f, 8.0f, ph.r, ph.g, ph.b, 1.0f, 2.0f);
        Renderer::draw_text(ph.name, x + 24.0f, row_y + 12.0f, 11.0f, 0.85f, 0.88f, 0.92f, 1.0f);
        const float w = avg_total > 0.0 ? static_cast<float>(std::min(1.0, ms / avg_total)) * bar_w : 0.0f;
        Renderer::draw_rect(bar_x, row_y + 4.0f, bar_w, 6.0f, 1.0f, 1.0f, 1.0f, 0.08f);
        if (w > 0.5f) Renderer::draw_rect(bar_x, row_y + 4.0f, w, 6.0f, ph.r, ph.g, ph.b, 1.0f);
        std::snprintf(text, sizeof(text), "%6.2f ms", ms);
        Renderer::draw_text(text, bar_x + bar_w + 6.0f, row_y + 12.0f, 11.0f, 0.85f, 0.88f, 0.92f, 1.0f);
        row_y += 16.0f;
    };
    for (int p = 0; p < kPhases; ++p) row(kPhase[p], avg[p]);
    row(kOther, avg_other);

    std::snprintf(text, sizeof(text), "rebuilds %d/%d   layout calls %d, cached %d   (F3 / R2)", rebuilds, n, calls / n, hits / n);
    Renderer::draw_text(text, x + 10.0f, row_y + 16.0f, 10.0f, 0.65f, 0.7f, 0.75f, 1.0f);
}

} // namespace perf
} // namespace linweb
