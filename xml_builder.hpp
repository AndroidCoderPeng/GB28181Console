//
// Created by pengx on 2025/9/26.
//

#ifndef GB28181_XML_BUILDER_HPP
#define GB28181_XML_BUILDER_HPP

#include <string>

class XmlBuilder {
public:
    static std::string buildDeviceInfo(const std::string& sn,
                                       const std::string& device_code,
                                       const std::string& device_name,
                                       const std::string& serial_number);

    static std::string buildCatalog(const std::string& sn,
                                    const std::string& device_code,
                                    const std::string& server_domain,
                                    double longitude,
                                    double latitude);

    static std::string buildHeartbeat(const std::string& sn, const std::string& device_code);
};


#endif //GB28181_XML_BUILDER_HPP
