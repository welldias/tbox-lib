/* v10 (docs/dev/historico.md's "v10 -- tbox_cmp"): visual regression tool.
 * Captures every tests/assets/NNN.html fixture in headless Firefox (the
 * "golden", taken on every run) and renders it with tbox at the same size
 * (via tbox_app_screenshot_from_files, v7 -- offscreen, no Wayland window
 * needed, see tests/CMakeLists.txt's OpenCV and Fontconfig gate), then
 * compares the two. SSIM is reported, while local content coverage
 * determines the verdict. Both screenshots and the annotated difference
 * are kept in the output directory.
 *
 * .cpp, not .c: the original version of this file (written by the
 * project's maintainer outside this session) used OpenCV's legacy C API
 * (IplImage/cvLoadImage/cvSmooth/...), which no longer exists -- it was
 * removed from OpenCV entirely well before the OpenCV 5.0.0 this project
 * builds against. Ported here to the modern C++ API (cv::Mat and friends);
 * the API calls that express it (and, per the maintainer's request, every
 * identifier/comment/message below, translated from the original's
 * Portuguese). This is the one .cpp file in an otherwise pure-C project --
 * see the root CMakeLists.txt's OpenCV detection for why that's an
 * isolated, opt-in cost (only when OpenCV is found at all) rather than
 * something the rest of the codebase has to accommodate. */

#define _POSIX_C_SOURCE 200809L

#include <tbox/tbox.h>

#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

// Result of comparing one pair of images.
struct ComparisonResult {
    double ssim_score;
    double golden_coverage;
    double render_coverage;
    double golden_edge_coverage;
    double render_edge_coverage;
    int failed_regions;
    bool completed;
    bool is_equal;
};

static constexpr int kInkThreshold       = 24;
static constexpr int kPositionTolerance  = 8;
static constexpr int kMinimumRegionInk   = 32;
static constexpr double kMinimumCoverage = 0.80;

// Count a pixel as content when at least one channel differs visibly from
// the white canvas used by both the browser fixtures and the screenshot API.
static cv::Mat content_mask(const cv::Mat &image) {
    cv::Mat distance, channels[3], maximum;
    cv::absdiff(image, cv::Scalar::all(255), distance);
    cv::split(distance, channels);
    cv::max(channels[0], channels[1], maximum);
    cv::max(maximum, channels[2], maximum);
    cv::Mat mask;
    cv::threshold(maximum, mask, kInkThreshold, 255, cv::THRESH_BINARY);
    return mask;
}

// Edges expose text even inside a filled cell, where the content mask alone
// sees the cell's background as a single large region.
static cv::Mat edge_mask(const cv::Mat &gray) {
    cv::Mat edges;
    cv::Canny(gray, edges, 40, 100);
    return edges;
}

struct ContentRegion {
    cv::Rect bounds;
};

// Horizontal closing joins nearby glyphs into a text run. The original mask
// supplies the ink count, so the closing operation cannot invent content.
static std::vector<ContentRegion> content_regions(const cv::Mat &mask) {
    cv::Mat grouped, labels, stats, centroids;
    cv::morphologyEx(mask, grouped, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(17, 3)));
    int count = cv::connectedComponentsWithStats(grouped, labels, stats, centroids, 8);
    std::vector<ContentRegion> regions;
    for (int label = 1; label < count; label++) {
        cv::Rect bounds(stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP), stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT));
        int ink = cv::countNonZero(mask(bounds));
        if (ink >= kMinimumRegionInk) {
            regions.push_back({ bounds });
        }
    }
    return regions;
}

static double coverage(const cv::Mat &source, const cv::Mat &near_target) {
    int ink = cv::countNonZero(source);
    if (ink == 0) {
        return 1.0;
    }
    cv::Mat covered;
    cv::bitwise_and(source, near_target, covered);
    return (double)cv::countNonZero(covered) / ink;
}

static cv::Rect expanded_to_image(cv::Rect bounds, cv::Size size) {
    return (bounds + cv::Size(2 * kPositionTolerance, 2 * kPositionTolerance) - cv::Point(kPositionTolerance, kPositionTolerance)) & cv::Rect(cv::Point(), size);
}

