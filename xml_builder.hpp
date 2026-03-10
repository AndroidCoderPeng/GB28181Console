//
// Created by pengx on 2025/9/26.
//

#ifndef GB28181CONSOLE_XML_BUILDER_HPP
#define GB28181CONSOLE_XML_BUILDER_HPP

#include <string>

#include "logger.hpp"

class XmlBuilder {
public:
    explicit XmlBuilder();

    static XmlBuilder* get() {
        static XmlBuilder instance;
        return &instance;
    }

    XmlBuilder(const XmlBuilder&) = delete;

    XmlBuilder& operator=(const XmlBuilder&) = delete;

    std::string buildDeviceInfo(const std::string& sn,
                                const std::string& device_code,
                                const std::string& device_name,
                                const std::string& serial_number);

    std::string buildCatalog(const std::string& sn,
                             const std::string& device_code,
                             const std::string& server_domain,
                             double longitude,
                             double latitude);

    std::string buildHeartbeat(const std::string& sn, const std::string& device_code);

private:
    Logger _logger;
};


#endif //GB28181CONSOLE_XML_BUILDER_HPP
