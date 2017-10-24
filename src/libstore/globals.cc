#include "globals.hh"
#include "util.hh"
#include "archive.hh"
#include "args.hh"

#include <algorithm>
#include <map>
#include <thread>


namespace nix {


/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"

/* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES "/System/Library /usr/lib /dev /bin/sh"
#else
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES ""
#endif

Settings settings;

Settings::Settings()
    : Config({})
    , nixPrefix(NIX_PREFIX)
    , nixStore(canonPath(getEnv("NIX_STORE_DIR", getEnv("NIX_STORE", NIX_STORE_DIR))))
    , nixDataDir(canonPath(getEnv("NIX_DATA_DIR", NIX_DATA_DIR)))
    , nixLogDir(canonPath(getEnv("NIX_LOG_DIR", NIX_LOG_DIR)))
    , nixStateDir(canonPath(getEnv("NIX_STATE_DIR", NIX_STATE_DIR)))
    , nixConfDir(canonPath(getEnv("NIX_CONF_DIR", NIX_CONF_DIR)))
    , nixLibexecDir(canonPath(getEnv("NIX_LIBEXEC_DIR", NIX_LIBEXEC_DIR)))
    , nixBinDir(canonPath(getEnv("NIX_BIN_DIR", NIX_BIN_DIR)))
    , nixDaemonSocketFile(canonPath(nixStateDir + DEFAULT_SOCKET_PATH))
{
    buildUsersGroup = getuid() == 0 ? "nixbld" : "";
    lockCPU = getEnv("NIX_AFFINITY_HACK", "1") == "1";

    caFile = getEnv("NIX_SSL_CERT_FILE", getEnv("SSL_CERT_FILE", ""));
    if (caFile == "") {
        for (auto & fn : {"/etc/ssl/certs/ca-certificates.crt", "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
            if (pathExists(fn)) {
                caFile = fn;
                break;
            }
    }

    /* Backwards compatibility. */
    auto s = getEnv("NIX_REMOTE_SYSTEMS");
    if (s != "") {
        Strings ss;
        for (auto & p : tokenizeString<Strings>(s, ":"))
            ss.push_back("@" + p);
        builders = concatStringsSep(" ", ss);
    }

#if defined(__linux__) && defined(SANDBOX_SHELL)
    sandboxPaths = tokenizeString<StringSet>("/bin/sh=" SANDBOX_SHELL);
#endif

    allowedImpureHostPrefixes = tokenizeString<StringSet>(DEFAULT_ALLOWED_IMPURE_PREFIXES);
}

void Settings::loadConfFile()
{
    applyConfigFile(nixConfDir + "/nix.conf");

    /* We only want to send overrides to the daemon, i.e. stuff from
       ~/.nix/nix.conf or the command line. */
    resetOverriden();

    applyConfigFile(getConfigDir() + "/nix/nix.conf");
}

void Settings::set(const string & name, const string & value)
{
    Config::set(name, value);
}

unsigned int Settings::getDefaultCores()
{
    return std::max(1U, std::thread::hardware_concurrency());
}

const string nixVersion = PACKAGE_VERSION;

template<> void BaseSetting<SandboxMode>::set(const std::string & str)
{
    if (str == "true") value = smEnabled;
    else if (str == "relaxed") value = smRelaxed;
    else if (str == "false") value = smDisabled;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<SandboxMode>::to_string()
{
    if (value == smEnabled) return "true";
    else if (value == smRelaxed) return "relaxed";
    else if (value == smDisabled) return "false";
    else abort();
}

template<> void BaseSetting<SandboxMode>::toJSON(JSONPlaceholder & out)
{
    AbstractSetting::toJSON(out);
}

template<> void BaseSetting<SandboxMode>::convertToArg(Args & args, const std::string & category)
{
    args.mkFlag()
        .longName(name)
        .description("Enable sandboxing.")
        .handler([=](std::vector<std::string> ss) { value = smEnabled; })
        .category(category);
    args.mkFlag()
        .longName("no-" + name)
        .description("Disable sandboxing.")
        .handler([=](std::vector<std::string> ss) { value = smDisabled; })
        .category(category);
    args.mkFlag()
        .longName("relaxed-" + name)
        .description("Enable sandboxing, but allow builds to disable it.")
        .handler([=](std::vector<std::string> ss) { value = smRelaxed; })
        .category(category);
}

void MaxBuildJobsSetting::set(const std::string & str)
{
    if (str == "auto") value = std::max(1U, std::thread::hardware_concurrency());
    else if (!string2Int(str, value))
        throw UsageError("configuration setting '%s' should be 'auto' or an integer", name);
}

}