static int failed_region_count(const cv::Mat &source, const cv::Mat &near_target, const std::vector<ContentRegion> &regions, cv::Mat &annotation) {
    int failures = 0;
    for (const ContentRegion &region : regions) {
        if (coverage(source(region.bounds), near_target(region.bounds)) >= kMinimumCoverage) {
            continue;
        }
        cv::rectangle(annotation, expanded_to_image(region.bounds, source.size()), cv::Scalar(0, 0, 255), 2);
        failures++;
    }
    return failures;
}

// Computes the SSIM between two grayscale images (same algorithm as the
// original C version, just re-expressed with cv::Mat instead of IplImage).
static double compute_ssim(const cv::Mat &img1, const cv::Mat &img2) {
    const double C1 = 6.5025, C2 = 58.5225;

    cv::Mat I1, I2;
    img1.convertTo(I1, CV_32F);
    img2.convertTo(I2, CV_32F);

    cv::Mat I1_2  = I1.mul(I1);
    cv::Mat I2_2  = I2.mul(I2);
    cv::Mat I1_I2 = I1.mul(I2);

    cv::Mat mu1, mu2;
    cv::GaussianBlur(I1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(I2, mu2, cv::Size(11, 11), 1.5);

    cv::Mat mu1_2   = mu1.mul(mu1);
    cv::Mat mu2_2   = mu2.mul(mu2);
    cv::Mat mu1_mu2 = mu1.mul(mu2);

    cv::Mat sigma1_2, sigma2_2, sigma12;
    cv::GaussianBlur(I1_2, sigma1_2, cv::Size(11, 11), 1.5);
    sigma1_2 -= mu1_2;

    cv::GaussianBlur(I2_2, sigma2_2, cv::Size(11, 11), 1.5);
    sigma2_2 -= mu2_2;

    cv::GaussianBlur(I1_I2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    // Numerator: (2*mu1_mu2 + C1) * (2*sigma12 + C2)
    cv::Mat t1 = 2 * mu1_mu2 + C1;
    cv::Mat t2 = 2 * sigma12 + C2;
    cv::Mat t3 = t1.mul(t2);

    // Denominator: (mu1_2 + mu2_2 + C1) * (sigma1_2 + sigma2_2 + C2)
    t1 = mu1_2 + mu2_2 + C1;
    t2 = sigma1_2 + sigma2_2 + C2;
    t1 = t1.mul(t2);

    cv::Mat ssim_map;
    cv::divide(t3, t1, ssim_map);

    cv::Scalar mssim = cv::mean(ssim_map);
    return mssim[0];
}

/* `diff_output_path` == "" skips writing the difference image ( --
 * replaces the fixed "resultado_diferencas_c.png" name the original code
 * had; always saving under that same name would make each asset overwrite
 * the previous one's difference image while iterating a whole directory). */
static ComparisonResult compare_images(const std::string &golden_path, const std::string &test_path, const std::string &diff_output_path) {
    ComparisonResult result = {};

    // 1. Load both images (BGR)
    cv::Mat img1 = cv::imread(golden_path, cv::IMREAD_COLOR);
    cv::Mat img2 = cv::imread(test_path, cv::IMREAD_COLOR);

    if (img1.empty() || img2.empty()) {
        fprintf(stderr, "Error: could not load %s or %s.\n", golden_path.c_str(), test_path.c_str());
        return result;
    }

    if (img1.size() != img2.size()) {
        fprintf(stderr, "Dimension mismatch: golden %dx%d, render %dx%d.\n", img1.cols, img1.rows, img2.cols, img2.rows);
        return result;
    }
    result.completed = true;

    // SSIM remains useful for tracking gradual fidelity changes, but a large
    // white background can make it high even when a small text run is gone.
    cv::Mat gray1, gray2;
    cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
    cv::cvtColor(img2, gray2, cv::COLOR_BGR2GRAY);
    result.ssim_score = compute_ssim(gray1, gray2);

    cv::Mat golden_mask = content_mask(img1);
    cv::Mat render_mask = content_mask(img2);
    cv::Mat near_golden, near_render;
    cv::Mat proximity = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * kPositionTolerance + 1, 2 * kPositionTolerance + 1));
    cv::dilate(golden_mask, near_golden, proximity);
    cv::dilate(render_mask, near_render, proximity);

    result.golden_coverage = coverage(golden_mask, near_render);
    result.render_coverage = coverage(render_mask, near_golden);

    cv::Mat annotation = img2.clone();
    result.failed_regions += failed_region_count(golden_mask, near_render, content_regions(golden_mask), annotation);
    result.failed_regions += failed_region_count(render_mask, near_golden, content_regions(render_mask), annotation);

    cv::Mat golden_edges = edge_mask(gray1);
    cv::Mat render_edges = edge_mask(gray2);
    cv::Mat near_golden_edges, near_render_edges;
    cv::dilate(golden_edges, near_golden_edges, proximity);
    cv::dilate(render_edges, near_render_edges, proximity);
    result.golden_edge_coverage = coverage(golden_edges, near_render_edges);
    result.render_edge_coverage = coverage(render_edges, near_golden_edges);
    result.failed_regions += failed_region_count(golden_edges, near_render_edges, content_regions(golden_edges), annotation);
    result.failed_regions += failed_region_count(render_edges, near_golden_edges, content_regions(render_edges), annotation);

    // A flat colored area has the same silhouette after changing color. Only
    // compare interior pixels, leaving antialiased edges to the ink check.
    cv::Mat inner_golden, inner_render, common, color_delta, channels[3], maximum, wrong_color;
    cv::Mat interior = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::erode(golden_mask, inner_golden, interior);
    cv::erode(render_mask, inner_render, interior);
    cv::bitwise_and(inner_golden, inner_render, common);
    cv::absdiff(img1, img2, color_delta);
    cv::split(color_delta, channels);
    cv::max(channels[0], channels[1], maximum);
    cv::max(maximum, channels[2], maximum);
    cv::threshold(maximum, wrong_color, 40, 255, cv::THRESH_BINARY);
    cv::bitwise_and(wrong_color, common, wrong_color);
    int color_area = cv::countNonZero(common);
    if (color_area >= 32 && cv::countNonZero(wrong_color) > color_area / 10) {
        result.failed_regions++;
        std::vector<cv::Point> points;
        cv::findNonZero(wrong_color, points);
        cv::rectangle(annotation, expanded_to_image(cv::boundingRect(points), img1.size()), cv::Scalar(0, 0, 255), 2);
    }

    // A small solid color change must remain visible even when a large
    // matching background makes its share of the total image tiny.
    cv::Mat solid_color_diff, labels, stats, centroids;
    cv::morphologyEx(wrong_color, solid_color_diff, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9)));
    int color_regions = cv::connectedComponentsWithStats(solid_color_diff, labels, stats, centroids, 8);
    for (int label = 1; label < color_regions; label++) {
        if (stats.at<int>(label, cv::CC_STAT_AREA) < 64) {
            continue;
        }
        cv::Rect bounds(stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP), stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT));
        cv::rectangle(annotation, expanded_to_image(bounds, img1.size()), cv::Scalar(0, 0, 255), 2);
        result.failed_regions++;
    }

    result.is_equal = result.failed_regions == 0 && result.golden_coverage >= kMinimumCoverage && result.render_coverage >= kMinimumCoverage && result.golden_edge_coverage >= kMinimumCoverage && result.render_edge_coverage >= kMinimumCoverage;

    // Global coverage can fail when every individual mismatch is small.
    if (!result.is_equal && result.failed_regions == 0) {
        cv::Mat missing_golden, missing_render, missing_edges_golden, missing_edges_render, unmatched;
        cv::bitwise_and(golden_mask, ~near_render, missing_golden);
        cv::bitwise_and(render_mask, ~near_golden, missing_render);
        cv::bitwise_and(golden_edges, ~near_render_edges, missing_edges_golden);
        cv::bitwise_and(render_edges, ~near_golden_edges, missing_edges_render);
        unmatched = missing_golden | missing_render | missing_edges_golden | missing_edges_render;
        std::vector<cv::Point> points;
        cv::findNonZero(unmatched, points);
        if (!points.empty()) {
            cv::rectangle(annotation, expanded_to_image(cv::boundingRect(points), img1.size()), cv::Scalar(0, 0, 255), 2);
            result.failed_regions = 1;
        }
    }

    printf("==================================================\n");
    printf("SSIM index: %.4f (diagnostic only)\n", result.ssim_score);
    printf("Content coverage: golden %.1f%%, render %.1f%%\n", result.golden_coverage * 100.0, result.render_coverage * 100.0);
    printf("Edge coverage: golden %.1f%%, render %.1f%%\n", result.golden_edge_coverage * 100.0, result.render_edge_coverage * 100.0);
    printf("Failed local checks: %d\n", result.failed_regions);
    printf("Status: %s\n", result.is_equal ? "EQUAL" : "DIFFERENT");
    printf("==================================================\n");

    // Save the annotated result image
    if (!diff_output_path.empty()) {
        if (!cv::imwrite(diff_output_path, annotation)) {
            fprintf(stderr, "Error: could not write %s.\n", diff_output_path.c_str());
            result.completed = false;
        }
    }

    return result;
}

