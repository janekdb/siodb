// Copyright (C) 2019-2020 Siodb GmbH. All rights reserved.
// Use of this source code is governed by a license that can be found
// in the LICENSE file.

#include "Siocli.h"

// Project headers
#include "Client.h"

// Common project headers
#include <siodb/common/config/SiodbDefs.h>
#include <siodb/common/config/SiodbVersion.h>
#include <siodb/common/crypto/TlsClient.h>
#include <siodb/common/io/FdIo.h>
#include <siodb/common/io/FileIO.h>
#include <siodb/common/net/NetConstants.h>
#include <siodb/common/net/TcpConnection.h>
#include <siodb/common/net/UnixConnection.h>
#include <siodb/common/options/DatabaseInstanceSocket.h>
#include <siodb/common/options/InstanceOptions.h>
#include <siodb/common/protobuf/ProtobufMessageIO.h>
#include <siodb/common/sys/Syscalls.h>
#include <siodb/common/utils/CheckOSUser.h>
#include <siodb/common/utils/Debug.h>
#include <siodb/common/utils/FileDescriptorGuard.h>
#include <siodb/common/utils/StartupActions.h>
#include <siodb/common/utils/StringBuilder.h>
#include <siodb/common/utils/SystemError.h>

// Protobuf message headers
#include <siodb/common/proto/ClientProtocol.pb.h>

// CRT headers
#include <cerrno>
#include <cstring>

// STL headers
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

// System headers
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

// Boost headers
#include <boost/algorithm/string.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

namespace {

const std::string kDefaultHost(siodb::kLocalhost);
const std::string kDefaultIdentityFile = siodb::utils::getHomeDir() + "/.ssh/id_rsa";
const char* kFirstLinePrompt = "\033[1msiocli> \033[0m";
const char* kSubsequentLinePrompt = "\033[1m      > \033[0m";
const char* kCommentStart = "--";

}  // namespace

extern "C" int siocliMain(int argc, char** argv)
{
    // Must be called very first!
    siodb::utils::performCommonStartupActions();

    DEBUG_SYSCALLS_LIBRARY_GUARD;

    const bool stdinIsTerminal = ::isatty(::fileno(stdin)) == 1;

    try {
        const auto uid = ::geteuid();
        const auto osUserName = siodb::utils::getOsUserName(uid);

        // Declare options
        boost::program_options::options_description desc("Allowed options");
        desc.add_options()("admin,a",
                boost::program_options::value<std::string>()->default_value(std::string()),
                "Connect to given instance in the admin mode");
        desc.add_options()("host,H",
                boost::program_options::value<std::string>()->default_value(kDefaultHost),
                "Server host name or IP address");
        desc.add_options()("port,p",
                boost::program_options::value<int>()->default_value(
                        siodb::config::kDefaultIpv4PortNumber),
                "Server port");
        desc.add_options()(
                "keep-going,k", "Keep going if stdin is pipe or file and error occurred");
        desc.add_options()("identity-file,i",
                boost::program_options::value<std::string>()->default_value(kDefaultIdentityFile),
                "Identity file (client private key)");
        desc.add_options()("user,u",
                boost::program_options::value<std::string>()->default_value(osUserName),
                "User name");
        desc.add_options()("plaintext,P", "Use plaintext connection");
        desc.add_options()("no-echo,N", "Do not commands if not on the terminal");
        desc.add_options()("help,h", "Produce help message");

        // Parse options
        boost::program_options::variables_map vm;
        boost::program_options::store(
                boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);

        // Handle help options
        if (vm.count("help") > 0) {
            std::cout << desc << std::endl;
            return 0;
        }

        // Handle options
        ClientParameters params;
        params.m_instance = vm["admin"].as<std::string>();
        params.m_host = vm["host"].as<std::string>();
        params.m_port = vm["port"].as<int>();
        params.m_exitOnError = !stdinIsTerminal && vm.count("keep-going") == 0;
        params.m_user = vm["user"].as<std::string>();
        const auto identityFile = vm["identity-file"].as<std::string>();
        params.m_identityKey = loadUserIdentityKey(identityFile.c_str());
        params.m_stdinIsTerminal = stdinIsTerminal;
        params.m_echoCommandsWhenNotOnATerminal = vm.count("no-echo") == 0;

        if (vm.count("plaintext") > 0)
            params.m_encryption = false;
        else {
            // Default simple client connection - secure
            // Default admin connection - non secure
            params.m_encryption = params.m_instance.empty();
        }

        // Print logo
        std::cout << "Siodb client v." << SIODB_VERSION_MAJOR << '.' << SIODB_VERSION_MINOR << '.'
                  << SIODB_VERSION_PATCH << "\nCopyright (C) " << SIODB_COPYRIGHT_YEARS
                  << " Siodb GmbH. All rights reserved." << std::endl;

        // Ignore SIGPIPE
        signal(SIGPIPE, SIG_IGN);

        // Execute command prompt
        return commandPrompt(params);
    } catch (std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '.' << std::endl;
        return 2;
    }
}

