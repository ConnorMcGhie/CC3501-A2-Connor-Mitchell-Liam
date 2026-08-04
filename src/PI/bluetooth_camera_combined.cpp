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
// #include <fcntl.h>
// #include <termios.h>
// #include <sys/select.h>
// #include <unistd.h>
// #include <thread>
// #include <mutex>
// #include <chrono>

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
//     std::string bleMac;              // Pico peripheral MAC address; empty = BLE sensor bridge disabled
//     std::string blePort = "/dev/serial0"; // serial device the RN4871 is attached to
//     int bleStaleSeconds = 0;         // 0 = auto (2x --interval); how old a reading can be before it's logged blank
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

// // Runs an external command/script to deliver an emial alert
// //  The message is passed as a single quoted argument, e.g.:
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

// // BLE sensor bridge - RN4871 in UART central/master mode, connecting to the
// // Pico peripheral that streams "DATA: Moist=..,Temp=..,Humid=..,Light=.."
// class RN4871Master {
// private:
//     int serial_fd;
//     std::string device_path;

// public:
//     RN4871Master(const std::string &port) : serial_fd(-1), device_path(port) {}
//     ~RN4871Master() { close_port(); }

//     bool begin() {
//         serial_fd = open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
//         if (serial_fd == -1) {
//             std::cerr << "BLE: unable to open serial port " << device_path << "\n";
//             return false;
//         }
//         fcntl(serial_fd, F_SETFL, 0);

//         struct termios options;
//         tcgetattr(serial_fd, &options);
//         cfsetispeed(&options, B115200);
//         cfsetospeed(&options, B115200);
//         options.c_cflag |= (CLOCAL | CREAD);
//         options.c_cflag &= ~PARENB;
//         options.c_cflag &= ~CSTOPB;
//         options.c_cflag &= ~CSIZE;
//         options.c_cflag |= CS8;
//         options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
//         options.c_oflag &= ~OPOST;
//         options.c_cc[VMIN] = 0;
//         options.c_cc[VTIME] = 1;

//         tcflush(serial_fd, TCIFLUSH);
//         if (tcsetattr(serial_fd, TCSANOW, &options) != 0) {
//             std::cerr << "BLE: failed to apply termios config.\n";
//             return false;
//         }
//         return true;
//     }

//     void close_port() {
//         if (serial_fd != -1) { close(serial_fd); serial_fd = -1; }
//     }

//     bool send_string(const std::string &data) {
//         if (serial_fd == -1) return false;
//         ssize_t n = write(serial_fd, data.c_str(), data.length());
//         return n == static_cast<ssize_t>(data.length());
//     }

//     // Reads until timeout_seconds passes with no new data.
//     std::string read_response(double timeout_seconds = 1.0) {
//         std::string response;
//         if (serial_fd == -1) return response;
//         char buffer;
//         fd_set set;
//         struct timeval timeout;
//         while (true) {
//             FD_ZERO(&set);
//             FD_SET(serial_fd, &set);
//             timeout.tv_sec = static_cast<long>(timeout_seconds);
//             timeout.tv_usec = static_cast<long>((timeout_seconds - timeout.tv_sec) * 1000000);
//             int rv = select(serial_fd + 1, &set, NULL, NULL, &timeout);
//             if (rv <= 0) break;
//             std::memset(&buffer, 0, sizeof(buffer));
//             ssize_t n = read(serial_fd, &buffer, sizeof(buffer));
//             if (n > 0) {
//                 response.append(&buffer, n);
//                 timeout_seconds = 0.05; // shrink timeout once data is flowing
//             } else break;
//         }
//         return response;
//     }

//     bool enter_command_mode() {
//         tcflush(serial_fd, TCIFLUSH);
//         send_string("r,1\r\n");
//         read_response(0.3);
//         usleep(100000); // RN4871 needs a quiet period around "$$$"
//         if (!send_string("$$$")) return false;
//         usleep(200000);
//         std::string resp = read_response(0.5);
//         return resp.find("CMD>") != std::string::npos;
//     }

//     bool send_command(const std::string &cmd, std::string &out_response) {
//         std::string full_cmd = cmd + "\r\n";
//         tcflush(serial_fd, TCIFLUSH);
//         if (!send_string(full_cmd)) return false;
//         out_response = read_response(0.5);
//         return true;
//     }

//     bool configure_hardware() {
//         std::string response;
//         if (!send_command("SN,PicoMaster", response)) return false;
//         if (!send_command("SS,C0", response)) return false;   // plain UART transparent mode
//         if (!send_command("GR,1", response)) return false;    // GAP role: central/master
//         if (!send_command("R,1", response)) return false;     // reboot to apply
//         usleep(1500000);
//         if (!enter_command_mode()) {
//             std::cerr << "BLE: module did not respond after reboot.\n";
//             return false;
//         }
//         std::cout << "BLE: hardware configured as master.\n";
//         return true;
//     }