/* Options of one run (see usage()). */
struct Options {
    const char *assets_dir = "tests/assets";
    const char *output_dir = NULL; /* NULL: a fresh temporary directory */
    const char *firefox    = NULL; /* NULL: $TBOX_CMP_FIREFOX, else "firefox" on PATH */
    int width              = 800;
    int height             = 600;
    std::vector<std::string> only; /* fixture stems to run; empty = all */
};

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [--size WxH] [--output DIR] [--firefox PATH] [assets_dir] [fixture...]\n"
            "  Captures every assets_dir/*.html in headless Firefox (the golden) and\n"
            "  renders it with tbox at the same size (default 800x600), then compares\n"
            "  them. DIR receives NNN_firefox.png, NNN_render.png and NNN_diff.png for\n"
            "  each fixture (default: a new temporary directory). `fixture` limits the\n"
            "  run to the named ones (\"065\" or \"065.html\").\n",
            argv0);
}

/* Removes `path` and everything under it (the Firefox profile). */
static void remove_tree(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            std::string child = path + "/" + entry->d_name;
            struct stat info;
            if (lstat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
                remove_tree(child);
            else
                unlink(child.c_str());
        }
        closedir(dir);
    }
    rmdir(path.c_str());
}

/* file:// URL of an absolute path, percent-encoding what a URL can't hold. */
static std::string file_url(const char *absolute) {
    static const char hex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (const unsigned char *c = (const unsigned char *)absolute; *c != '\0'; c++) {
        if (isalnum(*c) || strchr("/-_.~", *c) != NULL) {
            url += (char)*c;
        } else {
            url += '%';
            url += hex[*c >> 4];
            url += hex[*c & 15];
        }
    }
    return url;
}

