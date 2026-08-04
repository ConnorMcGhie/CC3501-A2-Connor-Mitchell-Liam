// // PlantMonitor

// //
// // Two modes:
// //   Calibration (default): opens the GUI with trackbars, live camera + mask
// //       preview, on-screen area readout. Press 's' to save the current
// //       thresholds to a config file, 'q' or ESC to quit.
// //   Headless (--headless): no GUI. Loads thresholds from the config file
// //       (falls back to built-in defaults), samples the camera at a fixed
// //       interval, logs area/centroid/timestamp to CSV, saves a snapshot
// //       image per sample, and prints an alert if the green area collapses
// //       relative to a rolling baseline.

// #include <opencv2/opencv.hpp>
// #include <sys/time.h>
// #include <csignal>
// #include <cstdio>
// #include <cstring>
// #include <ctime>
// #include <filesystem>
// #include <fstream>
// #include <iostream>
// #include <sstream>
// #include <string>
// #include <vector>

// using namespace cv;
// namespace fs = std::filesystem;

// // Configuration
// struct Thresholds {
//     int lowH = 55, highH = 94;
//     int lowS = 157, highS = 255;
//     int lowV = 26, highV = 255;
//     int openKernel = 5, closeKernel = 5;
// };

// struct Options {
//     bool headless = false;
//     std::string configPath = "plant_thresholds.cfg";
//     std::string logDir = "plant_monitor_data";
//     int intervalSeconds = 10;      // how often to sample/log in headless mode
//     int baselineSamples = 3;        // number of initial samples averaged into the baseline
//     double alertDropFraction = 0.70; // alert if area falls below this fraction of baseline
//     std::string alertCmd;            // optional external script/command to run on alert
// };

// static bool loadThresholds(const std::string &path, Thresholds &t) {
//     std::ifstream in(path);
//     if (!in.is_open()) return false;
//     std::string line;
//     while (std::getline(in, line)) {
//         std::istringstream iss(line);
//         std::string key;
//         if (!std::getline(iss, key, '=')) continue;
//         std::string valueStr;
//         if (!std::getline(iss, valueStr)) continue;
//         int value = std::atoi(valueStr.c_str());
//         if (key == "lowH") t.lowH = value;
//         else if (key == "highH") t.highH = value;
//         else if (key == "lowS") t.lowS = value;
//         else if (key == "highS") t.highS = value;
//         else if (key == "lowV") t.lowV = value;
//         else if (key == "highV") t.highV = value;
//         else if (key == "openKernel") t.openKernel = value;
//         else if (key == "closeKernel") t.closeKernel = value;
//     }
//     return true;
// }

// static bool saveThresholds(const std::string &path, const Thresholds &t) {
//     std::ofstream out(path, std::ios::trunc);
//     if (!out.is_open()) return false;
//     out << "lowH=" << t.lowH << "\n"
//         << "highH=" << t.highH << "\n"
//         << "lowS=" << t.lowS << "\n"
//         << "highS=" << t.highS << "\n"
//         << "lowV=" << t.lowV << "\n"
//         << "highV=" << t.highV << "\n"
//         << "openKernel=" << t.openKernel << "\n"
//         << "closeKernel=" << t.closeKernel << "\n";
//     return true;
// }

// static std::string timestampForFilename() {
//     std::time_t t = std::time(nullptr);
//     std::tm tm{};
//     localtime_r(&t, &tm);
//     char buf[32];
//     std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
//     return std::string(buf);
// }

// static std::string timestampIso() {
//     std::time_t t = std::time(nullptr);
//     std::tm tm{};
//     localtime_r(&t, &tm);
//     char buf[32];
//     std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
//     return std::string(buf);
// }

// static VideoCapture openCamera() {
//     std::string pipeline = "libcamerasrc"
//         " ! video/x-raw, width=800, height=600"
//         " ! videoconvert"
//         " ! videoscale"
//         " ! video/x-raw, width=400, height=300"
//         //" ! videoflip method=rotate-180" // uncomment if the image is upside-down
//         " ! appsink drop=true max_buffers=2";
//     return VideoCapture(pipeline, CAP_GSTREAMER);
// }