//     bool connect_to_slave(const std::string &mac) {
//         std::string response;
//         if (!send_command("C,0," + mac, response)) {
//             std::cerr << "BLE: failed to send connect command.\n";
//             return false;
//         }
//         std::string full_response = response + read_response(2.0);
//         int extra_wait_attempts = 0;
//         while (full_response.find("Trying") != std::string::npos
//                && full_response.find("%CONNECT,0,") == std::string::npos
//                && extra_wait_attempts < 5) {
//             full_response += read_response(1.0);
//             extra_wait_attempts++;
//         }
//         bool got_connect = full_response.find("%CONNECT,0," + mac) != std::string::npos
//                             || full_response.find("%CONNECT,0,") != std::string::npos;
//         if (got_connect) {
//             std::cout << "BLE: connected to " << mac << "\n";
//             return true;
//         }
//         std::cerr << "BLE: failed to connect to " << mac << ". Response: " << full_response << "\n";
//         return false;
//     }
// };

// // Latest parsed sensor reading from the Pico, shared between the BLE thread
// // and the camera/logging thread
// struct SensorReading {
//     float moisturePct = -1.0f;
//     float tempC = -1.0f;
//     float humidityRH = -1.0f;
//     float lux = -1.0f;
//     bool valid = false;
//     std::chrono::steady_clock::time_point receivedAt;
// };

// static std::mutex g_sensorMutex;
// static SensorReading g_latestReading;

// // Formats a sensor field for CSV/console: -1 is the Pico's "this sensor
// // failed to read" sentinel, so it's logged blank rather than as a fake -1.0.
// static std::string formatSensorField(float value) {
//     if (value < 0) return "";
//     std::ostringstream oss;
//     oss << value;
//     return oss.str();
// }

// // Parses a line: DATA: Moist=42.3%, Temp=21.7C, Humid=55.2%, Light=310.5lux
// static bool parseSensorLine(const std::string &line, SensorReading &out) {
//     float moist, temp, humid, lux;
//     int matched = std::sscanf(line.c_str(),
//                                "DATA: Moist=%f%%, Temp=%fC, Humid=%f%%, Light=%flux",
//                                &moist, &temp, &humid, &lux);
//     if (matched != 4) return false;
//     out.moisturePct = moist;
//     out.tempC = temp;
//     out.humidityRH = humid;
//     out.lux = lux;
//     out.valid = true;
//     out.receivedAt = std::chrono::steady_clock::now();
//     return true;
// }

// // Runs on its own thread: connects to the Pico peripheral and continuously
// // updates g_latestReading as new sensor packets arrive. Auto-reconnects on
// // disconnect. Checks g_stopRequested between reads so Ctrl+C stays responsive.
// static void bleReaderThread(std::string mac, std::string port) {
//     RN4871Master ble(port);
//     if (!ble.begin()) {
//         std::cerr << "BLE: could not open " << port << ", sensor logging disabled.\n";
//         return;
//     }
//     if (!ble.enter_command_mode()) {
//         std::cerr << "BLE: failed to sync with module.\n";
//         return;
//     }
//     if (!ble.configure_hardware()) {
//         std::cerr << "BLE: hardware configuration failed.\n";
//         return;
//     }

//     bool connected = false;
//     std::string internal_buffer;

//     while (!g_stopRequested) {
//         if (!connected) {
//             if (ble.connect_to_slave(mac)) {
//                 connected = true;
//             } else {
//                 for (int i = 0; i < 5 && !g_stopRequested; ++i) {
//                     struct timespec ts_sleep{1, 0};
//                     nanosleep(&ts_sleep, nullptr);
//                 }
//                 continue;
//             }
//         }

//         std::string incoming = ble.read_response(0.5);
//         if (!incoming.empty()) {
//             internal_buffer += incoming;

//             if (internal_buffer.find("%DISCONNECT%") != std::string::npos) {
//                 std::cerr << "BLE: connection dropped by Pico, reconnecting.\n";
//                 connected = false;
//                 internal_buffer.clear();
//                 continue;
//             }

//             size_t newline_pos;
//             while ((newline_pos = internal_buffer.find('\n')) != std::string::npos) {
//                 std::string current_line = internal_buffer.substr(0, newline_pos);
//                 internal_buffer.erase(0, newline_pos + 1);
//                 if (!current_line.empty() && current_line.back() == '\r') current_line.pop_back();
//                 if (current_line.empty() || current_line.front() == '%') continue;

//                 SensorReading parsed;
//                 if (parseSensorLine(current_line, parsed)) {
//                     std::lock_guard<std::mutex> lock(g_sensorMutex);
//                     g_latestReading = parsed;
//                 }
//             }
//         }
//     }

//     ble.close_port();
// }

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
//         csv << "timestamp,area_px,centroid_x,centroid_y,snapshot_file,"
//                "soil_moisture_pct,temp_c,humidity_pct,light_lux\n";
//     }