/* Captures `html_path` in headless Firefox at width x height into
 * `out_path` -- the golden of the comparison, taken on every run instead
 * of a PNG kept next to the fixture. `profile` is a scratch profile
 * directory, so a running Firefox and the user's own profile are never
 * touched. Gives up after 90 s. */
static bool capture_firefox(const char *firefox, const std::string &profile, const std::string &html_path, int width, int height, const std::string &out_path) {
    char absolute[PATH_MAX];
    if (realpath(html_path.c_str(), absolute) == NULL) {
        fprintf(stderr, "[%s] %s\n", html_path.c_str(), strerror(errno));
        return false;
    }
    std::string size = "--window-size=" + std::to_string(width) + "," + std::to_string(height);
    std::string url  = file_url(absolute);
    const char *argv[] = { firefox, "--headless", "--no-remote", "--profile", profile.c_str(), size.c_str(), "--screenshot", out_path.c_str(), url.c_str(), NULL };

    remove(out_path.c_str());
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int error = posix_spawnp(&pid, firefox, &actions, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        fprintf(stderr, "[%s] could not start %s: %s\n", html_path.c_str(), firefox, strerror(error));
        return false;
    }
    int status = 0;
    for (int waited = 0;; waited++) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 || waited >= 900) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fprintf(stderr, "[%s] Firefox did not finish the capture\n", html_path.c_str());
            return false;
        }
        struct timespec pause = { 0, 100 * 1000 * 1000 };
        nanosleep(&pause, NULL);
    }
    struct stat info;
    if (stat(out_path.c_str(), &info) != 0 || info.st_size == 0) {
        fprintf(stderr, "[%s] Firefox wrote no screenshot (exit status %d)\n", html_path.c_str(), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}

/* Captures html_path in Firefox, renders it via tbox_app_screenshot_from_files
 * (v7 -- offscreen, no Wayland at runtime) at the same size, compares the
 * two and prints that pair's result. Both screenshots and the annotated
 * difference stay in the output directory. Returns true if the comparison
 * COMPLETED (even with a DIFFERENT verdict -- see docs/dev/historico.md's
 * v10 "Escopo": several assets still exercise HTML/CSS tbox doesn't
 * implement, that is expected, not an operational failure); false on an
 * operational failure (couldn't capture, render, compare, or write the
 * annotated image). `out_is_equal`, if non-NULL, receives the
 * EQUAL/DIFFERENT verdict when the function returns true. */
static bool process_fixture(const Options &options, const char *firefox, const std::string &profile, const std::string &html_path, const std::string &golden_path, const std::string &render_path, const std::string &diff_path, bool *out_is_equal) {
    if (!capture_firefox(firefox, profile, html_path, options.width, options.height, golden_path))
        return false;

    if (!tbox_app_screenshot_from_files(html_path.c_str(), NULL, options.width, options.height, render_path.c_str())) {
        fprintf(stderr, "[%s] failed to render (tbox_app_screenshot_from_files)\n", html_path.c_str());
        return false;
    }

    printf("--- %s vs. %s ---\n", html_path.c_str(), golden_path.c_str());
    ComparisonResult result = compare_images(golden_path, render_path, diff_path);
    if (!result.completed) {
        return false;
    }

    if (out_is_equal != NULL) {
        *out_is_equal = result.is_equal;
    }
    return true;
}

/* True if `name` ends in ".html". */
static bool has_html_suffix(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".html") == 0;
}