// // Applies the HSV threshold + morphology cleanup, returns the cleaned mask
// // and fills contours/moments for the caller.
// static Mat segmentPlant(const Mat &frame, const Thresholds &t, Mat &hsv_img) {
//     cvtColor(frame, hsv_img, COLOR_BGR2HSV);
//     Mat thresh_img;
//     inRange(hsv_img, Scalar(t.lowH, t.lowS, t.lowV), Scalar(t.highH, t.highS, t.highV), thresh_img);

//     int openK = std::max(1, t.openKernel);
//     int closeK = std::max(1, t.closeKernel);
//     morphologyEx(thresh_img, thresh_img, MORPH_CLOSE,
//                  getStructuringElement(MORPH_RECT, Size(closeK, closeK)));
//     morphologyEx(thresh_img, thresh_img, MORPH_OPEN,
//                  getStructuringElement(MORPH_RECT, Size(openK, openK)));
//     return thresh_img;
// }

// // Runs an external command/script to deliver an alert (SMS, webhook, email,
// // etc). The message is passed as a single quoted argument, e.g.:
// //   ./send_sms_twilio.sh "ALERT: green area dropped to ..."
// static void runAlertCommand(const std::string &cmd, const std::string &message) {
//     if (cmd.empty()) return;
//     std::string escaped;
//     escaped.reserve(message.size());
//     for (char c : message) {
//         if (c == '\'') escaped += "'\\''"; // close quote, escaped quote, reopen quote
//         else escaped += c;
//     }
//     std::string fullCmd = cmd + " '" + escaped + "'";
//     int result = std::system(fullCmd.c_str());
//     if (result != 0) {
//         std::cerr << "Warning: alert command exited with status " << result
//                   << " (" << cmd << ")\n";
//     }
// }

// // Ctrl+C handling for clean shutdown in headless mode
// static volatile std::sig_atomic_t g_stopRequested = 0;
// static void handleSigint(int) { g_stopRequested = 1; }

// // Calibration mode (GUI, trackbars) - press 's' to save thresholds, 'q'/ESC to quit
// static int runCalibration(const Options &opts) {
//     Thresholds t;
//     if (loadThresholds(opts.configPath, t)) {
//         std::cout << "Loaded thresholds from " << opts.configPath << "\n";
//     } else {
//         std::cout << "No config found at " << opts.configPath << ", using defaults.\n";
//     }

//     VideoCapture cap = openCamera();
//     if (!cap.isOpened()) {
//         std::cerr << "Could not open camera.\n";
//         return 1;
//     }

//     namedWindow("Control", WINDOW_AUTOSIZE);
//     createTrackbar("LowH", "Control", &t.lowH, 179);
//     createTrackbar("HighH", "Control", &t.highH, 179);
//     createTrackbar("LowS", "Control", &t.lowS, 255);
//     createTrackbar("HighS", "Control", &t.highS, 255);
//     createTrackbar("LowV", "Control", &t.lowV, 255);
//     createTrackbar("HighV", "Control", &t.highV, 255);
//     createTrackbar("Open Kernel Size", "Control", &t.openKernel, 20);
//     createTrackbar("Close Kernel Size", "Control", &t.closeKernel, 20);

//     namedWindow("Display", WINDOW_AUTOSIZE);
//     namedWindow("Thresholded", WINDOW_AUTOSIZE);

//     std::cout << "Calibration mode. Adjust trackbars until the plant is cleanly segmented.\n"
//               << "  's' = save thresholds to " << opts.configPath << "\n"
//               << "  'q' or ESC = quit\n";

//     Mat frame, hsv_img;
//     for (;;) {
//         if (!cap.read(frame)) {
//             std::cerr << "Could not read a frame.\n";
//             break;
//         }
//         if (frame.empty()) continue;

//         Mat thresh_img = segmentPlant(frame, t, hsv_img);

//         std::vector<std::vector<Point>> contours;
//         std::vector<Vec4i> hierarchy;
//         findContours(thresh_img, contours, hierarchy, RETR_TREE, CHAIN_APPROX_NONE);

//         Mat display_img = frame.clone();
//         drawContours(display_img, contours, -1, Scalar(0, 255, 0), 2);

