#include "core.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        Settings settings;
        settings.piUrl = fromUtf8(argv[2]);
        std::istringstream input(readFile(fromUtf8(argv[1])));
        std::string row;
        while (std::getline(input, row)) {
            if (row.rfind("Token=", 0) == 0) {
                std::string token = row.substr(6);
                while (!token.empty() && (token.back() == '\r' || token.back() == '\n')) token.pop_back();
                settings.token = fromUtf8(token);
                break;
            }
        }
        auto response = piRequest(settings, L"GET", L"/v1/status");
        auto json = mini::parse(response.body);
        if (json.type != mini::Json::Object || json.get("app_version").is_null()) return 1;
        std::cout << "Pi status API OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