/* Walks `options.assets_dir` for every *.html (or the ones named in
 * `options.only`) and compares each against its Firefox capture. Returns
 * false only if the directories couldn't be opened or created; anything
 * else (an operational failure on one fixture) is counted in
 * `*out_operational_failures` and doesn't stop the rest of the run. */
static bool run_directory(const Options &options, int *out_total, int *out_equal_count, int *out_operational_failures) {
    DIR *dir = opendir(options.assets_dir);
    if (dir == NULL) {
        fprintf(stderr, "Could not open directory \"%s\": %s\n", options.assets_dir, strerror(errno));
        return false;
    }

    char output_dir[PATH_MAX];
    if (options.output_dir != NULL) {
        int output_length = snprintf(output_dir, sizeof(output_dir), "%s", options.output_dir);
        if (output_length < 0 || (size_t)output_length >= sizeof(output_dir) || (mkdir(output_dir, 0755) != 0 && errno != EEXIST)) {
            fprintf(stderr, "Could not create output directory \"%s\"\n", options.output_dir);
            closedir(dir);
            return false;
        }
    } else {
        int output_length = snprintf(output_dir, sizeof(output_dir), "%s/tbox_cmp_XXXXXX", P_tmpdir);
        if (output_length < 0 || (size_t)output_length >= sizeof(output_dir) || mkdtemp(output_dir) == NULL) {
            fprintf(stderr, "Could not create output directory: %s\n", strerror(errno));
            closedir(dir);
            return false;
        }
    }
    printf("Screenshots and difference images: %s\n", output_dir);

    const char *firefox = options.firefox;
    if (firefox == NULL)
        firefox = getenv("TBOX_CMP_FIREFOX");
    if (firefox == NULL || firefox[0] == '\0')
        firefox = "firefox";
    std::string profile = std::string(output_dir) + "/firefox-profile-XXXXXX";
    if (mkdtemp(&profile[0]) == NULL) {
        fprintf(stderr, "Could not create the Firefox profile directory: %s\n", strerror(errno));
        closedir(dir);
        return false;
    }

    int total                = 0;
    int equal_count          = 0;
    int operational_failures = 0;

    struct dirent *entry;
    std::vector<std::string> names;
    while ((entry = readdir(dir)) != NULL) {
        if (has_html_suffix(entry->d_name)) {
            names.emplace_back(entry->d_name);
        }
    }
    closedir(dir);
    std::sort(names.begin(), names.end());

    for (const std::string &name : names) {
        std::string stem = name.substr(0, name.size() - 5); /* without ".html" */
        if (!options.only.empty() && std::find(options.only.begin(), options.only.end(), stem) == options.only.end())
            continue;

        std::string html_path   = std::string(options.assets_dir) + "/" + name;
        std::string golden_path = std::string(output_dir) + "/" + stem + "_firefox.png";
        std::string render_path = std::string(output_dir) + "/" + stem + "_render.png";
        std::string diff_path   = std::string(output_dir) + "/" + stem + "_diff.png";

        total++;
        bool is_equal = false;
        if (process_fixture(options, firefox, profile, html_path, golden_path, render_path, diff_path, &is_equal)) {
            if (is_equal) {
                equal_count++;
            }
        } else {
            operational_failures++;
        }
    }
    remove_tree(profile);

    *out_total                = total;
    *out_equal_count          = equal_count;
    *out_operational_failures = operational_failures;
    return true;
}

