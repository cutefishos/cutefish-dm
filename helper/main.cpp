// One login, PAM to session end, outside the threaded daemon (greetd's session
// worker, sddm-helper). stdout carries only replies; the session writes stderr.

#include <security/pam_appl.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <optional>
#include <pwd.h>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "../common/wire.h"

namespace {

constexpr size_t kMaxLine = 64 * 1024;

// After authentication nobody answers; a session module's motd would hang.
bool g_conversing = true;

// The session's pid until it is reaped: unreaped, it cannot belong to anyone else.
volatile sig_atomic_t g_session = 0;

// SIGTERM ends the session, which this process then closes, or the login before one.
void onTerminate(int)
{
    if (g_session > 0)
        kill(pid_t(g_session), SIGTERM);
    else
        _exit(128 + SIGTERM);
}

std::optional<QJsonObject> receive()
{
    std::string line;
    char c = 0;
    ssize_t count = 0;
    while ((count = read(STDIN_FILENO, &c, 1)) == 1 || (count < 0 && errno == EINTR)) {
        if (count < 0)
            continue;
        if (c == '\n')
            break;
        line.push_back(c);
        if (line.size() > kMaxLine)
            return std::nullopt;
    }
    if (line.empty())
        return std::nullopt;
    const auto message = dm::decodeLine(QByteArray::fromStdString(line));
    explicit_bzero(line.data(), line.size());
    return message;
}

void send(const QJsonObject& message)
{
    const QByteArray bytes = dm::encodeLine(message);
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t count = write(STDOUT_FILENO, bytes.constData() + written,
                                    size_t(bytes.size() - written));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return;
        written += count;
    }
}

void fail(bool authentication, const QString& message)
{
    send({{QStringLiteral("type"), QStringLiteral("error")},
          {QStringLiteral("auth"), authentication},
          {QStringLiteral("message"), message}});
}

void freeResponses(pam_response* responses, int count)
{
    for (int i = 0; i < count; ++i) {
        if (responses[i].resp) {
            explicit_bzero(responses[i].resp, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    free(responses);
}

// Every PAM message goes to the greeter and waits for its answer, as in greetd.
int converse(int count, const pam_message** messages, pam_response** result, void*)
{
    if (count <= 0 || count > PAM_MAX_NUM_MSG)
        return PAM_CONV_ERR;
    auto* responses = static_cast<pam_response*>(calloc(size_t(count), sizeof(pam_response)));
    if (!responses)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        const int kind = messages[i]->msg_style;
        if (!g_conversing) {
            if (kind == PAM_PROMPT_ECHO_OFF || kind == PAM_PROMPT_ECHO_ON) {
                freeResponses(responses, count);
                return PAM_CONV_ERR;
            }
            fprintf(stderr, "PAM: %s\n", messages[i]->msg);
            continue;
        }
        QString style;
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF: style = QStringLiteral("secret"); break;
        case PAM_PROMPT_ECHO_ON: style = QStringLiteral("visible"); break;
        case PAM_ERROR_MSG: style = QStringLiteral("error"); break;
        case PAM_TEXT_INFO: style = QStringLiteral("info"); break;
        default:
            freeResponses(responses, count);
            return PAM_CONV_ERR;
        }
        send({{QStringLiteral("type"), QStringLiteral("question")},
              {QStringLiteral("style"), style},
              {QStringLiteral("text"), QString::fromUtf8(messages[i]->msg)}});
        auto answer = receive();
        if (!answer || answer->value(QStringLiteral("type")).toString() != QLatin1String("answer")) {
            freeResponses(responses, count);
            return PAM_CONV_ERR;
        }
        const int prompt = messages[i]->msg_style;
        if (prompt == PAM_PROMPT_ECHO_OFF || prompt == PAM_PROMPT_ECHO_ON) {
            QString text = answer->value(QStringLiteral("response")).toString();
            QByteArray bytes = text.toUtf8();
            responses[i].resp = strdup(bytes.constData());
            dm::wipe(bytes);
            dm::wipe(text);
            if (!responses[i].resp) {
                freeResponses(responses, count);
                return PAM_BUF_ERR;
            }
        }
        (*answer)[QStringLiteral("response")] = QJsonValue();
    }
    *result = responses;
    return PAM_SUCCESS;
}

struct Account {
    std::string name;
    std::string home;
    std::string shell;
    uid_t uid = 0;
    gid_t gid = 0;
};

std::optional<Account> account(pam_handle_t* pam)
{
    // PAM_USER, since a module may have changed it.
    const void* item = nullptr;
    if (pam_get_item(pam, PAM_USER, &item) != PAM_SUCCESS || !item)
        return std::nullopt;
    const passwd* entry = getpwnam(static_cast<const char*>(item));
    if (!entry)
        return std::nullopt;
    return Account {entry->pw_name, entry->pw_dir, entry->pw_shell, entry->pw_uid, entry->pw_gid};
}

std::vector<std::string> strings(const QJsonArray& array)
{
    std::vector<std::string> result;
    for (const auto& value : array)
        result.push_back(value.toString().toStdString());
    return result;
}

} // namespace

