/* v10 (ARCHITECTURE.md's "v10 -- tbox_cmp"): visual regression tool.
 * Renders every tests/assets/NNN.html fixture (via tbox_app_screenshot_from_files,
 * v7 -- offscreen, no Wayland window needed, see tests/CMakeLists.txt's
 * OpenCV and Fontconfig gate) and compares it
 * against the paired tests/assets/NNN.png "golden" reference via SSIM.
 *
 * .cpp, not .c: the original version of this file (written by the
 * project's maintainer outside this session) used OpenCV's legacy C API
 * (IplImage/cvLoadImage/cvSmooth/...), which no longer exists -- it was
 * removed from OpenCV entirely well before the OpenCV 5.0.0 this project
 * builds against. Ported here to the modern C++ API (cv::Mat and friends);
 * the SSIM/color-diff ALGORITHM is unchanged from the original, only the
 * API calls that express it (and, per the maintainer's request, every
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

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <dirent.h>
#include <limits.h>

// Result of comparing one pair of images.
struct ComparisonResult {
    double ssim_score;
    double percentage;
    int differences_found;
    bool is_equal;
};

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

/* `diff_output_path` == "" skips writing the difference image (NOVO v10 --
 * replaces the fixed "resultado_diferencas_c.png" name the original code
 * had; always saving under that same name would make each asset overwrite
 * the previous one's difference image while iterating a whole directory). */
static ComparisonResult compare_images(const std::string &golden_path, const std::string &test_path, double ssim_threshold, const std::string &diff_output_path) {
    ComparisonResult result = { 0.0, 0.0, 0, false };

    // 1. Load both images (BGR)
    cv::Mat img1 = cv::imread(golden_path, cv::IMREAD_COLOR);
    cv::Mat img2 = cv::imread(test_path, cv::IMREAD_COLOR);

    if (img1.empty() || img2.empty()) {
        fprintf(stderr, "Error: could not load one or both images.\n");
        return result;
    }

    int h1 = img1.rows, w1 = img1.cols;
    int h2 = img2.rows, w2 = img2.cols;

    cv::Mat img2_processed;

    // 2. Business rule: reconcile image 2's size against image 1's
    if (h2 != h1 || w2 != w1) {
        printf("[Info] Dimension mismatch -> Golden: (%dx%d) | Test: (%dx%d)\n", w1, h1, w2, h2);

        // Case 1: image 2 is larger or equal on both axes -> crop
        if (h2 >= h1 && w2 >= w1) {
            printf("[Action] Test image is LARGER. Applying CROP...\n");
            img2_processed = img2(cv::Rect(0, 0, w1, h1)).clone();
        }
        // Case 2: image 2 is smaller on either axis -> resize
        else {
            printf("[Action] Test image is SMALLER. Applying RESIZE...\n");
            cv::resize(img2, img2_processed, cv::Size(w1, h1), 0, 0, cv::INTER_CUBIC);
        }
    } else {
        img2_processed = img2;
    }

    // 3. Convert to grayscale
    cv::Mat gray1, gray2;
    cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
    cv::cvtColor(img2_processed, gray2, cv::COLOR_BGR2GRAY);

    // 4. Compute SSIM
    double ssim_score = compute_ssim(gray1, gray2);

    // 5. Color analysis: absolute RGB difference
    cv::Mat color_diff;
    cv::absdiff(img1, img2_processed, color_diff);

    cv::Mat color_diff_gray;
    cv::cvtColor(color_diff, color_diff_gray, cv::COLOR_BGR2GRAY);

    cv::Mat thresh;
    cv::threshold(color_diff_gray, thresh, 30, 255, cv::THRESH_BINARY);

    // 6. Find the contours of the differences
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    cv::Mat result_image = img2_processed.clone();
    int difference_count = 0;

    for (const auto &contour : contours) {
        double area = std::fabs(cv::contourArea(contour));
        if (area > 10.0) { // Filter out noise
            cv::Rect rect = cv::boundingRect(contour);
            // Draw a red rectangle (BGR: 0, 0, 255)
            cv::rectangle(result_image, rect, cv::Scalar(0, 0, 255), 2);
            difference_count++;
        }
    }

    // 7. Report
    result.ssim_score        = ssim_score;
    result.percentage        = ssim_score * 100.0;
    result.differences_found = difference_count;
    result.is_equal          = (ssim_score >= ssim_threshold && difference_count == 0);

    printf("==================================================\n");
    printf("SSIM index: %.4f (%.2f%%)\n", ssim_score, result.percentage);
    printf("Differences found: %d\n", difference_count);
    printf("Status: %s\n", result.is_equal ? "EQUAL" : "DIFFERENT");
    printf("==================================================\n");

    // Save the annotated result image
    if (!diff_output_path.empty()) {
        cv::imwrite(diff_output_path, result_image);
    }

    return result;
}

/* NOVO v10: reads only the dimensions of an already-existing PNG (the
 * golden) -- used as the exact viewport to render the corresponding HTML
 * with, so the golden and the render are directly comparable without
 * depending on compare_images's own crop/resize fallback (which still
 * exists as a safety net, not removed). */