/* "WxH" with both in 1..8192. */
static bool parse_size(const char *text, int *out_width, int *out_height) {
    char *end      = NULL;
    long width     = strtol(text, &end, 10);
    if (end == text || *end != 'x')
        return false;
    const char *rest = end + 1;
    long height      = strtol(rest, &end, 10);
    if (end == rest || *end != '\0' || width < 1 || width > 8192 || height < 1 || height > 8192)
        return false;
    *out_width  = (int)width;
    *out_height = (int)height;
    return true;
}

int main(int argc, char **argv) {
    Options options;
    bool have_dir = false;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if ((strcmp(arg, "--size") == 0 || strcmp(arg, "--output") == 0 || strcmp(arg, "--firefox") == 0) && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(arg, "--size") == 0) {
                if (!parse_size(value, &options.width, &options.height)) {
                    fprintf(stderr, "Invalid size: %s (expected WxH, e.g. 800x600)\n", value);
                    return 1;
                }
            } else if (strcmp(arg, "--output") == 0) {
                options.output_dir = value;
            } else {
                options.firefox = value;
            }
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 || arg[0] == '-') {
            usage(argv[0]);
            return arg[0] == '-' && strcmp(arg, "--help") != 0 && strcmp(arg, "-h") != 0 ? 1 : 0;
        } else if (!have_dir) {
            options.assets_dir = arg;
            have_dir           = true;
        } else {
            std::string stem = arg;
            if (has_html_suffix(stem.c_str()))
                stem.resize(stem.size() - 5);
            options.only.push_back(stem);
        }
    }

    int total, equal_count, operational_failures;
    if (!run_directory(options, &total, &equal_count, &operational_failures)) {
        return 1;
    }

    printf("==================================================\n");
    printf("Summary: %d fixtures | %d EQUAL | %d DIFFERENT | %d operational failures\n", total, equal_count, total - equal_count - operational_failures, operational_failures);
    printf("==================================================\n");

    /* Exit code reflects only OPERATIONAL failure (couldn't open the
     * directory, capture, render, load an image) -- not each fixture's
     * EQUAL/DIFFERENT verdict. See docs/dev/historico.md's v10 "Escopo":
     * several assets still exercise HTML/CSS tbox doesn't implement,
     * DIFFERENT is expected for them today and shouldn't break anything. */
    return operational_failures > 0 ? 1 : 0;
}