//     std::signal(SIGINT, handleSigint);
//     std::signal(SIGTERM, handleSigint);

//     std::cout << "Headless monitoring started. Logging to " << opts.logDir
//               << " every " << opts.intervalSeconds << "s. Ctrl+C to stop.\n";

//     std::vector<double> baselineReadings;
//     double baselineArea = -1.0;
//     long sampleCount =0;
//     Mat frame, hsv_img;

//     std::thread bleThread;
//     if (!opts.bleMac.empty()) {
//         std::cout << "Starting BLE sensor bridge for " << opts.bleMac
//                   << " on " << opts.blePort << "\n";
//         bleThread = std::thread(bleReaderThread, opts.bleMac, opts.blePort);
//     }

    
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

//             // Pull in the latest BLE sensor reading, if any, and treat it as
//             // stale (logged blank) if it's too old - e.g. the Pico went out
//             // of range or lost power.
//             SensorReading readingSnapshot;
//             bool readingFresh = false;
//             {
//                 std::lock_guard<std::mutex> lock(g_sensorMutex);
//                 readingSnapshot = g_latestReading;
//             }
//             if (readingSnapshot.valid) {
//                 int staleThreshold = opts.bleStaleSeconds > 0
//                                           ? opts.bleStaleSeconds
//                                           : opts.intervalSeconds * 2;
//                 auto ageSeconds = std::chrono::duration_cast<std::chrono::seconds>(
//                                        std::chrono::steady_clock::now() - readingSnapshot.receivedAt)
//                                        .count();
//                 readingFresh = ageSeconds <= staleThreshold;
//             }

//             std::string moistureField = readingFresh ? formatSensorField(readingSnapshot.moisturePct) : "";
//             std::string tempField = readingFresh ? formatSensorField(readingSnapshot.tempC) : "";
//             std::string humidityField = readingFresh ? formatSensorField(readingSnapshot.humidityRH) : "";
//             std::string luxField = readingFresh ? formatSensorField(readingSnapshot.lux) : "";

//             csv << ts << "," << (long)area << "," << cx << "," << cy << ","
//                 << ("snapshots/" + snapshotName) << ","
//                 << moistureField << "," << tempField << "," << humidityField << "," << luxField << "\n";
//             csv.flush();

//             std::cout << "[" << ts << "] area=" << (long)area
//                       << "px centroid=(" << cx << "," << cy << ")";
//             if (readingFresh) {
//                 std::cout << " moisture=" << moistureField << "% temp=" << tempField
//                           << "C humidity=" << humidityField << "% light=" << luxField << "lux";
//             }
//             std::cout << "\n";

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
//     if (bleThread.joinable()) {
//         bleThread.join();
//     }
//     cap.release();
//     return 0;
// }

// // Entry point / argument parsing
// static void printUsage(const char *prog) {
//     std::cout << "Usage: " << prog << " [--headless] [--interval SECONDS] "
//               << "[--config PATH] [--logdir PATH] [--baseline-samples N] "
//               << "[--alert-fraction FRACTION] [--alert-cmd PATH] "
//               << "[--ble-mac MAC] [--ble-port PATH] [--ble-stale-seconds N]\n\n"
//               << "  (no flags)         Calibration mode: GUI with trackbars.\n"
//               << "                     Press 's' to save thresholds, 'q'/ESC to quit.\n"
//               << "  --headless         Monitoring mode: no GUI, logs to --logdir on a timer.\n"
//               << "  --interval N       Seconds between samples in headless mode (default 300).\n"
//               << "  --config PATH      Threshold config file (default plant_thresholds.cfg).\n"
//               << "  --logdir PATH      Output directory for CSV log + snapshots (default plant_monitor_data).\n"
//               << "  --baseline-samples N   Samples averaged to form the baseline (default 5).\n"
//               << "  --alert-fraction F     Alert if area falls below F * baseline (default 0.70).\n"
//               << "  --alert-cmd PATH       Script/command to run on alert; the alert message is\n"
//               << "                         passed as its one argument (e.g. an email/SMS sender).\n"
//               << "  --ble-mac MAC          Pico peripheral MAC address; enables the BLE sensor\n"
//               << "                         bridge (moisture/temp/humidity/light) if set. Disabled\n"
//               << "                         by default.\n"
//               << "  --ble-port PATH        Serial device the RN4871 is on (default /dev/serial0).\n"
//               << "  --ble-stale-seconds N  How old a BLE reading can be before it's logged blank\n"
//               << "                         (default: 2x --interval).\n";
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
//         } else if (arg == "--ble-mac" && i + 1 < argc) {
//             opts.bleMac = argv[++i];
//         } else if (arg == "--ble-port" && i + 1 < argc) {
//             opts.blePort = argv[++i];
//         } else if (arg == "--ble-stale-seconds" && i + 1 < argc) {
//             opts.bleStaleSeconds = std::atoi(argv[++i]);
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