namespace {

std::string loadUserIdentityKey(const char* path)
{
    std::string key;
    siodb::FileDescriptorGuard fd(::open(path, O_RDONLY));
    if (!fd.isValidFd()) siodb::utils::throwSystemError("Can't open user identity key");

    struct stat st;
    if (::fstat(fd.getFd(), &st) < 0)
        siodb::utils::throwSystemError("Can't stat user identity key");

    if (static_cast<std::size_t>(st.st_size) > siodb::kMaxAccessKeySize) {
        throw std::runtime_error(siodb::utils::StringBuilder()
                                 << "User identity key is larger than " << siodb::kMaxAccessKeySize
                                 << " bytes got " << st.st_size);
    }

    key.resize(st.st_size);
    if (::readExact(fd.getFd(), key.data(), key.size(), kIgnoreSignals) != key.size())
        siodb::utils::throwSystemError("Can't read user identity key");

    return key;
}

int commandPrompt(const ClientParameters& params)
{
    std::uint64_t requestId = 1;
    bool hasMoreInput = true;
    std::unique_ptr<siodb::io::IoBase> connectionIo;
    std::unique_ptr<siodb::crypto::TlsClient> tlsClient;
    const bool needPrompt = params.m_stdinIsTerminal;

    do {
        try {
            auto singleWordCommand = SingleWordCommandType::kUnknownCommand;
            std::string command;
            {
                std::ostringstream text;
                std::size_t textLength = 0;
                char textLastChar = '\0';

                // Read command text, possibly multiline.
                // Multiline command-text must end with a semicolon.
                std::size_t lineNo = 0;
                if (needPrompt) std::cout << '\n' << kFirstLinePrompt << std::flush;
                do {
                    std::string line;
                    if (!std::getline(std::cin, line)) {
                        hasMoreInput = false;
                        break;
                    }

                    boost::trim(line);
                    if (line.empty()) {
                        if (needPrompt) {
                            std::cout
                                    << (textLength == 0 ? kFirstLinePrompt : kSubsequentLinePrompt)
                                    << std::flush;
                        }
                        continue;
                    }

                    // Do not send comments to siodb
                    if (boost::starts_with(line, kCommentStart)) {
                        if (needPrompt) {
                            std::cout << (textLength ? kFirstLinePrompt : kSubsequentLinePrompt)
                                      << std::flush;
                        }
                        continue;
                    }

                    if (line.back() != ';') {
                        if (needPrompt) std::cout << kSubsequentLinePrompt << std::flush;
                    }

                    if (textLength > 0) {
                        text << '\n';
                        ++textLength;
                    }

                    text << line;
                    textLength += line.length();
                    if (!line.empty()) textLastChar = line.back();

                    ++lineNo;
                    if (lineNo == 1) {
                        auto command1 = boost::to_lower_copy(line);
                        // Remove whitespace before ';' and ';' itself
                        boost::trim_right_if(command1, boost::is_any_of("\t\v\f ;"));
                        singleWordCommand = decodeSingleWordCommand(command1);
                    }
                } while (singleWordCommand == SingleWordCommandType::kUnknownCommand
                         && (textLength == 0 || textLastChar != ';'));
                command = text.str();
            }

            // Maybe echo command
            if (!params.m_stdinIsTerminal && params.m_echoCommandsWhenNotOnATerminal)
                std::cout << '\n' << command << '\n' << std::endl;

            // Handle single-word commands
            switch (singleWordCommand) {
                case SingleWordCommandType::kExit: {
                    std::cout << "Bye" << std::endl;
                    return 0;
                }
                case SingleWordCommandType::kHelp: {
                    std::cout << "Type SQL query with ';' symbol in the end. This query will be "
                                 "sent to the Siodb server.\n"
                                 "Example: SELECT * FROM <table_name>;\n"
                                 "Type exit to stop SioDB client.\n"
                              << std::flush;
                    continue;
                }
                case SingleWordCommandType::kUnknownCommand: break;
            }

            // Connect to server
            if (!connectionIo || !connectionIo->isValid()) {
                if (params.m_instance.empty()) {
                    auto connectionFd = siodb::net::openTcpConnection(params.m_host, params.m_port);
                    std::cout << "Connected to " << params.m_host << ':' << params.m_port
                              << std::endl;

                    if (params.m_encryption) {
                        tlsClient = std::make_unique<siodb::crypto::TlsClient>();
                        // Code below is for the certificate verification.
                        // Uncommenting it will lead to refusing self-signed certificates
                        // tlsClient->enableCertificateVerification();

                        auto tlsConnection = tlsClient->connectToServer(connectionFd);

                        auto x509Certificate = SSL_get_peer_certificate(tlsConnection->getSsl());
                        if (x509Certificate == nullptr)
                            throw siodb::crypto::OpenSslError("SSL_get_peer_certificate failed");

                        connectionIo = std::move(tlsConnection);
                    } else
                        connectionIo = std::make_unique<siodb::io::FdIo>(connectionFd, true);
                } else {
                    // Admin connection is always non-secure
                    const auto instanceSocketPath =
                            siodb::composeInstanceSocketPath(params.m_instance);
                    auto connectionFd = siodb::net::openUnixConnection(instanceSocketPath);
                    std::cout << "Connected to SIODB instance " << params.m_instance << " at "
                              << instanceSocketPath << " in the admin mode." << std::endl;
                    connectionIo = std::make_unique<siodb::io::FdIo>(connectionFd, true);
                }
                authenticate(params.m_identityKey, params.m_user, *connectionIo);
                requestId = 1;
            }

            // Execute command
            if (!command.empty()) {
                executeCommandOnServer(requestId++, std::move(command), *connectionIo, std::cout,
                        params.m_exitOnError);
            }
        } catch (std::exception& ex) {
            std::cerr << "Error: " << ex.what() << '.' << std::endl;
            if (connectionIo && connectionIo->isValid()) {
                connectionIo.reset();
                if (params.m_instance.empty()) {
                    std::cout << "Connection to " << params.m_host << ':' << params.m_port
                              << " closed." << std::endl;
                } else {
                    const auto instanceSocketPath =
                            siodb::composeInstanceSocketPath(params.m_instance);
                    std::cout << "Connection to " << instanceSocketPath << " closed." << std::endl;
                }
            }
            if (params.m_exitOnError) return 3;
        }
    } while (hasMoreInput);
    return 0;
}

SingleWordCommandType decodeSingleWordCommand(const std::string& command) noexcept
{
    if (command == "exit" || command == "quit")
        return SingleWordCommandType::kExit;
    else if (command == "help")
        return SingleWordCommandType::kHelp;
    return SingleWordCommandType::kUnknownCommand;
}

}  // anonymous namespace
