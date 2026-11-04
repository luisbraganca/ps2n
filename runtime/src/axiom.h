// Sends a run's log to Axiom (axiom.co), a log service, so that a developer
// sees what happened in a tester's runs without asking the tester for files.
//
// Off unless the settings file has an [Axiom] section with a Token (an API
// token that may ingest into the dataset), a Dataset, and a Domain (the
// dataset's edge deployment, such as eu-central-1.aws.edge.axiom.co), and
// Enabled is not false. Runs that read no settings file (headless runs
// without --settings, which the tests are) never send.
//
// What goes: a start event (the game, the runtime's build, the OS, the CPU,
// the command line), every log line at info level and above with the vblank
// it came at, the vblank rate every ten seconds, the exit summary's counts,
// and an end event. The user's home folder reads "~" in all of them. The
// events go in batches from a thread of their own, through the curl program
// (part of Windows since 2018 and of most Linux systems): every ten seconds,
// within a second of a warning, and at exit, which waits for the last batch.
#pragma once

#include "log.h"
#include "settings.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2 {
struct GameInfo;
}

namespace ps2::axiom {

struct Config {
    std::string token, dataset, domain;
    bool enabled = false;
    // Why a section that is there is not used (a key missing, a value that
    // cannot go in a request); empty when it is used or there is none.
    std::string problem;
};
Config readConfig(const settings::Ini& ini);

// One request's result.
struct Delivery {
    enum class Outcome {
        Sent,     // the service has the batch (`message`: events it refused, if any)
        Retry,    // not delivered (no network, the service busy): send it again later
        GiveUp,   // the service refuses requests like it (a wrong token, region or dataset)
    } outcome = Outcome::Sent;
    std::string message;
};
// Sends one batch, NDJSON. The default runs curl.
using Transport = std::function<Delivery(const std::string& body)>;

// Starts sending this run's log (nothing when !cfg.enabled). `args`: the
// command line, `build`: the runtime's commit.
void start(const Config& cfg, const GameInfo& game, const std::vector<std::string>& args, std::string_view build,
           Transport transport = {});
bool active();
// Every line logf prints at info level or above (any thread, under the log's
// lock). The sender's own lines (category "axiom") stay local.
void onLog(LogLevel level, const char* category, std::string_view message);
// Ends the session: the exit summary's counts and the end event, then waits
// for the last request.
void finish(int exitCode, const std::vector<std::pair<std::string, uint64_t>>& issues);

// The pieces, for the tests.
std::string jsonString(std::string_view s);   // a JSON string literal; bytes that are not UTF-8 become U+FFFD
// `s` with `home` (where a path starts and ends there or at a separator) as "~".
std::string withoutHome(std::string_view s, std::string_view home);
std::string isoTime(int64_t unixMillis);   // 2026-11-04T13:22:01.123Z
// One argument of a Windows command line, as the C runtime splits them.
std::string quoteForWindows(std::string_view arg);
std::string ingestUrl(const Config& cfg);
// curl's arguments for one request: headers from `headers` (the token is
// there, not on the command line), the body from `body`.
std::vector<std::string> curlArguments(const Config& cfg, const std::string& headers, const std::string& body,
                                       const std::string& response, const std::string& curlLog);
// What the service answered: `status` the HTTP status, `body` its reply.
Delivery judge(int status, std::string_view body);
// How often the sender sends and samples the vblank rate (10 s each).
void setIntervalsForTests(std::chrono::milliseconds send, std::chrono::milliseconds speed);

} // namespace ps2::axiom