//         Moments m = moments(thresh_img, true);
//         std::string text = "area: 0 px";
//         Point textPos(10, 20);
//         if (m.m00 > 0) {
//             int cx = int(m.m10 / m.m00);
//             int cy = int(m.m01 / m.m00);
//             circle(display_img, Point(cx, cy), 5, Scalar(0, 0, 255), -1);
//             text = "area: " + std::to_string((long)m.m00) + " px  centroid: (" +
//                    std::to_string(cx) + ", " + std::to_string(cy) + ")";
//         }
//         putText(display_img, text, textPos, FONT_HERSHEY_SIMPLEX, 0.5,
//                 Scalar(255, 0, 0), 1, LINE_AA);

//         imshow("Thresholded", thresh_img);
//         imshow("Display", display_img);

//         int key = waitKey(100);
//         if (key == 'q' || key == 27) {
//             break;
//         } else if (key == 's') {
//             if (saveThresholds(opts.configPath, t)) {
//                 std::cout << "Saved thresholds to " << opts.configPath << "\n";
//             } else {
//                 std::cerr << "Failed to save thresholds to " << opts.configPath << "\n";
//             }
//         }
//     }

//     cap.release();
//     return 0;
// }

// // Headless monitoring mode - sample on an interval, log to CSV, save
// // snapshots, and raise a simple baseline-drop / blackout alert.
// static int runHeadless(const Options &opts) {
//     Thresholds t;
//     if (loadThresholds(opts.configPath, t)) {
//         std::cout << "Loaded thresholds from " << opts.configPath << "\n";
//     } else {
//         std::cout << "No config found at " << opts.configPath
//                   << ", using built-in defaults. Run without --headless first to calibrate.\n";
//     }

//     VideoCapture cap = openCamera();
//     if (!cap.isOpened()) {
//         std::cerr << "Could not open camera.\n";
//         return 1;
//     }

//     fs::create_directories(opts.logDir);
//     fs::path snapshotDir = fs::path(opts.logDir) / "snapshots";
//     fs::create_directories(snapshotDir);
//     fs::path csvPath = fs::path(opts.logDir) / "plant_log.csv";
//     fs::path alertPath = fs::path(opts.logDir) / "alerts.log";

//     bool csvExists = fs::exists(csvPath) && fs::file_size(csvPath) > 0;
//     std::ofstream csv(csvPath, std::ios::app);
//     if (!csvExists) {
//         csv << "timestamp,area_px,centroid_x,centroid_y,snapshot_file\n";
//     }

//     std::signal(SIGINT, handleSigint);
//     std::signal(SIGTERM, handleSigint);

//     std::cout << "Headless monitoring started. Logging to " << opts.logDir
//               << " every " << opts.intervalSeconds << "s. Ctrl+C to stop.\n";

//     std::vector<double> baselineReadings;
//     double baselineArea = -1.0;
//     long sampleCount =0;
//     Mat frame, hsv_img;

    
    
//     while (!g_stopRequested) {
//         if (!cap.read(frame) || frame.empty()) {
//             std::cerr << "[" << timestampIso() << "] Warning: could not read a frame, retrying.\n";
//         } else {
//             Mat thresh_img = segmentPlant(frame, t, hsv_img);
//             Moments m = moments(thresh_img, true);
//             double area = m.m00;
//             int cx = 0, cy = 0;
//             if (area > 0) {
//                 cx = int(m.m10 / area);
//                 cy = int(m.m01 / area);
//             }

//             std::string ts = timestampIso();
//             std::string snapshotName = "plant_" + timestampForFilename() + ".jpg";
//             imwrite((snapshotDir / snapshotName).string(), frame);

//             csv << ts << "," << (long)area << "," << cx << "," << cy << ","
//                 << ("snapshots/" + snapshotName) << "\n";
//             csv.flush();

//             std::cout << "[" << ts << "] area=" << (long)area
//                       << "px centroid=(" << cx << "," << cy << ")\n";