static bool read_png_size(const std::string &path, int *out_width, int *out_height) {
    cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
    if (img.empty()) {
        return false;
    }
    *out_width  = img.cols;
    *out_height = img.rows;
    return true;
}

/* NOVO v10: renders html_path via tbox_app_screenshot_from_files (v7 --
 * offscreen, no Wayland at runtime) at golden_path's exact dimensions,
 * compares it against golden_path, and prints that pair's result. Returns
 * true if the comparison COMPLETED (even with a DIFFERENT verdict -- see
 * ARCHITECTURE.md's v10 "Escopo": several assets still exercise
 * HTML/CSS tbox doesn't implement, that is expected, not an operational
 * failure); false only on an operational failure (couldn't read the
 * golden's dimensions, couldn't render). `out_is_equal`, if non-NULL,
 * receives the EQUAL/DIFFERENT verdict when the function returns true. */
static bool process_pair(const std::string &html_path, const std::string &golden_path, const std::string &render_path, const std::string &diff_path, double threshold, bool *out_is_equal) {
    int width, height;
    if (!read_png_size(golden_path, &width, &height)) {
        fprintf(stderr, "[%s] could not read dimensions of %s\n", html_path.c_str(), golden_path.c_str());
        return false;
    }

    if (!tbox_app_screenshot_from_files(html_path.c_str(), NULL, width, height, render_path.c_str())) {
        fprintf(stderr, "[%s] failed to render (tbox_app_screenshot_from_files)\n", html_path.c_str());
        return false;
    }

    printf("--- %s vs. %s ---\n", html_path.c_str(), golden_path.c_str());
    ComparisonResult result = compare_images(golden_path, render_path, threshold, diff_path);
    remove(render_path.c_str());

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

/* NOVO v10: walks `assets_dir` looking for every *.html with a sibling
 * *.png (same name, different extension) -- pairs with no matching golden
 * are skipped with a warning, not treated as an error (an HTML fixture may
 * not have a captured golden yet). Returns false only if the directory
 * couldn't be opened; anything else (an operational failure on one
 * specific pair) is counted in `*out_operational_failures` and doesn't
 * stop the rest of the iteration. */
static bool run_directory(const char *assets_dir, double threshold, int *out_total, int *out_equal_count, int *out_operational_failures) {
    DIR *dir = opendir(assets_dir);
    if (dir == NULL) {
        fprintf(stderr, "Could not open directory \"%s\": %s\n", assets_dir, strerror(errno));
        return false;
    }

    int total                = 0;
    int equal_count          = 0;
    int operational_failures = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_html_suffix(entry->d_name)) {
            continue;
        }

        size_t stem_length = strlen(entry->d_name) - 5; /* without ".html" */

        char html_path[PATH_MAX];
        char golden_path[PATH_MAX];
        char render_path[PATH_MAX];
        char diff_path[PATH_MAX];
        snprintf(html_path, sizeof(html_path), "%s/%s", assets_dir, entry->d_name);
        snprintf(golden_path, sizeof(golden_path), "%s/%.*s.png", assets_dir, (int)stem_length, entry->d_name);
        snprintf(render_path, sizeof(render_path), "%s/tbox_cmp_%.*s_render.png", P_tmpdir, (int)stem_length, entry->d_name);
        snprintf(diff_path, sizeof(diff_path), "%s/tbox_cmp_%.*s_diff.png", P_tmpdir, (int)stem_length, entry->d_name);

        FILE *golden_check = fopen(golden_path, "rb");
        if (golden_check == NULL) {
            fprintf(stderr, "[%s] no matching golden PNG (%s), skipping\n", html_path, golden_path);
            continue;
        }
        fclose(golden_check);

        total++;
        bool is_equal = false;
        if (process_pair(html_path, golden_path, render_path, diff_path, threshold, &is_equal)) {
            if (is_equal) {
                equal_count++;
            }
        } else {
            operational_failures++;
        }
    }

    closedir(dir);

    *out_total                = total;
    *out_equal_count          = equal_count;
    *out_operational_failures = operational_failures;
    return true;
}

int main(int argc, char **argv) {
    const char *assets_dir = argc > 1 ? argv[1] : "tests/assets";
    double threshold       = argc > 2 ? atof(argv[2]) : 0.98;

    int total, equal_count, operational_failures;
    if (!run_directory(assets_dir, threshold, &total, &equal_count, &operational_failures)) {
        return 1;
    }

    printf("==================================================\n");
    printf("Summary: %d pairs | %d EQUAL | %d DIFFERENT | %d operational failures\n", total, equal_count, total - equal_count - operational_failures, operational_failures);
    printf("==================================================\n");

    /* Exit code reflects only OPERATIONAL failure (couldn't open the
     * directory, couldn't render, couldn't load an image) -- not each
     * pair's EQUAL/DIFFERENT verdict. See ARCHITECTURE.md's v10 "Escopo":
     * several assets still exercise HTML/CSS tbox doesn't implement,
     * DIFFERENT is expected for them today and shouldn't break `ctest`. */
    return operational_failures > 0 ? 1 : 0;
}