int main()
{
    // The daemon's signalfd mask is inherited; without this SIGTERM never lands.
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    struct sigaction action = {};
    action.sa_handler = onTerminate;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    // The daemon's death ends the login like its SIGTERM; no password goes to a core file.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    prctl(PR_SET_DUMPABLE, 0);
    signal(SIGPIPE, SIG_IGN);

    const auto login = receive();
    if (!login || login->value(QStringLiteral("type")).toString() != QLatin1String("login")) {
        fail(false, QStringLiteral("Expected a login request"));
        return 1;
    }
    const std::string service = login->value(QStringLiteral("service")).toString().toStdString();
    const std::string user = login->value(QStringLiteral("user")).toString().toStdString();
    const bool authenticate = login->value(QStringLiteral("authenticate")).toBool();
    const int vt = login->value(QStringLiteral("vt")).toInt();
    if (service.empty() || user.empty() || vt <= 0) {
        fail(false, QStringLiteral("Incomplete login request"));
        return 1;
    }

    const pam_conv conversation {converse, nullptr};
    pam_handle_t* pam = nullptr;
    int status = pam_start(service.c_str(), user.c_str(), &conversation, &pam);
    if (status != PAM_SUCCESS) {
        fail(false, QString::fromUtf8(pam_strerror(pam, status)));
        return 1;
    }
    const auto finish = [&](int result) {
        pam_end(pam, result);
        return 1;
    };

    if (authenticate) {
        status = pam_authenticate(pam, 0);
        if (status != PAM_SUCCESS) {
            fail(true, QString::fromUtf8(pam_strerror(pam, status)));
            return finish(status);
        }
    }
    status = pam_acct_mgmt(pam, 0);
    if (status == PAM_NEW_AUTHTOK_REQD)
        status = pam_chauthtok(pam, PAM_CHANGE_EXPIRED_AUTHTOK);
    if (status != PAM_SUCCESS) {
        fail(true, QString::fromUtf8(pam_strerror(pam, status)));
        return finish(status);
    }
    status = pam_setcred(pam, PAM_ESTABLISH_CRED);
    if (status != PAM_SUCCESS) {
        fail(false, QString::fromUtf8(pam_strerror(pam, status)));
        return finish(status);
    }
    g_conversing = false;
    send({{QStringLiteral("type"), QStringLiteral("authenticated")}});

    const auto request = receive();
    if (!request || request->value(QStringLiteral("type")).toString() != QLatin1String("start")) {
        pam_setcred(pam, PAM_DELETE_CRED);
        pam_end(pam, PAM_SUCCESS);
        return 0;
    }
    const std::vector<std::string> command = strings(request->value(QStringLiteral("cmd")).toArray());
    const auto owner = account(pam);
    if (command.empty() || !owner) {
        fail(false, QStringLiteral("No command or no account to start"));
        pam_setcred(pam, PAM_DELETE_CRED);
        return finish(PAM_SESSION_ERR);
    }

    // pam_systemd takes the seat, VT, type and class from here.
    const std::string tty = "tty" + std::to_string(vt);
    pam_set_item(pam, PAM_TTY, tty.c_str());
    for (const std::string& variable : strings(request->value(QStringLiteral("env")).toArray()))
        pam_putenv(pam, variable.c_str());
    const std::string home = "HOME=" + owner->home;
    const std::string name = "USER=" + owner->name;
    const std::string logname = "LOGNAME=" + owner->name;
    // The session wrapper picks the login shell from it.
    const std::string shell = "SHELL=" + owner->shell;
    pam_putenv(pam, home.c_str());
    pam_putenv(pam, name.c_str());
    pam_putenv(pam, logname.c_str());
    pam_putenv(pam, shell.c_str());
    if (!pam_getenv(pam, "PATH"))
        pam_putenv(pam, "PATH=/usr/local/bin:/usr/bin:/bin");

    status = pam_open_session(pam, 0);
    if (status != PAM_SUCCESS) {
        fail(false, QString::fromUtf8(pam_strerror(pam, status)));
        pam_setcred(pam, PAM_DELETE_CRED);
        return finish(status);
    }
    const char* sessionId = pam_getenv(pam, "XDG_SESSION_ID");
    char** environment = pam_getenvlist(pam);

    std::vector<char*> arguments;
    for (const std::string& argument : command)
        arguments.push_back(const_cast<char*>(argument.c_str()));
    arguments.push_back(nullptr);

    // The session runs in a child; this process stays to close the PAM session.
    const pid_t child = fork();
    if (child == 0) {
        setsid();
        const int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (null >= 0)
            dup2(null, STDIN_FILENO);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        // Not the session's: fds PAM modules left open, the ignored SIGPIPE.
        close_range(3, ~0U, 0);
        signal(SIGPIPE, SIG_DFL);
        if (initgroups(owner->name.c_str(), owner->gid) != 0 || setgid(owner->gid) != 0
            || setuid(owner->uid) != 0)
            _exit(127);
        // setuid resets it.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (chdir(owner->home.c_str()) != 0 && chdir("/") != 0)
            _exit(127);
        execve(arguments.front(), arguments.data(), environment);
        _exit(127);
    }
    if (child < 0) {
        fail(false, QString::fromUtf8(strerror(errno)));
        pam_close_session(pam, 0);
        pam_setcred(pam, PAM_DELETE_CRED);
        return finish(PAM_SESSION_ERR);
    }
    g_session = child;
    send({{QStringLiteral("type"), QStringLiteral("started")},
          {QStringLiteral("pid"), qint64(child)},
          {QStringLiteral("session"), QString::fromUtf8(sessionId ? sessionId : "")}});

    int wait = 0;
    while (waitpid(child, &wait, 0) < 0 && errno == EINTR) {
    }
    // The session is over; closing it is not to be cut short.
    signal(SIGTERM, SIG_IGN);
    // Closing the PAM session needs root.
    pam_close_session(pam, 0);
    pam_setcred(pam, PAM_DELETE_CRED);
    pam_end(pam, PAM_SUCCESS);
    send({{QStringLiteral("type"), QStringLiteral("finished")},
          {QStringLiteral("status"),
           WIFEXITED(wait) ? WEXITSTATUS(wait) : 128 + (WIFSIGNALED(wait) ? WTERMSIG(wait) : 0)}});
    return 0;
}