//             // Build the baseline from the first N valid (non-blackout) samples.
//             sampleCount++;
//             if (baselineArea < 0 && area > 0 && sampleCount > 1) {
//                 baselineReadings.push_back(area);
//                 if ((int)baselineReadings.size() >= opts.baselineSamples) {
//                     double sum = 0;
//                     for (double v : baselineReadings) sum += v;
//                     baselineArea = sum / baselineReadings.size();
//                     std::cout << "Baseline established: " << (long)baselineArea << " px\n";
//                 }
//             } else if (baselineArea > 0) {
//                 if (area <= 0) {
//                     std::ofstream alertLog(alertPath, std::ios::app);
//                     std::string msg = "[" + ts + "] ALERT: no plant detected (camera blocked, "
//                                        "lighting changed, or thresholds need recalibration).";
//                     std::cerr << msg << "\n";
//                     alertLog << msg << "\n";
//                     runAlertCommand(opts.alertCmd, msg);
//                 } else if (area < opts.alertDropFraction * baselineArea) {
//                     std::ofstream alertLog(alertPath, std::ios::app);
//                     std::string msg = "[" + ts + "] ALERT: green area dropped to " +
//                                        std::to_string((long)area) + "px, " +
//                                        std::to_string((int)(100 * area / baselineArea)) +
//                                        "% of baseline (" + std::to_string((long)baselineArea) + "px).";
//                     std::cerr << msg << "\n";
//                     alertLog << msg << "\n";
//                     runAlertCommand(opts.alertCmd, msg);
//                 }
//             }
//         }

//         // Sleep in 1-second slices so Ctrl+C is responsive.
//         for (int i = 0; i < opts.intervalSeconds && !g_stopRequested; ++i) {
//             struct timespec ts_sleep {1, 0};
//             nanosleep(&ts_sleep, nullptr);
//         }
//     }

//     std::cout << "Shutting down, log flushed to " << csvPath << "\n";
//     cap.release();
//     return 0;
// }

// // Entry point / argument parsing
// static void printUsage(const char *prog) {
//     std::cout << "Usage: " << prog << " [--headless] [--interval SECONDS] "
//               << "[--config PATH] [--logdir PATH] [--baseline-samples N] "
//               << "[--alert-fraction FRACTION] [--alert-cmd PATH]\n\n"
//               << "  (no flags)         Calibration mode: GUI with trackbars.\n"
//               << "                     Press 's' to save thresholds, 'q'/ESC to quit.\n"
//               << "  --headless         Monitoring mode: no GUI, logs to --logdir on a timer.\n"
//               << "  --interval N       Seconds between samples in headless mode (default 300).\n"
//               << "  --config PATH      Threshold config file (default plant_thresholds.cfg).\n"
//               << "  --logdir PATH      Output directory for CSV log + snapshots (default plant_monitor_data).\n"
//               << "  --baseline-samples N   Samples averaged to form the baseline (default 5).\n"
//               << "  --alert-fraction F     Alert if area falls below F * baseline (default 0.70).\n"
//               << "  --alert-cmd PATH       Script/command to run on alert; the alert message is\n"
//               << "                         passed as its one argument (e.g. an email/SMS sender).\n";
// }

// int main(int argc, char *argv[]) {
//     Options opts;
//     for (int i = 1; i < argc; ++i) {
//         std::string arg = argv[i];
//         if (arg == "--headless") {
//             opts.headless = true;
//         } else if (arg == "--interval" && i + 1 < argc) {
//             opts.intervalSeconds = std::atoi(argv[++i]);
//         } else if (arg == "--config" && i + 1 < argc) {
//             opts.configPath = argv[++i];
//         } else if (arg == "--logdir" && i + 1 < argc) {
//             opts.logDir = argv[++i];
//         } else if (arg == "--baseline-samples" && i + 1 < argc) {
//             opts.baselineSamples = std::atoi(argv[++i]);
//         } else if (arg == "--alert-fraction" && i + 1 < argc) {
//             opts.alertDropFraction = std::atof(argv[++i]);
//         } else if (arg == "--alert-cmd" && i + 1 < argc) {
//             opts.alertCmd = argv[++i];
//         } else if (arg == "-h" || arg == "--help") {
//             printUsage(argv[0]);
//             return 0;
//         } else {
//             std::cerr << "Unknown argument: " << arg << "\n";
//             printUsage(argv[0]);
//             return 1;
//         }
//     }

//     return opts.headless ? runHeadless(opts) : runCalibration(opts);
// }
